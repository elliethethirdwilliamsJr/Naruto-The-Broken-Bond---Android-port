// Exercise the exact Android SDK fiber backend without starting the game.
#include <rex/thread/fiber.h>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <signal.h>

using rex::thread::Fiber;
namespace {
constexpr unsigned kRounds = 5000;
constexpr uint64_t kMainVector = 0x123456789abcdef0ULL;
constexpr uint64_t kChildVector = 0xfedcba9876543210ULL;
struct State { Fiber* main; unsigned completed = 0; bool done = false; };

void Require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "fiber probe: %s\n", message); std::exit(1); }
}
void SetMask(int blocked, int unblocked) {
  sigset_t mask;
  Require(pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0, "read signal mask");
  sigaddset(&mask, blocked);
  sigdelset(&mask, unblocked);
  Require(pthread_sigmask(SIG_SETMASK, &mask, nullptr) == 0, "write signal mask");
}
void CheckMask(int blocked, int unblocked) {
  sigset_t mask;
  Require(pthread_sigmask(SIG_SETMASK, nullptr, &mask) == 0, "query signal mask");
  Require(sigismember(&mask, blocked) == 1 && sigismember(&mask, unblocked) == 0,
          "fiber signal mask was not restored");
}
void Nested(State& state, unsigned depth) {
  volatile uint64_t stack_marker = 0xabc00000ULL + depth;
  if (depth) {
    Nested(state, depth - 1);
  } else {
    uint64_t vector = kChildVector;
    asm volatile("fmov d8, %0" : : "r"(vector) : "v8");
    for (unsigned i = 0; i < kRounds; ++i) {
      Require(std::fegetround() == FE_UPWARD, "child rounding mode");
      Require((std::fetestexcept(FE_ALL_EXCEPT) & (FE_INVALID | FE_DIVBYZERO)) == FE_DIVBYZERO,
              "child floating-point exception flags");
      CheckMask(SIGUSR2, SIGUSR1);
      uint64_t observed;
      asm volatile("fmov %0, d8" : "=r"(observed));
      Require(observed == kChildVector, "child callee-saved SIMD register");
      ++state.completed;
      Fiber::SwitchTo(state.main);
    }
  }
  Require(stack_marker == 0xabc00000ULL + depth, "nested stack was corrupted");
}
void Entry(void* argument) {
  auto& state = *static_cast<State*>(argument);
  Require(std::fegetround() == FE_DOWNWARD, "new fiber did not inherit creator state");
  CheckMask(SIGUSR1, SIGUSR2);
  std::feclearexcept(FE_ALL_EXCEPT);
  Require(std::fesetround(FE_UPWARD) == 0, "set child rounding");
  std::feraiseexcept(FE_DIVBYZERO);
  SetMask(SIGUSR2, SIGUSR1);
  Nested(state, 32);
  state.done = true;
  Fiber::SwitchTo(state.main);
  std::abort();
}
}  // namespace

int main() {
  std::fenv_t original_environment;
  sigset_t original_mask;
  std::fegetenv(&original_environment);
  pthread_sigmask(SIG_SETMASK, nullptr, &original_mask);
  std::feclearexcept(FE_ALL_EXCEPT);
  Require(std::fesetround(FE_DOWNWARD) == 0, "set main rounding");
  std::feraiseexcept(FE_INVALID);
  SetMask(SIGUSR1, SIGUSR2);
  State state{Fiber::ConvertCurrentThread()};
  Require(state.main != nullptr, "convert main thread");
  Fiber* child = Fiber::Create(256 * 1024, Entry, &state);
  Require(child != nullptr, "create child fiber");
  uint64_t vector = kMainVector;
  asm volatile("fmov d8, %0" : : "r"(vector) : "v8");
  uintptr_t platform_before;
  asm volatile("mov %0, x18" : "=r"(platform_before));
  while (!state.done) {
    Fiber::SwitchTo(child);
    Require(Fiber::Current() == state.main, "current fiber TLS");
    Require(std::fegetround() == FE_DOWNWARD, "main rounding mode");
    Require((std::fetestexcept(FE_ALL_EXCEPT) & (FE_INVALID | FE_DIVBYZERO)) == FE_INVALID,
            "main floating-point exception flags");
    CheckMask(SIGUSR1, SIGUSR2);
    uint64_t observed;
    uintptr_t platform_after;
    asm volatile("fmov %0, d8" : "=r"(observed));
    asm volatile("mov %0, x18" : "=r"(platform_after));
    Require(observed == kMainVector, "main callee-saved SIMD register");
    Require(platform_before == platform_after, "Android platform register x18");
  }
  Require(state.completed == kRounds, "round trip count");
  child->Destroy();
  state.main->Destroy();
  Require(Fiber::Current() == nullptr, "destroy thread fiber");
  std::fesetenv(&original_environment);
  pthread_sigmask(SIG_SETMASK, &original_mask, nullptr);
  std::printf("{\"fiber_round_trips\":%u,\"nested_stack_depth\":32,\"state_checks\":\"passed\"}\n", state.completed);
}
