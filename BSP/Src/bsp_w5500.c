/**
 * @file    bsp_w5500.c
 * @brief   Dual W5500 Ethernet controller port abstraction implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "bsp_w5500.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include "wizchip_conf.h"

#include "bsp_spi.h"

/** @brief Hardware reset active pulse duration in milliseconds (min 500us per datasheet) */
#define BSP_W5500_RST_PULSE_MS        (2U)

/** @brief Post-reset stabilization delay for W5500 internal PLL lock in milliseconds */
#define BSP_W5500_PLL_LOCK_MS         (50U)

/** @brief Default mutex acquisition timeout in milliseconds for internal W5500 operations */
#define BSP_W5500_DEFAULT_LOCK_MS     (100U)

/** @brief Dummy byte clocked out on MOSI during SPI read operations */
#define BSP_W5500_DUMMY_BYTE          (0xFFU)

/** @brief Total number of hardware sockets available per W5500 chip */
#define BSP_W5500_SOCKET_COUNT        (8U)

/** @brief TX/RX buffer allocation size in KB per socket (8 * 2KB = 16KB total) */
#define BSP_W5500_SOCKET_MEM_KB       (2U)

/* RTOS-06 & NAM-04: Static mutex serializing WIZnet ioLibrary global state */
/** @brief Static control block for the WIZnet ioLibrary access mutex */
static StaticSemaphore_t s_wiz_mutex_buffer;

/** @brief Mutex handle ensuring thread-safe multiplexing across dual W5500 instances */
static SemaphoreHandle_t s_wiz_mutex_handle = NULL;

/** @brief Tracks which W5500 module currently holds the ioLibrary context */
static bsp_w5500_id_t s_active_chip = BSP_W5500_ID_A;

/**
 * @brief  Maps a logical W5500 chip ID to its underlying SPI bus.
 * @param  chip_id Logical W5500 module identifier.
 * @return Corresponding SPI bus identifier (BSP_SPI_BUS_1 or BSP_SPI_BUS_2).
 */
static bsp_spi_bus_t bsp_w5500_get_spi_bus(bsp_w5500_id_t chip_id)
{
    if (chip_id == BSP_W5500_ID_B)
    {
        return BSP_SPI_BUS_2;
    }

    return BSP_SPI_BUS_1;
}

/**
 * @brief  Context-aware blocking delay safe for both pre-scheduler and RTOS execution.
 * @param  delay_ms Delay duration in milliseconds.
 */
static void bsp_w5500_delay_ms(uint32_t delay_ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    else
    {
        HAL_Delay(delay_ms);
    }
}

/**
 * @brief  ioLibrary Callback: Asserts Chip Select (CS low) for the active W5500.
 */
static void bsp_w5500_cb_cs_select(void)
{
    (void)bsp_gpio_w5500_cs_write(s_active_chip, true);
}

/**
 * @brief  ioLibrary Callback: Deasserts Chip Select (CS high) for the active W5500.
 */
static void bsp_w5500_cb_cs_deselect(void)
{
    (void)bsp_gpio_w5500_cs_write(s_active_chip, false);
}

/**
 * @brief  ioLibrary Callback: Reads a single byte from the active W5500 SPI bus.
 * @return Byte received from the W5500 controller.
 */
static uint8_t bsp_w5500_cb_spi_read_byte(void)
{
    uint8_t rx_byte = 0U;
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_transfer_byte(bus_id, BSP_W5500_DUMMY_BYTE, &rx_byte);
    return rx_byte;
}

/**
 * @brief  ioLibrary Callback: Writes a single byte to the active W5500 SPI bus.
 * @param  tx_byte Byte to transmit to the W5500 controller.
 */
static void bsp_w5500_cb_spi_write_byte(uint8_t tx_byte)
{
    uint8_t dummy_rx = 0U;
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_transfer_byte(bus_id, tx_byte, &dummy_rx);
}

/**
 * @brief  ioLibrary Callback: Reads a burst of bytes from the active W5500 SPI bus.
 * @param  p_buf Pointer to destination buffer.
 * @param  len   Number of bytes to read.
 */
static void bsp_w5500_cb_spi_read_burst(uint8_t *p_buf, uint16_t len)
{
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_read_burst(bus_id, p_buf, (size_t)len);
}

/**
 * @brief  ioLibrary Callback: Writes a burst of bytes to the active W5500 SPI bus.
 * @param  p_buf Pointer to source data buffer.
 * @param  len   Number of bytes to write.
 */
static void bsp_w5500_cb_spi_write_burst(uint8_t *p_buf, uint16_t len)
{
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_write_burst(bus_id, p_buf, (size_t)len);
}

/**
 * @brief  Initializes SPI buses, RTOS mutex, ioLibrary callbacks, and both W5500 chips.
 * @return APP_STATUS_OK on success, or an error status if initialization fails.
 */
app_status_t bsp_w5500_init(void)
{
    app_status_t status = bsp_spi_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* RTOS-06 & RTOS-09: Static mutex with priority inheritance */
    s_wiz_mutex_handle = xSemaphoreCreateMutexStatic(&s_wiz_mutex_buffer);
    if (s_wiz_mutex_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    /* Register multiplexed SPI and CS callbacks into WIZnet ioLibrary */
    reg_wizchip_cs_cbfunc(bsp_w5500_cb_cs_select, bsp_w5500_cb_cs_deselect);
    reg_wizchip_spi_cbfunc(bsp_w5500_cb_spi_read_byte, bsp_w5500_cb_spi_write_byte);
    reg_wizchip_spiburst_cbfunc(bsp_w5500_cb_spi_read_burst, bsp_w5500_cb_spi_write_burst);

    /* Hardware reset both W5500 modules and allocate 2KB TX/RX per socket (total 16KB/16KB) */
    uint8_t socket_mem_size[BSP_W5500_SOCKET_COUNT] =
    {
        BSP_W5500_SOCKET_MEM_KB, BSP_W5500_SOCKET_MEM_KB,
        BSP_W5500_SOCKET_MEM_KB, BSP_W5500_SOCKET_MEM_KB,
        BSP_W5500_SOCKET_MEM_KB, BSP_W5500_SOCKET_MEM_KB,
        BSP_W5500_SOCKET_MEM_KB, BSP_W5500_SOCKET_MEM_KB
    };

    for (uint32_t id = 0U; id < (uint32_t)BSP_W5500_COUNT; id++)
    {
        const bsp_w5500_id_t chip_id = (bsp_w5500_id_t)id;

        /* Reset hardware and wait for internal clock stabilization */
        status = bsp_w5500_hw_reset(chip_id);
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        /* Acquire exclusive ioLibrary access to configure socket buffer sizes */
        status = bsp_w5500_lock(chip_id, BSP_W5500_DEFAULT_LOCK_MS);
        if (status != APP_STATUS_OK)
        {
            return status;
        }

        const int8_t wiz_ret = wizchip_init(socket_mem_size, socket_mem_size);
        bsp_w5500_unlock();

        if (wiz_ret != 0)
        {
            return APP_STATUS_IO_ERROR;
        }
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Acquires exclusive access to WIZnet ioLibrary and routes callbacks to chip_id.
 * @note   Must be paired with bsp_w5500_unlock() after completing ioLibrary calls.
 * @param  chip_id    Target W5500 module identifier.
 * @param  timeout_ms Maximum time in milliseconds to wait for the mutex.
 * @return APP_STATUS_OK on success, APP_STATUS_BUSY on timeout, or error status.
 */
app_status_t bsp_w5500_lock(
    bsp_w5500_id_t chip_id,
    uint32_t timeout_ms)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (s_wiz_mutex_handle == NULL)
    {
        return APP_STATUS_NOT_READY;
    }

    /* Take mutex only when FreeRTOS scheduler is active */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (xSemaphoreTake(s_wiz_mutex_handle, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        {
            return APP_STATUS_BUSY;
        }
    }

    /* Switch active SPI bus and CS pin to the locked chip */
    s_active_chip = chip_id;
    return APP_STATUS_OK;
}

/**
 * @brief  Releases exclusive access to the WIZnet ioLibrary mutex.
 */
void bsp_w5500_unlock(void)
{
    if ((xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && (s_wiz_mutex_handle != NULL))
    {
        (void)xSemaphoreGive(s_wiz_mutex_handle);
    }
}

/**
 * @brief  Executes a hardware reset sequence on the specified W5500 controller.
 * @param  chip_id Target W5500 module identifier.
 * @return APP_STATUS_OK on success, or APP_STATUS_INVALID_ARG if chip_id is out of range.
 */
app_status_t bsp_w5500_hw_reset(bsp_w5500_id_t chip_id)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* Ensure CS is deasserted, assert RST, and hold for minimum pulse width */
    (void)bsp_gpio_w5500_cs_write(chip_id, false);
    (void)bsp_gpio_w5500_rst_write(chip_id, true);
    bsp_w5500_delay_ms(BSP_W5500_RST_PULSE_MS);

    /* Release RST and wait for internal PLL to lock before SPI communication */
    (void)bsp_gpio_w5500_rst_write(chip_id, false);
    bsp_w5500_delay_ms(BSP_W5500_PLL_LOCK_MS);

    return APP_STATUS_OK;
}

/**
 * @brief  Reads the chip version register (VERSIONR) from the specified W5500.
 * @param  chip_id   Target W5500 module identifier.
 * @param  p_version Pointer to store the read version value (expected 0x04 for W5500).
 * @return APP_STATUS_OK on success, or an error status.
 */
app_status_t bsp_w5500_read_version(
    bsp_w5500_id_t chip_id,
    uint8_t *p_version)
{
    if ((chip_id >= BSP_W5500_COUNT) || (p_version == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    app_status_t status = bsp_w5500_lock(chip_id, BSP_W5500_DEFAULT_LOCK_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Read W5500 VERSIONR (0x0039) via ioLibrary macro */
    *p_version = getVERSIONR();
    bsp_w5500_unlock();

    return APP_STATUS_OK;
}

/**
 * @brief  Checks the physical Ethernet link status of the specified W5500.
 * @param  chip_id   Target W5500 module identifier.
 * @param  p_link_up Pointer to store link state (true if link is up, false otherwise).
 * @return APP_STATUS_OK on success, or an error status.
 */
app_status_t bsp_w5500_is_link_up(
    bsp_w5500_id_t chip_id,
    bool *p_link_up)
{
    if ((chip_id >= BSP_W5500_COUNT) || (p_link_up == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    app_status_t status = bsp_w5500_lock(chip_id, BSP_W5500_DEFAULT_LOCK_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Read W5500 PHYCFGR (0x002E) and mask LNK bit (0x01) */
    const uint8_t phy_cfg = getPHYCFGR();
    bsp_w5500_unlock();

    *p_link_up = ((phy_cfg & PHYCFGR_LNK_ON) != 0U);
    return APP_STATUS_OK;
}
