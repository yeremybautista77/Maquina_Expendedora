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
 *    La ISR se dispara por ambos flancos (senal activa en LOW) para medir el
 *    ancho del pulso.
 *  - El PCF8574 esta en el bus I2C (SDA=IO32, SCL=IO25):
 *        0x20 -> 6 modulos de rele de 1 canal (motores de 12 V). Jumper H/L
 *                fisico en LOW: IN en LOW (0 V) activa el motor.
 *                Cada canal lleva un transistor NPN inversor entre el
 *                PCF8574 y el IN del modulo (ver BOARD_RELAY_ACTIVE_LOW).
 *        0x21 -> sensores de posicion M1..M4 (P0..P3). La salida de 12 V del
 *                micro-switch se escala con un divisor 10 kOhm / 3.3 kOhm
 *                (~2.97 V) antes de entrar al PCF8574. Ver BOARD_SENSOR_*.
 *                P4..P7 quedan sin uso. M5 y M6 no tienen sensor: se
 *                despachan por tiempo. Si el expansor no responde, el firmware
 *                sigue en lazo abierto por tiempo (no bloquea la maquina).
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
 *
 *  Monedero multimoneda CH-926 por IO35 a traves del optoacoplador PC817.
 *  GPIO35 es un pin SOLO DE ENTRADA: requiere el pull-up externo de 10 kOhm a
 *  3.3 V que ya esta en la placa. El firmware NO habilita el pull-up interno.
 *
 *  BOARD_COIN_ACCEPTOR_ENABLED:
 *    1 = monedero activo (IO35 + ISR + task_monedero). El saldo tambien puede
 *        cargarse desde el panel ADM.
 *    0 = monedero deshabilitado; IO35 no se inicializa y el saldo solo se
 *        carga desde ADM.
 * ========================================================================== */
#define BOARD_COIN_ACCEPTOR_ENABLED 1

#define BOARD_COIN_GPIO             GPIO_NUM_35
#define BOARD_COIN_ACTIVE_LOW       1       /* Optoacoplador PC817 -> flanco de bajada */

/* ==========================================================================
 *  Bus I2C y expansor PCF8574 (Seccion 4)
 * ========================================================================== */
#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_SDA_GPIO          GPIO_NUM_32   /* Confirmado: SDA */
#define BOARD_I2C_SCL_GPIO          GPIO_NUM_25   /* Confirmado: SCL */
#define BOARD_I2C_CLOCK_HZ          100000         /* 100 kHz: robusto con cables largos */

#define BOARD_I2C_ADDR_RELAY        0x20    /* PCF8574: P0..P5 -> relees de motor */
#define BOARD_I2C_ADDR_SENSOR       0x21    /* PCF8574: P0..P3 -> sensores M1..M4 */

/*
 * Hardware actual: 6 modulos de 1 canal con el jumper H/L en LOW (entrada
 * activa en LOW: 0 V en INn enciende el rele) y un transistor NPN por canal
 * usado como inversor:
 *
 *      P0..P5 --[1 kOhm]-- BASE
 *                          EMISOR   -> GND
 *                          COLECTOR -> IN del modulo de rele
 *
 * Con ese transistor la logica queda invertida: el PCF8574 debe sacar HIGH
 * para encender (HIGH -> NPN saturado -> IN a 0 V -> rele ON). Por eso el
 * registro sombra se escribe TAL CUAL, sin invertirlo.
 *
 *   1 = salida del PCF8574 activa en LOW  (rele conectado directo, sin NPN)
 *   0 = salida del PCF8574 activa en HIGH (rele con transistor NPN inversor)
 */
#define BOARD_RELAY_ACTIVE_LOW      0

/* ==========================================================================
 *  Estrategia de motores
 *
 *  Sin sensores montados, los 6 canales M1..M6 se accionan con un pulso de
 *  BOARD_TIMED_PULSE_MS (lazo abierto). Con el PCF8574 0x21 presente, el
 *  firmware usa el sensor de posicion como condicion de parada y deja el pulso
 *  por tiempo solo como techo de seguridad.
 * ========================================================================== */
#define BOARD_TIMED_PULSE_MS        3000    /* Pulso de una vuelta (~3 s) */

/* ==========================================================================
 *  Sensores de posicion (PCF8574 0x21)
 *
 *  Canales M1..M4 (P0..P3). M5 y M6 NO tienen sensor: se despachan por
 *  tiempo con BOARD_TIMED_PULSE_MS.
 *
 *  Cableado por canal:
 *
 *      salida 12 V del micro-switch ---[10 kOhm]---+--- Pn del 0x21
 *                                                  |
 *                                              [3.3 kOhm]
 *                                                  |
 *                                                 GND
 *
 *  Con 12 V en la entrada del divisor, el pin del PCF8574 ve ~2.97 V (HIGH
 *  valido: Vih = 0.7 x 3.3 V = 2.31 V). En reposo/home el switch entrega 0 V
 *  y el pin lee LOW.
 *
 *  El PCF8574 se alimenta a 3.3 V para no superar el maximo del pin. El divisor
 *  esta definido para una senal de 12 V (dato confirmado del montaje):
 *  R1 = 10 kOhm en serie y R2 = 3.3 kOhm a GND -> Vpin = 2.97 V.
 *  Si en el futuro el sensor entregara otra tension, recalcular
 *  R2 = R1 * Vpin / (Vsensor - Vpin) con Vpin <= 3.0 V. No cambiar el firmware
 *  por el divisor.
 *
 *  BOARD_SENSOR_ENABLED = 1 activa la lectura. Si el expansor 0x21 no responde
 *  al arrancar, el firmware lo informa por log y sigue en lazo abierto.
 * ========================================================================== */
#define BOARD_SENSOR_ENABLED            1
#define BOARD_SENSOR_CHANNEL_COUNT      4   /* M1..M4 en P0..P3 */

/* Nivel que se considera "en reposo / home". Se asume LOW (switch cerrado a
 * GND en reposo). Confirmar con el multimetro antes de activar el homing. */
#define BOARD_SENSOR_HOME_LEVEL         0

/* Periodo de muestreo mientras se espera un cambio de sensor. */
#define BOARD_SENSOR_POLL_MS            10

/* Tiempo maximo para que el motor abandone el home tras energizar el rele. */
#define BOARD_SENSOR_LEAVE_TIMEOUT_MS   3000

/* Tiempo maximo para completar una vuelta y volver al home. Es el techo de
 * seguridad: si se cumple, el rele se apaga y el despacho se reporta fallido. */
#define BOARD_SENSOR_TRAVEL_TIMEOUT_MS  6000

/* Tiempo minimo de giro antes de aceptar el pulso de home (anti-rebote). */
#define BOARD_SENSOR_MIN_PULSE_MS       300

/* Homing de arranque: lleva a home los canales M1..M4 que quedaron a mitad de
 * vuelta. Se deja DESACTIVADO hasta validar la polaridad del sensor en la
 * placa; si se activa, cada canal puede girar hasta el timeout configurado. */
#define BOARD_SENSOR_HOMING_ENABLED     0
#define BOARD_SENSOR_HOMING_TIMEOUT_MS  8000

/* ==========================================================================
 *  Prueba de banco por etapas (puesta en marcha)
 *
 *  0 = produccion (sin prueba)
 *  1 = pulso directo del rele M1 durante BOARD_TIMED_PULSE_MS al arrancar
 *
 * En modo produccion (0) no se ejecuta ninguna prueba: las compras despachan
 * normalmente. El monedero se controla con BOARD_COIN_ACCEPTOR_ENABLED.
 * Dejar en 0 para la version final.
 * ========================================================================== */
#define BRINGUP_TEST_MOTOR1         0

/* ==========================================================================
 *  MicroSD por SPI (Seccion 2)
 *
 *  IMPORTANTE: las imagenes de los productos ya NO se leen de la MicroSD;
 *  van embebidas en flash (main/product_images.c). Por eso la SD queda
 *  DESACTIVADA para evitar el error de montaje en cada arranque.
 *
 *  BOARD_SD_ENABLED:
 *    0 = no se intenta montar la MicroSD (configuracion actual).
 *    1 = se monta la SD en BOARD_SD_MOUNT_POINT (codigo conservado por si se
 *        vuelve a usar para otros fines).
 *
 *  Se usa el host SDSPI que forma parte del componente "sdmmc"/"driver" de
 *  ESP-IDF. El bus SPI3 (VSPI) se reserva para la tarjeta; la pantalla TFT usa
 *  su propio bus. Si la placa comparte el bus SPI entre TFT y SD, se debera
 *  reutilizar el bus ya inicializado en lugar de llamar a spi_bus_initialize().
 *
 *  Si el esquematico conecta la MicroSD al periferico SDMMC nativo (CLK/CMD/D0),
 *  sustituir el montaje por esp_vfs_fat_sdmmc_mount() con SDMMC_HOST_DEFAULT()
 *  y SDMMC_SLOT_CONFIG_DEFAULT().
 * ========================================================================== */
#define BOARD_SD_ENABLED            0
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

/* Ajustes de orientacion/color del panel. El ST7796 de esta placa NO necesita
 * inversion: con BOARD_TFT_INVERT_COLOR = 1 el tema oscuro se veia claro y los
 * colores se invertian (verde -> rosado, rojo -> cian). */
#define BOARD_TFT_USE_BGR_ORDER         1
#define BOARD_TFT_INVERT_COLOR          0
#define BOARD_TFT_SWAP_XY               0
/* El panel entrega la imagen reflejada en X con MADCTL por defecto: se corrige
 * aqui. El tactil, en cambio, entrega las coordenadas rotadas 180 grados
 * respecto a la pantalla ya enderezada; por eso se invierten ambos ejes
 * (mirror_x = 0, mirror_y = 1). Si en otro montaje el tactil queda espejado,
 * basta alternar estos dos valores. */
#define BOARD_TFT_MIRROR_X              1
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

/* Ajustes de calibracion del tactil (confirmados en la placa real). */
#define BOARD_TOUCH_SWAP_XY             0
#define BOARD_TOUCH_MIRROR_X            0
#define BOARD_TOUCH_MIRROR_Y            1

#ifdef __cplusplus
}
#endif
