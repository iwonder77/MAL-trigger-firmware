/**
 * @file main.cpp
 * @brief Guest button -> BrightSign key press + sound module trigger, with
 *        presses locked out while the sound plays.
 *
 * A debounced button press starts one playback:
 *  - a USB keyboard tap (TRIGGER_KEY) tells the BrightSign LS5 to play its
 *    video
 *  - SOUND_TRIGGER_PIN starts the DY-HV20T sound. It goes through a BC547, so
 *    GPIO HIGH = the module's trigger pin LOW. The trigger is held until the
 *    module confirms it started (BUSY goes LOW): a handshake, not a fixed pulse
 *  - the firmware switches the LED on LED_TRIGGER_PIN on and off together with
 *    SOUND_TRIGGER_PIN, to visualize the trigger pulse
 *
 * Every exhibit pairs the BrightSign with the sound module, so the module's
 * BUSY line decides when a playback is over:
 *  - STARTING: trigger held until BUSY goes LOW, or BUSY_START_TIMEOUT_MS
 *  - PLAYING:  until BUSY goes HIGH again, or AUDIO_MAX_DURATION_MS
 *  - COOLDOWN: a short COOLDOWN_PERIOD_MS before the button rearms
 * Presses in any of these states are logged and ignored. Every wait on the
 * module has a time limit, so a missing or faulty module can't lock up the
 * exhibit. A held button triggers only once: it has to be released and
 * pressed again.
 *
 * Every pass of the main loop runs in the same order and never waits: service
 * USB (tud_task, key tap), read the clock and button, debounce, then run the
 * state machine on the result.
 */
#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"
#include "tusb.h"

#include "debouncer.h"
#include "key_tap.h"

// ---------- CONFIGURATION -------------------------
constexpr uint8_t LED_TRIGGER_PIN =
    2; // external LED used to visualize trigger pulses
constexpr uint8_t SOUND_TRIGGER_PIN =
    3; // drives base current through BJT to pull DY-HV20T's TRIG0 pin LOW
constexpr uint8_t SOUND_BUSY_PIN = 4; // on the DY-HV20T, this pin is HIGH when
                                      // idle and LOW when a sound is playing

constexpr uint8_t BUTTON_PIN =
    26; // Buttons are ACTIVE LOW, external pull-up
        // circuit is assumed here (pin silkscreen label is A0)

constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t BUSY_START_TIMEOUT_MS = 1000;
constexpr uint32_t AUDIO_MAX_DURATION_MS = 7000;
constexpr uint32_t COOLDOWN_PERIOD_MS = 100;

constexpr uint8_t USB_RHPORT = 0; // the RP2040 has only one USB port, number 0

// key sent to the BrightSign on each press
// HID_KEY_A comes from TinyUSB (other keys follow the same pattern, e.g.
// HID_KEY_1 or HID_KEY_SPACE or HID_KEY_B and so on)
// set up BrightSign presentation and event to match whichever key you pick here
constexpr uint8_t TRIGGER_KEY = HID_KEY_A;

// --------------------------------------------------

// ---------- HARDWARE ------------------------------
static void init_pins() {
  gpio_init(LED_TRIGGER_PIN);
  gpio_set_dir(LED_TRIGGER_PIN, GPIO_OUT);
  gpio_init(SOUND_TRIGGER_PIN);
  gpio_set_dir(SOUND_TRIGGER_PIN, GPIO_OUT);

  gpio_init(SOUND_BUSY_PIN);
  gpio_set_dir(SOUND_BUSY_PIN, GPIO_IN);
  // deliberately set this pin with weak internal pull down
  // in case sound module is mis-wired or unpowered or missing, the BUSY line
  // will always read low (that'll just lock presses out for
  // AUDIO_MAX_DURATION_MS)
  gpio_pull_down(SOUND_BUSY_PIN);

  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  // the RP2040 starts every pin with its internal pull-down enabled, and
  // gpio_init() leaves it that way, turn it off here so it doesn't fight
  // our external pull-up circuit:
  gpio_disable_pulls(BUTTON_PIN);
}

static void set_led(bool on) { gpio_put(LED_TRIGGER_PIN, on); }
static void trigger_sound(bool on) { gpio_put(SOUND_TRIGGER_PIN, on); }

// the button reads 0 (LOW) when pressed, inverting it here allows the
// rest of the code to treat TRUE as "pressed."
static bool button_is_pressed() { return !gpio_get(BUTTON_PIN); }

// similar to the button func above, sound module reads LOW when BUSY
static bool is_busy() { return !gpio_get(SOUND_BUSY_PIN); }

static uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }
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

enum class PlaybackState : uint8_t { IDLE, STARTING, PLAYING, COOLDOWN };

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
  uint32_t playback_ended_at_ms = 0;

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
        state = PlaybackState::STARTING;
        playback_started_at_ms = now;
        set_led(true);
        trigger_sound(true);
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

    case PlaybackState::STARTING:
      // consume press events only for logging, do nothing with them here
      if (press_event) {
        printf("button pressed during playback - IGNORED\n");
      }
      if (is_busy()) {
        // end trigger lines' pulse once module asserts as BUSY
        state = PlaybackState::PLAYING;
        set_led(false);
        trigger_sound(false);
      }
      // safety timeout: if playback doesn't begin for this long, log it,
      // end the pulses and force an exit
      if (now - playback_started_at_ms > BUSY_START_TIMEOUT_MS) {
        printf("WARNING: playback never began, forcing COOLDOWN\n");
        state = PlaybackState::COOLDOWN;
        set_led(false);
        trigger_sound(false);
        playback_ended_at_ms = now;
        break;
      }
      break;
    case PlaybackState::PLAYING:
      // consume press events only for logging, do nothing with them here
      if (press_event) {
        printf("button pressed during playback - IGNORED\n");
      }
      // safety timeout: if playback exceeds maximum duration, force an exit
      if (now - playback_started_at_ms > AUDIO_MAX_DURATION_MS) {
        printf("WARNING: BUSY line timed out, forcing COOLDOWN\n");
        state = PlaybackState::COOLDOWN;
        playback_ended_at_ms = now;
        break;
      }
      if (!is_busy()) {
        // only move on to brief cooldown period until playback ends, remember
        // to take timestamp too
        state = PlaybackState::COOLDOWN;
        printf("playback ended\n");
        playback_ended_at_ms = now;
      }
      break;
    case PlaybackState::COOLDOWN:
      if (now - playback_ended_at_ms >= COOLDOWN_PERIOD_MS) {
        state = PlaybackState::IDLE;
        printf("cooldown period ended\n");
      }
      break;
    }
  }
}
