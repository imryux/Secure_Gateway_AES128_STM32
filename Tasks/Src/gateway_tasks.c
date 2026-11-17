/**
 * @file    gateway_tasks.c
 * @brief   Static FreeRTOS tasks and IPC queues implementation.
 * @version 1.0
 * @date    2026-11-12
 */

#include "gateway_tasks.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_config.h"
#include "bsp_gpio.h"
#include "bsp_w5500.h"
#include "bsp_watchdog.h"
#include "log_service.h"
#include "relay_service.h"

/* RTOS-01: Explicit Task Priorities and Stack Sizes (in StackType_t words) */
#define TASK_RX_PRIORITY              (4U)
#define TASK_PROC_PRIORITY            (3U)
#define TASK_TX_PRIORITY              (3U)
#define TASK_HEARTBEAT_PRIORITY       (2U)

#define TASK_RX_STACK_WORDS           (256U)
#define TASK_PROC_STACK_WORDS         (320U)
#define TASK_TX_STACK_WORDS           (256U)
#define TASK_HEARTBEAT_STACK_WORDS    (192U)

#define TASK_RX_POLL_FALLBACK_MS      (20U)
#define TASK_QUEUE_SEND_TIMEOUT_MS    (20U)

/* RTOS-06: Static Queues passing buffer slot indices (RTOS-13 ownership transfer) */
static StaticQueue_t s_rx_queue_struct;
static uint8_t s_rx_queue_storage[APP_STATIC_BUFFER_COUNT];
static QueueHandle_t s_rx_queue_handle = NULL;

static StaticQueue_t s_tx_queue_struct;
static uint8_t s_tx_queue_storage[APP_STATIC_BUFFER_COUNT];
static QueueHandle_t s_tx_queue_handle = NULL;

/* RTOS-06: Static Task TCBs, Stacks, and Handles */
static StaticTask_t s_rx_spi1_tcb;
static StackType_t s_rx_spi1_stack[TASK_RX_STACK_WORDS];
static TaskHandle_t s_rx_spi1_handle = NULL;

static StaticTask_t s_rx_spi2_tcb;
static StackType_t s_rx_spi2_stack[TASK_RX_STACK_WORDS];
static TaskHandle_t s_rx_spi2_handle = NULL;

static StaticTask_t s_proc_tcb;
static StackType_t s_proc_stack[TASK_PROC_STACK_WORDS];
static TaskHandle_t s_proc_handle = NULL;

static StaticTask_t s_tx_tcb;
static StackType_t s_tx_stack[TASK_TX_STACK_WORDS];
static TaskHandle_t s_tx_handle = NULL;

static StaticTask_t s_heartbeat_tcb;
static StackType_t s_heartbeat_stack[TASK_HEARTBEAT_STACK_WORDS];
static TaskHandle_t s_heartbeat_handle = NULL;

/* RTOS-03 & HAL-04: Short ISR callback waking corresponding RX task via Task Notification */
static void gateway_on_w5500_exti_from_isr(bsp_w5500_id_t chip_id)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    TaskHandle_t target_task = NULL;

    if (chip_id == BSP_W5500_ID_A)
    {
        target_task = s_rx_spi1_handle;
    }
    else if (chip_id == BSP_W5500_ID_B)
    {
        target_task = s_rx_spi2_handle;
    }
    else
    {
        return;
    }

    if (target_task != NULL)
    {
        vTaskNotifyGiveFromISR(target_task, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static app_status_t gateway_enqueue_rx_slot(uint8_t slot_idx)
{
    if (s_rx_queue_handle == NULL)
    {
        return APP_STATUS_NOT_READY;
    }

    if (xQueueSend(
            s_rx_queue_handle,
            &slot_idx,
            pdMS_TO_TICKS(TASK_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_STATUS_BUSY;
    }

    return APP_STATUS_OK;
}

static void gateway_rx_spi1_task_entry(void *p_argument)
{
    (void)p_argument;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TASK_RX_POLL_FALLBACK_MS));
        (void)relay_service_poll_rx(BSP_W5500_ID_A, gateway_enqueue_rx_slot);
    }
}

static void gateway_rx_spi2_task_entry(void *p_argument)
{
    (void)p_argument;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TASK_RX_POLL_FALLBACK_MS));
        (void)relay_service_poll_rx(BSP_W5500_ID_B, gateway_enqueue_rx_slot);
    }
}

static void gateway_packet_proc_task_entry(void *p_argument)
{
    (void)p_argument;
    uint8_t slot_idx = 0U;

    for (;;)
    {
        if (xQueueReceive(s_rx_queue_handle, &slot_idx, portMAX_DELAY) == pdTRUE)
        {
            if (relay_service_encrypt_slot(slot_idx) == APP_STATUS_OK)
            {
                if (xQueueSend(
                        s_tx_queue_handle,
                        &slot_idx,
                        pdMS_TO_TICKS(TASK_QUEUE_SEND_TIMEOUT_MS)) != pdTRUE)
                {
                    (void)relay_service_free_slot(slot_idx);
                }
            }
            else
            {
                (void)relay_service_free_slot(slot_idx);
            }
        }
    }
}

static void gateway_tx_task_entry(void *p_argument)
{
    (void)p_argument;
    uint8_t slot_idx = 0U;

    for (;;)
    {
        if (xQueueReceive(s_tx_queue_handle, &slot_idx, portMAX_DELAY) == pdTRUE)
        {
            (void)relay_service_transmit_slot(slot_idx);
            (void)relay_service_free_slot(slot_idx);
        }
    }
}

static void gateway_heartbeat_task_entry(void *p_argument)
{
    (void)p_argument;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS);

    for (;;)
    {
        relay_service_supervise_health();
        bsp_gpio_led_toggle();

        /* Refresh 500 ms hardware watchdog every 200 ms (Ref: Section III-C & IV-D) */
        (void)bsp_watchdog_refresh();

        vTaskDelayUntil(&last_wake_time, period_ticks);
    }
}

app_status_t gateway_tasks_start_all(void)
{
    /* RTOS-06 & RTOS-07: Create static queues and check handles */
    s_rx_queue_handle = xQueueCreateStatic(
        APP_STATIC_BUFFER_COUNT,
        sizeof(uint8_t),
        s_rx_queue_storage,
        &s_rx_queue_struct);
    if (s_rx_queue_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    s_tx_queue_handle = xQueueCreateStatic(
        APP_STATIC_BUFFER_COUNT,
        sizeof(uint8_t),
        s_tx_queue_storage,
        &s_tx_queue_struct);
    if (s_tx_queue_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    /* RTOS-06 & RTOS-07: Create all 5 static worker tasks */
    s_rx_spi1_handle = xTaskCreateStatic(
        gateway_rx_spi1_task_entry,
        "RxSpi1Task",
        TASK_RX_STACK_WORDS,
        NULL,
        TASK_RX_PRIORITY,
        s_rx_spi1_stack,
        &s_rx_spi1_tcb);
    if (s_rx_spi1_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    s_rx_spi2_handle = xTaskCreateStatic(
        gateway_rx_spi2_task_entry,
        "RxSpi2Task",
        TASK_RX_STACK_WORDS,
        NULL,
        TASK_RX_PRIORITY,
        s_rx_spi2_stack,
        &s_rx_spi2_tcb);
    if (s_rx_spi2_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    s_proc_handle = xTaskCreateStatic(
        gateway_packet_proc_task_entry,
        "PacketProcTask",
        TASK_PROC_STACK_WORDS,
        NULL,
        TASK_PROC_PRIORITY,
        s_proc_stack,
        &s_proc_tcb);
    if (s_proc_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    s_tx_handle = xTaskCreateStatic(
        gateway_tx_task_entry,
        "TxTask",
        TASK_TX_STACK_WORDS,
        NULL,
        TASK_TX_PRIORITY,
        s_tx_stack,
        &s_tx_tcb);
    if (s_tx_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    s_heartbeat_handle = xTaskCreateStatic(
        gateway_heartbeat_task_entry,
        "HeartbeatTask",
        TASK_HEARTBEAT_STACK_WORDS,
        NULL,
        TASK_HEARTBEAT_PRIORITY,
        s_heartbeat_stack,
        &s_heartbeat_tcb);
    if (s_heartbeat_handle == NULL)
    {
        return APP_STATUS_NO_MEMORY;
    }

    /* Bind hardware EXTI interrupts to wake RX tasks immediately */
    bsp_w5500_register_exti_callback(gateway_on_w5500_exti_from_isr);

    (void)log_service_print("FreeRTOS 5 Static Tasks & Buffer Pool (10x550B) Started!\r\n");
    return APP_STATUS_OK;
}
