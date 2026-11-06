/**
 * @file sensor_input.h
 * @brief Lectura de los sensores de posicion M1..M4 por el PCF8574 0x21.
 *
 * Este modulo es el unico que conoce el expansor de sensores. motor_control
 * consume esta API para saber si un canal esta en home; no accede directamente
 * al hardware de sensores.
 *
 * Cableado (por canal):
 *
 *     salida 12 V del micro-switch ---[10 kOhm]---+--- Pn del 0x21
 *                                                 |
 *                                             [3.3 kOhm]
 *                                                 |
 *                                                GND
 *
 * P0..P3 = M1..M4. P4..P7 no se usan.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Canales con sensor de posicion (M1..M4). */
#define SENSOR_INPUT_CHANNEL_COUNT      4

/**
 * @brief Inicializa el PCF8574 de sensores y deja sus pines como entradas.
 *
 * Si el expansor no responde, devuelve error y el sistema debe continuar en
 * lazo abierto. No es un error fatal.
 *
 * @param i2c_bus Bus maestro ya creado por app_main().
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND si no
 *         hay dispositivo en 0x21, o error de I2C.
 */
esp_err_t sensor_input_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief Indica si el expansor de sensores esta operativo.
 */
bool sensor_input_available(void);

/**
 * @brief Indica si un canal tiene sensor de posicion.
 *
 * @param channel Canal 0..5.
 * @return true para M1..M4; false para M5/M6.
 */
bool sensor_input_is_channel(uint8_t channel);

/**
 * @brief Lee los cuatro sensores en una sola transaccion I2C.
 *
 * @param[out] out_mask Mascara cruda del PCF8574 (bit 0 = M1).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE si el expansor no
 *         esta disponible, o error de I2C.
 */
esp_err_t sensor_input_read_mask(uint8_t *out_mask);

/**
 * @brief Lee un sensor y lo interpreta segun BOARD_SENSOR_HOME_LEVEL.
 *
 * @param channel  Canal 0..5 (solo M1..M4 tienen sensor).
 * @param[out] out_home true si el mecanismo esta en reposo/home.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED para M5/M6 o si
 *         el expansor no esta disponible, o error de I2C.
 */
esp_err_t sensor_input_read_channel(uint8_t channel, bool *out_home);

#ifdef __cplusplus
}
#endif
