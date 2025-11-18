#ifndef ZMK_TRIE_H
#define ZMK_TRIE_H

#include <stdbool.h>
#include <stdint.h>

#define NULL_INDEX UINT16_MAX

struct trie_hash_entry {
    char key;
    uint16_t child_node_index;
    uint16_t next_entry_index;
};

struct trie_hash_table {
    uint16_t buckets_start_index;
    uint8_t num_buckets;
};

struct trie_node {
    uint16_t hash_table_index;
    uint16_t expanded_text_offset;
    uint16_t expanded_len_chars;
    bool is_terminal;
    bool preserve_trigger;
};

extern const uint16_t zmk_text_expander_trie_num_nodes;
extern const struct trie_node zmk_text_expander_trie_nodes[];
extern const struct trie_hash_table zmk_text_expander_hash_tables[];
extern const struct trie_hash_entry zmk_text_expander_hash_entries[];
extern const uint16_t zmk_text_expander_hash_buckets[];
extern const char zmk_text_expander_string_pool[];

const char *zmk_text_expander_get_string(uint16_t offset);
const struct trie_node *trie_search(const char *key);
const struct trie_node *trie_get_node_for_key(const char *key);

#endif /* ZMK_TRIE_H */
