/* Smarthouse – punkt wejścia firmware'u (docs/SPEC.md). */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app_display.h"
#include "app_platform.h"
#include "app_storage.h"
#include "device_keys.h"
#include "device_table.h"
#include "groups.h"
#include "semphr.h"
#include "stm32f7xx_hal.h"
#include "sys_util.h"
#include "tuya_client.h"
#include "tuya_dps.h"
#include "ui_details.h"

#define STATUS_PERIOD_MS 5000u /* log stanu sieci na VCP */
#define NET_POLL_MS      1000u /* odświeżenie paska stanu */
#define HEAP_PERIOD_MS   600000u /* log sterty co 10 min (R-SYS-2) */

/* Log diagnostyczny na VCP ST-LINK: USART1 (PA9 TX / PB7 RX, AF7), 115200 8N1 */
static UART_HandleTypeDef huart1;
static SemaphoreHandle_t log_lock; /* log z zadań main i tuya */

static void log_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = { 0 };
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

static void log_line(const char *s)
{
    if (log_lock != NULL)
        xSemaphoreTake(log_lock, portMAX_DELAY);
    HAL_UART_Transmit(&huart1, (const uint8_t *)s, (uint16_t)strlen(s), 100);
    HAL_UART_Transmit(&huart1, (const uint8_t *)"\r\n", 2, 100);
    if (log_lock != NULL)
        xSemaphoreGive(log_lock);
}

/* Stany początkowe kafelków – nieznane (przyciemnione, "Łączenie…") do odpowiedzi urządzeń */
static dev_state_t device_states[32];

/* Zadanie klienta Tuya: nowy stan urządzenia -> kafelek */
static void on_device_change(size_t idx)
{
    dev_state_t st;
    if (tuya_client_get_state(idx, &st))
        app_display_set_state(idx, &st);
}

/* Zadanie GUI: dotknięcie kafelka -> stan przeciwny (§6.4) */
static void on_tile_tap(size_t idx)
{
    dev_state_t st;
    char dps[32];
    if (!tuya_client_get_state(idx, &st) || tuya_dps_toggle(dps, sizeof dps, g_devices[idx].type, &st) <= 0)
        return;
    /* nowy stan od razu, przed wysłaniem (push może przyjść szybciej niż powrót z send);
     * brak potwierdzenia – także po błędzie wysłania – daje powrót i czerwony obrys po 3 s */
    dev_state_t target;
    if (ui_optimistic_state(g_devices[idx].type, &st, &target))
        app_display_begin_pending(idx, &target);
    int r = tuya_client_send_dps(idx, dps);
    if (r != 0) {
        char line[64];
        snprintf(line, sizeof line, "TUYA send error %d", r);
        log_line(line);
    }
}

/* Zadanie GUI: komenda z okna szczegółów (§6.5) */
static void on_details_command(size_t idx, const char *dps)
{
    int r = tuya_client_send_dps(idx, dps);
    if (r != 0) {
        char line[64];
        snprintf(line, sizeof line, "TUYA send error %d", r);
        log_line(line);
    }
}

/* Grupy wirtualne (§4.4): ~6,7 KB – jeden statyczny bufor (start i zapis), nie na stosie zadań */
static groups_t groups_buf;
static volatile bool groups_save_pending;

/* Zadanie GUI: edytor zapisał / usunął grupę – tylko flaga, zapis do QSPI w pętli main (bez blokowania GUI) */
static void on_groups_changed(const groups_t *gs)
{
    (void)gs; /* aktualną listę pobiera groups_save_if_pending z app_display */
    groups_save_pending = true;
}

/* Zapis w pętli main: kopia grup z app_display, kasowanie i zapis flash bez blokady GUI;
 * zmiana w trakcie zapisu ustawia flagę ponownie, nieudany zapis jest ponawiany w kolejnym obiegu. */
static void groups_save_if_pending(void)
{
    if (!groups_save_pending)
        return;
    groups_save_pending = false;
    app_display_get_groups(&groups_buf);

    int r = app_storage_groups_save(STORAGE_GROUPS_SLOT0, STORAGE_GROUPS_SLOT1, &groups_buf);
    char line[48];
    snprintf(line, sizeof line, "GROUPS save slot %d", r);
    log_line(line);
    if (r < 0) {
        app_storage_init(); /* np. QSPI nie wstała przy starcie – kolejna próba od inicjalizacji */
        groups_save_pending = true;
    }
}

/* Grupy z QSPI albo – przy braku / uszkodzeniu obu slotów – grupy startowe (O2) */
static void groups_load_at_boot(size_t n)
{
    char line[48];
    int slot = app_storage_init() == 0
                   ? app_storage_groups_load(STORAGE_GROUPS_SLOT0, STORAGE_GROUPS_SLOT1, &groups_buf)
                   : -2;
    if (slot < 0)
        groups_defaults(g_devices, n, &groups_buf);
    snprintf(line, sizeof line, "GROUPS %u from %s", (unsigned)groups_buf.count, slot >= 0 ? "flash" : "defaults");
    log_line(line);
    app_display_set_groups_handler(on_groups_changed);
    app_display_set_groups(&groups_buf);
}

static void main_task(void *arg)
{
    (void)arg;
    char line[96];

    log_lock = xSemaphoreCreateMutex();
    log_line("Smarthouse boot");
    sys_reset_line(line, sizeof line, app_reset_flags(), app_reset_detail()); /* R-SYS-1 */
    log_line(line);

    size_t n = g_device_count < sizeof device_states / sizeof device_states[0]
                   ? g_device_count : sizeof device_states / sizeof device_states[0];
    int r = app_display_start(g_devices, device_states, n);
    if (r != 0) {
        snprintf(line, sizeof line, "DISPLAY start error %d", r);
        log_line(line);
    }
    groups_load_at_boot(n);

    r = app_net_start();
    if (r != 0) {
        snprintf(line, sizeof line, "NET start error %d", r);
        log_line(line);
    }

    /* nadzór IWDG: main, gui, tuya (tuya rejestruje się po DHCP) */
    int wdg_id = app_wdg_register("main");
    app_watchdog_start();

    bool tuya_started = false;
    for (uint32_t tick = 0;; tick++) {
        app_wdg_alive(wdg_id);
        net_status_t st;
        app_net_get_status(&st);
        app_display_set_net_status(&st);
        if (!tuya_started && st.has_ip) {
            /* klient po DHCP; konfiguracja kluczy musi odpowiadać g_devices */
            tuya_started = true;
            if (g_device_endpoint_count != g_device_count) {
                log_line("TUYA device_keys.c nieaktualny - uruchom tools/gen_device_table.py");
            } else {
                tuya_client_set_log(log_line);
                r = tuya_client_start(g_devices, g_device_endpoints, n, on_device_change);
                snprintf(line, sizeof line, "TUYA start %d", r);
                log_line(line);
                if (r == 0) {
                    app_display_set_tap_handler(on_tile_tap);
                    app_display_set_command_handler(on_details_command);
                }
            }
        }
        if (tick % (STATUS_PERIOD_MS / NET_POLL_MS) == 0) {
            net_format_status(line, sizeof line, &st);
            log_line(line);
        }
        if (tick % (HEAP_PERIOD_MS / NET_POLL_MS) == 0) { /* R-SYS-2 */
            sys_heap_line(line, sizeof line, xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize(),
                          configTOTAL_HEAP_SIZE);
            log_line(line);
        }
        groups_save_if_pending();
        app_delay_ms(NET_POLL_MS);
    }
}

int main(void)
{
    app_board_init();
    log_init();
    app_rtos_start(main_task);
    return 0;
}
