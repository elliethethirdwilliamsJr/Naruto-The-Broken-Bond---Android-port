#include "touch_input.h"
#include "gpu/common/shader_activity.h"
#include <rex/input/input_driver.h>
#include <rex/ui/virtual_key.h>
#include <rex/logging.h>
#include <jni.h>
#include <deque>
#include <mutex>
#include <cstring>

namespace android_game {
namespace {
using namespace rex;
using namespace rex::input;
constexpr DeviceId kTouch = static_cast<DeviceId>(0x544F554348ULL);
std::mutex mutex;
uint16_t held = 0, pending = 0, published = 0;
uint32_t packet = 1;
std::deque<X_INPUT_KEYSTROKE> keys;
using Key = rex::ui::VirtualKey;
const std::pair<uint16_t, Key> mapping[]{
    {X_INPUT_GAMEPAD_START, Key::kXInputPadStart}, {X_INPUT_GAMEPAD_BACK, Key::kXInputPadBack},
    {X_INPUT_GAMEPAD_A, Key::kXInputPadA}, {X_INPUT_GAMEPAD_B, Key::kXInputPadB},
    {X_INPUT_GAMEPAD_X, Key::kXInputPadX}, {X_INPUT_GAMEPAD_Y, Key::kXInputPadY},
    {X_INPUT_GAMEPAD_DPAD_UP, Key::kXInputPadDpadUp}, {X_INPUT_GAMEPAD_DPAD_DOWN, Key::kXInputPadDpadDown},
    {X_INPUT_GAMEPAD_DPAD_LEFT, Key::kXInputPadDpadLeft}, {X_INPUT_GAMEPAD_DPAD_RIGHT, Key::kXInputPadDpadRight},
    {X_INPUT_GAMEPAD_LEFT_SHOULDER, Key::kXInputPadLShoulder},
    {X_INPUT_GAMEPAD_RIGHT_SHOULDER, Key::kXInputPadRShoulder}};

class TouchInput final : public InputDriver {
 public:
  TouchInput() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info; info.id = kTouch; info.name = "Android touch controller";
    info.key = "android-touch"; info.synthetic = true;
    out.push_back(std::move(info));
  }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* state) override {
    if (id != kTouch || !is_active()) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex);
    const uint16_t buttons = held | pending;
    pending = 0;  // Even a brief tap is observed at least once by the game.
    if (buttons != published) {
      published = buttons; ++packet;
      REXLOG_INFO("[android-touch] guest state buttons=0x{:04X}, packet={}", buttons, packet);
    }
    *state = {}; state->packet_number = packet; state->gamepad.buttons = buttons;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* caps) override {
    if (id != kTouch) return X_ERROR_DEVICE_NOT_CONNECTED;
    *caps = {}; caps->type = 1; caps->sub_type = 1;
    caps->gamepad.buttons = 0xF33F;
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    return id == kTouch ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE* key) override {
    if (id != kTouch || !is_active()) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex);
    if (keys.empty()) return X_ERROR_EMPTY;
    *key = keys.front(); keys.pop_front(); return X_ERROR_SUCCESS;
  }
};
}
void SetTouchButtons(uint16_t buttons) {
  std::lock_guard lock(mutex);
  const uint16_t changed = held ^ buttons;
  pending |= buttons & ~held;
  for (const auto& [mask, key] : mapping) if (changed & mask) {
    X_INPUT_KEYSTROKE event{}; event.virtual_key = uint16_t(key);
    event.flags = (buttons & mask) ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
    if (keys.size() == 64) keys.pop_front();
    keys.push_back(event);
  }
  held = buttons;
}
void ResetTouchButtons() {
  std::lock_guard lock(mutex);
  held = pending = 0; keys.clear();
}
std::unique_ptr<rex::input::InputDriver> CreateTouchInputDriver() { return std::make_unique<TouchInput>(); }
}
extern "C" JNIEXPORT void JNICALL
Java_org_brokenbond_nativecheck_GameActivity_setTouchButtons(JNIEnv*, jclass, jint buttons) {
  android_game::SetTouchButtons(uint16_t(buttons));
}
extern "C" JNIEXPORT void JNICALL
Java_org_brokenbond_nativecheck_GameActivity_resetTouchButtons(JNIEnv*, jclass) {
  android_game::ResetTouchButtons();
}
extern "C" JNIEXPORT jint JNICALL
Java_org_brokenbond_nativecheck_GameActivity_pendingGraphicsJobs(JNIEnv*, jclass) {
  return jint(gpu::shader_jobs.load());
}
