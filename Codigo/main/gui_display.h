/**
 * @file gui_display.h
 * @brief Interfaz grafica sobre la pantalla TFT ST7796 de 4.0" (320x480) y
 *        acceso a los activos graficos almacenados en la MicroSD.
 *
 * Arquitectura (Seccion 5):
 *
 *   +---------------------------+
 *   |  P1            |  P2      |
 *   |  RD$ 40.00     |  RD$ 50.00
 *   |  [Espacio Imagen]         |  Layout Flex 2x3
 *   |  [-] Cant: 0 [+]          |  (~83 % superior)
 *   |---------------------------|
 *   |  P3            |  P4      |
 *   |  ...                      |
 *   |---------------------------|
 *   |  P5            |  P6      |
 *   |  ...                      |
 *   +---------------------------+
 *   |  Saldo: RD$ XX.00         |
 *   |  Monto a Pagar: RD$ XX.00 |  Panel inferior
 *   |  Seleccione productos     |  [ Confirmar ]
 *   +---------------------------+
 *
 *   Al confirmar se muestra un modal con el resumen y [Si] / [No].
 *
 * Flujo de compra (correccion del riesgo #3):
 *   1. gui_display_confirm_purchase() SOLO valida y encola el pedido en
 *      task_despacho. NO descuenta saldo ni stock.
 *   2. motor_task() emite MOTOR_EVENT_UNIT_DONE cada vez que una unidad
 *      completa su vuelta (sensor LOW). En ese momento, y solo en ese momento,
 *      gui_display_on_unit_dispatched() resta el precio al saldo y reduce el
 *      stock en 1.
 *   3. Si un motor falla por timeout, motor_task() emite
 *      MOTOR_EVENT_REQUEST_FAILED y descarta el resto de la cola. Las unidades
 *      no entregadas no se cobran ni reducen stock.
 *
 * Encolado atomico (correccion del riesgo #4):
 *   gui_display_confirm_purchase() comprueba la capacidad de la cola para el
 *   lote COMPLETO antes de registrar el pedido y encola con
 *   motor_control_enqueue_batch(). Si no cabe, el pedido se rechaza entero y la
 *   GUI pide reducir la cantidad.
 *
 * Modo administracion:
 *   Pulsacion larga de 3 s sobre un boton invisible en la esquina superior
 *   izquierda. Abre un overlay con "Rellenar Stock (Reset a 10)", que guarda el
 *   nuevo stock en NVS y vuelve a la pantalla principal.
 *
 * Concurrencia:
 *   - El estado (saldo, stock, cantidades y pedido en curso) esta protegido por
 *     un Mutex interno (s_state_mutex).
 *   - task_monedero puede llamar a gui_display_add_balance_cents() y
 *     gui_display_show_message() desde su propio contexto: esas funciones solo
 *     tocan estado protegido y despiertan a task_gui.
 *   - TODAS las llamadas a LVGL se hacen exclusivamente desde gui_task(). Las
 *     funciones publicas de esta capa NUNCA dibujan directamente; marcan el
 *     estado como "sucio" y notifican a task_gui.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Canales/productos visibles en el grid. */
#define GUI_PRODUCT_COUNT           6

/** Maximo de unidades seleccionables por producto (regla de la Seccion 5). */
#define GUI_MAX_QTY_PER_PRODUCT     10

/** Precio sin configurar; se carga desde la SD o se fija en el codigo. */
#define GUI_PRICE_UNSET_CENTS       0

/**
 * @brief Modelo de un producto mostrado en el grid.
 */
typedef struct {
    const char *name;           /**< Nombre visible. */
    const char *image_path;     /**< Ruta del activo en la MicroSD. */
    uint32_t price_cents;       /**< Precio unitario en centavos. */
    uint8_t stock;              /**< Existencias disponibles. */
    uint8_t quantity;           /**< Unidades seleccionadas por el usuario. */
} gui_product_t;

/**
 * @brief Inicializa el bus SPI2, el panel ST7796, la retroiluminacion LEDC, el
 *        tactil XPT2046, LVGL y construye el grid de 2x3 + panel inferior.
 *
 * Crea ademas el Mutex de estado. Debe llamarse antes que cualquier otra
 * funcion de estado y antes de crear task_gui. Si el panel falla, devuelve el
 * error pero el Mutex queda creado para que el resto del sistema siga operando.
 *
 * @return ESP_OK en exito; error de SPI, esp_lcd, tactil o falta de memoria.
 */
esp_err_t gui_display_init(void);

/**
 * @brief Monta la MicroSD (FAT) en BOARD_SD_MOUNT_POINT ("/sdcard").
 *
 * Debe llamarse ANTES de gui_display_init() para que las imagenes BMP esten
 * disponibles al construir el grid.
 *
 * @return ESP_OK, error de SPI o error de montaje de FatFs.
 */
esp_err_t gui_display_mount_sd(void);

/**
 * @brief Desmonta la MicroSD y libera el bus SPI si lo inicializo este modulo.
 */
esp_err_t gui_display_unmount_sd(void);

/**
 * @brief Carga saldo y stock de los 6 productos desde la NVS.
 *
 * Los precios NO se persisten: siempre se toman de la tabla compilada
 * (RD$ 40..90). Si la NVS esta vacia o no esta inicializada se aplican los
 * valores por defecto (saldo 0 y 10 unidades por producto).
 *
 * gui_display_init() la invoca automaticamente. Es seguro llamarla de nuevo
 * desde cualquier tarea: solo toca estado protegido y despierta a task_gui.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE (Mutex no creado) o ESP_ERR_TIMEOUT.
 */
esp_err_t gui_display_load_state(void);

/**
 * @brief Guarda saldo y stock actuales en la NVS.
 *
 * Se llama internamente al acreditar monedas y al cobrar cada unidad
 * despachada. Es seguro desde cualquier tarea (nunca desde una ISR).
 *
 * @return ESP_OK o el error de NVS.
 */
esp_err_t gui_display_save_state(void);

/**
 * @brief Marca toda la pantalla para redibujarse en el proximo ciclo de GUI.
 *
 * Es seguro llamarla desde cualquier tarea: no toca LVGL.
 */
void gui_display_render(void);

/**
 * @brief Marca el grid completo como sucio (redibujo diferido).
 */
void gui_display_render_grid(void);

/**
 * @brief Marca una sola celda del grid como sucia (redibujo diferido).
 *
 * @param index Producto 0..5.
 */
void gui_display_render_product(uint8_t index);

/**
 * @brief Marca el panel inferior (saldo + confirmar) como sucio.
 */
void gui_display_render_bottom_panel(void);

/**
 * @brief Ajusta la cantidad seleccionada respetando 0 <= qty <= stock.
 *
 * @param index Producto 0..5.
 * @param delta -1 para [-], +1 para [+].
 * @return ESP_OK o ESP_ERR_INVALID_ARG.
 */
esp_err_t gui_display_adjust_quantity(uint8_t index, int delta);

/**
 * @brief Fija directamente la cantidad seleccionada (se recorta al stock).
 */
esp_err_t gui_display_set_quantity(uint8_t index, uint8_t quantity);

/**
 * @brief Cantidad seleccionada de un producto (0 si el indice es invalido).
 */
uint8_t gui_display_get_quantity(uint8_t index);

/**
 * @brief Actualiza el stock de un producto y recorta la cantidad si hace falta.
 */
esp_err_t gui_display_set_product_stock(uint8_t index, uint8_t stock);

/**
 * @brief Actualiza el precio unitario de un producto (en centavos).
 */
esp_err_t gui_display_set_product_price(uint8_t index, uint32_t price_cents);

/**
 * @brief Devuelve el modelo de un producto (NULL si el indice es invalido).
 *
 * @warning El puntero apunta al estado interno. Solo debe usarse desde la tarea
 *          de GUI o con el estado bloqueado; no conservar el puntero.
 */
const gui_product_t *gui_display_get_product(uint8_t index);

/**
 * @brief Saldo acumulado en centavos.
 */
int64_t gui_display_get_balance_cents(void);

/**
 * @brief Fija el saldo (nunca negativo) y refresca el panel inferior.
 */
void gui_display_set_balance_cents(int64_t cents);

/**
 * @brief Suma una moneda al saldo y refresca el panel inferior.
 *
 * Seguro desde task_monedero.
 */
void gui_display_add_balance_cents(uint32_t cents);

/**
 * @brief Total de la seleccion actual en centavos.
 */
int64_t gui_display_get_total_cents(void);

/**
 * @brief Solicita mostrar el modal "Confirmar compra" con el resumen actual.
 *
 * Valida que haya seleccion, que los precios esten configurados y que el saldo
 * alcance. El modal se construye en task_gui() en el siguiente ciclo.
 */
void gui_display_show_confirm_modal(void);

/**
 * @brief Solicita ocultar el modal de confirmacion.
 */
void gui_display_hide_confirm_modal(void);

/**
 * @brief Indica si el modal esta abierto (bloquea el grid mientras tanto).
 */
bool gui_display_is_modal_open(void);

/**
 * @brief Accion del boton [Si] del modal.
 *
 * Valida saldo/stock y ENCOLA una solicitud por producto en task_despacho.
 * NO descuenta saldo ni stock: eso ocurre cuando motor_task confirma cada
 * unidad con MOTOR_EVENT_UNIT_DONE.
 *
 * @return ESP_OK si el pedido fue encolado; error de validacion o de cola.
 */
esp_err_t gui_display_confirm_purchase(void);

/**
 * @brief Callback de unidad despachada con exito (desde motor_task).
 *
 * Resta el precio al saldo y reduce el stock en 1. Es el UNICO punto donde se
 * descuentan saldo y stock por una compra.
 *
 * @param channel Canal 0..5 que entrego una unidad.
 */
void gui_display_on_unit_dispatched(uint8_t channel);

/**
 * @brief Callback de fallo de despacho (desde motor_task).
 *
 * Cancela las unidades no entregadas del pedido: no se cobran ni reducen stock.
 * Las unidades ya entregadas si quedaron cobradas.
 *
 * @param channel Canal que fallo.
 * @param error   Codigo de error reportado por el motor.
 */
void gui_display_on_dispatch_failed(uint8_t channel, esp_err_t error);

/**
 * @brief Callback de cola vacia / fin de pedido (desde motor_task).
 */
void gui_display_on_dispatch_idle(void);

/**
 * @brief Indica si hay un pedido en curso (despachando).
 */
bool gui_display_is_order_active(void);

/**
 * @brief Muestra un aviso breve (toast) sobre la GUI.
 *
 * Seguro desde cualquier tarea: guarda el mensaje y despierta a task_gui.
 */
void gui_display_show_message(const char *message);

/**
 * @brief Tarea FreeRTOS de la GUI.
 *
 * Ejecuta lv_timer_handler(), inyecta el tick de LVGL y refresca los widgets
 * cuando el estado cambia. Prioridad sugerida: 5.
 */
void gui_task(void *arg);

#ifdef __cplusplus
}
#endif
