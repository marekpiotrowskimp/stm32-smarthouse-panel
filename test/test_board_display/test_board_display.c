/*
 * Testy sprzętowe wyświetlacza STM32F746G-DISCO (spec: display-tiles).
 * Sprawdzają rejestry LTDC, piny LCD, SDRAM, dotyk oraz zawartość bufora ramki
 * w ustalonych punktach (kolory i układ z ui_model.h) – czyli to, co faktycznie widać na ekranie.
 */
#include <unity.h>

#include "app_display.h"
#include "app_platform.h"
#include "stm32f7xx_hal.h"
#include "ui_details.h"
#include "ui_idle.h"
#include "ui_model.h"

#define SDRAM_BASE       0xC0000000u
#define SDRAM_SIZE       0x00800000u
#define SDRAM_TEST_BASE  0xC0700000u /* ostatni 1 MB – poza buforami ramki */
#define SDRAM_TEST_WORDS (256u * 1024u / 4u)
#define RENDER_WAIT_MS   500u

/* 10 urządzeń -> 2 strony (8 + 2); urządzenie 1 offline, reszta online */
static const dev_info_t DEVS[10] = {
    { "Roleta A", "id00000000000000000a", DEV_TYPE_BLIND }, { "Lampa B", "id00000000000000000b", DEV_TYPE_LIGHT },
    { "Gniazdo C", "id00000000000000000c", DEV_TYPE_PLUG }, { "Roleta D", "id00000000000000000d", DEV_TYPE_BLIND },
    { "Roleta E", "id00000000000000000e", DEV_TYPE_BLIND }, { "Roleta F", "id00000000000000000f", DEV_TYPE_BLIND },
    { "Roleta G", "id00000000000000000g", DEV_TYPE_BLIND }, { "Roleta H", "id00000000000000000h", DEV_TYPE_BLIND },
    { "Żarówka I", "id00000000000000000i", DEV_TYPE_LIGHT }, { "Kotłownia J", "id00000000000000000j", DEV_TYPE_BLIND },
};
static dev_state_t STATES[10];
static volatile unsigned tap_calls;

static void on_tap(size_t idx)
{
    (void)idx;
    tap_calls++;
}

void setUp(void) {}
void tearDown(void) {}

/* Odczyt piksela RGB565 z bufora ramki (unieważnienie cache – bufor mógł zapisać DMA2D). */
static uint16_t pixel(int x, int y)
{
    uint32_t fb = app_display_framebuffer();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0u, fb, "brak bufora ramki"); /* chroni przed odczytem spod adresu 0 */
    volatile uint16_t *p = (volatile uint16_t *)(fb + (uint32_t)(y * UI_SCREEN_W + x) * 2u);
    SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)p & ~31u), 32);
    return *p;
}

/* Punkt tła kafelka: 8 px od lewej krawędzi, w połowie wysokości (z dala od ikony i tekstu). */
static uint16_t tile_bg(unsigned slot)
{
    int x, y;
    ui_tile_origin(slot, &x, &y);
    return pixel(x + 8, y + UI_TILE_H / 2);
}

/* ---------- AC-5: SDRAM (inicjalizowany przez app_display_start) ---------- */

void test_sdram_pattern_write_read(void)
{
    /* bez skonfigurowanego FMC dostęp do 0xC0000000 kończy się HardFault – najpierw sprawdzamy kontroler */
    TEST_ASSERT_TRUE_MESSAGE((RCC->AHB3ENR & RCC_AHB3ENR_FMCEN) != 0, "FMC wyłączony");
    /* MT48LC4M32B2: 12 bitów wierszy -> NR = 01 (NC = 00 dla 8 bitów kolumn, więc NC nic nie mówi) */
    TEST_ASSERT_TRUE_MESSAGE((FMC_Bank5_6->SDCR[0] & FMC_SDCR1_NR) != 0, "SDRAM nieskonfigurowany");
    volatile uint32_t *m = (volatile uint32_t *)SDRAM_TEST_BASE;
    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++)
        m[i] = 0xA5000000u ^ (i * 2654435761u);
    SCB_CleanInvalidateDCache();
    uint32_t bad = 0;
    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++)
        if (m[i] != (0xA5000000u ^ (i * 2654435761u)))
            bad++;
    TEST_ASSERT_EQUAL_UINT32(0, bad);
}

/* ---------- AC-6: LCD ---------- */

void test_display_start_returns_ok(void)
{
    for (unsigned i = 0; i < 10; i++)
        STATES[i] = (dev_state_t){ .link = DEV_LINK_ONLINE, .power_dw = -1 };
    STATES[1].link = DEV_LINK_OFFLINE;
    TEST_ASSERT_EQUAL_INT(0, app_display_start(DEVS, STATES, 10));
    app_delay_ms(RENDER_WAIT_MS);
}

void test_ltdc_enabled_480x272_from_sdram(void)
{
    TEST_ASSERT_TRUE((LTDC->GCR & LTDC_GCR_LTDCEN) != 0);
    uint32_t aw = ((LTDC->AWCR >> 16) & 0xFFFu) - ((LTDC->BPCR >> 16) & 0xFFFu);
    uint32_t ah = (LTDC->AWCR & 0x7FFu) - (LTDC->BPCR & 0x7FFu);
    TEST_ASSERT_EQUAL_UINT32(480, aw);
    TEST_ASSERT_EQUAL_UINT32(272, ah);

    uint32_t fb = app_display_framebuffer();
    TEST_ASSERT_TRUE(fb >= SDRAM_BASE && fb + UI_SCREEN_W * UI_SCREEN_H * 2u <= SDRAM_BASE + SDRAM_SIZE);
    TEST_ASSERT_EQUAL_HEX32(fb, LTDC_Layer1->CFBAR);
    TEST_ASSERT_EQUAL_HEX32(LTDC_PIXEL_FORMAT_RGB565, LTDC_Layer1->PFCR);
}

void test_lcd_display_enable_and_backlight_on(void)
{
    TEST_ASSERT_EQUAL_INT(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_12)); /* LCD_DISP */
    TEST_ASSERT_EQUAL_INT(GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOK, GPIO_PIN_3));  /* LCD_BL_CTRL */
}

/* ---------- AC-7: dotyk ---------- */

void test_touch_controller_ready(void)
{
    TEST_ASSERT_TRUE(app_display_touch_ready());
}

/* ---------- AC-8: GUI działa ---------- */

void test_gui_keeps_refreshing(void)
{
    uint32_t f0 = app_display_frame_count();
    app_display_show_page(1);
    app_delay_ms(200);
    app_display_show_page(0);
    app_delay_ms(200);
    TEST_ASSERT_GREATER_THAN_UINT32(f0, app_display_frame_count());
}

/* ---------- AC-9: obraz strony 1 ---------- */

void test_page_one_of_two(void)
{
    TEST_ASSERT_EQUAL_UINT(2, app_display_page_count());
    TEST_ASSERT_EQUAL_UINT(0, app_display_page());
}

void test_status_bar_and_background_colors(void)
{
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BAR, pixel(2, 2));              /* lewy górny róg paska */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BAR, pixel(UI_SCREEN_W - 3, UI_BAR_H - 3));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BG, pixel(2, 100));             /* margines między krawędzią a kafelkami */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BG, pixel(6 + 112 + 3, 100));   /* odstęp między kafelkami 0 i 1 */
}

void test_tiles_online_normal_offline_dimmed(void)
{
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(0));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_DIM, tile_bg(1)); /* urządzenie offline */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(2));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(7));
}

/* ---------- AC-10: strona 2 ---------- */

void test_page_two_shows_two_tiles_and_empty_slots(void)
{
    app_display_show_page(1);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_UINT(1, app_display_page());
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(0));  /* Żarówka I */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(1));  /* Kotłownia J */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BG, tile_bg(2));    /* puste */
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BG, tile_bg(7));    /* puste */
}

void test_show_page_out_of_range_keeps_current(void)
{
    app_display_show_page(1);
    app_display_show_page(5);
    app_delay_ms(100);
    TEST_ASSERT_EQUAL_UINT(1, app_display_page());
}

/* ---------- AC-11 (tuya-control): stan urządzenia -> kolor kafelka ---------- */

void test_set_state_recolors_active_tiles(void)
{
    app_display_show_page(0);
    dev_state_t blind_open = { .link = DEV_LINK_ONLINE, .open_pct = 100, .power_dw = -1 };
    dev_state_t light_on = { .link = DEV_LINK_ONLINE, .on = true, .brightness = 800, .power_dw = -1 };
    dev_state_t plug_on = { .link = DEV_LINK_ONLINE, .on = true, .power_dw = 120 };
    app_display_set_state(0, &blind_open); /* Roleta A */
    app_display_set_state(1, &light_on);   /* Lampa B – wcześniej offline */
    app_display_set_state(2, &plug_on);    /* Gniazdo C */
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_BLIND, tile_bg(0));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, tile_bg(1));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_PLUG, tile_bg(2));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(3)); /* bez zmian */
}

void test_set_state_back_to_off_and_offline(void)
{
    dev_state_t blind_closed = { .link = DEV_LINK_ONLINE, .open_pct = 0, .power_dw = -1 };
    dev_state_t offline = { .link = DEV_LINK_OFFLINE, .power_dw = -1 };
    app_display_set_state(0, &blind_closed);
    app_display_set_state(2, &offline);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(0));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_DIM, tile_bg(2));
}

void test_set_state_for_other_page_shows_after_switch(void)
{
    dev_state_t light_on = { .link = DEV_LINK_ONLINE, .on = true, .brightness = 500, .power_dw = -1 };
    app_display_set_state(8, &light_on); /* Żarówka I – strona 2, slot 0 */
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE, tile_bg(4)); /* strona 1 bez zmian (Roleta E) */
    app_display_show_page(1);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, tile_bg(0));
}

void test_set_state_bad_index_is_ignored(void)
{
    dev_state_t light_on = { .link = DEV_LINK_ONLINE, .on = true, .brightness = 500, .power_dw = -1 };
    uint32_t f0 = app_display_frame_count();
    app_display_set_state(10, &light_on);
    app_display_set_state((size_t)-1, &light_on);
    app_delay_ms(200);
    TEST_ASSERT_EQUAL_UINT(1, app_display_page());
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_TILE_ON_LIGHT, tile_bg(0));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_BG, tile_bg(2));
    TEST_ASSERT_EQUAL_UINT32(f0, app_display_frame_count()); /* nic nie przerysowano */
}

/* ---------- system-reliability AC-9: programowy PWM podświetlenia ---------- */

/* Udział stanu wysokiego LCD_BL_CTRL (PK3) w % z próbek przez ms milisekund */
static unsigned backlight_high_pct(uint32_t ms)
{
    uint32_t high = 0, total = 0, t0 = app_millis();
    while (app_millis() - t0 < ms) {
        if (HAL_GPIO_ReadPin(GPIOK, GPIO_PIN_3) == GPIO_PIN_SET)
            high++;
        total++;
    }
    return total ? (unsigned)(high * 100u / total) : 0;
}

void test_backlight_pwm_20_and_100_percent(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, app_display_backlight()); /* po starcie pełne */
    TEST_ASSERT_EQUAL_UINT(100, backlight_high_pct(20));
    app_display_set_backlight(20);
    app_delay_ms(50);
    TEST_ASSERT_EQUAL_UINT8(20, app_display_backlight());
    unsigned pct = backlight_high_pct(40); /* 20 okresów PWM 500 Hz */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(15, pct);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(25, pct);
    app_display_set_backlight(100);
    app_delay_ms(50);
    TEST_ASSERT_EQUAL_UINT8(100, app_display_backlight());
    TEST_ASSERT_EQUAL_UINT(100, backlight_high_pct(40));
}

/* ---------- system-reliability AC-10, AC-11: bezczynność (czasy skrócone do 1 s / 2 s) ---------- */

static void tap_at(int x, int y)
{
    app_display_inject_touch(true, x, y);
    app_delay_ms(80);
    app_display_inject_touch(false, x, y);
    app_delay_ms(RENDER_WAIT_MS);
}

void test_idle_goes_home_dims_and_first_touch_wakes(void)
{
    int x, y;
    app_display_set_tap_handler(on_tap);
    app_display_set_idle_timeouts(1000, 2000);
    app_display_show_page(1);
    app_delay_ms(300);
    TEST_ASSERT_EQUAL_UINT(1, app_display_page());
    app_delay_ms(1000);                              /* 1,3 s bez dotyku */
    TEST_ASSERT_EQUAL_UINT(0, app_display_page());   /* powrót na stronę 1 */
    TEST_ASSERT_EQUAL_UINT8(100, app_display_backlight());
    app_delay_ms(1000);                              /* 2,3 s */
    TEST_ASSERT_EQUAL_UINT8(UI_BACKLIGHT_DIM_PCT, app_display_backlight());

    unsigned calls = tap_calls;
    ui_tile_origin(0, &x, &y);                       /* Roleta A – online */
    tap_at(x + UI_TILE_W / 2, y + UI_TILE_H / 2);
    TEST_ASSERT_EQUAL_UINT8(100, app_display_backlight()); /* wybudzony */
    TEST_ASSERT_EQUAL_UINT(calls, tap_calls);              /* bez akcji */
    tap_at(x + UI_TILE_W / 2, y + UI_TILE_H / 2);
    TEST_ASSERT_EQUAL_UINT(calls + 1, tap_calls);          /* kolejny dotyk działa */
    app_display_set_idle_timeouts(UI_IDLE_HOME_MS, UI_IDLE_DIM_MS);
}

void test_idle_keeps_open_dialog_page(void)
{
    int x, y;
    app_display_set_idle_timeouts(1000, 2000);
    app_display_show_page(1);
    app_delay_ms(300);
    ui_tile_origin(0, &x, &y);                       /* strona 2, miejsce 0: Żarówka I (idx 8) */
    app_display_inject_touch(true, x + UI_TILE_W / 2, y + UI_TILE_H / 2);
    app_delay_ms(700);                               /* przytrzymanie -> okno szczegółów */
    app_display_inject_touch(false, x + UI_TILE_W / 2, y + UI_TILE_H / 2);
    app_delay_ms(RENDER_WAIT_MS);
    TEST_ASSERT_EQUAL_INT(8, app_display_details_idx());
    app_delay_ms(2300);                              /* > 2 s bez dotyku */
    TEST_ASSERT_EQUAL_UINT(1, app_display_page());   /* okno otwarte – strona bez zmian */
    TEST_ASSERT_EQUAL_INT(8, app_display_details_idx());
    TEST_ASSERT_EQUAL_UINT8(UI_BACKLIGHT_DIM_PCT, app_display_backlight());
    tap_at(20, UI_SCREEN_H - 10);                    /* poza oknem – tylko wybudza, okno zostaje */
    TEST_ASSERT_EQUAL_UINT8(100, app_display_backlight());
    TEST_ASSERT_EQUAL_INT(8, app_display_details_idx());
    tap_at(UI_DLG_CLOSE_X + UI_DLG_CLOSE_W / 2, UI_DLG_CLOSE_Y + UI_DLG_CLOSE_H / 2); /* ✕ */
    TEST_ASSERT_EQUAL_INT(-1, app_display_details_idx());
    app_display_set_idle_timeouts(UI_IDLE_HOME_MS, UI_IDLE_DIM_MS);
}

static void test_task(void *arg)
{
    (void)arg;
    app_delay_ms(2000); /* czas na otwarcie portu szeregowego po resecie */
    UNITY_BEGIN();
    RUN_TEST(test_display_start_returns_ok);
    RUN_TEST(test_sdram_pattern_write_read);
    RUN_TEST(test_ltdc_enabled_480x272_from_sdram);
    RUN_TEST(test_lcd_display_enable_and_backlight_on);
    RUN_TEST(test_touch_controller_ready);
    RUN_TEST(test_gui_keeps_refreshing);
    RUN_TEST(test_page_one_of_two);
    RUN_TEST(test_status_bar_and_background_colors);
    RUN_TEST(test_tiles_online_normal_offline_dimmed);
    RUN_TEST(test_page_two_shows_two_tiles_and_empty_slots);
    RUN_TEST(test_show_page_out_of_range_keeps_current);
    RUN_TEST(test_set_state_recolors_active_tiles);
    RUN_TEST(test_set_state_back_to_off_and_offline);
    RUN_TEST(test_set_state_for_other_page_shows_after_switch);
    RUN_TEST(test_set_state_bad_index_is_ignored);
    RUN_TEST(test_backlight_pwm_20_and_100_percent);
    RUN_TEST(test_idle_goes_home_dims_and_first_touch_wakes);
    RUN_TEST(test_idle_keeps_open_dialog_page);
    UNITY_END();
    for (;;)
        app_delay_ms(1000);
}

int main(void)
{
    app_board_init();
    app_rtos_start(test_task);
    return 0;
}
