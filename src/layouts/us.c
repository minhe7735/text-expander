#include <zmk/layouts_common.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(keymap_utils_us, LOG_LEVEL_DBG);

// --- INPUT MAPPING (Keycode -> Char) ---

static const char hid_to_char_map[MAP_SIZE] = {
    [HID_USAGE_KEY_KEYBOARD_A ... HID_USAGE_KEY_KEYBOARD_Z] = 'a', 
    [HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION ... HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS] = '1',
    [HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS] = '0',
    [HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE] = '-',
    [HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS] = '=',
    [HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE] = '[',
    [HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE] = ']',
    [HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE] = '\\',
    [HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON] = ';',
    [HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE] = '\'',
    [HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE] = '`',
    [HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN] = ',',
    [HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN] = '.',
    [HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK] = '/',
};

char keycode_to_short_code_char(uint16_t keycode) {
    if (keycode >= MAP_SIZE) return '\0';

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
            return 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    }
    if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
            return '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) return '0';

    return hid_to_char_map[keycode];
}

// --- OUTPUT MAPPING (Char -> Keycode) ---

static const keycode_map_entry_t keycode_lut[KEYCODE_LUT_SIZE] __attribute__((section(".rodata"))) = {
    [IDX(' ')] = MAP_U(SPACEBAR),
    [IDX('!')] = MAP_S(1_AND_EXCLAMATION),
    [IDX('"')] = MAP_S(APOSTROPHE_AND_QUOTE),
    [IDX('#')] = MAP_S(3_AND_HASH),
    [IDX('$')] = MAP_S(4_AND_DOLLAR),
    [IDX('%')] = MAP_S(5_AND_PERCENT),
    [IDX('&')] = MAP_S(7_AND_AMPERSAND),
    [IDX('\'')] = MAP_U(APOSTROPHE_AND_QUOTE),
    [IDX('(')] = MAP_S(9_AND_LEFT_PARENTHESIS),
    [IDX(')')] = MAP_S(0_AND_RIGHT_PARENTHESIS),
    [IDX('*')] = MAP_S(8_AND_ASTERISK),
    [IDX('+')] = MAP_S(EQUAL_AND_PLUS),
    [IDX(',')] = MAP_U(COMMA_AND_LESS_THAN),
    [IDX('-')] = MAP_U(MINUS_AND_UNDERSCORE),
    [IDX('.')] = MAP_U(PERIOD_AND_GREATER_THAN),
    [IDX('/')] = MAP_U(SLASH_AND_QUESTION_MARK),
    [IDX('0')] = MAP_U(0_AND_RIGHT_PARENTHESIS),
    [IDX('1')] = MAP_U(1_AND_EXCLAMATION),
    [IDX('2')] = MAP_U(2_AND_AT),
    [IDX('3')] = MAP_U(3_AND_HASH),
    [IDX('4')] = MAP_U(4_AND_DOLLAR),
    [IDX('5')] = MAP_U(5_AND_PERCENT),
    [IDX('6')] = MAP_U(6_AND_CARET),
    [IDX('7')] = MAP_U(7_AND_AMPERSAND),
    [IDX('8')] = MAP_U(8_AND_ASTERISK),
    [IDX('9')] = MAP_U(9_AND_LEFT_PARENTHESIS),
    [IDX(':')] = MAP_S(SEMICOLON_AND_COLON),
    [IDX(';')] = MAP_U(SEMICOLON_AND_COLON),
    [IDX('<')] = MAP_S(COMMA_AND_LESS_THAN),
    [IDX('=')] = MAP_U(EQUAL_AND_PLUS),
    [IDX('>')] = MAP_S(PERIOD_AND_GREATER_THAN),
    [IDX('?')] = MAP_S(SLASH_AND_QUESTION_MARK),
    [IDX('@')] = MAP_S(2_AND_AT),
    [IDX('[')] = MAP_U(LEFT_BRACKET_AND_LEFT_BRACE),
    [IDX('\\')] = MAP_U(BACKSLASH_AND_PIPE),
    [IDX(']')] = MAP_U(RIGHT_BRACKET_AND_RIGHT_BRACE),
    [IDX('^')] = MAP_S(6_AND_CARET),
    [IDX('_')] = MAP_S(MINUS_AND_UNDERSCORE),
    [IDX('`')] = MAP_U(GRAVE_ACCENT_AND_TILDE),
    [IDX('{')] = MAP_S(LEFT_BRACKET_AND_LEFT_BRACE),
    [IDX('|')] = MAP_S(BACKSLASH_AND_PIPE),
    [IDX('}')] = MAP_S(RIGHT_BRACKET_AND_RIGHT_BRACE),
    [IDX('~')] = MAP_S(GRAVE_ACCENT_AND_TILDE),
};

uint32_t char_to_keycode(char c, bool *needs_shift) {
    *needs_shift = false;
    uint32_t keycode = 0;
    
    unsigned char uc = (unsigned char)c;

    if (uc < KEYCODE_LUT_OFFSET || uc >= (KEYCODE_LUT_OFFSET + KEYCODE_LUT_SIZE)) {
        if (c == '\n') { return HID_USAGE_KEY_KEYBOARD_RETURN_ENTER; }
        if (c == '\t') { return HID_USAGE_KEY_KEYBOARD_TAB; }
        if (c == '\b') { return HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE; }
        LOG_WRN("Character '%c' (0x%02X) out of lookup table range", c, uc);
        return 0;
    }

    const keycode_map_entry_t *entry = &keycode_lut[uc - KEYCODE_LUT_OFFSET];
    
    if (entry->keycode != 0) {
        *needs_shift = entry->needs_shift;
        keycode = entry->keycode;
        return keycode;
    }

    if (c >= 'a' && c <= 'z') {
        *needs_shift = false;
        keycode = HID_USAGE_KEY_KEYBOARD_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *needs_shift = true;
        keycode = HID_USAGE_KEY_KEYBOARD_A + (c - 'A');
    }

    LOG_DBG("Converted '%c' to keycode 0x%04X with shift %s", c, keycode, *needs_shift ? "true" : "false");
    return keycode;
}
