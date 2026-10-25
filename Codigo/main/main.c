/**
 * @file main.c
 * @brief Punto de entrada y orquestacion de la maquina expendedora de 6 canales.
 *
 * Arquitectura (documento_de_arquitectura_tecnico.md):
 *   - ESP-IDF + FreeRTOS, C nativo.
 *   - Monedero por ISR en IO35 (tarea de alta prioridad).
 *   - Despacho I2C de motores con Mutex (un motor a la vez).
 *   - GUI en pantalla TFT de 4.0" con activos leidos de la MicroSD.
 *
 * Tareas creadas:
 *   task_monedero  (prio 10) -> decodifica rafagas de pulsos.
 *   task_despacho  (prio  8) -> consume la cola y acciona los motores.
 *   task_gui       (prio  5) -> renderizado y tactil.
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_config.h"
#include "coin_acceptor.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gui_display.h"
#include "motor_control.h"

static const char *TAG = "app_main";

/* --- Recursos compartidos ------------------------------------------------- */

/**
 * Mutex global de despacho (Seccion 6).
 *
 * Se define aqui y se declara extern en motor_control.h. Se crea antes de
 * lanzar las tareas. Garantiza que solo un motor reciba energia a la vez para
 * proteger la fuente conmutada de picos de corriente.
 */
SemaphoreHandle_t g_dispatch_mutex = NULL;

/* --- Callbacks ------------------------------------------------------------ */

/**
 * @brief Se ejecuta al cerrar cada rafaga valida del monedero.
 */
static void on_coin_inserted(uint8_t pulses, uint32_t cents, void *ctx)
{
    (void)ctx;

    if (cents == 0) {
        ESP_LOGW(TAG, "Rafaga ignorada: %u pulso(s) no corresponden a una moneda",
                 (unsigned)pulses);
        gui_display_show_message("Moneda no reconocida");
        return;
    }

    gui_display_add_balance_cents(cents);

    const int64_t balance = gui_display_get_balance_cents();
    ESP_LOGI(TAG, "Moneda RD$ %" PRIu32 ".%02" PRIu32 " | Saldo: RD$ %lld.%02lld",
             cents / 100, cents % 100,
             (long long)(balance / 100), (long long)(balance % 100));
}

/**
 * @brief Adaptador entre motor_control y la GUI.
 *
 * Se ejecuta en el contexto de task_despacho. No llama a LVGL: solo actualiza
 * el estado protegido de la GUI y despierta a task_gui.
 */
static void on_motor_event(const motor_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case MOTOR_EVENT_UNIT_DONE:
        gui_display_on_unit_dispatched(event->channel);
        break;

    case MOTOR_EVENT_REQUEST_FAILED:
        gui_display_on_dispatch_failed(event->channel, event->error);
        break;

    case MOTOR_EVENT_IDLE:
        gui_display_on_dispatch_idle();
        break;

    default:
        break;
    }
}

/* --- Inicializacion del bus I2C ------------------------------------------- */

/**
 * @brief Crea el bus maestro I2C compartido por los dos PCF8574.
 */
static esp_err_t init_i2c_bus(i2c_master_bus_handle_t *out_bus)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, out_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el bus I2C: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bus I2C%d listo (SDA=IO%d, SCL=IO%d, %d Hz)",
             (int)BOARD_I2C_PORT,
             (int)BOARD_I2C_SDA_GPIO,
             (int)BOARD_I2C_SCL_GPIO,
             BOARD_I2C_CLOCK_HZ);
    return ESP_OK;
}

/* --- Creacion de tareas --------------------------------------------------- */

static void start_tasks(void)
{
    BaseType_t ok;

    ok = xTaskCreate(coin_acceptor_task, "task_monedero", 4096, NULL, 10, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear task_monedero");
    }

    ok = xTaskCreate(motor_task, "task_despacho", 4096, NULL, 8, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear task_despacho");
    }

    /*
     * LVGL, el decodificador BMP y los callbacks de eventos necesitan una pila
     * amplia. 16 KB deja margen para el peor caso de dibujo del modal.
     */
    ok = xTaskCreate(gui_task, "task_gui", 16384, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear task_gui");
    }
}

/* --- Punto de entrada ----------------------------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Maquina Expendedora - 6 canales (ESP32-32E 4.0\")");
    ESP_LOGI(TAG, " ESP-IDF %s | heap libre: %" PRIu32 " bytes",
             esp_get_idf_version(), (uint32_t)esp_get_free_heap_size());
    ESP_LOGI(TAG, "==================================================");

    /*
     * 1. Mutex global ANTES de crear cualquier tarea que pueda despachar.
     */
    g_dispatch_mutex = xSemaphoreCreateMutex();
    if (g_dispatch_mutex == NULL) {
        ESP_LOGE(TAG, "FATAL: no se pudo crear el Mutex de despacho");
        return;
    }
    ESP_LOGI(TAG, "Mutex de despacho creado (un motor a la vez)");

    /*
     * 2. Bus I2C + expansores PCF8574 (relees 0x20 y sensores 0x21).
     */
    i2c_master_bus_handle_t i2c_bus = NULL;
    if (init_i2c_bus(&i2c_bus) == ESP_OK) {
        esp_err_t err = motor_control_init(i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Control de motores no inicializado: %s",
                     esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Sin bus I2C: el despacho de motores no estara disponible");
    }

    /*
     * 3. Monedero: GPIO35 + ISR + cola de pulsos.
     */
    if (coin_acceptor_init() == ESP_OK) {
        coin_acceptor_set_callback(on_coin_inserted, NULL);
    } else {
        ESP_LOGE(TAG, "Monedero no inicializado");
    }

    /*
     * 4. Activos graficos de la MicroSD y GUI.
     *    La SD se monta primero para que el motor grafico pueda cargar las
     *    imagenes de los productos al inicializarse.
     */
    esp_err_t sd_err = gui_display_mount_sd();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "MicroSD no disponible (%s); las imagenes no se cargaran",
                 esp_err_to_name(sd_err));
    }

    esp_err_t gui_err = gui_display_init();
    if (gui_err != ESP_OK) {
        ESP_LOGW(TAG, "GUI no disponible (%s); se continua el arranque",
                 esp_err_to_name(gui_err));
    }

    /*
     * 5. Conectar los eventos de despacho con la GUI.
     *
     * El callback se registra antes de crear task_despacho. La GUI descuenta
     * saldo/stock solo cuando recibe MOTOR_EVENT_UNIT_DONE (unidad entregada).
     */
    esp_err_t cb_err = motor_control_set_event_callback(on_motor_event, NULL);
    if (cb_err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar el callback de despacho: %s",
                 esp_err_to_name(cb_err));
    }

    /*
     * 6. Tareas concurrentes.
     */
    start_tasks();

    ESP_LOGI(TAG, "Arranque completado. Sistema en espera de monedas.");
}
