/**
 * @file    bsp_w5500.c
 * @brief   Dual W5500 Ethernet controller port abstraction implementation.
 * @version 1.1
 * @date    2026-11-12
 */

#include "bsp_w5500.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "main.h"
#include "wizchip_conf.h"

#include "bsp_spi.h"

#define BSP_W5500_RST_PULSE_MS        (2U)
#define BSP_W5500_PLL_LOCK_MS         (50U)
#define BSP_W5500_DEFAULT_LOCK_MS     (100U)
#define BSP_W5500_DUMMY_BYTE          (0xFFU)
#define BSP_W5500_SOCKET_COUNT        (8U)
#define BSP_W5500_SOCKET_MEM_KB       (2U)
#define BSP_W5500_ACTIVE_SOCK_MASK    (0x07U) /* Enable interrupts for Socket 0, 1, 2 */

/* RTOS-06 & NAM-04: Static mutex serializing WIZnet ioLibrary global state */
static StaticSemaphore_t s_wiz_mutex_buffer;
static SemaphoreHandle_t s_wiz_mutex_handle = NULL;
static bsp_w5500_id_t s_active_chip = BSP_W5500_ID_A;
static bsp_w5500_exti_cb_t s_exti_callback = NULL;

static bsp_spi_bus_t bsp_w5500_get_spi_bus(bsp_w5500_id_t chip_id)
{
    if (chip_id == BSP_W5500_ID_B)
    {
        return BSP_SPI_BUS_2;
    }

    return BSP_SPI_BUS_1;
}

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

/* ioLibrary Callback: Assert Chip Select for currently active W5500 */
static void bsp_w5500_cb_cs_select(void)
{
    (void)bsp_gpio_w5500_cs_write(s_active_chip, true);
}

/* ioLibrary Callback: Deassert Chip Select for currently active W5500 */
static void bsp_w5500_cb_cs_deselect(void)
{
    (void)bsp_gpio_w5500_cs_write(s_active_chip, false);
}

/* ioLibrary Callback: Read 1 byte from active W5500 SPI bus */
static uint8_t bsp_w5500_cb_spi_read_byte(void)
{
    uint8_t rx_byte = 0U;
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_transfer_byte(bus_id, BSP_W5500_DUMMY_BYTE, &rx_byte);
    return rx_byte;
}

/* ioLibrary Callback: Write 1 byte to active W5500 SPI bus */
static void bsp_w5500_cb_spi_write_byte(uint8_t tx_byte)
{
    uint8_t dummy_rx = 0U;
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_transfer_byte(bus_id, tx_byte, &dummy_rx);
}

/* ioLibrary Callback: Read burst buffer from active W5500 SPI bus */
static void bsp_w5500_cb_spi_read_burst(uint8_t *p_buf, uint16_t len)
{
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_read_burst(bus_id, p_buf, (size_t)len);
}

/* ioLibrary Callback: Write burst buffer to active W5500 SPI bus */
static void bsp_w5500_cb_spi_write_burst(uint8_t *p_buf, uint16_t len)
{
    const bsp_spi_bus_t bus_id = bsp_w5500_get_spi_bus(s_active_chip);
    (void)bsp_spi_write_burst(bus_id, p_buf, (size_t)len);
}

app_status_t bsp_w5500_init(void)
{
    app_status_t status = bsp_spi_init();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    s_wiz_mutex_handle = xSemaphoreCreateMutexStatic(&s_wiz_mutex_buffer);
    if (s_wiz_mutex_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    reg_wizchip_cs_cbfunc(bsp_w5500_cb_cs_select, bsp_w5500_cb_cs_deselect);
    reg_wizchip_spi_cbfunc(bsp_w5500_cb_spi_read_byte, bsp_w5500_cb_spi_write_byte);
    reg_wizchip_spiburst_cbfunc(bsp_w5500_cb_spi_read_burst, bsp_w5500_cb_spi_write_burst);

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

        status = bsp_w5500_hw_reset(chip_id);
        if (status != APP_STATUS_OK)
        {
            return status;
        }

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

void bsp_w5500_register_exti_callback(bsp_w5500_exti_cb_t callback)
{
    s_exti_callback = callback;
}

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

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (xSemaphoreTake(s_wiz_mutex_handle, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        {
            return APP_STATUS_BUSY;
        }
    }

    s_active_chip = chip_id;
    return APP_STATUS_OK;
}

/**
 * @brief  Get the currently active W5500 chip ID (under lock).
 * @return Active chip identifier (BSP_W5500_ID_A or BSP_W5500_ID_B).
 */
bsp_w5500_id_t bsp_w5500_get_active_chip(void)
{
    return s_active_chip;
}

void bsp_w5500_unlock(void)
{
    if ((xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && (s_wiz_mutex_handle != NULL))
    {
        (void)xSemaphoreGive(s_wiz_mutex_handle);
    }
}

app_status_t bsp_w5500_hw_reset(bsp_w5500_id_t chip_id)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    (void)bsp_gpio_w5500_cs_write(chip_id, false);
    (void)bsp_gpio_w5500_rst_write(chip_id, true);
    bsp_w5500_delay_ms(BSP_W5500_RST_PULSE_MS);

    (void)bsp_gpio_w5500_rst_write(chip_id, false);
    bsp_w5500_delay_ms(BSP_W5500_PLL_LOCK_MS);

    return APP_STATUS_OK;
}

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

    *p_version = getVERSIONR();
    bsp_w5500_unlock();

    return APP_STATUS_OK;
}

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

    const uint8_t phy_cfg = getPHYCFGR();
    bsp_w5500_unlock();

    *p_link_up = ((phy_cfg & PHYCFGR_LNK_ON) != 0U);
    return APP_STATUS_OK;
}

app_status_t bsp_w5500_configure_network(
    bsp_w5500_id_t chip_id,
    const bsp_w5500_net_cfg_t *p_cfg)
{
    if ((chip_id >= BSP_W5500_COUNT) || (p_cfg == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    wiz_NetInfo net_info;
    (void)memset(&net_info, 0, sizeof(net_info));
    (void)memcpy(net_info.mac, p_cfg->mac, sizeof(net_info.mac));
    (void)memcpy(net_info.ip, p_cfg->ip, sizeof(net_info.ip));
    (void)memcpy(net_info.sn, p_cfg->subnet, sizeof(net_info.sn));
    (void)memcpy(net_info.gw, p_cfg->gateway, sizeof(net_info.gw));
    (void)memcpy(net_info.dns, p_cfg->dns, sizeof(net_info.dns));
    net_info.dhcp = NETINFO_STATIC;

    app_status_t status = bsp_w5500_lock(chip_id, BSP_W5500_DEFAULT_LOCK_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    ctlnetwork(CN_SET_NETINFO, (void *)&net_info);

    wiz_NetInfo readback_info;
    (void)memset(&readback_info, 0, sizeof(readback_info));
    ctlnetwork(CN_GET_NETINFO, (void *)&readback_info);
    bsp_w5500_unlock();

    if (memcmp(net_info.ip, readback_info.ip, sizeof(net_info.ip)) != 0)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

app_status_t bsp_w5500_enable_socket_interrupts(bsp_w5500_id_t chip_id)
{
    if (chip_id >= BSP_W5500_COUNT)
    {
        return APP_STATUS_INVALID_ARG;
    }

    app_status_t status = bsp_w5500_lock(chip_id, BSP_W5500_DEFAULT_LOCK_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* Enable socket-level interrupts for Sockets 0, 1, 2 in SIMR */
    setSIMR(BSP_W5500_ACTIVE_SOCK_MASK);

    /* Enable RECV, CON, DISCON interrupt events on each of the 3 sockets */
    const uint8_t sock_int_mask = (uint8_t)(Sn_IR_RECV | Sn_IR_CON | Sn_IR_DISCON);
    for (uint8_t sn = 0U; sn < 3U; sn++)
    {
        setSn_IR(sn, 0xFFU);
        setSn_IMR(sn, sock_int_mask);
    }

    bsp_w5500_unlock();
    return APP_STATUS_OK;
}

/* HAL-04, HAL-06 & NAM-09: Central EXTI callback owned by BSP layer */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (s_exti_callback == NULL)
    {
        return;
    }

    if (GPIO_Pin == W5500_A_INT_Pin)
    {
        s_exti_callback(BSP_W5500_ID_A);
    }
    else if (GPIO_Pin == W5500_B_INT_Pin)
    {
        s_exti_callback(BSP_W5500_ID_B);
    }
    else
    {
        /* Ignore unrelated EXTI pins */
    }
}
