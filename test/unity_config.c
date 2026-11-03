/*
 * Wyjście Unity na STM32F746G-DISCO: USART1 (PA9 TX / PB7 RX, AF7) = VCP ST-LINK, 115200 8N1.
 * Baud liczony z bieżącego zegara, więc działa niezależnie od konfiguracji zegarów aplikacji.
 */
#include "unity_config.h"

#ifdef STM32F746xx
#include "stm32f7xx_hal.h"

static UART_HandleTypeDef huart1;

void unity_output_start(void)
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

void unity_output_char(int c)
{
    uint8_t ch = (uint8_t)c;
    HAL_UART_Transmit(&huart1, &ch, 1, 100);
}

void unity_output_flush(void) {}

void unity_output_complete(void)
{
    HAL_UART_DeInit(&huart1);
}
#endif
