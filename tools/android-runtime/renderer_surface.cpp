// JNI presentation diagnostic using the production engine and full mobile cache.
// Original guest entry is deliberately not part of this development diagnostic.
#include "gpu/backend/engine.h"
#include "surface_format_checks.h"
#include <android/native_window_jni.h>
#include <jni.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_org_brokenbond_nativecheck_RendererActivity_runRenderer(
    JNIEnv* env, jclass, jobject surface, jstring output_path, jobject cancellation) {
  const char* text = env->GetStringUTFChars(output_path, nullptr);
  if (!text) return nullptr;
  const std::string path(text);
  env->ReleaseStringUTFChars(output_path, text);
  std::ofstream output(path);
  output.setf(std::ios::unitbuf);
  if (!output) return env->NewStringUTF("Could not open renderer evidence file.");
  const jclass cancel_class = env->GetObjectClass(cancellation);
  const jmethodID get_cancel = env->GetMethodID(cancel_class, "get", "()Z");
  if (!get_cancel) return nullptr;
  auto cancelled = [&] { return env->CallBooleanMethod(cancellation, get_cancel) == JNI_TRUE; };
  bool passed = true;
  std::string last_failure;
  auto check = [&](const char* kind, bool ok, const std::string& evidence) {
    passed &= ok;
    if (!ok) last_failure = std::string(kind) + ": " + evidence;
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false")
           << ",\"evidence\":" << (evidence.empty() ? "null" : evidence) << "}\n";
    return ok;
  };
  check("surface-format-policy", CheckSurfaceFormatPolicy(), "{\"cases\":8}");
  std::unique_ptr<ANativeWindow, decltype(&ANativeWindow_release)> native(
      ANativeWindow_fromSurface(env, surface), ANativeWindow_release);
  if (!native) return env->NewStringUTF("The Android surface is no longer available.");
  rex::InitLoggingEarly();
  gpu::Engine engine;
  gpu::RenderMode mode; mode.api = gpu::GraphicsApi::kVulkan;
  std::string evidence;
  bool attached = false;
  unsigned frames = 0, captures = 0;
  if (!cancelled() && check("initialize", engine.Initialize(evidence, mode), evidence)) {
    gpu::platform::NativeWindow window;
    window.kind = gpu::platform::NativeWindow::Kind::kAndroid;
    window.window = native.get();
    attached = engine.AttachWindow(window, evidence);
    if (!cancelled() && check("attach", attached, evidence) &&
        check("surface", engine.CreateSurface({0xA001, 64, 48}, evidence), evidence)) {
      gpu::ClearRequest clear;
      clear.color_keys[0] = 0xA001; clear.clear_color = true;
      uint64_t fence{};
      clear.color = {1, 0, 0, 1};
      bool ready = check("clear-red", engine.ClearSurfaces(clear, fence, evidence), evidence);
      clear.color = {0, 1, 0, 1}; clear.left = 32; clear.top = 24; clear.right = 64; clear.bottom = 48;
      ready &= check("clear-green-quadrant", engine.ClearSurfaces(clear, fence, evidence), evidence);
      gpu::UiFrame ui; ui.width = 64; ui.height = 48;
      ui.vertices = {{0, 0, 0, 0, 0xffff0000}, {8, 0, 1, 0, 0xffff0000},
                     {8, 8, 1, 1, 0xffff0000}, {0, 8, 0, 1, 0xffff0000}};
      ui.indices = {0, 1, 2, 0, 2, 3};
      gpu::UiDraw draw; draw.index_count = 6; ui.draws = {draw};
      ready &= check("ui-frame", engine.SetUiFrame(std::move(ui), evidence), evidence);
      for (unsigned frame = 0; ready && frame < 60 && !cancelled(); ++frame) {
        uint32_t slot{};
        if (!check("stage", engine.StageFront(0xA001, slot, evidence), evidence)) break;
        std::vector<uint8_t> pixels;
        uint32_t width{}, height{};
        bool presented = false;
        const bool capture = frame == 0 || frame == 59;
        const bool ok = engine.Present(slot, capture, pixels, width, height, evidence, &presented);
        if (cancelled()) break;
        if (!check("present", ok && presented, evidence)) break;
        ++frames;
        if (capture) {
          bool correct = width && height && pixels.size() == size_t(width) * height * 4;
          // Interior samples avoid scaling edges. The window can have letterbox bars.
          const double scale = std::min(double(width) / 64, double(height) / 48);
          const double ox = (width - 64 * scale) / 2, oy = (height - 48 * scale) / 2;
          for (unsigned y : {12u, 36u}) for (unsigned x : {16u, 48u}) {
            if (!correct) continue;
            const auto px = uint32_t(ox + x * scale), py = uint32_t(oy + y * scale);
            const auto* p = pixels.data() + (size_t(py) * width + px) * 4;
            const bool green = x == 48 && y == 36;
            correct &= p[0] == 0 && p[1] == (green ? 255 : 0) &&
                       p[2] == (green ? 0 : 255) && p[3] == 255;
          }
          if (correct) {
            const auto* blue = pixels.data() + (size_t(height / 16) * width + width / 16) * 4;
            correct &= blue[0] == 255 && blue[1] == 0 && blue[2] == 0 && blue[3] == 255;
          }
          if (!check("captured-colors-and-orientation", correct, "{}")) break;
          ++captures;
        }
      }
    }
    check("release", engine.ReleaseRemaining(evidence), evidence);
    if (attached) check("detach", engine.DetachWindow(evidence), evidence);
    check("shutdown", engine.Shutdown(evidence), evidence);
  }
  const bool stopped = cancelled();
  passed &= !stopped && frames == 60 && captures == 2;
  output << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false")
         << ",\"cancelled\":" << (stopped ? "true" : "false") << ",\"presented_frames\":" << frames
         << ",\"verified_captures\":" << captures << ",\"guest_entry_executed\":false}\n";
  const std::string result = stopped ? "Renderer test stopped." : passed ?
      "PASSED: 60 Vulkan frames presented; two captures verified. This is a renderer diagnostic, not game footage." :
      "Renderer test failed: " + last_failure;
  return env->NewStringUTF(result.c_str());
}
