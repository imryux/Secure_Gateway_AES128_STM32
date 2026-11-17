/**
 * @file    app_main.c
 * @brief   Application initialization implementation.
 *          Orchestrates system startup: hardware GPIO pin states, logging service,
 *          dual W5500 Ethernet controllers, network configuration, AES-128 crypto
 *          self-tests, static buffer pool, and FreeRTOS gateway tasks.
 * @version 1.3
 * @date    2026-11-17
 */

#include "app_main.h"

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#include "app_config.h"
#include "bsp_gpio.h"
#include "bsp_w5500.h"
#include "bsp_watchdog.h"
#include "crypto_service.h"
#include "gateway_tasks.h"
#include "log_service.h"
#include "net_service.h"
#include "relay_service.h"

/**
 * @brief  Primary entry point for system initialization.
 *         Called once by the startup FreeRTOS task (StartDefaultTask) after
 *         the RTOS scheduler has started.
 *
 * Execution sequence:
 * 1. Set safe idle levels on GPIO lines (CS deasserted HIGH, Reset released HIGH, LED OFF).
 * 2. Initialize the thread-safe UART logging service and DWT microsecond timer.
 * 3. Initialize dual W5500 Ethernet controllers (hardware reset & ioLibrary registration).
 * 4. Verify chip hardware version registers (VERSIONR = 0x04) to confirm SPI wiring.
 * 5. Configure static IPv4 addresses, subnet masks, gateways, and open TCP/UDP sockets.
 * 6. Initialize AES-128-CBC cryptographic contexts and run NIST SP 800-38A self-test.
 * 7. Initialize static zero-malloc frame buffer pool (10 slots x 550 bytes).
 * 8. Launch all 5 static FreeRTOS worker tasks and IPC queues.
 * 9. Perform initial hardware watchdog (IWDG) refresh.
 *
 * @return APP_STATUS_OK on success, or specific error status code on failure.
 */
app_status_t app_main_init(void)
{
    /* Step 1: Set default inactive levels for W5500 control lines.
     * SPI Chip Select (CS) is active-low: false -> HIGH (deselected).
     * Hardware Reset (RST) is active-low: false -> HIGH (not in reset).
     * Onboard LED is active-low: false -> HIGH (LED OFF). */
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_B, false);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_B, false);
    bsp_gpio_led_write(false);

    /* Step 2: Initialize thread-safe debug logging and ARM Cortex-M3 DWT cycle counter */
    app_status_t status = log_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Print startup banner and system core clock frequency (typically 72 MHz) */
    (void)log_service_print(
        "System Boot OK | SYSCLK = %lu MHz\r\n",
        (unsigned long)(SystemCoreClock / 1000000UL));

    /* Step 3: Initialize SPI buses, mutex, callbacks, and perform hardware reset on both W5500s */
    status = bsp_w5500_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Step 4: Verify SPI communication by reading VERSIONR from both W5500 chips.
     * Official WIZnet W5500 silicon always returns 0x04. If anything else is read,
     * check SPI wiring, power rail, or chip integrity. */
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
        return APP_STATUS_IO_ERROR;
    }

    /* Step 5: Configure static IPv4 parameters and open sockets on both Ethernet interfaces */
    status = net_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Step 6: Initialize AES-128 cryptographic service contexts and seed PRNG */
    status = crypto_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Run standard NIST SP 800-38A F.2.1 test vector self-test to verify AES correctness */
    status = crypto_service_run_self_test();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Step 7: Initialize static 10-slot frame buffer pool and socket health supervision tables */
    status = relay_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Step 8: Create all static FreeRTOS worker tasks (RxSpi1, RxSpi2, PacketProc, Tx, Heartbeat)
     * and static message queues */
    status = gateway_tasks_start_all();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Step 9: Kick hardware watchdog (IWDG 500ms counter) to prevent reset after initialization */
    (void)bsp_watchdog_refresh();
    return APP_STATUS_OK;
}
