/**
 * @file key_tap.h
 * @brief Sends one key "tap" (press, then release) over USB without waiting
 *
 * A tap is two keyboard messages
 * (1) key is down
 * (2) nothing is down
 * The board can only hold one message at a time until the computer picks it up
 * (within ~10ms), so update() sends each one when the endpoint is free
 */
#pragma once

#include <cstdint>

#include "tusb.h"

class KeyTap {
public:
  /**
   * @brief Ask for one tap of `keycode` (e.g. HID_KEY_A).
   * @return false if it can't be sent: not connected to a computer or a tap is
   * already in progress
   */
  bool request(uint8_t keycode) {
    if (!tud_mounted() || step_ != Step::IDLE) {
      return false;
    }
    keycode_ = keycode;
    step_ = Step::SEND_PRESS;
    return true;
  }

  /**
   * @brief Moves the tap along. Call once per pass of the main loop.
   */
  void update() {
    if (!tud_mounted()) {
      // unplugged mid-tap: forget it, the computer has already let go
      step_ = Step::IDLE;
      return;
    }
    if (step_ == Step::IDLE || !tud_hid_ready()) {
      // nothing to send, or the endpoint is still full: try again next pass
      return;
    }
    if (step_ == Step::SEND_PRESS) {
      uint8_t keys[6] = {keycode_, 0, 0, 0, 0, 0}; // up to 6 keys; we use 1
      if (tud_hid_keyboard_report(
              0, 0, keys)) { // no report ID (no Shift/Ctrl/... key)
        step_ = Step::SEND_RELEASE;
      }
    } else {
      // SEND_RELEASE
      if (tud_hid_keyboard_report(0, 0, nullptr)) {
        // nothing held
        step_ = Step::IDLE;
      }
    }
    return;
  }

private:
  enum class Step : uint8_t { IDLE, SEND_PRESS, SEND_RELEASE };
  Step step_ = Step::IDLE;
  uint8_t keycode_ = 0;
};
