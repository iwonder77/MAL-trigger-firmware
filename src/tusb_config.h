/**
 * This file is TinyUSB's compile-time configuration, TinyUSB has no settings of
 * its own; it reads this file (must be created by us).
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// compile the **device** half of TinyUSB (tud_* functions): we are the device,
// the BrightSign is the host. The host half (tuh_*) stays off by default
#define CFG_TUD_ENABLED 1

// controls how many HID interfaces TinyUSB sizes (1 keyboard)
#define CFG_TUD_HID 1

// each keyboard message is exactly 8 bytes, so that's all the buffer space
// we need:
// 1 modifier byte (Shift/Ctrl/...),
// 1 reserved byte
// 6 keycodes
#define CFG_TUD_HID_EP_BUFSIZE 8

// use the standard size of the setup chatter that happens when the board is
// plugged in
#define CFG_TUD_ENDPOINT0_SIZE 64

#endif /* TUSB_CONFIG_H_ */
