/**
 * @file main.cpp
 * @brief debounced button input to trigger output (LED) with timed lockout
 *
 * A debounced button press starts a timed "playback" output which runs for
 * PLAYBACK_DURATION_MS. For now the status LED stands in for the real trigger
 * outputs (BrightSign and/or sound module). Presses during playback are
 * ignored (no BUSY line implemented here) and a held button triggeres only
 * once: it has to be released and pressed again (rearm).
 *
 * Every pass of the main loop follows the same order, take in the current
 * time and raw button reading, pass it to the debouncer, and run the state
 * machine based on the event returned.
 */
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"
#include "tusb.h"

#include "debouncer.h"
#include "key_tap.h"

// ---------- CONFIGURATION -------------------------
constexpr uint8_t STATUS_LED_PIN = 4; // on the Pro Micro, GPIO25 drives a
                                      // WS2812, use an external LED on this pin
constexpr uint8_t BUTTON_PIN =
    26; // Buttons are ACTIVE LOW, external pull-up
        // circuit is assumed here (pin silkscreen label is A0)

constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t PLAYBACK_DURATION_MS = 5000;

constexpr uint8_t USB_RHPORT = 0; // the RP2040 has only one USB port, number 0
// --------------------------------------------------

// ---------- HARDWARE ------------------------------
static void init_pins() {
  gpio_init(STATUS_LED_PIN);
  gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);

  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  // the RP2040 starts every pin with its internal pull-down enabled, and
  // gpio_init() leaves it that way, turn it off here so it doesn't fight
  // our external pull-up circuit:
  gpio_disable_pulls(BUTTON_PIN);
}

static void set_led(bool on) { gpio_put(STATUS_LED_PIN, on); }

// the button reads 0 (LOW) when pressed, inverting it here allows the
// rest of the code to treat TRUE as "pressed."
static bool button_is_pressed() { return !gpio_get(BUTTON_PIN); }

static uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// key sent to the BrightSign on each press
// HID_KEY_A comes from TinyUSB (other keys follow the same pattern, e.g.
// HID_KEY_1 or HID_KEY_SPACE or HID_KEY_B and so on)
// set up BrightSign presentation and event to match whichever key you pick here
constexpr uint8_t TRIGGER_KEY = HID_KEY_A;
// --------------------------------------------------

// ---------- USB CALLBACKS -------------------------
// TinyUSB calls these by name from inside tud_task(). They must NOT be `static`
// or else TinyUSB can't find them.

// the computer accepted us as a keyboard
void tud_mount_cb(void) { printf("USB mounted\n"); }

// the computer disconnected us
void tud_umount_cb(void) { printf("USB unmounted\n"); }

// the computer asks for our current key state; we don't need to answer
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqLen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqLen;
  return 0;
}

// the computer sends us data, like "Caps Lock is on"; we have no lock LEDs
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

enum class PlaybackState : uint8_t { IDLE, PLAYING };

int main(void) {
  stdio_init_all(); // start UART for printf()

  init_pins();

  // init device stack on configured roothub port
  const tusb_rhport_init_t usb_settings = {.role = TUSB_ROLE_DEVICE,
                                           .speed = TUSB_SPEED_FULL};
  if (!tusb_init(USB_RHPORT, &usb_settings)) {
    printf("USB failed to start\n");
  }

  PlaybackState state = PlaybackState::IDLE;
  uint32_t playback_started_at_ms = 0;

  Debouncer button(DEBOUNCE_MS);
  button.reset(button_is_pressed(), now_ms());

  KeyTap key_tap;

  while (true) {
    tud_task();       // let TinyUSB handle anything the computer asked for
    key_tap.update(); // sends the next half of a tap (if one is waiting)

    // read the time once on every loop, so every decision in this pass uses
    // the same value
    const uint32_t now = now_ms();

    // update the debouncer on every pass, in every state (see below)
    const bool raw = button_is_pressed();
    const bool press_event = button.update(raw, now);

    switch (state) {
    case PlaybackState::IDLE:
      if (press_event) {
        state = PlaybackState::PLAYING;
        playback_started_at_ms = now;
        set_led(true);
        // request a tap (the first half of one, i.e. the press) when playback
        // begins here:
        if (!key_tap.request(TRIGGER_KEY)) {
          printf("USB not ready, key not sent\n");
          // notice how a board that isn't connected to a computer still runs
          // the LED playback, but no key tap goes through and we log why
        }
        printf("button pressed! starting playback\n");
      }
      break;
    case PlaybackState::PLAYING:
      // call update only for logging, presses are ignored and not acted upon
      if (press_event) {
        printf("button pressed during playback - IGNORED\n");
      }
      if (now - playback_started_at_ms > PLAYBACK_DURATION_MS) {
        state = PlaybackState::IDLE;
        set_led(false);
        printf("playback ended\n");
      }
      break;
    }
  }
}
