/*
 * Klawiatura ekranowa z polskimi znakami (§6.7 krok „Nazwa”; spec: virtual-groups) – stały układ:
 *   wiersz 0: q w e r t y u i o p
 *   wiersz 1: ą ć ę ł ń ó ś ź ż  ⌫
 *   wiersz 2: a s d f g h j k l  ⇧
 *   wiersz 3: z x c v b n m  123  [spacja: kolumny 8..9]
 * Układ cyfr (UI_KBD_DIGITS):
 *   wiersz 0: 1 2 3 4 5 6 7 8 9 0
 *   wiersz 1: - ( ) . , / & + '  ⌫
 *   wiersz 2: ! ? : ; " # % = _  [pusto]
 *   wiersz 3: * @ [ ] < > ~  abc  [spacja: kolumny 8..9]
 * Klawisz (wiersz r, kolumna c): x = c * UI_KBD_KEY_W, y = UI_KBD_Y0 + r * UI_KBD_KEY_H.
 */
#ifndef UI_KEYBOARD_H
#define UI_KEYBOARD_H

#include <stdbool.h>

#define UI_KBD_Y0    136
#define UI_KBD_KEY_W 48
#define UI_KBD_KEY_H 34
#define UI_KBD_ROWS  4
#define UI_KBD_COLS  10

#define UI_KBD_BACKSPACE "\b"
#define UI_KBD_SHIFT     "\x0e"
#define UI_KBD_MODE      "\x0f" /* przełącznik liter / cyfr: „123” w literach, „abc” w cyfrach */

typedef enum { UI_KBD_LOWER, UI_KBD_UPPER, UI_KBD_DIGITS } ui_kbd_layout_t;

/*
 * Klawisz pod punktem (x, y): znak UTF-8 (z shift – wielka litera, także Ą..Ż), " " dla spacji,
 * UI_KBD_BACKSPACE, UI_KBD_SHIFT; NULL poza klawiaturą lub w pustym miejscu.
 */
const char *ui_kbd_key_at(int x, int y, bool shift);

/* Jak ui_kbd_key_at dla wybranego układu (LOWER / UPPER = litery, DIGITS = cyfry i znaki); także UI_KBD_MODE. */
const char *ui_kbd_key_at_layout(int x, int y, ui_kbd_layout_t layout);

#endif /* UI_KEYBOARD_H */
