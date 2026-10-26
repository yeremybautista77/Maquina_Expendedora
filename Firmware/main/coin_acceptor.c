/**
 * @file coin_acceptor.c
 * @brief ISR y decodificacion de rafagas del monedero (Pesos Dominicanos).
 *
 * SPDX-License-Identifier: MIT
 */

#include "coin_acceptor.h"

#include <stdbool.h>
#include <stddef.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "coin_acceptor";

/** Profundidad suficiente para varias rafagas sin perder pulsos. */
#define COIN_QUEUE_LENGTH       64

/** Evento que deposita la ISR: un pulso valido (sin carga util). */
typedef uint8_t coin_pulse_event_t;

static QueueHandle_t s_pulse_queue = NULL;
static coin_acceptor_cb_t s_callback = NULL;
static void *s_callback_ctx = NULL;
static bool s_initialized = false;

/** Tick del ultimo pulso, usado para el antirrebote dentro de la ISR. */
static volatile TickType_t s_last_isr_tick = 0;

/** Pulsos descartados por cola llena (para diagnostico). */
static volatile uint32_t s_isr_dropped = 0;

/**
 * @brief ISR del monedero.
 *
 * Debe ser corta: solo valida el antirrebote y encola el evento. La logica de
 * la ventana de 300 ms vive en coin_acceptor_task().
 */
static void IRAM_ATTR coin_isr_handler(void *arg)
{
    (void)arg;

    const TickType_t now_ticks = xTaskGetTickCountFromISR();
    if ((now_ticks - s_last_isr_tick) < pdMS_TO_TICKS(COIN_PULSE_DEBOUNCE_MS)) {
        return;
    }
    s_last_isr_tick = now_ticks;

    const coin_pulse_event_t event = 1;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_pulse_queue, &event, &higher_priority_task_woken) != pdTRUE) {
        s_isr_dropped++;
    }

    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t coin_acceptor_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_pulse_queue = xQueueCreate(COIN_QUEUE_LENGTH, sizeof(coin_pulse_event_t));
    if (s_pulse_queue == NULL) {
        ESP_LOGE(TAG, "Sin memoria para la cola de pulsos");
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_COIN_GPIO),
        .mode = GPIO_MODE_INPUT,
        /* GPIO35 es input-only y no tiene pull-up interno. */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
#if BOARD_COIN_ACTIVE_LOW
        .intr_type = GPIO_INTR_NEGEDGE,
#else
        .intr_type = GPIO_INTR_POSEDGE,
#endif
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(IO%u) fallo: %s",
                 (unsigned)BOARD_COIN_GPIO, esp_err_to_name(err));
        vQueueDelete(s_pulse_queue);
        s_pulse_queue = NULL;
        return err;
    }

    /*
     * El servicio de ISR puede haber sido instalado por otro modulo; en ese
     * caso ESP_ERR_INVALID_STATE es un resultado valido.
     */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service fallo: %s", esp_err_to_name(err));
        vQueueDelete(s_pulse_queue);
        s_pulse_queue = NULL;
        return err;
    }

    err = gpio_isr_handler_add(BOARD_COIN_GPIO, coin_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add fallo: %s", esp_err_to_name(err));
        vQueueDelete(s_pulse_queue);
        s_pulse_queue = NULL;
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Monedero listo en GPIO%u (flanco %s, ventana %u ms)",
             (unsigned)BOARD_COIN_GPIO,
             BOARD_COIN_ACTIVE_LOW ? "de bajada" : "de subida",
             (unsigned)COIN_BURST_GAP_MS);
    return ESP_OK;
}

void coin_acceptor_set_callback(coin_acceptor_cb_t cb, void *ctx)
{
    s_callback = cb;
    s_callback_ctx = ctx;
}

uint32_t coin_acceptor_pulses_to_cents(uint8_t pulses)
{
    switch (pulses) {
    case COIN_PULSES_RD1:
        return 100;     /* RD$ 1.00 */
    case COIN_PULSES_RD5:
        return 500;     /* RD$ 5.00 */
    case COIN_PULSES_RD10:
        return 1000;    /* RD$ 10.00 */
    case COIN_PULSES_RD25:
        return 2500;    /* RD$ 25.00 */
    default:
        return 0;       /* Rafaga desconocida. */
    }
}

void coin_acceptor_task(void *arg)
{
    (void)arg;

    if (s_pulse_queue == NULL) {
        ESP_LOGE(TAG, "coin_acceptor_task sin cola inicializada; tarea finalizada");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Tarea del monedero iniciada");

    uint8_t pulse_count = 0;
    int64_t last_pulse_us = 0;

    for (;;) {
        /*
         * Sin pulsos la tarea duerme indefinidamente. Con una rafaga en curso,
         * espera un poco mas que la ventana para detectar el silencio final.
         */
        const TickType_t wait_ticks = (pulse_count == 0)
                                          ? portMAX_DELAY
                                          : pdMS_TO_TICKS(COIN_BURST_GAP_MS + 20);

        coin_pulse_event_t event;
        if (xQueueReceive(s_pulse_queue, &event, wait_ticks) == pdTRUE) {
            if (pulse_count < UINT8_MAX) {
                pulse_count++;
            }
            last_pulse_us = esp_timer_get_time();
            continue;
        }

        if (pulse_count == 0) {
            continue;
        }

        const int64_t silence_ms = (esp_timer_get_time() - last_pulse_us) / 1000;
        if (silence_ms < COIN_BURST_GAP_MS) {
            continue; /* Redondeo de ticks: esperar un poco mas. */
        }

        const uint8_t detected_pulses = pulse_count;
        pulse_count = 0;

        const uint32_t cents = coin_acceptor_pulses_to_cents(detected_pulses);
        if (s_isr_dropped > 0) {
            ESP_LOGW(TAG, "%lu pulso(s) descartado(s) por cola llena",
                     (unsigned long)s_isr_dropped);
            s_isr_dropped = 0;
        }

        if (cents == 0) {
            ESP_LOGW(TAG, "Rafaga no reconocida: %u pulso(s)",
                     (unsigned)detected_pulses);
        } else {
            ESP_LOGI(TAG, "Moneda valida: %u pulso(s) = RD$ %lu.%02lu",
                     (unsigned)detected_pulses,
                     (unsigned long)(cents / 100),
                     (unsigned long)(cents % 100));
        }

        if (s_callback != NULL) {
            s_callback(detected_pulses, cents, s_callback_ctx);
        }
    }
}
