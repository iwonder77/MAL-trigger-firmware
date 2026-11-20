/**
 * @file usb_descriptors.c
 * @brief What the board tells the computer about itself when plugged in.
 *
 * TinyUSB calls the *_cb functions in this file when the computer asks.
 */
#include <string.h> // for strlen()

#include "pico/unique_id.h" // the chip's unique ID, for the serial number
#include "tusb.h"

// ---------- DEVICE DESCRIPTOR -------------------------
// The board's "ID card": the first thing the computer reads on plug-in.

#define USB_VID                                                                \
  0xCafe // vendor ID: TinyUSB's test ID (placeholder, fine in-house)
#define USB_PID 0x4004 // product ID (placeholder)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,     // speaks USB 2.0
    .bDeviceClass = 0x00, // "see my interfaces for what I am" (a keyboard)
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, // from tusb_config.h
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100, // our device version, 1.00
    .iManufacturer = 1,  // "my maker's name is string #1"
    .iProduct = 2,       // "my product name is string #2"
    .iSerialNumber = 3,  // "my serial number is string #3"
    .bNumConfigurations = 1,

};

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&desc_device;
}
// -----------------------------------------------------

// ---------- HID REPORT DESCRIPTOR --------------------
// Describes what our keyboard messages look like: the standard 8-byte keyboard
// layout. Empty () = no report ID, since a keyboard is all we are.

static const uint8_t desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

// called when the computer asks "what do your messages look like?"
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}
// ----------------------------------------------------

// ---------- CONFIGURATION DESCRIPTOR ---------------
// answers host's question "what can you do?": describes how much power we
// need, our one function (a keyboard), and the endpoint the computer collects
// key messages from.

// interface numbers: keyboard = 0, and ITF_NUM_TOTAL counts them for us (= 1)
enum { ITF_NUM_KEYBOARD, ITF_NUM_TOTAL };

#define EPNUM_KEYBOARD                                                         \
  0x81 // endpoint #1; the 0x80 part means "board -> computer"

// the computer reads this whole block at once, so it needs the total size
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // config #1, interface count, no name string, total size,
    // no special features, ask for up to 100 mA of power
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),

    // interface #0, no name string, a "boot" keyboard, size of piece 2,
    // endpoint address, endpoint size (8 bytes), check it every 10 ms
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report), EPNUM_KEYBOARD,
                       CFG_TUD_HID_EP_BUFSIZE, 10),
};

// called when the computer asks us that question, "what can you do?"
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index; // we only have one configuration
  return desc_configuration;
}
// ----------------------------------------------------

// ---------- STRING DESCRIPTORS ----------------------
// Answers host's questions "what are your names?": string of text the computer
// can show, e.g. in macOS System Information. The numbers match the ones in the
// device descriptor.

#define USB_MANUFACTURER "Thanksgiving Point"
#define USB_PRODUCT "MAL Trigger Controller"

enum {
  STRID_LANGUAGE = 0, // special one: "which languages do you speak?"
  STRID_MANUFACTURER = 1,
  STRID_PRODUCT = 2,
  STRID_SERIAL = 3,
};

// the answer we hand back. `static` so it still exists after this function
// returns, because the computer reads it a moment later
static uint16_t desc_str[32];

// called when the computer asks "what is string #index?"
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid; // we only speak English
  size_t len;   // number of characters in our answer

  if (index == STRID_LANGUAGE) {
    desc_str[1] = 0x0409; // English (US)
    len = 1;
  } else {
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *text;

    switch (index) {
    case STRID_MANUFACTURER:
      text = USB_MANUFACTURER;
      break;
    case STRID_PRODUCT:
      text = USB_PRODUCT;
      break;
    case STRID_SERIAL:
      pico_get_unique_board_id_string(serial, sizeof(serial));
      text = serial;
      break;
    default:
      return NULL; // no such string; TinyUSB tells the computer so
    }

    // copy the text into USB's 2-bytes-per-character format,
    // capped at 31 characters so it fits in desc_str
    len = strlen(text);
    if (len > 31) {
      len = 31;
    }
    for (size_t i = 0; i < len; i++) {
      desc_str[1 + i] = (uint8_t)text[i];
    }
  }

  // slot 0 is a small header: total size in bytes, and "this is a string"
  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return desc_str;
}
// --------------------------------------------------
