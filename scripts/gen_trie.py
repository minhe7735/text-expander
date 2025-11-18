import sys
import argparse
from pathlib import Path
import re

try:
    from devicetree import dtlib
except ImportError:
    print("Error: The 'dtlib' library is required but could not be imported.", file=sys.stderr)
    print("       This may be an issue with the PYTHONPATH environment for the build command.", file=sys.stderr)
    sys.exit(1)

# Sentinel value for a null/invalid index in the generated C code.
NULL_INDEX = 0xFFFF

# Opcodes for expansion engine (Must match include/zmk/expansion_engine.h)
OP_CMD_WIN   = 0x01
OP_CMD_MAC   = 0x02
OP_CMD_LINUX = 0x03

class TrieNode:
    """Represents a node in the trie during the Python build process."""
    def __init__(self):
        self.children = {}
        self.is_terminal = False
        self.expanded_text = None
        self.preserve_trigger = True 
        self.expanded_len_chars = 0

def compile_text_to_bytecode(text):
    """
    Compiles user text into bytecode and returns (bytecode, logical_char_count).
    Handles:
    - Literal blocks: {{{ ... }}}
    - Single escapes: \{
    - OS Commands: {{cmd:win}}, etc. (Zero logical length)
    - Unicode Codepoints: {{u:XXXX}} (One logical char)
    """
    if text is None:
        return b"", 0

    cmd_map = {
        "{{cmd:win}}": bytes([OP_CMD_WIN]),
        "{{cmd:mac}}": bytes([OP_CMD_MAC]),
        "{{cmd:linux}}": bytes([OP_CMD_LINUX]),
    }
    
    result = bytearray()
    logical_len = 0
    i = 0
    length = len(text)
    
    while i < length:
        # 1. Handle Literal Blocks: {{{ content }}}
        if text.startswith("{{{", i):
            end_idx = text.find("}}}", i + 3)
            if end_idx == -1:
                print(f"Warning: Unclosed literal block '{{{{{{' found. Treating remainder as literal.", file=sys.stderr)
                literal_content = text[i+3:]
                result.extend(literal_content.encode('utf-8'))
                logical_len += len(literal_content)
                break
            else:
                literal_content = text[i+3:end_idx]
                result.extend(literal_content.encode('utf-8'))
                logical_len += len(literal_content)
                i = end_idx + 3
                continue

        # 2. Handle Single Escapes: \{ -> {
        if text.startswith(r"\{", i):
             result.extend(b"{")
             logical_len += 1
             i += 2 
             continue

        # 3. Handle OS Commands: {{cmd:xxx}}
        match_found = False
        for cmd_str, opcode_bytes in cmd_map.items():
            if text.startswith(cmd_str, i):
                result.extend(opcode_bytes)
                i += len(cmd_str)
                match_found = True
                # Commands do not count towards the logical length of printed characters
                break
        if match_found:
            continue

        # 4. Handle Unicode Escapes: {{u:XXXX}}
        if text.startswith("{{u:", i):
            end_u = text.find("}}", i + 4)
            if end_u != -1:
                hex_str = text[i+4:end_u]
                try:
                    codepoint = int(hex_str, 16)
                    # Unicode char counts as 1 logical character for backspacing
                    char_str = chr(codepoint)
                    result.extend(char_str.encode('utf-8'))
                    logical_len += 1 
                    i = end_u + 2
                    continue
                except ValueError:
                    print(f"Warning: Invalid hex code '{hex_str}' in {{u:...}} at index {i}. Treating as literal.", file=sys.stderr)
            else:
                 print(f"Warning: Unclosed {{u: tag at index {i}.", file=sys.stderr)

        # 5. Standard Character
        char_bytes = text[i].encode('utf-8')
        result.extend(char_bytes)
        logical_len += 1
        i += 1
        
    return result, logical_len

def build_trie_from_expansions(expansions):
    """Builds a Python-based trie from the dictionary of expansions."""
    root = TrieNode()
    for short_code, expansion_data in expansions.items():
        node = root
        for char in short_code:
            if char not in node.children:
                node.children[char] = TrieNode()
            node = node.children[char]
        node.is_terminal = True
        node.expanded_text = expansion_data['text']
        node.preserve_trigger = expansion_data['preserve_trigger']
    return root

def parse_dts_for_expansions(dts_path_str):
    """Parses the given DTS file to find and extract text expansion definitions."""
    expansions = {}
    try:
        dt = dtlib.DT(dts_path_str)

        def process_expander_node(expander_node):
            global_preserve_default = "disable-preserve-trigger" not in expander_node.props

            for child in expander_node.nodes.values():
                if "short-code" in child.props and "expanded-text" in child.props:
                    short_code = child.props["short-code"].to_string().lower()
                    
                    if ' ' in short_code:
                        print(f"Warning: The short code '{short_code}' contains a space. Skipping.", file=sys.stderr)
                        continue

                    expanded_text = child.props["expanded-text"].to_string()

                    final_preserve_setting = global_preserve_default
                    if "preserve-trigger" in child.props:
                        final_preserve_setting = True
                    elif "disable-preserve-trigger" in child.props:
                        final_preserve_setting = False

                    expansions[short_code] = {
                        "text": expanded_text, 
                        "preserve_trigger": final_preserve_setting
                    }

        for node in dt.node_iter():
            if "compatible" not in node.props:
                continue

            compatible_prop = node.props["compatible"]
            compat_strings = []
            if compatible_prop.type == dtlib.Type.STRING:
                compat_strings.append(compatible_prop.to_string())
            elif compatible_prop.type == dtlib.Type.STRINGS:
                compat_strings.extend(compatible_prop.to_strings())

            if "zmk,behavior-text-expander" in compat_strings:
                process_expander_node(node)

    except Exception as e:
        print(f"Error parsing DTS file with dtlib: {e}", file=sys.stderr)

    return expansions

def get_next_power_of_2(n):
    if n == 0: return 1
    p = 1
    while p < n: p <<= 1
    return p

def escape_for_c_string(byte_data):
    if byte_data is None: return ""
    result = []
    for byte in byte_data:
        if byte == ord('"'): result.append('\\"')
        elif byte == ord('\\'): result.append('\\\\')
        elif byte == ord('\n'): result.append('\\n')
        elif byte == ord('\t'): result.append('\\t')
        elif byte == ord('\r'): result.append('\\r')
        elif 32 <= byte <= 126: result.append(chr(byte))
        else: result.append(f'\\{byte:03o}')
    return "".join(result)

def generate_static_trie_c_code(expansions):
    if not expansions:
        return """
#include <zmk/trie.h>
#include <stddef.h>
const uint16_t zmk_text_expander_trie_num_nodes = 0;
const struct trie_node zmk_text_expander_trie_nodes[] = {};
const struct trie_hash_table zmk_text_expander_hash_tables[] = {};
const struct trie_hash_entry zmk_text_expander_hash_entries[] = {};
const uint16_t zmk_text_expander_hash_buckets[] = {};
const char zmk_text_expander_string_pool[] = "";
const char *zmk_text_expander_get_string(uint16_t offset) { return NULL; }
"""
    root = build_trie_from_expansions(expansions)

    string_pool_builder = bytearray()
    c_trie_nodes, c_hash_tables, c_hash_buckets, c_hash_entries = [], [], [], []
    node_q, node_map = [root], {id(root): 0}

    head = 0
    while head < len(node_q):
        py_node = node_q[head]
        head += 1
        for child in sorted(py_node.children.values(), key=id): 
            if id(child) not in node_map:
                node_map[id(child)] = len(node_map)
                node_q.append(child)

    c_trie_nodes = [None] * len(node_q)
    for py_node in node_q:
        c_trie_nodes[node_map[id(py_node)]] = py_node

    for py_node in c_trie_nodes:
        hash_table_index = NULL_INDEX
        if py_node.children:
            hash_table_index = len(c_hash_tables)
            num_children = len(py_node.children)
            num_buckets = get_next_power_of_2(num_children) if num_children > 1 else 1
            buckets_start_index = len(c_hash_buckets)
            buckets = [NULL_INDEX] * num_buckets
            c_hash_tables.append({"buckets_start_index": buckets_start_index, "num_buckets": num_buckets})

            for char, child_py_node in sorted(py_node.children.items()):
                hash_val = ord(char) % num_buckets
                child_node_index = node_map[id(child_py_node)]
                new_entry_index = len(c_hash_entries)
                next_entry_index = buckets[hash_val]
                c_hash_entries.append({"key": char, "child_node_index": child_node_index, "next_entry_index": next_entry_index})
                buckets[hash_val] = new_entry_index
            c_hash_buckets.extend(buckets)

        expanded_text_offset = NULL_INDEX
        expanded_len_chars = 0
        
        if py_node.is_terminal:
            current_pool_pos = len(string_pool_builder)
            if current_pool_pos > 65535:
                 print("Error: String pool exceeds 64KB limit (uint16_t overflow).", file=sys.stderr)
                 sys.exit(1)
            
            bytecode, expanded_len_chars = compile_text_to_bytecode(py_node.expanded_text)
            string_pool_builder.extend(bytecode)
            string_pool_builder.append(0) 
            expanded_text_offset = current_pool_pos

        py_node.c_struct_data = {
            "hash_table_index": hash_table_index,
            "expanded_text_offset": expanded_text_offset,
            "expanded_len_chars": expanded_len_chars,
            "is_terminal": 1 if py_node.is_terminal else 0,
            "preserve_trigger": 1 if py_node.preserve_trigger else 0,
        }

    c_parts = ["#include <zmk/trie.h>\n#include <stddef.h> // For NULL\n\n"]
    c_parts.append(f"const uint16_t zmk_text_expander_trie_num_nodes = {len(c_trie_nodes)};\n\n")

    escaped_string_pool = escape_for_c_string(string_pool_builder)
    c_parts.append(f'const char zmk_text_expander_string_pool[] = "{escaped_string_pool}";\n\n')

    c_parts.append("const struct trie_node zmk_text_expander_trie_nodes[] = {\n")
    for py_node in c_trie_nodes:
        d = py_node.c_struct_data
        c_parts.append(f"    {{ .hash_table_index = {d['hash_table_index']}, .expanded_text_offset = {d['expanded_text_offset']}, .expanded_len_chars = {d['expanded_len_chars']}, .is_terminal = {d['is_terminal']}, .preserve_trigger = {d['preserve_trigger']} }},\n")
    c_parts.append("};\n\n")

    c_parts.append("const struct trie_hash_table zmk_text_expander_hash_tables[] = {\n")
    for ht in c_hash_tables:
        c_parts.append(f"    {{ .buckets_start_index = {ht['buckets_start_index']}, .num_buckets = {ht['num_buckets']} }},\n")
    c_parts.append("};\n\n")

    c_parts.append("const uint16_t zmk_text_expander_hash_buckets[] = {\n    " + ", ".join(map(str, c_hash_buckets)) + "\n};\n\n")

    c_parts.append("const struct trie_hash_entry zmk_text_expander_hash_entries[] = {\n")
    for entry in c_hash_entries:
        escaped_key = entry['key'].replace('\\', '\\\\').replace("'", "\\'")
        c_parts.append(f"    {{ .key = '{escaped_key}', .child_node_index = {entry['child_node_index']}, .next_entry_index = {entry['next_entry_index']} }},\n")
    c_parts.append("};\n\n")

    c_parts.append("const char *zmk_text_expander_get_string(uint16_t offset) {\n")
    c_parts.append("    if (offset >= sizeof(zmk_text_expander_string_pool)) return NULL;\n")
    c_parts.append("    return &zmk_text_expander_string_pool[offset];\n}\n")

    return "".join(c_parts)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate ZMK Text Expander trie C code from DeviceTree.")
    parser.add_argument("build_dir", help="The build directory containing zephyr.dts")
    parser.add_argument("output_c", help="Output C file path")
    parser.add_argument("output_h", help="Output H file path")
    
    args = parser.parse_args()

    build_path = Path(args.build_dir)
    dts_files = list(build_path.rglob('zephyr.dts'))

    if not dts_files:
        print(f"Error: Processed devicetree source (zephyr.dts) not found in '{args.build_dir}'", file=sys.stderr)
        Path(args.output_c).touch()
        Path(args.output_h).touch()
        sys.exit(1)

    dts_path = dts_files[0]
    expansions = parse_dts_for_expansions(str(dts_path))

    c_code = generate_static_trie_c_code(expansions)
    with open(args.output_c, 'w', encoding='utf-8') as f:
        f.write(c_code)

    longest_short_len = len(max(expansions.keys(), key=len)) if expansions else 0
    h_file_content = f"""
#pragma once
// Automatically generated file. Do not edit.
#define ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN {longest_short_len}
"""
    with open(args.output_h, 'w', encoding='utf-8') as f:
        f.write(h_file_content)
