/*
 * Wyświetlacz STM32F746G-DISCO (spec: display-tiles, docs/SPEC.md §6):
 * SDRAM (FMC) + LTDC 480x272 RGB565 + dotyk FT5336 + LVGL 9 w zadaniu FreeRTOS.
 *
 * Wywołania LVGL są chronione mutexem gui_lock – API wołane z innych zadań
 * (show_page, set_net_status, set_state, begin_pending) blokuje go na czas zmiany obiektów.
 *
 * Dotyk (FT5336 lub zadany przez app_display_inject_touch) obsługuje jeden callback ekranu:
 * kafelki i elementy okna szczegółów są rozpoznawane po współrzędnych z ui_model.h / ui_details.h.
 * Handlery (dotknięcie kafelka, komendy z okna) są wołane w gui_task poza gui_lock.
 */
#include "app_display.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "group_editor.h"
#include "lvgl.h"
#include "semphr.h"
#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_sdram.h"
#include "stm32746g_discovery_ts.h"
#include "task.h"
#include "tuya_dps.h"
#include "ui_details.h"
#include "app_platform.h"
#include "ui_idle.h"
#include "ui_keyboard.h"

#define FB_ADDR              0xC0000000u /* bufor ramki LTDC (LCD_FB_START_ADDRESS) */
#define DRAW_BUF_LINES       24u
#define GUI_TASK_STACK_WORDS 3072u
#define GUI_PERIOD_MS        5u
#define MAX_DEVICES          (UI_TILES_PER_PAGE * 8u) /* 8 stron = liczba kropek */

/* Paleta (24-bit, LVGL konwertuje do RGB565). Tło / pasek / kafelki – z ui_model.h. */
#define COLOR_TEXT           0xFFFFFF
#define COLOR_TEXT_2         0xC4CEDD /* linia stanu, etykiety paska */
#define COLOR_TEXT_DIM       0x6E7A8C /* kafelki offline / nieznane */
#define COLOR_ICON_OFF       0xA3AEBF /* ikona urządzenia wył. – jasna, czytelna na kafelku */

/* Akcent typu: ikona, linia stanu i obramowanie aktywnego kafelka */
#define COLOR_ACCENT_BLIND   0x4FC3F7 /* błękit */
/* Roleta: ikona i napis w kolorze ramki (UI_COLOR_BORDER_BLIND_* z ui_model.h) */
#define COLOR_BLIND_CLOSED   0x4FC3F7 /* zamknięta – niebieska */
#define COLOR_BLIND_OPEN     0x5EE07A /* otwarta / w ruchu – zielona */
#define COLOR_ACCENT_LIGHT   0xFFC93C /* bursztyn */
#define COLOR_ACCENT_PLUG    0x5EE07A /* zieleń */

/* Tło aktywnego kafelka (wł. / otwarta / w ruchu) – przyciemniony akcent */
#define COLOR_TILE_BLIND_ON  0x12405A
#define COLOR_TILE_LIGHT_ON  0x4A3A10
#define COLOR_TILE_PLUG_ON   0x15452A

#define COLOR_DOT            0x34405A
#define COLOR_DOT_ACTIVE     COLOR_ACCENT_BLIND

/* Kolor RGB565 z ui_model.h -> lv_color_t (konwersja zwrotna do RGB565 daje tę samą wartość) */
static lv_color_t rgb565(uint16_t c)
{
    return lv_color_make((uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3Fu) << 2), (uint8_t)((c & 0x1Fu) << 3));
}

/* Czcionki bitmapowe z polskimi znakami (tools/gen_font.py) */
LV_FONT_DECLARE(font_pl_13)
LV_FONT_DECLARE(font_pl_14)
LV_FONT_DECLARE(font_pl_16)

static SemaphoreHandle_t gui_lock;
static volatile bool s_started;
static volatile bool s_touch_ok;
static volatile uint32_t s_frames;

static const dev_info_t *s_devs;
static dev_state_t s_states[MAX_DEVICES]; /* kopia – zmieniana przez app_display_set_state */
static size_t s_count;
static unsigned s_page;
static unsigned s_page_count;

static const lv_font_t *const font_bar = &font_pl_16;
static const lv_font_t *const font_name = &font_pl_14;
static const lv_font_t *const font_status = &font_pl_13;
static lv_obj_t *tiles[UI_TILES_PER_PAGE];
static lv_obj_t *dots[8];
static lv_obj_t *net_label, *page_label;

static uint8_t draw_buf[UI_SCREEN_W * DRAW_BUF_LINES * 2] __attribute__((aligned(4)));

/* gest: punkt i czas wciśnięcia */
static lv_point_t press_pt;
static uint32_t press_tick;

static bool s_long_fired; /* przytrzymanie już obsłużone – puszczenie bez akcji */

/* dotknięcie kafelka: handler wołany w gui_task poza gui_lock */
static volatile app_display_tap_handler_t s_tap_handler;
static int s_pending_tap = -1;

/* komendy z okna szczegółów: kolejka (pod gui_lock), wysyłana w gui_task poza gui_lock */
#define CMD_QUEUE_LEN (GROUP_MEMBERS_MAX + 8u) /* fan-out grupy mieści się w jednej porcji */
typedef struct {
    size_t idx;
    char   dps[64];
} cmd_t;
static volatile app_display_command_handler_t s_cmd_handler;
static cmd_t s_cmds[CMD_QUEUE_LEN];
static unsigned s_cmd_count;

/* dotyk zadany programowo */
typedef enum { INJECT_NONE, INJECT_PRESSED, INJECT_RELEASE } inject_t;
static volatile inject_t s_inject;
static volatile int16_t s_inject_x, s_inject_y;

/* stan optymistyczny kafelków (R-UI-8) */
static ui_pending_t s_pend[MAX_DEVICES];
static bool s_err_shown[MAX_DEVICES]; /* kafelek narysowany z czerwonym obrysem */

/* okno szczegółów */
static lv_obj_t *s_dlg;
static int s_dlg_idx = -1;
static uint32_t s_dlg_last_input;
static int s_drag_slider;   /* 0 = brak, 1 / 2 = przeciągany suwak */
static bool s_dlg_stale;    /* stan zmienił się w trakcie przeciągania – przebudowa po puszczeniu */
static ui_drag_t s_drag;
static lv_obj_t *s_slider_fill[2];
static lv_obj_t *s_slider_label[2];

/* grupy wirtualne i lista kafelków (urządzenia, grupy, „＋”) */
#define MAX_TILES (MAX_DEVICES + GROUP_MAX + 1u)
static groups_t s_groups;       /* ~6,7 KB – zawsze statycznie */
static bool s_groups_on;        /* app_display_set_groups wywołane */
static tile_ref_t s_tiles[MAX_TILES];
static size_t s_tile_count;
static volatile app_display_groups_handler_t s_groups_handler;
static bool s_groups_dirty;     /* edytor zapisał / usunął – handler w gui_task */
static groups_t s_groups_out;   /* kopia dla handlera (poza gui_lock) */

/* okno grupy (pełny ekran) */
static lv_obj_t *s_gd;
static int s_gd_idx = -1;
static uint16_t s_gd_mask;      /* zaznaczeni członkowie (bit = indeks w members) */
static uint32_t s_gd_last_input;

/* edytor grup (pełny ekran) */
static lv_obj_t *s_ed;
static group_editor_t s_editor;
static bool s_shift;            /* wielka litera dla następnego znaku */
static bool s_digits;           /* klawiatura: układ cyfr i znaków („123”) */

static ui_kbd_layout_t kbd_layout(void)
{
    return s_digits ? UI_KBD_DIGITS : s_shift ? UI_KBD_UPPER : UI_KBD_LOWER;
}

static void lock(void)
{
    xSemaphoreTake(gui_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(gui_lock);
}

/* ------------------------------------------------------------------ */
/* Sprzęt                                                              */
/* ------------------------------------------------------------------ */

/* SDRAM jako pamięć normalna niecache'owana: domyślnie 0xC0000000 to obszar "device"
 * (brak dostępów niewyrównanych), a LTDC/DMA2D czytają bufor bez udziału cache. */
static void mpu_sdram_region(void)
{
    MPU_Region_InitTypeDef r = { 0 };

    HAL_MPU_Disable();
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER1; /* region 0: bufory Ethernetu (app_platform.c) */
    r.BaseAddress = FB_ADDR;
    r.Size = MPU_REGION_SIZE_8MB;
    r.SubRegionDisable = 0x00;
    r.TypeExtField = MPU_TEX_LEVEL1;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    /* Sterta LVGL (lv_conf.h: LV_MEM_ADR) używana tylko przez CPU – cache write-back,
     * bez tego przebudowa strony trwała ~500 ms. Wyższy numer regionu ma pierwszeństwo. */
    r.Number = MPU_REGION_NUMBER2;
    r.BaseAddress = LV_MEM_ADR;
    r.Size = MPU_REGION_SIZE_1MB;
    r.IsCacheable = MPU_ACCESS_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static int hw_init(void)
{
    mpu_sdram_region();
    if (BSP_SDRAM_Init() != SDRAM_OK)
        return -2;

    /* tło ekranu jeszcze przed włączeniem LCD – bez migania śmieciami z SDRAM */
    volatile uint16_t *fb = (volatile uint16_t *)FB_ADDR;
    for (uint32_t i = 0; i < UI_SCREEN_W * UI_SCREEN_H; i++)
        fb[i] = UI_COLOR_BG;

    if (BSP_LCD_Init() != LCD_OK)
        return -3;
    BSP_LCD_LayerRgb565Init(0, FB_ADDR); /* warstwa 0 = LTDC_Layer1 */
    BSP_LCD_SelectLayer(0);
    BSP_LCD_DisplayOn();

    s_touch_ok = (BSP_TS_Init(UI_SCREEN_W, UI_SCREEN_H) == TS_OK);
    return 0;
}

/* ------------------------------------------------------------------ */
/* LVGL: wyświetlacz i dotyk                                           */
/* ------------------------------------------------------------------ */

static uint32_t tick_cb(void)
{
    return HAL_GetTick();
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    size_t row_bytes = (size_t)w * 2u;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint8_t *dst = (uint8_t *)FB_ADDR + ((size_t)y * UI_SCREEN_W + (size_t)area->x1) * 2u;
        memcpy(dst, px_map, row_bytes);
        px_map += row_bytes;
    }
    if (lv_display_flush_is_last(disp))
        s_frames++;
    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/* Podświetlenie: programowy PWM na LCD_BL_CTRL (PK3, bez wyjścia timera) */
/* ------------------------------------------------------------------ */

#define BL_PORT      GPIOK
#define BL_PIN       GPIO_PIN_3
#define BL_STEPS     20u  /* krok 5% */
#define BL_TICK_HZ   10000u /* przerwanie TIM7 -> PWM 500 Hz */
#define BL_IRQ_PRIO  7u

static volatile uint8_t s_bl_pct = 100;
static volatile uint8_t s_bl_on_steps = BL_STEPS;
static uint8_t s_bl_step;

void TIM7_IRQHandler(void)
{
    TIM7->SR = ~TIM_SR_UIF;
    s_bl_step = (uint8_t)((s_bl_step + 1u) % BL_STEPS);
    BL_PORT->BSRR = s_bl_step < s_bl_on_steps ? BL_PIN : (uint32_t)BL_PIN << 16;
}

static void backlight_apply(uint8_t pct)
{
    if (pct > 100)
        pct = 100;
    uint8_t steps = (uint8_t)((pct * BL_STEPS + 50u) / 100u);
    s_bl_pct = pct;
    s_bl_on_steps = steps;
    if (steps == 0 || steps >= BL_STEPS) { /* stały stan – bez przerwań */
        TIM7->CR1 &= ~TIM_CR1_CEN;
        BL_PORT->BSRR = steps ? BL_PIN : (uint32_t)BL_PIN << 16;
        return;
    }
    if (!(TIM7->CR1 & TIM_CR1_CEN)) {
        __HAL_RCC_TIM7_CLK_ENABLE();
        RCC_ClkInitTypeDef clk;
        uint32_t latency;
        HAL_RCC_GetClockConfig(&clk, &latency);
        uint32_t pclk1 = HAL_RCC_GetPCLK1Freq(); /* zegar timerów APB1 = PCLK1 x2, gdy dzielnik APB1 > 1 */
        uint32_t tim_clk = clk.APB1CLKDivider == RCC_HCLK_DIV1 ? pclk1 : 2u * pclk1;
        TIM7->PSC = tim_clk / 1000000u - 1u;            /* 1 MHz */
        TIM7->ARR = 1000000u / BL_TICK_HZ - 1u;
        TIM7->EGR = TIM_EGR_UG;
        TIM7->SR = 0;
        TIM7->DIER = TIM_DIER_UIE;
        HAL_NVIC_SetPriority(TIM7_IRQn, BL_IRQ_PRIO, 0);
        HAL_NVIC_EnableIRQ(TIM7_IRQn);
        TIM7->CR1 |= TIM_CR1_CEN;
    }
}

/* Bezczynność (§6.6): czasy i stan – tylko w zadaniu GUI (pod gui_lock) */
static ui_idle_t s_idle;
static uint32_t s_idle_home_ms = UI_IDLE_HOME_MS;
static uint32_t s_idle_dim_ms = UI_IDLE_DIM_MS;
static bool s_wake_hold; /* wybudzający dotyk – ignorowany do puszczenia */

static void touch_read_raw(lv_indev_data_t *data)
{
    inject_t inj = s_inject;
    if (inj != INJECT_NONE) {
        data->point.x = s_inject_x;
        data->point.y = s_inject_y;
        data->state = inj == INJECT_PRESSED ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (inj == INJECT_RELEASE)
            s_inject = INJECT_NONE; /* puszczenie zgłoszone – dalej FT5336 */
        return;
    }
    TS_StateTypeDef ts;
    if (s_touch_ok && BSP_TS_GetState(&ts) == TS_OK && ts.touchDetected > 0) {
        data->point.x = ts.touchX[0];
        data->point.y = ts.touchY[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_read_raw(data);
    if (data->state != LV_INDEV_STATE_PRESSED) {
        s_wake_hold = false;
        return;
    }
    if (ui_idle_touch(&s_idle, lv_tick_get())) { /* był przyciemniony: tylko wybudzenie */
        backlight_apply(100);
        s_wake_hold = true;
    }
    if (s_wake_hold)
        data->state = LV_INDEV_STATE_RELEASED; /* LVGL nie widzi tego dotyku */
}

/* ------------------------------------------------------------------ */
/* Obiekty pomocnicze                                                  */
/* ------------------------------------------------------------------ */

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

/* ------------------------------------------------------------------ */
/* Ikony 40x40 rysowane z prostych kształtów (§6.2)                    */
/* ------------------------------------------------------------------ */

static uint32_t accent_color(dev_type_t type)
{
    switch (type) {
    case DEV_TYPE_LIGHT: return COLOR_ACCENT_LIGHT;
    case DEV_TYPE_PLUG:  return COLOR_ACCENT_PLUG;
    case DEV_TYPE_BLIND:
    default:             return COLOR_ACCENT_BLIND;
    }
}

/* Aktywny = włączony / otwarty (choć częściowo) / w ruchu – kafelek dostaje kolorowe tło i ramkę */
static bool view_active(const ui_tile_view_t *v)
{
    return v->icon == UI_ICON_BULB_ON || v->icon == UI_ICON_PLUG_ON || v->icon == UI_ICON_BLIND_OPEN ||
           v->icon == UI_ICON_BLIND_PARTIAL || v->icon == UI_ICON_BLIND_MOVING;
}

static uint32_t active_tile_color(dev_type_t type)
{
    switch (type) {
    case DEV_TYPE_LIGHT: return COLOR_TILE_LIGHT_ON;
    case DEV_TYPE_PLUG:  return COLOR_TILE_PLUG_ON;
    case DEV_TYPE_BLIND:
    default:             return COLOR_TILE_BLIND_ON;
    }
}

static void icon_bulb(lv_obj_t *ic, const ui_tile_view_t *v)
{
    bool on = v->icon == UI_ICON_BULB_ON;
    uint32_t c = on ? COLOR_ACCENT_LIGHT : COLOR_ICON_OFF;
    lv_obj_t *glass = box(ic, 8, 2, 24, 24, c);
    lv_obj_set_style_radius(glass, LV_RADIUS_CIRCLE, 0);
    if (on) {
        lv_obj_set_style_shadow_color(glass, lv_color_hex(COLOR_ACCENT_LIGHT), 0);
        lv_obj_set_style_shadow_width(glass, 6 + v->glow_pct / 5, 0);
        lv_obj_set_style_shadow_opa(glass, (lv_opa_t)(LV_OPA_40 + v->glow_pct), 0);
    } else {
        /* zgaszona: pusty klosz z jasnym konturem */
        lv_obj_set_style_bg_opa(glass, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(glass, lv_color_hex(c), 0);
        lv_obj_set_style_border_width(glass, 3, 0);
    }
    box(ic, 14, 28, 12, 4, c);
    box(ic, 15, 34, 10, 3, c);
}

static void icon_blind(lv_obj_t *ic, const ui_tile_view_t *v, uint8_t open_pct, uint32_t c)
{
    /* okno: rama + szyba (niebieskawa, widoczna gdy roleta odsłonięta) + lamele z góry */
    lv_obj_t *frame = box(ic, 3, 2, 34, 34, COLOR_ACCENT_BLIND);
    lv_obj_set_style_bg_opa(frame, LV_OPA_20, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(c), 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_radius(frame, 3, 0);

    unsigned closed_pct;
    switch (v->icon) {
    case UI_ICON_BLIND_OPEN:    closed_pct = 0; break;
    case UI_ICON_BLIND_PARTIAL: closed_pct = 100u - open_pct; break;
    case UI_ICON_BLIND_MOVING:  closed_pct = 50; break;
    case UI_ICON_BLIND_CLOSED:
    default:                    closed_pct = 100; break;
    }
    int32_t h = (int32_t)(30u * closed_pct / 100u);
    for (int32_t y = 0; y < h; y += 5)
        box(ic, 5, 4 + y, 30, (h - y) >= 4 ? 4 : h - y, c);

    if (v->icon == UI_ICON_BLIND_MOVING) {
        lv_obj_t *arrow = text(ic, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_REFRESH);
        lv_obj_align(arrow, LV_ALIGN_BOTTOM_RIGHT, 2, 2);
    }
}

static void icon_plug(lv_obj_t *ic, const ui_tile_view_t *v)
{
    bool on = v->icon == UI_ICON_PLUG_ON;
    uint32_t c = on ? COLOR_ACCENT_PLUG : COLOR_ICON_OFF;
    lv_obj_t *ring = box(ic, 4, 2, 32, 32, c);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, on ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(c), 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_t *sym = text(ring, &lv_font_montserrat_14, c, LV_SYMBOL_POWER);
    lv_obj_center(sym);
}

/* ------------------------------------------------------------------ */
/* Kafelki i strony                                                    */
/* ------------------------------------------------------------------ */

/* Wspólny wygląd kafelka: urządzenie, grupa (badge > 0) lub „＋” */
static lv_obj_t *tile_draw(lv_obj_t *scr, unsigned slot, dev_type_t type, const char *label, const ui_tile_view_t *v,
                           uint8_t open_pct, bool error, bool busy, uint8_t badge)
{
    bool active = !v->dimmed && view_active(v);
    uint32_t accent = accent_color(type);

    int x, y;
    ui_tile_origin(slot, &x, &y);
    lv_obj_t *t = box(scr, x, y, UI_TILE_W, UI_TILE_H, 0);
    lv_obj_set_style_bg_color(t, v->dimmed ? rgb565(UI_COLOR_TILE_DIM) : rgb565(UI_COLOR_TILE), 0);
    /* ramka 2 px: każdy dostępny kafelek ma ramkę w kolorze stanu – roleta otwarta zielona / zamknięta niebieska,
     * światło / gniazdko wł. akcent / wył. jasnoszara; offline bursztyn; łączenie bez ramki */
    bool blind = type == DEV_TYPE_BLIND && label != NULL;
    if (active)
        lv_obj_set_style_bg_color(t, lv_color_hex(active_tile_color(type)), 0);
    if (v->offline_mark) {
        lv_obj_set_style_border_color(t, rgb565(UI_COLOR_WARN), 0);
        lv_obj_set_style_border_width(t, 2, 0);
    } else if (!v->dimmed) {
        uint16_t c = blind ? (active ? UI_COLOR_BORDER_BLIND_OPEN : UI_COLOR_BORDER_BLIND_CLOSED)
                           : UI_COLOR_TILE_BORDER;
        lv_obj_set_style_border_color(t, active && !blind ? lv_color_hex(accent) : rgb565(c), 0);
        lv_obj_set_style_border_width(t, 2, 0);
    }
    if (error) { /* brak potwierdzenia komendy (R-UI-8) */
        lv_obj_set_style_border_color(t, rgb565(UI_COLOR_ERROR), 0);
        lv_obj_set_style_border_width(t, 3, 0);
    }
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);

    /* ikona u góry na środku; lewa krawędź kafelka (x+8) pozostaje tłem */
    lv_obj_t *ic = box(t, (UI_TILE_W - 40) / 2, 6, 40, 40, 0);
    lv_obj_set_style_bg_opa(ic, LV_OPA_TRANSP, 0);
    if (v->dimmed)
        lv_obj_set_style_opa(ic, LV_OPA_50, 0);
    if (label == NULL) { /* „＋ Nowa grupa” */
        lv_obj_center(text(ic, &lv_font_montserrat_14, COLOR_TEXT_2, LV_SYMBOL_PLUS));
        label = "Nowa grupa";
    } else {
        switch (type) {
        case DEV_TYPE_LIGHT: icon_bulb(ic, v); break;
        case DEV_TYPE_PLUG:  icon_plug(ic, v); break;
        case DEV_TYPE_BLIND:
        default:
            icon_blind(ic, v, open_pct, v->dimmed ? COLOR_ICON_OFF : active ? COLOR_BLIND_OPEN : COLOR_BLIND_CLOSED);
            break;
        }
    }

    lv_obj_t *name = text(t, font_name, v->dimmed ? COLOR_TEXT_DIM : COLOR_TEXT, label);
    lv_obj_set_width(name, UI_TILE_W - 24); /* margines 12 px z obu stron */
    lv_obj_set_pos(name, 12, 48);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_height(name, 2 * lv_font_get_line_height(font_name));

    uint32_t status_color = v->dimmed ? COLOR_TEXT_DIM
                            : blind   ? (active ? COLOR_BLIND_OPEN : COLOR_BLIND_CLOSED)
                            : active  ? accent
                                      : COLOR_TEXT_2;
    lv_obj_t *status = text(t, font_status, status_color, v->status);
    if (v->warn)
        lv_obj_set_style_text_color(status, rgb565(UI_COLOR_WARN), 0);
    lv_obj_set_width(status, UI_TILE_W - 24);
    lv_obj_set_pos(status, 12, UI_TILE_H - lv_font_get_line_height(font_status) - 6);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);

    if (badge > 0) { /* grupa: plakietka z liczbą członków w prawym górnym rogu (lewa krawędź = tło) */
        lv_obj_t *b = box(t, UI_TILE_W - 28, 6, 22, 22, accent);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        char n[4];
        snprintf(n, sizeof n, "%u", (unsigned)badge);
        lv_obj_center(text(b, font_status, 0x000000, n));
    }
    if (v->offline_mark) { /* znak ostrzeżenia w lewym górnym rogu (prawy zajmuje plakietka grupy / spinner) */
        lv_obj_t *w = text(t, &lv_font_montserrat_14, 0, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(w, rgb565(UI_COLOR_WARN), 0);
        lv_obj_set_pos(w, 8, 6);
    }
    if (busy) { /* spinner w prawym górnym rogu do potwierdzenia */
        lv_obj_t *sp = lv_spinner_create(t);
        lv_obj_set_size(sp, 16, 16);
        lv_obj_set_pos(sp, UI_TILE_W - 22, 6);
        lv_obj_set_style_arc_width(sp, 3, 0);
        lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, lv_color_hex(accent), LV_PART_INDICATOR);
        lv_obj_add_flag(sp, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    return t;
}

static lv_obj_t *tile_create_device(lv_obj_t *scr, unsigned slot, size_t idx)
{
    const ui_pending_t *pend = &s_pend[idx];
    const dev_state_t *st = pend->active ? &pend->target : &s_states[idx]; /* optymistycznie do potwierdzenia */
    bool error = ui_pending_error(pend, lv_tick_get());
    s_err_shown[idx] = error;
    ui_tile_view_t v;
    ui_tile_view(s_devs[idx].type, st, &v);
    if (error)
        ui_tile_view_error(&v); /* „Brak reakcji” (online) / napis bez zmian (niedostępny) */
    return tile_draw(scr, slot, s_devs[idx].type, s_devs[idx].name, &v, st->open_pct, error, pend->active, 0);
}

static lv_obj_t *tile_create_group(lv_obj_t *scr, unsigned slot, size_t gi)
{
    const group_cfg_t *g = &s_groups.g[gi];
    group_summary_t s;
    group_view_t v;
    groups_summary(g, s_devs, s_states, s_count, &s);
    groups_tile_view(g, &s, &v);
    return tile_draw(scr, slot, g->type, g->name, &v.view, s.avg_pct, false, false, v.badge);
}

static lv_obj_t *tile_create_add(lv_obj_t *scr, unsigned slot)
{
    ui_tile_view_t v = { .icon = UI_ICON_PLUG_OFF };
    bool can = groups_can_add(&s_groups);
    v.dimmed = !can;
    snprintf(v.status, sizeof v.status, "%s", can ? "" : "Maks. 16 grup");
    return tile_draw(scr, slot, DEV_TYPE_BLIND, NULL, &v, 0, false, false, 0);
}

/* Kafelek na pozycji listy (strona * 8 + slot) albo NULL */
static const tile_ref_t *tile_ref(unsigned page, unsigned slot)
{
    size_t pos = (size_t)page * UI_TILES_PER_PAGE + slot;
    return slot < UI_TILES_PER_PAGE && pos < s_tile_count ? &s_tiles[pos] : NULL;
}

static lv_obj_t *tile_create(lv_obj_t *scr, unsigned slot, const tile_ref_t *ref)
{
    switch (ref->kind) {
    case TILE_GROUP: return tile_create_group(scr, slot, ref->index);
    case TILE_ADD:   return tile_create_add(scr, slot);
    case TILE_DEVICE:
    default:         return tile_create_device(scr, slot, ref->index);
    }
}

/* Okna nad kafelkami */
static void modals_front(void)
{
    if (s_dlg != NULL)
        lv_obj_move_foreground(s_dlg);
    if (s_gd != NULL)
        lv_obj_move_foreground(s_gd);
    if (s_ed != NULL)
        lv_obj_move_foreground(s_ed);
}

static bool group_has_device(const group_cfg_t *g, size_t idx)
{
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX; m++)
        if (strcmp(g->members[m], s_devs[idx].id) == 0)
            return true;
    return false;
}

/* Przerysowuje kafelek urządzenia idx i kafelki grup z tym urządzeniem na bieżącej stronie. */
static void tile_refresh(size_t idx)
{
    bool shown = false;
    for (unsigned slot = 0; slot < UI_TILES_PER_PAGE; slot++) {
        const tile_ref_t *ref = tile_ref(s_page, slot);
        if (ref == NULL)
            break;
        bool mine = ref->kind == TILE_DEVICE && ref->index == idx;
        if (!mine && !(ref->kind == TILE_GROUP && group_has_device(&s_groups.g[ref->index], idx)))
            continue;
        if (tiles[slot] != NULL)
            lv_obj_delete(tiles[slot]);
        tiles[slot] = tile_create(lv_screen_active(), slot, ref);
        shown |= mine;
    }
    modals_front();
    if (!shown)
        s_err_shown[idx] = false; /* kafelek poza bieżącą stroną */
}

/* Kafelek pod punktem p na bieżącej stronie albo NULL. */
static const tile_ref_t *tile_at(const lv_point_t *p)
{
    for (unsigned slot = 0; slot < UI_TILES_PER_PAGE; slot++) {
        int x, y;
        ui_tile_origin(slot, &x, &y);
        if (p->x >= x && p->x < x + UI_TILE_W && p->y >= y && p->y < y + UI_TILE_H)
            return tile_ref(s_page, slot);
    }
    return NULL;
}

/* Kropki stron (liczba zależy od listy kafelków) */
static void dots_build(void)
{
    lv_obj_t *scr = lv_screen_active();
    for (unsigned i = 0; i < sizeof dots / sizeof dots[0]; i++) {
        if (dots[i] != NULL)
            lv_obj_delete(dots[i]);
        dots[i] = NULL;
    }
    unsigned n = s_page_count < sizeof dots / sizeof dots[0] ? s_page_count : sizeof dots / sizeof dots[0];
    int32_t x0 = (UI_SCREEN_W - (int32_t)n * 16) / 2;
    for (unsigned i = 0; i < n; i++) {
        dots[i] = box(scr, x0 + (int32_t)i * 16 + 4, UI_SCREEN_H - UI_DOTS_H / 2 - 4, 8, 8, COLOR_DOT);
        lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    }
}

/* Lista kafelków: same urządzenia albo (po set_groups) urządzenia + grupy + „＋” */
static void tiles_rebuild(void)
{
    if (s_groups_on) {
        s_tile_count = groups_tile_list(&s_groups, s_count, s_tiles, MAX_TILES);
    } else {
        for (size_t i = 0; i < s_count; i++)
            s_tiles[i] = (tile_ref_t){ TILE_DEVICE, (uint8_t)i };
        s_tile_count = s_count;
    }
    s_page_count = ui_page_count(s_tile_count);
    if (s_page >= s_page_count)
        s_page = s_page_count - 1;
}

static void page_render(void)
{
    lv_obj_t *scr = lv_screen_active();
    for (unsigned slot = 0; slot < UI_TILES_PER_PAGE; slot++) {
        if (tiles[slot] != NULL) {
            lv_obj_delete(tiles[slot]);
            tiles[slot] = NULL;
        }
        const tile_ref_t *ref = tile_ref(s_page, slot);
        if (ref != NULL)
            tiles[slot] = tile_create(scr, slot, ref);
    }
    modals_front();

    char label[8];
    ui_page_label(label, sizeof label, s_page, s_page_count);
    lv_label_set_text(page_label, label);
    for (unsigned i = 0; i < s_page_count && i < sizeof dots / sizeof dots[0]; i++)
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == s_page ? COLOR_DOT_ACTIVE : COLOR_DOT), 0);
}

/* ------------------------------------------------------------------ */
/* Okno szczegółów (§6.5)                                              */
/* ------------------------------------------------------------------ */

static bool in_rect(const lv_point_t *p, int x, int y, int w, int h)
{
    return p->x >= x && p->x < x + w && p->y >= y && p->y < y + h;
}

/* Prostokąt w okno o początku (ox, oy); współrzędne x, y – ekranowe */
static lv_obj_t *modal_box(lv_obj_t *parent, int ox, int oy, int x, int y, int w, int h, uint16_t color)
{
    lv_obj_t *o = box(parent, x - ox, y - oy, w, h, 0);
    lv_obj_set_style_bg_color(o, rgb565(color), 0);
    return o;
}

static lv_obj_t *modal_button(lv_obj_t *parent, int ox, int oy, int x, int y, int w, int h, const char *label,
                              uint16_t color)
{
    lv_obj_t *b = modal_box(parent, ox, oy, x, y, w, h, color);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_center(text(b, font_name, COLOR_TEXT, label));
    return b;
}

static void dlg_button(unsigned n, const char *label, uint16_t color)
{
    modal_button(s_dlg, UI_DLG_X, UI_DLG_Y, UI_DLG_BTN_X0 + (int)n * UI_DLG_BTN_STEP, UI_DLG_BTN_Y, UI_DLG_BTN_W,
                 UI_DLG_BTN_H, label, color);
}

static void slider_show(int n, unsigned pct)
{
    if (s_slider_fill[n] == NULL)
        return;
    lv_obj_set_width(s_slider_fill[n], (int32_t)(UI_DLG_SLIDER_W * pct / 100u) + 1);
    char s[8];
    snprintf(s, sizeof s, "%u%%", pct);
    lv_label_set_text(s_slider_label[n], s);
}

/* Suwak n w wierszu y..y+h okna parent (początek ox, oy); tor UI_DLG_SLIDER_X / _W, etykieta % po lewej */
static void slider_build(lv_obj_t *parent, int ox, int oy, int n, int y, int h, uint32_t fill_color, unsigned pct)
{
    int cy = y + h / 2;
    lv_obj_t *track = modal_box(parent, ox, oy, UI_DLG_SLIDER_X, cy - 4, UI_DLG_SLIDER_W, 8, UI_COLOR_TILE);
    lv_obj_set_style_radius(track, 4, 0);
    s_slider_fill[n] = box(track, 0, 0, 1, 8, fill_color);
    lv_obj_set_style_radius(s_slider_fill[n], 4, 0);
    s_slider_label[n] = text(parent, font_status, COLOR_TEXT_2, "");
    lv_obj_set_pos(s_slider_label[n], 4 + (UI_DLG_X - ox), cy - oy - lv_font_get_line_height(font_status) / 2);
    lv_obj_set_width(s_slider_label[n], UI_DLG_SLIDER_X - UI_DLG_X - 6);
    lv_obj_set_style_text_align(s_slider_label[n], LV_TEXT_ALIGN_RIGHT, 0);
    slider_show(n, pct);
}

static void dlg_build(void)
{
    size_t idx = (size_t)s_dlg_idx;
    const dev_info_t *dev = &s_devs[idx];
    const dev_state_t *st = &s_states[idx];
    lv_obj_t *scr = lv_screen_active();

    s_slider_fill[0] = s_slider_fill[1] = NULL;
    s_dlg_stale = false;
    s_dlg = box(scr, UI_DLG_X, UI_DLG_Y, UI_DLG_W, UI_DLG_H, 0);
    lv_obj_set_style_bg_color(s_dlg, rgb565(UI_COLOR_DIALOG), 0);
    lv_obj_set_style_radius(s_dlg, 10, 0);
    lv_obj_set_style_border_color(s_dlg, lv_color_hex(accent_color(dev->type)), 0);
    lv_obj_set_style_border_width(s_dlg, 1, 0);

    lv_obj_t *title = text(s_dlg, font_bar, COLOR_TEXT, dev->name);
    lv_obj_set_pos(title, 16, 12);
    lv_obj_set_width(title, UI_DLG_CLOSE_X - UI_DLG_X - 24);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_t *close = modal_box(s_dlg, UI_DLG_X, UI_DLG_Y, UI_DLG_CLOSE_X, UI_DLG_CLOSE_Y, UI_DLG_CLOSE_W,
                                UI_DLG_CLOSE_H, UI_COLOR_TILE);
    lv_obj_set_style_radius(close, 8, 0);
    lv_obj_center(text(close, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_CLOSE));

    if (st->link != DEV_LINK_ONLINE) {
        lv_obj_t *l = text(s_dlg, font_bar, COLOR_TEXT_DIM, "Urządzenie offline");
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    switch (dev->type) {
    case DEV_TYPE_LIGHT: {
        dlg_button(0, st->on ? "Wł." : "Wył.", st->on ? UI_COLOR_TILE_ON_LIGHT : UI_COLOR_TILE);
        unsigned pct = (ui_light_level(st) + 5u) / 10u; /* V w trybie koloru, DP22 w bieli */
        slider_build(s_dlg, UI_DLG_X, UI_DLG_Y, 0, UI_DLG_SLIDER1_Y, UI_DLG_SLIDER_H, COLOR_ACCENT_LIGHT,
                     pct > 100 ? 100 : pct);
        slider_build(s_dlg, UI_DLG_X, UI_DLG_Y, 1, UI_DLG_SLIDER2_Y, UI_DLG_SLIDER_H, COLOR_TEXT_2, st->temp / 10u);
        for (unsigned i = 0; i < UI_PALETTE_N; i++) {
            lv_obj_t *sw = box(s_dlg, UI_DLG_PAL_X0 + (int)(i * UI_DLG_PAL_STEP) - UI_DLG_X, UI_DLG_PAL_Y - UI_DLG_Y,
                               UI_DLG_PAL_W, UI_DLG_PAL_H, 0);
            lv_obj_set_style_bg_color(sw, lv_color_hsv_to_rgb((uint16_t)ui_palette_hue(i), 100, 100), 0);
            lv_obj_set_style_radius(sw, 6, 0);
        }
        break;
    }
    case DEV_TYPE_BLIND:
        dlg_button(0, "Otwórz", UI_COLOR_TILE);
        dlg_button(1, "Stop", UI_COLOR_TILE);
        dlg_button(2, "Zamknij", UI_COLOR_TILE);
        slider_build(s_dlg, UI_DLG_X, UI_DLG_Y, 0, UI_DLG_SLIDER1_Y, UI_DLG_SLIDER_H, COLOR_ACCENT_BLIND,
                     st->open_pct > 100 ? 100 : st->open_pct);
        break;
    case DEV_TYPE_PLUG: {
        dlg_button(0, st->on ? "Wł." : "Wył.", st->on ? UI_COLOR_TILE_ON_PLUG : UI_COLOR_TILE);
        char readout[96];
        ui_plug_readout(readout, sizeof readout, st);
        lv_obj_t *l = text(s_dlg, font_name, COLOR_TEXT_2, readout);
        lv_obj_set_pos(l, UI_DLG_BTN_X0 - UI_DLG_X, UI_DLG_SLIDER1_Y - UI_DLG_Y);
        break;
    }
    }
}

static void dlg_close(void)
{
    if (s_dlg != NULL)
        lv_obj_delete(s_dlg);
    s_dlg = NULL;
    s_dlg_idx = -1;
    s_drag_slider = 0;
    s_slider_fill[0] = s_slider_fill[1] = NULL;
}

static void gd_close(void);
static void ed_close(void);

static void dlg_open(int idx)
{
    dlg_close();
    gd_close();
    s_dlg_idx = idx;
    s_dlg_last_input = lv_tick_get();
    dlg_build();
}

static void dlg_rebuild(void)
{
    int idx = s_dlg_idx;
    lv_obj_delete(s_dlg);
    s_dlg = NULL;
    s_dlg_idx = idx;
    dlg_build();
}

static void queue_command(size_t idx, const char *dps)
{
    if (dps[0] == '\0' || s_cmd_count >= CMD_QUEUE_LEN)
        return;
    s_cmds[s_cmd_count].idx = idx;
    snprintf(s_cmds[s_cmd_count].dps, sizeof s_cmds[0].dps, "%s", dps);
    s_cmd_count++;
}

static void queue_group_commands(const group_cmd_t *cmds, size_t n)
{
    for (size_t i = 0; i < n; i++)
        queue_command(cmds[i].idx, cmds[i].dps);
}

/* Komenda z otwartego okna: okno grupy -> zaznaczeni członkowie online, okno urządzenia -> urządzenie */
static void send_dps(const char *dps)
{
    if (s_gd != NULL) {
        group_cmd_t cmds[GROUP_MEMBERS_MAX];
        size_t n = groups_command(&s_groups.g[s_gd_idx], s_devs, s_states, s_count, s_gd_mask, dps, cmds,
                                  GROUP_MEMBERS_MAX);
        queue_group_commands(cmds, n);
    } else if (s_dlg != NULL) {
        queue_command((size_t)s_dlg_idx, dps);
    }
}

static dev_type_t modal_type(void)
{
    return s_gd != NULL ? s_groups.g[s_gd_idx].type : s_devs[s_dlg_idx].type;
}

/* Komenda dla wartości suwaka n (0 = jasność / pozycja, 1 = barwa) */
static void slider_command(int n, unsigned pct)
{
    char dps[64];
    if (modal_type() == DEV_TYPE_BLIND) {
        tuya_dps_blind_position(dps, sizeof dps, pct);
    } else if (n == 0) {
        /* jasność w bieżącym trybie urządzenia: biel DP22, kolor DP24 z jego odcieniem (także członkowie grupy) */
        unsigned value = ui_brightness_from_pct(pct);
        if (s_gd != NULL) {
            group_cmd_t cmds[GROUP_MEMBERS_MAX];
            size_t k = groups_command_level(&s_groups.g[s_gd_idx], s_devs, s_states, s_count, s_gd_mask, value,
                                            cmds, GROUP_MEMBERS_MAX);
            queue_group_commands(cmds, k);
            return;
        }
        tuya_dps_light_level(dps, sizeof dps, &s_states[s_dlg_idx], value);
    } else
        tuya_dps_temperature(dps, sizeof dps, pct * 10u);
    send_dps(dps);
}

static bool dlg_controls_enabled(void)
{
    return s_states[s_dlg_idx].link == DEV_LINK_ONLINE;
}

static void slider_dragging(const lv_point_t *p, bool released);

/* Początek przeciągania, gdy p trafia w suwak (wiersze y1 / y2 o wysokości h) */
static void slider_try_press(const lv_point_t *p, int y1, int y2, int h)
{
    dev_type_t type = modal_type();
    int n = 0;
    if (type != DEV_TYPE_PLUG && in_rect(p, UI_DLG_SLIDER_X - 8, y1, UI_DLG_SLIDER_W + 16, h))
        n = 1;
    else if (type == DEV_TYPE_LIGHT && in_rect(p, UI_DLG_SLIDER_X - 8, y2, UI_DLG_SLIDER_W + 16, h))
        n = 2;
    if (n == 0)
        return;
    s_drag_slider = n;
    ui_drag_begin(&s_drag);
    slider_dragging(p, false); /* dotknięcie suwaka = komenda od razu */
}

static void dlg_pressed(const lv_point_t *p)
{
    s_dlg_last_input = lv_tick_get();
    if (dlg_controls_enabled())
        slider_try_press(p, UI_DLG_SLIDER1_Y, UI_DLG_SLIDER2_Y, UI_DLG_SLIDER_H);
}

static void gd_rebuild(void);

static void slider_dragging(const lv_point_t *p, bool released)
{
    int n = s_drag_slider - 1;
    unsigned pct = ui_slider_value(p->x);
    slider_show(n, pct);
    bool send = released ? ui_drag_end(&s_drag, lv_tick_get(), pct) : ui_drag_move(&s_drag, lv_tick_get(), pct);
    if (send)
        slider_command(n, pct);
    if (released) {
        s_drag_slider = 0;
        if (s_dlg_stale && s_gd != NULL)
            gd_rebuild();
        else if (s_dlg_stale && s_dlg != NULL)
            dlg_rebuild();
    }
}

/* Puszczenie bez przeciągania suwaka: przyciski, paleta, ✕, poza oknem */
static void dlg_released(const lv_point_t *p)
{
    s_dlg_last_input = lv_tick_get();
    if (!in_rect(p, UI_DLG_X, UI_DLG_Y, UI_DLG_W, UI_DLG_H) ||
        in_rect(p, UI_DLG_CLOSE_X, UI_DLG_CLOSE_Y, UI_DLG_CLOSE_W, UI_DLG_CLOSE_H)) {
        dlg_close();
        return;
    }
    if (!dlg_controls_enabled())
        return;

    const dev_state_t *st = &s_states[s_dlg_idx];
    dev_type_t type = s_devs[s_dlg_idx].type;
    char dps[64] = "";
    for (unsigned b = 0; b < 3; b++) {
        if (!in_rect(p, UI_DLG_BTN_X0 + (int)b * UI_DLG_BTN_STEP, UI_DLG_BTN_Y, UI_DLG_BTN_W, UI_DLG_BTN_H))
            continue;
        if (type == DEV_TYPE_BLIND)
            tuya_dps_blind_control(dps, sizeof dps, b == 0 ? "open" : b == 1 ? "stop" : "close");
        else if (b == 0)
            tuya_dps_switch(dps, sizeof dps, type, !st->on);
    }
    if (type == DEV_TYPE_LIGHT) {
        for (unsigned i = 0; i < UI_PALETTE_N; i++)
            if (in_rect(p, UI_DLG_PAL_X0 + (int)(i * UI_DLG_PAL_STEP), UI_DLG_PAL_Y, UI_DLG_PAL_W, UI_DLG_PAL_H))
                tuya_dps_palette(dps, sizeof dps, st, ui_palette_hue(i)); /* bieżąca jasność żarówki */
    }
    send_dps(dps);
}

/* ------------------------------------------------------------------ */
/* Okno grupy (§6.5 GROUP, R-UI-7) – pełny ekran                       */
/* ------------------------------------------------------------------ */

#define GD_VISIBLE_MEMBERS 12u /* siatka 3 x 4 */

/* Czy którykolwiek zaznaczony członek online jest wł. */
static bool gd_any_selected_on(const group_cfg_t *g)
{
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX; m++) {
        int d = groups_find_device(s_devs, s_count, g->members[m]);
        if ((s_gd_mask & (1u << m)) && d >= 0 && s_states[d].link == DEV_LINK_ONLINE && s_states[d].on)
            return true;
    }
    return false;
}

/* Pierwszy zaznaczony członek online (do pokazania wartości suwaków) albo NULL */
static const dev_state_t *gd_first_selected(const group_cfg_t *g)
{
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX; m++) {
        int d = groups_find_device(s_devs, s_count, g->members[m]);
        if ((s_gd_mask & (1u << m)) && d >= 0 && s_states[d].link == DEV_LINK_ONLINE)
            return &s_states[d];
    }
    return NULL;
}

static void gd_button(unsigned n, const char *label, uint16_t color)
{
    modal_button(s_gd, 0, 0, UI_GD_BTN_X0 + (int)n * UI_GD_BTN_STEP, UI_GD_BTN_Y, UI_GD_BTN_W, UI_GD_BTN_H, label,
                 color);
}

static void gd_build(void)
{
    const group_cfg_t *g = &s_groups.g[s_gd_idx];
    s_slider_fill[0] = s_slider_fill[1] = NULL;
    s_dlg_stale = false;
    s_gd = box(lv_screen_active(), 0, 0, UI_SCREEN_W, UI_SCREEN_H, 0);
    lv_obj_set_style_bg_color(s_gd, rgb565(UI_COLOR_DIALOG), 0);

    char name[GROUP_NAME_LEN + 16];
    if (g->member_count > GD_VISIBLE_MEMBERS) /* ukryci członkowie nie dostają komend z okna */
        snprintf(name, sizeof name, "%s (%u z %u)", g->name, GD_VISIBLE_MEMBERS, (unsigned)g->member_count);
    else
        snprintf(name, sizeof name, "%s", g->name);
    lv_obj_t *title = text(s_gd, font_bar, COLOR_TEXT, name);
    lv_obj_set_pos(title, 8, 10);
    lv_obj_set_width(title, UI_GD_EDIT_X - 16);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    modal_button(s_gd, 0, 0, UI_GD_EDIT_X, UI_GD_TOP_Y, UI_GD_EDIT_W, UI_GD_TOP_H, "Edytuj", UI_COLOR_TILE);
    lv_obj_t *close = modal_box(s_gd, 0, 0, UI_GD_CLOSE_X, UI_GD_TOP_Y, UI_GD_CLOSE_W, UI_GD_TOP_H, UI_COLOR_TILE);
    lv_obj_set_style_radius(close, 8, 0);
    lv_obj_center(text(close, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_CLOSE));

    /* członkowie: pole wyboru = tło w kolorze typu, gdy zaznaczony */
    for (unsigned m = 0; m < g->member_count && m < GD_VISIBLE_MEMBERS; m++) {
        int d = groups_find_device(s_devs, s_count, g->members[m]);
        bool sel = (s_gd_mask & (1u << m)) != 0;
        uint16_t bg = sel ? (g->type == DEV_TYPE_LIGHT ? UI_COLOR_TILE_ON_LIGHT
                             : g->type == DEV_TYPE_PLUG ? UI_COLOR_TILE_ON_PLUG : UI_COLOR_TILE_ON_BLIND)
                          : UI_COLOR_TILE;
        lv_obj_t *c = modal_box(s_gd, 0, 0, UI_GD_CELL_X0 + (int)(m % 3) * UI_GD_CELL_DX,
                                UI_GD_CELL_Y0 + (int)(m / 3) * UI_GD_CELL_DY, UI_GD_CELL_W, UI_GD_CELL_H, bg);
        lv_obj_set_style_radius(c, 6, 0);
        char label[48];
        if (d < 0)
            snprintf(label, sizeof label, "%s brak urządzenia", sel ? LV_SYMBOL_OK : "");
        else
            snprintf(label, sizeof label, "%s%s", s_devs[d].name, s_states[d].link == DEV_LINK_ONLINE ? "" : " (offline)");
        lv_obj_t *l = text(c, font_status, d < 0 || s_states[d].link != DEV_LINK_ONLINE ? COLOR_TEXT_DIM : COLOR_TEXT,
                           label);
        lv_obj_set_width(l, UI_GD_CELL_W - 12);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    }

    const dev_state_t *first = gd_first_selected(g);
    switch (g->type) {
    case DEV_TYPE_LIGHT: {
        bool on = gd_any_selected_on(g);
        gd_button(0, on ? "Wł." : "Wył.", on ? UI_COLOR_TILE_ON_LIGHT : UI_COLOR_TILE);
        unsigned pct = first ? (ui_light_level(first) + 5u) / 10u : 0;
        slider_build(s_gd, 0, 0, 0, UI_GD_SLIDER1_Y, UI_GD_SLIDER_H, COLOR_ACCENT_LIGHT, pct > 100 ? 100 : pct);
        slider_build(s_gd, 0, 0, 1, UI_GD_SLIDER2_Y, UI_GD_SLIDER_H, COLOR_TEXT_2, first ? first->temp / 10u : 0);
        break;
    }
    case DEV_TYPE_BLIND: {
        group_summary_t s;
        groups_summary(g, s_devs, s_states, s_count, &s);
        gd_button(0, "Otwórz", UI_COLOR_TILE);
        gd_button(1, "Stop", UI_COLOR_TILE);
        gd_button(2, "Zamknij", UI_COLOR_TILE);
        slider_build(s_gd, 0, 0, 0, UI_GD_SLIDER1_Y, UI_GD_SLIDER_H, COLOR_ACCENT_BLIND, s.avg_pct);
        break;
    }
    case DEV_TYPE_PLUG: {
        bool on = gd_any_selected_on(g);
        gd_button(0, on ? "Wł." : "Wył.", on ? UI_COLOR_TILE_ON_PLUG : UI_COLOR_TILE);
        break;
    }
    }
}

static void gd_close(void)
{
    if (s_gd != NULL)
        lv_obj_delete(s_gd);
    s_gd = NULL;
    s_gd_idx = -1;
    s_drag_slider = 0;
    s_slider_fill[0] = s_slider_fill[1] = NULL;
}

static void gd_open(int gi)
{
    dlg_close();
    gd_close();
    s_gd_idx = gi;
    /* domyślnie zaznaczeni wszyscy widoczni (max 12) – ukrytych okno nie steruje; kafelek grupy steruje wszystkimi */
    unsigned mc = s_groups.g[gi].member_count;
    s_gd_mask = (uint16_t)((1u << (mc < GD_VISIBLE_MEMBERS ? mc : GD_VISIBLE_MEMBERS)) - 1u);
    s_gd_last_input = lv_tick_get();
    gd_build();
}

static void gd_rebuild(void)
{
    int gi = s_gd_idx;
    lv_obj_delete(s_gd);
    s_gd = NULL;
    s_gd_idx = gi;
    gd_build();
}

static void gd_pressed(const lv_point_t *p)
{
    s_gd_last_input = lv_tick_get();
    slider_try_press(p, UI_GD_SLIDER1_Y, UI_GD_SLIDER2_Y, UI_GD_SLIDER_H);
}

static void ed_open_edit(int gi);

static void gd_released(const lv_point_t *p)
{
    s_gd_last_input = lv_tick_get();
    if (in_rect(p, UI_GD_CLOSE_X, UI_GD_TOP_Y, UI_GD_CLOSE_W, UI_GD_TOP_H)) {
        gd_close();
        return;
    }
    if (in_rect(p, UI_GD_EDIT_X, UI_GD_TOP_Y, UI_GD_EDIT_W, UI_GD_TOP_H)) {
        int gi = s_gd_idx;
        gd_close();
        ed_open_edit(gi);
        return;
    }
    const group_cfg_t *g = &s_groups.g[s_gd_idx];
    for (unsigned m = 0; m < g->member_count && m < GD_VISIBLE_MEMBERS; m++) {
        if (in_rect(p, UI_GD_CELL_X0 + (int)(m % 3) * UI_GD_CELL_DX, UI_GD_CELL_Y0 + (int)(m / 3) * UI_GD_CELL_DY,
                    UI_GD_CELL_W, UI_GD_CELL_H)) {
            s_gd_mask ^= (uint16_t)(1u << m); /* zaznaczenie nie zmienia składu grupy */
            gd_rebuild();
            return;
        }
    }
    char dps[64] = "";
    for (unsigned b = 0; b < 3; b++) {
        if (!in_rect(p, UI_GD_BTN_X0 + (int)b * UI_GD_BTN_STEP, UI_GD_BTN_Y, UI_GD_BTN_W, UI_GD_BTN_H))
            continue;
        if (g->type == DEV_TYPE_BLIND)
            tuya_dps_blind_control(dps, sizeof dps, b == 0 ? "open" : b == 1 ? "stop" : "close");
        else if (b == 0)
            tuya_dps_switch(dps, sizeof dps, g->type, !gd_any_selected_on(g));
    }
    send_dps(dps);
}

/* ------------------------------------------------------------------ */
/* Edytor grup (§6.7) – pełny ekran                                    */
/* ------------------------------------------------------------------ */

#define ED_VISIBLE_CANDIDATES 12u /* siatka 2 x 6 */

/* Grupy zmienione przez edytor: nowa lista kafelków, handler w gui_task */
static void groups_changed(void)
{
    tiles_rebuild();
    dots_build();
    page_render();
    s_groups_dirty = true;
}

/* Najdalsza pozycja kafelka grupy: wszystkie kafelki bez „＋” i bez edytowanej grupy */
static unsigned ed_position_max(void)
{
    return (unsigned)(s_count + s_groups.count) - (s_editor.group >= 0 ? 1u : 0u);
}

static void ed_build_keyboard(void)
{
    for (unsigned r = 0; r < UI_KBD_ROWS; r++) {
        for (unsigned c = 0; c < UI_KBD_COLS; c++) {
            int x = (int)c * UI_KBD_KEY_W, y = UI_KBD_Y0 + (int)r * UI_KBD_KEY_H;
            const char *k = ui_kbd_key_at_layout(x + UI_KBD_KEY_W / 2, y + UI_KBD_KEY_H / 2, kbd_layout());
            if (k == NULL)
                continue;
            bool space = k[0] == ' ';
            if (space && c > 8)
                continue; /* spacja rysowana raz, na kolumnach 8..9 */
            int w = space ? 2 * UI_KBD_KEY_W : UI_KBD_KEY_W;
            bool shift_key = strcmp(k, UI_KBD_SHIFT) == 0;
            lv_obj_t *b = modal_box(s_ed, 0, 0, x + 2, y + 2, w - 4, UI_KBD_KEY_H - 4,
                                    shift_key && s_shift ? UI_COLOR_TILE_ON_BLIND : UI_COLOR_TILE);
            lv_obj_set_style_radius(b, 4, 0);
            if (strcmp(k, UI_KBD_BACKSPACE) == 0)
                lv_obj_center(text(b, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_BACKSPACE));
            else if (shift_key)
                lv_obj_center(text(b, &lv_font_montserrat_14, COLOR_TEXT, LV_SYMBOL_UP));
            else if (strcmp(k, UI_KBD_MODE) == 0)
                lv_obj_center(text(b, font_name, COLOR_TEXT, s_digits ? "abc" : "123"));
            else
                lv_obj_center(text(b, font_name, COLOR_TEXT, space ? "spacja" : k));
        }
    }
}

static void ed_build(void)
{
    static const char *const steps[] = { "Typ", "Członkowie", "Nazwa", "Pozycja" };
    group_editor_t *ed = &s_editor;
    s_ed = box(lv_screen_active(), 0, 0, UI_SCREEN_W, UI_SCREEN_H, 0);
    lv_obj_set_style_bg_color(s_ed, rgb565(UI_COLOR_DIALOG), 0);

    modal_button(s_ed, 0, 0, UI_ED_BACK_X, UI_ED_BTN_Y, UI_ED_BTN_W, UI_ED_BTN_H, "Wstecz", UI_COLOR_TILE);
    bool last = ed->step == ED_STEP_POSITION;
    modal_button(s_ed, 0, 0, UI_ED_NEXT_X, UI_ED_BTN_Y, UI_ED_BTN_W, UI_ED_BTN_H, last ? "Zapisz" : "Dalej",
                 ed_can_next(ed) ? UI_COLOR_TILE_ON_BLIND : UI_COLOR_TILE_DIM);
    char title[48];
    snprintf(title, sizeof title, "%s: krok %u/4, %s", ed->group >= 0 ? "Edycja grupy" : "Nowa grupa",
             (unsigned)ed->step + 1u, steps[ed->step]);
    lv_obj_t *t = text(s_ed, font_name, COLOR_TEXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);

    switch (ed->step) {
    case ED_STEP_TYPE: {
        static const char *const names[] = { "Rolety", "Światła", "Gniazdka" };
        static const dev_type_t types[] = { DEV_TYPE_BLIND, DEV_TYPE_LIGHT, DEV_TYPE_PLUG };
        for (unsigned i = 0; i < 3; i++)
            modal_button(s_ed, 0, 0, UI_ED_TYPE_X0 + (int)i * UI_ED_TYPE_STEP, UI_ED_TYPE_Y, UI_ED_TYPE_W, UI_ED_TYPE_H,
                         names[i], ed->type_chosen && ed->type == types[i] ? UI_COLOR_TILE_ON_BLIND : UI_COLOR_TILE);
        break;
    }
    case ED_STEP_MEMBERS: {
        int cand[ED_MAX_DEVICES];
        size_t n = ed_candidates(ed, s_devs, s_count, cand, ED_MAX_DEVICES);
        for (size_t i = 0; i < n && i < ED_VISIBLE_CANDIDATES; i++) {
            bool sel = (ed->selected & (1ull << cand[i])) != 0;
            char label[48];
            snprintf(label, sizeof label, "%s %s%s", sel ? LV_SYMBOL_OK : " ", s_devs[cand[i]].name,
                     s_states[cand[i]].link == DEV_LINK_ONLINE ? "" : " (offline)");
            lv_obj_t *c = modal_box(s_ed, 0, 0, UI_ED_CELL_X0 + (int)(i % 2) * UI_ED_CELL_DX,
                                    UI_ED_CELL_Y0 + (int)(i / 2) * UI_ED_CELL_DY, UI_ED_CELL_W, UI_ED_CELL_H,
                                    sel ? UI_COLOR_TILE_ON_BLIND : UI_COLOR_TILE);
            lv_obj_set_style_radius(c, 6, 0);
            lv_obj_t *l = text(c, font_status, COLOR_TEXT, label);
            lv_obj_set_width(l, UI_ED_CELL_W - 12);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
        }
        char info[48];
        snprintf(info, sizeof info, "Wybrano %u (od 2 do 16)", ed_member_count(ed));
        if (ed->missing > 0)
            snprintf(info, sizeof info, "Wybrano %u, brak urządzenia: %u", ed_member_count(ed), (unsigned)ed->missing);
        lv_obj_t *l = text(s_ed, font_status, COLOR_TEXT_2, info);
        lv_obj_set_pos(l, UI_ED_DEL_X + UI_ED_DEL_W + 12, UI_ED_DEL_Y + 8);
        break;
    }
    case ED_STEP_NAME: {
        lv_obj_t *f = modal_box(s_ed, 0, 0, 8, 48, UI_SCREEN_W - 16, 40, UI_COLOR_TILE);
        lv_obj_set_style_radius(f, 6, 0);
        char shown[GROUP_NAME_LEN + 4];
        snprintf(shown, sizeof shown, "%s|", ed->name);
        lv_obj_align(text(f, font_bar, COLOR_TEXT, shown), LV_ALIGN_LEFT_MID, 8, 0);
        ed_build_keyboard();
        break;
    }
    case ED_STEP_POSITION: {
        char pos[64];
        snprintf(pos, sizeof pos, "Miejsce %u: strona %u, kafelek %u", ed->position + 1u,
                 ed->position / UI_TILES_PER_PAGE + 1u, ed->position % UI_TILES_PER_PAGE + 1u);
        lv_obj_t *l = text(s_ed, font_name, COLOR_TEXT, pos);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 60);
        modal_button(s_ed, 0, 0, UI_ED_POS_MINUS_X, UI_ED_POS_Y, UI_ED_POS_W, UI_ED_POS_H, "-", UI_COLOR_TILE);
        modal_button(s_ed, 0, 0, UI_ED_POS_PLUS_X, UI_ED_POS_Y, UI_ED_POS_W, UI_ED_POS_H, "+", UI_COLOR_TILE);
        break;
    }
    }

    /* „Usuń grupę” tylko przy edycji; w kroku nazwy miejsce zajmuje klawiatura */
    if (ed->group >= 0 && ed->step != ED_STEP_NAME)
        modal_button(s_ed, 0, 0, UI_ED_DEL_X, UI_ED_DEL_Y, UI_ED_DEL_W, UI_ED_DEL_H, "Usuń grupę", UI_COLOR_ERROR);

    if (ed->confirm_delete) {
        lv_obj_t *c = box(s_ed, 90, 90, 300, 110, 0);
        lv_obj_set_style_bg_color(c, rgb565(UI_COLOR_BAR), 0);
        lv_obj_set_style_radius(c, 10, 0);
        lv_obj_align(text(c, font_name, COLOR_TEXT, "Usunąć grupę?"), LV_ALIGN_TOP_MID, 0, 14);
        modal_button(s_ed, 0, 0, UI_ED_CONFIRM_OK_X, UI_ED_CONFIRM_Y, UI_ED_CONFIRM_W, UI_ED_CONFIRM_H, "Usuń",
                     UI_COLOR_ERROR);
        modal_button(s_ed, 0, 0, UI_ED_CONFIRM_CANCEL_X, UI_ED_CONFIRM_Y, UI_ED_CONFIRM_W, UI_ED_CONFIRM_H,
                     "Anuluj", UI_COLOR_TILE);
    }
}

static void ed_close(void)
{
    if (s_ed != NULL)
        lv_obj_delete(s_ed);
    s_ed = NULL;
    s_editor.active = false;
}

static void ed_rebuild(void)
{
    if (s_ed != NULL)
        lv_obj_delete(s_ed);
    s_ed = NULL;
    if (s_editor.active)
        ed_build();
}

static void ed_open_new(void)
{
    if (!groups_can_add(&s_groups))
        return; /* „Maks. 16 grup” */
    dlg_close();
    gd_close();
    ed_begin_new(&s_editor);
    s_shift = false;
    s_digits = false;
    ed_rebuild();
}

static void ed_open_edit(int gi)
{
    dlg_close();
    gd_close();
    ed_begin_edit(&s_editor, &s_groups, gi, s_devs, s_count);
    s_shift = false;
    s_digits = false;
    ed_rebuild();
}

static void ed_released(const lv_point_t *p)
{
    group_editor_t *ed = &s_editor;
    if (ed->confirm_delete) {
        if (in_rect(p, UI_ED_CONFIRM_OK_X, UI_ED_CONFIRM_Y, UI_ED_CONFIRM_W, UI_ED_CONFIRM_H)) {
            if (ed_delete(ed, &s_groups)) {
                ed_close();
                groups_changed();
                return;
            }
        }
        if (in_rect(p, UI_ED_CONFIRM_CANCEL_X, UI_ED_CONFIRM_Y, UI_ED_CONFIRM_W, UI_ED_CONFIRM_H))
            ed->confirm_delete = false;
        ed_rebuild();
        return;
    }

    if (in_rect(p, UI_ED_BACK_X, UI_ED_BTN_Y, UI_ED_BTN_W, UI_ED_BTN_H)) {
        ed_back(ed);
        if (!ed->active) {
            ed_close(); /* porzucenie zmian */
            return;
        }
    } else if (in_rect(p, UI_ED_NEXT_X, UI_ED_BTN_Y, UI_ED_BTN_W, UI_ED_BTN_H)) {
        if (ed->step == ED_STEP_POSITION) {
            if (ed_save(ed, &s_groups, s_devs, s_count)) {
                ed_close();
                groups_changed();
                return;
            }
        } else {
            ed_next(ed);
        }
    } else if (ed->group >= 0 && ed->step != ED_STEP_NAME &&
               in_rect(p, UI_ED_DEL_X, UI_ED_DEL_Y, UI_ED_DEL_W, UI_ED_DEL_H)) {
        ed->confirm_delete = true;
    } else {
        switch (ed->step) {
        case ED_STEP_TYPE: {
            static const dev_type_t types[] = { DEV_TYPE_BLIND, DEV_TYPE_LIGHT, DEV_TYPE_PLUG };
            for (unsigned i = 0; i < 3; i++)
                if (in_rect(p, UI_ED_TYPE_X0 + (int)i * UI_ED_TYPE_STEP, UI_ED_TYPE_Y, UI_ED_TYPE_W, UI_ED_TYPE_H))
                    ed_select_type(ed, types[i]);
            break;
        }
        case ED_STEP_MEMBERS: {
            int cand[ED_MAX_DEVICES];
            size_t n = ed_candidates(ed, s_devs, s_count, cand, ED_MAX_DEVICES);
            for (size_t i = 0; i < n && i < ED_VISIBLE_CANDIDATES; i++)
                if (in_rect(p, UI_ED_CELL_X0 + (int)(i % 2) * UI_ED_CELL_DX, UI_ED_CELL_Y0 + (int)(i / 2) * UI_ED_CELL_DY,
                            UI_ED_CELL_W, UI_ED_CELL_H))
                    ed_toggle_member(ed, s_devs, s_count, (size_t)cand[i]);
            break;
        }
        case ED_STEP_NAME: {
            const char *k = ui_kbd_key_at_layout(p->x, p->y, kbd_layout());
            if (k == NULL)
                break;
            if (strcmp(k, UI_KBD_MODE) == 0) {
                s_digits = !s_digits; /* „123” / „abc” */
                s_shift = false;
            } else if (strcmp(k, UI_KBD_SHIFT) == 0) {
                s_shift = !s_shift;
            } else if (strcmp(k, UI_KBD_BACKSPACE) == 0) {
                ed_name_backspace(ed);
            } else {
                ed_name_append(ed, k);
                s_shift = false; /* shift na jeden znak */
            }
            break;
        }
        case ED_STEP_POSITION:
            if (in_rect(p, UI_ED_POS_MINUS_X, UI_ED_POS_Y, UI_ED_POS_W, UI_ED_POS_H) && ed->position > 0)
                ed->position--;
            else if (in_rect(p, UI_ED_POS_PLUS_X, UI_ED_POS_Y, UI_ED_POS_W, UI_ED_POS_H) &&
                     ed->position < ed_position_max())
                ed->position++;
            break;
        }
    }
    ed_rebuild();
}

/* ------------------------------------------------------------------ */
/* Gesty (§6.3)                                                        */
/* ------------------------------------------------------------------ */

/* Dotknięcie kafelka: urządzenie -> handler, grupa -> fan-out, „＋” -> edytor */
static void tile_tapped(const tile_ref_t *ref)
{
    if (ref == NULL)
        return;
    switch (ref->kind) {
    case TILE_DEVICE:
        s_pending_tap = ref->index;
        break;
    case TILE_GROUP: {
        group_cmd_t cmds[GROUP_MEMBERS_MAX];
        size_t n = groups_toggle(&s_groups.g[ref->index], s_devs, s_states, s_count, cmds, GROUP_MEMBERS_MAX);
        queue_group_commands(cmds, n);
        break;
    }
    case TILE_ADD:
        ed_open_new();
        break;
    }
}

static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL)
        return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        press_pt = p;
        press_tick = lv_tick_get();
        s_long_fired = false;
        if (s_ed != NULL)
            return; /* edytor reaguje na puszczenie */
        if (s_gd != NULL)
            gd_pressed(&p);
        else if (s_dlg != NULL)
            dlg_pressed(&p);
    } else if (code == LV_EVENT_PRESSING) {
        if (s_ed != NULL || s_gd != NULL || s_dlg != NULL) {
            if (s_drag_slider != 0)
                slider_dragging(&p, false);
            return;
        }
        /* przytrzymanie: okno otwiera się po przekroczeniu progu, jeszcze przed puszczeniem */
        if (!s_long_fired &&
            ui_gesture_classify(lv_tick_elaps(press_tick), p.x - press_pt.x, p.y - press_pt.y) == UI_GESTURE_LONG_PRESS) {
            s_long_fired = true;
            const tile_ref_t *ref = tile_at(&press_pt);
            if (ref != NULL && ref->kind == TILE_DEVICE)
                dlg_open(ref->index);
            else if (ref != NULL && ref->kind == TILE_GROUP)
                gd_open(ref->index);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_long_fired)
            return; /* przytrzymanie już obsłużone */
        if (s_ed != NULL) {
            ed_released(&press_pt);
            return;
        }
        if (s_gd != NULL || s_dlg != NULL) {
            if (s_drag_slider != 0)
                slider_dragging(&p, true);
            else if (s_gd != NULL)
                gd_released(&press_pt);
            else
                dlg_released(&press_pt);
            return;
        }
        ui_gesture_t g = ui_gesture_classify(lv_tick_elaps(press_tick), p.x - press_pt.x, p.y - press_pt.y);
        unsigned next = ui_page_after_gesture(s_page, s_page_count, g);
        if (next != s_page) {
            s_page = next;
            page_render();
        } else if (g == UI_GESTURE_TAP) {
            tile_tapped(tile_at(&press_pt));
        }
    }
}

static void screen_build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, rgb565(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *bar = box(scr, 0, 0, UI_SCREEN_W, UI_BAR_H, 0);
    lv_obj_set_style_bg_color(bar, rgb565(UI_COLOR_BAR), 0);
    lv_obj_t *title = text(bar, font_bar, COLOR_TEXT, "Smarthouse");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
    net_label = text(bar, font_bar, COLOR_ACCENT_BLIND, "Brak sieci");
    lv_obj_align(net_label, LV_ALIGN_CENTER, 40, 0);
    page_label = text(bar, font_bar, COLOR_TEXT_2, "");
    lv_obj_align(page_label, LV_ALIGN_RIGHT_MID, -10, 0);

    dots_build();
    page_render();
}

static void gui_task(void *arg)
{
    (void)arg;
    static cmd_t cmds[CMD_QUEUE_LEN];
    int wdg_id = app_wdg_register("gui");
    for (;;) {
        app_wdg_alive(wdg_id);
        lock();
        lv_timer_handler();
        uint32_t now = lv_tick_get();
        /* bezczynność (§6.6): strona 1 tylko bez otwartego okna / edytora; przyciemnienie zawsze */
        unsigned idle = ui_idle_tick(&s_idle, now, s_idle_home_ms, s_idle_dim_ms);
        if ((idle & UI_IDLE_GO_HOME) && s_dlg == NULL && s_gd == NULL && !s_editor.active && s_page != 0) {
            s_page = 0;
            page_render();
        }
        if (idle & UI_IDLE_DIM)
            backlight_apply(UI_BACKLIGHT_DIM_PCT);
        for (size_t i = 0; i < s_count; i++) {
            /* brak potwierdzenia -> stan rzeczywisty + obrys; koniec obrysu -> zwykły kafelek */
            if (ui_pending_tick(&s_pend[i], now) || (s_err_shown[i] && !ui_pending_error(&s_pend[i], now)))
                tile_refresh(i);
        }
        if (s_dlg != NULL && s_drag_slider == 0 && ui_details_should_close(s_dlg_last_input, now))
            dlg_close();
        if (s_gd != NULL && s_drag_slider == 0 && ui_details_should_close(s_gd_last_input, now))
            gd_close();
        int tap = s_pending_tap;
        s_pending_tap = -1;
        if (tap >= 0 && s_states[tap].link != DEV_LINK_ONLINE) {
            /* niedostępne urządzenie: bez komendy, czerwony obrys na 2 s (przerysowanie poza callbackiem LVGL) */
            ui_pending_reject(&s_pend[tap], now);
            tile_refresh((size_t)tap);
            tap = -1;
        }
        unsigned n_cmds = s_cmd_count;
        memcpy(cmds, s_cmds, n_cmds * sizeof cmds[0]);
        s_cmd_count = 0;
        bool groups_dirty = s_groups_dirty;
        if (groups_dirty)
            s_groups_out = s_groups; /* kopia dla handlera – zapis flash poza gui_lock */
        s_groups_dirty = false;
        unlock();

        app_display_groups_handler_t groups_handler = s_groups_handler;
        if (groups_dirty && groups_handler != NULL)
            groups_handler(&s_groups_out);

        app_display_tap_handler_t handler = s_tap_handler;
        if (tap >= 0 && handler != NULL)
            handler((size_t)tap);
        app_display_command_handler_t cmd_handler = s_cmd_handler;
        for (unsigned i = 0; i < n_cmds && cmd_handler != NULL; i++)
            cmd_handler(cmds[i].idx, cmds[i].dps);
        vTaskDelay(pdMS_TO_TICKS(GUI_PERIOD_MS));
    }
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

int app_display_start(const dev_info_t *devs, const dev_state_t *states, size_t n)
{
    if (s_started)
        return 0;
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return -1;
    if (gui_lock == NULL && (gui_lock = xSemaphoreCreateMutex()) == NULL)
        return -1;

    int r = hw_init();
    if (r != 0)
        return r;

    if (n > MAX_DEVICES)
        n = MAX_DEVICES;
    s_devs = devs;
    memcpy(s_states, states, n * sizeof *states);
    s_count = n;
    s_page = 0;
    tiles_rebuild();

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(UI_SCREEN_W, UI_SCREEN_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    screen_build();
    lv_refr_now(disp);
    ui_idle_init(&s_idle, lv_tick_get());

    if (xTaskCreate(gui_task, "gui", GUI_TASK_STACK_WORDS, NULL, osPriorityBelowNormal, NULL) != pdPASS)
        return -5;
    s_started = true;
    return 0;
}

uint32_t app_display_framebuffer(void)
{
    return s_started ? LTDC_Layer1->CFBAR : 0u;
}

bool app_display_touch_ready(void)
{
    return s_touch_ok;
}

/* Podświetlenie i bezczynność (spec: system-reliability) */
void app_display_set_backlight(uint8_t pct)
{
    if (!s_started)
        return;
    lock();
    backlight_apply(pct);
    unlock();
}

uint8_t app_display_backlight(void)
{
    return s_bl_pct;
}

void app_display_set_idle_timeouts(uint32_t home_ms, uint32_t dim_ms)
{
    if (!s_started)
        return;
    lock();
    s_idle_home_ms = home_ms;
    s_idle_dim_ms = dim_ms;
    bool dimmed = s_idle.dimmed;
    ui_idle_init(&s_idle, lv_tick_get()); /* nowy okres bezczynności */
    s_idle.dimmed = dimmed;               /* bez wybudzania – to robi tylko dotyk */
    unlock();
}

uint32_t app_display_frame_count(void)
{
    return s_frames;
}

unsigned app_display_page(void)
{
    return s_page;
}

unsigned app_display_page_count(void)
{
    return s_page_count;
}

void app_display_show_page(unsigned page)
{
    if (!s_started || page >= s_page_count)
        return;
    lock();
    if (page != s_page) {
        s_page = page;
        page_render();
    }
    unlock();
}

void app_display_set_net_status(const net_status_t *st)
{
    if (!s_started)
        return;
    char label[24];
    ui_net_label(label, sizeof label, st);
    lock();
    if (strcmp(lv_label_get_text(net_label), label) != 0)
        lv_label_set_text(net_label, label);
    unlock();
}

void app_display_set_state(size_t idx, const dev_state_t *st)
{
    if (!s_started || idx >= s_count)
        return;
    lock();
    bool confirmed = ui_pending_confirm(&s_pend[idx], s_devs[idx].type, st);
    if (confirmed || memcmp(&s_states[idx], st, sizeof *st) != 0) {
        s_states[idx] = *st;
        tile_refresh(idx);
        if (s_dlg_idx == (int)idx) {
            if (st->link != DEV_LINK_ONLINE)
                s_drag_slider = 0; /* utrata połączenia przerywa przeciąganie – bez komend do offline */
            if (s_drag_slider == 0)
                dlg_rebuild(); /* okno pokazuje stan na żywo */
            else
                s_dlg_stale = true;
        }
        if (s_gd != NULL && group_has_device(&s_groups.g[s_gd_idx], idx)) {
            if (s_drag_slider == 0)
                gd_rebuild(); /* stan członków na żywo */
            else
                s_dlg_stale = true;
        }
    }
    unlock();
}

void app_display_set_tap_handler(app_display_tap_handler_t handler)
{
    s_tap_handler = handler;
}

void app_display_set_command_handler(app_display_command_handler_t handler)
{
    s_cmd_handler = handler;
}

void app_display_inject_touch(bool pressed, int x, int y)
{
    s_inject_x = (int16_t)x;
    s_inject_y = (int16_t)y;
    s_inject = pressed ? INJECT_PRESSED : INJECT_RELEASE;
}

int app_display_details_idx(void)
{
    return s_dlg_idx;
}

void app_display_begin_pending(size_t idx, const dev_state_t *target)
{
    if (!s_started || idx >= s_count)
        return;
    lock();
    ui_pending_start(&s_pend[idx], lv_tick_get(), &s_states[idx], target);
    tile_refresh(idx);
    unlock();
}

void app_display_set_groups(const groups_t *gs)
{
    if (!s_started)
        return;
    lock();
    s_groups = *gs;
    s_groups_on = true;
    gd_close(); /* indeksy grup mogły się zmienić */
    ed_close();
    tiles_rebuild();
    dots_build();
    page_render();
    unlock();
}

void app_display_set_groups_handler(app_display_groups_handler_t handler)
{
    s_groups_handler = handler;
}

void app_display_get_groups(groups_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof *out);
        return;
    }
    lock();
    *out = s_groups;
    unlock();
}

int app_display_group_details_idx(void)
{
    return s_gd_idx;
}

int app_display_editor_step(void)
{
    return s_ed != NULL && s_editor.active ? (int)s_editor.step : -1;
}
