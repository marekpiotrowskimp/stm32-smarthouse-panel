/*
 * Warstwa platformy STM32F746G-DISCO (docs/SPEC.md E1, R-NET-1):
 * zegary 216 MHz, cache + MPU, FreeRTOS, Ethernet RMII (LAN8742A) + lwIP + DHCP.
 */
#include "app_platform.h"

#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "lan8742.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"
#include "semphr.h"
#include "sys_util.h"
#include "stm32f7xx_hal.h"
#include "task.h"

#define MAIN_TASK_STACK_WORDS  2048u
#define NET_TASK_STACK_WORDS   512u
#define ETH_IRQ_PRIORITY       6u  /* > configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (FreeRTOS API w ISR) */
#define LINK_POLL_MS           250u
#define RX_POLL_MS             100u

#define ETH_RX_BUF_SIZE_BYTES  1536u
#define ETH_RX_BUF_COUNT       12u
#define ETH_TX_BUF_SIZE_BYTES  1536u
#define ETH_DMA_MEM_SIZE       (32u * 1024u)

/* ------------------------------------------------------------------ */
/* Pamięć DMA Ethernetu – region MPU niecache'owany (D-cache włączony)  */
/* ------------------------------------------------------------------ */

typedef struct {
    ETH_DMADescTypeDef rx_desc[ETH_RX_DESC_CNT];
    ETH_DMADescTypeDef tx_desc[ETH_TX_DESC_CNT];
    uint8_t rx_buf[ETH_RX_BUF_COUNT][ETH_RX_BUF_SIZE_BYTES] __attribute__((aligned(32)));
    uint8_t tx_buf[ETH_TX_BUF_SIZE_BYTES] __attribute__((aligned(32)));
} eth_dma_mem_t;

_Static_assert(sizeof(eth_dma_mem_t) <= ETH_DMA_MEM_SIZE, "eth_dma_mem_t nie mieści się w regionie MPU");

static union {
    eth_dma_mem_t m;
    uint8_t raw[ETH_DMA_MEM_SIZE];
} eth_mem __attribute__((aligned(ETH_DMA_MEM_SIZE)));

static void fatal_error(void)
{
    __disable_irq();
    for (;;) {
    }
}

/* ------------------------------------------------------------------ */
/* Zegary, MPU, cache                                                  */
/* ------------------------------------------------------------------ */

static void mpu_config(void)
{
    MPU_Region_InitTypeDef r = { 0 };

    HAL_MPU_Disable();
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = (uint32_t)&eth_mem;
    r.Size = MPU_REGION_SIZE_32KB;
    r.SubRegionDisable = 0x00;
    r.TypeExtField = MPU_TEX_LEVEL1; /* TEX=1, C=0, B=0: normal, niecache'owany */
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void system_clock_config(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* HSE 25 MHz -> PLL: 25 / 25 * 432 / 2 = 216 MHz, PLLQ 9 -> 48 MHz */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25;
    osc.PLL.PLLN = 432;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        fatal_error();
    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
        fatal_error();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK)
        fatal_error();
}

static void reset_info_capture(void);

void app_board_init(void)
{
    reset_info_capture(); /* flagi RCC->CSR i szczegół z poprzedniego przebiegu – przed czymkolwiek innym */
    mpu_config();
    SCB_EnableICache();
    SCB_EnableDCache();
    HAL_Init();
    system_clock_config();
}

/* ------------------------------------------------------------------ */
/* HAL tick z TIM6 (SysTick należy do FreeRTOS)                        */
/* ------------------------------------------------------------------ */

static TIM_HandleTypeDef htim6;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    RCC_ClkInitTypeDef clk;
    uint32_t latency;

    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clk, &latency);
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t tim_clk = (clk.APB1CLKDivider == RCC_HCLK_DIV1) ? pclk1 : 2u * pclk1;

    htim6.Instance = TIM6;
    htim6.Init.Prescaler = tim_clk / 1000000u - 1u; /* 1 MHz */
    htim6.Init.Period = 1000u - 1u;                 /* 1 kHz */
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
        return HAL_ERROR;

    if (TickPriority >= (1u << __NVIC_PRIO_BITS))
        return HAL_ERROR;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    uwTickPrio = TickPriority;
    return HAL_TIM_Base_Start_IT(&htim6);
}

void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
        HAL_IncTick();
}

/* ------------------------------------------------------------------ */
/* FreeRTOS                                                            */
/* ------------------------------------------------------------------ */

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}

static void reset_detail_store(const char *task, const char *note);

/* Błąd krytyczny: szczegół do backup SRAM, a IWDG (jeśli działa) zrestartuje płytkę po ~4 s;
 * w logu po starcie np. „RESET: watchdog (zadanie: gui, przepełnienie stosu)” */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    reset_detail_store(name, ", przepełnienie stosu");
    fatal_error();
}

void vApplicationMallocFailedHook(void)
{
    reset_detail_store(pcTaskGetName(NULL), ", brak pamięci");
    fatal_error();
}
void app_rtos_start(void (*main_task)(void *arg))
{
    if (osKernelInitialize() != osOK)
        fatal_error();
    if (xTaskCreate(main_task, "main", MAIN_TASK_STACK_WORDS, NULL, osPriorityNormal, NULL) != pdPASS)
        fatal_error();
    osKernelStart();
    fatal_error(); /* osKernelStart wraca tylko przy błędzie */
}

bool app_rtos_is_running(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

void app_delay_ms(uint32_t ms)
{
    if (app_rtos_is_running())
        vTaskDelay(pdMS_TO_TICKS(ms));
    else
        HAL_Delay(ms);
}

uint32_t app_millis(void)
{
    return HAL_GetTick();
}

u32_t sys_now(void)
{
    return HAL_GetTick();
}

/* ------------------------------------------------------------------ */
/* Ethernet: GPIO, HAL ETH, PHY                                        */
/* ------------------------------------------------------------------ */

static ETH_HandleTypeDef heth;
static ETH_TxPacketConfigTypeDef tx_cfg;
static lan8742_Object_t phy;
static struct netif s_netif;
static uint8_t s_mac[6];
static SemaphoreHandle_t rx_sem;
static volatile bool s_net_started;

/* Pula buforów RX (wołana z zadań: rx i link) */
static uint8_t *rx_free[ETH_RX_BUF_COUNT];
static unsigned rx_free_cnt;

static void rx_pool_init(void)
{
    for (unsigned i = 0; i < ETH_RX_BUF_COUNT; i++)
        rx_free[i] = eth_mem.m.rx_buf[i];
    rx_free_cnt = ETH_RX_BUF_COUNT;
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    taskENTER_CRITICAL();
    *buff = rx_free_cnt ? rx_free[--rx_free_cnt] : NULL;
    taskEXIT_CRITICAL();
}

static void rx_pool_put(uint8_t *buff)
{
    taskENTER_CRITICAL();
    if (rx_free_cnt < ETH_RX_BUF_COUNT)
        rx_free[rx_free_cnt++] = buff;
    taskEXIT_CRITICAL();
}

/* Kopiuje bufor DMA do pbuf z puli lwIP i od razu zwraca bufor do DMA. */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, Length, PBUF_POOL);
    if (p != NULL)
        pbuf_take(p, buff, Length);
    rx_pool_put(buff);
    if (p == NULL)
        return; /* brak pamięci – ramka porzucona, TCP ją retransmituje */

    if (*pStart == NULL)
        *pStart = p;
    else
        pbuf_cat((struct pbuf *)*pStart, p);
    *pEnd = p;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *h)
{
    (void)h;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(rx_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

void ETH_IRQHandler(void)
{
    HAL_ETH_IRQHandler(&heth);
}

void HAL_ETH_MspInit(ETH_HandleTypeDef *h)
{
    (void)h;
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_ETH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* RMII: PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1, PG11 TX_EN, PG13 TXD0, PG14 TXD1 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF11_ETH;
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &g);

    HAL_NVIC_SetPriority(ETH_IRQn, ETH_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

static int32_t phy_io_init(void)
{
    HAL_ETH_SetMDIOClockRange(&heth);
    return 0;
}

static int32_t phy_io_deinit(void)
{
    return 0;
}

static int32_t phy_io_read(uint32_t addr, uint32_t reg, uint32_t *val)
{
    return HAL_ETH_ReadPHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}

static int32_t phy_io_write(uint32_t addr, uint32_t reg, uint32_t val)
{
    return HAL_ETH_WritePHYRegister(&heth, addr, reg, val) == HAL_OK ? 0 : -1;
}

static int32_t phy_io_tick(void)
{
    return (int32_t)HAL_GetTick();
}

static lan8742_IOCtx_t phy_io = { phy_io_init, phy_io_deinit, phy_io_write, phy_io_read, phy_io_tick };

static void mac_from_uid(void)
{
    const uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
    net_mac_from_uid(uid, s_mac);
}

static int eth_hw_init(void)
{
    heth.Instance = ETH;
    heth.Init.MACAddr = s_mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.RxDesc = eth_mem.m.rx_desc;
    heth.Init.TxDesc = eth_mem.m.tx_desc;
    heth.Init.RxBuffLen = ETH_RX_BUF_SIZE_BYTES;
    rx_pool_init();
    if (HAL_ETH_Init(&heth) != HAL_OK)
        return -1;

    memset(&tx_cfg, 0, sizeof tx_cfg);
    /* Sumy kontrolne IP/TCP/UDP/ICMP liczy sprzęt (lwipopts.h: CHECKSUM_GEN_* = 0) */
    tx_cfg.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    tx_cfg.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    tx_cfg.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    if (LAN8742_RegisterBusIO(&phy, &phy_io) != LAN8742_STATUS_OK)
        return -2;
    if (LAN8742_Init(&phy) != LAN8742_STATUS_OK)
        return -3;
    return 0;
}

/* ------------------------------------------------------------------ */
/* lwIP netif                                                          */
/* ------------------------------------------------------------------ */

/* Wołane przez lwIP z zablokowanym rdzeniem TCP/IP – jeden bufor TX wystarcza. */
static err_t low_level_output(struct netif *n, struct pbuf *p)
{
    (void)n;
    if (p->tot_len > ETH_TX_BUF_SIZE_BYTES)
        return ERR_BUF;

    pbuf_copy_partial(p, eth_mem.m.tx_buf, p->tot_len, 0);
    ETH_BufferTypeDef buf = { .buffer = eth_mem.m.tx_buf, .len = p->tot_len, .next = NULL };
    tx_cfg.Length = p->tot_len;
    tx_cfg.TxBuffer = &buf;

    HAL_StatusTypeDef r = HAL_ETH_Transmit(&heth, &tx_cfg, 20);
    HAL_ETH_ReleaseTxPacket(&heth);
    return r == HAL_OK ? ERR_OK : ERR_IF;
}

static err_t netif_init_cb(struct netif *n)
{
    n->name[0] = 's';
    n->name[1] = 't';
    n->output = etharp_output;
    n->linkoutput = low_level_output;
    n->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(n->hwaddr, s_mac, ETH_HWADDR_LEN);
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static void rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(rx_sem, pdMS_TO_TICKS(RX_POLL_MS));
        for (;;) {
            void *p = NULL;
            if (HAL_ETH_ReadData(&heth, &p) != HAL_OK)
                break;
            if (p != NULL && s_netif.input((struct pbuf *)p, &s_netif) != ERR_OK)
                pbuf_free((struct pbuf *)p);
        }
    }
}

static void link_set_up(int32_t phy_state)
{
    ETH_MACConfigTypeDef mc;
    HAL_ETH_GetMACConfig(&heth, &mc);
    mc.Speed = (phy_state == LAN8742_STATUS_100MBITS_FULLDUPLEX || phy_state == LAN8742_STATUS_100MBITS_HALFDUPLEX)
                   ? ETH_SPEED_100M : ETH_SPEED_10M;
    mc.DuplexMode = (phy_state == LAN8742_STATUS_100MBITS_FULLDUPLEX || phy_state == LAN8742_STATUS_10MBITS_FULLDUPLEX)
                        ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    HAL_ETH_SetMACConfig(&heth, &mc);
    HAL_ETH_Start_IT(&heth);

    LOCK_TCPIP_CORE();
    netif_set_link_up(&s_netif);
    UNLOCK_TCPIP_CORE();
}

static void link_set_down(void)
{
    HAL_ETH_Stop_IT(&heth);
    LOCK_TCPIP_CORE();
    netif_set_link_down(&s_netif);
    UNLOCK_TCPIP_CORE();
}

static void link_task(void *arg)
{
    (void)arg;
    for (;;) {
        int32_t state = LAN8742_GetLinkState(&phy);
        bool up = state >= LAN8742_STATUS_100MBITS_FULLDUPLEX && state <= LAN8742_STATUS_10MBITS_HALFDUPLEX;
        if (up && !netif_is_link_up(&s_netif))
            link_set_up(state);
        else if (!up && netif_is_link_up(&s_netif))
            link_set_down();
        vTaskDelay(pdMS_TO_TICKS(LINK_POLL_MS));
    }
}

static void tcpip_ready(void *arg)
{
    xSemaphoreGive((SemaphoreHandle_t)arg);
}

/*
 * Start jest etapowy: każdy etap wykonuje się najwyżej raz, więc ponowne wywołanie
 * po błędzie kontynuuje od nieudanego etapu (bez drugiego tcpip_init / netif_add).
 */
int app_net_start(void)
{
    static bool eth_ready, tcpip_started, netif_added, dhcp_started, rx_task_created, link_task_created;

    if (s_net_started)
        return 0;
    if (!app_rtos_is_running())
        return -1;

    mac_from_uid();
    if (rx_sem == NULL && (rx_sem = xSemaphoreCreateBinary()) == NULL)
        return -2;

    if (!eth_ready) {
        if (eth_hw_init() != 0)
            return -3;
        eth_ready = true;
    }

    if (!tcpip_started) {
        SemaphoreHandle_t ready = xSemaphoreCreateBinary();
        if (ready == NULL)
            return -2;
        tcpip_init(tcpip_ready, ready);
        xSemaphoreTake(ready, portMAX_DELAY);
        vSemaphoreDelete(ready);
        tcpip_started = true;
    }

    LOCK_TCPIP_CORE();
    if (!netif_added) {
        netif_add(&s_netif, NULL, NULL, NULL, NULL, netif_init_cb, tcpip_input);
        netif_set_default(&s_netif);
        netif_set_up(&s_netif);
        netif_added = true;
    }
    if (!dhcp_started)
        dhcp_started = (dhcp_start(&s_netif) == ERR_OK);
    UNLOCK_TCPIP_CORE();
    if (!dhcp_started)
        return -4;

    if (!rx_task_created)
        rx_task_created = xTaskCreate(rx_task, "eth_rx", NET_TASK_STACK_WORDS, NULL, osPriorityHigh, NULL) == pdPASS;
    if (!link_task_created)
        link_task_created = xTaskCreate(link_task, "eth_link", NET_TASK_STACK_WORDS, NULL, osPriorityNormal, NULL) == pdPASS;
    if (!rx_task_created || !link_task_created)
        return -5;

    s_net_started = true;
    return 0;
}

void app_net_get_status(net_status_t *st)
{
    memset(st, 0, sizeof *st);
    if (!s_net_started) {
        mac_from_uid();
        memcpy(st->mac, s_mac, sizeof st->mac);
        return;
    }

    LOCK_TCPIP_CORE();
    st->link_up = netif_is_link_up(&s_netif);
    st->has_ip = dhcp_supplied_address(&s_netif);
    if (st->has_ip) {
        st->ip = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&s_netif)));
        st->netmask = lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(&s_netif)));
        st->gw = lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(&s_netif)));
    }
    memcpy(st->mac, s_netif.hwaddr, sizeof st->mac);
    UNLOCK_TCPIP_CORE();
}

int app_net_tcp_probe(uint32_t ip, uint16_t port, uint32_t timeout_ms)
{
    if (!s_net_started)
        return -1;

    int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -2;
    lwip_fcntl(s, F_SETFL, lwip_fcntl(s, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_len = sizeof addr;
    addr.sin_family = AF_INET;
    addr.sin_port = lwip_htons(port);
    addr.sin_addr.s_addr = lwip_htonl(ip);

    int result;
    if (lwip_connect(s, (struct sockaddr *)&addr, sizeof addr) == 0) {
        result = 0;
    } else {
        /* Nieblokujące connect: wynik (sukces / RST / timeout) rozstrzyga select + SO_ERROR */
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s, &wr);
        FD_SET(s, &ex);
        struct timeval tv = { .tv_sec = (long)(timeout_ms / 1000u), .tv_usec = (long)(timeout_ms % 1000u) * 1000 };
        int n = lwip_select(s + 1, NULL, &wr, &ex, &tv);
        if (n <= 0) {
            result = -3; /* timeout */
        } else {
            int err = 0;
            socklen_t len = sizeof err;
            lwip_getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
            result = (err == 0) ? 0 : -4;
        }
    }
    lwip_close(s);
    return result;
}

/* ------------------------------------------------------------------ */
/* Niezawodność (R-SYS-1; spec: system-reliability)                    */
/* ------------------------------------------------------------------ */

#define WDG_TASK_STACK_WORDS 256u
#define WDG_CHECK_MS         500u
#define IWDG_PR_DIV64        4u    /* LSI 32 kHz / 64 = 500 Hz */
#define IWDG_RELOAD          1999u /* (1999 + 1) / 500 Hz = 4,0 s */
#define RESET_INFO_MAGIC     0x52535431u /* "RST1" */

/* Szczegół przyczyny w backup SRAM – przetrwa reset systemowy (nie utratę zasilania) */
typedef struct {
    uint32_t magic;
    char     detail[32];
} reset_info_t;

#define RESET_INFO ((volatile reset_info_t *)BKPSRAM_BASE)

static uint32_t s_reset_flags;
static char s_reset_detail[32];
static sys_wdg_t s_wdg;
static volatile bool s_wdg_running;
static volatile bool s_wdg_stalled;

static void bkpsram_enable(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR1 |= PWR_CR1_DBP;
    __HAL_RCC_BKPSRAM_CLK_ENABLE();
}

/* Flagi RCC->CSR -> SYS_RST_*, wyczyszczenie (RMVF); szczegół tylko po resecie z watchdoga */
static void reset_info_capture(void)
{
    uint32_t csr = RCC->CSR;
    uint32_t f = 0;
    if (csr & RCC_CSR_PINRSTF)
        f |= SYS_RST_PIN;
    if (csr & RCC_CSR_PORRSTF)
        f |= SYS_RST_POR;
    if (csr & RCC_CSR_SFTRSTF)
        f |= SYS_RST_SFT;
    if (csr & RCC_CSR_IWDGRSTF)
        f |= SYS_RST_IWDG;
    if (csr & RCC_CSR_WWDGRSTF)
        f |= SYS_RST_WWDG;
    if (csr & RCC_CSR_LPWRRSTF)
        f |= SYS_RST_LPWR;
    if (csr & RCC_CSR_BORRSTF)
        f |= SYS_RST_BOR;
    s_reset_flags = f;
    RCC->CSR |= RCC_CSR_RMVF;

    bkpsram_enable();
    s_reset_detail[0] = '\0';
    if ((f & SYS_RST_IWDG) && RESET_INFO->magic == RESET_INFO_MAGIC) {
        for (size_t i = 0; i + 1 < sizeof s_reset_detail; i++) {
            s_reset_detail[i] = RESET_INFO->detail[i];
            if (s_reset_detail[i] == '\0')
                break;
        }
        s_reset_detail[sizeof s_reset_detail - 1] = '\0';
    }
    RESET_INFO->magic = 0;
}

/* Zapis szczegółu: nazwa zadania + uwaga (bez blokad – wołane także z hooków przy zablokowanych przerwaniach) */
static void reset_detail_store(const char *task, const char *note)
{
    size_t n = 0;
    for (const char *s = task; s && *s && n + 1 < sizeof RESET_INFO->detail; s++)
        RESET_INFO->detail[n++] = *s;
    for (const char *s = note; s && *s && n + 1 < sizeof RESET_INFO->detail; s++)
        RESET_INFO->detail[n++] = *s;
    RESET_INFO->detail[n] = '\0';
    RESET_INFO->magic = RESET_INFO_MAGIC;
}

/* Odświeża IWDG tylko, gdy wszystkie zadania żyją; zawieszone -> zapis nazwy i brak odświeżania (reset ~4 s) */
static void wdg_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_wdg_stalled) {
            int stalled = sys_wdg_check(&s_wdg, app_millis());
            if (stalled < 0) {
                IWDG->KR = 0xAAAAu;
            } else {
                reset_detail_store(s_wdg.name[stalled], "");
                s_wdg_stalled = true;
            }
        }
        app_delay_ms(WDG_CHECK_MS);
    }
}

void app_watchdog_start(void)
{
    if (s_wdg_running)
        return;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP; /* zatrzymanie w debuggerze nie resetuje */
    IWDG->KR = 0xCCCCu;                              /* start (LSI włącza się sam) */
    IWDG->KR = 0x5555u;                              /* dostęp do PR / RLR */
    IWDG->PR = IWDG_PR_DIV64;
    IWDG->RLR = IWDG_RELOAD;
    while (IWDG->SR != 0) {
    }
    IWDG->KR = 0xAAAAu;
    if (xTaskCreate(wdg_task, "wdg", WDG_TASK_STACK_WORDS, NULL, osPriorityAboveNormal, NULL) != pdPASS)
        fatal_error(); /* IWDG działa – restart */
    s_wdg_running = true;
}

bool app_watchdog_running(void)
{
    return s_wdg_running;
}

int app_wdg_register(const char *name)
{
    taskENTER_CRITICAL();
    int id = sys_wdg_register(&s_wdg, name, app_millis());
    taskEXIT_CRITICAL();
    return id;
}

void app_wdg_alive(int id)
{
    sys_wdg_alive(&s_wdg, id, app_millis()); /* każde zadanie pisze tylko swoje pole */
}

uint32_t app_reset_flags(void)
{
    return s_reset_flags;
}

const char *app_reset_detail(void)
{
    return s_reset_detail;
}
