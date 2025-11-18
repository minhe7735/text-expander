#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/text_expander.h>
#include <zmk/trie.h>
#include <zmk/expansion_engine.h>
#include <zmk/keymap_utils.h>

LOG_MODULE_REGISTER(text_expander, LOG_LEVEL_DBG);

#define EXPANDER_INST DT_DRV_INST(0)

#define NO_REPLAY_KEY 0
#define ZMK_HID_USAGE_ID_MASK 0xFFFF

static uint16_t extract_hid_usage(uint32_t zmk_hid_usage) {
    return (uint16_t)(zmk_hid_usage & ZMK_HID_USAGE_ID_MASK);
}

static const uint32_t reset_keycodes[] = DT_INST_PROP_OR(0, reset_keycodes, {});
static const uint32_t auto_expand_keycodes[] = DT_INST_PROP_OR(0, auto_expand_keycodes, {});
static const uint32_t ignored_keycodes[] = DT_INST_PROP_OR(0, ignore_keycodes, {});

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static const uint32_t undo_keycodes[] = DT_INST_PROP_OR(0, undo_keycodes, {});
#endif

struct text_expander_data expander_data;

static void process_event(struct text_expander_event *ev);
static bool handle_undo(uint16_t keycode);
static void handle_alphanumeric(char next_char);
static void handle_backspace();
static void handle_auto_expand(uint16_t keycode);
static void handle_reset_buffer_check();
static bool trigger_expansion(const char *short_code, enum expansion_context context, uint16_t trigger_keycode);
#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static void save_undo_state(const char *short_code, size_t short_len, uint16_t expanded_len, uint16_t trigger_keycode, bool is_completion);
#endif

void text_expander_processor_work_handler(struct k_work *work);
K_WORK_DEFINE(text_expander_processor_work, text_expander_processor_work_handler);

static bool keycode_in_array(uint16_t keycode, const uint32_t* arr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint16_t array_keycode = extract_hid_usage(arr[i]);
        if (array_keycode == keycode) {
            return true;
        }
    }
    return false;
}



/**
 * @brief Resets the current short code buffer to empty state.
 * Clears the buffer and sets length to zero.
 */
static void reset_current_short(void) {
    LOG_DBG("Resetting current short code. Was: '%s'", expander_data.current_short);
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
}

/**
 * @brief Appends a character to the current short code buffer.
 * @param c Character to add
 * 
 * Adds the character if space is available, otherwise logs a warning.
 */
static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0';
    } else {
        LOG_WRN("Short code buffer full at length %d. Ignoring character '%c'.", expander_data.current_short_len, c);
    }
}

static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) { return ZMK_EV_EVENT_BUBBLE; }

    // Queue the event - state checking will happen in the work handler
    // This avoids race conditions with concurrent state access
    struct text_expander_event ev_msg = {
        .type = TE_EV_KEY_PRESS,
        .keycode = ev->keycode,
        .pressed = ev->state
    };

    if (k_msgq_put(&expander_data.event_msgq, &ev_msg, K_NO_WAIT) != 0) {
        LOG_WRN("Failed to queue key event for keycode 0x%04X", ev->keycode);
    } else {
        k_work_submit(&text_expander_processor_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

void text_expander_processor_work_handler(struct k_work *work) {
    struct text_expander_event ev;
    
    // Hoist mutex locking to cover the entire batch of events
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    while (k_msgq_get(&expander_data.event_msgq, &ev, K_NO_WAIT) == 0) {
        process_event(&ev);
    }
    k_mutex_unlock(&expander_data.mutex);
}

static void handle_manual_trigger_event(void) {
    if (expander_data.current_short_len > 0) {
        if (!trigger_expansion(expander_data.current_short, EXPAND_FROM_MANUAL_TRIGGER, NO_REPLAY_KEY)) {
            reset_current_short();
        }
    }
}

static void handle_key_press_event(struct text_expander_event *ev) {
    if (handle_undo(ev->keycode)) {
        return;
    }

    char next_char = keycode_to_short_code_char(ev->keycode);
    if (next_char != '\0') {
        handle_alphanumeric(next_char);
    } else if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        handle_backspace();
    } else if (keycode_in_array(ev->keycode, auto_expand_keycodes, ARRAY_SIZE(auto_expand_keycodes))) {
        handle_auto_expand(ev->keycode);
    } else if (keycode_in_array(ev->keycode, reset_keycodes, ARRAY_SIZE(reset_keycodes))) {
        handle_reset_buffer_check();
    } else if (keycode_in_array(ev->keycode, ignored_keycodes, ARRAY_SIZE(ignored_keycodes))) {
        // Ignore
    } else {
        handle_reset_buffer_check();
    }
}

static void process_event(struct text_expander_event *ev) {
    // Mutex is already held by the caller (text_expander_processor_work_handler)
    
    // Handle reset/undo keys during expansion (now mutex-protected)
    if (expander_data.expansion_work_item.state != EXPANSION_STATE_IDLE) {
        if (ev->type == TE_EV_KEY_PRESS && ev->pressed) {
            #if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
            if (keycode_in_array(ev->keycode, undo_keycodes, ARRAY_SIZE(undo_keycodes))) {
                LOG_DBG("Undo key pressed during expansion, starting partial undo.");
                
                // Capture state BEFORE canceling, as cancel resets these values
                uint16_t current_chars_typed = expander_data.expansion_work_item.characters_typed;
                uint16_t current_backspace_count = expander_data.expansion_work_item.backspace_count;
                enum expansion_state current_state = expander_data.expansion_work_item.state;

                // Adjust for pending actions that cancel_current_expansion will complete
                if (current_state == EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE) {
                    // Key is pressed, cancel will release it, so it counts as typed
                    current_chars_typed++;
                } else if (current_state == EXPANSION_STATE_BACKSPACE_RELEASE) {
                    // Backspace is pressed, cancel will release it, so it counts as done
                    if (current_backspace_count > 0) {
                        current_backspace_count--;
                    }
                }

                cancel_current_expansion(&expander_data.expansion_work_item, false);

                uint16_t cleanup_backspaces = 0;

                if (current_chars_typed > 0) {
                    cleanup_backspaces = current_chars_typed;
                } else {
                    cleanup_backspaces = current_backspace_count;
                }

                const char* short_code_to_restore = expander_data.last_short_code;
                if (expander_data.last_expansion_was_completion) {
                    short_code_to_restore = "";
                }
                reset_current_short();
                start_expansion(&expander_data.expansion_work_item, short_code_to_restore, cleanup_backspaces, NO_REPLAY_KEY);
                return;
            }
            #endif

            if (keycode_in_array(ev->keycode, reset_keycodes, ARRAY_SIZE(reset_keycodes))) {
                LOG_DBG("Reset key pressed, canceling in-progress expansion.");
                cancel_current_expansion(&expander_data.expansion_work_item, false);
                return;
            }
        }
        // During expansion, ignore other key events
        return;
    }

    if (ev->type == TE_EV_MANUAL_TRIGGER) {
        handle_manual_trigger_event();
        return;
    }

    if (ev->type == TE_EV_KEY_PRESS) {
        if (ev->pressed) {
            if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
                expander_data.backspace_press_time = k_uptime_get();
            }
            handle_key_press_event(ev);
        } else {
            // Handle key releases
            if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
                if (k_uptime_get() - expander_data.backspace_press_time > BACKSPACE_RESET_TIMEOUT_MS) {
                    LOG_INF("Backspace held > %dms, resetting buffer to avoid desync.", BACKSPACE_RESET_TIMEOUT_MS);
                    reset_current_short();
                }
            }
        }
    }
}

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static bool handle_undo(uint16_t keycode) {
    if (expander_data.just_expanded) {
        expander_data.just_expanded = false;
        if (keycode_in_array(keycode, undo_keycodes, ARRAY_SIZE(undo_keycodes))) {
            
            uint16_t undo_backspaces = expander_data.last_expanded_len;
            
            if (expander_data.last_trigger_keycode != 0) {
                undo_backspaces++;
            }
            LOG_INF("Undo triggered. Restoring '%s', backspacing %d", expander_data.last_short_code, undo_backspaces);
            
            reset_current_short();
            start_expansion(&expander_data.expansion_work_item, expander_data.last_short_code, undo_backspaces, NO_REPLAY_KEY);
            return true;
        }
    }
    return false;
}
#else
static bool handle_undo(uint16_t keycode) { return false; }
#endif

/**
 * @brief Handles alphanumeric character input and manages aggressive reset mode.
 * @param next_char The character to process
 * 
 * Adds character to short code buffer. In aggressive reset mode, validates
 * against trie and resets if no matching prefix exists.
 */
static void handle_alphanumeric(char next_char) {
    add_to_current_short(next_char);
    #ifdef CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE
    if (expander_data.current_short_len > 0) {
        if (!trie_get_node_for_key(expander_data.current_short)) {
            reset_current_short();
            #ifdef CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR
            add_to_current_short(next_char);
            #endif
        }
    }
    #endif
}

static void handle_backspace() {
    if (expander_data.current_short_len > 0) {
        // Always remove at least one byte
        expander_data.current_short_len--;
        
        // If we just removed a continuation byte (10xxxxxx), keep removing until we remove the lead byte (11xxxxxx)
        // Continuation byte: (byte & 0xC0) == 0x80
        // Lead byte: (byte & 0xC0) != 0x80 (usually 110xxxxx, 1110xxxx, 11110xxx)
        // ASCII: 0xxxxxxx (so (byte & 0xC0) == 0x00, which is != 0x80)
        
        while (expander_data.current_short_len > 0 && 
              ((uint8_t)expander_data.current_short[expander_data.current_short_len] & 0xC0) == 0x80) {
            expander_data.current_short_len--;
        }
        
        expander_data.current_short[expander_data.current_short_len] = '\0';
    }
}

static void handle_auto_expand(uint16_t keycode) {
    if (expander_data.current_short_len > 0) {
        if (!trigger_expansion(expander_data.current_short, EXPAND_FROM_AUTO_TRIGGER, keycode)) {
            reset_current_short();
        }
    }
}

static void handle_reset_buffer_check() {
    if (expander_data.current_short_len > 0) {
        reset_current_short();
    }
}

/**
 * @brief Triggers an expansion for the given short code.
 * @param short_code The short code to expand
 * @param context Whether triggered manually or automatically
 * @param trigger_keycode The keycode that triggered the expansion (e.g., Space)
 * @return true if expansion was triggered, false if short code not found
 * 
 * Looks up the short code in the trie, determines if it's a completion or replacement,
 * saves undo state, and starts the expansion engine.
 */
static bool trigger_expansion(const char *short_code, enum expansion_context context, uint16_t trigger_keycode) {
    const struct trie_node *node = trie_search(short_code);
    if (!node) return false;

    const char *expanded_ptr = zmk_text_expander_get_string(node->expanded_text_offset);
    if (!expanded_ptr) return false;

    size_t short_len = strlen(short_code);
    uint16_t len_to_delete = short_len + (context == EXPAND_FROM_AUTO_TRIGGER ? 1 : 0);
    const char *text_for_engine = expanded_ptr;

    bool is_completion = (strncmp(expanded_ptr, short_code, short_len) == 0);

    if (is_completion) {
        text_for_engine = expanded_ptr + short_len;
        len_to_delete = (context == EXPAND_FROM_AUTO_TRIGGER ? 1 : 0);
    }

    uint16_t keycode_to_replay = node->preserve_trigger ? trigger_keycode : NO_REPLAY_KEY;

    #if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
    save_undo_state(short_code, short_len, node->expanded_len_chars, keycode_to_replay, is_completion);
    #endif

    reset_current_short();
    start_expansion(&expander_data.expansion_work_item, text_for_engine, len_to_delete, keycode_to_replay);
    return true;
}

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static void save_undo_state(const char *short_code, size_t short_len, uint16_t expanded_len, uint16_t trigger_keycode, bool is_completion) {
    memset(expander_data.last_short_code, 0, MAX_SHORT_LEN);
    size_t copy_len = short_len >= MAX_SHORT_LEN ? MAX_SHORT_LEN - 1 : short_len;
    memcpy(expander_data.last_short_code, short_code, copy_len);
    
    expander_data.last_expanded_len = expanded_len; 
    expander_data.last_trigger_keycode = trigger_keycode;
    expander_data.just_expanded = true;
    expander_data.last_expansion_was_completion = is_completion;
}
#endif

static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event binding_event) {
    struct text_expander_event ev = {
        .type = TE_EV_MANUAL_TRIGGER,
        .keycode = 0,
        .pressed = true
    };
    if (k_msgq_put(&expander_data.event_msgq, &ev, K_NO_WAIT) != 0) {
        LOG_WRN("Failed to queue manual trigger event");
    } else {
        k_work_submit(&text_expander_processor_work);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event binding_event) {
    return ZMK_BEHAVIOR_TRANSPARENT;
}

ZMK_LISTENER(text_expander_listener_interface, text_expander_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(text_expander_listener_interface, zmk_keycode_state_changed);

static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
};

static int text_expander_init(const struct device *dev) {
    static bool initialized = false;
    if (initialized) { return 0; }

    k_mutex_init(&expander_data.mutex);
    k_msgq_init(&expander_data.event_msgq, expander_data.event_msgq_buffer, sizeof(struct text_expander_event), KEY_EVENT_QUEUE_SIZE);
    reset_current_short();

    #if CONFIG_ZMK_TEXT_EXPANDER_DEFAULT_OS_LINUX
        extern const struct os_typing_driver linux_driver;
        expander_data.os_driver = &linux_driver;
    #elif CONFIG_ZMK_TEXT_EXPANDER_DEFAULT_OS_MACOS
        extern const struct os_typing_driver mac_driver;
        expander_data.os_driver = &mac_driver;
    #else 
        extern const struct os_typing_driver win_driver;
        expander_data.os_driver = &win_driver;
    #endif

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
    expander_data.just_expanded = false;
    expander_data.last_expanded_len = 0;
    expander_data.last_trigger_keycode = 0;
    memset(expander_data.last_short_code, 0, MAX_SHORT_LEN);
#endif

    if (zmk_text_expander_trie_num_nodes > 0) {
         expander_data.root = &zmk_text_expander_trie_nodes[0];
    }

    k_work_init_delayable(&expander_data.expansion_work_item.work, expansion_work_handler);
    initialized = true;
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, text_expander_init, NULL, &expander_data, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &text_expander_driver_api);
