# Máquina Expendedora — Firmware (ESP-IDF)

Firmware en **C nativo** para una máquina expendedora de **6 canales** sobre una
placa **ESP32-32E 4.0"**. La fuente de verdad de la arquitectura es
[`documento_de_arquitectura_tecnico.md`](../Documentacion/Documentacion%20Tecnica/documento_de_arquitectura_tecnico.md).

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

### Verificaciones antes de energizar

- Alimentar los **PCF8574 a 3.3 V** (no 5 V) para mantener los niveles del bus I2C.
- Confirmar que los **módulos de relé conmutan de forma fiable con 3.3 V** en su
  entrada; algunos módulos de 5 V con optoacoplador necesitan un adaptador de nivel.
- **MicroSD confirmada en VSPI**: CS=IO5, CLK=IO18, MISO=IO19, MOSI=IO23.
- **Pines de la TFT y del táctil sin documentar**: los valores actuales
  (`SCLK=14, MOSI=13, MISO=12, CS=15, DC=2, RST=NC, BL=27, táctil CS=33`)
  corresponden al pinout habitual de la ESP32-32E 4.0" y están marcados con
  `TODO_VERIFICAR_PINES` en `board_config.h`. Ajustar solo esas macros.
- Los pines I2C (21/22) siguen marcados `VERIFICAR`.

### Activos gráficos en la MicroSD

```
/sdcard/productos/p1.bmp ... p6.bmp   # BMP de 64x64 px
```

LVGL los abre como `S:/productos/pN.bmp` (FS stdio con letra `S` y raíz
`/sdcard`). Si falta un archivo, la celda muestra el placeholder `IMG` y la
interfaz sigue funcionando.

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
# Desde esta carpeta (Codigo/)
.\compilar.ps1                              # build
.\compilar.ps1 -Clean                       # build limpio
.\compilar.ps1 -Flash -Port COM3            # build + flash
.\compilar.ps1 -Flash -Port COM3 -Monitor   # build + flash + monitor
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
Codigo/
├── CMakeLists.txt          # Proyecto: maquina_expendedora
├── compilar.ps1            # Sincroniza a ruta ASCII y compila/flashea
├── ESTADO_DEL_PROYECTO.md  # Estado actual y pendientes (handoff para IA)
├── sdkconfig.defaults      # Target ESP32, tick 1 ms, LVGL, XPT2046
└── main/
    ├── CMakeLists.txt      # Dependencias: freertos, driver, esp_lcd, lvgl, ...
    ├── idf_component.yml   # Dependencias gestionadas (LVGL, ST7796, XPT2046)
    ├── board_config.h      # Mapa de pines y constantes de hardware
    ├── main.c              # app_main: I2C, SD, callback de despacho y 3 tareas
    ├── motor_control.h/.c  # PCF8574 (0x20 relés / 0x21 sensores), cola, eventos
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
- **Motores:** relé activo en LOW, sensor de fin de carrera a GND. Secuencia:
  verificar reposo → energizar → esperar salida de reposo → esperar retorno →
  apagar siempre → liberar Mutex.
- **Eventos de despacho:** `motor_task` emite `MOTOR_EVENT_UNIT_DONE` por cada
  unidad entregada, `MOTOR_EVENT_REQUEST_FAILED` si un motor falla y
  `MOTOR_EVENT_IDLE` al vaciar la cola. `main.c` los adapta a la GUI.
- **GUI:** grid 2×3 con imagen, nombre, precio y `[-] Cantidad:N [+]`; panel
  inferior con saldo y botón *Confirmar*; modal de resumen `[Sí] [No]` y toast.
  El estado (saldo, stock, cantidades, pedido) está protegido por un Mutex y
  **todas** las llamadas a LVGL ocurren en `task_gui`.
- **Cobro diferido (corrección del riesgo #3):** al confirmar, la GUI **solo
  encola** el pedido; no descuenta saldo ni stock. El descuento ocurre en
  `gui_display_on_unit_dispatched()`, únicamente cuando el motor confirma una
  vuelta con el sensor en LOW. Si un motor falla por timeout, el pedido se
  descarta, el saldo queda intacto y el stock no se reduce.

## Pendiente (iterativo)

1. Verificar contra el esquemático los pines de la TFT/táctil
   (`TODO_VERIFICAR_PINES`) y los I2C (21/22).
2. Calibrar el táctil (`BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`, `TODO_CALIBRAR`).
3. Añadir una rutina de *homing*: el despacho exige que el sensor de fin de
   carrera esté en LOW (mecanismo en reposo) antes de energizar el motor.
4. Cargar precios/stock reales desde la MicroSD (hoy se usan precios de
   demostración con `GUI_USE_DEMO_PRICES 1`).
5. Hacer atómico el encolado (hoy se encola producto por producto).
6. Añadir NVS para persistir stock/ventas y telemetría Wi-Fi (V2.0).
