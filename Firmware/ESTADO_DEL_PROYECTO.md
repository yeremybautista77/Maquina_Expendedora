# Estado del proyecto — Máquina Expendedora (6 canales)

> **Propósito de este documento:** servir como *handoff* completo para una IA (o
> un desarrollador) que continúe el proyecto. Describe qué existe hoy, cómo está
> construido, qué funciona, qué falta y en qué orden conviene atacarlo.
>
> **Última actualización:** 2026-10-30
> **Estado global:** **fase P1 de software completada y modo administración
> ampliado** (inventario, precios mutables en NVS y prueba de motores) y se
> sube a GitHub al cierre de esta sesión. Pines confirmados, estrategia híbrida
> de motores, *homing* de M1–M4, NVS, layout Flex 2×3 y cobro diferido por
> unidad implementados; compila sin errores ni warnings
> (`maquina_expendedora.bin` = **613 664 B**, 41 % libre).
> **Pantalla validada en hardware:** flasheo por COM5 correcto, ST7796 +
> XPT2046 funcionando, táctil calibrado (rotación de 180° compensada) y colores
> corregidos (sin inversión). Pendiente: monedero, módulos I2C/motores,
> *homing*, NVS y modo administración en la placa real.

### Resumen rápido: qué está hecho y qué falta

**Hecho y verificado (compila y pantalla validada):**

- [x] Monedero por ISR en IO35 con ventana de ráfaga y antirrebote.
- [x] Filtro anti-saldo-fantasma en IO35: gracia de arranque, ancho de pulso
      válido y armado solo con la línea en reposo estable (ver §5.2).
- [x] Despacho I2C de 6 motores con Mutex (un motor a la vez). M1–M4 con sensor; M5–M6 por tiempo.
- [x] Eventos de despacho (`UNIT_DONE` / `REQUEST_FAILED` / `IDLE`) y descarte
      de la cola ante fallo.
- [x] GUI real: ST7796 320×480 por `esp_lcd`, XPT2046, LVGL 8.4, layout Flex
      2×3 (`ROW_WRAP`, celdas al 48 %) con BMP de la MicroSD, panel de saldo y
      modal `[Sí] [No]`.
- [x] Concurrencia: Mutex de estado y LVGL solo desde `task_gui`.
- [x] Cobro diferido: confirmar solo encola; se cobra por unidad entregada.
- [x] Encolado atómico: lote todo-o-nada con `s_queue_mutex` y
      `motor_control_check_capacity()`; si no cabe el pedido completo, se
      rechaza entero y la GUI pide reducir la cantidad.
- [x] Modo administración oculto: pulsación larga de 3 s en la esquina superior
      izquierda → panel con scroll de tres secciones: **inventario** (reset a 10),
      **precios** (6 productos, ± RD$ 5 y guardado en NVS) y **prueba de motores**
      (`Test M1`…`Test M6`, sin tocar el saldo).
- [x] Pines de hardware confirmados (I2C 32/25, TFT/táctil y MicroSD).
- [x] *Homing* secuencial de los canales M1–M4 al arrancar (timeout 10 s).
- [x] Persistencia en NVS de saldo, stock y precios (carga al arrancar; guardado
      por moneda, unidad entregada o cambio de precio).
- [x] GUI rediseñada: 2×3, cabecera nombre+precio, placeholder
      `[Espacio Imagen]`, `Cant: N`, `Saldo:`, `Monto a Pagar:` y `Confirmar`.
- [x] Build limpio (**613 664 B**, 41 % libre) y `Firmware/` versionado.

**Falta (en orden de prioridad):**

1. **Validar el resto de la placa real**: conectar monedero y módulos I2C/motores;
   probar *homing*, NVS, rechazo por cola llena y modo administración completo
   (precios y tests de motor). La pantalla, el táctil y los colores ya están
   validados.
2. Pruebas automatizadas y CI (transversal).
3. Extras V2.0 (telemetría Wi-Fi, audio, RFID, log en SD).
4. Ampliar el modo administración con historial de ventas/recaudación y
   diagnóstico de sensores (los precios y los tests de motor ya están hechos).

**Estado de las acciones de esta sesión:**

- [x] Expansión del modo administración: inventario, precios mutables en NVS y
      prueba de motores con marca `admin_test`.
- [x] Build limpio sin warnings → **613 664 B** (`0x95d20`), 41 % libre.
- [ ] Flasheo + monitor serie → **no ejecutado**: no hay ningún puerto COM
      presente (placa desconectada o cable solo de carga).

---

## 0. Preguntas para ti (responde debajo de cada una)

> Estas eran las dos decisiones pendientes de esta sesión. **Ambas respondidas**
> (ver abajo); el push se completó y el flasheo se intentó, pero no había placa
> conectada.

### Pregunta 1 — ¿Subo los commits a GitHub?

`main` tenía **commits locales sin subir** a `origin/main`
(`6d5ec5e`, `c118960`, `37b05c1`, `1d8c3e5`, `3fbefa7` y los de documentación
posteriores).

- Opciones: **sí, haz push** / **no, déjalo local** / **espera**.
- Si dices que sí, ejecuto `git push origin main` (no toco nada más).

**Respuesta:** Sí, haz push. Asegura los commits locales en `origin/main`.

**Ejecutado:** `git push origin main` → **OK** (`fbe3060..80f3282`).
`main` y `origin/main` quedaron sincronizados.

### Pregunta 2 — ¿Flasheamos ahora en la placa real?

Para probar homing, NVS y táctil necesito la placa conectada.

- Dime el **puerto COM** (p. ej. `COM3`) o si prefieres que lo detecte.
- ¿Quieres **monitor serie** para ver los logs de arranque? (sí/no)
- ¿O lo dejamos para después y seguimos solo con el código?

**Respuesta:** Sí, flashea y activa el monitor serie usando el puerto **COM3**
(o detéctalo automáticamente si COM3 falla).

**Ejecutado:** `.\compilar.ps1 -Flash -Port COM3 -Monitor` → el build fue
correcto, pero **esptool no pudo abrir COM3**:

```
A fatal error occurred: Could not open COM3, the port is busy or doesn't exist.
```

Se buscaron puertos automáticamente (`[System.IO.Ports.SerialPort]::GetPortNames()`
y `Get-PnpDevice -Class Ports -PresentOnly`): **no hay ningún COM presente**.
Para completar este paso hay que conectar la placa (y usar un cable de datos) y
volver a ejecutar el comando; si aparece otro puerto, sustituir `COM3` por ese.

---

### Últimos cambios de esta sesión (2026-10-25)

Commit `3fbefa7` — `feat(firmware): pines confirmados, homing, NVS y rediseno de la GUI`
(10 archivos, +722 / −258). El fix de layout y este documento van en `80f3282`.

1. **Pines confirmados** en `board_config.h`: I2C **SDA=IO32 / SCL=IO25**, TFT
   **14/13/12/15/2/NC/27**, táctil **CS=33 / IRQ=NC** y MicroSD
   **5/18/19/23**. Se eliminaron todas las etiquetas `TODO_VERIFICAR_PINES`.
2. **Homing** `motor_control_home_all()`: secuencial (un motor a la vez), toma
   `g_dispatch_mutex` y usa timeout de **10 s** por canal; se ejecuta al final de
   `motor_control_init()`. El relé se apaga siempre.
3. **NVS** (`nvs_flash`): namespace `vending`, claves `balance` (int64) y
   `stock` (blob de 6 B). Carga en `gui_display_init()`; guarda al acreditar
   monedas y en cada unidad cobrada.
4. **GUI rediseñada**: layout Flex **2×3** con `LV_FLEX_FLOW_ROW_WRAP` y celdas
   al **48 %** de ancho (fuerza el salto de línea tras cada pareja), cabecera
   `Producto N\nRD$ XX.00`, recuadro `[Espacio Imagen]`, `[-] Cant: N [+]`;
   panel inferior de **80 px** con `Saldo:`, `Monto a Pagar:` reactivo y
   `Confirmar` grande.
5. **Precios**: tabla compilada `k_product_prices` = **RD$ 40/50/60/70/80/90**;
   eliminado `GUI_USE_DEMO_PRICES`.
6. **Build**: `maquina_expendedora.bin` = **592 512 B** (`0x90a80`), **43 %
   libre**, sin warnings.
7. **Docs**: README y este documento actualizados.
8. **Corrección de layout (posterior a `3fbefa7`)**: el contenedor de productos
   pasó de `LV_LAYOUT_GRID` a **Flex `ROW_WRAP`** con celdas al **48 %** de ancho
   y **127 px** de alto, forzando el 2×3 real (antes los productos podían
   colapsar en una sola fila). Build de ese cambio: **589 936 B** (`0x90070`).
9. **P1 de software (sesión anterior)**: encolado atómico con `s_queue_mutex` +
   `motor_control_check_capacity()` + `motor_control_enqueue_batch()`, y modo
   administración oculto (pulsación larga de 3 s, overlay y reset de stock a 10
   con guardado en NVS). Build de esa fase: **592 512 B** (`0x90a80`).

### Expansión del modo administración (sesión actual, 2026-10-25)

1. **Precios mutables en NVS**: se eliminó `k_product_prices`; los precios viven
   en `s_products[i].price_cents` (protegido por `s_state_mutex`) y se cargan /
   guardan con la clave NVS `prices` (blob de 6 × `uint32_t` = 24 B). Si la clave
   no existe o es inválida se aplican los valores de fábrica
   **4000/5000/6000/7000/8000/9000** centavos.
2. **Panel de administración con scroll**: cabecera fija (título + cierre),
   línea de estado y contenedor Flex vertical con tres secciones.
   - **Inventario:** stock actual de los 6 canales y botón
     `Rellenar Stock (Reset a 10)` (persiste en NVS y mantiene el panel abierto).
   - **Precios:** una fila por producto con precio actual, `[-]`/`[+]` en pasos de
     **500 centavos (RD$ 5)** y botón `Guardar Precios`. Los cambios son un
     borrador local hasta pulsar guardar; el rango válido es RD$ 5.00–1000.00.
   - **Prueba de motores:** `Test M1`…`Test M6`. Cada test encola **una** unidad
     con `motor_control_enqueue_batch()` y `admin_test = true`.
3. **Separación venta real / test:** `motor_request_t` y `motor_event_t` llevan
   `bool admin_test`, propagado por `motor_control.c` y `main.c`. En
   `gui_display_on_unit_dispatched(channel, admin_test)`, un test **solo reduce
   stock en 1** (el producto cae) y guarda NVS; **no toca saldo, pedido ni
   modal**. Un fallo de test tampoco cancela ningún pedido.
4. **Guardas de seguridad:** los tests se rechazan si hay pedido activo, modal
   abierto, motor/cola ocupados o stock 0. El guardado de precios se rechaza con
   el aviso `Espere a que termine el despacho` si hay una compra en curso.
5. **Build:** **613 664 B** (`0x95d20`), **41 % libre**, sin warnings.

**Sesión anterior (fase P0):** API táctil migrada a `esp_lcd_touch_get_data()`,
rutas LVGL corregidas a `S:/productos/pN.bmp` y `image_src` estático para
`lv_img_set_src()`.

### Tabla de estado por bloque

| Bloque | Estado | Qué falta / riesgo |
| :--- | :---: | :--- |
| Pines de hardware | ✅ Confirmado | — |
| Monedero (ISR IO35) | ✅ Implementado | Filtro anti-fantasma probado sin monedero; falta validar monedas reales |
| Despacho I2C + Mutex | ✅ Implementado | Validar con motores reales |
| *Homing* de arranque | ✅ Implementado | Probar con motor fuera de reposo |
| Persistencia NVS | ✅ Implementado | Probar apagón/reinicio real |
| GUI 2×3 + panel inferior | ✅ Implementado | Táctil y colores validados en placa |
| Cobro diferido por unidad | ✅ Implementado | Decidir reembolso de pedido parcial |
| Encolado atómico | ✅ Implementado | Probar rechazo con la cola llena en placa |
| Modo administración | ✅ Implementado | Panel completo: inventario, precios y test de motores |
| Pruebas + CI | ❌ Falta (P2) | Sin tests automatizados |
| V2.0 (Wi-Fi, audio, RFID) | ❌ Falta (P2) | Según arquitectura §7 |
| Push a GitHub | ✅ Hecho | `origin/main` incluye la expansión del modo administración |
| Flasheo + monitor | ✅ Hecho | COM5, ESP32-D0WD-V3, GUI validada |

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
| Carpeta de código | `Firmware/` (este directorio) |
| Repositorio Git | Sí. `main` sincronizado con `origin/main` e incluye la expansión del modo administración |

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
6. **El saldo y el stock solo se descuentan cuando el motor confirma la unidad**
   (sensor en M1–M4 o pulso completado en M5–M6). Confirmar la compra únicamente
   encola el pedido.
7. **Un test de administrador no es una venta:** solo descuenta el stock de la
   unidad que cae; nunca toca saldo, pedido ni modal.

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
| `0x21` | Sensores de fin de carrera (solo M1–M4) | P0..P3 → canales 1..4; P4/P5 sin conexión |

- I2C0, **SDA = IO32, SCL = IO25** (confirmado físicamente), **100 kHz**.
- Relé **activo en LOW**; el driver invierte el bit al escribir.
- **M1–M4 (LK20052 de 3 hilos):** el micro-switch entrega 12 V en reposo y se
  acondiciona con un divisor **10 kΩ en serie + 3.3 kΩ a GND** (≈ 2.97 V). La
  placa LK20052 queda intacta: **no se cortan pistas**. El reposo se lee **HIGH**
  (`SENSOR_HOME_LEVEL = 1`), ajustable por software.
- **M5–M6 (LK20052 Fy-007 de 2 hilos):** sin sensor ni homing; se aplica un pulso
  calibrado de **~2.0 s** al relé correspondiente. P4/P5 de `0x21` no se leen.

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

Pines **confirmados físicamente** en la ESP32-32E 4.0"
(`board_config.h`):

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
- Calibración validada en placa: `BOARD_TOUCH_MIRROR_X = 0`,
  `BOARD_TOUCH_MIRROR_Y = 1` (compensa la rotación de 180° del XPT2046).
- El panel se crea con `esp_lcd_new_panel_io_spi()` + `esp_lcd_new_panel_st7796()`,
  con orden **BGR** y **sin inversión de color** (`BOARD_TFT_INVERT_COLOR = 0`).

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
(`...\Tecnológico de Las Américas...\Proyecto Final Maquina Expendedora\Firmware`).
Con esa ruta:

- `ccache` aborta con `filesystem error: Cannot convert character sequence:
  Illegal byte sequence`.
- `kconfiglib` no puede abrir `sdkconfig` (la ruta se corrompe a `\u251c\u2502...`).

**Solución adoptada:** compilar desde una copia en ruta ASCII.
El script `compilar.ps1` sincroniza `Firmware/` → `C:\Users\asus\esp\maquina_expendedora`
y compila allí con el build fuera de OneDrive.

```powershell
cd Firmware
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
maquina_expendedora.bin → 592 512 bytes (0x90a80)
Partición app 0x100000 → 43 % libre
Sin warnings del compilador en los archivos del proyecto.
```

> La compilación usa LVGL 8.4.0 y los drivers gestionados de `esp_lcd`; el
> primer build descarga ~26 MB de componentes y tarda varios minutos.

---

## 4. Estructura de archivos

```
Firmware/
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

Base: `freertos`, `driver`, `esp_timer`, `sdmmc`, `fatfs`, `nvs_flash`,
`esp_lcd`, `lvgl`.
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
- Fuentes Montserrat 10/12/14/16/20.
- El tick **no** usa `LV_TICK_CUSTOM` (el Kconfig de LVGL 8.4 no expone
  `LV_TICK_CUSTOM_SYS_TIME_EXPR`): `gui_task` inyecta `lv_tick_inc()` cada ciclo.

---

## 5. Qué está implementado (y funciona)

### 5.1 `main.c` — orquestación

Orden de arranque en `app_main()`:

1. `nvs_flash_init()` (con borrado y reinicialización si la partición es
   incompatible). Debe ir antes de la GUI, que carga saldo/stock.
2. Crea `g_dispatch_mutex` (Mutex global de despacho) **antes** de cualquier tarea.
3. Crea el bus I2C (`i2c_new_master_bus`, driver moderno `driver/i2c_master.h`).
4. `motor_control_init(i2c_bus)` → configura ambos PCF8574, la cola y ejecuta el
   *homing* secuencial de los canales con sensor (M1–M4).
5. `coin_acceptor_init()` + callback `on_coin_inserted`.
6. `gui_display_mount_sd()` y luego `gui_display_init()` (carga la NVS).
   - Si la SD falla: solo warning, el sistema sigue (placeholder
     `[Espacio Imagen]`).
   - Si la GUI falla: solo warning, el sistema sigue (sin pantalla).
7. `motor_control_set_event_callback(on_motor_event, NULL)` **antes** de crear
   `task_despacho`.
8. Crea las 3 tareas.

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

- GPIO35 configurado como entrada con ISR por **ambos flancos** (se mide el ancho
  del pulso).
- La ISR **solo cuenta pulsos** y los envía a una cola (no hace trabajo pesado).
- `coin_acceptor_task()` implementa la ventana de 300 ms: cada pulso reinicia el
  temporizador; al cerrar la ráfaga valida y llama al callback con
  `(pulses, cents, ctx)`.
- `coin_acceptor_pulses_to_cents()` mapea 1/3/5/7 → 100/500/1000/2500; cualquier
  otra cantidad devuelve 0.
- Antirrebote de 20 ms por pulso.
- **Protección contra saldo fantasma** (IO35 flotante si el monedero no está
  conectado o falta el pull-up de 10 kΩ):
  - Ventana de gracia de **2500 ms** tras el arranque: los transitorios de
    encendido no se acreditan.
  - Solo cuenta pulsos de **10–150 ms** de ancho; los glitches de EMI duran
    microsegundos y una línea pegada en LOW dura demasiado.
  - El monedero se **arma** solo cuando la línea lleva **500 ms** en reposo
    (HIGH). Si la línea se queda en el nivel activo, se **desarma** y vuelve a
    exigir reposo estable.
  - Al cerrar la ráfaga la línea debe haber vuelto al reposo; si no, se descarta.
  - El log avisa al arrancar si IO35 no está en reposo:
    `La linea arranco en nivel activo: revise el pull-up externo...`

### 5.3 `motor_control` — despacho

- Registro sombra de relés para no alterar los otros canales al escribir el
  PCF8574.
- Mutex interno `s_i2c_mutex` para serializar el bus I2C.
- Cola FIFO de despacho (`motor_request_t { channel, quantity }`, longitud 8).
- `motor_control_home_all()` (invocada al final de `motor_control_init()`):
  - Solo recorre **M1–M4** (P0–P3 de `0x21`); P4/P5 no se leen.
  - Los canales ya en reposo (`SENSOR_HOME_LEVEL`) no se tocan.
  - Para cada canal fuera de reposo: toma `g_dispatch_mutex`, energiza su relé
    y espera el nivel de reposo con timeout de **10 s**.
  - Es secuencial (un motor a la vez) y apaga el relé siempre, incluso si vence
    el timeout. Un fallo de homing no bloquea el arranque.
- `motor_control_dispatch_one()`:

  **M1–M4 (con sensor):**
  1. Toma `g_dispatch_mutex` (timeout configurable).
  2. Verifica que el canal esté en reposo (`SENSOR_HOME_LEVEL`). Si no, rechaza
     con `ESP_ERR_INVALID_STATE`.
  3. Energiza el relé.
  4. Espera que el sensor se libere (arranque) — timeout **3000 ms**.
  5. Espera que vuelva al nivel de reposo (vuelta completa) — timeout **20000 ms**.
  6. **Apaga el relé siempre** (`cleanup`), incluso con error/timeout.
  7. Pausa de asentamiento **250 ms** y libera el Mutex.

  **M5–M6 (sin sensor):**
  1. Toma `g_dispatch_mutex`.
  2. Energiza el relé y mantiene el pulso `MOTOR_TIMED_PULSE_MS` (**~2.0 s**).
  3. Apaga el relé, pausa de asentamiento **250 ms** y libera el Mutex.

- `motor_task()` procesa la cola en orden; entre unidades del mismo canal espera
  **400 ms**.
- Emite eventos al callback registrado (desde el contexto de `motor_task`):
  - `MOTOR_EVENT_UNIT_DONE` tras cada unidad entregada (sensor en M1–M4 o pulso
    completado en M5–M6).
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
- Valores actuales: **stock inicial = 10** (persistido en NVS) y precios
  **mutables** en `s_products[i].price_cents`, persistidos en NVS. Valores de
  fábrica: **RD$ 40/50/60/70/80/90** (4000..9000 centavos). Ya no existe
  `k_product_prices` ni `GUI_USE_DEMO_PRICES`.
- Layout:
  - Layout Flex **2 columnas × 3 filas** con `LV_FLEX_FLOW_ROW_WRAP` en el 83 %
    superior (400 px). Cada celda mide **48 %** del ancho útil (≈150 px) y
    **127 px** de alto, lo que obliga a que solo entren dos celdas por fila.
  - Cada celda es un Flex en columna centrado: cabecera `Producto N\nRD$ XX.00`,
    recuadro gris de 52×52 con el BMP o `[Espacio Imagen]`, y fila Flex con
    `[-]`, `Cant: N` y `[+]` (botones de 34×34 px).
  - Panel inferior fijo de 80 px (17 %) con Flex en fila:
    `Saldo: RD$ 0.00`, `Monto a Pagar: RD$ 0.00` (reactivo) y botón
    `Confirmar` de 118×64 px.
- Reglas implementadas:
  - Cantidad entre 0 y `min(stock, 10)`.
  - `[-]`/`[+]` ajustan y recortan contra stock.
  - Cambiar stock recorta la cantidad seleccionada.
  - Saldo nunca negativo; total = Σ(cantidad × precio).
  - El modal **no se abre** si no hay unidades, si el total es 0 o si el saldo
    no alcanza (muestra un toast).
- `gui_display_init()`:
  - Inicializa SPI2, panel ST7796 (`esp_lcd`), retroiluminación LEDC y XPT2046.
  - Registra display e input en LVGL, crea el layout 2×3 y el panel inferior.
  - Crea el Mutex de estado antes de cualquier acceso concurrente.
  - Carga saldo, stock y precios desde NVS (`storage_load_locked()`).
- Persistencia:
  - `gui_display_load_state()` / `gui_display_save_state()` son la API pública.
  - Namespace NVS `vending`: `balance` (int64), `stock` (blob de 6 bytes) y
    `prices` (blob de 6 × `uint32_t` = 24 bytes).
  - Se guarda al acreditar monedas, al cambiar stock, al guardar precios y en
    cada unidad cobrada o entregada por un test
    (`gui_display_on_unit_dispatched()`).
- **Modo administración** (pulsación larga de 3 s en la esquina superior
  izquierda): overlay a pantalla completa con un contenedor Flex vertical
  desplazable y tres secciones:
  - **Inventario:** stock de los 6 canales y `Rellenar Stock (Reset a 10)`.
  - **Precios:** filas con `[-]`/`[+]` (paso RD$ 5) y `Guardar Precios`. El
    borrador se aplica al estado y a NVS solo al guardar; luego se refresca la
    pantalla principal.
  - **Prueba de motores:** `Test M1`…`Test M6`, que encolan una unidad con
    `admin_test = true`. Se rechazan si hay pedido activo, modal abierto, cola o
    motor ocupados, o stock 0.
- `task_gui` inyecta `lv_tick_inc()`, llama a `lv_timer_handler()` y refresca
  los widgets solo cuando el estado cambia (notificación + snapshot).
- Concurrencia: `s_state_mutex` protege saldo, stock, precios, cantidades, modal
  y pedido. `task_monedero` y `motor_task` solo mutan estado y despiertan a
  `task_gui`; **ninguna** llamada a LVGL ocurre fuera de `task_gui`.
- `gui_display_confirm_purchase()`:
  - Valida modal abierto, stock, total > 0 y saldo suficiente.
  - Limpia cantidades y registra el pedido en curso.
  - **Solo encola** en `task_despacho`; **no** descuenta saldo ni stock.
  - Si el encolado falla a mitad, cancela las unidades no encoladas y no las
    cobra.
- `gui_display_on_unit_dispatched(channel, admin_test)`:
  - Venta real: **único punto de cobro**; resta el precio al saldo y reduce el
    stock en 1 por cada unidad confirmada por el motor.
  - Test de administrador: **solo** reduce el stock en 1 y persiste; no toca
    saldo, pedido, modal ni totales.
- `gui_display_on_dispatch_failed(channel, error, admin_test)`:
  - Venta real: marca el pedido como fallido, pone a cero las unidades
    reservadas y muestra un toast; el saldo de lo no entregado queda intacto.
  - Test: solo informa el fallo; no hay pedido que cancelar ni stock que
    descontar.

### 5.5 Flujo actual de una compra

```
Moneda → ISR IO35 → cola de pulsos → task_monedero (ventana 300 ms)
      → callback → saldo += cents

Usuario → ajusta cantidades → "Confirmar" → modal → [Sí]
      → gui_display_confirm_purchase(): valida y ENCOLA (no cobra)
      → task_despacho mueve motores uno a uno
      → por cada vuelta OK: MOTOR_EVENT_UNIT_DONE (admin_test = false)
      → gui_display_on_unit_dispatched(): saldo -= precio, stock -= 1
      → si un motor falla: MOTOR_EVENT_REQUEST_FAILED
         → se descarta la cola; lo no entregado no se cobra ni baja stock

Administrador → pulsación larga (3 s) → panel → "Test Mx"
      → motor_control_enqueue_batch(..., admin_test = true)
      → por cada vuelta OK: MOTOR_EVENT_UNIT_DONE (admin_test = true)
      → gui_display_on_unit_dispatched(): stock -= 1, NVS; saldo intacto
```

---

## 6. Qué falta por hacer (priorizado)

### P0 — Bloqueante para que la máquina sea usable

- [x] **Integrar la pantalla real (`esp_lcd`)**
  - ST7796 320×480 por SPI2 con `esp_lcd_new_panel_io_spi` +
    `esp_lcd_new_panel_st7796`, retroiluminación por LEDC.
  - Pines **confirmados** en `board_config.h`.
  - *Criterio:* la pantalla enciende y muestra el grid.

- [x] **Integrar LVGL y el táctil**
  - LVGL 8.4.0 por Kconfig + XPT2046 en modo sondeo.
  - Layout 2×3 con Flex `ROW_WRAP`, botones `[-] [+]`, contador `Cant: N`,
    panel con saldo, monto a pagar y modal `Confirmar compra [Sí] [No]`.
  - `lv_timer_handler()` + `lv_tick_inc()` en `task_gui`.
  - *Criterio:* se puede seleccionar producto y confirmar solo con el dedo;
    calibración validada en la placa real (M1–M6).

- [x] **Concurrencia de la GUI**
  - `s_state_mutex` protege saldo, stock, cantidades, modal y pedido.
  - `task_monedero` y `motor_task` solo mutan estado y notifican a `task_gui`;
    LVGL se usa exclusivamente en `task_gui`.
  - *Criterio:* sin carreras ni *asserts* de LVGL al insertar monedas mientras
    se pulsa la pantalla.

- [x] **Configurar precios y stock reales**
  - Precios mutables en `s_products[i].price_cents`, con valores de fábrica
    RD$ 40/50/60/70/80/90 y persistencia en NVS (`prices`).
  - El modo administración permite ajustarlos en pasos de RD$ 5.
  - Stock inicial de 10 por producto y persistencia en NVS.
  - *Criterio:* los 6 productos tienen nombre y precio editables en un único
    sitio (`gui_display.c`), y precio + stock sobreviven a un reinicio.

- [ ] **Validación en la placa real (checklist §8)**
  - Flashear y confirmar en el log: `ST7796 listo`, `Tactil XPT2046 listo`,
    `MicroSD montada` y `Homing completado`.
  - Probar: monedas, compra de 1 unidad, cobro diferido, homing tras un corte y
    persistencia NVS tras reinicio.
  - *Criterio:* se completa el checklist de §8 sin fallos.

- [x] **Calibrar el táctil y los colores**
  - `BOARD_TFT_MIRROR_X = 1` y `BOARD_TFT_INVERT_COLOR = 0`.
  - `BOARD_TOUCH_MIRROR_X = 0` y `BOARD_TOUCH_MIRROR_Y = 1` para compensar la
    rotación de 180° que entrega el XPT2046 en esta placa.
  - Validado en placa: cada `+`/`−` afecta su propio canal y los colores
    verde/rojo se muestran correctamente.

### P1 — Robustez y consistencia

- [x] **Rutina de *homing***
  - `motor_control_home_all()` se ejecuta al final de `motor_control_init()`.
  - Solo recorre M1–M4; si un canal está fuera de reposo, energiza su relé hasta
    alcanzar `SENSOR_HOME_LEVEL` con timeout de 10 s por canal.
  - Es secuencial y usa `g_dispatch_mutex`; el relé se apaga siempre.
  - *Criterio:* tras un corte de energía a mitad de vuelta, el sistema se
    recupera solo en los canales con sensor.

- [x] **Descontar stock/saldo solo al confirmar el despacho**
  - Implementado **por unidad**: `motor_task` emite `MOTOR_EVENT_UNIT_DONE`
    cuando el sensor confirma la vuelta (M1–M4) o termina el pulso (M5–M6), y
    solo entonces la GUI cobra.
  - Si un motor falla, se descarta el resto de la cola y lo no entregado no se
    cobra ni reduce stock.
  - *Pendiente:* decidir si un pedido parcialmente entregado debe
    reembolsarse por completo (hoy las unidades entregadas quedan cobradas).

- [x] **Encolado atómico**
  - `motor_control.c` incorpora `s_queue_mutex`, que serializa la validación de
    capacidad con el encolado del lote completo.
  - `motor_control_check_capacity(count)` devuelve `ESP_OK` solo si hay huecos
    para **todas** las solicitudes; `motor_control_enqueue_batch()` vuelve a
    validar y encola el lote entero bajo el mismo Mutex.
  - `gui_display_confirm_purchase()` construye el lote, comprueba la capacidad
    **antes** de registrar el pedido y, si no cabe, mantiene el modal abierto y
    muestra `Cola ocupada: reduzca la cantidad`. Si el encolado fallara de forma
    inesperada, revierte el pedido completo y devuelve las cantidades.
  - `motor_control_enqueue()` queda como envoltorio de una sola solicitud.
  - *Criterio:* una compra se acepta completa o se rechaza completa.

- [x] **Persistencia en NVS**
  - Namespace `vending`: saldo (`balance`), stock de los 6 productos (`stock`) y
    precios de los 6 productos (`prices`, 6 × `uint32_t`).
  - Se carga en `gui_display_init()` y se guarda al acreditar monedas, al cobrar
    o entregar una unidad y al guardar precios desde el modo administración.
  - *Criterio:* apagar y encender conserva saldo, stock y precios.

- [x] **Modo administración completo**
  - Botón invisible de 40×40 px en la esquina superior izquierda, fuera de los
    botones `[-] [+]`.
  - El driver táctil usa `long_press_time = 3000 ms`; al recibir
    `LV_EVENT_LONG_PRESSED` se abre un overlay a pantalla completa que bloquea
    los botones de producto.
  - **Inventario:** muestra el stock de los 6 canales y el botón
    `Rellenar Stock (Reset a 10)`, que pone todo a 10, limpia cantidades y
    persiste en NVS sin cerrar el panel.
  - **Precios:** filas con nombre, precio actual, `[-]`/`[+]` (paso RD$ 5) y
    `Guardar Precios`. Los cambios son un borrador local; al guardar se aplican
    a `s_products[].price_cents`, se persisten y se refresca la pantalla
    principal. Rango válido: RD$ 5.00–1000.00.
  - **Prueba de motores:** `Test M1`…`Test M6` encolan una unidad con
    `admin_test = true`. El evento de unidad entregada solo descuenta stock;
    nunca toca saldo, pedido ni modal.
  - Guardas: el reset y los tests se rechazan con
    `Espere a que termine el despacho` si hay una compra activa; los tests
    también se rechazan con motor/cola ocupados o stock 0.
  - *Pendiente:* historial de ventas/recaudación y diagnóstico de sensores.

### P2 — Extras / V2.0 (según documento de arquitectura §7)

- [ ] Telemetría Wi-Fi (reportes de ganancias y stock a app/BD).
- [ ] Audio por *Speaker Interface* (moneda aceptada, error, etc.).
- [ ] Lector RFID por SPI para tarjetas corporativas.
- [ ] Registro de eventos/errores en la MicroSD.

### Calidad / tooling (transversal)

- [x] Historial de commits del firmware en `main` (último código: `9639a48`; después solo documentación).
- [x] **`git push`** de los commits locales a `origin/main` (ver §0).
- [x] `.gitignore` excluye `build/`, `sdkconfig`, `managed_components/`, etc.
- [ ] Añadir pruebas de la lógica pura (mapeo de pulsos, total, stock) con
      Unity o tests de host; hoy no hay ninguno.
- [x] `sdkconfig.defaults` con LVGL, FS stdio, BMP, fuentes y XPT2046.
- [ ] Añadir CI (build automatizado) si el proyecto se publica.

---

## 7. Riesgos, bugs conocidos y deuda técnica

| # | Problema | Impacto | Dónde |
| :---: | :--- | :--- | :--- |
| 1 | ~~Pines de TFT/táctil sin verificar~~ **Resuelto** | Pines confirmados físicamente | `board_config.h` |
| 2 | ~~Precios demo y stock fijo~~ **Resuelto** | Precios mutables + stock en NVS | `gui_display.c` |
| 3 | ~~Stock/saldo se descuentan antes del motor~~ **Resuelto** | Cobro por unidad confirmada | `gui_display_on_unit_dispatched()` |
| 4 | ~~Encolado no atómico~~ **Resuelto** | Lote todo-o-nada con `s_queue_mutex`; rechazo entero si no cabe | `motor_control_enqueue_batch()` |
| 5 | ~~Sin *homing*~~ **Resuelto** | Homing secuencial M1–M4 con timeout de 10 s | `motor_control_home_all()` |
| 6 | ~~Estado de GUI sin mutex~~ **Resuelto** | `s_state_mutex` protege todo el estado | `gui_display.c` |
| 7 | ~~Pines I2C sin verificar~~ **Resuelto** | SDA=IO32, SCL=IO25 | `board_config.h` |
| 8 | Táctil sin calibrar (ejes/espejo) | Pulsaciones en zona equivocada | `BOARD_TOUCH_*` |
| 9 | Sin pruebas automatizadas ni CI | Regresiones silenciosas | repo |
| 10 | ~~`Firmware/` sin commitear~~ **Resuelto** | Ya versionado en `main` | repo |

### Notas de implementación delicadas

- **GPIO35 es input-only y sin pull-up interno.** Si no hay pull-up externo, la
  ISR puede dispararse de forma errática o no dispararse. No habilitar el
  pull-up por software: cargaría el optoacoplador.
- **El relé se apaga en el bloque `cleanup`.** No eliminar esa garantía al
  refactorizar: es la protección contra motores girando indefinidamente.
- **`g_dispatch_mutex` debe existir antes de crear `task_despacho`.** El homing
  también lo usa, así que debe crearse antes de `motor_control_init()`.
- **NVS antes de la GUI:** `nvs_flash_init()` debe ejecutarse antes de
  `gui_display_init()` para que la carga de saldo/stock funcione.
- **El guardado NVS ocurre con el Mutex de estado tomado.** Es una escritura
  corta (una por moneda o unidad); no añadir esperas largas dentro del lock.
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
cd Firmware
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

> **Estado 2026-10-25:** este comando se ejecutó y falló con
> `Could not open COM3, the port is busy or doesn't exist.` No hay ningún puerto
> COM presente en el equipo. Antes de reintentar: conectar la placa con un cable
> de **datos**, comprobar que aparece en el Administrador de dispositivos y
> sustituir `COM3` por el puerto real (`[System.IO.Ports.SerialPort]::GetPortNames()`).

Salir del monitor: `Ctrl + ]`.

Al arrancar, el log esperado es similar a:

```
app_main: Maquina Expendedora - 6 canales (ESP32-32E 4.0")
app_main: Mutex de despacho creado (un motor a la vez)
app_main: Bus I2C0 listo (SDA=IO32, SCL=IO25, 100000 Hz)
motor_control: Homing inicial de 4 canales con sensor (M1-M4, timeout 10000 ms por canal)
motor_control: Homing completado: M1-M4 en reposo (M5-M6 por tiempo)
motor_control: Control de motores listo (relees 0x20, sensores 0x21 en M1-M4, M5-M6 por tiempo 2000 ms)
gui_display: NVS vacia: saldo RD$ 0.00, stock 10 y precios por defecto
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
2. **Layout:** confirmar visualmente 6 celdas en 2 columnas × 3 filas, sin
   productos agrupados en una sola fila ni solapados con el panel inferior.
3. **Confirmar el log:** debe aparecer `ST7796 listo`, `Tactil XPT2046 listo` y
   `MicroSD montada`. Si falta alguno, revisar pines/alimentación.
4. **Colores y orientación:** si los colores salen invertidos, cambiar
   `BOARD_TFT_INVERT_COLOR`; si rojo/azul están cambiados, cambiar
   `BOARD_TFT_USE_BGR_ORDER`; si la imagen está girada/espejada, ajustar
   `BOARD_TFT_SWAP_XY/MIRROR_X/MIRROR_Y`.
5. **Táctil:** pulsar cada esquina. Si el eje X/Y está intercambiado o invertido,
   ajustar `BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`. Repetir hasta que el botón
   pulsado coincida con el dibujado.
6. **Imágenes:** copiar `p1.bmp`…`p6.bmp` (64×64) a `/sdcard/productos/` y
   reiniciar. Sin archivos se muestra `[Espacio Imagen]`.
7. **Homing:** reiniciar con un motor M1–M4 fuera de reposo y comprobar en el log
   que el canal alcanza `SENSOR_HOME_LEVEL` en menos de 10 s y que solo se
   energiza un relé a la vez. M5–M6 no participan del homing.
8. **Monedero:** insertar RD$ 1/5/10/25 y verificar que el saldo sube en pantalla
   con el valor correcto.
9. **Compra de prueba:** seleccionar 1 unidad, confirmar y comprobar que el
   saldo baja **solo cuando el motor termina la vuelta**.
10. **Persistencia:** apagar y encender; el saldo, el stock y los precios deben
    conservarse.
11. **Fallo controlado:** desconectar un sensor de M1–M4 o bloquear un motor y
    verificar que el pedido se cancela sin cobrar las unidades no entregadas.
12. **Modo administración:** mantener pulsada la esquina superior izquierda
    durante 3 s; debe abrirse el panel con scroll. Verificar:
    - Inventario: `Rellenar Stock (Reset a 10)` deja los 6 productos a 10 y
      sobrevive a un reinicio.
    - Precios: `[-]`/`[+]` cambian el borrador en RD$ 5; `Guardar Precios`
      actualiza la pantalla principal y persiste tras reiniciar.
    - Motores: `Test Mx` entrega una unidad, baja el stock en 1 y **no** cambia
      el saldo. Repetir con stock 0: debe rechazarse.
13. **Encolado atómico:** con la UI normal es difícil llenar la cola (máximo 6
    líneas por pedido, cola de 8 y un solo pedido a la vez). Para probar el
    rechazo, reducir temporalmente `MOTOR_QUEUE_LENGTH` a 1 o añadir una prueba
    de host; el pedido debe rechazarse entero con el aviso
    "Cola ocupada: reduzca la cantidad".

---

## 9. Preguntas abiertas (datos que faltan)

> Las dos decisiones **inmediatas** (push y flasheo) están en la
> [§0](#0-preguntas-para-ti-responde-debajo-de-cada-una).

1. ¿Los módulos de relé conmutan de forma fiable con 3.3 V en la entrada?
   (Algunos módulos de 5 V con optoacoplador necesitan adaptador de nivel).
2. ¿Se requiere otro método de acceso al modo administración (SD, Wi-Fi, PIN)?
   Hoy es una pulsación larga de 3 s en la esquina superior izquierda.
3. ¿La versión 1.0 debe incluir telemetría Wi-Fi o se deja para V2.0?
4. ¿Un pedido parcialmente entregado debe reembolsarse completo o solo cobrar
   las unidades que salieron? (Hoy se cobran solo las entregadas).
5. ¿Los precios de fábrica RD$ 40/50/60/70/80/90 son los definitivos? Ahora
   pueden cambiarse desde el panel de administración y quedan en NVS.

---

## 10. Definición de "MVP terminado"

Se considerará la versión 1.0 funcional cuando:

1. La pantalla muestra el grid 2×3 con imágenes desde la SD.
2. El táctil permite ajustar cantidades y confirmar con modal.
3. Los precios son editables desde el modo administración, persisten en NVS y
   el stock también sobrevive a un reinicio.
4. Insertar monedas actualiza el saldo en pantalla en < 1 s.
5. Confirmar una compra despacha los productos **uno a uno**, sin reinicios.
6. Si un despacho falla, **no se cobra** lo no entregado (hoy ya se cumple por
   unidad; falta definir el reembolso del pedido parcial).
7. Tras un corte de energía, el sistema hace *homing* en M1–M4 y vuelve a operar.
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
- Los pines de `board_config.h` están **confirmados físicamente** (I2C 32/25,
  TFT/táctil y MicroSD). No los cambies sin verificar de nuevo la placa.
- La GUI ya está implementada: **no bloquees `task_gui`** con esperas largas y
  no llames a LVGL desde `task_monedero` ni desde `motor_task`.
- Si tocas el flujo de compra, mantén el cobro **solo** en
  `gui_display_on_unit_dispatched()`.
- La marca `admin_test` viaja en `motor_request_t` y `motor_event_t`: si añades
  una ruta de despacho nueva, propágala para que un test nunca cobre saldo.
- Mantén el patrón actual: un `.h` con API documentada y un `.c` con
  implementación; logs con `ESP_LOGI/W/E` y `TAG` por módulo.
- Cualquier función nueva que energice un motor **debe** tomar
  `g_dispatch_mutex`.
- Si añades archivos, actualiza `main/CMakeLists.txt` y este documento.
