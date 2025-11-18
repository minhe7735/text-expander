#include <zmk/layouts_common.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(keymap_utils_german, LOG_LEVEL_DBG);

// --- INPUT MAPPING (Keycode -> Char) ---

static const char hid_to_char_map[MAP_SIZE] = {
    [0x04 ... 0x1B] = 'a', 
    [0x1C] = 'z', [0x1D] = 'y',
    [0x1E ... 0x27] = '1', 
    [0x2D] = '/', [0x33] = 'o', [0x34] = 'a', [0x2F] = 'u', [0x30] = '+', 
    [0x31] = '#', [0x36] = ',', [0x37] = '.', [0x38] = '-', 
};

char keycode_to_short_code_char(uint16_t keycode) {
    if (keycode >= MAP_SIZE) return '\0';

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
            if (keycode == 0x1C) return 'z';
            if (keycode == 0x1D) return 'y';
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
    [IDX('"')] = MAP_S(2_AND_AT),
    [IDX('#')] = MAP_U(NON_US_HASH),
    [IDX('$')] = MAP_S(4_AND_DOLLAR),
    [IDX('%')] = MAP_S(5_AND_PERCENT),
    [IDX('&')] = MAP_S(6_AND_CARET),
    [IDX('\'')] = MAP_S(NON_US_HASH),
    [IDX('(')] = MAP_S(8_AND_ASTERISK),
    [IDX(')')] = MAP_S(9_AND_LEFT_PARENTHESIS),
    [IDX('*')] = MAP_S(RIGHT_BRACKET_AND_RIGHT_BRACE),
    [IDX('+')] = MAP_U(RIGHT_BRACKET_AND_RIGHT_BRACE),
    [IDX(',')] = MAP_U(COMMA_AND_LESS_THAN),
    [IDX('-')] = MAP_U(SLASH_AND_QUESTION_MARK),
    [IDX('.')] = MAP_U(PERIOD_AND_GREATER_THAN),
    [IDX('/')] = MAP_S(7_AND_AMPERSAND),
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
    [IDX(':')] = MAP_S(PERIOD_AND_GREATER_THAN),
    [IDX(';')] = MAP_S(COMMA_AND_LESS_THAN),
    [IDX('<')] = MAP_U(NON_US_BACKSLASH_AND_PIPE),
    [IDX('=')] = MAP_S(0_AND_RIGHT_PARENTHESIS),
    [IDX('>')] = MAP_S(NON_US_BACKSLASH_AND_PIPE),
    [IDX('?')] = MAP_S(MINUS_AND_UNDERSCORE),
    [IDX('@')] = MAP_S(Q),
    [IDX('[')] = MAP_S(8_AND_ASTERISK),
    [IDX('\\')] = MAP_S(MINUS_AND_UNDERSCORE),
    [IDX(']')] = MAP_S(9_AND_LEFT_PARENTHESIS),
    [IDX('^')] = MAP_U(GRAVE_ACCENT_AND_TILDE),
    [IDX('_')] = MAP_S(SLASH_AND_QUESTION_MARK),
    [IDX('`')] = MAP_S(EQUAL_AND_PLUS),
    [IDX('{')] = MAP_S(7_AND_AMPERSAND),
    [IDX('|')] = MAP_S(NON_US_BACKSLASH_AND_PIPE),
    [IDX('}')] = MAP_S(0_AND_RIGHT_PARENTHESIS),
    [IDX('~')] = MAP_S(RIGHT_BRACKET_AND_RIGHT_BRACE),
    [IDX('a')] = MAP_U(A), [IDX('A')] = MAP_S(A),
    [IDX('b')] = MAP_U(B), [IDX('B')] = MAP_S(B),
    [IDX('c')] = MAP_U(C), [IDX('C')] = MAP_S(C),
    [IDX('d')] = MAP_U(D), [IDX('D')] = MAP_S(D),
    [IDX('e')] = MAP_U(E), [IDX('E')] = MAP_S(E),
    [IDX('f')] = MAP_U(F), [IDX('F')] = MAP_S(F),
    [IDX('g')] = MAP_U(G), [IDX('G')] = MAP_S(G),
    [IDX('h')] = MAP_U(H), [IDX('H')] = MAP_S(H),
    [IDX('i')] = MAP_U(I), [IDX('I')] = MAP_S(I),
    [IDX('j')] = MAP_U(J), [IDX('J')] = MAP_S(J),
    [IDX('k')] = MAP_U(K), [IDX('K')] = MAP_S(K),
    [IDX('l')] = MAP_U(L), [IDX('L')] = MAP_S(L),
    [IDX('m')] = MAP_U(M), [IDX('M')] = MAP_S(M),
    [IDX('n')] = MAP_U(N), [IDX('N')] = MAP_S(N),
    [IDX('o')] = MAP_U(O), [IDX('O')] = MAP_S(O),
    [IDX('p')] = MAP_U(P), [IDX('P')] = MAP_S(P),
    [IDX('q')] = MAP_U(Q), [IDX('Q')] = MAP_S(Q),
    [IDX('r')] = MAP_U(R), [IDX('R')] = MAP_S(R),
    [IDX('s')] = MAP_U(S), [IDX('S')] = MAP_S(S),
    [IDX('t')] = MAP_U(T), [IDX('T')] = MAP_S(T),
    [IDX('u')] = MAP_U(U), [IDX('U')] = MAP_S(U),
    [IDX('v')] = MAP_U(V), [IDX('V')] = MAP_S(V),
    [IDX('w')] = MAP_U(W), [IDX('W')] = MAP_S(W),
    [IDX('x')] = MAP_U(X), [IDX('X')] = MAP_S(X),
    [IDX('y')] = MAP_U(Z), [IDX('Y')] = MAP_S(Z),
    [IDX('z')] = MAP_U(Y), [IDX('Z')] = MAP_S(Y),
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

    LOG_DBG("Converted '%c' to keycode 0x%04X with shift %s", c, keycode, *needs_shift ? "true" : "false");
    return keycode;
}
