# Estado del proyecto — Máquina Expendedora (6 canales)

> **Propósito de este documento:** servir como *handoff* completo para una IA (o
> un desarrollador) que continúe el proyecto. Describe qué existe hoy, cómo está
> construido, qué funciona, qué falta y en qué orden conviene atacarlo.
>
> **Última actualización:** 2026-10-24
> **Estado global:** fase **P0 (interfaz gráfica) implementada, compilando sin
> errores ni warnings y versionada en Git**. El grid 2×3, el táctil, el modal y
> el cobro diferido por unidad están integrados. Queda la puesta en marcha en
> hardware real (pines, calibración táctil), precios/stock reales y las tareas
> P1 (homing, encolado atómico, NVS).

### Resumen rápido: qué está hecho y qué falta

**Hecho y verificado (compila):**

- [x] Monedero por ISR en IO35 con ventana de ráfaga y antirrebote.
- [x] Despacho I2C de 6 motores con Mutex (un motor a la vez) y sensores.
- [x] Eventos de despacho (`UNIT_DONE` / `REQUEST_FAILED` / `IDLE`) y descarte
      de la cola ante fallo.
- [x] GUI real: ST7796 320×480 por `esp_lcd`, XPT2046, LVGL 8.4, grid 2×3 con
      BMP de la MicroSD, panel de saldo y modal `[Sí] [No]`.
- [x] Concurrencia: Mutex de estado y LVGL solo desde `task_gui`.
- [x] Cobro diferido: confirmar solo encola; se cobra por unidad entregada.
- [x] Build limpio (573 216 B, 45 % libre) y `Codigo/` commiteado.

**Falta (en orden de prioridad):**

1. Probar en la placa real y verificar pines de TFT/táctil (`TODO_VERIFICAR_PINES`).
2. Calibrar el táctil (`BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`).
3. Cargar precios y stock reales (hoy demo con `GUI_USE_DEMO_PRICES 1`).
4. Rutina de *homing* al arrancar (P1).
5. Encolado atómico de la compra (P1).
6. Persistencia en NVS de stock/ventas (P1) y extras V2.0.

### Últimos cambios de esta sesión

1. **API táctil actualizada:** se sustituyó la API obsoleta
   `esp_lcd_touch_get_coordinates()` por `esp_lcd_touch_get_data()`; la build
   quedó sin warnings.
2. **Ruta de imágenes LVGL corregida:** el driver `LV_FS_STDIO` concatena
   `LV_FS_STDIO_PATH` sin separador, así que la ruta ahora es
   `S:/productos/pN.bmp` (antes se generaba `/sdcardproductos/pN.bmp` y las
   imágenes no habrían cargado).
3. **Lifetime del puntero de imagen:** `lv_img_set_src()` no copia la cadena.
   La ruta se guarda en `gui_cell_widgets_t.image_src[96]` (estático), no en la
   pila de `create_cell()`.
4. **Documentación y Git:** README y este documento actualizados; `Codigo/`
   commiteado en 3 commits (`6d5ec5e`, `c118960`, `37b05c1`).

---

## 1. Datos clave

| Dato | Valor |
| :--- | :--- |
| Proyecto | `maquina_expendedora` |
| Placa | ESP32-32E 4.0" (ESP32 clásico, pantalla TFT 4.0" integrada) |
| Canales | 6 (un motor de 12 V por canal) |
| Framework | **ESP-IDF v5.5.6** + FreeRTOS, **C nativo** (no Arduino) |
| Target | `esp32` |
| Flash | 4 MB |
| Tick de FreeRTOS | 1000 Hz (1 ms) |
| Fuente de verdad de la arquitectura | `../Documentacion/Documentacion Tecnica/documento_de_arquitectura_tecnico.md` |
| Carpeta de código | `Codigo/` (este directorio) |
| Repositorio Git | Sí. `Codigo/` versionado en `main` (commits `6d5ec5e`, `c118960`, `37b05c1`); árbol de trabajo limpio |

### Restricciones que no se deben romper

1. **No usar Arduino.** Solo ESP-IDF + FreeRTOS en C.
2. **Un solo motor a la vez.** Obligatorio por el Mutex de despacho (protege la
   fuente conmutada de 12 V de picos de corriente).
3. **La MicroSD es obligatoria** para los activos gráficos (no cargar imágenes
   en Flash/RAM interna).
4. **El monedero entra por IO35**, que es *input-only* y **no tiene pull-up
   interno**: requiere pull-up externo de 10 kΩ a 3.3 V. El firmware **no debe
   habilitar el pull-up por software** (ya está el externo y cargaría el opto).
5. Los PCF8574 deben alimentarse a **3.3 V**, no 5 V.
6. **El saldo y el stock solo se descuentan cuando el motor confirma una vuelta**
   (sensor en LOW). Confirmar la compra únicamente encola el pedido.

---

## 2. Hardware y cableado

### 2.1 Monedero (Pesos Dominicanos)

| Moneda | Pulsos | Valor interno (centavos) |
| :--- | :---: | ---: |
| RD$ 1 | 1 | 100 |
| RD$ 5 | 3 | 500 |
| RD$ 10 | 5 | 1000 |
| RD$ 25 | 7 | 2500 |

- Señal por optoacoplador PC817 sobre **IO35**, activa en **LOW** (flanco de
  bajada).
- Ventana de ráfaga: **300 ms** sin pulsos ⇒ la moneda terminó.
- Antirrebote por pulso: **20 ms**.

### 2.2 Bus I2C y expansores PCF8574

| Dirección | Función | Bits |
| :--- | :--- | :--- |
| `0x20` | Módulo de relés (motores 12 V) | P0..P5 → canales 1..6 |
| `0x21` | Sensores de fin de carrera | P0..P5 → canales 1..6 |

- I2C0, **SDA = IO21, SCL = IO22** (`VERIFICAR` contra el esquemático),
  **100 kHz**.
- Relé **activo en LOW**; el driver invierte el bit al escribir.
- Sensor: micro-switch cierra a **GND** ⇒ **LOW = final de carrera alcanzado**.
- En reposo se asume el sensor en **LOW** (mecanismo en posición inicial).

### 2.3 MicroSD (SPI)

| Señal | GPIO |
| :--- | :--- |
| CS | IO5 |
| CLK | IO18 |
| MISO | IO19 |
| MOSI | IO23 |

- **VSPI confirmado** (`SPI3_HOST`), montaje FAT en `/sdcard`.
- Se usa `esp_vfs_fat_sdspi_mount()`. La SD **no** está en el periférico SDMMC
  nativo, así que no hay que cambiar a `esp_vfs_fat_sdmmc_mount()`.

### 2.4 Pantalla TFT y táctil

| Elemento | Valor |
| :--- | :--- |
| Controlador | **ST7796**, 320×480 vertical, RGB565 |
| Bus de pantalla | **SPI2 (HSPI)**, 40 MHz |
| Táctil | **XPT2046** resistivo, comparte SPI2, CS propio |
| Retroiluminación | LEDC PWM |

Pines (marcados `TODO_VERIFICAR_PINES` en `board_config.h`, corresponden al
pinout habitual de la ESP32-32E 4.0"):

| Señal | GPIO |
| :--- | :--- |
| TFT SCLK | IO14 |
| TFT MOSI | IO13 |
| TFT MISO | IO12 |
| TFT CS | IO15 |
| TFT DC | IO2 |
| TFT RST | NC (atado a EN en la placa) |
| TFT BL | IO27 |
| Táctil CS | IO33 |
| Táctil IRQ | NC (modo sondeo) |

- El táctil se lee por **polling** (`BOARD_TOUCH_PIN_IRQ = GPIO_NUM_NC`) para no
  registrar una IRQ sin manejador.
- Calibración pendiente: `BOARD_TOUCH_SWAP_XY`, `MIRROR_X`, `MIRROR_Y`
  (`TODO_CALIBRAR`). Si el eje X/Y sale invertido, cambiar solo esas macros.
- El panel se crea con `esp_lcd_new_panel_io_spi()` + `esp_lcd_new_panel_st7796()`,
  con orden **BGR** e inversión de color activados (verificables en hardware).

---

## 3. Entorno de desarrollo instalado (este equipo)

| Componente | Ruta | Versión |
| :--- | :--- | :--- |
| ESP-IDF | `C:\Users\asus\esp\esp-idf` | v5.5.6 |
| Toolchain + Python env | `C:\Users\asus\esp\espressif` (`IDF_TOOLS_PATH`) | xtensa-esp-elf 14.2.0 |
| Python para IDF | `C:\Users\asus\AppData\Local\Programs\Python\Python312` | 3.12.10 |
| Build generado | `C:\Users\asus\esp\build_maquina` | — |
| Copia de trabajo ASCII | `C:\Users\asus\esp\maquina_expendedora` | — |

### ⚠️ Problema importante: ruta con acentos

La ruta real del proyecto contiene acentos y espacios
(`...\Tecnológico de Las Américas...\Proyecto Final Maquina Expendedora\Codigo`).
Con esa ruta:

- `ccache` aborta con `filesystem error: Cannot convert character sequence:
  Illegal byte sequence`.
- `kconfiglib` no puede abrir `sdkconfig` (la ruta se corrompe a `\u251c\u2502...`).

**Solución adoptada:** compilar desde una copia en ruta ASCII.
El script `compilar.ps1` sincroniza `Codigo/` → `C:\Users\asus\esp\maquina_expendedora`
y compila allí con el build fuera de OneDrive.

```powershell
cd Codigo
.\compilar.ps1                              # build
.\compilar.ps1 -Clean                       # build limpio
.\compilar.ps1 -Flash -Port COM3            # build + flash
.\compilar.ps1 -Flash -Port COM3 -Monitor   # build + flash + monitor
```

> **Si eres una IA trabajando en este repo:** edita siempre los archivos en la
> ruta real de OneDrive (la del workspace), y usa `compilar.ps1` para verificar.
> No edites la copia de `C:\Users\asus\esp\maquina_expendedora`: se sobrescribe
> en cada build.

### Resultado del build actual

```
Project build complete.
maquina_expendedora.bin → 573 216 bytes (0x8bf20)
Partición app 0x100000 → 45 % libre
Sin warnings del compilador en los archivos del proyecto.
```

> La compilación usa LVGL 8.4.0 y los drivers gestionados de `esp_lcd`; el
> primer build descarga ~26 MB de componentes y tarda varios minutos.

---

## 4. Estructura de archivos

```
Codigo/
├── CMakeLists.txt              # Proyecto "maquina_expendedora"
├── sdkconfig.defaults          # Target esp32, 4 MB, tick 1 ms, LVGL, XPT2046
├── compilar.ps1                # Sync a ruta ASCII + build/flash
├── README.md                   # Guía rápida de uso
├── ESTADO_DEL_PROYECTO.md      # Este documento
└── main/
    ├── CMakeLists.txt          # Dependencias (ver abajo)
    ├── idf_component.yml       # LVGL, esp_lcd_st7796, esp_lcd_touch_xpt2046
    ├── board_config.h          # Pines, direcciones I2C, polaridades
    ├── main.c                  # app_main: I2C, SD, callback de despacho, tareas
    ├── motor_control.h/.c      # Relés, sensores, cola, Mutex y eventos
    ├── coin_acceptor.h/.c      # ISR IO35 y decodificación de ráfagas
    └── gui_display.h/.c        # ST7796 + XPT2046 + LVGL, grid, modal, cobro
```

### Dependencias (`main/CMakeLists.txt`)

Base: `freertos`, `driver`, `esp_timer`, `sdmmc`, `fatfs`, `esp_lcd`, `lvgl`.
Gestionadas vía `main/idf_component.yml`: `esp_lcd_st7796`, `esp_lcd_touch`,
`esp_lcd_touch_xpt2046` (se exponen como componentes normales).
Además se añaden condicionalmente si existen (compatibilidad IDF 5.2 → 6.x):
`esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_spi`, `esp_driver_sdspi`,
`esp_driver_ledc`.

### Configuración LVGL (`sdkconfig.defaults`)

- `LV_CONF_SKIP=y`: no se mantiene un `lv_conf.h` propio; todo va por Kconfig.
- RGB565 con `LV_COLOR_16_SWAP` (el bus SPI del ST7796 recibe el byte alto
  primero).
- Heap de LVGL: 64 KB.
- FS stdio con letra `S` y raíz `/sdcard`; decodificador BMP habilitado.
- Fuentes Montserrat 12/14/16/20.
- El tick **no** usa `LV_TICK_CUSTOM` (el Kconfig de LVGL 8.4 no expone
  `LV_TICK_CUSTOM_SYS_TIME_EXPR`): `gui_task` inyecta `lv_tick_inc()` cada ciclo.

---

## 5. Qué está implementado (y funciona)

### 5.1 `main.c` — orquestación

Orden de arranque en `app_main()`:

1. Crea `g_dispatch_mutex` (Mutex global de despacho) **antes** de cualquier tarea.
2. Crea el bus I2C (`i2c_new_master_bus`, driver moderno `driver/i2c_master.h`).
3. `motor_control_init(i2c_bus)` → configura ambos PCF8574 y la cola.
4. `coin_acceptor_init()` + callback `on_coin_inserted`.
5. `gui_display_mount_sd()` y luego `gui_display_init()`.
   - Si la SD falla: solo warning, el sistema sigue (placeholder `IMG`).
   - Si la GUI falla: solo warning, el sistema sigue (sin pantalla).
6. `motor_control_set_event_callback(on_motor_event, NULL)` **antes** de crear
   `task_despacho`.
7. Crea las 3 tareas.

| Tarea | Prioridad | Pila | Función |
| :--- | :---: | :---: | :--- |
| `task_monedero` | 10 | 4096 B | Decodifica ráfagas y acredita saldo |
| `task_despacho` | 8 | 4096 B | Consume la cola, mueve motores y emite eventos |
| `task_gui` | 5 | 16384 B | `lv_timer_handler()`, tick, táctil y refresco |

- `g_dispatch_mutex` se **define** en `main.c` y se declara `extern` en
  `motor_control.h`.
- El callback del monedero suma saldo con `gui_display_add_balance_cents()` y
  muestra mensaje si la ráfaga no corresponde a una moneda.
- `on_motor_event()` es un adaptador: traduce `MOTOR_EVENT_*` a llamadas de la
  GUI (`gui_display_on_unit_dispatched`, `..._on_dispatch_failed`,
  `..._on_dispatch_idle`) sin acoplar `motor_control` a la GUI.

### 5.2 `coin_acceptor` — monedero

- GPIO35 configurado como entrada con ISR por **flanco de bajada**.
- La ISR **solo cuenta pulsos** y los envía a una cola (no hace trabajo pesado).
- `coin_acceptor_task()` implementa la ventana de 300 ms: cada pulso reinicia el
  temporizador; al cerrar la ráfaga valida y llama al callback con
  `(pulses, cents, ctx)`.
- `coin_acceptor_pulses_to_cents()` mapea 1/3/5/7 → 100/500/1000/2500; cualquier
  otra cantidad devuelve 0.
- Antirrebote de 20 ms por pulso.

### 5.3 `motor_control` — despacho

- Registro sombra de relés para no alterar los otros canales al escribir el
  PCF8574.
- Mutex interno `s_i2c_mutex` para serializar el bus I2C.
- Cola FIFO de despacho (`motor_request_t { channel, quantity }`, longitud 8).
- `motor_control_dispatch_one()`:

  1. Toma `g_dispatch_mutex` (timeout configurable).
  2. Verifica que el canal esté en reposo (sensor LOW). Si no, rechaza con
     `ESP_ERR_INVALID_STATE`.
  3. Energiza el relé.
  4. Espera que el sensor se libere (arranque) — timeout **3000 ms**.
  5. Espera que vuelva a LOW (vuelta completa) — timeout **20000 ms**.
  6. **Apaga el relé siempre** (`cleanup`), incluso con error/timeout.
  7. Pausa de asentamiento **250 ms** y libera el Mutex.

- `motor_task()` procesa la cola en orden; entre unidades del mismo canal espera
  **400 ms**.
- Emite eventos al callback registrado (desde el contexto de `motor_task`):
  - `MOTOR_EVENT_UNIT_DONE` tras cada vuelta confirmada (sensor LOW).
  - `MOTOR_EVENT_REQUEST_FAILED` si una unidad falla; incluye el código de error
    y cuántas unidades quedaron sin entregar.
  - `MOTOR_EVENT_IDLE` cuando la cola queda vacía.
- Ante un fallo, `motor_task` **descarta el resto de la cola** (el pedido se
  cancela) y luego emite `IDLE`. Las unidades ya entregadas permanecen cobradas.
- `motor_control_cancel_pending()` permite a la GUI cancelar lo que aún no se
  despacha.
- `motor_control_is_busy()` indica si hay despacho en curso o cola pendiente.

### 5.4 `gui_display` — interfaz real (ST7796 + XPT2046 + LVGL)

- Modelo de 6 productos: nombre, ruta de imagen (`/sdcard/productos/pN.bmp`),
  precio, stock y cantidad seleccionada.
- Valores actuales: **stock = 10** y **precios de demostración** con
  `GUI_USE_DEMO_PRICES 1` (RD$ 25/15/10/20/35/50) para poder probar el flujo.
- Reglas implementadas:
  - Cantidad entre 0 y `min(stock, 10)`.
  - `[-]`/`[+]` ajustan y recortan contra stock.
  - Cambiar stock recorta la cantidad seleccionada.
  - Saldo nunca negativo; total = Σ(cantidad × precio).
  - El modal **no se abre** si no hay unidades, si el total es 0 o si el saldo
    no alcanza (muestra un toast).
- `gui_display_init()`:
  - Inicializa SPI2, panel ST7796 (`esp_lcd`), retroiluminación LEDC y XPT2046.
  - Registra display e input en LVGL, crea el grid 2×3 y el panel inferior.
  - Crea el Mutex de estado antes de cualquier acceso concurrente.
- `task_gui` inyecta `lv_tick_inc()`, llama a `lv_timer_handler()` y refresca
  los widgets solo cuando el estado cambia (notificación + snapshot).
- Concurrencia: `s_state_mutex` protege saldo, stock, cantidades, modal y pedido.
  `task_monedero` y `motor_task` solo mutan estado y despiertan a `task_gui`;
  **ninguna** llamada a LVGL ocurre fuera de `task_gui`.
- `gui_display_confirm_purchase()`:
  - Valida modal abierto, stock, total > 0 y saldo suficiente.
  - Limpia cantidades y registra el pedido en curso.
  - **Solo encola** en `task_despacho`; **no** descuenta saldo ni stock.
  - Si el encolado falla a mitad, cancela las unidades no encoladas y no las
    cobra.
- `gui_display_on_unit_dispatched()` es el **único punto de cobro**: resta el
  precio al saldo y reduce el stock en 1 por cada unidad confirmada por el motor.
- `gui_display_on_dispatch_failed()` marca el pedido como fallido, pone a cero
  las unidades reservadas y muestra un toast; el saldo de lo no entregado queda
  intacto.

### 5.5 Flujo actual de una compra

```
Moneda → ISR IO35 → cola de pulsos → task_monedero (ventana 300 ms)
      → callback → saldo += cents

Usuario → ajusta cantidades → "Confirmar" → modal → [Sí]
      → gui_display_confirm_purchase(): valida y ENCOLA (no cobra)
      → task_despacho mueve motores uno a uno
      → por cada vuelta OK: MOTOR_EVENT_UNIT_DONE
      → gui_display_on_unit_dispatched(): saldo -= precio, stock -= 1
      → si un motor falla: MOTOR_EVENT_REQUEST_FAILED
         → se descarta la cola; lo no entregado no se cobra ni baja stock
```

---

## 6. Qué falta por hacer (priorizado)

### P0 — Bloqueante para que la máquina sea usable

- [x] **Integrar la pantalla real (`esp_lcd`)**
  - ST7796 320×480 por SPI2 con `esp_lcd_new_panel_io_spi` +
    `esp_lcd_new_panel_st7796`, retroiluminación por LEDC.
  - Pines en `board_config.h` marcados `TODO_VERIFICAR_PINES`.
  - *Criterio:* la pantalla enciende y muestra el grid.

- [x] **Integrar LVGL y el táctil**
  - LVGL 8.4.0 por Kconfig + XPT2046 en modo sondeo.
  - Grid 2×3, botones `[-] [+]`, contador `Cantidad:N`, panel de saldo y modal
    `Confirmar compra [Sí] [No]`.
  - `lv_timer_handler()` + `lv_tick_inc()` en `task_gui`.
  - *Criterio:* se puede seleccionar producto y confirmar solo con el dedo
    (pendiente de calibrar el táctil en la placa real).

- [x] **Concurrencia de la GUI**
  - `s_state_mutex` protege saldo, stock, cantidades, modal y pedido.
  - `task_monedero` y `motor_task` solo mutan estado y notifican a `task_gui`;
    LVGL se usa exclusivamente en `task_gui`.
  - *Criterio:* sin carreras ni *asserts* de LVGL al insertar monedas mientras
    se pulsa la pantalla.

- [ ] **Configurar precios y stock reales**
  - Hoy hay precios de demostración (`GUI_USE_DEMO_PRICES 1`) y stock fijo = 10.
  - Cargar productos desde la MicroSD (p. ej. `/sdcard/productos.json`) o
    definir tabla en código.
  - *Criterio:* 6 productos con nombre, precio y stock editables sin recompilar
    (preferible) o al menos en un único sitio.

- [ ] **Calibrar el táctil y verificar pines**
  - Ajustar `BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y` y
    `BOARD_TFT_USE_BGR_ORDER/INVERT_COLOR` con la pantalla real.
  - Confirmar los pines `TODO_VERIFICAR_PINES` contra el esquemático.

### P1 — Robustez y consistencia

- [ ] **Rutina de *homing***
  - Hoy, si un canal no está en reposo, el despacho se rechaza para siempre.
  - Al arrancar: leer sensores y, si un canal no está en LOW, girar el motor
    hasta encontrarlo (con timeout y límite de intentos).
  - *Criterio:* tras un corte de energía a mitad de vuelta, el sistema se
    recupera solo.

- [x] **Descontar stock/saldo solo al confirmar el despacho**
  - Implementado **por unidad**: `motor_task` emite `MOTOR_EVENT_UNIT_DONE`
    cuando el sensor confirma la vuelta y solo entonces la GUI cobra.
  - Si un motor falla, se descarta el resto de la cola y lo no entregado no se
    cobra ni reduce stock.
  - *Pendiente:* decidir si un pedido parcialmente entregado debe
    reembolsarse por completo (hoy las unidades entregadas quedan cobradas).

- [ ] **Encolado atómico**
  - `gui_display_confirm_purchase()` encola producto por producto; si uno falla,
    los anteriores ya quedaron encolados.
  - Validar capacidad total de la cola antes de encolar o usar una sola
    solicitud con lista de ítems.
  - *Criterio:* una compra se acepta completa o se rechaza completa.

- [ ] **Persistencia en NVS**
  - Guardar stock, ventas/recaudación y configuración entre reinicios.
  - *Criterio:* apagar y encender conserva stock y contadores.

- [ ] **Modo administración**
  - Rellenar stock, cambiar precios, ver ventas, probar motores/sensores.
  - Puede ser una pantalla oculta (p. ej. pulsación larga en una esquina) o por
    SD.

### P2 — Extras / V2.0 (según documento de arquitectura §7)

- [ ] Telemetría Wi-Fi (reportes de ganancias y stock a app/BD).
- [ ] Audio por *Speaker Interface* (moneda aceptada, error, etc.).
- [ ] Lector RFID por SPI para tarjetas corporativas.
- [ ] Registro de eventos/errores en la MicroSD.

### Calidad / tooling (transversal)

- [x] Primer commit de `Codigo/` (ya versionado en `main`).
- [x] `.gitignore` excluye `build/`, `sdkconfig`, `managed_components/`, etc.
- [ ] Añadir pruebas de la lógica pura (mapeo de pulsos, total, stock) con
      Unity o tests de host; hoy no hay ninguno.
- [x] `sdkconfig.defaults` con LVGL, FS stdio, BMP, fuentes y XPT2046.
- [ ] Añadir CI (build automatizado) si el proyecto se publica.

---

## 7. Riesgos, bugs conocidos y deuda técnica

| # | Problema | Impacto | Dónde |
| :---: | :--- | :--- | :--- |
| 1 | Pines de TFT/táctil sin verificar (`TODO_VERIFICAR_PINES`) | Pantalla/táctil podría no responder | `board_config.h` |
| 2 | Precios de demostración y stock fijo | No reflejan inventario real | `gui_display.c` |
| 3 | ~~Stock/saldo se descuentan antes del motor~~ **Resuelto** | Cobro por unidad confirmada | `gui_display_on_unit_dispatched()` |
| 4 | Encolado no atómico | Compra parcialmente aceptada si la cola se llena | `gui_display_confirm_purchase()` |
| 5 | Sin *homing*; canal fuera de reposo queda bloqueado | Máquina inoperativa tras fallo | `motor_control_dispatch_one()` |
| 6 | ~~Estado de GUI sin mutex~~ **Resuelto** | `s_state_mutex` protege todo el estado | `gui_display.c` |
| 7 | Pines I2C (21/22) sin verificar contra el esquemático | Puede no comunicarse | `board_config.h` |
| 8 | Táctil sin calibrar (ejes/espejo) | Pulsaciones en zona equivocada | `BOARD_TOUCH_*` |
| 9 | Sin pruebas automatizadas ni CI | Regresiones silenciosas | repo |
| 10 | ~~`Codigo/` sin commitear~~ **Resuelto** | Ya versionado en `main` | repo |

### Notas de implementación delicadas

- **GPIO35 es input-only y sin pull-up interno.** Si no hay pull-up externo, la
  ISR puede dispararse de forma errática o no dispararse. No habilitar el
  pull-up por software: cargaría el optoacoplador.
- **El relé se apaga en el bloque `cleanup`.** No eliminar esa garantía al
  refactorizar: es la protección contra motores girando indefinidamente.
- **`g_dispatch_mutex` debe existir antes de crear `task_despacho`.**
- **El cobro vive en un solo sitio:** `gui_display_on_unit_dispatched()`. No
  volver a descontar saldo/stock en `gui_display_confirm_purchase()`.
- **LVGL solo se toca desde `task_gui`.** Los callbacks de `motor_task` y
  `task_monedero` únicamente mutan estado protegido y llaman a `ui_notify()`.
- **No usar `esp_lcd_touch_get_coordinates()`**: está deprecado en
  `esp_lcd_touch` 1.2.x; se usa `esp_lcd_touch_get_data()`.
- El driver I2C usado es el **moderno** (`driver/i2c_master.h`), disponible desde
  IDF 5.2. No volver al driver legacy.
- El `sdkconfig` se genera localmente y está ignorado por Git; los valores por
  defecto viven en `sdkconfig.defaults`.
- `compilar.ps1` conserva `managed_components/` y `dependencies.lock` entre
  sincronizaciones; **no** conserva `sdkconfig`, para reaplicar los defaults.

---

## 8. Cómo verificar el estado actual

```powershell
cd Codigo
.\compilar.ps1
```

Debe terminar con:

```
Project build complete.
Build OK: C:\Users\asus\esp\build_maquina\maquina_expendedora.bin
```

Para flashear (requiere placa conectada):

```powershell
.\compilar.ps1 -Flash -Port COM3 -Monitor
```

Salir del monitor: `Ctrl + ]`.

Al arrancar, el log esperado es similar a:

```
app_main: Maquina Expendedora - 6 canales (ESP32-32E 4.0")
app_main: Mutex de despacho creado (un motor a la vez)
app_main: Bus I2C0 listo (SDA=IO21, SCL=IO22, 100000 Hz)
gui_display: MicroSD montada en /sdcard
gui_display: Tactil XPT2046 listo (CS=IO33, 1000000 Hz)
gui_display: ST7796 listo (320x480, SPI2, 40 MHz)
gui_display: GUI lista: ST7796 320x480 + XPT2046 + LVGL 8.4
gui_display: task_gui iniciada (LVGL + tactil)
app_main: Arranque completado. Sistema en espera de monedas.
```

Si la SD o la pantalla no están conectadas, se emite un warning y el sistema
sigue arrancando (sin imágenes o sin interfaz, respectivamente).

### Checklist de puesta en marcha en la placa real

1. **Flashear** con `.\compilar.ps1 -Flash -Port COMx -Monitor`.
2. **Confirmar el log:** debe aparecer `ST7796 listo`, `Tactil XPT2046 listo` y
   `MicroSD montada`. Si falta alguno, revisar pines/alimentación.
3. **Colores y orientación:** si los colores salen invertidos, cambiar
   `BOARD_TFT_INVERT_COLOR`; si rojo/azul están cambiados, cambiar
   `BOARD_TFT_USE_BGR_ORDER`; si la imagen está girada/espejada, ajustar
   `BOARD_TFT_SWAP_XY/MIRROR_X/MIRROR_Y`.
4. **Táctil:** pulsar cada esquina. Si el eje X/Y está intercambiado o invertido,
   ajustar `BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`. Repetir hasta que el botón
   pulsado coincida con el dibujado.
5. **Imágenes:** copiar `p1.bmp`…`p6.bmp` (64×64) a `/sdcard/productos/` y
   reiniciar. Sin archivos se muestra el placeholder `IMG`.
6. **Monedero:** insertar RD$ 1/5/10/25 y verificar que el saldo sube en pantalla
   con el valor correcto.
7. **Compra de prueba:** seleccionar 1 unidad, confirmar y comprobar que el
   saldo baja **solo cuando el motor termina la vuelta**.
8. **Fallo controlado:** desconectar un sensor o bloquear un motor y verificar
   que el pedido se cancela sin cobrar las unidades no entregadas.

---

## 9. Preguntas abiertas (datos que faltan)

1. ¿Los pines de la TFT/táctil de esta placa coinciden con los asumidos
   (`TODO_VERIFICAR_PINES`)? Confirmar contra el esquemático.
2. ¿SDA/SCL reales de la placa son IO21/IO22?
3. ¿Los módulos de relé conmutan de forma fiable con 3.3 V en la entrada?
   (Algunos módulos de 5 V con optoacoplador necesitan adaptador de nivel).
4. ¿Precios y stock reales de los 6 productos?
5. ¿Se requiere modo administración? ¿Cómo se accede (pantalla, SD, Wi-Fi)?
6. ¿La versión 1.0 debe incluir telemetría Wi-Fi o se deja para V2.0?
7. ¿Un pedido parcialmente entregado debe reembolsarse completo o solo cobrar
   las unidades que salieron? (Hoy se cobran solo las entregadas).

---

## 10. Definición de "MVP terminado"

Se considerará la versión 1.0 funcional cuando:

1. La pantalla muestra el grid 2×3 con imágenes desde la SD.
2. El táctil permite ajustar cantidades y confirmar con modal.
3. Los precios y el stock son configurables sin recompilar.
4. Insertar monedas actualiza el saldo en pantalla en < 1 s.
5. Confirmar una compra despacha los productos **uno a uno**, sin reinicios.
6. Si un despacho falla, **no se cobra** lo no entregado (hoy ya se cumple por
   unidad; falta definir el reembolso del pedido parcial).
7. Tras un corte de energía, el sistema hace *homing* y vuelve a operar.
8. Stock y recaudación sobreviven a un reinicio (NVS).

---

## 11. Instrucciones para la IA que continúe

- **Lee primero** el documento de arquitectura
  (`../Documentacion/Documentacion Tecnica/documento_de_arquitectura_tecnico.md`)
  y este archivo.
- **Respeta** las restricciones de §1 (C nativo, ESP-IDF, un motor a la vez,
  SD obligatoria, IO35 con pull-up externo).
- **No compiles directamente** en la ruta de OneDrive: usa `compilar.ps1`.
- **No edites** `C:\Users\asus\esp\maquina_expendedora` (copia temporal).
- Antes de cambiar pines, confirma con el usuario/el esquemático: los de la
  TFT/táctil están marcados `TODO_VERIFICAR_PINES` y los I2C como `VERIFICAR`.
- La GUI ya está implementada: **no bloquees `task_gui`** con esperas largas y
  no llames a LVGL desde `task_monedero` ni desde `motor_task`.
- Si tocas el flujo de compra, mantén el cobro **solo** en
  `gui_display_on_unit_dispatched()`.
- Mantén el patrón actual: un `.h` con API documentada y un `.c` con
  implementación; logs con `ESP_LOGI/W/E` y `TAG` por módulo.
- Cualquier función nueva que energice un motor **debe** tomar
  `g_dispatch_mutex`.
- Si añades archivos, actualiza `main/CMakeLists.txt` y este documento.
