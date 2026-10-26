/**
 * @file coin_acceptor.h
 * @brief Lectura del monedero multimoneda por interrupcion de hardware (IO35)
 *        y decodificacion de rafagas de pulsos en Pesos Dominicanos.
 *
 * Arquitectura (Secciones 2 y 3):
 *
 *   Moneda        Pulsos        Valor
 *   RD$ 1            1          RD$  1.00
 *   RD$ 5            3          RD$  5.00
 *   RD$ 10           5          RD$ 10.00
 *   RD$ 25           7          RD$ 25.00
 *
 *   La ISR solo cuenta pulsos y los deposita en una cola. La tarea del monedero
 *   abre una ventana de 300 ms: cada pulso nuevo reinicia el temporizador; si
 *   pasan 300 ms sin actividad se cierra la rafaga, se valida la cantidad de
 *   pulsos y se informa el valor mediante el callback registrado.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cantidad de pulsos por denominacion (Seccion 3). */
#define COIN_PULSES_RD1             1
#define COIN_PULSES_RD5             3
#define COIN_PULSES_RD10            5
#define COIN_PULSES_RD25            7

/** Silencio (ms) que determina el final de una rafaga. */
#define COIN_BURST_GAP_MS           300

/** Antirrebote por pulso (ms). */
#define COIN_PULSE_DEBOUNCE_MS      20

/**
 * @brief Callback invocado al cerrar una rafaga de pulsos.
 *
 * Se ejecuta en el contexto de coin_acceptor_task(), nunca en la ISR.
 *
 * @param pulses Cantidad de pulsos detectados en la rafaga.
 * @param cents  Valor reconocido en centavos (0 si la rafaga no es valida).
 * @param ctx    Contexto opcional registrado por el llamador.
 */
typedef void (*coin_acceptor_cb_t)(uint8_t pulses, uint32_t cents, void *ctx);

/**
 * @brief Configura GPIO35 como entrada con interrupcion por flanco de bajada,
 *        instala el servicio de ISR y crea la cola de pulsos.
 *
 * GPIO35 no tiene pull-up interno: la placa debe aportar uno externo.
 *
 * @return ESP_OK en exito, ESP_ERR_NO_MEM si no hay cola, o error de GPIO.
 */
esp_err_t coin_acceptor_init(void);

/**
 * @brief Registra el callback que recibira cada rafaga valida.
 *
 * @param cb  Funcion a invocar (NULL para desregistrar).
 * @param ctx Contexto de usuario.
 */
void coin_acceptor_set_callback(coin_acceptor_cb_t cb, void *ctx);

/**
 * @brief Traduce una rafaga de pulsos a centavos.
 *
 * @param pulses Pulsos detectados.
 * @return 100, 500, 1000 o 2500 centavos; 0 si la rafaga no corresponde a una
 *         moneda valida.
 */
uint32_t coin_acceptor_pulses_to_cents(uint8_t pulses);

/**
 * @brief Tarea FreeRTOS del monedero.
 *
 * Debe crearse con xTaskCreate() despues de coin_acceptor_init().
 * Prioridad sugerida: 10 (la mas alta del sistema, para no perder pulsos).
 */
void coin_acceptor_task(void *arg);

#ifdef __cplusplus
}
#endif
