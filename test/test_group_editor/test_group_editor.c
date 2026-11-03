/*
 * Kontrakt edytora grup i klawiatury ekranowej (spec: virtual-groups, docs/SPEC.md §6.7,
 * R-GRP-1, R-GRP-8, R-GRP-10).
 */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "group_editor.h"
#include "ui_keyboard.h"

enum { R1, R2, R3, R4, L1, L2, P1, N_DEV };

static const dev_info_t DEVS[N_DEV] = {
    { "Roleta 1", "id01", DEV_TYPE_BLIND }, { "Roleta 2", "id02", DEV_TYPE_BLIND },
    { "Roleta 3", "id03", DEV_TYPE_BLIND }, { "Roleta 4", "id04", DEV_TYPE_BLIND },
    { "Lampa 1", "id05", DEV_TYPE_LIGHT },  { "Lampa 2", "id06", DEV_TYPE_LIGHT },
    { "Gniazdo", "id07", DEV_TYPE_PLUG },
};

static group_editor_t ed;
static groups_t gs;

void setUp(void)
{
    memset(&gs, 0, sizeof gs);
    gs.count = 1;
    gs.g[0] = (group_cfg_t){ .id = 4, .name = "Stare", .type = DEV_TYPE_BLIND, .member_count = 3, .position = 2 };
    strcpy(gs.g[0].members[0], "id02");
    strcpy(gs.g[0].members[1], "idXX"); /* brak urządzenia */
    strcpy(gs.g[0].members[2], "id04");
}

void tearDown(void) {}

/* Nowa grupa rolet z członkami R1..R3 aż do kroku POSITION */
static void new_blind_group_to_position(void)
{
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    ed_next(&ed);
    ed_toggle_member(&ed, DEVS, N_DEV, R1);
    ed_toggle_member(&ed, DEVS, N_DEV, R2);
    ed_toggle_member(&ed, DEVS, N_DEV, R3);
    ed_next(&ed);
    ed_next(&ed);
}

/* ---------- AC-7: kroki nowej grupy (R-GRP-1) ---------- */

void test_new_group_starts_at_type_step(void)
{
    ed_begin_new(&ed);
    TEST_ASSERT_TRUE(ed.active);
    TEST_ASSERT_EQUAL_INT(-1, ed.group);
    TEST_ASSERT_EQUAL_INT(ED_STEP_TYPE, ed.step);
    TEST_ASSERT_FALSE(ed_can_next(&ed));
    TEST_ASSERT_FALSE(ed_next(&ed));
    TEST_ASSERT_TRUE(ed_select_type(&ed, DEV_TYPE_BLIND));
    TEST_ASSERT_TRUE(ed_can_next(&ed));
    TEST_ASSERT_TRUE(ed_next(&ed));
    TEST_ASSERT_EQUAL_INT(ED_STEP_MEMBERS, ed.step);
}

void test_candidates_only_of_chosen_type(void)
{
    int c[ED_MAX_DEVICES];
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_LIGHT); /* R-GRP-8 */
    TEST_ASSERT_EQUAL_UINT(2, ed_candidates(&ed, DEVS, N_DEV, c, ED_MAX_DEVICES));
    TEST_ASSERT_EQUAL_INT(L1, c[0]);
    TEST_ASSERT_EQUAL_INT(L2, c[1]);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    TEST_ASSERT_EQUAL_UINT(4, ed_candidates(&ed, DEVS, N_DEV, c, ED_MAX_DEVICES));
    TEST_ASSERT_EQUAL_INT(R4, c[3]);
}

void test_members_need_2_to_16_and_same_type(void)
{
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    ed_next(&ed);
    ed_toggle_member(&ed, DEVS, N_DEV, R1);
    TEST_ASSERT_FALSE(ed_can_next(&ed));
    ed_toggle_member(&ed, DEVS, N_DEV, L1); /* inny typ – ignorowane */
    TEST_ASSERT_EQUAL_UINT(1, ed_member_count(&ed));
    ed_toggle_member(&ed, DEVS, N_DEV, R2);
    TEST_ASSERT_TRUE(ed_can_next(&ed));
    ed_toggle_member(&ed, DEVS, N_DEV, R2); /* odznaczenie */
    TEST_ASSERT_EQUAL_UINT(1, ed_member_count(&ed));
    TEST_ASSERT_FALSE(ed_can_next(&ed));
}

void test_member_limit_16(void)
{
    static dev_info_t many[20];
    static char ids[20][8];
    for (unsigned i = 0; i < 20; i++) {
        snprintf(ids[i], sizeof ids[i], "r%02u", i);
        many[i] = (dev_info_t){ "Roleta", ids[i], DEV_TYPE_BLIND };
    }
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    ed_next(&ed);
    for (unsigned i = 0; i < 20; i++)
        ed_toggle_member(&ed, many, 20, i);
    TEST_ASSERT_EQUAL_UINT(GROUP_MEMBERS_MAX, ed_member_count(&ed));
    TEST_ASSERT_TRUE(ed_can_next(&ed));
}

void test_changing_type_clears_members(void)
{
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    ed_next(&ed);
    ed_toggle_member(&ed, DEVS, N_DEV, R1);
    ed_toggle_member(&ed, DEVS, N_DEV, R2);
    ed_back(&ed);
    TEST_ASSERT_EQUAL_INT(ED_STEP_TYPE, ed.step);
    ed_select_type(&ed, DEV_TYPE_LIGHT);
    TEST_ASSERT_EQUAL_UINT(0, ed_member_count(&ed));
}

void test_default_names(void)
{
    char b[32];
    ed_default_name(b, sizeof b, DEV_TYPE_BLIND, 4);
    TEST_ASSERT_EQUAL_STRING("Rolety (4)", b);
    ed_default_name(b, sizeof b, DEV_TYPE_LIGHT, 2);
    TEST_ASSERT_EQUAL_STRING("Światła (2)", b);
    ed_default_name(b, sizeof b, DEV_TYPE_PLUG, 3);
    TEST_ASSERT_EQUAL_STRING("Gniazdka (3)", b);
}

void test_name_step_proposes_default_and_requires_text(void)
{
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    ed_next(&ed);
    ed_toggle_member(&ed, DEVS, N_DEV, R1);
    ed_toggle_member(&ed, DEVS, N_DEV, R3);
    ed_toggle_member(&ed, DEVS, N_DEV, R4);
    TEST_ASSERT_TRUE(ed_next(&ed));
    TEST_ASSERT_EQUAL_INT(ED_STEP_NAME, ed.step);
    TEST_ASSERT_EQUAL_STRING("Rolety (3)", ed.name);
    for (unsigned i = 0; i < 10; i++)
        ed_name_backspace(&ed);
    TEST_ASSERT_EQUAL_STRING("", ed.name);
    TEST_ASSERT_FALSE(ed_can_next(&ed));
    ed_name_append(&ed, "P");
    ed_name_append(&ed, "a");
    TEST_ASSERT_TRUE(ed_can_next(&ed));
    ed_back(&ed); /* wróć do członków i dalej – wpisana nazwa zostaje */
    ed_next(&ed);
    TEST_ASSERT_EQUAL_STRING("Pa", ed.name);
}

void test_name_utf8_limit_and_backspace(void)
{
    ed_begin_new(&ed);
    for (unsigned i = 0; i < 15; i++)
        TEST_ASSERT_TRUE(ed_name_append(&ed, "ż")); /* 15 x 2 bajty = 30 */
    TEST_ASSERT_FALSE(ed_name_append(&ed, "ą"));   /* 32 > 31 */
    TEST_ASSERT_TRUE(ed_name_append(&ed, "x"));    /* 31 */
    TEST_ASSERT_FALSE(ed_name_append(&ed, "y"));
    TEST_ASSERT_EQUAL_UINT(31, strlen(ed.name));
    ed_name_backspace(&ed);
    ed_name_backspace(&ed); /* usuwa całe „ż” */
    TEST_ASSERT_EQUAL_UINT(28, strlen(ed.name));
    TEST_ASSERT_EQUAL_STRING("ż", ed.name + 26);
}

void test_save_new_group(void)
{
    new_blind_group_to_position();
    TEST_ASSERT_EQUAL_INT(ED_STEP_POSITION, ed.step);
    TEST_ASSERT_FALSE(ed_next(&ed)); /* ostatni krok – „Zapisz” */
    ed.position = 0;
    TEST_ASSERT_TRUE(ed_save(&ed, &gs, DEVS, N_DEV));
    TEST_ASSERT_FALSE(ed.active);
    TEST_ASSERT_EQUAL_UINT8(2, gs.count);
    const group_cfg_t *g = &gs.g[1];
    TEST_ASSERT_EQUAL_UINT8(1, g->id); /* najmniejszy wolny */
    TEST_ASSERT_EQUAL_STRING("Rolety (3)", g->name);
    TEST_ASSERT_EQUAL_INT(DEV_TYPE_BLIND, g->type);
    TEST_ASSERT_EQUAL_UINT8(3, g->member_count);
    TEST_ASSERT_EQUAL_STRING("id01", g->members[0]);
    TEST_ASSERT_EQUAL_STRING("id02", g->members[1]);
    TEST_ASSERT_EQUAL_STRING("id03", g->members[2]);
    TEST_ASSERT_EQUAL_UINT8(0, g->position);
}

void test_save_rejected_at_16_groups_or_incomplete(void)
{
    gs.count = GROUP_MAX;
    for (unsigned i = 0; i < GROUP_MAX; i++)
        gs.g[i].id = (uint8_t)(i + 1);
    new_blind_group_to_position();
    TEST_ASSERT_FALSE(ed_save(&ed, &gs, DEVS, N_DEV));
    TEST_ASSERT_EQUAL_UINT8(GROUP_MAX, gs.count);

    gs.count = 1;
    ed_begin_new(&ed);
    ed_select_type(&ed, DEV_TYPE_BLIND);
    TEST_ASSERT_FALSE(ed_save(&ed, &gs, DEVS, N_DEV)); /* krok TYPE – niekompletne */
    TEST_ASSERT_EQUAL_UINT8(1, gs.count);
}

void test_back_from_first_step_abandons(void)
{
    ed_begin_new(&ed);
    ed_back(&ed);
    TEST_ASSERT_FALSE(ed.active);
    ed_begin_edit(&ed, &gs, 0, DEVS, N_DEV);
    ed_back(&ed); /* edycja zaczyna od MEMBERS */
    TEST_ASSERT_FALSE(ed.active);
    TEST_ASSERT_EQUAL_STRING("Stare", gs.g[0].name);
}

/* ---------- AC-8: edycja i usuwanie istniejącej grupy (R-GRP-10) ---------- */

void test_edit_existing_group_keeps_type_and_id(void)
{
    ed_begin_edit(&ed, &gs, 0, DEVS, N_DEV);
    TEST_ASSERT_TRUE(ed.active);
    TEST_ASSERT_EQUAL_INT(0, ed.group);
    TEST_ASSERT_EQUAL_UINT8(4, ed.group_id);
    TEST_ASSERT_EQUAL_INT(ED_STEP_MEMBERS, ed.step);
    TEST_ASSERT_EQUAL_INT(DEV_TYPE_BLIND, ed.type);
    TEST_ASSERT_EQUAL_UINT(2, ed_member_count(&ed)); /* id02, id04; idXX brak urządzenia */
    TEST_ASSERT_EQUAL_UINT8(1, ed.missing);
    TEST_ASSERT_EQUAL_STRING("Stare", ed.name);
    TEST_ASSERT_EQUAL_UINT(2, ed.position);
    TEST_ASSERT_FALSE(ed_select_type(&ed, DEV_TYPE_LIGHT));

    ed_toggle_member(&ed, DEVS, N_DEV, R1);
    ed_next(&ed);
    TEST_ASSERT_EQUAL_STRING("Stare", ed.name); /* bez propozycji przy edycji */
    ed_next(&ed);
    TEST_ASSERT_TRUE(ed_save(&ed, &gs, DEVS, N_DEV));
    TEST_ASSERT_EQUAL_UINT8(1, gs.count);
    TEST_ASSERT_EQUAL_UINT8(4, gs.g[0].id);
    TEST_ASSERT_EQUAL_UINT8(3, gs.g[0].member_count);
    TEST_ASSERT_EQUAL_STRING("id01", gs.g[0].members[0]);
    TEST_ASSERT_EQUAL_STRING("id02", gs.g[0].members[1]);
    TEST_ASSERT_EQUAL_STRING("id04", gs.g[0].members[2]);
}

void test_delete_existing_group_only(void)
{
    ed_begin_new(&ed);
    TEST_ASSERT_FALSE(ed_delete(&ed, &gs));
    TEST_ASSERT_EQUAL_UINT8(1, gs.count);
    ed_begin_edit(&ed, &gs, 0, DEVS, N_DEV);
    TEST_ASSERT_TRUE(ed_delete(&ed, &gs));
    TEST_ASSERT_EQUAL_UINT8(0, gs.count);
    TEST_ASSERT_FALSE(ed.active);
}

/* ---------- AC-9: klawiatura ekranowa ---------- */

static const char *key(unsigned row, unsigned col, bool shift)
{
    return ui_kbd_key_at((int)col * UI_KBD_KEY_W + UI_KBD_KEY_W / 2, UI_KBD_Y0 + (int)row * UI_KBD_KEY_H + UI_KBD_KEY_H / 2,
                         shift);
}

void test_keyboard_letters_and_shift(void)
{
    TEST_ASSERT_EQUAL_STRING("q", key(0, 0, false));
    TEST_ASSERT_EQUAL_STRING("p", key(0, 9, false));
    TEST_ASSERT_EQUAL_STRING("P", key(0, 9, true));
    TEST_ASSERT_EQUAL_STRING("a", key(2, 0, false));
    TEST_ASSERT_EQUAL_STRING("m", key(3, 6, false));
    TEST_ASSERT_EQUAL_STRING("Z", key(3, 0, true));
}

void test_keyboard_polish_row(void)
{
    static const char *lower[] = { "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż" };
    static const char *upper[] = { "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ź", "Ż" };
    for (unsigned c = 0; c < 9; c++) {
        TEST_ASSERT_EQUAL_STRING(lower[c], key(1, c, false));
        TEST_ASSERT_EQUAL_STRING(upper[c], key(1, c, true));
    }
}

void test_keyboard_special_keys_and_outside(void)
{
    TEST_ASSERT_EQUAL_STRING(UI_KBD_BACKSPACE, key(1, 9, false));
    TEST_ASSERT_EQUAL_STRING(UI_KBD_SHIFT, key(2, 9, false));
    TEST_ASSERT_EQUAL_STRING(UI_KBD_MODE, key(3, 7, false)); /* control-polish AC-8: „123” (było: spacja) */
    TEST_ASSERT_EQUAL_STRING(UI_KBD_MODE, key(3, 7, true));
    TEST_ASSERT_EQUAL_STRING(" ", key(3, 8, false));
    TEST_ASSERT_EQUAL_STRING(" ", key(3, 9, true));
    TEST_ASSERT_NULL(ui_kbd_key_at(100, UI_KBD_Y0 - 1, false));
    TEST_ASSERT_NULL(ui_kbd_key_at(-1, UI_KBD_Y0 + 5, false));
    TEST_ASSERT_NULL(ui_kbd_key_at(480, UI_KBD_Y0 + 5, false));
}

/* control-polish AC-9: układ cyfr i znaków */
static const char *dkey(unsigned row, unsigned col)
{
    return ui_kbd_key_at_layout((int)col * UI_KBD_KEY_W + UI_KBD_KEY_W / 2,
                                UI_KBD_Y0 + (int)row * UI_KBD_KEY_H + UI_KBD_KEY_H / 2, UI_KBD_DIGITS);
}

void test_keyboard_digits_layout(void)
{
    static const char *r0[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
    static const char *r1[] = { "-", "(", ")", ".", ",", "/", "&", "+", "'" };
    static const char *r2[] = { "!", "?", ":", ";", "\"", "#", "%", "=", "_" };
    static const char *r3[] = { "*", "@", "[", "]", "<", ">", "~" };
    for (unsigned c = 0; c < 10; c++)
        TEST_ASSERT_EQUAL_STRING(r0[c], dkey(0, c));
    for (unsigned c = 0; c < 9; c++) {
        TEST_ASSERT_EQUAL_STRING(r1[c], dkey(1, c));
        TEST_ASSERT_EQUAL_STRING(r2[c], dkey(2, c));
    }
    for (unsigned c = 0; c < 7; c++)
        TEST_ASSERT_EQUAL_STRING(r3[c], dkey(3, c));
    TEST_ASSERT_EQUAL_STRING(UI_KBD_BACKSPACE, dkey(1, 9));
    TEST_ASSERT_NULL(dkey(2, 9)); /* bez shift */
    TEST_ASSERT_EQUAL_STRING(UI_KBD_MODE, dkey(3, 7)); /* „abc” */
    TEST_ASSERT_EQUAL_STRING(" ", dkey(3, 8));
    TEST_ASSERT_EQUAL_STRING(" ", dkey(3, 9));
    TEST_ASSERT_EQUAL_STRING("q", ui_kbd_key_at_layout(UI_KBD_KEY_W / 2, UI_KBD_Y0 + 5, UI_KBD_LOWER));
    TEST_ASSERT_EQUAL_STRING("Q", ui_kbd_key_at_layout(UI_KBD_KEY_W / 2, UI_KBD_Y0 + 5, UI_KBD_UPPER));
    TEST_ASSERT_NULL(ui_kbd_key_at_layout(100, UI_KBD_Y0 - 1, UI_KBD_DIGITS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_new_group_starts_at_type_step);
    RUN_TEST(test_candidates_only_of_chosen_type);
    RUN_TEST(test_members_need_2_to_16_and_same_type);
    RUN_TEST(test_member_limit_16);
    RUN_TEST(test_changing_type_clears_members);
    RUN_TEST(test_default_names);
    RUN_TEST(test_name_step_proposes_default_and_requires_text);
    RUN_TEST(test_name_utf8_limit_and_backspace);
    RUN_TEST(test_save_new_group);
    RUN_TEST(test_save_rejected_at_16_groups_or_incomplete);
    RUN_TEST(test_back_from_first_step_abandons);
    RUN_TEST(test_edit_existing_group_keeps_type_and_id);
    RUN_TEST(test_delete_existing_group_only);
    RUN_TEST(test_keyboard_letters_and_shift);
    RUN_TEST(test_keyboard_polish_row);
    RUN_TEST(test_keyboard_special_keys_and_outside);
    RUN_TEST(test_keyboard_digits_layout);
    return UNITY_END();
}
