#ifndef ZMK_TEXT_EXPANDER_LAYOUTS_COMMON_H
#define ZMK_TEXT_EXPANDER_LAYOUTS_COMMON_H

#include <zephyr/kernel.h>
#include <zmk/hid.h>
#include <zmk/keymap_utils.h>

// --- SHARED DEFINITIONS ---

#define KEYCODE_LUT_OFFSET 32
#define KEYCODE_LUT_SIZE (127 - KEYCODE_LUT_OFFSET)

// Helper macro to safely calculate array index
#define IDX(c) ((c) - KEYCODE_LUT_OFFSET)

typedef struct __attribute__((packed)) {
    uint16_t keycode;
    uint8_t needs_shift : 1;
    uint8_t reserved : 7;
} keycode_map_entry_t;

// Macros for defining the LUT
#define MAP_K(code) HID_USAGE_KEY_KEYBOARD_##code
#define MAP_S(code) {HID_USAGE_KEY_KEYBOARD_##code, 1}
#define MAP_U(code) {HID_USAGE_KEY_KEYBOARD_##code, 0}

#endif // ZMK_TEXT_EXPANDER_LAYOUTS_COMMON_H
