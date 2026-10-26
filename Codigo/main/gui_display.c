/**
 * @file gui_display.c
 * @brief Interfaz grafica de la maquina expendedora: ST7796 + XPT2046 + LVGL.
 *
 * Implementa la fase P0 del documento de arquitectura:
 *   - Panel ST7796 320x480 por SPI2 (HSPI) con esp_lcd.
 *   - Tactil resistivo XPT2046 compartiendo el bus SPI2.
 *   - Retroiluminacion por LEDC.
 *   - Grid de 2 columnas x 3 filas con imagenes BMP de la MicroSD.
 *   - Panel inferior con saldo y boton de confirmacion.
 *   - Modal de resumen [Si] / [No] y toast de avisos.
 *
 * Regla de negocio critica (riesgo #3):
 *   gui_display_confirm_purchase() SOLO encola el pedido. El saldo y el stock
 *   se descuentan UNICAMENTE en gui_display_on_unit_dispatched(), que motor_task
 *   invoca cuando una unidad completa su vuelta (sensor LOW). Si el motor falla,
 *   las unidades no entregadas no se cobran ni reducen stock.
 *
 * Concurrencia:
 *   - s_state_mutex protege saldo, stock, cantidades, pedido y modal.
 *   - task_monedero y motor_task solo tocan estado y despiertan a task_gui.
 *   - LVGL se usa exclusivamente desde gui_task() y desde gui_display_init()
 *     (antes de crear la tarea).
 *
 * SPDX-License-Identifier: MIT
 */

#include "gui_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "motor_control.h"
#include "nvs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "gui_display";

_Static_assert(GUI_PRODUCT_COUNT == BOARD_CHANNEL_COUNT,
               "El grid debe tener una celda por canal fisico");

/* --- Parametros de la interfaz ------------------------------------------- */

/** Lineas del buffer de dibujo de LVGL (320 x 40 x 2 B = 25.6 KB por buffer). */
#define GUI_DRAW_BUF_LINES          40

/** Altura del panel inferior (saldo + total + confirmar). 80 px = 16.7 %. */
#define GUI_BOTTOM_PANEL_HEIGHT     80

/** Tamano del recuadro de imagen de cada celda. */
#define GUI_CELL_IMAGE_SIZE         52

/** Tamano de los botones tactiles [-] y [+]. */
#define GUI_CELL_BUTTON_SIZE        34

/** Altura de la fila de controles de cantidad. */
#define GUI_CELL_CONTROLS_HEIGHT    34

/**
 * Ancho de cada celda como porcentaje del area util del contenedor.
 * 48 % x 2 celdas + separacion = ~304 px de 312 px utiles: fuerza el salto de
 * linea despues del segundo producto (grid 2x3 real con ROW_WRAP).
 */
#define GUI_CELL_WIDTH_PCT          48

/** Altura fija de cada celda: 3 x 127 + 2 x 4 (gap) + 8 (padding) = 397 px. */
#define GUI_CELL_HEIGHT             127

/** Stock inicial de un producto cuando la NVS esta vacia. */
#define GUI_DEFAULT_STOCK           10

/** Duracion del toast en milisegundos. */
#define GUI_TOAST_DURATION_MS       2500

/** Longitud maxima del mensaje de un toast. */
#define GUI_TOAST_MAX_LEN           72

/** Timeout para tomar el Mutex de estado (no debe bloquear a motor_task). */
#define GUI_STATE_LOCK_TIMEOUT_MS   100

/**
 * Duracion de la pulsacion larga que abre el modo administracion.
 * Se configura en el driver de entrada (LVGL 8.4).
 */
#define GUI_ADMIN_LONG_PRESS_MS     3000

/** Lado del boton invisible de la esquina superior izquierda. */
#define GUI_ADMIN_HOTSPOT_SIZE      40

/* --- Persistencia (NVS) --------------------------------------------------- */

/** Espacio de nombres NVS propio del inventario. */
#define GUI_NVS_NAMESPACE           "vending"

/** Clave del saldo (int64 en centavos). */
#define GUI_NVS_KEY_BALANCE         "balance"

/** Clave del stock (blob de GUI_PRODUCT_COUNT bytes). */
#define GUI_NVS_KEY_STOCK           "stock"

/** Clave de los precios (blob de GUI_PRODUCT_COUNT uint32_t = 24 B). */
#define GUI_NVS_KEY_PRICES          "prices"

/* --- Precios -------------------------------------------------------------- */

/** Paso de ajuste de precio desde el modo administracion (RD$ 5). */
#define GUI_PRICE_STEP_CENTS        500

/** Precio minimo permitido (RD$ 5.00). */
#define GUI_PRICE_MIN_CENTS         500

/** Precio maximo permitido (RD$ 1000.00). */
#define GUI_PRICE_MAX_CENTS         100000

/* --- Modelo de productos -------------------------------------------------- */

/*
 * Los precios ya NO son una tabla constante: viven en s_products[i].price_cents
 * (estado global protegido por s_state_mutex) y se persisten en la NVS con la
 * clave "prices". Los valores por defecto solo se aplican al cargar si la clave
 * no existe o el blob es invalido.
 */

static gui_product_t s_products[GUI_PRODUCT_COUNT] = {
    { .name = "Producto 1", .image_path = "/sdcard/productos/p1.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
    { .name = "Producto 2", .image_path = "/sdcard/productos/p2.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
    { .name = "Producto 3", .image_path = "/sdcard/productos/p3.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
    { .name = "Producto 4", .image_path = "/sdcard/productos/p4.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
    { .name = "Producto 5", .image_path = "/sdcard/productos/p5.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
    { .name = "Producto 6", .image_path = "/sdcard/productos/p6.bmp",
      .price_cents = GUI_PRICE_UNSET_CENTS, .stock = GUI_DEFAULT_STOCK,
      .quantity = 0 },
};

/* --- Estado compartido (protegido por s_state_mutex) ---------------------- */

static SemaphoreHandle_t s_state_mutex = NULL;
static int64_t s_balance_cents = 0;
static bool s_modal_open = false;
static bool s_ui_dirty = true;

static char s_toast_msg[GUI_TOAST_MAX_LEN];
static bool s_toast_pending = false;

/** Indica si el panel y LVGL quedaron operativos. */
static bool s_display_ready = false;
static bool s_lvgl_ready = false;

/** Handle de task_gui para despertarla cuando cambia el estado. */
static TaskHandle_t s_gui_task_handle = NULL;

/* --- Pedido en curso ------------------------------------------------------ */

/**
 * Un pedido se cobra unidad por unidad. `reserved[ch]` son las unidades de ese
 * canal que aun no han sido confirmadas por motor_task.
 */
typedef struct {
    bool active;                            /**< Hay un pedido despachandose. */
    bool failed;                            /**< Un motor fallo. */
    uint8_t reserved[GUI_PRODUCT_COUNT];    /**< Unidades pendientes de cobro. */
    uint8_t requested[GUI_PRODUCT_COUNT];   /**< Unidades solicitadas. */
    uint32_t total_units;                   /**< Unidades del pedido. */
    uint32_t pending_units;                 /**< Unidades aun no entregadas. */
    uint32_t delivered_units;               /**< Unidades ya entregadas. */
    int64_t total_cents;                    /**< Importe del pedido. */
    int64_t charged_cents;                  /**< Importe ya cobrado. */
} gui_order_t;

static gui_order_t s_order;

/* --- Instantanea del modal ------------------------------------------------ */

typedef struct {
    uint8_t channel;
    uint8_t quantity;
    uint32_t unit_price_cents;
    int64_t subtotal_cents;
    char name[24];
} gui_modal_item_t;

static gui_modal_item_t s_modal_items[GUI_PRODUCT_COUNT];
static uint8_t s_modal_item_count = 0;
static int64_t s_modal_total_cents = 0;
static int64_t s_modal_balance_cents = 0;

/* --- Hardware de pantalla ------------------------------------------------- */

static bool s_tft_bus_owned = false;
static esp_lcd_panel_io_handle_t s_tft_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;

/* --- LVGL ----------------------------------------------------------------- */

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_disp_t *s_disp = NULL;
static lv_indev_drv_t s_indev_drv;
static void *s_draw_buf1 = NULL;
static void *s_draw_buf2 = NULL;

/* --- Widgets -------------------------------------------------------------- */

typedef struct {
    lv_obj_t *cell;
    lv_obj_t *img;
    lv_obj_t *img_box;
    lv_obj_t *img_placeholder;
    lv_obj_t *header_label;     /**< Nombre + precio ("Producto 1\nRD$ 40.00"). */
    lv_obj_t *qty_label;        /**< Texto "Cant: N". */
    lv_obj_t *minus_btn;
    lv_obj_t *plus_btn;
    /**
     * Ruta en formato LVGL ("S:/productos/pN.bmp").
     *
     * lv_img_set_src() NO copia la cadena: guarda el puntero. Por eso debe
     * vivir tanto como la imagen, no en la pila de create_cell().
     */
    char image_src[96];
} gui_cell_widgets_t;

static lv_obj_t *s_grid = NULL;
static gui_cell_widgets_t s_cells[GUI_PRODUCT_COUNT];

static lv_obj_t *s_bottom_panel = NULL;
static lv_obj_t *s_balance_label = NULL;
static lv_obj_t *s_total_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_confirm_btn = NULL;

static lv_obj_t *s_modal_overlay = NULL;
static lv_obj_t *s_modal_card = NULL;

static lv_obj_t *s_toast_box = NULL;
static lv_obj_t *s_toast_label = NULL;
static lv_timer_t *s_toast_timer = NULL;

/*
 * Modo administracion: boton invisible en la esquina superior izquierda que se
 * activa con una pulsacion larga de 3 s. El overlay se crea y se destruye
 * siempre desde task_gui (callbacks de LVGL).
 */
static lv_obj_t *s_admin_hotspot = NULL;
static lv_obj_t *s_admin_overlay = NULL;
static bool s_admin_open = false;

/*
 * Widgets y borrador de precios del panel de administracion. El borrador se
 * aplica al estado global y a la NVS solo cuando el operador pulsa
 * "Guardar Precios".
 */
static lv_obj_t *s_admin_stock_label = NULL;
static lv_obj_t *s_admin_status_label = NULL;
static lv_obj_t *s_admin_price_labels[GUI_PRODUCT_COUNT];
static lv_obj_t *s_admin_test_btns[GUI_PRODUCT_COUNT];
static uint32_t s_admin_price_draft[GUI_PRODUCT_COUNT];

/* --- Estado de la MicroSD ------------------------------------------------- */

static sdmmc_card_t *s_sd_card = NULL;
static bool s_sd_bus_owned = false;

/* ==========================================================================
 *  Utilidades de estado
 * ========================================================================== */

static bool state_lock(uint32_t timeout_ms)
{
    if (s_state_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void state_unlock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
}

/** Despierta a task_gui (seguro desde cualquier tarea). */
static void ui_notify(void)
{
    const TaskHandle_t task = s_gui_task_handle;
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

/** Guarda un toast. Debe llamarse con el Mutex de estado tomado. */
static void set_toast_locked(const char *message)
{
    if (message == NULL) {
        return;
    }
    strncpy(s_toast_msg, message, sizeof(s_toast_msg) - 1);
    s_toast_msg[sizeof(s_toast_msg) - 1] = '\0';
    s_toast_pending = true;
}

/** Finaliza el pedido en curso. Debe llamarse con el Mutex tomado. */
static void order_finalize_locked(void)
{
    if (!s_order.active) {
        return;
    }

    s_order.active = false;
    s_order.pending_units = 0;

    if (s_order.failed) {
        if (s_order.delivered_units == 0) {
            set_toast_locked("Pedido cancelado. No se cobro nada.");
        } else {
            snprintf(s_toast_msg, sizeof(s_toast_msg),
                     "Pedido incompleto: %u entregada(s). Resto no cobrado.",
                     (unsigned)s_order.delivered_units);
            s_toast_pending = true;
        }
    } else {
        snprintf(s_toast_msg, sizeof(s_toast_msg),
                 "Compra completada: %u unidad(es).",
                 (unsigned)s_order.delivered_units);
        s_toast_pending = true;
    }

    s_ui_dirty = true;
}

/* ==========================================================================
 *  Persistencia en NVS (saldo + stock de los 6 productos)
 * ========================================================================== */

/**
 * @brief Carga saldo, stock y precios desde NVS.
 *
 * Debe llamarse con el Mutex de estado tomado (o antes de crear task_gui).
 * Los precios viven en s_products[i].price_cents y se persisten en la clave
 * "prices"; si no existe o es invalida se usan los valores por defecto.
 *
 * Si la NVS esta vacia, no esta inicializada o el blob es invalido, se aplican
 * los valores por defecto (saldo 0, 10 unidades y RD$ 40..90 por producto).
 */
static void storage_load_locked(void)
{
    /* Valores de fabrica; solo se usan si la NVS no tiene precios validos. */
    static const uint32_t default_prices[GUI_PRODUCT_COUNT] = {
        4000,   /* Producto 1: RD$ 40.00 */
        5000,   /* Producto 2: RD$ 50.00 */
        6000,   /* Producto 3: RD$ 60.00 */
        7000,   /* Producto 4: RD$ 70.00 */
        8000,   /* Producto 5: RD$ 80.00 */
        9000,   /* Producto 6: RD$ 90.00 */
    };

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        s_products[i].price_cents = default_prices[i];
        s_products[i].stock = GUI_DEFAULT_STOCK;
        s_products[i].quantity = 0;
    }
    s_balance_cents = 0;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(GUI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS vacia: saldo RD$ 0.00, stock %u y precios por defecto",
                 (unsigned)GUI_DEFAULT_STOCK);
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS no disponible al cargar (%s); valores por defecto",
                 esp_err_to_name(err));
        return;
    }

    int64_t balance = 0;
    if (nvs_get_i64(handle, GUI_NVS_KEY_BALANCE, &balance) == ESP_OK &&
        balance >= 0) {
        s_balance_cents = balance;
    }

    uint8_t stock[GUI_PRODUCT_COUNT];
    size_t length = sizeof(stock);
    if (nvs_get_blob(handle, GUI_NVS_KEY_STOCK, stock, &length) == ESP_OK &&
        length == sizeof(stock)) {
        for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
            s_products[i].stock = stock[i];
        }
    }

    uint32_t prices[GUI_PRODUCT_COUNT];
    length = sizeof(prices);
    if (nvs_get_blob(handle, GUI_NVS_KEY_PRICES, prices, &length) == ESP_OK &&
        length == sizeof(prices)) {
        for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
            if (prices[i] >= GUI_PRICE_MIN_CENTS &&
                prices[i] <= GUI_PRICE_MAX_CENTS) {
                s_products[i].price_cents = prices[i];
            }
        }
    }

    nvs_close(handle);

    ESP_LOGI(TAG,
             "NVS cargada: saldo RD$ %lld.%02lld | stock %u/%u/%u/%u/%u/%u",
             (long long)(s_balance_cents / 100),
             (long long)(s_balance_cents % 100),
             (unsigned)s_products[0].stock, (unsigned)s_products[1].stock,
             (unsigned)s_products[2].stock, (unsigned)s_products[3].stock,
             (unsigned)s_products[4].stock, (unsigned)s_products[5].stock);
    ESP_LOGI(TAG,
             "Precios: RD$ %u.%02u / %u.%02u / %u.%02u / %u.%02u / %u.%02u / %u.%02u",
             (unsigned)(s_products[0].price_cents / 100),
             (unsigned)(s_products[0].price_cents % 100),
             (unsigned)(s_products[1].price_cents / 100),
             (unsigned)(s_products[1].price_cents % 100),
             (unsigned)(s_products[2].price_cents / 100),
             (unsigned)(s_products[2].price_cents % 100),
             (unsigned)(s_products[3].price_cents / 100),
             (unsigned)(s_products[3].price_cents % 100),
             (unsigned)(s_products[4].price_cents / 100),
             (unsigned)(s_products[4].price_cents % 100),
             (unsigned)(s_products[5].price_cents / 100),
             (unsigned)(s_products[5].price_cents % 100));
}

/**
 * @brief Guarda saldo, stock y precios en NVS.
 *
 * Debe llamarse con el Mutex de estado tomado para obtener una instantanea
 * coherente. La escritura real la hace la tarea que invoca (monedero,
 * task_despacho o task_gui), nunca una ISR.
 */
static esp_err_t storage_save_locked(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(GUI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir la NVS para guardar: %s",
                 esp_err_to_name(err));
        return err;
    }

    uint8_t stock[GUI_PRODUCT_COUNT];
    uint32_t prices[GUI_PRODUCT_COUNT];
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        stock[i] = s_products[i].stock;
        prices[i] = s_products[i].price_cents;
    }

    err = nvs_set_i64(handle, GUI_NVS_KEY_BALANCE, s_balance_cents);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, GUI_NVS_KEY_STOCK, stock, sizeof(stock));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, GUI_NVS_KEY_PRICES, prices, sizeof(prices));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar saldo/stock/precios en NVS: %s",
                 esp_err_to_name(err));
    }
    return err;
}

/* ==========================================================================
 *  Utilidades de archivos / rutas
 * ========================================================================== */

static bool sd_file_exists(const char *path)
{
    if (path == NULL) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

/**
 * Convierte "/sdcard/productos/p1.bmp" en "S:/productos/p1.bmp" para LVGL.
 *
 * El driver stdio de LVGL concatena CONFIG_LV_FS_STDIO_PATH ("/sdcard") con la
 * ruta que recibe DESPUES de la letra de unidad, sin insertar separadores. Por
 * eso la barra inicial es obligatoria: "/sdcard" + "/productos/p1.bmp".
 */
static void to_lvgl_path(const char *sd_path, char *out, size_t out_len)
{
    if (sd_path == NULL || out == NULL || out_len == 0) {
        return;
    }

    const size_t mount_len = strlen(BOARD_SD_MOUNT_POINT);
    const char *relative = sd_path;
    if (strncmp(sd_path, BOARD_SD_MOUNT_POINT, mount_len) == 0) {
        relative = sd_path + mount_len;
    }
    while (*relative == '/') {
        relative++;
    }

    snprintf(out, out_len, "%c:/%s", (char)LV_FS_STDIO_LETTER, relative);
}

/* ==========================================================================
 *  LVGL: flush, tick y tactil
 * ========================================================================== */

static void lv_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                        lv_color_t *color_map)
{
    if (s_panel == NULL) {
        lv_disp_flush_ready(drv);
        return;
    }

    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap fallo: %s", esp_err_to_name(err));
        lv_disp_flush_ready(drv);
    }
}

/**
 * @brief Callback de fin de transferencia SPI del panel.
 *
 * Se ejecuta en el contexto de la ISR del bus SPI: solo libera el buffer de
 * LVGL. No imprimir ni bloquear aqui.
 */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
    if (drv != NULL) {
        lv_disp_flush_ready(drv);
    }
    return false;
}

static void lv_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    if (s_touch == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /*
     * esp_lcd_touch_get_data() sustituye a la API obsoleta
     * esp_lcd_touch_get_coordinates() (eliminada en esp_lcd_touch 2.0).
     * La pantalla es resistiva: se pide un unico punto.
     */
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t points[1] = {0};
    uint8_t point_count = 0;
    const esp_err_t err =
        esp_lcd_touch_get_data(s_touch, points, &point_count, 1);

    if (err == ESP_OK && point_count > 0) {
        uint16_t x = points[0].x;
        uint16_t y = points[0].y;

        if (x >= BOARD_TFT_H_RES) {
            x = BOARD_TFT_H_RES - 1;
        }
        if (y >= BOARD_TFT_V_RES) {
            y = BOARD_TFT_V_RES - 1;
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ==========================================================================
 *  Construccion de la interfaz (solo desde gui_task / antes de crearla)
 * ========================================================================== */

static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast_box != NULL) {
        lv_obj_add_flag(s_toast_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast_timer != NULL) {
        lv_timer_pause(s_toast_timer);
    }
}

static void show_toast(const char *message)
{
    if (s_toast_box == NULL || s_toast_label == NULL || message == NULL) {
        return;
    }

    lv_label_set_text(s_toast_label, message);
    lv_obj_clear_flag(s_toast_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast_box);

    if (s_toast_timer != NULL) {
        lv_timer_reset(s_toast_timer);
        lv_timer_resume(s_toast_timer);
    }
}

static void on_minus_clicked(lv_event_t *event)
{
    const uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    gui_display_adjust_quantity(index, -1);
}

static void on_plus_clicked(lv_event_t *event)
{
    const uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    gui_display_adjust_quantity(index, +1);
}

static void on_confirm_clicked(lv_event_t *event)
{
    (void)event;
    gui_display_show_confirm_modal();
}

static void on_modal_yes_clicked(lv_event_t *event)
{
    (void)event;
    gui_display_confirm_purchase();
}

static void on_modal_no_clicked(lv_event_t *event)
{
    (void)event;
    gui_display_hide_confirm_modal();
}

static void create_cell(uint8_t index)
{
    gui_cell_widgets_t *cell = &s_cells[index];
    const gui_product_t *product = &s_products[index];

    cell->cell = lv_obj_create(s_grid);
    /*
     * Flex ROW_WRAP: el ancho del 48 % obliga a que solo entren dos celdas por
     * fila, de modo que el sexto producto cierra el grid 2x3.
     */
    lv_obj_set_size(cell->cell, LV_PCT(GUI_CELL_WIDTH_PCT), GUI_CELL_HEIGHT);
    lv_obj_set_style_bg_color(cell->cell, lv_color_hex(0x1E2A38), 0);
    lv_obj_set_style_bg_opa(cell->cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cell->cell, lv_color_hex(0x3A5068), 0);
    lv_obj_set_style_border_width(cell->cell, 1, 0);
    lv_obj_set_style_radius(cell->cell, 8, 0);
    lv_obj_set_style_pad_all(cell->cell, 3, 0);
    lv_obj_set_style_pad_row(cell->cell, 1, 0);
    lv_obj_clear_flag(cell->cell, LV_OBJ_FLAG_SCROLLABLE);

    /* Columna centrada: cabecera, imagen y controles de cantidad. */
    lv_obj_set_layout(cell->cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell->cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell->cell, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Cabecera: nombre + precio en un solo label de dos lineas. */
    cell->header_label = lv_label_create(cell->cell);
    lv_label_set_text_fmt(cell->header_label, "%s\nRD$ %u.%02u",
                          product->name,
                          (unsigned)(product->price_cents / 100),
                          (unsigned)(product->price_cents % 100));
    lv_label_set_long_mode(cell->header_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cell->header_label, LV_PCT(100));
    lv_obj_set_style_text_align(cell->header_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cell->header_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(cell->header_label, &lv_font_montserrat_12, 0);

    /*
     * Recuadro gris claro para la imagen de la MicroSD. Si el BMP no existe se
     * muestra "[Espacio Imagen]" centrado; si existe, se oculta el placeholder.
     */
    cell->img_box = lv_obj_create(cell->cell);
    lv_obj_set_size(cell->img_box, GUI_CELL_IMAGE_SIZE, GUI_CELL_IMAGE_SIZE);
    lv_obj_set_style_bg_color(cell->img_box, lv_color_hex(0xD5DBDB), 0);
    lv_obj_set_style_bg_opa(cell->img_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell->img_box, 0, 0);
    lv_obj_set_style_radius(cell->img_box, 4, 0);
    lv_obj_set_style_pad_all(cell->img_box, 0, 0);
    lv_obj_clear_flag(cell->img_box, LV_OBJ_FLAG_SCROLLABLE);

    cell->img = lv_img_create(cell->img_box);
    lv_obj_center(cell->img);

    to_lvgl_path(product->image_path, cell->image_src,
                 sizeof(cell->image_src));
    const bool has_image = sd_file_exists(product->image_path);

    cell->img_placeholder = lv_label_create(cell->img_box);
    lv_label_set_text(cell->img_placeholder, "[Espacio Imagen]");
    lv_label_set_long_mode(cell->img_placeholder, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cell->img_placeholder, GUI_CELL_IMAGE_SIZE - 4);
    lv_obj_set_style_text_align(cell->img_placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cell->img_placeholder,
                                lv_color_hex(0x5D6D7E), 0);
    lv_obj_set_style_text_font(cell->img_placeholder,
                               &lv_font_montserrat_10, 0);
    lv_obj_center(cell->img_placeholder);

    if (has_image) {
        /* cell->image_src es estatico: LVGL conserva el puntero. */
        lv_img_set_src(cell->img, cell->image_src);
        lv_obj_add_flag(cell->img_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cell->img, LV_OBJ_FLAG_HIDDEN);
    }

    /* Fila de controles: [-]  Cant: N  [+]. */
    lv_obj_t *controls = lv_obj_create(cell->cell);
    lv_obj_set_size(controls, LV_PCT(100), GUI_CELL_CONTROLS_HEIGHT);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_radius(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, 0);
    lv_obj_set_style_pad_column(controls, 2, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    cell->minus_btn = lv_btn_create(controls);
    lv_obj_set_size(cell->minus_btn, GUI_CELL_BUTTON_SIZE,
                    GUI_CELL_BUTTON_SIZE);
    lv_obj_set_style_radius(cell->minus_btn, 6, 0);
    lv_obj_set_style_bg_color(cell->minus_btn, lv_color_hex(0x2C3E50), 0);
    lv_obj_add_event_cb(cell->minus_btn, on_minus_clicked, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
    lv_obj_t *minus_label = lv_label_create(cell->minus_btn);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_20, 0);
    lv_obj_center(minus_label);

    cell->qty_label = lv_label_create(controls);
    lv_label_set_text(cell->qty_label, "Cant: 0");
    lv_obj_set_style_text_align(cell->qty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cell->qty_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(cell->qty_label, &lv_font_montserrat_12, 0);

    cell->plus_btn = lv_btn_create(controls);
    lv_obj_set_size(cell->plus_btn, GUI_CELL_BUTTON_SIZE,
                    GUI_CELL_BUTTON_SIZE);
    lv_obj_set_style_radius(cell->plus_btn, 6, 0);
    lv_obj_set_style_bg_color(cell->plus_btn, lv_color_hex(0x2C3E50), 0);
    lv_obj_add_event_cb(cell->plus_btn, on_plus_clicked, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
    lv_obj_t *plus_label = lv_label_create(cell->plus_btn);
    lv_label_set_text(plus_label, "+");
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_20, 0);
    lv_obj_center(plus_label);
}

static void create_bottom_panel(void)
{
    s_bottom_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_bottom_panel, BOARD_TFT_H_RES, GUI_BOTTOM_PANEL_HEIGHT);
    lv_obj_align(s_bottom_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bottom_panel, lv_color_hex(0x16202C), 0);
    lv_obj_set_style_bg_opa(s_bottom_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bottom_panel, lv_color_hex(0x3A5068), 0);
    lv_obj_set_style_border_side(s_bottom_panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_bottom_panel, 2, 0);
    lv_obj_set_style_radius(s_bottom_panel, 0, 0);
    lv_obj_set_style_pad_all(s_bottom_panel, 6, 0);
    lv_obj_set_style_pad_column(s_bottom_panel, 6, 0);
    lv_obj_clear_flag(s_bottom_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Fila: informacion a la izquierda y boton grande a la derecha. */
    lv_obj_set_layout(s_bottom_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_bottom_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bottom_panel, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *info = lv_obj_create(s_bottom_panel);
    lv_obj_set_height(info, LV_PCT(100));
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_radius(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_pad_row(info, 2, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(info, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);

    s_balance_label = lv_label_create(info);
    lv_label_set_text(s_balance_label, "Saldo: RD$ 0.00");
    lv_label_set_long_mode(s_balance_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_balance_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_balance_label, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_text_font(s_balance_label, &lv_font_montserrat_14, 0);

    s_total_label = lv_label_create(info);
    lv_label_set_text(s_total_label, "Monto a Pagar: RD$ 0.00");
    lv_label_set_long_mode(s_total_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_total_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_total_label, lv_color_hex(0xF1C40F), 0);
    lv_obj_set_style_text_font(s_total_label, &lv_font_montserrat_14, 0);

    s_status_label = lv_label_create(info);
    lv_label_set_text(s_status_label, "Seleccione productos");
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status_label, LV_PCT(100));
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_10, 0);

    s_confirm_btn = lv_btn_create(s_bottom_panel);
    lv_obj_set_size(s_confirm_btn, 118, 64);
    lv_obj_set_style_radius(s_confirm_btn, 10, 0);
    lv_obj_set_style_bg_color(s_confirm_btn, lv_color_hex(0x2980B9), 0);
    lv_obj_add_event_cb(s_confirm_btn, on_confirm_clicked, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_state(s_confirm_btn, LV_STATE_DISABLED);

    lv_obj_t *confirm_label = lv_label_create(s_confirm_btn);
    lv_label_set_text(confirm_label, "[ Confirmar ]");
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_14, 0);
    lv_obj_center(confirm_label);
}

static void create_toast(void)
{
    s_toast_box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_toast_box, 280, 44);
    lv_obj_align(s_toast_box, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(s_toast_box, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_bg_opa(s_toast_box, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_toast_box, lv_color_hex(0x5D6D7E), 0);
    lv_obj_set_style_border_width(s_toast_box, 1, 0);
    lv_obj_set_style_radius(s_toast_box, 8, 0);
    lv_obj_set_style_pad_all(s_toast_box, 4, 0);
    lv_obj_clear_flag(s_toast_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_toast_box, LV_OBJ_FLAG_HIDDEN);

    s_toast_label = lv_label_create(s_toast_box);
    lv_label_set_text(s_toast_label, "");
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_toast_label, 264);
    lv_obj_set_style_text_align(s_toast_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_toast_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(s_toast_label, &lv_font_montserrat_12, 0);
    lv_obj_center(s_toast_label);

    s_toast_timer = lv_timer_create(toast_timer_cb, GUI_TOAST_DURATION_MS, NULL);
    if (s_toast_timer != NULL) {
        lv_timer_pause(s_toast_timer);
    }
}

/* ==========================================================================
 *  Modo administracion (pulsacion larga en la esquina superior izquierda)
 * ========================================================================== */

static void destroy_admin_overlay(void)
{
    if (s_admin_overlay != NULL) {
        /*
         * Borrado diferido: es seguro aunque se llame desde el callback de uno
         * de los botones del propio overlay (LVGL 8.4).
         */
        lv_obj_del_async(s_admin_overlay);
        s_admin_overlay = NULL;
    }
    s_admin_open = false;

    /* Los widgets ya no existen: invalidar las referencias colgantes. */
    s_admin_stock_label = NULL;
    s_admin_status_label = NULL;
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        s_admin_price_labels[i] = NULL;
        s_admin_test_btns[i] = NULL;
    }
}

/** Actualiza la linea de estado fija del panel de administracion. */
static void admin_set_status(const char *message)
{
    if (s_admin_status_label != NULL && message != NULL) {
        lv_label_set_text(s_admin_status_label, message);
    }
}

/** Crea una seccion con titulo dentro del panel de administracion. */
static lv_obj_t *create_admin_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(section, 8, 0);
    lv_obj_set_style_pad_row(section, 8, 0);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x263445), 0);
    lv_obj_set_style_bg_opa(section, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_radius(section, 8, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(section);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF1C40F), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    return section;
}

/** Crea un boton con etiqueta centrada y callback de clic. */
static lv_obj_t *create_admin_button(lv_obj_t *parent, const char *text,
                                     lv_coord_t width, lv_coord_t height,
                                     uint32_t color, const lv_font_t *font,
                                     lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);

    return button;
}

static void on_admin_close_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    destroy_admin_overlay();

    /* Rearma el grid: sin este aviso los botones quedan deshabilitados. */
    if (state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        s_ui_dirty = true;
        state_unlock();
    }
    ui_notify();
}

static void on_admin_reset_stock_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (motor_control_is_busy()) {
        admin_set_status("Espere a que termine el despacho");
        return;
    }

    bool reset_done = false;
    if (state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        if (s_order.active || s_modal_open) {
            set_toast_locked("Espere a que termine el despacho");
            s_ui_dirty = true;
        } else {
            for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
                s_products[i].stock = GUI_DEFAULT_STOCK;
                s_products[i].quantity = 0;
            }

            const esp_err_t save_err = storage_save_locked();
            set_toast_locked(save_err == ESP_OK
                                 ? "Stock restablecido a 10 unidades"
                                 : "Stock restablecido; fallo al guardar NVS");
            s_ui_dirty = true;
            reset_done = true;
        }
        state_unlock();
    }

    if (reset_done) {
        admin_set_status("Stock restablecido a 10 unidades");
        ESP_LOGI(TAG, "Modo administracion: stock restablecido a %u por producto",
                 (unsigned)GUI_DEFAULT_STOCK);
    } else {
        admin_set_status("Espere a que termine el despacho");
    }
    ui_notify();
}

static void on_admin_price_step_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    /*
     * user_data codifica el producto y la direccion:
     *   bit 0 = 1 suma, 0 resta; bits 1..3 = indice 0..5.
     */
    const uint32_t code = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    const uint8_t index = (uint8_t)(code >> 1);
    const bool is_plus = (code & 1U) != 0;
    if (index >= GUI_PRODUCT_COUNT) {
        return;
    }

    int32_t price = (int32_t)s_admin_price_draft[index];
    price += is_plus ? GUI_PRICE_STEP_CENTS : -GUI_PRICE_STEP_CENTS;
    if (price < GUI_PRICE_MIN_CENTS) {
        price = GUI_PRICE_MIN_CENTS;
    }
    if (price > GUI_PRICE_MAX_CENTS) {
        price = GUI_PRICE_MAX_CENTS;
    }
    s_admin_price_draft[index] = (uint32_t)price;

    if (s_admin_price_labels[index] != NULL) {
        lv_label_set_text_fmt(s_admin_price_labels[index], "RD$ %u.%02u",
                              (unsigned)(s_admin_price_draft[index] / 100),
                              (unsigned)(s_admin_price_draft[index] % 100));
    }
    admin_set_status("Cambios de precio sin guardar");
}

static void on_admin_save_prices_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *status = "No se pudieron guardar los precios";
    bool applied = false;
    esp_err_t save_err = ESP_FAIL;

    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        admin_set_status("Estado ocupado: intente de nuevo");
        return;
    }

    if (s_order.active || s_modal_open) {
        set_toast_locked("Espere a que termine el despacho");
        s_ui_dirty = true;
        status = "Espere a que termine el despacho";
    } else {
        for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
            uint32_t price = s_admin_price_draft[i];
            if (price < GUI_PRICE_MIN_CENTS) {
                price = GUI_PRICE_MIN_CENTS;
            }
            if (price > GUI_PRICE_MAX_CENTS) {
                price = GUI_PRICE_MAX_CENTS;
            }
            s_admin_price_draft[i] = price;
            s_products[i].price_cents = price;
        }
        save_err = storage_save_locked();
        set_toast_locked(save_err == ESP_OK
                             ? "Precios guardados"
                             : "Precios aplicados; fallo al guardar NVS");
        s_ui_dirty = true;
        applied = true;
        status = (save_err == ESP_OK)
                     ? "Precios guardados en NVS"
                     : "Precios aplicados; fallo al guardar NVS";
    }

    state_unlock();

    admin_set_status(status);
    if (applied) {
        ESP_LOGI(TAG, "Modo administracion: precios actualizados (%s)",
                 esp_err_to_name(save_err));
    }
    ui_notify();
}

static void on_admin_test_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const uint8_t channel =
        (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (channel >= GUI_PRODUCT_COUNT) {
        return;
    }

    /*
     * Un test no puede convivir con un pedido real ni con otro test: la cola y
     * la fuente de 12 V son recursos unicos. Se rechaza antes de encolar.
     */
    if (motor_control_is_busy()) {
        admin_set_status("Espere a que termine el despacho");
        return;
    }

    bool allowed = false;
    const char *reason = "Prueba no disponible";
    if (state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        if (s_order.active || s_modal_open) {
            reason = "Espere a que termine el despacho";
        } else if (s_products[channel].stock == 0) {
            reason = "Producto agotado: no se puede probar";
        } else {
            allowed = true;
        }
        state_unlock();
    } else {
        reason = "Estado ocupado: intente de nuevo";
    }

    if (!allowed) {
        admin_set_status(reason);
        return;
    }

    const motor_request_t request = {
        .channel = channel,
        .quantity = 1,
        .admin_test = true,
    };
    const esp_err_t err = motor_control_enqueue_batch(&request, 1);

    char status[GUI_TOAST_MAX_LEN];
    if (err == ESP_OK) {
        snprintf(status, sizeof(status), "Prueba M%u encolada",
                 (unsigned)(channel + 1));
        ESP_LOGI(TAG, "Modo administracion: prueba del motor %u encolada",
                 (unsigned)(channel + 1));
    } else {
        snprintf(status, sizeof(status), "No se pudo encolar la prueba M%u",
                 (unsigned)(channel + 1));
        ESP_LOGW(TAG, "Modo administracion: fallo al encolar prueba M%u: %s",
                 (unsigned)(channel + 1), esp_err_to_name(err));
    }
    admin_set_status(status);
}

static void build_admin_overlay(void)
{
    if (s_admin_overlay != NULL) {
        return;
    }

    uint8_t stock[GUI_PRODUCT_COUNT] = {0};
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Modo administracion: no se pudo leer el estado");
        return;
    }
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        stock[i] = s_products[i].stock;
        s_admin_price_draft[i] = s_products[i].price_cents;
    }
    state_unlock();

    s_admin_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_admin_overlay, BOARD_TFT_H_RES, BOARD_TFT_V_RES);
    lv_obj_set_pos(s_admin_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_admin_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_admin_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_admin_overlay, 0, 0);
    lv_obj_set_style_radius(s_admin_overlay, 0, 0);
    lv_obj_clear_flag(s_admin_overlay, LV_OBJ_FLAG_SCROLLABLE);
    /* Captura todos los clics: bloquea productos y panel inferior. */
    lv_obj_add_flag(s_admin_overlay, LV_OBJ_FLAG_CLICKABLE);
    s_admin_open = true;

    lv_obj_t *card = lv_obj_create(s_admin_overlay);
    lv_obj_set_size(card, 306, 464);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2A38), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3A5068), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Cabecera fija: titulo + cierre. */
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 32);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Modo Administracion");
    lv_obj_set_style_text_color(title, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    (void)create_admin_button(header, "X", 36, 30, 0x7F8C8D,
                              &lv_font_montserrat_16,
                              on_admin_close_clicked, NULL);

    /* Linea de estado fija (feedback de guardado y pruebas). */
    s_admin_status_label = lv_label_create(card);
    lv_label_set_text(s_admin_status_label, "Ajustes del operador");
    lv_obj_set_width(s_admin_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_admin_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_admin_status_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(s_admin_status_label, &lv_font_montserrat_12, 0);

    /* Contenedor con scroll vertical para las tres secciones. */
    lv_obj_t *content = lv_obj_create(card);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 2, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    /* --- Seccion 1: inventario ------------------------------------------- */
    lv_obj_t *inventory = create_admin_section(content, "Inventario");

    s_admin_stock_label = lv_label_create(inventory);
    lv_label_set_text_fmt(s_admin_stock_label,
                          "Stock: %u / %u / %u\n       %u / %u / %u",
                          (unsigned)stock[0], (unsigned)stock[1],
                          (unsigned)stock[2], (unsigned)stock[3],
                          (unsigned)stock[4], (unsigned)stock[5]);
    lv_obj_set_style_text_color(s_admin_stock_label, lv_color_hex(0xD5DBDB), 0);
    lv_obj_set_style_text_font(s_admin_stock_label, &lv_font_montserrat_14, 0);

    (void)create_admin_button(inventory, "Rellenar Stock (Reset a 10)",
                              LV_PCT(100), 42, 0x27AE60,
                              &lv_font_montserrat_14,
                              on_admin_reset_stock_clicked, NULL);

    /* --- Seccion 2: precios ---------------------------------------------- */
    lv_obj_t *prices = create_admin_section(content, "Precios (RD$)");

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(prices);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 34);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, s_products[i].name);
        lv_obj_set_width(name, 74);
        lv_obj_set_style_text_color(name, lv_color_hex(0xECF0F1), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);

        s_admin_price_labels[i] = lv_label_create(row);
        lv_label_set_text_fmt(s_admin_price_labels[i], "RD$ %u.%02u",
                              (unsigned)(s_admin_price_draft[i] / 100),
                              (unsigned)(s_admin_price_draft[i] % 100));
        lv_obj_set_width(s_admin_price_labels[i], 72);
        lv_obj_set_style_text_align(s_admin_price_labels[i],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(s_admin_price_labels[i],
                                    lv_color_hex(0xF1C40F), 0);
        lv_obj_set_style_text_font(s_admin_price_labels[i],
                                   &lv_font_montserrat_14, 0);

        (void)create_admin_button(
            row, "-", 34, 30, 0xC0392B, &lv_font_montserrat_16,
            on_admin_price_step_clicked,
            (void *)(uintptr_t)(((uint32_t)i << 1) | 0U));
        (void)create_admin_button(
            row, "+", 34, 30, 0x27AE60, &lv_font_montserrat_16,
            on_admin_price_step_clicked,
            (void *)(uintptr_t)(((uint32_t)i << 1) | 1U));
    }

    (void)create_admin_button(prices, "Guardar Precios", LV_PCT(100), 40,
                              0x2980B9, &lv_font_montserrat_14,
                              on_admin_save_prices_clicked, NULL);

    /* --- Seccion 3: prueba de motores ------------------------------------ */
    lv_obj_t *tests = create_admin_section(content, "Prueba de Motores");

    lv_obj_t *test_grid = lv_obj_create(tests);
    lv_obj_set_width(test_grid, LV_PCT(100));
    lv_obj_set_height(test_grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(test_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(test_grid, 0, 0);
    lv_obj_set_style_pad_all(test_grid, 0, 0);
    lv_obj_set_style_pad_row(test_grid, 6, 0);
    lv_obj_set_style_pad_column(test_grid, 6, 0);
    lv_obj_clear_flag(test_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(test_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(test_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(test_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        char text[16];
        snprintf(text, sizeof(text), "Test M%u", (unsigned)(i + 1));
        s_admin_test_btns[i] =
            create_admin_button(test_grid, text, 78, 38, 0x8E44AD,
                                &lv_font_montserrat_12,
                                on_admin_test_clicked,
                                (void *)(uintptr_t)i);
    }

    lv_obj_t *hint = lv_label_create(tests);
    lv_label_set_text(hint, "Solo descuenta stock; no toca el saldo.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x95A5A6), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
}

static void on_admin_hotspot_long_pressed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) {
        return;
    }
    ESP_LOGI(TAG, "Pulsacion larga: abriendo modo administracion");
    build_admin_overlay();
}

static void create_admin_hotspot(void)
{
    s_admin_hotspot = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_admin_hotspot, GUI_ADMIN_HOTSPOT_SIZE,
                    GUI_ADMIN_HOTSPOT_SIZE);
    lv_obj_set_pos(s_admin_hotspot, 0, 0);
    lv_obj_set_style_bg_opa(s_admin_hotspot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_admin_hotspot, 0, 0);
    lv_obj_set_style_radius(s_admin_hotspot, 0, 0);
    lv_obj_set_style_pad_all(s_admin_hotspot, 0, 0);
    lv_obj_clear_flag(s_admin_hotspot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_admin_hotspot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_admin_hotspot, on_admin_hotspot_long_pressed,
                        LV_EVENT_LONG_PRESSED, NULL);
}

static void build_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Area superior: contenedor Flex con ROW_WRAP. No se usa LV_GRID porque el
     * flex con ancho fijo por celda garantiza 2 columnas x 3 filas en 320x480.
     * Ocupa todo el espacio por encima del panel inferior (400 px = 83.3 %).
     */
    s_grid = lv_obj_create(screen);
    lv_obj_set_size(s_grid, BOARD_TFT_H_RES,
                    BOARD_TFT_V_RES - GUI_BOTTOM_PANEL_HEIGHT);
    lv_obj_align(s_grid, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_radius(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, 4, 0);
    lv_obj_set_style_pad_row(s_grid, 4, 0);
    lv_obj_set_style_pad_column(s_grid, 4, 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * ROW_WRAP + ancho 48 % por celda = salto de linea automatico tras cada
     * pareja de productos. Sin esto los 6 productos se agrupan en una fila.
     */
    lv_obj_set_layout(s_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        create_cell(i);
    }

    create_bottom_panel();
    create_toast();
    /* Se crea al final para quedar por encima del grid y del toast. */
    create_admin_hotspot();
}

/* ==========================================================================
 *  Modal de confirmacion
 * ========================================================================== */

static void destroy_modal(void)
{
    if (s_modal_overlay != NULL) {
        lv_obj_del(s_modal_overlay);
        s_modal_overlay = NULL;
        s_modal_card = NULL;
    }
}

static void build_modal(const gui_modal_item_t *items, uint8_t item_count,
                        int64_t total_cents, int64_t balance_cents)
{
    destroy_modal();

    s_modal_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_modal_overlay, BOARD_TFT_H_RES, BOARD_TFT_V_RES);
    lv_obj_set_pos(s_modal_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_modal_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_modal_overlay, 0, 0);
    lv_obj_set_style_radius(s_modal_overlay, 0, 0);
    lv_obj_clear_flag(s_modal_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_modal_card = lv_obj_create(s_modal_overlay);
    lv_obj_set_size(s_modal_card, 290, 300);
    lv_obj_center(s_modal_card);
    lv_obj_set_style_bg_color(s_modal_card, lv_color_hex(0x1E2A38), 0);
    lv_obj_set_style_bg_opa(s_modal_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_card, lv_color_hex(0x3A5068), 0);
    lv_obj_set_style_border_width(s_modal_card, 1, 0);
    lv_obj_set_style_radius(s_modal_card, 10, 0);
    lv_obj_set_style_pad_all(s_modal_card, 12, 0);
    lv_obj_clear_flag(s_modal_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal_card);
    lv_label_set_text(title, "Confirmar compra");
    lv_obj_set_style_text_color(title, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *summary = lv_label_create(s_modal_card);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(summary, 260);
    lv_obj_set_height(summary, 110);
    lv_obj_set_style_text_align(summary, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(0xD5DBDB), 0);
    lv_obj_set_style_text_font(summary, &lv_font_montserrat_12, 0);
    lv_obj_align(summary, LV_ALIGN_TOP_LEFT, 0, 30);

    char text[384];
    size_t offset = 0;
    text[0] = '\0';
    for (uint8_t i = 0; i < item_count; i++) {
        const gui_modal_item_t *item = &items[i];
        const int written = snprintf(
            text + offset, sizeof(text) - offset, "%s  x%u   RD$ %u.%02u\n",
            item->name, (unsigned)item->quantity,
            (unsigned)(item->unit_price_cents / 100),
            (unsigned)(item->unit_price_cents % 100));
        if (written <= 0 || (size_t)written >= (sizeof(text) - offset)) {
            break;
        }
        offset += (size_t)written;
    }
    lv_label_set_text(summary, text);

    lv_obj_t *total_label = lv_label_create(s_modal_card);
    lv_label_set_text_fmt(total_label,
                          "TOTAL: RD$ %lld.%02lld\nSaldo: RD$ %lld.%02lld",
                          (long long)(total_cents / 100),
                          (long long)(total_cents % 100),
                          (long long)(balance_cents / 100),
                          (long long)(balance_cents % 100));
    lv_obj_set_style_text_align(total_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(total_label, lv_color_hex(0xF1C40F), 0);
    lv_obj_set_style_text_font(total_label, &lv_font_montserrat_14, 0);
    lv_obj_align(total_label, LV_ALIGN_BOTTOM_MID, 0, -48);

    lv_obj_t *yes_btn = lv_btn_create(s_modal_card);
    lv_obj_set_size(yes_btn, 118, 40);
    lv_obj_set_style_radius(yes_btn, 8, 0);
    lv_obj_set_style_bg_color(yes_btn, lv_color_hex(0x27AE60), 0);
    lv_obj_align(yes_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(yes_btn, on_modal_yes_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Si, comprar");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_14, 0);
    lv_obj_center(yes_label);

    lv_obj_t *no_btn = lv_btn_create(s_modal_card);
    lv_obj_set_size(no_btn, 118, 40);
    lv_obj_set_style_radius(no_btn, 8, 0);
    lv_obj_set_style_bg_color(no_btn, lv_color_hex(0xC0392B), 0);
    lv_obj_align(no_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(no_btn, on_modal_no_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(no_label, &lv_font_montserrat_14, 0);
    lv_obj_center(no_label);
}

/* ==========================================================================
 *  Refresco diferido de la interfaz (solo desde gui_task)
 * ========================================================================== */

typedef struct {
    gui_product_t products[GUI_PRODUCT_COUNT];
    int64_t balance_cents;
    bool modal_open;
    bool order_active;
    uint32_t pending_units;
    bool toast_pending;
    char toast[GUI_TOAST_MAX_LEN];
    gui_modal_item_t modal_items[GUI_PRODUCT_COUNT];
    uint8_t modal_item_count;
    int64_t modal_total_cents;
    int64_t modal_balance_cents;
} gui_snapshot_t;

static void gui_refresh_ui(void)
{
    if (!s_display_ready || !s_lvgl_ready) {
        return;
    }

    gui_snapshot_t snapshot;
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }
    memcpy(snapshot.products, s_products, sizeof(s_products));
    snapshot.balance_cents = s_balance_cents;
    snapshot.modal_open = s_modal_open;
    snapshot.order_active = s_order.active;
    snapshot.pending_units = s_order.pending_units;
    snapshot.toast_pending = s_toast_pending;
    memcpy(snapshot.toast, s_toast_msg, sizeof(snapshot.toast));
    memcpy(snapshot.modal_items, s_modal_items, sizeof(s_modal_items));
    snapshot.modal_item_count = s_modal_item_count;
    snapshot.modal_total_cents = s_modal_total_cents;
    snapshot.modal_balance_cents = s_modal_balance_cents;
    s_toast_pending = false;
    s_ui_dirty = false;
    state_unlock();

    const bool input_enabled =
        !snapshot.order_active && !snapshot.modal_open && !s_admin_open;

    uint32_t selected_units = 0;
    int64_t selected_total_cents = 0;

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        gui_cell_widgets_t *cell = &s_cells[i];
        const gui_product_t *product = &snapshot.products[i];

        selected_units += product->quantity;
        selected_total_cents +=
            (int64_t)product->quantity * (int64_t)product->price_cents;

        lv_label_set_text_fmt(cell->qty_label, "Cant: %u",
                              (unsigned)product->quantity);

        if (product->stock == 0) {
            lv_label_set_text_fmt(cell->header_label, "%s\nAGOTADO",
                                  product->name);
            lv_obj_set_style_text_color(cell->header_label,
                                        lv_color_hex(0xE74C3C), 0);
        } else if (product->price_cents == GUI_PRICE_UNSET_CENTS) {
            lv_label_set_text_fmt(cell->header_label, "%s\nSin precio",
                                  product->name);
            lv_obj_set_style_text_color(cell->header_label,
                                        lv_color_hex(0x95A5A6), 0);
        } else {
            lv_label_set_text_fmt(cell->header_label, "%s\nRD$ %u.%02u",
                                  product->name,
                                  (unsigned)(product->price_cents / 100),
                                  (unsigned)(product->price_cents % 100));
            lv_obj_set_style_text_color(cell->header_label,
                                        lv_color_hex(0xECF0F1), 0);
        }

        const bool can_edit = input_enabled && product->stock > 0;
        if (can_edit) {
            lv_obj_clear_state(cell->minus_btn, LV_STATE_DISABLED);
            lv_obj_clear_state(cell->plus_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(cell->minus_btn, LV_STATE_DISABLED);
            lv_obj_add_state(cell->plus_btn, LV_STATE_DISABLED);
        }
        if (product->quantity == 0) {
            lv_obj_add_state(cell->minus_btn, LV_STATE_DISABLED);
        }
        if (product->quantity >= product->stock ||
            product->quantity >= GUI_MAX_QTY_PER_PRODUCT) {
            lv_obj_add_state(cell->plus_btn, LV_STATE_DISABLED);
        }
    }

    lv_label_set_text_fmt(s_balance_label, "Saldo: RD$ %lld.%02lld",
                          (long long)(snapshot.balance_cents / 100),
                          (long long)(snapshot.balance_cents % 100));

    lv_label_set_text_fmt(s_total_label, "Monto a Pagar: RD$ %lld.%02lld",
                          (long long)(selected_total_cents / 100),
                          (long long)(selected_total_cents % 100));

    if (snapshot.order_active) {
        lv_label_set_text_fmt(s_status_label, "Despachando... %u unidad(es)",
                              (unsigned)snapshot.pending_units);
    } else if (snapshot.modal_open) {
        lv_label_set_text(s_status_label, "Confirme o cancele la compra");
    } else if (selected_units == 0) {
        lv_label_set_text(s_status_label, "Seleccione productos");
    } else {
        lv_label_set_text(s_status_label, "Pulse [ Confirmar ]");
    }

    const bool can_confirm =
        !snapshot.order_active && !snapshot.modal_open && selected_units > 0;
    if (can_confirm) {
        lv_obj_clear_state(s_confirm_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_confirm_btn, LV_STATE_DISABLED);
    }

    /*
     * Panel de administracion abierto: refrescar stock y habilitar/deshabilitar
     * los tests segun el estado real. El overlay vive en la misma tarea, asi
     * que tocar sus widgets aqui es seguro.
     */
    if (s_admin_overlay != NULL) {
        if (s_admin_stock_label != NULL) {
            lv_label_set_text_fmt(s_admin_stock_label,
                                  "Stock: %u / %u / %u\n       %u / %u / %u",
                                  (unsigned)snapshot.products[0].stock,
                                  (unsigned)snapshot.products[1].stock,
                                  (unsigned)snapshot.products[2].stock,
                                  (unsigned)snapshot.products[3].stock,
                                  (unsigned)snapshot.products[4].stock,
                                  (unsigned)snapshot.products[5].stock);
        }
        for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
            if (s_admin_test_btns[i] == NULL) {
                continue;
            }
            if (snapshot.order_active || snapshot.products[i].stock == 0) {
                lv_obj_add_state(s_admin_test_btns[i], LV_STATE_DISABLED);
            } else {
                lv_obj_clear_state(s_admin_test_btns[i], LV_STATE_DISABLED);
            }
        }
        if (snapshot.toast_pending && s_admin_status_label != NULL) {
            lv_label_set_text(s_admin_status_label, snapshot.toast);
        }
    }

    if (snapshot.modal_open && s_modal_overlay == NULL) {
        build_modal(snapshot.modal_items, snapshot.modal_item_count,
                    snapshot.modal_total_cents, snapshot.modal_balance_cents);
    } else if (!snapshot.modal_open && s_modal_overlay != NULL) {
        destroy_modal();
    }

    if (snapshot.toast_pending) {
        show_toast(snapshot.toast);
    }
}

/* ==========================================================================
 *  Hardware: SPI, ST7796, retroiluminacion y XPT2046
 * ========================================================================== */

static void init_backlight(void)
{
    if (BOARD_TFT_PIN_BL < 0) {
        return;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = BOARD_TFT_BL_LEDC_MODE,
        .duty_resolution = BOARD_TFT_BL_LEDC_RES,
        .timer_num = BOARD_TFT_BL_LEDC_TIMER,
        .freq_hz = BOARD_TFT_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LEDC timer no configurado: %s", esp_err_to_name(err));
        return;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_TFT_PIN_BL,
        .speed_mode = BOARD_TFT_BL_LEDC_MODE,
        .channel = BOARD_TFT_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_TFT_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LEDC channel no configurado: %s", esp_err_to_name(err));
        return;
    }

    ledc_set_duty(BOARD_TFT_BL_LEDC_MODE, BOARD_TFT_BL_LEDC_CHANNEL,
                  BOARD_TFT_BL_DUTY_ON);
    ledc_update_duty(BOARD_TFT_BL_LEDC_MODE, BOARD_TFT_BL_LEDC_CHANNEL);
}

static esp_err_t init_touch(void)
{
    esp_lcd_panel_io_spi_config_t touch_io_config =
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(BOARD_TOUCH_PIN_CS);
    touch_io_config.pclk_hz = BOARD_TOUCH_SPI_CLOCK_HZ;

    esp_err_t err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_TOUCH_SPI_HOST, &touch_io_config,
        &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo crear el IO del tactil: %s",
                 esp_err_to_name(err));
        return err;
    }

    const esp_lcd_touch_config_t touch_config = {
        .x_max = BOARD_TFT_H_RES,
        .y_max = BOARD_TFT_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BOARD_TOUCH_PIN_IRQ, /* NC: se usa sondeo */
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = BOARD_TOUCH_SWAP_XY,
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = NULL,
    };

    err = esp_lcd_touch_new_spi_xpt2046(s_touch_io, &touch_config, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "XPT2046 no inicializado: %s", esp_err_to_name(err));
        s_touch = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Tactil XPT2046 listo (CS=IO%d, %d Hz)",
             (int)BOARD_TOUCH_PIN_CS, BOARD_TOUCH_SPI_CLOCK_HZ);
    return ESP_OK;
}

static esp_err_t init_display_hardware(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_TFT_PIN_MOSI,
        .miso_io_num = BOARD_TFT_PIN_MISO,
        .sclk_io_num = BOARD_TFT_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz =
            BOARD_TFT_H_RES * GUI_DRAW_BUF_LINES * (int)sizeof(uint16_t),
    };

    esp_err_t err = spi_bus_initialize(BOARD_TFT_SPI_HOST, &bus_config,
                                       SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        s_tft_bus_owned = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Bus SPI de la TFT ya inicializado; se reutiliza");
        s_tft_bus_owned = false;
    } else {
        ESP_LOGE(TAG, "No se pudo inicializar SPI para la TFT: %s",
                 esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_TFT_PIN_CS,
        .dc_gpio_num = BOARD_TFT_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_TFT_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = &s_disp_drv,
        .lcd_cmd_bits = BOARD_TFT_CMD_BITS,
        .lcd_param_bits = BOARD_TFT_PARAM_BITS,
    };

    err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_TFT_SPI_HOST, &io_config, &s_tft_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el IO del panel: %s",
                 esp_err_to_name(err));
        if (s_tft_bus_owned) {
            spi_bus_free(BOARD_TFT_SPI_HOST);
            s_tft_bus_owned = false;
        }
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_TFT_PIN_RST,
#if BOARD_TFT_USE_BGR_ORDER
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };

    err = esp_lcd_new_panel_st7796(s_tft_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el panel ST7796: %s",
                 esp_err_to_name(err));
        esp_lcd_panel_io_del(s_tft_io);
        s_tft_io = NULL;
        if (s_tft_bus_owned) {
            spi_bus_free(BOARD_TFT_SPI_HOST);
            s_tft_bus_owned = false;
        }
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reset del panel: %s", esp_err_to_name(err));
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Init del panel ST7796 fallo: %s", esp_err_to_name(err));
        return err;
    }

#if BOARD_TFT_INVERT_COLOR
    esp_lcd_panel_invert_color(s_panel, true);
#endif
    esp_lcd_panel_swap_xy(s_panel, BOARD_TFT_SWAP_XY != 0);
    esp_lcd_panel_mirror(s_panel, BOARD_TFT_MIRROR_X != 0,
                         BOARD_TFT_MIRROR_Y != 0);
    esp_lcd_panel_disp_on_off(s_panel, true);

    init_backlight();

    /* El tactil es opcional: si falla, la pantalla sigue funcionando. */
    (void)init_touch();

    ESP_LOGI(TAG, "ST7796 listo (%dx%d, SPI%d, %d MHz)",
             BOARD_TFT_H_RES, BOARD_TFT_V_RES, (int)BOARD_TFT_SPI_HOST,
             BOARD_TFT_PIXEL_CLOCK_HZ / 1000000);
    return ESP_OK;
}

static esp_err_t init_lvgl(void)
{
    lv_init();

    const size_t buffer_pixels = BOARD_TFT_H_RES * GUI_DRAW_BUF_LINES;
    const size_t buffer_bytes = buffer_pixels * sizeof(lv_color_t);

    s_draw_buf1 = heap_caps_malloc(buffer_bytes,
                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_draw_buf2 = heap_caps_malloc(buffer_bytes,
                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_draw_buf1 == NULL || s_draw_buf2 == NULL) {
        ESP_LOGE(TAG, "Sin memoria DMA para los buffers de LVGL (%u B x2)",
                 (unsigned)buffer_bytes);
        free(s_draw_buf1);
        free(s_draw_buf2);
        s_draw_buf1 = NULL;
        s_draw_buf2 = NULL;
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_draw_buf1, s_draw_buf2,
                          buffer_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = BOARD_TFT_H_RES;
    s_disp_drv.ver_res = BOARD_TFT_V_RES;
    s_disp_drv.flush_cb = lv_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "No se pudo registrar el display en LVGL");
        return ESP_ERR_NO_MEM;
    }

    if (s_touch != NULL) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type = LV_INDEV_TYPE_POINTER;
        s_indev_drv.read_cb = lv_touch_read_cb;
        /*
         * Pulsacion larga global de 3 s: es la que abre el modo
         * administracion desde el boton invisible. Los botones normales solo
         * reaccionan a CLICKED, por lo que no se ven afectados.
         * En LVGL 8.4 se configura en el driver antes de registrarlo.
         */
        s_indev_drv.long_press_time = GUI_ADMIN_LONG_PRESS_MS;
        if (lv_indev_drv_register(&s_indev_drv) == NULL) {
            ESP_LOGW(TAG, "No se pudo registrar el tactil en LVGL");
        }
    }

    s_lvgl_ready = true;
    return ESP_OK;
}

/* ==========================================================================
 *  API publica: inicializacion
 * ========================================================================== */

esp_err_t gui_display_init(void)
{
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            ESP_LOGE(TAG, "No se pudo crear el Mutex de estado de la GUI");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_display_ready) {
        return ESP_OK;
    }

    s_modal_open = false;
    s_balance_cents = 0;
    s_ui_dirty = true;
    s_toast_pending = false;
    s_toast_msg[0] = '\0';
    memset(&s_order, 0, sizeof(s_order));
    memset(s_modal_items, 0, sizeof(s_modal_items));
    s_modal_item_count = 0;
    s_modal_total_cents = 0;
    s_modal_balance_cents = 0;

    /*
     * Precios, saldo y stock persistidos en NVS.
     * Si la NVS esta vacia se arranca con saldo 0, stock GUI_DEFAULT_STOCK y
     * precios de fabrica (RD$ 40..90).
     */
    storage_load_locked();

    esp_err_t err = init_display_hardware();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware de pantalla no disponible: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = init_lvgl();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL no inicializado: %s", esp_err_to_name(err));
        return err;
    }

    build_ui();
    s_display_ready = true;

    /* Primera pintura antes de que arranque task_gui. */
    gui_refresh_ui();

    ESP_LOGI(TAG, "GUI lista: ST7796 %dx%d + XPT2046 + LVGL %d.%d",
             BOARD_TFT_H_RES, BOARD_TFT_V_RES, LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR);
    return ESP_OK;
}

/* ==========================================================================
 *  MicroSD
 * ========================================================================== */

esp_err_t gui_display_mount_sd(void)
{
    if (s_sd_card != NULL) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_PIN_MOSI,
        .miso_io_num = BOARD_SD_PIN_MISO,
        .sclk_io_num = BOARD_SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg,
                                       SDSPI_DEFAULT_DMA);
    if (err == ESP_OK) {
        s_sd_bus_owned = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Bus SPI de la SD ya inicializado; se reutiliza");
        s_sd_bus_owned = false;
    } else {
        ESP_LOGE(TAG, "No se pudo inicializar el bus SPI de la SD: %s",
                 esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = BOARD_SD_PIN_CS;
    slot_cfg.host_id = BOARD_SD_SPI_HOST;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false, /* Nunca formatear: contiene imagenes. */
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(BOARD_SD_MOUNT_POINT, &host, &slot_cfg,
                                  &mount_cfg, &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo montar la MicroSD en %s: %s",
                 BOARD_SD_MOUNT_POINT, esp_err_to_name(err));
        s_sd_card = NULL;
        if (s_sd_bus_owned) {
            spi_bus_free(BOARD_SD_SPI_HOST);
            s_sd_bus_owned = false;
        }
        return err;
    }

    ESP_LOGI(TAG, "MicroSD montada en %s", BOARD_SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

esp_err_t gui_display_unmount_sd(void)
{
    if (s_sd_card == NULL) {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(BOARD_SD_MOUNT_POINT, s_sd_card);
    s_sd_card = NULL;

    if (s_sd_bus_owned) {
        spi_bus_free(BOARD_SD_SPI_HOST);
        s_sd_bus_owned = false;
    }

    ESP_LOGI(TAG, "MicroSD desmontada");
    return err;
}

/* ==========================================================================
 *  API publica: persistencia en NVS
 * ========================================================================== */

esp_err_t gui_display_load_state(void)
{
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    storage_load_locked();
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
    return ESP_OK;
}

esp_err_t gui_display_save_state(void)
{
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t err = storage_save_locked();

    state_unlock();
    return err;
}

/* ==========================================================================
 *  API publica: redibujo diferido
 * ========================================================================== */

void gui_display_render_grid(void)
{
    if (state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        s_ui_dirty = true;
        state_unlock();
    }
    ui_notify();
}

void gui_display_render_product(uint8_t index)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return;
    }
    gui_display_render_grid();
}

void gui_display_render_bottom_panel(void)
{
    gui_display_render_grid();
}

void gui_display_render(void)
{
    gui_display_render_grid();
}

/* ==========================================================================
 *  API publica: seleccion de productos
 * ========================================================================== */

esp_err_t gui_display_adjust_quantity(uint8_t index, int delta)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_order.active || s_modal_open) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    int quantity = (int)s_products[index].quantity + delta;
    const int max_quantity =
        (s_products[index].stock < GUI_MAX_QTY_PER_PRODUCT)
            ? s_products[index].stock
            : GUI_MAX_QTY_PER_PRODUCT;

    if (quantity < 0) {
        quantity = 0;
    }
    if (quantity > max_quantity) {
        quantity = max_quantity;
    }

    if (s_products[index].quantity != (uint8_t)quantity) {
        s_products[index].quantity = (uint8_t)quantity;
        s_ui_dirty = true;
    }

    state_unlock();
    ui_notify();
    return ESP_OK;
}

esp_err_t gui_display_set_quantity(uint8_t index, uint8_t quantity)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_order.active || s_modal_open) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t max_quantity =
        (s_products[index].stock < GUI_MAX_QTY_PER_PRODUCT)
            ? s_products[index].stock
            : GUI_MAX_QTY_PER_PRODUCT;

    s_products[index].quantity =
        (quantity > max_quantity) ? max_quantity : quantity;
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
    return ESP_OK;
}

uint8_t gui_display_get_quantity(uint8_t index)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return 0;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return 0;
    }
    const uint8_t quantity = s_products[index].quantity;
    state_unlock();
    return quantity;
}

esp_err_t gui_display_set_product_stock(uint8_t index, uint8_t stock)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    s_products[index].stock = stock;
    if (s_products[index].quantity > stock) {
        s_products[index].quantity = stock;
    }
    s_ui_dirty = true;
    (void)storage_save_locked();

    state_unlock();
    ui_notify();
    return ESP_OK;
}

esp_err_t gui_display_set_product_price(uint8_t index, uint32_t price_cents)
{
    if (index >= GUI_PRODUCT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (price_cents < GUI_PRICE_MIN_CENTS) {
        price_cents = GUI_PRICE_MIN_CENTS;
    }
    if (price_cents > GUI_PRICE_MAX_CENTS) {
        price_cents = GUI_PRICE_MAX_CENTS;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    s_products[index].price_cents = price_cents;
    s_ui_dirty = true;
    const esp_err_t err = storage_save_locked();

    state_unlock();
    ui_notify();
    return err;
}

const gui_product_t *gui_display_get_product(uint8_t index)
{
    return (index < GUI_PRODUCT_COUNT) ? &s_products[index] : NULL;
}

/* ==========================================================================
 *  API publica: saldo y total
 * ========================================================================== */

int64_t gui_display_get_balance_cents(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return 0;
    }
    const int64_t balance = s_balance_cents;
    state_unlock();
    return balance;
}

void gui_display_set_balance_cents(int64_t cents)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }
    s_balance_cents = (cents < 0) ? 0 : cents;
    s_ui_dirty = true;
    (void)storage_save_locked();
    state_unlock();
    ui_notify();
}

void gui_display_add_balance_cents(uint32_t cents)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }
    s_balance_cents += (int64_t)cents;
    s_ui_dirty = true;
    /* El saldo debe sobrevivir a un corte de energia. */
    (void)storage_save_locked();
    state_unlock();
    ui_notify();
}

int64_t gui_display_get_total_cents(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return 0;
    }

    int64_t total = 0;
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        total += (int64_t)s_products[i].quantity *
                 (int64_t)s_products[i].price_cents;
    }

    state_unlock();
    return total;
}

/* ==========================================================================
 *  API publica: modal y compra
 * ========================================================================== */

void gui_display_show_confirm_modal(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }

    if (s_order.active) {
        set_toast_locked("Espere a que termine el despacho");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return;
    }

    uint32_t units = 0;
    int64_t total_cents = 0;
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        const uint8_t quantity = s_products[i].quantity;
        if (quantity == 0) {
            continue;
        }
        units += quantity;
        total_cents += (int64_t)quantity * (int64_t)s_products[i].price_cents;
    }

    if (units == 0) {
        set_toast_locked("Seleccione al menos un producto");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return;
    }
    if (total_cents <= 0) {
        set_toast_locked("Precios no configurados");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return;
    }
    if (s_balance_cents < total_cents) {
        set_toast_locked("Saldo insuficiente");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return;
    }

    s_modal_item_count = 0;
    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        const uint8_t quantity = s_products[i].quantity;
        if (quantity == 0) {
            continue;
        }

        gui_modal_item_t *item = &s_modal_items[s_modal_item_count++];
        item->channel = i;
        item->quantity = quantity;
        item->unit_price_cents = s_products[i].price_cents;
        item->subtotal_cents =
            (int64_t)quantity * (int64_t)s_products[i].price_cents;
        strncpy(item->name, s_products[i].name, sizeof(item->name) - 1);
        item->name[sizeof(item->name) - 1] = '\0';
    }

    s_modal_total_cents = total_cents;
    s_modal_balance_cents = s_balance_cents;
    s_modal_open = true;
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
}

void gui_display_hide_confirm_modal(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }
    s_modal_open = false;
    s_ui_dirty = true;
    state_unlock();
    ui_notify();
}

bool gui_display_is_modal_open(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return false;
    }
    const bool open = s_modal_open;
    state_unlock();
    return open;
}

bool gui_display_is_order_active(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return false;
    }
    const bool active = s_order.active;
    state_unlock();
    return active;
}

esp_err_t gui_display_confirm_purchase(void)
{
    motor_request_t batch[GUI_PRODUCT_COUNT];
    uint8_t batch_count = 0;
    uint32_t total_units = 0;
    int64_t total_cents = 0;

    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_modal_open) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_order.active) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        const uint8_t quantity = s_products[i].quantity;
        if (quantity == 0) {
            continue;
        }
        if (quantity > s_products[i].stock) {
            set_toast_locked("Stock insuficiente");
            s_ui_dirty = true;
            state_unlock();
            ui_notify();
            return ESP_ERR_INVALID_STATE;
        }

        total_units += quantity;
        total_cents += (int64_t)quantity * (int64_t)s_products[i].price_cents;

        batch[batch_count].channel = i;
        batch[batch_count].quantity = quantity;
        batch_count++;
    }

    if (total_units == 0 || total_cents <= 0) {
        state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_balance_cents < total_cents) {
        set_toast_locked("Saldo insuficiente");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Encolado atomico (riesgo #4): se valida la capacidad para el pedido
     * COMPLETO antes de registrar nada. Si no cabe, el modal sigue abierto,
     * las cantidades se conservan y el usuario puede reducirlas.
     */
    const esp_err_t capacity_err = motor_control_check_capacity(batch_count);
    if (capacity_err != ESP_OK) {
        set_toast_locked("Cola ocupada: reduzca la cantidad");
        s_ui_dirty = true;
        state_unlock();
        ui_notify();
        return capacity_err;
    }

    /*
     * Se registra el pedido y se limpian las cantidades ANTES de encolar, para
     * que los callbacks de motor_task encuentren el estado consistente aunque
     * la primera unidad se despache inmediatamente.
     */
    memset(&s_order, 0, sizeof(s_order));
    s_order.active = true;
    s_order.total_units = total_units;
    s_order.pending_units = total_units;
    s_order.total_cents = total_cents;

    for (uint8_t i = 0; i < GUI_PRODUCT_COUNT; i++) {
        s_order.reserved[i] = s_products[i].quantity;
        s_order.requested[i] = s_products[i].quantity;
        s_products[i].quantity = 0;
    }

    s_modal_open = false;
    s_ui_dirty = true;

    state_unlock();
    ui_notify();

    /*
     * Un solo lote todo-o-nada: si falla, no quedo ninguna solicitud en cola y
     * se revierte el pedido completo devolviendo las cantidades al usuario.
     */
    const esp_err_t enqueue_error =
        motor_control_enqueue_batch(batch, batch_count);

    if (enqueue_error != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo encolar el pedido completo: %s",
                 esp_err_to_name(enqueue_error));

        if (state_lock(GUI_STATE_LOCK_TIMEOUT_MS * 10)) {
            memset(&s_order, 0, sizeof(s_order));
            for (uint8_t i = 0; i < batch_count; i++) {
                s_products[batch[i].channel].quantity = batch[i].quantity;
            }
            s_modal_open = true;
            set_toast_locked("No se pudo encolar el pedido. Intente de nuevo.");
            s_ui_dirty = true;
            state_unlock();
        } else {
            ESP_LOGE(TAG, "No se pudo revertir el pedido (Mutex de estado)");
        }
        ui_notify();
        return enqueue_error;
    }

    gui_display_show_message("Compra confirmada. Despachando...");
    return ESP_OK;
}

/* ==========================================================================
 *  Callbacks de motor_task (cobro por unidad entregada)
 * ========================================================================== */

void gui_display_on_unit_dispatched(uint8_t channel, bool admin_test)
{
    if (channel >= GUI_PRODUCT_COUNT) {
        return;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "No se pudo bloquear el estado al cobrar la unidad");
        return;
    }

    /*
     * PRUEBA DE ADMINISTRADOR: el motor giro por un boton "Test Mx". El
     * producto cae fisicamente, asi que se descuenta stock, pero NO se toca el
     * saldo, el pedido ni el flujo de compra.
     */
    if (admin_test) {
        if (s_products[channel].stock > 0) {
            s_products[channel].stock--;
        }
        (void)storage_save_locked();

        char message[GUI_TOAST_MAX_LEN];
        snprintf(message, sizeof(message),
                 "Test M%u: unidad entregada (stock %u)",
                 (unsigned)(channel + 1),
                 (unsigned)s_products[channel].stock);
        set_toast_locked(message);
        s_ui_dirty = true;

        ESP_LOGI(TAG,
                 "Prueba de administrador: unidad del canal %u entregada. "
                 "Stock restante: %u (saldo sin cambios)",
                 (unsigned)(channel + 1),
                 (unsigned)s_products[channel].stock);

        state_unlock();
        ui_notify();
        return;
    }

    if (!s_order.active || s_order.reserved[channel] == 0) {
        state_unlock();
        return;
    }

    /*
     * PUNTO UNICO DE COBRO: el motor confirmo fisicamente la unidad.
     */
    s_order.reserved[channel]--;
    if (s_order.pending_units > 0) {
        s_order.pending_units--;
    }
    s_order.delivered_units++;

    const int64_t price = (int64_t)s_products[channel].price_cents;
    s_balance_cents -= price;
    if (s_balance_cents < 0) {
        s_balance_cents = 0;
    }
    s_order.charged_cents += price;

    if (s_products[channel].stock > 0) {
        s_products[channel].stock--;
    }

    /*
     * Persistencia: cada unidad cobrada deja el nuevo saldo y stock en NVS.
     * Si el guardado falla, la venta sigue siendo valida en RAM y se registra
     * el error; nunca se deshace el cobro por un fallo de flash.
     */
    (void)storage_save_locked();

    ESP_LOGI(TAG, "Cobrada unidad del canal %u (RD$ %lld.%02lld). Saldo: RD$ %lld.%02lld",
             (unsigned)(channel + 1), (long long)(price / 100),
             (long long)(price % 100), (long long)(s_balance_cents / 100),
             (long long)(s_balance_cents % 100));

    if (s_order.pending_units == 0) {
        order_finalize_locked();
    }
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
}

void gui_display_on_dispatch_failed(uint8_t channel, esp_err_t error,
                                    bool admin_test)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }

    /*
     * Fallo de una prueba: no hay pedido que cancelar ni saldo que devolver.
     * Solo se informa al operador; el stock no cambia porque no cayo unidad.
     */
    if (admin_test) {
        char message[GUI_TOAST_MAX_LEN];
        snprintf(message, sizeof(message), "Test M%u fallo: %s",
                 (unsigned)(channel + 1), esp_err_to_name(error));
        set_toast_locked(message);
        s_ui_dirty = true;

        ESP_LOGW(TAG, "Prueba de administrador fallida en canal %u: %s",
                 (unsigned)(channel + 1), esp_err_to_name(error));

        state_unlock();
        ui_notify();
        return;
    }

    if (s_order.active) {
        ESP_LOGW(TAG, "Fallo de despacho en canal %u: %s. "
                      "No se cobran las unidades no entregadas.",
                 (unsigned)(channel + 1), esp_err_to_name(error));

        s_order.failed = true;
        memset(s_order.reserved, 0, sizeof(s_order.reserved));
        s_order.pending_units = 0;
        order_finalize_locked();
        s_ui_dirty = true;
    }

    state_unlock();
    ui_notify();
}

void gui_display_on_dispatch_idle(void)
{
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }

    if (s_order.active && s_order.pending_units == 0) {
        order_finalize_locked();
    }
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
}

/* ==========================================================================
 *  API publica: avisos y tarea de GUI
 * ========================================================================== */

void gui_display_show_message(const char *message)
{
    if (message == NULL) {
        return;
    }
    if (!state_lock(GUI_STATE_LOCK_TIMEOUT_MS)) {
        return;
    }

    set_toast_locked(message);
    s_ui_dirty = true;

    state_unlock();
    ui_notify();
}

void gui_task(void *arg)
{
    (void)arg;

    s_gui_task_handle = xTaskGetCurrentTaskHandle();
    ESP_LOGI(TAG, "task_gui iniciada (LVGL + tactil)");

    int64_t last_tick_us = esp_timer_get_time();

    for (;;) {
        if (s_lvgl_ready) {
            const int64_t now_us = esp_timer_get_time();
            const uint32_t elapsed_ms =
                (uint32_t)((now_us - last_tick_us) / 1000);
            if (elapsed_ms > 0) {
                lv_tick_inc(elapsed_ms);
                last_tick_us += (int64_t)elapsed_ms * 1000;
            }

            lv_timer_handler();
        }

        /*
         * task_gui duerme hasta 10 ms o hasta que otra tarea la despierte por
         * un cambio de estado (moneda, unidad despachada, error, etc.).
         */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10)) > 0) {
            gui_refresh_ui();
        }
    }
}
