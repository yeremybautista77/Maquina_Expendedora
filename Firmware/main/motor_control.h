/**
 * @file motor_control.h
 * @brief Control de los 6 motores de despacho mediante el PCF8574 0x20 y
 *        lectura de sensores M1..M4 por el PCF8574 0x21.
 *
 * Estrategia:
 *
 *   - M1..M4 tienen micro-switch de posicion. El rele se energiza y el motor
 *     se detiene al volver al home (misma posicion en cada vuelta). El pulso
 *     por tiempo es solo el techo de seguridad si el sensor no responde.
 *   - M5..M6 no tienen sensor: lazo abierto con MOTOR_TIMED_PULSE_MS.
 *   - Si el PCF8574 0x21 no responde, los 6 canales usan lazo abierto.
 *   - El PCF8574 0x20 escribe el bit del canal: con el transistor NPN inversor
 *     y el jumper H/L del modulo en LOW, HIGH (3.3 V) activa el rele y LOW
 *     (0 V) lo desactiva.
 *   - Cada despacho apaga el rele SIEMPRE al terminar, incluso ante error.
 *   - Solo un motor gira a la vez: motor_control_dispatch_one() toma
 *     g_dispatch_mutex antes de energizar el rele y lo libera al terminar.
 *   - Las solicitudes se encolan con motor_control_enqueue() y las consume
 *     secuencialmente motor_task().
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cantidad de canales fisicos de la maquina. */
#define MOTOR_CHANNEL_COUNT             6

/** Duracion del pulso para M5/M6 (sin sensor) o si 0x21 no responde. */
#define MOTOR_TIMED_PULSE_MS            3000

/** Pausa de asentamiento antes de liberar el Mutex / arrancar el siguiente motor. */
#define MOTOR_SETTLE_MS                 250

/** Pausa entre unidades consecutivas del mismo canal. */
#define MOTOR_REPEAT_DELAY_MS           400

/** Profundidad de la cola de despacho (una solicitud por producto). */
#define MOTOR_QUEUE_LENGTH              8

/**
 * @brief Solicitud de despacho: cuantas unidades de un canal deben salir.
 */
typedef struct {
    uint8_t channel;    /**< Indice 0..5 (Producto 1..6). */
    uint8_t quantity;   /**< Unidades a despachar (1..stock). */
    /**
     * true si la solicitud viene del modo administracion (prueba de motor).
     * En ese caso la GUI NO cobra saldo: solo descuenta el stock de la unidad
     * que fisicamente cae.
     */
    bool admin_test;
} motor_request_t;

/**
 * @brief Tipo de evento emitido por task_despacho.
 *
 * Los eventos se emiten SIEMPRE desde el contexto de motor_task(), nunca desde
 * una ISR. El callback debe ser corto y no bloquear: la GUI solo actualiza su
 * estado protegido y despierta a task_gui.
 */
typedef enum {
    /** Una unidad salio correctamente. La GUI descuenta 1 unidad. */
    MOTOR_EVENT_UNIT_DONE = 0,

    /**
     * Un motor fallo (I2C u otro error). `units` indica cuantas unidades de esa
     * solicitud NO se entregaron. task_despacho descarta ademas el resto de la
     * cola: el pedido completo se cancela.
     */
    MOTOR_EVENT_REQUEST_FAILED,

    /** La cola quedo vacia y ningun motor esta girando (fin de pedido). */
    MOTOR_EVENT_IDLE,
} motor_event_type_t;

/**
 * @brief Datos de un evento de despacho.
 */
typedef struct {
    motor_event_type_t type;    /**< Tipo de evento. */
    uint8_t channel;            /**< Canal 0..5 (solo UNIT_DONE / FAILED). */
    uint8_t units;              /**< Unidades entregadas o no entregadas. */
    esp_err_t error;            /**< Codigo de error (solo FAILED). */
    /**
     * true si la solicitud origen era una prueba de administrador. La GUI usa
     * este campo para no tocar el saldo ni el pedido de compra.
     */
    bool admin_test;
} motor_event_t;

/**
 * @brief Callback de eventos de despacho.
 *
 * @param event Evento (valido solo durante la llamada).
 * @param ctx   Contexto registrado por el usuario.
 */
typedef void (*motor_event_cb_t)(const motor_event_t *event, void *ctx);

/**
 * @brief Mutex global de despacho.
 *
 * Se DEFINE en main.c y se crea en app_main() antes de lanzar las tareas.
 * Protege el recurso "fuente de 12 V + motores" para que solo gire un motor
 * a la vez. Cualquier modulo que energice un motor debe tomarlo.
 */
extern SemaphoreHandle_t g_dispatch_mutex;

/**
 * @brief Inicializa los PCF8574 0x20 (relees) y 0x21 (sensores), la cola de
 *        despacho y el Mutex interno que protege el bus I2C.
 *
 * El expansor de sensores es opcional: si no responde, se registra un aviso y
 * el sistema continua en lazo abierto por tiempo.
 *
 * @param i2c_bus Bus maestro I2C ya creado por app_main().
 * @return ESP_OK en exito; ESP_ERR_INVALID_ARG si el bus es NULL;
 *         ESP_ERR_NO_MEM si no hay memoria para colas/semaforos; error I2C si
 *         no se puede configurar el expansor de relees.
 */
esp_err_t motor_control_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief Activa o desactiva el rele de un canal.
 *
 * Actualiza un registro sombra para no alterar los otros 5 canales. Es
 * thread-safe.
 *
 * @param channel Canal 0..5.
 * @param enabled true energiza el motor (HIGH fisico en 0x20, NPN saturado),
 *                false lo apaga (LOW fisico, NPN al corte).
 */
esp_err_t motor_control_set_relay(uint8_t channel, bool enabled);

/**
 * @brief Indica si el PCF8574 de sensores (0x21) esta operativo.
 *
 * Si devuelve false, todos los canales se despachan en lazo abierto.
 */
bool motor_control_sensors_available(void);

/**
 * @brief Indica si un canal tiene sensor de posicion.
 *
 * @param channel Canal 0..5.
 * @return true para M1..M4; false para M5..M6.
 */
bool motor_control_channel_uses_sensor(uint8_t channel);

/**
 * @brief Lee los cuatro sensores en una sola transaccion I2C.
 *
 * Solo los bits P0..P3 tienen significado fisico (M1..M4); P4..P7 no se usan.
 *
 * @param[out] out_mask Mascara cruda del PCF8574 (bit 0 = M1).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED si 0x21 no esta
 *         disponible o error de I2C.
 */
esp_err_t motor_control_read_sensors(uint8_t *out_mask);

/**
 * @brief Lee un sensor y lo interpreta segun BOARD_SENSOR_HOME_LEVEL.
 *
 * Solo valido para M1..M4. Para M5/M6 devuelve ESP_ERR_NOT_SUPPORTED.
 *
 * @param channel Canal 0..5.
 * @param[out] out_home true si el mecanismo esta en reposo/home.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_SUPPORTED o error de I2C.
 */
esp_err_t motor_control_read_sensor(uint8_t channel, bool *out_home);

/**
 * @brief Lleva a home los canales M1..M4 que quedaron a mitad de vuelta.
 *
 * Rutina SECUENCIAL: toma g_dispatch_mutex antes de energizar cada canal y lo
 * libera al terminar, de modo que nunca hay dos motores alimentados a la vez.
 * Cada canal tiene un timeout de seguridad; si vence, el rele se apaga y se
 * continua con el siguiente.
 *
 * Solo se invoca automaticamente si BOARD_SENSOR_HOMING_ENABLED = 1.
 *
 * @return ESP_OK si todos los canales quedaron en reposo; ESP_ERR_TIMEOUT si
 *         alguno no lo logro; ESP_ERR_NOT_SUPPORTED si no hay sensores.
 */
esp_err_t motor_control_home_all(void);

/**
 * @brief Encola una solicitud de despacho (una entrada por producto).
 *
 * Envoltorio de motor_control_enqueue_batch() para una sola solicitud.
 *
 * @param channel  Canal 0..5.
 * @param quantity Unidades a despachar (>= 1).
 * @return ESP_OK, ESP_ERR_INVALID_ARG o ESP_ERR_TIMEOUT si la cola esta llena.
 */
esp_err_t motor_control_enqueue(uint8_t channel, uint8_t quantity);

/**
 * @brief Comprueba si la cola puede aceptar un pedido completo de forma atomica.
 *
 * Se usa antes de registrar un pedido en la GUI: si no hay espacio para TODAS
 * las solicitudes, el pedido se rechaza entero y no se encola ninguna.
 *
 * @param count Numero de solicitudes del pedido (1..MOTOR_QUEUE_LENGTH).
 * @return ESP_OK si hay espacio para todas; ESP_ERR_TIMEOUT si no lo hay;
 *         ESP_ERR_INVALID_ARG si count es 0 o mayor que la cola;
 *         ESP_ERR_INVALID_STATE si el modulo no esta inicializado.
 */
esp_err_t motor_control_check_capacity(uint8_t count);

/**
 * @brief Encola un pedido completo de forma atomica (todo o nada).
 *
 * Valida primero que haya huecos libres para las `count` solicitudes. Si no
 * hay espacio suficiente, NO encola ninguna y devuelve ESP_ERR_TIMEOUT, de
 * modo que el usuario nunca recibe una compra parcial por culpa de la cola.
 *
 * @param requests Array de `count` solicitudes (canal 0..5, quantity >= 1).
 * @param count    Numero de solicitudes (1..MOTOR_QUEUE_LENGTH).
 * @return ESP_OK si se encolaron todas; ESP_ERR_TIMEOUT si la cola no tiene
 *         espacio para el pedido completo; ESP_ERR_INVALID_ARG si algun dato es
 *         invalido; ESP_ERR_INVALID_STATE si el modulo no esta inicializado.
 */
esp_err_t motor_control_enqueue_batch(const motor_request_t *requests,
                                      uint8_t count);

/**
 * @brief Registra el callback de eventos de despacho.
 *
 * Debe llamarse antes de crear motor_task(). El callback se invoca desde
 * motor_task(), por lo que no puede llamar directamente a LVGL.
 *
 * @param callback Funcion a invocar (NULL para desregistrar).
 * @param ctx      Contexto opaco entregado al callback.
 */
esp_err_t motor_control_set_event_callback(motor_event_cb_t callback, void *ctx);

/**
 * @brief Descarta las solicitudes que aun no han empezado a despacharse.
 *
 * No interrumpe un motor que ya esta girando. Se usa cuando la GUI no pudo
 * encolar un pedido completo y necesita cancelar lo pendiente.
 *
 * @param[out] out_discarded Numero de solicitudes eliminadas (puede ser NULL).
 */
esp_err_t motor_control_cancel_pending(uint32_t *out_discarded);

/**
 * @brief Indica si hay un motor girando o solicitudes pendientes.
 */
bool motor_control_is_busy(void);

/**
 * @brief Despacha UNA unidad de un canal, de forma exclusiva.
 *
 * Canales M1..M4 (con sensor disponible):
 *   1. Toma g_dispatch_mutex (solo un motor a la vez).
 *   2. Lee el sensor y energiza el rele (HIGH fisico en 0x20).
 *   3. Espera que el motor abandone el home y vuelva a el.
 *   4. Apaga el rele SIEMPRE, aun si hubo error o timeout.
 *   5. Libera el Mutex.
 *
 * Canales M5..M6 (sin sensor) o si 0x21 no responde:
 *   1. Toma g_dispatch_mutex.
 *   2. Energiza el rele.
 *   3. Mantiene el pulso MOTOR_TIMED_PULSE_MS (3000 ms).
 *   4. Apaga el rele y libera el Mutex.
 *
 * @param channel          Canal 0..5.
 * @param mutex_timeout_ms Tiempo maximo para adquirir el Mutex.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT
 *         si el sensor no alcanza el home a tiempo, o error de I2C.
 */
esp_err_t motor_control_dispatch_one(uint8_t channel, uint32_t mutex_timeout_ms);

/**
 * @brief Tarea FreeRTOS que consume la cola de despacho en orden (FIFO).
 *
 * Emite MOTOR_EVENT_UNIT_DONE por cada unidad entregada. Si una unidad falla,
 * emite MOTOR_EVENT_REQUEST_FAILED, descarta el resto de la cola (el pedido se
 * cancela) y finalmente emite MOTOR_EVENT_IDLE. Cuando termina el ultimo
 * pedido sin errores tambien emite MOTOR_EVENT_IDLE.
 *
 * Debe crearse con xTaskCreate() despues de motor_control_init() y de
 * motor_control_set_event_callback(). Prioridad sugerida: 8.
 */
void motor_task(void *arg);

#ifdef __cplusplus
}
#endif
