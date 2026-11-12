/**
 * @file    app_main.c
 * @brief   Application initialization implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "app_main.h"

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#include "app_config.h"
#include "bsp_gpio.h"
#include "bsp_w5500.h"
#include "log_service.h"
#include "net_service.h"
#include "crypto_service.h"

app_status_t app_main_init(void)
{
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

    status = bsp_w5500_init();
    if (status != APP_STATUS_OK)
    {
        (void)log_service_print("ERROR: bsp_w5500_init failed (code=%d)\r\n", (int)status);
        return status;
    }

    uint8_t ver_a = 0U;
    uint8_t ver_b = 0U;
    (void)bsp_w5500_read_version(BSP_W5500_ID_A, &ver_a);
    (void)bsp_w5500_read_version(BSP_W5500_ID_B, &ver_b);

    (void)log_service_print(
        "W5500 #1 (SPI1) VERSIONR = 0x%02X | W5500 #2 (SPI2) VERSIONR = 0x%02X\r\n",
        (unsigned int)ver_a,
        (unsigned int)ver_b);

    if ((ver_a != BSP_W5500_EXPECTED_VERSION) || (ver_b != BSP_W5500_EXPECTED_VERSION))
    {
        (void)log_service_print("ERROR: W5500 SPI verification failed!\r\n");
        return APP_STATUS_IO_ERROR;
    }

    status = net_service_init();
    if (status != APP_STATUS_OK)
    {
        (void)log_service_print("ERROR: net_service_init failed (code=%d)\r\n", (int)status);
        return status;
    }
    status = crypto_service_init();
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        status = crypto_service_run_self_test();
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        status = crypto_service_run_benchmark();
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        return APP_STATUS_OK;
    return APP_STATUS_OK;
}
