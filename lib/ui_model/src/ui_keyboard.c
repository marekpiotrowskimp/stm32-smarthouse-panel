/* Klawiatura ekranowa z polskimi znakami (spec: virtual-groups, control-polish). */
#include "ui_keyboard.h"

#include <stddef.h>

/* Układy z ui_keyboard.h; spacja zajmuje kolumny 8..9 ostatniego rzędu, kolumna 7 przełącza litery / cyfry */
static const char *const LOWER[UI_KBD_ROWS][UI_KBD_COLS] = {
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż", UI_KBD_BACKSPACE },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", UI_KBD_SHIFT },
    { "z", "x", "c", "v", "b", "n", "m", UI_KBD_MODE, " ", " " },
};
static const char *const UPPER[UI_KBD_ROWS][UI_KBD_COLS] = {
    { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
    { "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ź", "Ż", UI_KBD_BACKSPACE },
    { "A", "S", "D", "F", "G", "H", "J", "K", "L", UI_KBD_SHIFT },
    { "Z", "X", "C", "V", "B", "N", "M", UI_KBD_MODE, " ", " " },
};
static const char *const DIGITS[UI_KBD_ROWS][UI_KBD_COLS] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "-", "(", ")", ".", ",", "/", "&", "+", "'", UI_KBD_BACKSPACE },
    { "!", "?", ":", ";", "\"", "#", "%", "=", "_", NULL },
    { "*", "@", "[", "]", "<", ">", "~", UI_KBD_MODE, " ", " " },
};

const char *ui_kbd_key_at_layout(int x, int y, ui_kbd_layout_t layout)
{
    if (x < 0 || y < UI_KBD_Y0)
        return NULL;
    int col = x / UI_KBD_KEY_W;
    int row = (y - UI_KBD_Y0) / UI_KBD_KEY_H;
    if (col >= UI_KBD_COLS || row >= UI_KBD_ROWS)
        return NULL;
    switch (layout) {
    case UI_KBD_UPPER:  return UPPER[row][col];
    case UI_KBD_DIGITS: return DIGITS[row][col];
    case UI_KBD_LOWER:
    default:            return LOWER[row][col];
    }
}

const char *ui_kbd_key_at(int x, int y, bool shift)
{
    return ui_kbd_key_at_layout(x, y, shift ? UI_KBD_UPPER : UI_KBD_LOWER);
}
