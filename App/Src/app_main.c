/**
 * @file    app_main.c
 * @brief   Application initialization implementation.
 * @version 1.0
 * @date    2026-11-07
 */

#include "app_main.h"

#include "stm32f1xx_hal.h"

#include "app_config.h"
#include "bsp_gpio.h"
#include "log_service.h"

app_status_t app_main_init(void)
{
	const char test_msg[] = "UART_RAW_TEST_OK\r\n";
	extern UART_HandleTypeDef huart1;
	HAL_UART_Transmit(&huart1, (uint8_t *)test_msg, sizeof(test_msg) - 1, 100);
    /* Ensure both W5500 CS and RST lines are deasserted (HIGH) at boot */
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_B, false);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_B, false);
    bsp_gpio_led_write(false);

    app_status_t status = log_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    (void)log_service_print(
        "System Boot OK | SYSCLK = %lu MHz\r\n",
        (unsigned long)(SystemCoreClock / 1000000UL));

    return APP_STATUS_OK;
}
