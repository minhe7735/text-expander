#include <zephyr/logging/log.h>
#include <zmk/trie.h>
#include <stddef.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

static const struct trie_node *get_node(uint16_t index) {
    if (index >= zmk_text_expander_trie_num_nodes) {
        LOG_WRN("Node index %u out of bounds.", index);
        return NULL;
    }
    return &zmk_text_expander_trie_nodes[index];
}

/**
 * @brief Traverse the trie to find the node corresponding to a given key.
 * @param key The key string to search for
 * @return Pointer to the trie node if found, NULL otherwise
 * 
 * Performance: Uses actual key length for iteration instead of hardcoded bound.
 * Safety: 256-char maximum enforced to prevent infinite loops on malformed keys.
 */
const struct trie_node *trie_get_node_for_key(const char *key) {
    if (!key || zmk_text_expander_trie_num_nodes == 0) {
        return NULL;
    }

    const struct trie_node *current_node = get_node(0);
    if (!current_node) {
        LOG_ERR("Root node (index 0) is invalid.");
        return NULL;
    }

    // Optimization: use actual key length for loop bound
    // Safety: cap at 256 chars to prevent infinite loops on malformed keys
    size_t key_len = strlen(key);
    if (key_len > 256) {
        LOG_WRN("Key length %zu exceeds safety limit of 256 chars", key_len);
        key_len = 256;
    }
    
    for (size_t i = 0; i < key_len; i++) {
        char current_char = key[i];

        if (current_node->hash_table_index == NULL_INDEX) {
            return NULL;
        }

        const struct trie_hash_table *ht = &zmk_text_expander_hash_tables[current_node->hash_table_index];
        if (ht->num_buckets == 0) {
            return NULL;
        }

        uint8_t bucket_index = (uint8_t)((unsigned char)current_char % ht->num_buckets);

        uint16_t entry_index = zmk_text_expander_hash_buckets[ht->buckets_start_index + bucket_index];

        bool found_child = false;
        while (entry_index != NULL_INDEX) {
            const struct trie_hash_entry *entry = &zmk_text_expander_hash_entries[entry_index];
            if (entry->key == current_char) {
                current_node = get_node(entry->child_node_index);
                found_child = true;
                break;
            }
            entry_index = entry->next_entry_index;
        }

        if (!found_child) {
            return NULL;
        }
    }

    return current_node;
}

const struct trie_node *trie_search(const char *key) {
    LOG_DBG("trie_search called for key: \"%s\"", key);
    const struct trie_node *node = trie_get_node_for_key(key);
    if (node && node->is_terminal) {
        LOG_DBG("Node found for key and it is a terminal node. Search successful.");
        return node;
    }
    LOG_DBG("Node not found or not a terminal node. Search failed.");
    return NULL;
}
