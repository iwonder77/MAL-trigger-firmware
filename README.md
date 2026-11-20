# MAL trigger firmware

Firmware for the SparkFun Pro Micro RP2040. A visitor presses a button and a timed output signal runs. For now the status LED stands in for real playback (BrightSign and/or sound module). Presses during playback are ignored, and a held button triggers only once.

**Status:** the board enumerates as a USB HID keyboard ("MAL Trigger Controller"), but doesn't send key presses yet. Sending a key on each button press (to trigger the BrightSign LS5) is next.

## Hardware

Board: SparkFun Pro Micro RP2040 (`PICO_BOARD=sparkfun_promicro`).

| Signal     | GPIO       | Notes                                                                                            |
| ---------- | ---------- | ------------------------------------------------------------------------------------------------ |
| Button     | 26 (A0)    | Active low: button to GND with an external pull-up. The internal pulls are disabled in firmware. |
| Status LED | 4          | External LED. The board has no plain LED: GPIO 25 drives its WS2812.                             |
| Debug UART | 0 TX, 1 RX | `printf` output, 115200 baud, through a USB-to-UART adapter                                     |
| USB-C      |            | Reserved for the USB HID keyboard. There is no USB serial port.                                  |

## How it works

Every pass of the main loop does the same three things, and never waits:

```
read clock + button  →  debouncer.update()  →  state machine  →  LED / printf
     (sense)                (clean up)            (decide)          (act)
```

### Playback state machine

```mermaid
stateDiagram-v2
    [*] --> IDLE: power-up, debouncer seeded from the real button
    IDLE --> PLAYING: press event, LED on
    PLAYING --> PLAYING: press event, logged and ignored
    PLAYING --> IDLE: PLAYBACK_DURATION_MS elapsed, LED off
```

The debouncer is updated on every pass in both states, so a press during playback is used up immediately instead of starting another playback when this one ends.

### Debouncing

```
                  press + bounces                 release + bounces
raw       __________|‾|_|‾|_|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|_|‾|_____________
                    ^ ^ ^ ^ ^                         ^ ^ ^
                            ├───────┤                     ├───────┤
                              20 ms                         20 ms
stable    __________________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|_____
                                    ▲                             ▲
                              true (press)                  false (release)
update()  __________________________|‾|_________________________________
                                    ▲                             ▲
                              true only during press        false elsewhere and upon release
```

Every change in the raw reading restarts a stopwatch, so bounces are never accepted. Once the reading has been steady for `DEBOUNCE_MS`, it becomes the stable state. `update()` returns true only on the pass where the stable state becomes pressed, which makes it an event rather than a state: holding the button produces one press, not a stream of them.

## Files

| File                | Contents                                                                                                                 |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `src/main.cpp`      | Pin setup, hardware helpers and the playback state machine                                                               |
| `src/debouncer.h`   | The `Debouncer` class. Pure logic with no Pico SDK calls, so it can be reused for other inputs and tested on a computer. |
| `src/tusb_config.h` | TinyUSB's compile-time configuration: device mode, one HID keyboard interface                                            |
| `src/usb_descriptors.c` | What the board tells the computer when plugged in: IDs, keyboard layout, names and serial number                     |
| `CMakeLists.txt`    | Build configuration, in numbered sections that follow the configure → compile → link flow                                |

## Configuration

| Constant                       | Default | Meaning                                                       |
| ------------------------------ | ------- | ------------------------------------------------------------- |
| `DEBOUNCE_MS`                  | 20      | How long the raw reading must be steady before it is accepted |
| `PLAYBACK_DURATION_MS`         | 5000    | How long each playback lasts                                  |
| `BUTTON_PIN`, `STATUS_LED_PIN` | 26, 4   | GPIO numbers                                                  |

## Build

```zsh
cmake -S . -B build -G Ninja -DPICO_BOARD=sparkfun_promicro
cmake --build build
```

The `.uf2` is named after the project in `CMakeLists.txt`: `build/MAL-trigger-firmware.uf2`.

## Flash

The USB port belongs to the HID keyboard, so this firmware has no USB serial port, and `picotool load -f` can't reboot the board for you. Put it into BOOTSEL mode by hand:

1. Hold **BOOT**, tap **RESET**, then release **BOOT**. (Or hold **BOOT** while plugging in USB.)
2. Load and run:

```zsh
picotool load -x build/MAL-trigger-firmware.uf2
```

`-x` starts the new firmware right after loading. To check which firmware is on a board, put it in BOOTSEL mode and run `picotool info`: it shows the program name and version set in `CMakeLists.txt`.

## Debug output

`printf` goes to UART0 only. Connect a 3.3 V USB-to-UART adapter (adapter RX → GPIO 0 TX, adapter TX → GPIO 1 RX, GND → GND), then:

```zsh
tio -b 115200 /dev/tty.usbserial-*
```

The device name depends on the adapter; `ls /dev/tty.*` lists the options.

## Tests to run after changes

| Test                           | Expected                                                            |
| ------------------------------ | ------------------------------------------------------------------- |
| One press                      | One 5 s playback, then the LED turns off and the button works again |
| Hold the button for 12 s       | Exactly one playback                                                |
| Tap during playback            | Logged as ignored; playback is unaffected                           |
| Power up with the button held  | Nothing until the button is released and pressed again              |
| Bare jumper wire tapped to GND | One press per tap                                                   |
