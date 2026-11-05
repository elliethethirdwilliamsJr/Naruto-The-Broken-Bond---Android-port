// Exercise the real SDK wait handles and the game's renderer shutdown path.
#include <rex/thread.h>
#include <rex/logging.h>
#include "gpu/backend/dispatcher.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  rex::InitLoggingEarly();
  rex::thread::AndroidInitialize();
  using namespace rex::thread;
  using namespace std::chrono_literals;
  unsigned checks = 0;
  auto check = [&](bool ok) { if (ok) ++checks; return ok; };
  for (unsigned iteration = 0; iteration < 16; ++iteration) {
    auto release = Event::CreateManualResetEvent(false);
    auto worker = Thread::Create({}, [&] { Wait(release.get(), false); });
    if (!worker) return 2;
    std::atomic<unsigned> successful{0};
    std::vector<std::thread> waiters;
    for (unsigned i = 0; i < 4; ++i) waiters.emplace_back([&] {
      if (Wait(worker.get(), false, 5s) == WaitResult::kSuccess) ++successful;
    });
    release->Set();
    for (auto& waiter : waiters) waiter.join();
    if (!check(successful == 4)) return 3;
    for (unsigned i = 0; i < 4; ++i)
      if (!check(Wait(worker.get(), false, 0ms) == WaitResult::kSuccess)) return 4;
    WaitHandle* handles[]{worker.get(), release.get()};
    if (!check(WaitAll(handles, 2, false, 0ms) == WaitResult::kSuccess)) return 5;
    auto any = WaitAny(handles, 2, false, 0ms);
    if (!check(any.first == WaitResult::kSuccess && any.second == 0)) return 6;
  }
  std::cout << "{\"phase\":\"repeated-and-concurrent-thread-waits\",\"passed\":true,\"checks\":" << checks << "}" << std::endl;
  for (bool deferred : {false, true}) {
    gpu::Dispatcher dispatcher(true, deferred);
    gpu::RenderMode mode; mode.api = gpu::GraphicsApi::kVulkan;
    std::string evidence;
    if (!dispatcher.Initialize(evidence, mode)) { std::cerr << evidence; return 7; }
    std::atomic<unsigned> work{0};
    if (!dispatcher.StartPool(8, 2, [&](size_t, gpu::Engine&) { ++work; }, [](bool) {})) return 8;
    if (!dispatcher.Run([](gpu::Engine&, std::string&) { return true; }, evidence)) return 9;
    if (!dispatcher.Shutdown(evidence)) { std::cerr << evidence; return 10; }
    std::cout << "{\"phase\":\"dispatcher-shutdown\",\"deferred\":" << deferred << ",\"evidence\":" << evidence << "}" << std::endl;
    if (!dispatcher.Shutdown(evidence)) return 11;
    // Destructor calls Shutdown a third time, after real pipeline workers ran.
  }
  std::cout << "{\"phase\":\"complete\",\"passed\":true}" << std::endl;
}
