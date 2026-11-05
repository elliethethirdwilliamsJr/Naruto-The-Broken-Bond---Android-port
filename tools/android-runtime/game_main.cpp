// Original-game Android development host, entered by SDLActivity in :game.
#include "guest_image.h"
#include "touch_input.h"
#include <rex/input/input_system.h>
#include "game_image.h"
#include "crash_report.h"
#include "gpu/host/app.h"
#include "gpu/common/platform.h"
#include "gpu/shaders/pipelines.h"
#include "gpu/d3d/frame.h"
#include "gpu/d3d/present.h"
#include "gpu/host/services.h"
#include "gpu/common/shader_activity.h"
#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/thread.h>
#include <rex/ui/windowed_app_context_sdl.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <dlfcn.h>
#include <thread>
#include <chrono>

extern "C" void sub_8219FEB8(PPCContext&, uint8_t*);
extern "C" void sub_821B8D78(PPCContext&, uint8_t*);
extern "C" void sub_821A5498(PPCContext&, uint8_t*);

REXCVAR_DEFINE_UINT32(android_boot_seconds, 0, "broken_bond/Diagnostics",
    "Bound an Android diagnostic boot; zero runs until the game is closed").range(0, 600);
REXCVAR_DEFINE_UINT32(android_capture_period, 0, "broken_bond/Diagnostics",
    "Sample Android presentation and progress without a boot time limit").range(0, 3600);
REXCVAR_DEFINE_BOOL(android_fast_pipeline_compile, false, "broken_bond/Diagnostics",
    "Test Android driver compilation with pipeline optimization disabled");

namespace {
std::filesystem::path session_root;
struct ProgressMonitor {
  std::atomic<bool> stopped{false};
  std::thread thread;
  ~ProgressMonitor() {
    stopped.store(true);
    if (thread.joinable()) thread.join();
  }
};
void Checkpoint(const char* phase) {
  std::ofstream out(session_root / "boot-checkpoints.jsonl", std::ios::app);
  out << "{\"phase\":\"" << phase << "\"}\n";
  REXLOG_INFO("[android-game] {}", phase);
}
PPCFunc* original_entry = nullptr;
void OriginalEntryWitness(PPCContext& context, uint8_t* base) {
  Checkpoint("original-entry-executed");
  original_entry(context, base);
  Checkpoint("original-entry-returned");
}
class AndroidGameApp final : public rex::ReXApp {
 public:
  explicit AndroidGameApp(rex::ui::WindowedAppContext& context)
      : rex::ReXApp(context, "broken_bond_android", *broken_bond_GetImageInfo()) {}
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto identity = broken_bond::game_image::Identify(paths.game_data_root);
    if (identity.status != broken_bond::game_image::Status::kReady)
      throw std::runtime_error(std::string("Unsupported or incomplete original game: ") +
                               broken_bond::game_image::StatusName(identity.status));
    Checkpoint("original-game-revision-and-archives-verified");
    paths.config_path = session_root / "game.toml";
    paths.log_dir = session_root;
    std::filesystem::create_directories(paths.user_data_root);
    std::filesystem::create_directories(paths.cache_root);
    setenv("REX_ANDROID_PIPELINE_CACHE", (paths.cache_root / "vulkan-driver-cache.bin").c_str(), 1);
    gpu::SetPlayerPipelineFolder(paths.cache_root / "pipelines");
  }
  void OnPreSetup(rex::RuntimeConfig& config) override {
    Checkpoint("configure-native-frontend");
    gpu::host::Configure(config);
    config.input_factory = [](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
      auto input = rex::input::CreateDefaultInputSystem(tool_mode);
      if (!tool_mode) input->AddDriver(android_game::CreateTouchInputDriver());
      return input;
    };
  }
  void OnPostLoadXexImage() override {
    gpu::host::ApplyAspectRatio(runtime()->virtual_membase());
    Checkpoint("original-xex-loaded");
  }
  void OnPostSetup() override {
    broken_bond::crash_report::Install();
    // Shared original-code reuse is valid only if its weak function mappings
    // bind the real Android frontend hooks rather than the legacy GPU bodies.
    const std::pair<uint32_t, PPCFunc*> hooks[]{
        {0x8219FEB8, sub_8219FEB8}, {0x821B8D78, sub_821B8D78}, {0x821A5498, sub_821A5498}};
    for (const auto& [address, expected] : hooks) {
      const auto actual = runtime()->function_dispatcher()->GetFunction(address);
      Dl_info module{};
      if (actual != expected || !dladdr(reinterpret_cast<void*>(actual), &module) ||
          std::filesystem::path(module.dli_fname).filename() != "libbroken_bond_game.so")
        throw std::runtime_error("Original function table did not bind the Android native hooks");
    }
    Checkpoint("native-hook-binding-verified");
    Checkpoint("runtime-setup-complete");
  }
  void OnPostLaunchModule(rex::system::XThread*) override {
    Checkpoint("original-main-thread-created-before-resume");
  }
  void OnPreLaunchModule() override {
    const auto module = runtime()->kernel_state()->GetExecutableModule();
    if (!module) throw std::runtime_error("Original executable module is missing");
    const auto entry = module->entry_point();
    auto* dispatcher = runtime()->function_dispatcher();
    original_entry = dispatcher->GetFunction(entry);
    if (!original_entry || original_entry == OriginalEntryWitness ||
        !dispatcher->SetFunction(entry, OriginalEntryWitness))
      throw std::runtime_error("Could not install the original-entry execution witness");
    std::ofstream(session_root / "entry-address.txt") << std::hex << entry << '\n';
    Checkpoint("original-entry-witness-installed");
  }
  bool OnWindowCloseRequested() override { return gpu::host::CloseRequested(); }
};
}  // namespace

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
  try {
    // Java creates this writable session folder before starting the SDL thread.
    // The first custom argument is removed before the SDK cvar parser runs.
    if (argc < 2 || !argv[1] || !*argv[1]) throw std::runtime_error("Android session path is missing");
    session_root = argv[1];
    rex::InitLoggingEarly();
    rex::memory::AndroidInitialize();
    rex::thread::AndroidInitialize();
    Checkpoint("sdl-main-entered");
    std::vector<char*> options{argv[0]};
    for (int i = 2; i < argc; ++i) options.push_back(argv[i]);
    rex::cvar::Init(int(options.size()), options.data());
    rex::cvar::ApplyEnvironment();
    const auto seconds = REXCVAR_GET(android_boot_seconds);
    const auto capture_period = REXCVAR_GET(android_capture_period);
    if (REXCVAR_GET(android_fast_pipeline_compile)) setenv("REX_ANDROID_PIPELINE_NO_OPTIMIZE", "1", 1);
    setenv("REX_NATIVE_WATCHDOG_SECONDS", std::to_string(seconds).c_str(), 1);
    if (seconds) {
      setenv("REX_EXPORT_TRACE", (session_root / "exports.jsonl").c_str(), 1);
      const auto frames = session_root / "frames";
      std::filesystem::create_directories(frames);
      setenv("REX_NATIVE_FRAME_DIR", frames.c_str(), 1);
      setenv("REX_NATIVE_TRANSITION_CAPTURES", "120", 1);
      setenv("REX_ANDROID_PRESENT_CAPTURE_PERIOD", "120", 1);
      setenv("REX_NATIVE_FRAME_TRACE", "30", 1);
    }
    if (capture_period) {
      setenv("REX_NATIVE_PIPELINE_TRACE", (session_root / "pipelines.jsonl").c_str(), 1);
      const auto frames = session_root / "frames";
      std::filesystem::create_directories(frames);
      setenv("REX_NATIVE_FRAME_DIR", frames.c_str(), 1);
      setenv("REX_NATIVE_TRANSITION_CAPTURES", std::to_string(capture_period).c_str(), 1);
      setenv("REX_ANDROID_PRESENT_CAPTURE_PERIOD", std::to_string(capture_period).c_str(), 1);
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    rex::ui::SDLWindowedAppContext context;
    if (!context.Initialize()) throw std::runtime_error("SDL window context initialization failed");
    Checkpoint("sdl-context-ready");
    std::unique_ptr<rex::ui::WindowedApp> app = std::make_unique<AndroidGameApp>(context);
    app->SetParsedArguments({});
    if (!app->OnInitialize()) { Checkpoint("application-initialize-failed"); return 3; }
    Checkpoint("application-initialized");
    ProgressMonitor progress;
    if (capture_period) progress.thread = std::thread([&progress] {
      std::ofstream out(session_root / "progress.jsonl");
      out.setf(std::ios::unitbuf);
      unsigned seconds = 0;
      while (!progress.stopped.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (++seconds % 5) continue;
        out << "{\"seconds\":" << seconds << ",\"swaps\":" << gpu::swaps.load()
            << ",\"compiling\":" << gpu::shader_jobs.load()
            << ",\"present\":" << gpu::PresentSummary()
            << ",\"completion\":" << gpu::host::InterruptSummary() << "}\n";
      }
    });
    const int result = context.RunMainMessageLoop();
    app->InvokeOnDestroy();
    Checkpoint("message-loop-returned");
    return result;
  } catch (const std::exception& error) {
    REXLOG_ERROR("[android-game] startup failed: {}", error.what());
    if (!session_root.empty()) {
      std::ofstream out(session_root / "startup-error.txt"); out << error.what() << '\n';
      Checkpoint("host-exception");
    }
    return 2;
  }
}
