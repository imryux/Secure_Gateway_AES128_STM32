/**
 * @file    bsp_gpio.c
 * @brief   GPIO hardware abstraction implementation.
 * @version 1.0
 * @date    2026-11-07
 */

#include "bsp_gpio.h"
#include "main.h"

/**
 * @brief  Controls the ON/OFF state of the heartbeat LED.
 * @param  is_on Boolean: true to turn ON the LED, false to turn OFF.
 * @note   The Blue Pill onboard LED (PC13) operates on active-low logic:
 *         - GPIO_PIN_RESET -> LED ON
 *         - GPIO_PIN_SET   -> LED OFF
 */
void bsp_gpio_led_write(bool is_on)
{
    GPIO_PinState pin_state = GPIO_PIN_SET;

    /* Determine GPIO output level based on active-low configuration */
    if (is_on == true)
    {
        pin_state = GPIO_PIN_RESET; /* LOW level: Turn ON LED */
    }
    else
    {
        pin_state = GPIO_PIN_SET;   /* HIGH level: Turn OFF LED */
    }

    HAL_GPIO_WritePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin, pin_state);
}

/**
 * @brief  Toggles the current state of the heartbeat LED.
 */
void bsp_gpio_led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_HEARTBEAT_GPIO_Port, LED_HEARTBEAT_Pin);
}

/**
 * @brief  Controls the Chip Select (CS) line for the W5500 IC via SPI.
 * @param  chip_id Identifier of the W5500 chip (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param  is_selected Boolean: true to select chip (CS active), false to deselect.
 * @return app_status_t APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if chip_id is invalid.
 * @note   SPI Chip Select (CS) signal is active-low:
 *         - GPIO_PIN_RESET -> Enable communication with the chip
 *         - GPIO_PIN_SET   -> Disable communication
 */
app_status_t bsp_gpio_w5500_cs_write(
    bsp_w5500_id_t chip_id,
    bool is_selected)
{
    GPIO_PinState pin_state = GPIO_PIN_SET;

    /* CS signal is active-low: pull LOW to select chip */
    if (is_selected == true)
    {
        pin_state = GPIO_PIN_RESET;
    }
    else
    {
        pin_state = GPIO_PIN_SET;
    }

    /* Output signal to the designated GPIO pin based on chip ID */
    if (chip_id == BSP_W5500_ID_A)
    {
        HAL_GPIO_WritePin(W5500_A_CS_GPIO_Port, W5500_A_CS_Pin, pin_state);
    }
    else if (chip_id == BSP_W5500_ID_B)
    {
        HAL_GPIO_WritePin(W5500_B_CS_GPIO_Port, W5500_B_CS_Pin, pin_state);
    }
    else
    {
        /* Return error if chip_id is out of range */
        return APP_STATUS_INVALID_ARG;
    }

    return APP_STATUS_OK;
}

/**
 * @brief  Controls the hardware reset (RST) line for the W5500 IC.
 * @param  chip_id Identifier of the W5500 chip (BSP_W5500_ID_A or BSP_W5500_ID_B).
 * @param  is_reset_active Boolean: true to hold the chip in reset, false for normal operation.
 * @return app_status_t APP_STATUS_OK on success, APP_STATUS_INVALID_ARG if chip_id is invalid.
 * @note   W5500 RST pin is active-low (pulled LOW to reset the chip).
 */
app_status_t bsp_gpio_w5500_rst_write(
    bsp_w5500_id_t chip_id,
    bool is_reset_active)
{
    GPIO_PinState pin_state = GPIO_PIN_SET;

    /* RST signal is active-low: pull LOW to activate hardware reset */
    if (is_reset_active == true)
    {
        pin_state = GPIO_PIN_RESET;
    }
    else
    {
        pin_state = GPIO_PIN_SET;
    }

    /* Output signal to the designated GPIO pin based on chip ID */
    if (chip_id == BSP_W5500_ID_A)
    {
        HAL_GPIO_WritePin(W5500_A_RST_GPIO_Port, W5500_A_RST_Pin, pin_state);
    }
    else if (chip_id == BSP_W5500_ID_B)
    {
        HAL_GPIO_WritePin(W5500_B_RST_GPIO_Port, W5500_B_RST_Pin, pin_state);
    }
    else
    {
        /* Return error if chip_id is out of range */
        return APP_STATUS_INVALID_ARG;
    }

    return APP_STATUS_OK;
}
