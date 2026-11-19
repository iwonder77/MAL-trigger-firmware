# =============================================================================
# MAL-trigger-firmware: build configuration
# =============================================================================
#
# CMake doesn't compile anything itself. It reads this file once (the
# "configure" step) and writes build instructions for Ninja, which then runs
# the real tools:
#
#   compiler  arm-none-eabi-gcc / g++   each .c / .cpp file -> one .o file
#             needs every #include'd header to be found (-I paths)
#   linker    arm-none-eabi-gcc (ld)    all .o files -> one .elf firmware image
#             needs exactly one definition of every function that is used
#
# The file reads top to bottom in the order one could explain the build:
#
#   1. Bootstrap          which CMake, where the SDK and cross-compiler are
#   2. Project            name, version, languages
#   3. Pico SDK           check its version, load its libraries
#   4. Firmware target    the one thing this project builds:
#        4a. sources        what we compile
#        4b. include paths  where the compiler looks for our headers
#        4c. libraries      what we link (and the source files they bring)
#        4d. stdio          where printf() goes
#        4e. metadata       what `picotool info` reports
#        4f. outputs        .uf2 and friends next to the .elf
#
# Configure and build from the repo root:
#   cmake -S . -B build -G Ninja -DPICO_BOARD=sparkfun_promicro
#   cmake --build build

# -----------------------------------------------------------------------------
# 1. Bootstrap (must come before project())
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13)

# Write build/compile_commands.json: the exact compiler command for every
# source file. clangd (Neovim) reads it to resolve includes and defines, and it
# is the place to check which flags a file was really compiled with.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point CMake at the Pico SDK and the arm-none-eabi cross-compiler. This must
# come before project(), because project() is where CMake picks and tests the
# compiler; without it, CMake would pick the Mac's own clang.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# -----------------------------------------------------------------------------
# 2. Project
# -----------------------------------------------------------------------------

# The name becomes ${PROJECT_NAME} (the target and output file names) and
# VERSION becomes ${PROJECT_VERSION} (embedded in the binary, see 4e).
# ASM is enabled because the SDK's startup code is written in assembly.
project(MAL-trigger-firmware VERSION 0.1.0 LANGUAGES C CXX ASM)

# C11 for .c files (TinyUSB, and usb_descriptors.c in M3), C++17 for ours.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# -----------------------------------------------------------------------------
# 3. Pico SDK
# -----------------------------------------------------------------------------

# Stop at configure time if a different SDK is installed, instead of building
# against an SDK this firmware was never tested with.
set(REQUIRED_PICO_SDK_VERSION "2.3.1")

if(NOT PICO_SDK_VERSION_STRING STREQUAL REQUIRED_PICO_SDK_VERSION)
  message(FATAL_ERROR
    "Pico SDK version mismatch.\n"
    "Expected: ${REQUIRED_PICO_SDK_VERSION}\n"
    "Actual: ${PICO_SDK_VERSION_STRING}"
  )
endif()

# Define every SDK library as a CMake target (pico_stdlib, tinyusb_device, ...)
# so the firmware target below can link them by name.
pico_sdk_init()

# -----------------------------------------------------------------------------
# 4. Firmware target
# -----------------------------------------------------------------------------
# Everything below configures one target: the firmware executable.
#
# target_* commands take PRIVATE / PUBLIC / INTERFACE, which say whether a
# setting also passes on to targets that link this one. Nothing ever links
# a firmware executable, so PRIVATE is used throughout.

# 4a. Our source files. Headers are not listed; the compiler finds them through
#     #include and the include paths in 4b.
add_executable(${PROJECT_NAME}
  src/main.cpp
)

# 4b. Add src/ to the header search path (-I) of EVERY file compiled into this
#     target. That includes TinyUSB's own .c files (see 4c) which
#     #include "tusb_config.h" and expect the application (us) to provide it.
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
)

# 4c. SDK libraries. These are not precompiled: each is a CMake INTERFACE
#     library, a bundle of source files, include paths, -D defines and further
#     dependencies. Linking one adds its .c files to THIS target, compiled with
#     THIS target's settings (including 4b).
#
#     pico_stdlib     gpio, timers, stdio, and the startup code that runs
#                     before main()
#     tinyusb_device  the TinyUSB device stack, configured by src/tusb_config.h.
#                     Also defines LIB_TINYUSB_DEVICE=1, which tells the SDK
#                     that this application owns the USB port.
target_link_libraries(${PROJECT_NAME} PRIVATE
  pico_stdlib
  tinyusb_device
)

# 4d. Route printf() to UART0 (GPIO0 TX / GPIO1 RX, from the board header).
#     USB stdio stays off because the USB port is a HID keyboard now. Turning
#     it on would still build, but only print a warning, and output nothing.
pico_enable_stdio_uart(${PROJECT_NAME} 1)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

# 4e. Stored inside the binary and shown by `picotool info`, so a board pulled
#     from an exhibit can be identified without its source code.
pico_set_program_name(${PROJECT_NAME} "${PROJECT_NAME}")
pico_set_program_version(${PROJECT_NAME} "${PROJECT_VERSION}")

# 4f. Besides the .elf, generate .uf2 (for flashing), .bin, .hex, .map (where
#     the linker put everything, and what it discarded) and .dis (disassembly).
pico_add_extra_outputs(${PROJECT_NAME})
