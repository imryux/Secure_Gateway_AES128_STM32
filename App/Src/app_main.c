/**
 * @file    app_main.c
 * @brief   Application initialization and Power-On Self-Test (POST).
 * @details Implements system boot verification, diagnostic logging initialization,
 *          dual W5500 Ethernet controller reset, and SPI communication verification
 *          prior to launching RTOS application tasks.
 * @version 1.0
 * @date    2026-11-08
 */

/* ========================================================================== */
/*                             INCLUDE DIRECTORIES                            */
/* ========================================================================== */

/* Own header */
#include "app_main.h"

/* Standard C headers */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Vendor / HAL headers */
#include "stm32f1xx_hal.h"

/* Application, Service, and BSP headers */
#include "app_config.h"
#include "bsp_gpio.h"
#include "bsp_w5500.h"
#include "log_service.h"

/* ========================================================================== */
/*                             INTERNAL FUNCTIONS                             */
/* ========================================================================== */

/**
 * @brief  Execute power-on verification test for a specific W5500 Ethernet controller.
 * @details Reads the hardware VERSIONR register via SPI to verify bus integrity
 *          and validates the hardware silicon ID against @ref W5500_EXPECTED_VERSION.
 *          Also queries and logs the physical layer (PHY) link status.
 * @param  chip_id     Target W5500 peripheral identifier (@ref bsp_w5500_id_t).
 * @param  p_chip_name Human-readable descriptor string used for diagnostic logging.
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Communication succeeded and version matched expected ID.
 *         - @ref APP_STATUS_INVALID_ARG: Null pointer passed for p_chip_name.
 *         - @ref APP_STATUS_IO_ERROR: Version mismatch or SPI communication failure.
 */
static app_status_t app_main_verify_w5500_chip(
    bsp_w5500_id_t chip_id,
    const char *p_chip_name)
{
    /* SAF-02: Defensive pointer validation */
    if (p_chip_name == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    uint8_t version = 0U;
    bool is_link_up = false;

    /* Step 1: Read chip hardware version register via SPI */
    app_status_t status = bsp_w5500_read_version(chip_id, &version);
    if (status != APP_STATUS_OK)
    {
        (void)log_service_print(
            "[ERROR] Failed to communicate with %s (Status: %d)\r\n",
            p_chip_name,
            (int)status);
        return status;
    }

    /* Step 2: Query physical link status (non-fatal if unlinked at boot) */
    const app_status_t link_status = bsp_w5500_read_phy_link(chip_id, &is_link_up);
    if (link_status != APP_STATUS_OK)
    {
        is_link_up = false;
    }

    /* Step 3: Emit diagnostic verification log */
    (void)log_service_print(
        "%s (SPI%d) VERSIONR = 0x%02X [%s] | PHY Link: %s\r\n",
        p_chip_name,
        (chip_id == BSP_W5500_ID_A) ? 1 : 2,
        version,
        (version == W5500_EXPECTED_VERSION) ? "PASS" : "FAIL (Expected 0x04)",
        (is_link_up == true) ? "UP" : "DOWN");

    /* Step 4: Validate silicon version against expected constant (0x04) */
    if (version != W5500_EXPECTED_VERSION)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

/* ========================================================================== */
/*                              PUBLIC FUNCTIONS                              */
/* ========================================================================== */

/**
 * @brief  Initialize primary application services and perform Power-On Self-Test (POST).
 * @details Brings up diagnostic logging, retrieves system clock rate, initializes
 *          dual W5500 GPIO/SPI hardware with reset sequencing, and verifies SPI bus
 *          connectivity with both Ethernet controllers before task scheduler dispatch.
 * @return Operation status:
 *         - @ref APP_STATUS_OK: All services and peripheral checks passed.
 *         - @ref APP_STATUS_IO_ERROR: SPI connectivity or silicon validation failed.
 *         - Error code forwarded from @ref log_service_init or @ref bsp_w5500_init.
 */
app_status_t app_main_init(void)
{
    /* Step 1: Initialize logging service and hardware cycle profiler (DWT) */
    app_status_t status = log_service_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    (void)log_service_print(
        "System Boot OK | SYSCLK = %lu MHz\r\n",
        (unsigned long)(SystemCoreClock / 1000000UL));

    /* Step 2: Initialize W5500 GPIO, RTOS mutex, and hardware reset pulse */
    status = bsp_w5500_init();
    if (status != APP_STATUS_OK)
    {
        (void)log_service_print("[ERROR] W5500 BSP init failed!\r\n");
        return status;
    }

    /* Step 3: Run POST verification on both W5500 modules */
    const app_status_t status_a = app_main_verify_w5500_chip(
        BSP_W5500_ID_A,
        "W5500 #1 (Net A)");
    const app_status_t status_b = app_main_verify_w5500_chip(
        BSP_W5500_ID_B,
        "W5500 #2 (Net B)");

    /* Step 4: Validate overall POST result */
    if ((status_a != APP_STATUS_OK) || (status_b != APP_STATUS_OK))
    {
        (void)log_service_print(
            "[CRITICAL] Hardware verification failed! Check SPI wiring/power.\r\n");
        return APP_STATUS_IO_ERROR;
    }

    (void)log_service_print(
        "[SUCCESS] Both W5500 modules verified successfully!\r\n");
    return APP_STATUS_OK;
}
