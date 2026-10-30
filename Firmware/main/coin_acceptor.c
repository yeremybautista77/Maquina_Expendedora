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

/** Instante (us) de coin_acceptor_init(): base de la ventana de arranque. */
static volatile int64_t s_boot_us = 0;

/** Flanco de activacion del pulso en curso y su instante de inicio. */
static volatile bool s_pulse_active = false;
static volatile int64_t s_pulse_start_us = 0;

/** Instante del ultimo pulso aceptado, para el antirrebote. */
static volatile int64_t s_last_pulse_us = 0;

/** Pulsos descartados por cola llena (para diagnostico). */
static volatile uint32_t s_isr_dropped = 0;

/** Pulsos descartados por ancho invalido o por la ventana de arranque. */
static volatile uint32_t s_isr_rejected = 0;

/** Flancos de activacion vistos por la ISR (diagnostico de cableado). */
static volatile uint32_t s_isr_edges = 0;

/**
 * Monedero armado: solo cuando la linea lleva COIN_ARM_STABLE_MS en reposo.
 * Mientras esta en false la ISR descarta todo pulso, de modo que un IO35
 * flotante (monedero sin conectar) no acredita saldo fantasma.
 */
static volatile bool s_armed = false;

/** @brief true si el nivel leido corresponde al estado activo del monedero. */
static inline bool coin_level_is_active(int level)
{
#if BOARD_COIN_ACTIVE_LOW
    return level == 0;
#else
    return level != 0;
#endif
}

/**
 * @brief ISR del monedero.
 *
 * Debe ser corta: mide el ancho del pulso, valida la ventana de arranque y
 * encola el evento. La logica de la rafaga de 300 ms vive en
 * coin_acceptor_task().
 *
 * Se dispara en AMBOS flancos: con el flanco de activacion se marca el inicio
 * del pulso y con el de reposo se mide su duracion. Un glitch de EMI dura
 * microsegundos y se descarta; una linea flotante se queda en LOW demasiado
 * tiempo y tambien se descarta.
 */
static void IRAM_ATTR coin_isr_handler(void *arg)
{
    (void)arg;

    const int64_t now_us = esp_timer_get_time();
    const int level = gpio_get_level(BOARD_COIN_GPIO);

    if (coin_level_is_active(level)) {
        /* Inicio del pulso: solo se registra la primera vez. */
        if (!s_pulse_active) {
            s_pulse_active = true;
            s_pulse_start_us = now_us;
            s_isr_edges++;
        }
        return;
    }

    /* Fin del pulso: se valida el ancho. */
    if (!s_pulse_active) {
        return;
    }
    s_pulse_active = false;

    const int64_t width_us = now_us - s_pulse_start_us;

    /* Sin armar: la linea no es confiable (monedero sin conectar o ruido). */
    if (!s_armed) {
        s_isr_rejected++;
        return;
    }

    /* Ventana de arranque: los transitorios de encendido no son monedas. */
    if ((now_us - s_boot_us) < (int64_t)COIN_STARTUP_IGNORE_MS * 1000) {
        s_isr_rejected++;
        return;
    }

    /* Glitch de EMI (muy corto) o linea pegada en LOW (muy largo). */
    if (width_us < (int64_t)COIN_PULSE_MIN_WIDTH_MS * 1000 ||
        width_us > (int64_t)COIN_PULSE_MAX_WIDTH_MS * 1000) {
        s_isr_rejected++;
        return;
    }

    if ((now_us - s_last_pulse_us) < (int64_t)COIN_PULSE_DEBOUNCE_MS * 1000) {
        return;
    }
    s_last_pulse_us = now_us;

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
        /* Se necesitan ambos flancos para medir el ancho del pulso. */
        .intr_type = GPIO_INTR_ANYEDGE,
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

    /*
     * Estado inicial: si la linea ya esta en el nivel activo (por ejemplo una
     * linea flotante que arranco en LOW) se toma ese instante como inicio del
     * pulso, para no medir un ancho gigante cuando vuelva al reposo.
     */
    const int initial_level = gpio_get_level(BOARD_COIN_GPIO);
    s_boot_us = esp_timer_get_time();
    s_pulse_active = coin_level_is_active(initial_level);
    s_pulse_start_us = s_boot_us;

    s_initialized = true;
    ESP_LOGI(TAG, "Monedero listo en GPIO%u (activo en %s, pulso %u-%u ms, "
                  "ventana %u ms, gracia %u ms)",
             (unsigned)BOARD_COIN_GPIO,
             BOARD_COIN_ACTIVE_LOW ? "LOW" : "HIGH",
             (unsigned)COIN_PULSE_MIN_WIDTH_MS,
             (unsigned)COIN_PULSE_MAX_WIDTH_MS,
             (unsigned)COIN_BURST_GAP_MS,
             (unsigned)COIN_STARTUP_IGNORE_MS);
    ESP_LOGI(TAG, "Nivel inicial de IO%u: %s (%s)",
             (unsigned)BOARD_COIN_GPIO,
             initial_level ? "HIGH" : "LOW",
             coin_level_is_active(initial_level) ? "activo" : "reposo");
    if (!coin_level_is_active(initial_level)) {
        ESP_LOGI(TAG, "IO35 en reposo (HIGH): el monedero se armara al "
                      "estabilizarse la linea");
    } else {
        ESP_LOGW(TAG, "La linea arranco en nivel activo: revise el pull-up "
                      "externo de 10 kOhm a 3.3 V en IO%u",
                 (unsigned)BOARD_COIN_GPIO);
    }
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

    /* Seguimiento de la linea para armar/desarmar el monedero. */
    int64_t idle_since_us = esp_timer_get_time();
    int64_t active_since_us = 0;
    bool line_was_active = false;

    /* Diagnostico periodico de flancos (para verificar el cableado). */
    int64_t last_diag_us = esp_timer_get_time();
    uint32_t diag_edges = 0;

    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        const bool active = coin_level_is_active(gpio_get_level(BOARD_COIN_GPIO));

        /* La linea debe estar en reposo para considerar que hay monedero. */
        if (active) {
            idle_since_us = now_us;
            if (!line_was_active) {
                line_was_active = true;
                active_since_us = now_us;
            }
        } else {
            line_was_active = false;
        }

        if (!s_armed) {
            /* Se arma solo tras COIN_ARM_STABLE_MS de reposo continuo. */
            if (!active &&
                (now_us - idle_since_us) >= (int64_t)COIN_ARM_STABLE_MS * 1000) {
                s_armed = true;
                ESP_LOGI(TAG, "Monedero armado: linea en reposo estable (%u ms)",
                         (unsigned)COIN_ARM_STABLE_MS);
            } else {
                /* Descartar cualquier pulso anterior al armado. */
                xQueueReset(s_pulse_queue);
                pulse_count = 0;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        } else if (active &&
                   (now_us - active_since_us) >
                       (int64_t)COIN_DISARM_AFTER_MS * 1000) {
            /* La linea se quedo en el nivel activo: no hay monedero confiable. */
            s_armed = false;
            idle_since_us = now_us;
            xQueueReset(s_pulse_queue);
            pulse_count = 0;
            ESP_LOGW(TAG, "Monedero desarmado: la linea no volvio al reposo");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /*
         * Diagnostico: cada 2 s informa si la ISR vio flancos nuevos. Permite
         * distinguir "el monedero no manda pulsos" de "los pulsos se descartan
         * por ancho o por falta de armado".
         */
        if ((now_us - last_diag_us) >= 2000000) {
            last_diag_us = now_us;
            const uint32_t edges = s_isr_edges;
            if (edges != diag_edges) {
                ESP_LOGI(TAG, "IO35: %lu flanco(s) nuevo(s), %lu rechazado(s)",
                         (unsigned long)(edges - diag_edges),
                         (unsigned long)s_isr_rejected);
                diag_edges = edges;
                s_isr_rejected = 0;
            }
        }

        /*
         * Sin pulsos se sondea la linea cada 50 ms (para el armado). Con una
         * rafaga en curso, espera un poco mas que la ventana para detectar el
         * silencio final.
         */
        const TickType_t wait_ticks = (pulse_count == 0)
                                          ? pdMS_TO_TICKS(50)
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

        if (s_isr_dropped > 0) {
            ESP_LOGW(TAG, "%lu pulso(s) descartado(s) por cola llena",
                     (unsigned long)s_isr_dropped);
            s_isr_dropped = 0;
        }

        if (s_isr_rejected > 0) {
            ESP_LOGW(TAG, "%lu pulso(s) descartado(s) por ruido/ancho invalido",
                     (unsigned long)s_isr_rejected);
            s_isr_rejected = 0;
        }

        /*
         * La linea debe haber vuelto al reposo. Si sigue en el nivel activo,
         * el pulso no termino (linea flotante o monedero sin conectar): la
         * rafaga completa se descarta para no acreditar saldo fantasma.
         */
        if (coin_level_is_active(gpio_get_level(BOARD_COIN_GPIO))) {
            ESP_LOGW(TAG, "Rafaga descartada: la linea no volvio al reposo");
            continue;
        }

        const uint32_t cents = coin_acceptor_pulses_to_cents(detected_pulses);

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
