#pragma once
#include <cstdint>
#include <memory>
namespace rex::input { class InputDriver; }
namespace android_game {
std::unique_ptr<rex::input::InputDriver> CreateTouchInputDriver();
void SetTouchButtons(uint16_t buttons);
void ResetTouchButtons();
}
