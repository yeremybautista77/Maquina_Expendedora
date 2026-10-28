/**
 * @file board_config.h
 * @brief Mapa de pines y constantes de hardware de la maquina expendedora.
 *
 * Fuente de verdad: Documentacion/Documentacion Tecnica/
 *                  documento_de_arquitectura_tecnico.md
 *
 * IMPORTANTE (hardware confirmado fisicamente 2026-10-25):
 *  - El monedero entrega pulsos por el optoacoplador PC817 sobre IO35.
 *    GPIO35 es un pin SOLO DE ENTRADA y NO dispone de pull-up interno. La placa
 *    ya tiene una resistencia externa de 10 kOhm a 3.3 V, por lo que el firmware
 *    DEBE dejar el pull-up interno DESHABILITADO (no cargar el optoacoplador).
 *    La ISR se dispara por flanco de bajada (senal activa en LOW).
 *  - Los dos PCF8574 comparten el mismo bus I2C (SDA=IO32, SCL=IO25):
 *        0x20 -> modulo de relees (motores de 12 V), activo en LOW
 *        0x21 -> sensores de fin de carrera SOLO en P0..P3 (M1..M4). La salida
 *                de 12 V del micro-switch se escala con un divisor externo de
 *                10 kOhm en serie y 3.3 kOhm a GND (~2.97 V). La placa LK20052
 *                queda intacta: no se cortan pistas.
 *        P4/P5 de 0x21 no tienen conexion fisica: M5 y M6 (LK20052 Fy-007, de
 *        2 hilos) se despachan en lazo abierto con un pulso temporizado.
 *  - Pantalla TFT ST7796 de 320x480 (vertical) por SPI dedicado (HSPI/SPI2).
 *  - Tactil resistivo XPT2046 por SPI, compartiendo el bus de la TFT.
 *  - MicroSD por VSPI (SPI3): CS=5, CLK=18, MISO=19, MOSI=23.
 *  - Todos los pines de este archivo estan CONFIRMADOS contra la placa real.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  Canales de producto
 * ========================================================================== */
#define BOARD_CHANNEL_COUNT         6

/* ==========================================================================
 *  Monedero (Seccion 2 y 3 del documento de arquitectura)
 * ========================================================================== */
#define BOARD_COIN_GPIO             GPIO_NUM_35
#define BOARD_COIN_ACTIVE_LOW       1       /* Optoacoplador PC817 -> flanco de bajada */

/* ==========================================================================
 *  Bus I2C y expansores PCF8574 (Seccion 4)
 * ========================================================================== */
#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_SDA_GPIO          GPIO_NUM_32   /* Confirmado: SDA */
#define BOARD_I2C_SCL_GPIO          GPIO_NUM_25   /* Confirmado: SCL */
#define BOARD_I2C_CLOCK_HZ          100000         /* 100 kHz: robusto con cables largos */

#define BOARD_I2C_ADDR_RELAY        0x20    /* PCF8574: P0..P5 -> relees de motor */
#define BOARD_I2C_ADDR_SENSOR       0x21    /* PCF8574: P0..P3 -> fines de carrera M1..M4 */

#define BOARD_RELAY_ACTIVE_LOW      1       /* Modulo de relees tipico activo en LOW */

/* ==========================================================================
 *  Estrategia hibrida de motores
 *
 *  M1..M4 (3 hilos, LK20052): micro-switch de 12 V acondicionado con divisor
 *  10 kOhm / 3.3 kOhm. En reposo el switch entrega 12 V y el divisor deja
 *  ~2.97 V en el pin del PCF8574, por lo que el reposo se lee HIGH.
 *  SENSOR_HOME_LEVEL abstrae esa polaridad: cambiarla a 0 invierte la
 *  interpretacion sin tocar el circuito.
 *
 *  M5..M6 (2 hilos, LK20052 Fy-007): sin sensor ni homing. Se aplica un pulso
 *  calibrado de BOARD_TIMED_PULSE_MS al rele correspondiente de 0x20.
 * ========================================================================== */
#define BOARD_SENSOR_CHANNEL_COUNT  4       /* Canales M1..M4 con retroalimentacion */
#define BOARD_TIMED_CHANNEL_FIRST   4       /* Primer canal por tiempo (M5) */
#define SENSOR_HOME_LEVEL           1       /* 1 = reposo HIGH (~2.97 V) | 0 = reposo LOW */
#define BOARD_TIMED_PULSE_MS        2000    /* Pulso calibrado en lazo abierto */

/* ==========================================================================
 *  MicroSD por SPI (Seccion 2)
 *
 *  Se usa el host SDSPI que forma parte del componente "sdmmc"/"driver" de
 *  ESP-IDF. El bus SPI3 (VSPI) se reserva para la tarjeta; la pantalla TFT usara
 *  su propio bus. Si la placa comparte el bus SPI entre TFT y SD, se debera
 *  reutilizar el bus ya inicializado en lugar de llamar a spi_bus_initialize().
 *
 *  Si el esquematico conecta la MicroSD al periferico SDMMC nativo (CLK/CMD/D0),
 *  sustituir el montaje por esp_vfs_fat_sdmmc_mount() con SDMMC_HOST_DEFAULT()
 *  y SDMMC_SLOT_CONFIG_DEFAULT().
 * ========================================================================== */
#define BOARD_SD_MOUNT_POINT        "/sdcard"
#define BOARD_SD_SPI_HOST           SPI3_HOST       /* VSPI confirmado */
#define BOARD_SD_PIN_CS             GPIO_NUM_5      /* Confirmado: VSPI CS  */
#define BOARD_SD_PIN_CLK            GPIO_NUM_18     /* Confirmado: VSPI CLK */
#define BOARD_SD_PIN_MISO           GPIO_NUM_19     /* Confirmado: VSPI MISO */
#define BOARD_SD_PIN_MOSI           GPIO_NUM_23     /* Confirmado: VSPI MOSI */

/* ==========================================================================
 *  Pantalla TFT ST7796 320x480 (SPI2 / HSPI)
 *
 *  Pines confirmados fisicamente en la placa "ESP32-32E 4.0 inch".
 *  El bus SPI2 se comparte con el tactil XPT2046 (mismo SCLK/MOSI/MISO y un CS
 *  independiente). La MicroSD usa SPI3 (VSPI) y no comparte bus.
 * ========================================================================== */
#define BOARD_TFT_SPI_HOST              SPI2_HOST
#define BOARD_TFT_H_RES                 320
#define BOARD_TFT_V_RES                 480

#define BOARD_TFT_PIN_SCLK              GPIO_NUM_14   /* Confirmado */
#define BOARD_TFT_PIN_MOSI              GPIO_NUM_13   /* Confirmado */
#define BOARD_TFT_PIN_MISO              GPIO_NUM_12   /* Confirmado */
#define BOARD_TFT_PIN_CS                GPIO_NUM_15   /* Confirmado */
#define BOARD_TFT_PIN_DC                GPIO_NUM_2    /* Confirmado */
#define BOARD_TFT_PIN_RST               GPIO_NUM_NC   /* Confirmado: RST atado a EN */
#define BOARD_TFT_PIN_BL                GPIO_NUM_27   /* Confirmado */

#define BOARD_TFT_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)
#define BOARD_TFT_CMD_BITS              8
#define BOARD_TFT_PARAM_BITS            8

/* Retroiluminacion por LEDC (PWM). */
#define BOARD_TFT_BL_LEDC_MODE          LEDC_LOW_SPEED_MODE
#define BOARD_TFT_BL_LEDC_TIMER         LEDC_TIMER_0
#define BOARD_TFT_BL_LEDC_CHANNEL       LEDC_CHANNEL_0
#define BOARD_TFT_BL_LEDC_RES           LEDC_TIMER_10_BIT
#define BOARD_TFT_BL_LEDC_FREQ_HZ       5000
#define BOARD_TFT_BL_DUTY_ON            1023

/* Ajustes de orientacion/color del panel (vertical, BGR e inversion confirmados). */
#define BOARD_TFT_USE_BGR_ORDER         1
#define BOARD_TFT_INVERT_COLOR          1
#define BOARD_TFT_SWAP_XY               0
#define BOARD_TFT_MIRROR_X              0
#define BOARD_TFT_MIRROR_Y              0

/* ==========================================================================
 *  Tactil resistivo XPT2046 (comparte SPI2 con la TFT)
 *
 *  El driver se usa en modo sondeo (polling): el pin PENIRQ no se conecta al
 *  GPIO para evitar interrupciones sin manejador. Si se desea usar la IRQ,
 *  definir BOARD_TOUCH_PIN_IRQ con un GPIO valido y configurar el callback.
 * ========================================================================== */
#define BOARD_TOUCH_SPI_HOST            BOARD_TFT_SPI_HOST
#define BOARD_TOUCH_PIN_CS              GPIO_NUM_33   /* Confirmado */
#define BOARD_TOUCH_PIN_IRQ             GPIO_NUM_NC   /* Confirmado: sin IRQ, sondeo */
#define BOARD_TOUCH_SPI_CLOCK_HZ        (1 * 1000 * 1000)

/* Ajustes de calibracion del tactil. TODO_CALIBRAR con la pantalla real. */
#define BOARD_TOUCH_SWAP_XY             0
#define BOARD_TOUCH_MIRROR_X            1
#define BOARD_TOUCH_MIRROR_Y            0

#ifdef __cplusplus
}
#endif
