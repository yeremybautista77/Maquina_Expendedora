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
#include <string.h>

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
#include "nvs.h"
#include "nvs_flash.h"

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

#if BOARD_COIN_ACCEPTOR_ENABLED
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
#endif /* BOARD_COIN_ACCEPTOR_ENABLED */

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
        gui_display_on_unit_dispatched(event->channel, event->admin_test);
        break;

    case MOTOR_EVENT_REQUEST_FAILED:
        gui_display_on_dispatch_failed(event->channel, event->error,
                                       event->admin_test);
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

#if BOARD_COIN_ACCEPTOR_ENABLED
    ok = xTaskCreate(coin_acceptor_task, "task_monedero", 4096, NULL, 10, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear task_monedero");
    }
#else
    ESP_LOGW(TAG, "Monedero deshabilitado por configuracion (saldo solo por ADM)");
#endif

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

/* --- Prueba de banco (temporal) ------------------------------------------- */

#if BRINGUP_TEST_MOTOR1
static void bringup_scan_i2c(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Bring-up I2C: escaneando 0x20..0x27");
    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "Bring-up I2C: dispositivo responde en 0x%02X",
                     (unsigned)addr);
        }
    }
}

static void bringup_motor1(void)
{
    ESP_LOGW(TAG, "Bring-up M1: activando rele %d ms", MOTOR_TIMED_PULSE_MS);
    esp_err_t err = motor_control_set_relay(0, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bring-up M1: fallo activando rele: %s",
                 esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(MOTOR_TIMED_PULSE_MS));
    err = motor_control_set_relay(0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bring-up M1: fallo apagando rele: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "Bring-up M1: rele apagado");
    }
}

static void bringup_task(void *arg)
{
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)arg;

    /* Deja que la GUI arranque y muestre la pantalla normal. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    bringup_scan_i2c(bus);

    if (xSemaphoreTake(g_dispatch_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Bring-up: mutex de despacho ocupado");
        vTaskDelete(NULL);
        return;
    }

    bringup_motor1();
    xSemaphoreGive(g_dispatch_mutex);

    vTaskDelete(NULL);
}
#endif /* BRINGUP_TEST_MOTOR1 */

/* --- Mantenimiento unico de NVS ------------------------------------------- */

/*
 * Reinicio de stock solicitado el 2026-11-04. Se hace UNA sola vez por placa:
 * se escribe el blob de stock a 10 y se marca la clave de control. En arranques
 * posteriores no se vuelve a tocar, de modo que el stock real no se pierde.
 *
 * Se ejecuta ANTES de gui_display_init() para que la GUI cargue el valor nuevo.
 * No toca saldo ni precios.
 */
#define MAINT_NVS_NAMESPACE     "vending"
#define MAINT_NVS_KEY_STOCK     "stock"
#define MAINT_NVS_KEY_DONE      "stock_reset_v1"
#define MAINT_STOCK_VALUE       10

static void maintenance_reset_stock_once(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MAINT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reset de stock omitido: NVS no abre (%s)",
                 esp_err_to_name(err));
        return;
    }

    uint8_t done = 0;
    if (nvs_get_u8(handle, MAINT_NVS_KEY_DONE, &done) == ESP_OK && done != 0) {
        nvs_close(handle);
        return;
    }

    uint8_t stock[BOARD_CHANNEL_COUNT];
    memset(stock, MAINT_STOCK_VALUE, sizeof(stock));

    err = nvs_set_blob(handle, MAINT_NVS_KEY_STOCK, stock, sizeof(stock));
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, MAINT_NVS_KEY_DONE, 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Stock reiniciado a %d por producto (una sola vez)",
                 MAINT_STOCK_VALUE);
    } else {
        ESP_LOGE(TAG, "No se pudo reiniciar el stock: %s",
                 esp_err_to_name(err));
    }
    nvs_close(handle);
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
     * 0. NVS: saldo y stock sobreviven a los cortes de energia.
     *    Debe inicializarse antes de gui_display_init(), que carga el estado.
     */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS incompatible (%s); se borra y se reinicializa",
                 esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS no disponible (%s); saldo y stock no se guardaran",
                 esp_err_to_name(nvs_err));
    } else {
        maintenance_reset_stock_once();
    }

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
     * 2. Bus I2C + PCF8574 de relees (0x20) + PCF8574 de sensores (0x21).
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
     *
     * Con BOARD_COIN_ACCEPTOR_ENABLED = 1 se inicializa el CH-926 y el saldo se
     * acredita por monedas (ademas del panel ADM). Con 0 se omite por completo
     * para no leer una linea flotante.
     */
#if !BOARD_COIN_ACCEPTOR_ENABLED
    ESP_LOGW(TAG, "Monedero omitido: saldo administrado desde el panel ADM");
#else
    if (coin_acceptor_init() == ESP_OK) {
        coin_acceptor_set_callback(on_coin_inserted, NULL);
    } else {
        ESP_LOGE(TAG, "Monedero no inicializado");
    }
#endif

    /*
     * 4. Activos graficos de la MicroSD y GUI.
     *    La SD se monta primero para que el motor grafico pueda cargar las
     *    imagenes de los productos al inicializarse.
     */
    esp_err_t sd_err = gui_display_mount_sd();
    if (sd_err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "MicroSD desactivada; las imagenes van en flash");
    } else if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "MicroSD no disponible (%s); se continua sin ella",
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

#if BRINGUP_TEST_MOTOR1
    BaseType_t bringup_ok = xTaskCreate(bringup_task, "task_bringup", 4096,
                                        i2c_bus, 3, NULL);
    if (bringup_ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea de bring-up");
    }
#endif

    ESP_LOGI(TAG, "Arranque completado. Sistema en espera de monedas.");
}
