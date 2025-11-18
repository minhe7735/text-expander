#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <ctype.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/expansion_engine.h>
#include <zmk/hid_utils.h>
#include <zmk/text_expander.h>
LOG_MODULE_REGISTER(expansion_engine, LOG_LEVEL_DBG);

// Timing constants
#define TYPING_JITTER_DIVISOR 2
#define MIN_TYPING_DELAY_MS 1
#define EXPANSION_START_DELAY_MS 10
#define CHAR_PRESS_DELAY_MS 1

/**
 * @brief Converts a 32-bit unsigned integer to decimal string.
 * @param val Value to convert (for Unicode: valid range 0-0x10FFFF for codepoints)
 * @param buf Output buffer
 * @param buf_size Size of output buffer (must be at least 11 bytes for max uint32)
 * 
 * Used for Windows Unicode input (Alt+numpad decimal codes).
 * Note: Valid Unicode codepoints are 0x0000-0x10FFFF, but this function accepts
 * full uint32 range. Caller should validate codepoint bounds before calling.
 */
static void u32_to_str_dec(uint32_t val, char *buf, size_t buf_size) {
    if (buf_size == 0) return;
    if (buf_size < 2) {
        buf[0] = '\0';
        LOG_ERR("Buffer too small for decimal conversion");
        return;
    }
    // Safety: temp buffer sized for max uint32_t (4294967295 = 10 digits + null)
    char temp[12];
    int idx = 0;
    if (val == 0) { temp[idx++] = '0'; }
    while (val > 0) {
        temp[idx++] = '0' + (val % 10);
        val /= 10;
    }
    int out_idx = 0;
    // Buffer overflow protection: respect buf_size
    while (idx > 0 && out_idx < (int)(buf_size - 1)) {
        buf[out_idx++] = temp[--idx];
    }
    buf[out_idx] = '\0';
}

/**
 * @brief Converts a 32-bit unsigned integer to hexadecimal string.
 * @param val Value to convert (for Unicode: valid range 0x0000-0x10FFFF)
 * @param buf Output buffer
 * @param buf_size Size of output buffer (must be at least 9 bytes for max uint32)
 * @param zero_pad_4 If true, zero-pad to 4 hex digits (for macOS Unicode input)
 * 
 * Used for macOS (Option+hex) and Linux (Ctrl+Shift+U+hex) Unicode input.
 * Note: Max valid Unicode codepoint is U+10FFFF (6 hex digits).
 * Caller should validate codepoint bounds before calling.
 */
static void u32_to_str_hex(uint32_t val, char *buf, size_t buf_size, bool zero_pad_4) {
    if (buf_size == 0) return;
    if (buf_size < 2) {
        buf[0] = '\0';
        LOG_ERR("Buffer too small for hex conversion");
        return;
    }
    // Safety: temp buffer sized for max uint32_t (FFFFFFFF = 8 hex digits + padding + null)
    char temp[10];
    int idx = 0;
    if (val == 0) { temp[idx++] = '0'; }
    while (val > 0) {
        int digit = val % 16;
        temp[idx++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        val /= 16;
    }
    
    // macOS requires 4-digit hex codes
    while (zero_pad_4 && idx < 4) {
        temp[idx++] = '0';
    }

    int out_idx = 0;
    // Buffer overflow protection: respect buf_size
    while (idx > 0 && out_idx < (int)(buf_size - 1)) {
        buf[out_idx++] = temp[--idx];
    }
    buf[out_idx] = '\0';
}

static void handle_start_backspace(struct expansion_work *exp_work);
static void handle_backspace_press(struct expansion_work *exp_work);
static void handle_backspace_release(struct expansion_work *exp_work);
static void handle_start_typing(struct expansion_work *exp_work);
static void handle_type_char_start(struct expansion_work *exp_work);
static void handle_type_char_key_press(struct expansion_work *exp_work);
static void handle_type_char_key_release(struct expansion_work *exp_work);
static void handle_finish(struct expansion_work *exp_work);
static void handle_replay_key_press(struct expansion_work *exp_work);
static void handle_replay_key_release(struct expansion_work *exp_work);

static void win_start_unicode_typing(struct expansion_work *exp_work);
static void handle_win_uni_press_alt(struct expansion_work *exp_work);
static void handle_win_uni_type_numpad_press(struct expansion_work *exp_work);
static void handle_win_uni_type_numpad_release(struct expansion_work *exp_work);
static void handle_win_uni_release_alt(struct expansion_work *exp_work);

static void macos_start_unicode_typing(struct expansion_work *exp_work);
static void handle_mac_uni_press_option(struct expansion_work *exp_work);
static void handle_mac_uni_type_hex_press(struct expansion_work *exp_work);
static void handle_mac_uni_type_hex_release(struct expansion_work *exp_work);
static void handle_mac_uni_release_option(struct expansion_work *exp_work);

static void linux_start_unicode_typing(struct expansion_work *exp_work);
static void handle_linux_uni_press_ctrl_shift(struct expansion_work *exp_work);
static void handle_linux_uni_press_u(struct expansion_work *exp_work);
static void handle_linux_uni_release_u(struct expansion_work *exp_work);
static void handle_linux_uni_release_ctrl_shift(struct expansion_work *exp_work);
static void handle_linux_uni_type_hex_press(struct expansion_work *exp_work);
static void handle_linux_uni_type_hex_release(struct expansion_work *exp_work);
static void handle_linux_uni_press_terminator(struct expansion_work *exp_work);
static void handle_linux_uni_release_terminator(struct expansion_work *exp_work);


static uint32_t get_numpad_keycode(char digit);
static uint32_t get_hex_keycode(char hex_digit);

const struct os_typing_driver win_driver = { .start_unicode_typing = win_start_unicode_typing };
const struct os_typing_driver mac_driver = { .start_unicode_typing = macos_start_unicode_typing };
const struct os_typing_driver linux_driver = { .start_unicode_typing = linux_start_unicode_typing };

static k_timeout_t get_typing_delay() {
    uint32_t delay = TYPING_DELAY;
    
    // Safety: Clamp delay to reasonable bounds to prevent overflow
    // Max reasonable typing delay is 1000ms
    if (delay > 1000) {
        delay = 1000;
    }
    
    uint32_t jitter_range = delay / TYPING_JITTER_DIVISOR;
    
    if (jitter_range > 0) {
        uint32_t jitter = sys_rand32_get() % jitter_range;
        uint32_t jitter_amount = jitter / TYPING_JITTER_DIVISOR;
        
        if (sys_rand32_get() % 2 == 0) {
            // Safe addition: jitter_amount is much smaller than delay
            delay += jitter_amount;
        } else {
            // Safe subtraction: ensure we don't underflow
            if (delay > jitter_amount) {
                delay -= jitter_amount;
            }
        }
    }
    return K_MSEC(MAX(MIN_TYPING_DELAY_MS, delay));
}

static void clear_shift_if_active(struct expansion_work *exp_work) {
    if (exp_work->shift_mod_active) {
        LOG_DBG("Clearing active shift modifier.");
        zmk_hid_unregister_mods(MOD_LSFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        exp_work->shift_mod_active = false;
    }
}

/**
 * @brief Cancels the current expansion and optionally performs partial undo.
 * @param work_item The expansion work context
 * @param partial_undo If true, backspaces typed characters before canceling
 * 
 * Releases any stuck keys, clears shift modifier, and either performs
 * partial undo or transitions to idle state. Handles all cancellation
 * scenarios with appropriate error recovery.
 */
void cancel_current_expansion(struct expansion_work *work_item, bool partial_undo) {
    int cancel_result = k_work_cancel_delayable(&work_item->work);
    
    // Handle different cancellation results
    if (cancel_result < 0) {
        // Negative values indicate errors
        if (cancel_result == -EINVAL) {
            LOG_ERR("Invalid work item during cancellation");
        } else {
            LOG_WRN("Failed to cancel work, result=%d (work may have completed)", cancel_result);
        }
        // Continue with cleanup - state recovery is critical even if cancel failed
    } else if (cancel_result > 0) {
        LOG_DBG("Canceled %d pending work item(s)", cancel_result);
    } else {
        // cancel_result == 0: work was not pending (idle or running)
        LOG_DBG("Work was not pending during cancellation");
    }
    
    // Release any stuck keycode to prevent modifier/key sticking
    if (work_item->current_keycode > 0) {
        LOG_DBG("Releasing potentially stuck keycode: 0x%04X", work_item->current_keycode);
        send_and_flush_key_action(work_item->current_keycode, false);
        
        // Update character count if we were in the middle of typing a character
        if (work_item->state == EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE) {
            work_item->characters_typed++;
        }

        // Update backspace count if we were in the middle of backspacing
        if (work_item->state == EXPANSION_STATE_BACKSPACE_RELEASE) {
            if (work_item->backspace_count > 0) {
                work_item->backspace_count--;
            }
        }
    }
    
    // Always clear shift modifier to prevent stuck shift
    clear_shift_if_active(work_item);

    // Handle partial undo or complete cancellation
    if (partial_undo && work_item->characters_typed > 0) {
        LOG_INF("Canceling and initiating partial undo of %d chars", work_item->characters_typed);
        work_item->backspace_count = work_item->characters_typed;
        work_item->expanded_text = "";
        work_item->trigger_keycode_to_replay = 0;
        work_item->text_index = 0;
        work_item->current_keycode = 0;
        work_item->state = EXPANSION_STATE_START_BACKSPACE;
        k_work_reschedule(&work_item->work, K_MSEC(1));
    } else {
        LOG_INF("Cancelling current expansion work (no undo).");
        // Reset to consistent idle state
        work_item->state = EXPANSION_STATE_IDLE;
        work_item->current_keycode = 0;
        work_item->text_index = 0;
        work_item->backspace_count = 0;
        work_item->characters_typed = 0;
        work_item->trigger_keycode_to_replay = 0;
        
        // Verify state consistency
        if (work_item->shift_mod_active) {
            LOG_WRN("Shift modifier still active after cancellation cleanup");
        }
    }
}



static void handle_idle(struct expansion_work *exp_work) {
    // No-op
}

static void handle_unicode_start(struct expansion_work *exp_work) {
    if (expander_data.os_driver && expander_data.os_driver->start_unicode_typing) {
        expander_data.os_driver->start_unicode_typing(exp_work);
    }
}

typedef void (*expansion_state_handler_t)(struct expansion_work *exp_work);

static const expansion_state_handler_t state_handlers[] = {
    [EXPANSION_STATE_IDLE] = handle_idle,
    [EXPANSION_STATE_START_BACKSPACE] = handle_start_backspace,
    [EXPANSION_STATE_BACKSPACE_PRESS] = handle_backspace_press,
    [EXPANSION_STATE_BACKSPACE_RELEASE] = handle_backspace_release,
    [EXPANSION_STATE_START_TYPING] = handle_start_typing,
    [EXPANSION_STATE_TYPE_CHAR_START] = handle_type_char_start,
    [EXPANSION_STATE_TYPE_CHAR_KEY_PRESS] = handle_type_char_key_press,
    [EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE] = handle_type_char_key_release,
    [EXPANSION_STATE_FINISH] = handle_finish,
    [EXPANSION_STATE_REPLAY_KEY_PRESS] = handle_replay_key_press,
    [EXPANSION_STATE_REPLAY_KEY_RELEASE] = handle_replay_key_release,
    [EXPANSION_STATE_UNICODE_START] = handle_unicode_start,
    [EXPANSION_STATE_WIN_UNI_PRESS_ALT] = handle_win_uni_press_alt,
    [EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS] = handle_win_uni_type_numpad_press,
    [EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_RELEASE] = handle_win_uni_type_numpad_release,
    [EXPANSION_STATE_WIN_UNI_RELEASE_ALT] = handle_win_uni_release_alt,
    [EXPANSION_STATE_MAC_UNI_PRESS_OPTION] = handle_mac_uni_press_option,
    [EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS] = handle_mac_uni_type_hex_press,
    [EXPANSION_STATE_MAC_UNI_TYPE_HEX_RELEASE] = handle_mac_uni_type_hex_release,
    [EXPANSION_STATE_MAC_UNI_RELEASE_OPTION] = handle_mac_uni_release_option,
    [EXPANSION_STATE_LINUX_UNI_PRESS_CTRL_SHIFT] = handle_linux_uni_press_ctrl_shift,
    [EXPANSION_STATE_LINUX_UNI_PRESS_U] = handle_linux_uni_press_u,
    [EXPANSION_STATE_LINUX_UNI_RELEASE_U] = handle_linux_uni_release_u,
    [EXPANSION_STATE_LINUX_UNI_RELEASE_CTRL_SHIFT] = handle_linux_uni_release_ctrl_shift,
    [EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS] = handle_linux_uni_type_hex_press,
    [EXPANSION_STATE_LINUX_UNI_TYPE_HEX_RELEASE] = handle_linux_uni_type_hex_release,
    [EXPANSION_STATE_LINUX_UNI_PRESS_TERMINATOR] = handle_linux_uni_press_terminator,
    [EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR] = handle_linux_uni_release_terminator,
};

// Compile-time assertion: ensure state_handlers array covers all states
// This prevents accidental omissions when adding new expansion states
BUILD_ASSERT(ARRAY_SIZE(state_handlers) == EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR + 1,
             "state_handlers array must have entries for all expansion states");

/**
 * @brief Main work handler for the expansion state machine.
 * 
 * Performance Note: The LOG_DBG call below has minimal overhead when debug
 * logging is disabled (LOG_LEVEL_DBG not set). The Zephyr logging framework
 * optimizes away disabled log levels at compile time. For critical performance
 * paths during character typing, logging is intentionally minimal.
 */
void expansion_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
    struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);

    LOG_DBG("Expansion engine state: %d", exp_work->state);

    if (exp_work->state < ARRAY_SIZE(state_handlers) && state_handlers[exp_work->state]) {
        state_handlers[exp_work->state](exp_work);
    } else {
        LOG_WRN("Unhandled expansion state: %d. Setting to IDLE.", exp_work->state);
        exp_work->state = EXPANSION_STATE_IDLE;
    }
}

static void handle_start_backspace(struct expansion_work *exp_work) {
    if (exp_work->backspace_count > 0) {
        LOG_DBG("Starting backspace sequence, %d to go.", exp_work->backspace_count);
        exp_work->state = EXPANSION_STATE_BACKSPACE_PRESS;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
    } else {
        LOG_DBG("No backspaces needed, starting typing.");
        exp_work->state = EXPANSION_STATE_START_TYPING;
        k_work_reschedule(&exp_work->work, get_typing_delay());
    }
}

static void handle_backspace_press(struct expansion_work *exp_work) {
    LOG_DBG("Pressing backspace");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
    exp_work->state = EXPANSION_STATE_BACKSPACE_RELEASE;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void handle_backspace_release(struct expansion_work *exp_work) {
    LOG_DBG("Releasing backspace");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
    exp_work->backspace_count--;
    exp_work->state = EXPANSION_STATE_START_BACKSPACE;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void handle_start_typing(struct expansion_work *exp_work) {
    LOG_DBG("Beginning to type expanded text.");
    handle_type_char_start(exp_work);
}

/**
 * @brief Handles the start of character typing in an expansion.
 * @param exp_work The expansion work context
 * 
 * Processes the current character from expanded text:
 * - OS command bytecodes to switch Unicode input method
 * - ASCII characters using keycode mapping
 * - UTF-8 multi-byte sequences for Unicode characters
 */
static void handle_type_char_start(struct expansion_work *exp_work) {
    const char *text = exp_work->expanded_text;
    uint8_t current_byte = (uint8_t)text[exp_work->text_index];

    if (current_byte == 0) {
        exp_work->state = EXPANSION_STATE_FINISH;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }

    if (current_byte == EXP_OP_CMD_WIN) {
        expander_data.os_driver = &win_driver;
        exp_work->text_index++;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    } else if (current_byte == EXP_OP_CMD_MAC) {
        expander_data.os_driver = &mac_driver;
        exp_work->text_index++;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    } else if (current_byte == EXP_OP_CMD_LINUX) {
        expander_data.os_driver = &linux_driver;
        exp_work->text_index++;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    } 
    
    uint8_t first_byte = text[exp_work->text_index];

    if (first_byte < 0x80) {
        exp_work->current_keycode = char_to_keycode(first_byte, &exp_work->current_char_needs_shift);
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
        k_work_reschedule(&exp_work->work, K_MSEC(CHAR_PRESS_DELAY_MS));
        return;
    } else {
        uint32_t codepoint = 0;
        int utf8_len = 0;

        if ((first_byte & 0xE0) == 0xC0) {
            utf8_len = 2;
            codepoint = (first_byte & 0x1F) << 6;
        } else if ((first_byte & 0xF0) == 0xE0) {
            utf8_len = 3;
            codepoint = (first_byte & 0x0F) << 12;
        } else if ((first_byte & 0xF8) == 0xF0) {
            utf8_len = 4;
            codepoint = (first_byte & 0x07) << 18;
        }

        if (utf8_len > 0) {
            for (int i = 1; i < utf8_len; i++) {
                // Bounds check: ensure we don't read beyond the string
                if (text[exp_work->text_index + i] == '\0') {
                    LOG_WRN("Malformed UTF-8: unexpected end of string");
                    codepoint = 0;
                    break;
                }
                uint8_t cont_byte = text[exp_work->text_index + i];
                if ((cont_byte & 0xC0) == 0x80) {
                    codepoint |= (cont_byte & 0x3F) << (6 * (utf8_len - 1 - i));
                } else {
                    LOG_WRN("Malformed UTF-8: invalid continuation byte 0x%02X", cont_byte);
                    codepoint = 0;
                    break;
                }
            }
            if (codepoint != 0) {
                exp_work->unicode_codepoint = codepoint;
                exp_work->text_index += utf8_len; 
                exp_work->state = EXPANSION_STATE_UNICODE_START;
                k_work_reschedule(&exp_work->work, get_typing_delay());
                return;
            }
        }
        
        exp_work->text_index++;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }
}

static void handle_type_char_key_press(struct expansion_work *exp_work) {
    if (exp_work->current_char_needs_shift && !exp_work->shift_mod_active) {
        zmk_hid_register_mods(MOD_LSFT);
        exp_work->shift_mod_active = true;
    } else if (!exp_work->current_char_needs_shift && exp_work->shift_mod_active) {
        zmk_hid_unregister_mods(MOD_LSFT);
        exp_work->shift_mod_active = false;
    }
    
    if (exp_work->current_keycode > 0) {
        send_and_flush_key_action(exp_work->current_keycode, true);
    }
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void handle_type_char_key_release(struct expansion_work *exp_work) {
    if (exp_work->current_keycode > 0) {
        send_and_flush_key_action(exp_work->current_keycode, false);
        exp_work->current_keycode = 0;
    }
    if (exp_work->state != EXPANSION_STATE_UNICODE_START) {
       exp_work->text_index++;
    }

    exp_work->characters_typed++;

    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void handle_finish(struct expansion_work *exp_work) {
    clear_shift_if_active(exp_work);
    if (exp_work->trigger_keycode_to_replay > 0) {
        exp_work->state = EXPANSION_STATE_REPLAY_KEY_PRESS;
        k_work_reschedule(&exp_work->work, get_typing_delay());
    } else {
        exp_work->state = EXPANSION_STATE_IDLE;
    }
}

static void handle_replay_key_press(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, true);
    exp_work->state = EXPANSION_STATE_REPLAY_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void handle_replay_key_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, false);
    exp_work->state = EXPANSION_STATE_IDLE;
}

static void win_start_unicode_typing(struct expansion_work *exp_work) {
    // Convert to decimal string
    u32_to_str_dec(exp_work->unicode_codepoint, exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer));
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_WIN_UNI_PRESS_ALT;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_win_uni_press_alt(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_win_uni_type_numpad_press(struct expansion_work *exp_work) {
    char digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (digit == '\0') {
        exp_work->state = EXPANSION_STATE_WIN_UNI_RELEASE_ALT;
    } else {
        exp_work->current_keycode = get_numpad_keycode(digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_RELEASE;
    }
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_win_uni_type_numpad_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_win_uni_release_alt(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->characters_typed++;
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void macos_start_unicode_typing(struct expansion_work *exp_work) {
    // Convert to hex string (pad to 4)
    u32_to_str_hex(exp_work->unicode_codepoint, exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), true);
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_MAC_UNI_PRESS_OPTION;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_mac_uni_press_option(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_mac_uni_type_hex_press(struct expansion_work *exp_work) {
    char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (hex_digit == '\0') {
        exp_work->state = EXPANSION_STATE_MAC_UNI_RELEASE_OPTION;
    } else {
        exp_work->current_keycode = get_hex_keycode(hex_digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_RELEASE;
    }
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_mac_uni_type_hex_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_mac_uni_release_option(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->characters_typed++;
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static void linux_start_unicode_typing(struct expansion_work *exp_work) {
    // Convert to hex string (no padding)
    u32_to_str_hex(exp_work->unicode_codepoint, exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), false);
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_CTRL_SHIFT;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_linux_uni_press_ctrl_shift(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LCTL | MOD_LSFT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_U;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_press_u(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, true);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_U;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_release_u(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, false);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_CTRL_SHIFT;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_release_ctrl_shift(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LCTL | MOD_LSFT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_type_hex_press(struct expansion_work *exp_work) {
    char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (hex_digit == '\0') {
        exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_TERMINATOR;
    } else {
        exp_work->current_keycode = get_hex_keycode(hex_digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_RELEASE;
    }
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_type_hex_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_press_terminator(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, true);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}
static void handle_linux_uni_release_terminator(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, false);
    exp_work->characters_typed++;
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, get_typing_delay());
}

static uint32_t get_numpad_keycode(char digit) {
    switch (digit) {
        case '0': return HID_USAGE_KEY_KEYPAD_0_AND_INSERT;
        case '1': return HID_USAGE_KEY_KEYPAD_1_AND_END;
        case '2': return HID_USAGE_KEY_KEYPAD_2_AND_DOWN_ARROW;
        case '3': return HID_USAGE_KEY_KEYPAD_3_AND_PAGEDN;
        case '4': return HID_USAGE_KEY_KEYPAD_4_AND_LEFT_ARROW;
        case '5': return HID_USAGE_KEY_KEYPAD_5;
        case '6': return HID_USAGE_KEY_KEYPAD_6_AND_RIGHT_ARROW;
        case '7': return HID_USAGE_KEY_KEYPAD_7_AND_HOME;
        case '8': return HID_USAGE_KEY_KEYPAD_8_AND_UP_ARROW;
        case '9': return HID_USAGE_KEY_KEYPAD_9_AND_PAGEUP;
        default:  return 0;
    }
}

static uint32_t get_hex_keycode(char hex_digit) {
    char d = tolower(hex_digit);
    if (d >= '0' && d <= '9') {
        if (d == '0') return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
        return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (d - '1');
    }
    if (d >= 'a' && d <= 'f') {
        return HID_USAGE_KEY_KEYBOARD_A + (d - 'a');
    }
    return 0;
}

int start_expansion(struct expansion_work *work_item, const char *expanded_text, uint16_t len_to_delete, uint16_t trigger_keycode) {
    LOG_INF("Starting expansion: text='%s', backspaces=%d, replay_keycode=0x%04X", expanded_text, len_to_delete, trigger_keycode);
    cancel_current_expansion(work_item, false);

    work_item->expanded_text = expanded_text;
    work_item->trigger_keycode_to_replay = trigger_keycode;
    work_item->backspace_count = len_to_delete;
    work_item->text_index = 0;
    work_item->shift_mod_active = false;
    work_item->current_keycode = 0;
    work_item->characters_typed = 0;

    work_item->state = (work_item->backspace_count > 0) ? EXPANSION_STATE_START_BACKSPACE : EXPANSION_STATE_START_TYPING;

    LOG_DBG("Scheduling expansion work, initial state: %d", work_item->state);
    k_work_reschedule(&work_item->work, K_MSEC(EXPANSION_START_DELAY_MS));
    return 0;
}
