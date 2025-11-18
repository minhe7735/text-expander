#ifndef ZMK_TEXT_EXPANDER_KEYMAP_UTILS_H
#define ZMK_TEXT_EXPANDER_KEYMAP_UTILS_H

#include <stdint.h>
#include <zmk/hid.h>

// --- INPUT LAYOUT MAPPING ---
#define HID_USAGE_A 0x04
#define HID_USAGE_SLASH 0x38
#define MAP_SIZE (HID_USAGE_SLASH + 1)

/**
 * @brief Converts a HID keycode to a character based on the configured layout.
 * 
 * @param keycode The HID usage ID of the key.
 * @return char The corresponding character, or '\0' if not mapped.
 */
char keycode_to_short_code_char(uint16_t keycode);

/**
 * Converts a character to a ZMK HID keycode.
 * @param c The character to convert (e.g., 'a', '?', '\n').
 * @param needs_shift Output pointer. Set to true if the keycode requires Shift held down.
 * @return The 32-bit HID usage code, or 0 if not supported.
 */
uint32_t char_to_keycode(char c, bool *needs_shift);

#endif // ZMK_TEXT_EXPANDER_KEYMAP_UTILS_H
