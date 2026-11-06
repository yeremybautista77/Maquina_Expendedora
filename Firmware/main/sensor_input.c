/**
 * @file sensor_input.c
 * @brief Lectura del PCF8574 0x21 con los sensores de posicion M1..M4.
 *
 * El PCF8574 es cuasi-bidireccional: para usar un pin como entrada hay que
 * escribir un 1 logico (0xFF) y dejar que el divisor externo fije el nivel.
 * La lectura devuelve el byte fisico completo; solo P0..P3 tienen significado.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensor_input.h"

#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "sensor_input";

_Static_assert(SENSOR_INPUT_CHANNEL_COUNT == BOARD_SENSOR_CHANNEL_COUNT,
               "Los canales con sensor deben coincidir con board_config.h");

/** Handle I2C del PCF8574 de sensores (0x21). */
static i2c_master_dev_handle_t s_sensor_dev = NULL;

/** Protege el acceso al bus y al expansor de sensores. */
static SemaphoreHandle_t s_bus_mutex = NULL;

/** true si 0x21 respondio y sus pines quedaron configurados como entradas. */
static bool s_ready = false;

/**
 * @brief Escribe un byte en el expansor de sensores.
 */
static esp_err_t sensor_write(uint8_t value)
{
    if (s_sensor_dev == NULL || s_bus_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = i2c_master_transmit(s_sensor_dev, &value, sizeof(value), 100);

    xSemaphoreGive(s_bus_mutex);
    return err;
}

esp_err_t sensor_input_init(i2c_master_bus_handle_t i2c_bus)
{
    if (i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    s_bus_mutex = xSemaphoreCreateMutex();
    if (s_bus_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (i2c_master_probe(i2c_bus, BOARD_I2C_ADDR_SENSOR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "PCF8574 de sensores NO responde en 0x%02X",
                 BOARD_I2C_ADDR_SENSOR);
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_I2C_ADDR_SENSOR,
        .scl_speed_hz = BOARD_I2C_CLOCK_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_sensor_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar el PCF8574 de sensores: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    /* Todos los pines en 1: quedan como entradas y los divisores fijan el nivel. */
    err = sensor_write(0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar el puerto de sensores: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    /* Lectura inicial para diagnostico de cableado y polaridad. */
    uint8_t mask = 0xFF;
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }
    err = i2c_master_receive(s_sensor_dev, &mask, sizeof(mask), 100);
    xSemaphoreGive(s_bus_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo leer el estado inicial de los sensores: %s",
                 esp_err_to_name(err));
        goto fail;
    }

    s_ready = true;
    ESP_LOGI(TAG,
             "Sensores listos en 0x%02X: M1-M%u = P0-P%u (nivel inicial 0x%02X; "
             "home = %s)",
             BOARD_I2C_ADDR_SENSOR, (unsigned)SENSOR_INPUT_CHANNEL_COUNT,
             (unsigned)(SENSOR_INPUT_CHANNEL_COUNT - 1), (unsigned)mask,
             BOARD_SENSOR_HOME_LEVEL ? "HIGH" : "LOW");

    for (uint8_t ch = 0; ch < SENSOR_INPUT_CHANNEL_COUNT; ch++) {
        const bool pin_high = ((mask >> ch) & 0x01u) != 0u;
        const bool at_home = pin_high == (BOARD_SENSOR_HOME_LEVEL != 0);
        ESP_LOGI(TAG, "  M%u: %s", (unsigned)(ch + 1),
                 at_home ? "home (reposo)" : "fuera de home (transito)");
    }

    const uint8_t channel_mask =
        (uint8_t)((1u << SENSOR_INPUT_CHANNEL_COUNT) - 1u);
    if ((mask & channel_mask) != 0u) {
        ESP_LOGW(TAG,
                 "Hay sensores fuera de home al arrancar (0x%02X). Si el "
                 "mecanismo esta en reposo, revise el divisor y la polaridad",
                 (unsigned)(mask & channel_mask));
    }
    return ESP_OK;

fail:
    if (s_sensor_dev != NULL) {
        i2c_master_bus_rm_device(s_sensor_dev);
        s_sensor_dev = NULL;
    }
    if (s_bus_mutex != NULL) {
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
    }
    s_ready = false;
    return err;
}

bool sensor_input_available(void)
{
    return s_ready;
}

bool sensor_input_is_channel(uint8_t channel)
{
#if BOARD_SENSOR_ENABLED
    return channel < SENSOR_INPUT_CHANNEL_COUNT;
#else
    (void)channel;
    return false;
#endif
}

esp_err_t sensor_input_read_mask(uint8_t *out_mask)
{
    if (out_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        } else {
            uint8_t value = 0xFF;
            err = i2c_master_receive(s_sensor_dev, &value, sizeof(value), 100);
            xSemaphoreGive(s_bus_mutex);
            if (err == ESP_OK) {
                *out_mask = value;
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return err;
}

esp_err_t sensor_input_read_channel(uint8_t channel, bool *out_home)
{
    if (out_home == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sensor_input_is_channel(channel)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t mask = 0xFF;
    esp_err_t err = sensor_input_read_mask(&mask);
    if (err != ESP_OK) {
        return err;
    }

    const bool pin_high = ((mask >> channel) & 0x01u) != 0u;
    *out_home = pin_high == (BOARD_SENSOR_HOME_LEVEL != 0);
    return ESP_OK;
}
