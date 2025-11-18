#ifndef ZMK_HID_UTILS_H
#define ZMK_HID_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <zmk/hid.h>



/**
 * Sends a key press or release event and flushes the HID report.
 * @param keycode The ZMK HID usage keycode.
 * @param pressed true for key down, false for key up.
 * @return 0 on success, or negative error code.
 */
int send_and_flush_key_action(uint32_t keycode, bool pressed);

static inline int send_key_action(uint32_t keycode, bool pressed) {
    return pressed ? zmk_hid_keyboard_press(keycode) : zmk_hid_keyboard_release(keycode);
}

#endif /* ZMK_HID_UTILS_H */
