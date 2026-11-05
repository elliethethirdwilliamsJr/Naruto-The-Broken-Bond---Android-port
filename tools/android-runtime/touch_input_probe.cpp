#include "touch_input.h"
#include <rex/input/input_system.h>
#include <rex/ui/virtual_key.h>
#include <rex/logging.h>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace rex;
  using namespace rex::input;
  InitLoggingEarly();
  auto input = CreateDefaultInputSystem(true);
  input->AddDriver(android_game::CreateTouchInputDriver());
  unsigned checks = 0;
  auto require = [&](bool ok) { if (!ok) throw std::runtime_error("Touch input regression failed"); ++checks; };
  auto poll = [&] { X_INPUT_STATE s{}; require(input->GetState(0, &s) == X_ERROR_SUCCESS); return s; };
  android_game::ResetTouchButtons();
  auto initial = poll(); require(initial.gamepad.buttons == 0);
  auto stable = poll(); require(initial.packet_number == stable.packet_number);
  X_INPUT_CAPABILITIES caps{};
  require(input->GetCapabilities(0, 0, &caps) == X_ERROR_SUCCESS && caps.type == 1 && (caps.gamepad.buttons & X_INPUT_GAMEPAD_START));
  android_game::SetTouchButtons(X_INPUT_GAMEPAD_START);
  android_game::SetTouchButtons(0);  // A tap shorter than one slow game frame.
  auto tap = poll(); require(tap.gamepad.buttons == X_INPUT_GAMEPAD_START);
  require(tap.packet_number != initial.packet_number);
  require(poll().gamepad.buttons == 0);
  X_INPUT_KEYSTROKE key{};
  require(input->GetKeystroke(0, 0, &key) == X_ERROR_SUCCESS && key.virtual_key == uint16_t(ui::VirtualKey::kXInputPadStart) && key.flags == X_INPUT_KEYSTROKE_KEYDOWN);
  require(input->GetKeystroke(0, 0, &key) == X_ERROR_SUCCESS && key.flags == X_INPUT_KEYSTROKE_KEYUP);
  android_game::SetTouchButtons(X_INPUT_GAMEPAD_DPAD_DOWN | X_INPUT_GAMEPAD_A);
  auto chord = poll(); require(chord.gamepad.buttons == (X_INPUT_GAMEPAD_DPAD_DOWN | X_INPUT_GAMEPAD_A));
  require(poll().packet_number == chord.packet_number);
  android_game::SetTouchButtons(X_INPUT_GAMEPAD_A);
  require(poll().gamepad.buttons == X_INPUT_GAMEPAD_A);
  android_game::ResetTouchButtons(); require(poll().gamepad.buttons == 0);
  require(input->GetKeystroke(0, 0, &key) == X_ERROR_EMPTY);
  require(input->GetState(1, &initial) == X_ERROR_DEVICE_NOT_CONNECTED);
  input->Shutdown();
  std::cout << "{\"passed\":true,\"checks\":" << checks << ",\"guest_user\":0}" << std::endl;
}
