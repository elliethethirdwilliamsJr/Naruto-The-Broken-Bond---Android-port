#include "guest_checks.h"
#include "guest_image.h"
#include <rex/runtime.h>
#include <rex/ppc/context.h>
#include <rex/system/xthread.h>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(base + address, &value, sizeof(value));
}
}  // namespace

GuestChecks CheckOriginalGuestCode(rex::Runtime& runtime) {
  const auto* image = broken_bond_GetImageInfo();
  Require(image && image->func_mappings, "missing original game descriptor");
  auto* dispatcher = runtime.function_dispatcher();
  std::unordered_set<uint32_t> addresses;
  constexpr uint32_t expected_count = 32765;
  for (uint32_t i = 0; i < expected_count; ++i) {
    const auto& mapping = image->func_mappings[i];
    Require(mapping.guest >= image->code_base &&
            mapping.guest < uint64_t(image->code_base) + image->code_size &&
            (mapping.guest & 3) == 0 && mapping.host,
            "invalid original game function mapping");
    const auto guest = uint32_t(mapping.guest);
    Require(addresses.insert(guest).second, "duplicate original game function address");
    Require(dispatcher->GetFunction(guest) == mapping.host,
            "original game function missing from runtime dispatcher");
  }
  Require(image->func_mappings[expected_count].guest == 0,
          "unexpected original game function count");
  std::cerr << "Verified " << expected_count << " original function registrations.\n";

  uint8_t* base = runtime.virtual_membase();
  const uint32_t allocation = runtime.memory()->SystemHeapAlloc(4096);
  Require(allocation != 0 && (allocation & 15) == 0, "guest routine test allocation");
  struct Allocation {
    rex::memory::Memory* memory;
    uint32_t address;
    ~Allocation() { memory->SystemHeapFree(address); }
  } cleanup{runtime.memory(), allocation};
  const auto saved_fpcr = rex::platform::FPSCRPlatform::getcsr();
  struct FpcrRestore {
    uint32_t saved;
    ~FpcrRestore() { rex::platform::FPSCRPlatform::setcsr(saved); }
  } fpcr{saved_fpcr};
  uint32_t cases = 0;
  auto call = [&](uint32_t address, PPCContext& context) {
    auto* routine = dispatcher->GetFunction(address);
    Require(routine != nullptr, "missing original routine");
    // These audited leaf routines need no guest thread, kernel or renderer.
    routine(context, base);
    ++cases;
  };

  PPCContext context{};
  call(0x82148680, context);
  Require(context.r3.u32 == 0x820E0D78, "original constant-address routine");
  call(0x8214A270, context);
  Require(context.r3.u32 == 0x10003, "original integer routine");
  std::cerr << "Original integer routine checks passed.\n";

  // Original routine 827E95B0 compares XY squared distance against the sum
  // of radii, then checks the Z separation. Exercise both branches and equality.
  const uint32_t first = allocation + 256, second = allocation + 272;
  const float positions[][3] = {{3, 4, 1}, {3, 4, 4}, {6, 0, 0}, {0, 0, 0}};
  const uint32_t expected[] = {1, 0, 0, 1};
  for (unsigned i = 0; i < 4; ++i) {
    for (unsigned axis = 0; axis < 3; ++axis) {
      Store32(base, first + axis * 4, std::bit_cast<uint32_t>(positions[i][axis]));
      Store32(base, second + axis * 4, 0);
    }
    context = {};
    context.r3.u32 = first;
    context.r6.u32 = second;
    context.f1.f64 = 2;
    context.f3.f64 = 3;
    context.f2.f64 = 1;
    context.f4.f64 = 2;
    call(0x827E95B0, context);
    Require(context.r3.u32 == expected[i], "original float/distance routine");
  }
  std::cerr << "Original floating-point distance checks passed.\n";

  // Original VMX128 packing/unpacking routines. Guest vector lanes are reversed
  // in the host register representation. Check bytes independently of the
  // inverse routine so matching endian mistakes cannot hide in a round trip.
  const uint32_t table = allocation + 512, output = allocation + 768;
  const uint32_t shifts[] = {0, 1, 2, 4};
  const uint32_t values[] = {0x25, 0x33, 0x12, 0x0D};
  context = {};
  context.r1.u32 = allocation + 2048;
  context.r3.u32 = output;
  context.r4.u32 = table;
  for (unsigned lane = 0; lane < 4; ++lane) {
    Store32(base, table + 12 + lane * 4, lane * 3);
    Store32(base, table + 28 + lane * 4, 0xFF);
    Store32(base, table + 44 + lane * 4, shifts[lane]);
    context.v1.u32[3 - lane] = values[lane];
    base[output + lane * 3] = 0;
  }
  call(0x824B0018, context);
  for (unsigned lane = 0; lane < 4; ++lane)
    Require(base[output + lane * 3] == uint8_t(values[lane] << shifts[lane]),
            "original VMX packing bytes");
  context = {};
  context.r1.u32 = allocation + 2048;
  context.r3.u32 = output;
  context.r4.u32 = table;
  call(0x824AFDF0, context);
  for (unsigned lane = 0; lane < 4; ++lane)
    Require(context.v1.u32[3 - lane] == values[lane], "original VMX unpacked lanes");
  std::cerr << "Original VMX packing/unpacking checks passed.\n";

  // Exercise the real SDK's guest-thread setup, PCR/TLS, suspended start,
  // original code dispatch, implicit exit and host wait/cleanup. The audited
  // constant routine is the only original code reached by these raw threads.
  for (unsigned i = 0; i < 3; ++i) {
    auto thread = rex::system::object_ref<rex::system::XThread>(new rex::system::XThread(
        runtime.kernel_state(), 256 * 1024, 0, 0x8214A270, 0,
        rex::system::X_CREATE_SUSPENDED, true));
    Require(XSUCCEEDED(thread->Create()), "original-code guest thread creation");
    Require(thread->pcr_ptr() && thread->tls_ptr() &&
            thread->thread_state()->context()->r13.u32 == thread->pcr_ptr(),
            "guest thread PCR/TLS setup");
    uint32_t suspended = 0;
    Require(XSUCCEEDED(thread->Resume(&suspended)) && suspended == 1,
            "guest thread suspended start");
    Require(rex::thread::Wait(thread->thread(), false, std::chrono::seconds(10)) ==
            rex::thread::WaitResult::kSuccess, "original-code guest thread completion");
    const auto* guest_thread = thread->guest_object<rex::system::X_KTHREAD>();
    Require(uint32_t(guest_thread->exit_status) == 0x10003 && guest_thread->terminated,
            "original-code guest thread exit status");
    std::cerr << "Original-code guest thread " << i + 1 << " completed.\n";
  }

  return {expected_count, 5, cases, 3};
}
