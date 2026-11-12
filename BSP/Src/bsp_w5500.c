/**
 * @file    bsp_w5500.c
 * @brief   W5500 dual-chip hardware driver implementation.
 * @details Provides board-support hardware abstraction for dual WIZnet W5500
 *          Ethernet controllers, including FreeRTOS-safe bus arbitration via
 *          static mutex, hardware reset sequencing, CS line assertion, and
 *          common register access.
 * @version 1.0
 * @date    2026-11-08
 */

/* ========================================================================== */
/*                             INCLUDE DIRECTORIES                            */
/* ========================================================================== */

/* Own header */
#include "bsp_w5500.h"

/* Standard C headers */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* RTOS headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Project & BSP headers */
#include "bsp_gpio.h"
#include "bsp_spi.h"

/* ========================================================================== */
/*                           PREPROCESSOR DEFINITIONS                         */
/* ========================================================================== */

/** @brief Default timeout in milliseconds when waiting to acquire SPI bus mutex. */
#define W5500_MUTEX_TIMEOUT_DEFAULT_MS  (100U)

/** @brief Active-low duration in milliseconds to hold hardware reset line. */
#define W5500_RST_HOLD_DELAY_MS         (10U)

/** @brief Settle delay in milliseconds required after reset release for internal PLL. */
#define W5500_RST_SETTLE_DELAY_MS       (50U)

/**
 * @brief Common Register Block Read Control Byte.
 * @note  Frame layout: BSB[4:0] = 00000b (Common Block), R/W = 0 (Read), OM[1:0] = 00 (VDM).
 */
#define W5500_CTRL_COMMON_READ          (0x00U)

/** @brief Bitmask for PHY link status bit (bit 0: 1 = Link Up, 0 = Link Down) in PHYCFGR. */
#define W5500_PHYCFGR_LNK_BIT_MASK      (0x01U)

/* ========================================================================== */
/*                              STATIC VARIABLES                              */
/* ========================================================================== */

/**
 * @brief  Static buffer for the shared WIZnet bus arbitration mutex.
 * @note   Complies with RTOS-06 & RTOS-09 to prohibit runtime dynamic allocation.
 */
static StaticSemaphore_t s_wiz_mutex_buffer;

/** @brief Handle to the statically allocated WIZnet access mutex. */
static SemaphoreHandle_t s_wiz_mutex_handle = NULL;

/* ========================================================================== */
/*                              PUBLIC FUNCTIONS                              */
/* ========================================================================== */

/**
 * @brief  Initialize W5500 hardware peripheral lines and synchronization mutex.
 * @details Creates a static FreeRTOS mutex to arbitrate multi-task access,
 *          deasserts CS lines, and drives a simultaneous hardware reset pulse
 *          to both W5500 controllers.
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Successfully initialized hardware lines and mutex.
 *         - @ref APP_STATUS_NO_MEMORY: Mutex allocation failed.
 */
app_status_t bsp_w5500_init(void)
{
    /* RTOS-06: Static creation of the mutual exclusion semaphore */
    s_wiz_mutex_handle = xSemaphoreCreateMutexStatic(&s_wiz_mutex_buffer);
    if (s_wiz_mutex_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    /* Initialize CS lines to inactive (deasserted) state */
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_cs_write(BSP_W5500_ID_B, false);

    /* Assert reset line on both W5500 modules simultaneously */
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_A, true);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_B, true);
    HAL_Delay(W5500_RST_HOLD_DELAY_MS);

    /* Deassert reset line and wait for internal oscillator/PLL to stabilize */
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_A, false);
    (void)bsp_gpio_w5500_rst_write(BSP_W5500_ID_B, false);
    HAL_Delay(W5500_RST_SETTLE_DELAY_MS);

    return APP_STATUS_OK;
}

/**
 * @brief  Acquire exclusive bus access and assert Chip Select for a W5500 module.
 * @details Takes the shared FreeRTOS mutex (when scheduler is active) within the
 *          specified timeout and drives the active-low CS line low.
 * @param  chip_id    Target W5500 chip identifier (@ref bsp_w5500_id_t).
 * @param  timeout_ms Maximum time to wait for bus mutex in milliseconds.
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Bus acquired and CS asserted.
 *         - @ref APP_STATUS_INVALID_ARG: chip_id exceeds valid range.
 *         - @ref APP_STATUS_BUSY: Mutex could not be acquired before timeout.
 */
app_status_t bsp_w5500_acquire(
    bsp_w5500_id_t chip_id,
    uint32_t timeout_ms)
{
    /* SAF-03: Validate input argument bounds */
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Enforce mutual exclusion only when FreeRTOS scheduler is active */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (s_wiz_mutex_handle != NULL)
        {
            if (xSemaphoreTake(
                    s_wiz_mutex_handle,
                    pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
            {
                return APP_STATUS_BUSY;
            }
        }
    }

    /* Assert CS (active-low) for target chip */
    return bsp_gpio_w5500_cs_write(chip_id, true);
}

/**
 * @brief  Release Chip Select and surrender exclusive bus access.
 * @details Drives the CS line inactive (high) and releases the FreeRTOS mutex.
 * @param  chip_id Target W5500 chip identifier (@ref bsp_w5500_id_t).
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Bus released successfully.
 *         - @ref APP_STATUS_INVALID_ARG: chip_id exceeds valid range.
 */
app_status_t bsp_w5500_release(bsp_w5500_id_t chip_id)
{
    /* SAF-03: Validate input argument bounds */
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Deassert CS line (return to inactive state) */
    (void)bsp_gpio_w5500_cs_write(chip_id, false);

    /* Release mutex back to other waiting tasks */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (s_wiz_mutex_handle != NULL)
        {
            (void)xSemaphoreGive(s_wiz_mutex_handle);
        }
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Read an 8-bit register from the W5500 Common Register Block.
 * @details Handles bus locking, 3-byte SPI header transmission ([Address High,
 *          Address Low, Control Byte]), data reception, and bus release.
 * @param  chip_id  Target W5500 chip identifier.
 * @param  reg_addr 16-bit register offset within Common Register Block.
 * @param  p_value  Pointer to destination memory for the read byte.
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Register read successfully.
 *         - @ref APP_STATUS_INVALID_ARG: p_value is NULL or chip_id is invalid.
 *         - @ref APP_STATUS_BUSY: Failed to acquire bus lock within timeout.
 *         - @ref APP_STATUS_IO_ERROR: SPI transfer failure.
 */
app_status_t bsp_w5500_read_common_reg(
    bsp_w5500_id_t chip_id,
    uint16_t reg_addr,
    uint8_t *p_value)
{
    /* SAF-02: Pointer validation */
    if (p_value == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Acquire bus mutex and assert chip select */
    app_status_t status = bsp_w5500_acquire(
        chip_id,
        W5500_MUTEX_TIMEOUT_DEFAULT_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Prepare 3-byte command frame: Address (MSB), Address (LSB), Control Byte */
    const uint8_t header[3] =
    {
        (uint8_t)(reg_addr >> 8U),
        (uint8_t)(reg_addr & 0x00FFU),
        W5500_CTRL_COMMON_READ
    };

    /* Transmit 3-byte header */
    status = bsp_spi_transfer_buffer(chip_id, header, NULL, sizeof(header));
    if (status == APP_STATUS_OK)
    {
        /* Clock out dummy byte (0x00) to read received data */
        status = bsp_spi_transfer_byte(chip_id, 0x00U, p_value);
    }

    /* Always release bus and CS regardless of transfer status */
    (void)bsp_w5500_release(chip_id);
    return status;
}

/**
 * @brief  Read the hardware chip version register (VERSIONR).
 * @param  chip_id   Target W5500 chip identifier.
 * @param  p_version Pointer to store the version value (expected: 0x04 for W5500).
 * @return Operation status forwarded from @ref bsp_w5500_read_common_reg.
 */
app_status_t bsp_w5500_read_version(
    bsp_w5500_id_t chip_id,
    uint8_t *p_version)
{
    return bsp_w5500_read_common_reg(chip_id, W5500_REG_VERSIONR, p_version);
}

/**
 * @brief  Query the PHY configuration register to evaluate Ethernet link status.
 * @param  chip_id       Target W5500 chip identifier.
 * @param  p_is_link_up  Pointer to boolean flag (true = Link Up, false = Link Down).
 * @return Operation status:
 *         - @ref APP_STATUS_OK: Successfully retrieved link status.
 *         - @ref APP_STATUS_INVALID_ARG: p_is_link_up is NULL.
 *         - Other errors forwarded from @ref bsp_w5500_read_common_reg.
 */
app_status_t bsp_w5500_read_phy_link(
    bsp_w5500_id_t chip_id,
    bool *p_is_link_up)
{
    /* SAF-02: Validate nullable output pointer */
    if (p_is_link_up == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    uint8_t phy_cfg = 0U;
    const app_status_t status = bsp_w5500_read_common_reg(
        chip_id,
        W5500_REG_PHYCFGR,
        &phy_cfg);

    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Extract Link Status bit */
    *p_is_link_up = ((phy_cfg & W5500_PHYCFGR_LNK_BIT_MASK) != 0U);
    return APP_STATUS_OK;
}
