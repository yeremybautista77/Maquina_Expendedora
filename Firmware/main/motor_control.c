/**
 * @file motor_control.c
 * @brief Implementacion del control I2C de motores, sensores y Mutex de
 *        seguridad de despacho.
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_control.h"

#include <inttypes.h>
#include <stddef.h>

#include "board_config.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "motor_control";

_Static_assert(MOTOR_CHANNEL_COUNT == BOARD_CHANNEL_COUNT,
               "El numero de canales de motor debe coincidir con el hardware");

/* --- Estado interno del modulo ------------------------------------------- */

/** Handle I2C del PCF8574 de relees (0x20). */
static i2c_master_dev_handle_t s_relay_dev = NULL;

/** Handle I2C del PCF8574 de sensores (0x21). */
static i2c_master_dev_handle_t s_sensor_dev = NULL;

/** Cola FIFO de solicitudes de despacho. */
static QueueHandle_t s_dispatch_queue = NULL;

/**
 * Protege la cola de despacho.
 *
 * Serializa la comprobacion de capacidad con el encolado por lotes: mientras
 * un pedido se valida y se encola, ningun otro productor puede ocupar los
 * huecos, de modo que el lote es todo-o-nada.
 */
static SemaphoreHandle_t s_queue_mutex = NULL;

/** Protege el bus I2C y el registro sombra de relees entre tareas. */
static SemaphoreHandle_t s_i2c_mutex = NULL;

/** Estado logico de los 6 relees: bit en 1 = motor energizado. */
static uint8_t s_relay_shadow = 0x00;

/** true mientras un motor esta girando (informativo para la GUI). */
static volatile bool s_dispatching = false;

/** true mientras motor_task esta procesando una solicitud o vaciando la cola. */
static volatile bool s_task_active = false;

/** Callback de eventos de despacho (registrado por main.c). */
static motor_event_cb_t s_event_cb = NULL;
static void *s_event_ctx = NULL;

/* --- Utilidades internas -------------------------------------------------- */

/**
 * @brief Emite un evento al callback registrado (contexto de motor_task).
 */
static void emit_event(motor_event_type_t type, uint8_t channel, uint8_t units,
                       esp_err_t error, bool admin_test)
{
    const motor_event_cb_t callback = s_event_cb;
    if (callback == NULL) {
        return;
    }

    const motor_event_t event = {
        .type = type,
        .channel = channel,
        .units = units,
        .error = error,
        .admin_test = admin_test,
    };
    callback(&event, s_event_ctx);
}

/**
 * @brief Agrega un PCF8574 al bus I2C.
 */
static esp_err_t add_expander(i2c_master_bus_handle_t bus, uint8_t address,
                              i2c_master_dev_handle_t *out_dev)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = BOARD_I2C_CLOCK_HZ,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, out_dev);
}

/**
 * @brief Escribe un byte en un expansor bajo el Mutex del bus.
 */
static esp_err_t write_expander(i2c_master_dev_handle_t dev, uint8_t value)
{
    if (dev == NULL || s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = i2c_master_transmit(dev, &value, sizeof(value), 100);

    xSemaphoreGive(s_i2c_mutex);
    return err;
}

/**
 * @brief Lee un byte de un expansor bajo el Mutex del bus.
 */
static esp_err_t read_expander(i2c_master_dev_handle_t dev, uint8_t *out_value)
{
    if (dev == NULL || out_value == NULL || s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t value = 0xFF;
    esp_err_t err = i2c_master_receive(dev, &value, sizeof(value), 100);

    xSemaphoreGive(s_i2c_mutex);

    if (err == ESP_OK) {
        *out_value = value;
    }
    return err;
}

/**
 * @brief Espera a que un sensor alcance el estado solicitado.
 *
 * @param channel        Canal 0..5.
 * @param expected_logic Estado interpretado que se espera (true = final de
 *                       carrera alcanzado).
 * @param timeout_ms     Tiempo maximo de espera.
 */
static esp_err_t wait_sensor(uint8_t channel, bool expected_logic, uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    for (;;) {
        bool triggered = false;
        esp_err_t err = motor_control_read_sensor(channel, &triggered);
        if (err != ESP_OK) {
            return err;
        }
        if (triggered == expected_logic) {
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* --- API publica ---------------------------------------------------------- */

esp_err_t motor_control_home_all(void)
{
    if (s_relay_dev == NULL || s_sensor_dev == NULL || s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_dispatch_mutex == NULL) {
        ESP_LOGE(TAG, "g_dispatch_mutex no fue creado; homing omitido");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;

    ESP_LOGI(TAG, "Homing inicial de %u canales (timeout %d ms por canal)",
             (unsigned)MOTOR_CHANNEL_COUNT, MOTOR_HOMING_TIMEOUT_MS);

    /*
     * Secuencial y canal por canal: se toma g_dispatch_mutex antes de energizar
     * cada rele, de modo que nunca hay dos motores alimentados a la vez.
     */
    for (uint8_t channel = 0; channel < MOTOR_CHANNEL_COUNT; channel++) {
        bool at_home = false;
        esp_err_t err = motor_control_read_sensor(channel, &at_home);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Homing canal %u: fallo leyendo el sensor: %s",
                     (unsigned)(channel + 1), esp_err_to_name(err));
            if (result == ESP_OK) {
                result = err;
            }
            continue;
        }
        if (at_home) {
            ESP_LOGI(TAG, "Homing canal %u: ya esta en reposo",
                     (unsigned)(channel + 1));
            continue;
        }

        if (xSemaphoreTake(g_dispatch_mutex,
                           pdMS_TO_TICKS(MOTOR_HOMING_TIMEOUT_MS + 2000)) != pdTRUE) {
            ESP_LOGE(TAG, "Homing canal %u: Mutex de despacho ocupado",
                     (unsigned)(channel + 1));
            if (result == ESP_OK) {
                result = ESP_ERR_TIMEOUT;
            }
            continue;
        }

        /* Se relee bajo el Mutex: el canal pudo moverse mientras se esperaba. */
        err = motor_control_read_sensor(channel, &at_home);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Homing canal %u: fallo releyendo el sensor: %s",
                     (unsigned)(channel + 1), esp_err_to_name(err));
        } else if (!at_home) {
            ESP_LOGW(TAG, "Homing canal %u: fuera de reposo, energizando rele",
                     (unsigned)(channel + 1));

            err = motor_control_set_relay(channel, true);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Homing canal %u: no se pudo activar el rele: %s",
                         (unsigned)(channel + 1), esp_err_to_name(err));
            } else {
                err = wait_sensor(channel, true, MOTOR_HOMING_TIMEOUT_MS);

                /*
                 * El rele se apaga SIEMPRE, tambien si vencio el timeout, para
                 * que el motor no quede girando indefinidamente.
                 */
                const esp_err_t off_err = motor_control_set_relay(channel, false);
                if (off_err != ESP_OK) {
                    ESP_LOGE(TAG, "Homing canal %u: FALLO APAGANDO EL RELE: %s",
                             (unsigned)(channel + 1), esp_err_to_name(off_err));
                    if (err == ESP_OK) {
                        err = off_err;
                    }
                }

                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Homing canal %u: reposo alcanzado",
                             (unsigned)(channel + 1));
                } else {
                    ESP_LOGE(TAG, "Homing canal %u: no se alcanzo el reposo: %s",
                             (unsigned)(channel + 1), esp_err_to_name(err));
                }
            }

            vTaskDelay(pdMS_TO_TICKS(MOTOR_SETTLE_MS));
        }

        xSemaphoreGive(g_dispatch_mutex);

        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Homing completado: los 6 canales estan en reposo");
    } else {
        ESP_LOGW(TAG, "Homing incompleto (%s); se continua el arranque",
                 esp_err_to_name(result));
    }
    return result;
}

esp_err_t motor_control_init(i2c_master_bus_handle_t i2c_bus)
{
    if (i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_relay_dev != NULL && s_sensor_dev != NULL &&
        s_dispatch_queue != NULL && s_queue_mutex != NULL) {
        return ESP_OK; /* Ya inicializado. */
    }

    s_i2c_mutex = xSemaphoreCreateMutex();
    if (s_i2c_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = add_expander(i2c_bus, BOARD_I2C_ADDR_RELAY, &s_relay_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar el PCF8574 de relees (0x%02X): %s",
                 BOARD_I2C_ADDR_RELAY, esp_err_to_name(err));
        goto fail;
    }

    err = add_expander(i2c_bus, BOARD_I2C_ADDR_SENSOR, &s_sensor_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar el PCF8574 de sensores (0x%02X): %s",
                 BOARD_I2C_ADDR_SENSOR, esp_err_to_name(err));
        goto fail;
    }

    /*
     * PCF8574: escribir 1 en un pin lo configura como entrada. Los sensores se
     * alimentan con 0xFF para que el micro-switch pueda cerrar a GND.
     */
    err = write_expander(s_sensor_dev, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar el puerto de sensores: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    /*
     * Todos los relees apagados. Con BOARD_RELAY_ACTIVE_LOW el byte fisico es
     * 0xFF (nivel alto = rele desenergizado).
     */
    s_relay_shadow = 0x00;
    err = write_expander(s_relay_dev, (uint8_t)~s_relay_shadow);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo apagar los relees al inicio: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    s_queue_mutex = xSemaphoreCreateMutex();
    if (s_queue_mutex == NULL) {
        ESP_LOGE(TAG, "Sin memoria para el Mutex de la cola de despacho");
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_dispatch_queue = xQueueCreate(MOTOR_QUEUE_LENGTH, sizeof(motor_request_t));
    if (s_dispatch_queue == NULL) {
        ESP_LOGE(TAG, "Sin memoria para la cola de despacho");
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG, "Control de motores listo (relees 0x%02X, sensores 0x%02X, %u canales)",
             BOARD_I2C_ADDR_RELAY, BOARD_I2C_ADDR_SENSOR, (unsigned)MOTOR_CHANNEL_COUNT);

    /*
     * Homing de arranque: recupera los canales que quedaron a mitad de vuelta
     * tras un corte de energia. Se ejecuta antes de crear motor_task, por lo
     * que no compite con ningun despacho. Un fallo de homing no impide que la
     * maquina arranque: el canal afectado rechazara el despacho de forma segura.
     */
    (void)motor_control_home_all();

    return ESP_OK;

fail:
    if (s_dispatch_queue != NULL) {
        vQueueDelete(s_dispatch_queue);
        s_dispatch_queue = NULL;
    }
    if (s_queue_mutex != NULL) {
        vSemaphoreDelete(s_queue_mutex);
        s_queue_mutex = NULL;
    }
    if (s_relay_dev != NULL) {
        i2c_master_bus_rm_device(s_relay_dev);
        s_relay_dev = NULL;
    }
    if (s_sensor_dev != NULL) {
        i2c_master_bus_rm_device(s_sensor_dev);
        s_sensor_dev = NULL;
    }
    if (s_i2c_mutex != NULL) {
        vSemaphoreDelete(s_i2c_mutex);
        s_i2c_mutex = NULL;
    }
    return err;
}

esp_err_t motor_control_set_relay(uint8_t channel, bool enabled)
{
    if (channel >= MOTOR_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_relay_dev == NULL || s_i2c_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t mask = (uint8_t)(1u << channel);
    const uint8_t new_shadow = enabled ? (uint8_t)(s_relay_shadow | mask)
                                       : (uint8_t)(s_relay_shadow & (uint8_t)~mask);

#if BOARD_RELAY_ACTIVE_LOW
    const uint8_t physical = (uint8_t)~new_shadow;
#else
    const uint8_t physical = new_shadow;
#endif

    esp_err_t err = i2c_master_transmit(s_relay_dev, &physical, sizeof(physical), 100);
    if (err == ESP_OK) {
        s_relay_shadow = new_shadow;
        ESP_LOGD(TAG, "Canal %u -> %s", (unsigned)(channel + 1),
                 enabled ? "ON" : "OFF");
    }

    xSemaphoreGive(s_i2c_mutex);
    return err;
}

esp_err_t motor_control_read_sensors(uint8_t *out_mask)
{
    if (out_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return read_expander(s_sensor_dev, out_mask);
}

esp_err_t motor_control_read_sensor(uint8_t channel, bool *out_triggered)
{
    if (channel >= MOTOR_CHANNEL_COUNT || out_triggered == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mask = 0xFF;
    esp_err_t err = motor_control_read_sensors(&mask);
    if (err != ESP_OK) {
        return err;
    }

    const bool pin_low = ((mask >> channel) & 0x01u) == 0u;
#if BOARD_SENSOR_ACTIVE_LOW
    *out_triggered = pin_low;
#else
    *out_triggered = !pin_low;
#endif
    return ESP_OK;
}

esp_err_t motor_control_check_capacity(uint8_t count)
{
    if (count == 0 || count > MOTOR_QUEUE_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dispatch_queue == NULL || s_queue_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex de la cola ocupado; no se pudo validar capacidad");
        return ESP_ERR_TIMEOUT;
    }

    const UBaseType_t spaces = uxQueueSpacesAvailable(s_dispatch_queue);
    xSemaphoreGive(s_queue_mutex);

    if (spaces < (UBaseType_t)count) {
        ESP_LOGW(TAG,
                 "Capacidad insuficiente: %u solicitud(es), %u hueco(s) libre(s)",
                 (unsigned)count, (unsigned)spaces);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t motor_control_enqueue_batch(const motor_request_t *requests,
                                      uint8_t count)
{
    if (requests == NULL || count == 0 || count > MOTOR_QUEUE_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (requests[i].channel >= MOTOR_CHANNEL_COUNT ||
            requests[i].quantity == 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (s_dispatch_queue == NULL || s_queue_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Todo el lote se valida y se encola bajo el mismo Mutex. Como solo la GUI
     * produce solicitudes y motor_task solo consume, no hay otro productor que
     * pueda ocupar los huecos entre la comprobacion y los xQueueSend.
     */
    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex de la cola ocupado; lote no encolado");
        return ESP_ERR_TIMEOUT;
    }

    const UBaseType_t spaces = uxQueueSpacesAvailable(s_dispatch_queue);
    if (spaces < (UBaseType_t)count) {
        xSemaphoreGive(s_queue_mutex);
        ESP_LOGW(TAG,
                 "Pedido rechazado: %u solicitud(es), solo %u hueco(s) libre(s)",
                 (unsigned)count, (unsigned)spaces);
        return ESP_ERR_TIMEOUT;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (xQueueSend(s_dispatch_queue, &requests[i], 0) != pdTRUE) {
            /*
             * No puede ocurrir con un unico productor y capacidad validada;
             * si ocurriera, el pedido quedaria parcial y se informa como error.
             */
            xSemaphoreGive(s_queue_mutex);
            ESP_LOGE(TAG,
                     "Fallo inesperado al encolar el canal %u (lote parcial)",
                     (unsigned)(requests[i].channel + 1));
            return ESP_ERR_TIMEOUT;
        }
    }

    xSemaphoreGive(s_queue_mutex);

    for (uint8_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "Solicitud encolada: canal %u x %u unidad(es)",
                 (unsigned)(requests[i].channel + 1),
                 (unsigned)requests[i].quantity);
    }
    return ESP_OK;
}

esp_err_t motor_control_enqueue(uint8_t channel, uint8_t quantity)
{
    const motor_request_t request = {
        .channel = channel,
        .quantity = quantity,
    };
    return motor_control_enqueue_batch(&request, 1);
}

esp_err_t motor_control_set_event_callback(motor_event_cb_t callback, void *ctx)
{
    /*
     * Se publica primero el contexto y despues el puntero a la funcion. El
     * callback solo se invoca desde motor_task, que se crea despues de esta
     * llamada en app_main().
     */
    s_event_ctx = ctx;
    s_event_cb = callback;
    return ESP_OK;
}

esp_err_t motor_control_cancel_pending(uint32_t *out_discarded)
{
    if (s_dispatch_queue == NULL || s_queue_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t discarded = 0;
    motor_request_t request;
    while (xQueueReceive(s_dispatch_queue, &request, 0) == pdTRUE) {
        discarded++;
    }

    xSemaphoreGive(s_queue_mutex);

    if (out_discarded != NULL) {
        *out_discarded = discarded;
    }
    if (discarded > 0) {
        ESP_LOGW(TAG, "Canceladas %" PRIu32 " solicitud(es) pendiente(s)",
                 discarded);
    }
    return ESP_OK;
}

bool motor_control_is_busy(void)
{
    if (s_dispatch_queue == NULL) {
        return false;
    }

    UBaseType_t pending = 0;
    if (s_queue_mutex != NULL &&
        xSemaphoreTake(s_queue_mutex, 0) == pdTRUE) {
        pending = uxQueueMessagesWaiting(s_dispatch_queue);
        xSemaphoreGive(s_queue_mutex);
    } else {
        pending = uxQueueMessagesWaiting(s_dispatch_queue);
    }

    return s_task_active || s_dispatching || (pending > 0);
}

esp_err_t motor_control_dispatch_one(uint8_t channel, uint32_t mutex_timeout_ms)
{
    if (channel >= MOTOR_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_dispatch_mutex == NULL) {
        ESP_LOGE(TAG, "g_dispatch_mutex no fue creado por app_main()");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_dispatch_mutex, pdMS_TO_TICKS(mutex_timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex de despacho ocupado; no se pudo iniciar el canal %u",
                 (unsigned)(channel + 1));
        return ESP_ERR_TIMEOUT;
    }

    s_dispatching = true;
    esp_err_t result = ESP_OK;
    bool relay_on = false;

    /* 1. El canal debe estar en reposo antes de energizar. */
    bool at_home = false;
    result = motor_control_read_sensor(channel, &at_home);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Canal %u: fallo leyendo el sensor: %s",
                 (unsigned)(channel + 1), esp_err_to_name(result));
        goto cleanup;
    }
    if (!at_home) {
        ESP_LOGW(TAG, "Canal %u no esta en reposo; despacho bloqueado",
                 (unsigned)(channel + 1));
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    /* 2. Energizar el motor. */
    result = motor_control_set_relay(channel, true);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Canal %u: no se pudo activar el rele: %s",
                 (unsigned)(channel + 1), esp_err_to_name(result));
        goto cleanup;
    }
    relay_on = true;
    ESP_LOGI(TAG, "Canal %u: motor en marcha", (unsigned)(channel + 1));

    /* 3. Confirmar que el motor salio del reposo. */
    result = wait_sensor(channel, false, MOTOR_START_TIMEOUT_MS);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Canal %u: el motor no arranco (sensor sin cambio)",
                 (unsigned)(channel + 1));
        goto cleanup;
    }

    /* 4. Esperar la vuelta completa: el micro-switch vuelve a GND. */
    result = wait_sensor(channel, true, MOTOR_TRAVEL_TIMEOUT_MS);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Canal %u: timeout esperando el fin de carrera",
                 (unsigned)(channel + 1));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Canal %u: vuelta completa", (unsigned)(channel + 1));

cleanup:
    /* El rele se apaga SIEMPRE, incluso ante error o timeout. */
    if (relay_on) {
        esp_err_t off_err = motor_control_set_relay(channel, false);
        if (off_err != ESP_OK) {
            ESP_LOGE(TAG, "Canal %u: FALLO APAGANDO EL RELE: %s",
                     (unsigned)(channel + 1), esp_err_to_name(off_err));
            if (result == ESP_OK) {
                result = off_err;
            }
        }
    }

    s_dispatching = false;
    vTaskDelay(pdMS_TO_TICKS(MOTOR_SETTLE_MS));
    xSemaphoreGive(g_dispatch_mutex);
    return result;
}

void motor_task(void *arg)
{
    (void)arg;

    if (s_dispatch_queue == NULL) {
        ESP_LOGE(TAG, "motor_task sin cola inicializada; tarea finalizada");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Tarea de despacho iniciada");

    motor_request_t request;
    for (;;) {
        if (xQueueReceive(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        s_task_active = true;
        bool failed = false;

        ESP_LOGI(TAG, "Despachando canal %u x %u unidad(es)",
                 (unsigned)(request.channel + 1), (unsigned)request.quantity);

        for (uint8_t unit = 0; unit < request.quantity; unit++) {
            /* Margen suficiente para esperar a que otro motor termine su vuelta. */
            const uint32_t mutex_wait_ms = MOTOR_START_TIMEOUT_MS +
                                           MOTOR_TRAVEL_TIMEOUT_MS +
                                           MOTOR_SETTLE_MS + 1000;
            esp_err_t err = motor_control_dispatch_one(request.channel, mutex_wait_ms);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Despacho abortado en canal %u (unidad %u): %s",
                         (unsigned)(request.channel + 1), (unsigned)(unit + 1),
                         esp_err_to_name(err));
                emit_event(MOTOR_EVENT_REQUEST_FAILED, request.channel,
                           (uint8_t)(request.quantity - unit), err,
                           request.admin_test);
                failed = true;
                break;
            }

            /*
             * La unidad salio fisicamente: se notifica de inmediato para que la
             * GUI descuente saldo/stock solo por unidades realmente entregadas.
             */
            emit_event(MOTOR_EVENT_UNIT_DONE, request.channel, 1, ESP_OK,
                       request.admin_test);

            if ((unit + 1) < request.quantity) {
                vTaskDelay(pdMS_TO_TICKS(MOTOR_REPEAT_DELAY_MS));
            }
        }

        if (failed) {
            /*
             * Politica del pedido: si un motor falla, se descarta el resto de la
             * cola. La GUI conserva el saldo de las unidades no entregadas y no
             * reduce su stock.
             */
            motor_request_t discarded_request;
            uint32_t discarded = 0;
            while (xQueueReceive(s_dispatch_queue, &discarded_request, 0) == pdTRUE) {
                discarded++;
            }
            if (discarded > 0) {
                ESP_LOGW(TAG, "Pedido descartado: %" PRIu32
                              " solicitud(es) pendiente(s) eliminadas", discarded);
            }
        }

        s_task_active = false;

        /* Fin de pedido: la cola quedo vacia y no hay motor girando. */
        if (uxQueueMessagesWaiting(s_dispatch_queue) == 0) {
            emit_event(MOTOR_EVENT_IDLE, 0, 0, ESP_OK, false);
        }
    }
}
