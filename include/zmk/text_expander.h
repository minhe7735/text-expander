#ifndef ZMK_TEXT_EXPANDER_H
#define ZMK_TEXT_EXPANDER_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#include <zmk/trie.h>
#include <zmk/expansion_engine.h>
#include "generated_trie.h"

#if ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN > 0
#define MAX_SHORT_LEN (ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN + 1)
#else
#define MAX_SHORT_LEN 16
#endif

#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define KEY_EVENT_QUEUE_SIZE CONFIG_ZMK_TEXT_EXPANDER_EVENT_QUEUE_SIZE
#define BACKSPACE_RESET_TIMEOUT_MS 500

enum expansion_context {
    EXPAND_FROM_AUTO_TRIGGER,
    EXPAND_FROM_MANUAL_TRIGGER,
};

enum text_expander_event_type {
    TE_EV_KEY_PRESS,
    TE_EV_MANUAL_TRIGGER,
};

struct text_expander_event {
    enum text_expander_event_type type;
    uint16_t keycode;
    bool pressed;
};

struct text_expander_data {
  const struct trie_node *root;
  char current_short[MAX_SHORT_LEN];
  uint8_t current_short_len;
  struct k_mutex mutex;
  struct expansion_work expansion_work_item;
  struct k_msgq event_msgq;
  char event_msgq_buffer[KEY_EVENT_QUEUE_SIZE * sizeof(struct text_expander_event)];
  const struct os_typing_driver *os_driver;
  int64_t backspace_press_time;

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
  char last_short_code[MAX_SHORT_LEN];
  uint16_t last_expanded_len;
  uint16_t last_trigger_keycode;
  bool just_expanded;
  bool last_expansion_was_completion;
#endif
};

extern struct text_expander_data expander_data;

#endif /* ZMK_TEXT_EXPANDER_H */
