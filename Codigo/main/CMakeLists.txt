# =============================================================================
#  Componente principal: main
#
#  Dependencias base:
#    - freertos   : tareas, colas, semaforos y Mutex de despacho.
#    - driver     : GPIO (ISR del monedero), I2C (PCF8574), SPI y host SDSPI.
#    - esp_timer  : marca de tiempo en microsegundos para la ventana de rafaga.
#    - sdmmc      : pila SD/MMC y estructuras de la tarjeta.
#    - fatfs      : sistema de archivos FAT para leer las imagenes de la SD.
#    - esp_lcd    : capa de abstraccion de paneles y bus SPI de pantalla.
#    - lvgl       : motor grafico (8.4.x, configurado por Kconfig).
#
#  Dependencias gestionadas (ver idf_component.yml):
#    - esp_lcd_st7796          : driver del panel ST7796 320x480.
#    - esp_lcd_touch           : capa comun de controladores tactiles.
#    - esp_lcd_touch_xpt2046   : driver del tactil resistivo XPT2046.
#
#  Compatibilidad de versiones:
#    En ESP-IDF 5.2 los drivers viven dentro de "driver". A partir de 5.3 se
#    separaron en componentes esp_driver_*. Se agregan de forma condicional para
#    que el proyecto compile tanto en 5.2 como en 5.3+ / 6.x.
# =============================================================================

set(esp_driver_components "")
foreach(component esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi esp_driver_ledc)
    if(EXISTS "${IDF_PATH}/components/${component}")
        list(APPEND esp_driver_components ${component})
    endif()
endforeach()

idf_component_register(
    SRCS
        "main.c"
        "motor_control.c"
        "coin_acceptor.c"
        "gui_display.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        freertos
        driver
        esp_timer
        sdmmc
        fatfs
        esp_lcd
        esp_lcd_st7796
        esp_lcd_touch
        esp_lcd_touch_xpt2046
        lvgl
        ${esp_driver_components}
)
