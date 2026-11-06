# Máquina Expendedora — Firmware (ESP-IDF)

Firmware en **C nativo** para una máquina expendedora de **6 canales** sobre una
placa **ESP32-32E 4.0"**. La fuente de verdad de la arquitectura es
[`documento_de_arquitectura_tecnico.md`](../Documentacion/Documentacion%20Tecnica/documento_de_arquitectura_tecnico.md).

> **Estado:** **Fase P1 (Firmware v1.0) validada en hardware el 2026-11-04.**
> Compila sin errores ni warnings (`maquina_expendedora.bin` = **611 600 B**,
> 42 % libre). Los **6 motores (M1–M6)** se probaron físicamente: relé + giro de
> 3 s + parada + cobro correcto.
>
> **Configuración actual de banco:** **sin monedero**
> (`BOARD_COIN_ACCEPTOR_ENABLED = 0`). IO35 no se inicializa y el saldo se carga
> desde el panel `ADM`. `BRINGUP_TEST_MOTOR1 = 0`.
> **Relés con transistor NPN inversor:** `BOARD_RELAY_ACTIVE_LOW = 0`. El
> PCF8574 saca **HIGH** para encender y **LOW** para apagar.

## Requisitos

- **ESP-IDF v5.2 o superior** (se usa el driver I2C moderno `i2c_master`).
  Verificado con **v5.5.6**.
- Placa ESP32-32E 4.0" con pantalla TFT **ST7796 320x480** y táctil resistivo
  **XPT2046**, más ranura MicroSD.
- Monedero con salida por optoacoplador PC817 sobre **IO35**.

### Dependencias gestionadas (`main/idf_component.yml`)

| Componente | Versión | Función |
| :--- | :--- | :--- |
| `lvgl/lvgl` | 8.4.0 | Motor gráfico (configurado por Kconfig, sin `lv_conf.h`) |
| `espressif/esp_lcd_st7796` | ^1.5.0 | Driver del panel ST7796 |
| `espressif/esp_lcd_touch` | ^1.2.1 | Capa común de táctiles |
| `atanisoft/esp_lcd_touch_xpt2046` | ^1.0.6 | Driver del táctil resistivo |

El IDF Component Manager las descarga en `managed_components/` (ignorado por
Git). `compilar.ps1` conserva esa carpeta entre builds para no re-descargar LVGL.

> **Nota de hardware:** GPIO35 es *input-only* y no tiene pull-up interno. Es
> obligatorio un pull-up externo de 10 kΩ a 3.3 V en la salida del PC817, y el
> firmware **no debe habilitar el pull-up por software**.
>
> **Sin monedero conectado IO35 queda flotante** y puede leer LOW. Por eso el
> firmware no acredita nada hasta que la línea lleva 500 ms en reposo, descarta
> pulsos fuera de 10–150 ms y muestra en el log:
> `La linea arranco en nivel activo: revise el pull-up externo...`.
> Si aparece ese aviso, el monedero **no está armado** y no aceptará monedas
> hasta corregir el pull-up y reiniciar.

### Verificaciones antes de energizar

- Alimentar el **PCF8574 a 3.3 V** (no 5 V) para mantener los niveles del bus I2C.
- Los **6 relés** son módulos de **1 canal** con bobina de 12 V y entrada optoacoplada. El **jumper `H/L` va en LOW** (0 V en `IN` enciende). Entre el PCF8574 y cada `IN` hay un **transistor NPN inversor**: `EMISOR`→GND, `BASE`→1 kΩ→P0…P5, `COLECTOR`→`IN`. Por eso el PCF8574 enciende con **HIGH**.
- Los **6 canales** se despachan por tiempo; el expansor `0x21` **no está montado todavía** (reservado para los sensores de posición **M1–M4**).
- **Pines confirmados físicamente** (ver `board_config.h`):
  - I2C (PCF8574 0x20): **SDA=IO32, SCL=IO25**, 100 kHz.
  - TFT ST7796 (SPI2/HSPI): **SCLK=14, MOSI=13, MISO=12, CS=15, DC=2,
    RST=NC, BL=27**.
  - Táctil XPT2046: **CS=33, IRQ=NC** (modo sondeo).
  - MicroSD (SPI3/VSPI): **CS=5, CLK=18, MISO=19, MOSI=23**.

### Activos gráficos en la MicroSD

```
/sdcard/productos/p1.bmp ... p6.bmp   # BMP de 64x64 px
```

LVGL los abre como `S:/productos/pN.bmp` (FS stdio con letra `S` y raíz
`/sdcard`). Si falta un archivo, la celda muestra el recuadro gris con
`[Espacio Imagen]` y la interfaz sigue funcionando.

## Entorno instalado en este equipo

| Componente | Ruta / versión |
| :--- | :--- |
| ESP-IDF | `C:\Users\asus\esp\esp-idf` (v5.5.6) |
| Toolchain y entorno Python | `C:\Users\asus\esp\espressif` (`IDF_TOOLS_PATH`) |
| Python para IDF | `C:\Users\asus\AppData\Local\Programs\Python\Python312` |
| Build generado | `C:\Users\asus\esp\build_maquina` |

> **Importante — ruta con acentos:** la carpeta del proyecto está en OneDrive y
> contiene acentos (`Tecnológico`, `Américas`). `ccache` y `kconfiglib` fallan
> con esa ruta (`Illegal byte sequence` / `Could not open sdkconfig`). Por eso
> **no compiles directamente aquí**: usa `compilar.ps1`, que copia las fuentes a
> `C:\Users\asus\esp\maquina_expendedora` (ruta ASCII) y compila allí.

## Compilar y flashear

```powershell
# Desde esta carpeta (Firmware/)
.\compilar.ps1                              # build
.\compilar.ps1 -Clean                       # build limpio
.\compilar.ps1 -Flash -Port COM3            # build + flash
.\compilar.ps1 -Flash -Port COM3 -Monitor   # build + flash + monitor
.\compilar.ps1 -Flash -Port COM5 -Erase     # borra NVS (saldo/stock) y flashea
```

Salir del monitor: `Ctrl + ]`.

Si prefieres hacerlo a mano, exporta el entorno y compila desde la copia ASCII:

```bat
set IDF_TOOLS_PATH=C:\Users\asus\esp\espressif
set PATH=C:\Users\asus\AppData\Local\Programs\Python\Python312;C:\Users\asus\AppData\Local\Programs\Python\Python312\Scripts;%PATH%
cd /d C:\Users\asus\esp\esp-idf
export.bat
cd /d C:\Users\asus\esp\maquina_expendedora
idf.py -B C:\Users\asus\esp\build_maquina build
```

## Estructura

```
Firmware/
├── CMakeLists.txt          # Proyecto: maquina_expendedora
├── compilar.ps1            # Sincroniza a ruta ASCII y compila/flashea
├── ESTADO_DEL_PROYECTO.md  # Estado actual y pendientes (handoff para IA)
├── sdkconfig.defaults      # Target ESP32, tick 1 ms, LVGL, XPT2046
└── main/
    ├── CMakeLists.txt      # Dependencias: freertos, driver, esp_lcd, lvgl, ...
    ├── idf_component.yml   # Dependencias gestionadas (LVGL, ST7796, XPT2046)
    ├── board_config.h      # Mapa de pines y constantes de hardware
    ├── main.c              # app_main: I2C, SD, callback de despacho y 3 tareas
    ├── motor_control.h/.c  # PCF8574 (0x20 relés), cola, eventos y pulso de 3 s
    ├── coin_acceptor.h/.c  # ISR IO35 y ráfagas 1/3/5/7 pulsos
    └── gui_display.h/.c    # ST7796 + XPT2046 + LVGL, grid 2x3, modal, cobro
```

## Arquitectura de tareas

| Tarea | Prioridad | Pila | Responsabilidad |
| :--- | :---: | :---: | :--- |
| `task_monedero` | 10 | 4096 B | ISR + ventana de 300 ms, valida monedas |
| `task_despacho` | 8 | 4096 B | Consume la cola, un motor a la vez, emite eventos |
| `task_gui` | 5 | 16384 B | `lv_timer_handler()`, tick de LVGL, refresco diferido |

## Lógica implementada

- **Monedero:** RD$ 1 = 1 pulso, RD$ 5 = 3, RD$ 10 = 5, RD$ 25 = 7. La ISR solo
  encola; la tarea cierra la ráfaga tras 300 ms de silencio y acredita el saldo.
- **Motores en lazo abierto:** los 6 canales (M1–M6) usan la misma estrategia.
  El firmware escribe el bit del canal en `0x20` en **HIGH (3.3 V)** durante
  `MOTOR_TIMED_PULSE_MS` (**3000 ms**) y después lo pone en **LOW (0 V)**. El
  HIGH satura el NPN, que lleva el `IN` del relé a 0 V; el LOW lo deja al corte.
  El relé se apaga siempre, incluso ante error, y solo un motor gira a la vez.
- **Eventos de despacho:** `motor_task` emite `MOTOR_EVENT_UNIT_DONE` por cada
  unidad entregada, `MOTOR_EVENT_REQUEST_FAILED` si un motor falla y
  `MOTOR_EVENT_IDLE` al vaciar la cola. `main.c` los adapta a la GUI.
- **GUI:** grid estricto 2×3 (≈83 % superior) con cabecera
  `Producto N\nRD$ XX.00`, recuadro de imagen o `[Espacio Imagen]`, y
  `[-] Cant: N [+]`. Panel inferior fijo (≈17 %) con `Saldo:`, `Monto a Pagar:`
  reactivo y botón grande `Confirmar`; modal `[Sí] [No]` y toast.
  El estado (saldo, stock, cantidades, pedido) está protegido por un Mutex y
  **todas** las llamadas a LVGL ocurren en `task_gui`.
- **Persistencia NVS:** `nvs_flash_init()` en `main.c`; saldo, stock y precios de
  los 6 productos se cargan al arrancar y se guardan al acreditar monedas, al
  cobrar/entregar una unidad y al guardar precios desde el modo administración.
  Precios de fábrica: RD$ 40/50/60/70/80/90 (mutables, clave NVS `prices`).
- **Cobro diferido (corrección del riesgo #3):** al confirmar, la GUI **solo
  encola** el pedido; no descuenta saldo ni stock. El descuento ocurre en
  `gui_display_on_unit_dispatched()`, únicamente cuando se completa el pulso
  temporizado de 3 s de la unidad. Si un motor falla por error de I2C, se
  descarta el resto del pedido y **lo no entregado no se cobra ni reduce stock**.
- **Modo administración:** botón `ADM` (46×40 px) junto a `Confirmar` y
  contraseña provisional **0000**. El teclado numérico se valida antes de abrir
  el panel con scroll de tres secciones: saldo (± RD$ 1/5/10/25, persistido en
  NVS al instante), inventario (reset a 10) y precios (`[-]`/`[+]` en RD$ 5 +
  guardado en NVS). El panel no incluye prueba de motores.

## Pendiente (iterativo)

1. ~~Cablear los 6 motores y verificar el pulso de 3 s en cada canal.~~ **Hecho
   el 2026-11-04:** los 6 motores giran y se detienen correctamente.
2. **Sensores de posición M1–M4 (siguiente etapa):** añadir el segundo PCF8574
   en `0x21` con divisores de tensión en las entradas, y usar la lectura para
   detener el motor en el home. M5 y M6 seguirán por tiempo.
3. MicroSD: el montaje falla con `sdmmc_card_init failed (0x107)`; solo afecta
   las imágenes de los productos.
4. Telemetría Wi-Fi y audio (V2.0).
