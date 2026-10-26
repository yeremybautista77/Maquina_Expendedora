/**
 * @file motor_control.h
 * @brief Control de los 6 motores de despacho mediante los expansores PCF8574
 *        del bus I2C y proteccion de la fuente con un Mutex de FreeRTOS.
 *
 * Arquitectura (Secciones 4 y 6):
 *
 *   - Modulo de relees  @ 0x20 : un bit por canal. Al poner el bit en 1 logico
 *     se activa el rele (la placa es activa en LOW, se invierte al escribir).
 *   - Modulo de sensores @ 0x21: un bit por canal. El micro-switch cierra a GND,
 *     por lo que un bit en 0 significa "final de carrera alcanzado".
 *
 *   - Solo un motor puede girar a la vez. La funcion
 *     motor_control_dispatch_one() toma g_dispatch_mutex antes de energizar el
 *     rele y lo libera al terminar (o al vencer el tiempo de espera), evitando
 *     picos de corriente que reinicien el ESP32.
 *
 *   - Se asume que el mecanismo queda con el micro-switch cerrado (LOW) en
 *     reposo. Si el canal no esta en esa posicion, el despacho se rechaza de
 *     forma segura; en la puesta en marcha hara falta una rutina de homing.
 *
 *   - Las solicitudes de compra se encolan con motor_control_enqueue() y son
 *     consumidas secuencialmente por motor_task().
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

/** Tiempo maximo para que el motor abandone el reposo tras energizar el rele. */
#define MOTOR_START_TIMEOUT_MS          3000

/** Tiempo maximo para completar una vuelta completa (sensor LOW). */
#define MOTOR_TRAVEL_TIMEOUT_MS         20000

/** Pausa de asentamiento antes de liberar el Mutex / arrancar el siguiente motor. */
#define MOTOR_SETTLE_MS                 250

/** Pausa entre unidades consecutivas del mismo canal. */
#define MOTOR_REPEAT_DELAY_MS           400

/**
 * Tiempo maximo que un canal puede estar energizado durante el homing.
 * Si el fin de carrera no se alcanza en este plazo, el rele se apaga y se
 * continua con el canal siguiente (la maquina no queda bloqueada).
 */
#define MOTOR_HOMING_TIMEOUT_MS         10000

/** Profundidad de la cola de despacho (una solicitud por producto). */
#define MOTOR_QUEUE_LENGTH              8

/**
 * @brief Solicitud de despacho: cuantas unidades de un canal deben salir.
 */
typedef struct {
    uint8_t channel;    /**< Indice 0..5 (Producto 1..6). */
    uint8_t quantity;   /**< Unidades a despachar (1..stock). */
} motor_request_t;

/**
 * @brief Tipo de evento emitido por task_despacho.
 *
 * Los eventos se emiten SIEMPRE desde el contexto de motor_task(), nunca desde
 * una ISR. El callback debe ser corto y no bloquear: la GUI solo actualiza su
 * estado protegido y despierta a task_gui.
 */
typedef enum {
    /** Una unidad salio correctamente (sensor LOW). La GUI descuenta 1 unidad. */
    MOTOR_EVENT_UNIT_DONE = 0,

    /**
     * Un motor fallo (timeout, sensor, I2C...). `units` indica cuantas unidades
     * de esa solicitud NO se entregaron. task_despacho descarta ademas el resto
     * de la cola: el pedido completo se cancela.
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
 * @brief Inicializa los dos PCF8574, la cola de despacho y el Mutex interno
 *        que protege el bus I2C.
 *
 * @param i2c_bus Bus maestro I2C ya creado por app_main().
 * @return ESP_OK en exito; ESP_ERR_INVALID_ARG si el bus es NULL;
 *         ESP_ERR_NO_MEM si no hay memoria para colas/semaforos; error I2C si
 *         no se puede configurar algun expansor.
 */
esp_err_t motor_control_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief Lleva los 6 canales a su posicion de reposo (homing de arranque).
 *
 * Para cada canal, lee el sensor de fin de carrera (PCF8574 0x21):
 *   - Si ya esta en LOW (reposo), no hace nada.
 *   - Si esta en HIGH (mecanismo fuera de posicion), energiza su rele
 *     (PCF8574 0x20) hasta que el sensor marque LOW.
 *
 * La rutina es SECUENCIAL: toma g_dispatch_mutex antes de energizar cada canal
 * y lo libera al terminar, de modo que nunca hay dos motores alimentados a la
 * vez (protege la fuente de 12 V). Cada canal tiene un timeout de seguridad de
 * MOTOR_HOMING_TIMEOUT_MS; si vence, el rele se apaga y se continua con el
 * siguiente canal.
 *
 * Debe llamarse despues de motor_control_init() y con g_dispatch_mutex creado.
 * Se invoca automaticamente al final de motor_control_init().
 *
 * @return ESP_OK si todos los canales quedaron en reposo; ESP_ERR_TIMEOUT si
 *         alguno no lo logro; error de I2C si fallo la lectura/escritura.
 */
esp_err_t motor_control_home_all(void);

/**
 * @brief Activa o desactiva el rele de un canal.
 *
 * Actualiza un registro sombra para no alterar los otros 5 canales del mismo
 * PCF8574. Es thread-safe.
 *
 * @param channel Canal 0..5.
 * @param enabled true energiza el motor, false lo apaga.
 */
esp_err_t motor_control_set_relay(uint8_t channel, bool enabled);

/**
 * @brief Lee los 6 sensores de fin de carrera en una sola transaccion I2C.
 *
 * @param[out] out_mask Mascara cruda del PCF8574 (bit 0 = canal 1).
 *                      Con BOARD_SENSOR_ACTIVE_LOW, un 0 indica sensor activo.
 */
esp_err_t motor_control_read_sensors(uint8_t *out_mask);

/**
 * @brief Lee un sensor y lo interpreta segun la polaridad de la placa.
 *
 * @param channel Canal 0..5.
 * @param[out] out_triggered true si el micro-switch esta cerrado (final de
 *             carrera alcanzado).
 */
esp_err_t motor_control_read_sensor(uint8_t channel, bool *out_triggered);

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
 * La comprobacion se hace bajo el Mutex interno de la cola, por lo que el valor
 * devuelto es una instantanea coherente. Como solo la GUI produce solicitudes,
 * el espacio disponible no puede reducirse entre esta comprobacion y
 * motor_control_enqueue_batch(); aun asi, el encolado por lotes vuelve a
 * validar la capacidad para ser robusto ante futuros productores.
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
 * Secuencia:
 *   1. Toma g_dispatch_mutex (solo un motor a la vez).
 *   2. Verifica que el canal este en reposo (sensor LOW).
 *   3. Energiza el rele.
 *   4. Espera que el sensor se libere (motor arranco).
 *   5. Espera que el sensor vuelva a LOW (vuelta completa).
 *   6. Apaga el rele SIEMPRE, aun si hubo error o timeout.
 *   7. Libera el Mutex.
 *
 * @param channel          Canal 0..5.
 * @param mutex_timeout_ms Tiempo maximo para adquirir el Mutex.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE si el canal no
 *         esta en reposo, ESP_ERR_TIMEOUT o error de I2C.
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
