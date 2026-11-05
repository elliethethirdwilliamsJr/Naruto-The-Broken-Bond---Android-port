// Load the real SDK runtime and inspect the original XEX in analysis mode.
// No guest entry point, video surface, input backend or audio backend is started.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/thread.h>
#include <rex/platform/fpscr.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef BROKEN_BOND_GUEST_PROBE
#include "guest_image.h"
#include "guest_checks.h"
#endif

namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

uint64_t CheckProfiles() {
  using rex::input::mnk::Profile;
  using rex::input::mnk::GetProfile;
  using rex::input::mnk::SetProfile;
  std::atomic<bool> deleted{false};
  SetProfile(std::shared_ptr<const Profile>(new Profile(), [&](const Profile* value) {
    auto current = GetProfile();  // Reenter after the publication lock is released.
    deleted.store(true);
    delete value;
  }));
  SetProfile(nullptr);
  Require(deleted.load(), "reentrant profile deletion");
  auto initial = std::make_shared<Profile>();
  initial->enabled = true;
  initial->user = 0;
  initial->sensitivity = 0;
  initial->bindings[0] = "0";
  SetProfile(initial);
  std::atomic<unsigned> ready{0};
  std::atomic<bool> stop{false}, broken{false};
  std::atomic<uint64_t> reads{0};
  auto reader = [&] {
    bool announced = false;
    while (!stop.load(std::memory_order_acquire)) {
      auto profile = GetProfile();
      if (!profile || !profile->enabled || profile->sensitivity != double(profile->user) ||
          profile->bindings[0] != std::to_string(profile->user)) broken.store(true);
      ++reads;
      if (!announced) { ++ready; announced = true; }
    }
  };
  std::thread first(reader), second(reader);
  while (ready.load() != 2) std::this_thread::yield();
  for (unsigned i = 1; i <= 1000; ++i) {
    auto profile = std::make_shared<Profile>();
    profile->enabled = true;
    profile->user = i;
    profile->sensitivity = double(i);
    profile->bindings[0] = std::to_string(i);
    SetProfile(std::move(profile));
  }
  stop.store(true, std::memory_order_release);
  first.join();
  second.join();
  SetProfile(nullptr);
  Require(!broken.load() && reads.load() >= 2, "concurrent input profile snapshots");
  return reads.load();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "usage: android_runtime_probe <private test directory>");
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    std::filesystem::create_directories(root / "cache");
    { std::ofstream file(root / "game/MixedCase.bin", std::ios::binary); file << "native-runtime-probe"; }
    rex::InitLoggingEarly();
    rex::memory::AndroidInitialize();
    rex::thread::AndroidInitialize();
    const uint32_t saved_control = rex::platform::FPSCRPlatform::getcsr();
    for (unsigned mode = 0; mode < 4; ++mode) {
      const uint32_t value = (saved_control & ~uint32_t(rex::platform::FPSCRPlatform::RoundMaskVal)) |
                            (mode << rex::platform::FPSCRPlatform::RoundShift);
      rex::platform::FPSCRPlatform::setcsr(value);
      Require(rex::platform::FPSCRPlatform::getcsr() == value, "ARM FPCR round trip");
    }
    rex::platform::FPSCRPlatform::setcsr(saved_control);
    const uint64_t profile_reads = CheckProfiles();
    uint32_t title_id = 0;
#ifdef BROKEN_BOND_GUEST_PROBE
    GuestChecks guest_checks{};
#endif
    {
      rex::Runtime runtime(root / "game", root / "user", {}, root / "cache");
#ifdef BROKEN_BOND_GUEST_PROBE
      Require(XSUCCEEDED(runtime.Setup(*broken_bond_GetImageInfo(), rex::RuntimeConfig{
#else
      Require(XSUCCEEDED(runtime.Setup(rex::RuntimeConfig{
#endif
          .kernel_init = rex::kernel::InitializeKernel, .tool_mode = true})), "runtime setup");
      uint32_t address = runtime.memory()->SystemHeapAlloc(4096);
      Require(address != 0, "guest heap allocation");
      auto* bytes = runtime.memory()->TranslateVirtual<uint8_t*>(address);
      for (unsigned i = 0; i < 4096; ++i) bytes[i] = uint8_t(i ^ (i >> 4));
      for (unsigned i = 0; i < 4096; ++i)
        Require(bytes[i] == uint8_t(i ^ (i >> 4)), "guest memory round trip");
      runtime.memory()->SystemHeapFree(address);
      auto* entry = runtime.file_system()->ResolvePath("game:\\mixedcase.bin");
      Require(entry && entry->size() == 20, "case-insensitive guest VFS lookup");
      Require(XSUCCEEDED(runtime.LoadXexImage("game:\\default.xex")), "original XEX image load");
      title_id = runtime.kernel_state()->title_id();
      Require(title_id == 0x55530825, "unexpected game title ID");
#ifdef BROKEN_BOND_GUEST_PROBE
      guest_checks = CheckOriginalGuestCode(runtime);
#endif
      runtime.Shutdown();
    }
    rex::thread::AndroidShutdown();
    rex::memory::AndroidShutdown();
#ifdef BROKEN_BOND_GUEST_PROBE
    std::ofstream result(root / "guest-result.json");
#else
    std::ofstream result(root / "runtime-result.json");
#endif
    result << "{\"runtime_setup\":true,\"guest_memory_bytes_checked\":4096,"
           << "\"case_insensitive_vfs\":true,\"profile_publications\":1000,\"profile_reads\":"
           << profile_reads << ",\"fpcr_modes_checked\":4,\"xex_loaded\":true,"
           << "\"title_id\":\"55530825\",\"guest_entry_executed\":false,\"graphics_started\":false";
#ifdef BROKEN_BOND_GUEST_PROBE
    result << ",\"registered_functions_checked\":" << guest_checks.registered_functions
           << ",\"original_routines_executed\":" << guest_checks.original_routines_executed
           << ",\"original_routine_cases\":" << guest_checks.routine_cases
           << ",\"guest_threads_completed\":" << guest_checks.guest_threads_completed;
#endif
    result << "}\n";
    result.close();
    Require(bool(result), "write diagnostic result");
    std::cout << "Runtime and original XEX analysis checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "runtime probe: " << error.what() << '\n';
    return 1;
  }
}
