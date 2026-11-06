# Estado del proyecto — Máquina Expendedora (6 canales)

> **Propósito de este documento:** servir como *handoff* completo para una IA (o
> un desarrollador) que continúe el proyecto. Describe qué existe hoy, cómo está
> construido, qué funciona, qué falta y en qué orden conviene atacarlo.
>
> **Última actualización:** 2026-11-05
> **Estado global:** **firmware final de la versión 1.0.** Compila sin errores ni
> warnings (`maquina_expendedora.bin` = **0x8fb10**, 588 560 B, 44 % libre).
> Pantalla, táctil, NVS, modo administración y los **6 canales** ya estaban
> validados en hardware. En el banco se probó **un único motor recorriendo M1–M6**
> (conectándolo a ambos expansores canal por canal): relé, giro y lectura del
> sensor respondieron en cada posición. Queda pendiente el **montaje definitivo
> de los 6 motores** y la prueba del **monedero con monedas reales**.
>
> **Montaje actual:** 6 módulos de relé de **1 canal** (SRD-12VDC-SL-C) con
> **jumper `H/L` en LOW** y un **transistor NPN inversor** por canal
> (`EMISOR`→GND, `BASE`→1 kΩ→P0…P5, `COLECTOR`→`IN`). El PCF8574 `0x20`
> enciende con **HIGH** (`BOARD_RELAY_ACTIVE_LOW = 0`).
>
> **Sensores de posición (nuevo):** segundo PCF8574 en **`0x21`** para **M1–M4**
> (P0–P3) con divisor 10 kΩ / 3.3 kΩ desde 12 V. `home = LOW`
> (`BOARD_SENSOR_HOME_LEVEL = 0`), aún **sin confirmar con multímetro**. **M5 y
> M6 siguen en lazo abierto por tiempo (3 s).** Si `0x21` no responde, los 6
> canales caen automáticamente a lazo abierto.
>
> **Monedero habilitado** (`BOARD_COIN_ACCEPTOR_ENABLED = 1`): CH-926 por IO35
> (pull-up externo 10 kΩ), acreditación 1/3/5/7 pulsos.
> **MicroSD desactivada** (`BOARD_SD_ENABLED = 0`): las imágenes de producto van
> embebidas en flash (`main/product_images.c`).

### Resumen rápido: qué está hecho y qué falta

**Hecho y verificado (compila y pantalla validada):**

- [x] Monedero por ISR en IO35 con ventana de ráfaga y antirrebote.
- [x] Filtro anti-saldo-fantasma en IO35: gracia de arranque, ancho de pulso
      válido y armado solo con la línea en reposo estable (ver §5.2).
- [x] Despacho I2C de 6 motores con Mutex (un motor a la vez). **M1–M4 en lazo
      cerrado con sensor de posición** (`0x21`); **M5–M6 por tiempo (3 s)** y
      fallback automático a tiempo si el `0x21` no responde.
- [x] Eventos de despacho (`UNIT_DONE` / `REQUEST_FAILED` / `IDLE`) y descarte
      de la cola ante fallo.
- [x] GUI real: ST7796 320×480 por `esp_lcd`, XPT2046, LVGL 8.4, layout Flex
      2×3 (`ROW_WRAP`, celdas al 48 %) con **imágenes embebidas en flash**
      (`product_images.c`, 52×52 RGB565) y panel inferior con saldo y total.
- [x] Concurrencia: Mutex de estado y LVGL solo desde `task_gui`.
- [x] Cobro diferido: confirmar solo encola; se cobra por unidad entregada.
- [x] Encolado atómico: lote todo-o-nada con `s_queue_mutex` y
      `motor_control_check_capacity()`; si no cabe el pedido completo, se
      rechaza entero y la GUI pide reducir la cantidad.
- [x] Modo administración por botón `ADM` junto a `Confirmar` + contraseña
      **20250456 (8 dígitos)** → panel con scroll de tres secciones: **saldo**
      (± RD$ 1/5/10/25, persistido en NVS al instante), **inventario** (reset a
      10) y **precios** (6 productos, ± RD$ 5 y guardado en NVS). Sin prueba de
      motores.
- [x] Pines de hardware confirmados (I2C 32/25, TFT/táctil y MicroSD).
- [x] Persistencia en NVS de saldo, stock y precios (carga al arrancar; guardado
      por moneda, unidad entregada o cambio de precio).
- [x] GUI rediseñada: 2×3, cabecera nombre+precio, imagen embebida, `Cant: N`,
      panel inferior de 82 px con `Saldo:`, `Total:` y `Confirmar`.
- [x] **Monedero CH-926 habilitado** por IO35 (`BOARD_COIN_ACCEPTOR_ENABLED = 1`).
- [x] **MicroSD desactivada** (`BOARD_SD_ENABLED = 0`): ya no se intenta montar.
- [x] **Sensores M1–M4** por segundo PCF8574 `0x21` (`sensor_input.c`).
- [x] Build limpio (**0x8fb10**, 44 % libre) y `Firmware/` versionado.

**Falta (en orden de prioridad):**

1. **Montaje definitivo de los 6 motores**: la prueba de banco se hizo con un
   solo motor recorriendo M1–M6. Falta instalar los 6 a la vez y validar el
   despacho secuencial completo.
2. **Monedero CH-926**: probar con monedas reales la acreditación 1/3/5/7 pulsos.
3. Confirmar con multímetro la polaridad del sensor (`home = LOW`) y el divisor
   10 kΩ/3.3 kΩ; si es correcta, evaluar `BOARD_SENSOR_HOMING_ENABLED = 1`.
4. Definir P4–P6 (nombre, precio e imagen): hoy tienen precios provisionales
   RD$ 70/80/90 para pruebas de electrónica.
5. Pruebas automatizadas y CI (transversal).
6. Extras V2.0 (telemetría Wi-Fi, audio, RFID).
7. Ampliar el modo administración con historial de ventas/recaudación.

**Estado de las acciones de esta sesión (2026-11-05):**

- [x] Contraseña del modo administración cambiada a **20250456** (8 dígitos).
- [x] Las cantidades del carrito vuelven a cero al cerrar cualquier pedido.
- [x] Precios P1–P3 = RD$ 50/50/50 y P4–P6 = RD$ 70/80/90 (provisional).
- [x] Imágenes de producto embebidas en flash; MicroSD desactivada.
- [x] Build limpio sin warnings → **0x8fb10**, 44 % libre.
- [x] Flasheo por **COM6** verificado (arranque, precios, SD desactivada,
      `0x21` detectado, monedero armado).
- [x] Prueba de banco con **un solo motor recorriendo M1–M6**: relé y sensor
      responden en cada canal; el log mostró `home` en el canal conectado.

### Fase final (2026-11-05): sensores, monedero, imágenes en flash

1. **`sensor_input.c/.h` (nuevo):** único módulo que conoce el PCF8574 `0x21`.
   Hace probe al arrancar, lee P0–P3 como entradas (escribe `0xFF`), reintenta
   hasta 3 veces, interpreta `home` según `BOARD_SENSOR_HOME_LEVEL` y registra
   el estado por canal. Si el expansor no responde, `sensor_input_is_ready()`
   devuelve `false`.
2. **`motor_control.c`:** M1–M4 usan lazo cerrado (giro hasta salir de `home`,
   luego hasta volver a `home`, con timeout); M5–M6 mantienen el pulso fijo de
   3 s. Si `0x21` falla al arrancar, los 6 canales caen a lazo abierto. Si el
   sensor falla durante el giro, el relé se apaga y se reporta error sin cobrar.
   El *homing* opcional existe pero está **desactivado**
   (`BOARD_SENSOR_HOMING_ENABLED = 0`) hasta confirmar la polaridad real.
   En la prueba de banco (un solo motor recorriendo M1–M6) el sensor respondió
   en cada canal: el log marcó `home` en la posición conectada y `fuera de home`
   en las demás.
3. **Monedero habilitado:** `BOARD_COIN_ACCEPTOR_ENABLED = 1`; CH-926 por IO35
   con pull-up externo, acreditación 1/3/5/7 pulsos.
4. **Imágenes en flash:** `herramientas/generar_imagenes.ps1` convierte
   `imagenes_productos/pN.(bmp|png|jpg)` a `main/product_images.c/.h`
   (52×52, RGB565 con bytes intercambiados para `LV_COLOR_16_SWAP=1`). Si falta
   una imagen se genera un marcador de posición. La GUI ya no usa rutas ni la
   MicroSD.
5. **Productos y precios:** P1 “Galletas chocochip”, P2 “Galletas Saladas” y
   P3 “Brownies” a RD$ 50; P4–P6 quedan como “Producto 4/5/6” con RD$ 70/80/90
   **provisionales** para pruebas. Migración one-shot con clave NVS `prices_v`
   (versión actual **3**).
6. **MicroSD desactivada:** `BOARD_SD_ENABLED = 0`; `gui_display_mount_sd()`
   retorna `ESP_ERR_NOT_SUPPORTED` y `main.c` lo registra como info.
7. **Interfaz inferior:** panel de 82 px; `Saldo:` y `Total:` en una sola línea
   (Montserrat 12, `LV_LABEL_LONG_DOT`); botón `ADM` 40×40 y `Confirmar`
   110×60.
8. **Contraseña de administración:** **20250456** (8 dígitos); el teclado valida
   automáticamente al octavo dígito.
9. **Carrito:** las cantidades se limpian al confirmar y se vuelven a forzar a
   cero en `order_finalize_locked()` al cerrar cualquier pedido.
10. **Build:** **0x8fb10** (588 560 B), **44 % libre**, sin warnings.

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

Para probar NVS, táctil y el despacho de motores necesito la placa conectada.

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
2. **Motores:** en esa fecha se añadió una rutina de referencia de posición;
   quedó **eliminada el 2026-11-04** al adoptar el despacho por tiempo en los
   6 canales.
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
5. **Build:** **616 960 B** (`0x96a00`), **41 % libre**, sin warnings.

### Rediseño del acceso administrador (2026-10-30)

1. **Acceso por botón:** se eliminó el botón invisible de 40×40 px y la
   pulsación larga de 3 s (`GUI_ADMIN_LONG_PRESS_MS`, `long_press_time` del
   driver táctil). En su lugar, el panel inferior tiene un botón `ADM` de
   **46×40 px** junto a `Confirmar`.
2. **Contraseña 0000 (histórico; hoy `20250456`):** el botón abre un teclado numérico 3×4
   (`1`–`9`, `C`, `0`, `OK`). El cuarto dígito se valida automáticamente;
   `C` limpia el campo y `OK` reintenta el envío. Con la clave incorrecta se
   muestra `Contrasena incorrecta`. El panel de ajustes solo se construye tras
   un acceso correcto.
3. **Ajuste de saldo:** nueva sección **Saldo** al inicio del panel, con botones
   `-1`, `-5`, `-10`, `-25` (rojo) y `+1`, `+5`, `+10`, `+25` (verde). Cada
   pulsación actualiza `s_balance_cents`, guarda en NVS y refresca la pantalla
   principal. Rango acotado a RD$ 0.00–10 000.00.
4. **Sin prueba de motores:** se eliminaron la sección `Prueba de Motores`, los
   botones `Test M1`…`Test M6` y el callback `on_admin_test_clicked`. El soporte
   `admin_test` permanece en `motor_control.c`/`main.c`, pero la GUI ya no lo usa.
5. **Build:** **616 960 B** (`0x96a00`), **41 % libre**, sin warnings.

### Eliminación total de sensores y despacho por tiempo (2026-11-04; el punto 8 completa la lógica de relés)

1. **Se elimina el expansor `0x21`.** El firmware ya no lo inicializa, no lo lee
   y no existe ninguna ruta de lectura de fines de carrera. También se
   eliminaron `motor_control_read_sensor*()`, `motor_control_home_all()`,
   `motor_control_write_relay_raw()`, `channel_uses_sensor()` y las etapas de
   bring-up 2, 3 y 4.
2. **Los 6 canales usan la misma estrategia.** `motor_control_dispatch_one()`
   energiza el relé del canal, mantiene el pulso `MOTOR_TIMED_PULSE_MS` =
   **3000 ms** y apaga el relé siempre en el bloque `cleanup`.
3. **Lógica del módulo de relés (histórico, superado por el punto 8).** Jumper
   físico en **HIGH**: escribir **LOW (0 V)** en el pin INn activa el motor y
   **HIGH (3.3 V)** lo apaga. El driver mantiene un registro sombra lógico y lo
   invierte al escribir en `0x20` (`BOARD_RELAY_ACTIVE_LOW = 1`). Con el NPN
   actual el jumper va en **LOW** y `BOARD_RELAY_ACTIVE_LOW = 0`.
4. **Sin acondicionamiento de señal.** Se eliminaron los divisores 10 kΩ /
   3.3 kΩ, las resistencias de pull-up/pull-down asociadas a los sensores y
   cualquier mención a micro-switches en el código y en la documentación.
5. **Bring-up reducido.** `BRINGUP_TEST_MOTOR1 = 1` hace un único pulso de 3 s
   sobre M1 al arrancar; `0` deja el sistema en producción.
6. **Build:** **611 600 B** (`0x95510`), **42 % libre**, sin warnings.
7. **Modo produccion y correccion de compra (2026-11-04).**
   - **Monedero deshabilitado** con `BOARD_COIN_ACCEPTOR_ENABLED = 0`: no se
     inicializa IO35 ni se crea `task_monedero`. El saldo se carga desde el
     panel `ADM`.
   - `BRINGUP_TEST_MOTOR1 = 0`.
   - `gui_display_confirm_purchase()` ahora inicializa `batch[].admin_test =
     false`. Antes el campo quedaba indeterminado y una venta real podia
     tratarse como test de administrador.
   - `motor_control_init()` hace `i2c_master_probe(0x20)` y registra si el
     PCF8574 responde. Cada conmutacion registra el byte fisico enviado:
     `Canal N -> ON/OFF (0x20 = 0xXX)`.
   - **Reset de stock unico:** `maintenance_reset_stock_once()` escribe el blob
     de stock a 10 y marca `stock_reset_v1` en NVS. No toca saldo ni precios y
     no se repite en arranques posteriores.

8. **Transistor NPN inversor y validación de los 6 motores (2026-11-04, noche).**
   - El módulo de relé de 12 V tiene el LED del opto referenciado a 12 V: con el
     jumper en LOW, un HIGH de 3.3 V no lo apagaba y el motor quedaba pegado.
   - Se sustituyó el módulo de 6 canales por **6 módulos de 1 canal**
     (SRD-12VDC-SL-C) y se añadió un **NPN por canal**:
     `P0…P5 → 1 kΩ → BASE`, `EMISOR → GND`, `COLECTOR → IN`.
   - `BOARD_RELAY_ACTIVE_LOW = 0`: el PCF8574 enciende con **HIGH** (`0x01`,
     `0x02`, `0x04`, `0x08`, `0x10`, `0x20`) y apaga con **LOW** (`0x00`).
   - **Validado en placa:** M1–M6 giran y se detienen; cada pulso dura ~3.009 s
     y el cobro ocurre después del apagado.
   - **Efecto conocido:** el PCF8574 arranca con sus pines en HIGH, así que al
     energizar los 6 NPN pueden conducir durante el arranque (~0.4 s) hasta que
     `motor_control_init()` escribe `0x00`. Es un tirón corto, no una activación
     sostenida.

### Historial superado (2026-11-04)

> Esta sección describe una etapa intermedia. El firmware **vigente**
> (2026-11-05) sí usa sensores en **M1–M4** (`0x21`) y mantiene **M5–M6** por
> tiempo; ver la “Fase final” al inicio de este documento.

**Sesión anterior (fase P0):** API táctil migrada a `esp_lcd_touch_get_data()`,
rutas LVGL corregidas a `S:/productos/pN.bmp` y `image_src` estático para
`lv_img_set_src()`.

### Tabla de estado por bloque

| Bloque | Estado | Qué falta / riesgo |
| :--- | :---: | :--- |
| Pines de hardware | ✅ Confirmado | — |
| Monedero (ISR IO35) | ✅ Implementado | CH-926 NO + FAST: 1/3/5/7 pulsos = RD$ 1/5/10/25. Falta prueba física final |
| Despacho I2C + Mutex | ✅ Validado en placa | M1–M4 con sensor `0x21`; M5–M6 por tiempo (3 s); NPN validado |
| Relés + transistor NPN | ✅ Validado en placa | PCF8574 HIGH = encendido; jumper `H/L` en LOW |
| Sensores M1–M4 (`0x21`) | ✅ Probado en banco | Un solo motor recorriendo M1–M6; falta montaje definitivo y confirmar polaridad con multímetro |
| *Homing* de arranque | ⏸️ Desactivado | Implementado pero `BOARD_SENSOR_HOMING_ENABLED = 0` |
| Persistencia NVS | ✅ Implementado | Saldo, stock y precios (`prices` + `prices_v`) |
| GUI 2×3 + panel inferior | ✅ Implementado | Imágenes en flash; táctil y colores validados |
| Cobro diferido por unidad | ✅ Implementado | Decidir reembolso de pedido parcial |
| Encolado atómico | ✅ Implementado | Probar rechazo con la cola llena en placa |
| Modo administración | ✅ Implementado | Botón `ADM` + clave `20250456`; saldo, inventario y precios |
| Imágenes de producto | ✅ Implementado | Embebidas en flash; P4–P6 usan marcador provisional |
| Pruebas + CI | ❌ Falta (P2) | Sin tests automatizados |
| V2.0 (Wi-Fi, audio, RFID) | ❌ Falta (P2) | Según arquitectura §7 |
| Push a GitHub | ✅ Hecho | `origin/main` al día tras la fase final |
| Flasheo + monitor | ✅ Hecho | COM6, ESP32-D0WD-V3, arranque verificado |

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
3. **Las imágenes de producto están embebidas en flash** (`product_images.c`,
   52×52 RGB565). La **MicroSD está desactivada** (`BOARD_SD_ENABLED = 0`); no
   se usa para activos gráficos ni para logs.
4. **El monedero entra por IO35**, que es *input-only* y **no tiene pull-up
   interno**: requiere pull-up externo de 10 kΩ a 3.3 V. El firmware **no debe
   habilitar el pull-up por software** (ya está el externo y cargaría el opto).
   Está **habilitado** (`BOARD_COIN_ACCEPTOR_ENABLED = 1`).
5. El PCF8574 debe alimentarse a **3.3 V**, no 5 V.
6. **El saldo y el stock solo se descuentan cuando se completa el pulso de la
   unidad** (3 s). Confirmar la compra únicamente encola el pedido.
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

### 2.2 Bus I2C y expansor PCF8574

| Dirección | Función | Bits |
| :--- | :--- | :--- |
| `0x20` | 6 módulos de relé (motores 12 V) | P0..P5 → canales 1..6 |
| `0x21` | Sensores de posición M1–M4 | P0..P3 → canales 1..4 |

- I2C0, **SDA = IO32, SCL = IO25** (confirmado físicamente), **100 kHz**.
- Cada canal: **P0…P5 → 1 kΩ → BASE del NPN**, `EMISOR → GND`,
  `COLECTOR → IN` del módulo. El jumper `H/L` de los 6 módulos está en **LOW**
  (0 V en `IN` enciende). Como el NPN invierte, el PCF8574 enciende con **HIGH**
  y apaga con **LOW** (`BOARD_RELAY_ACTIVE_LOW = 0`, sin invertir el sombra).
- **M1–M4** usan sensor de posición por el PCF8574 `0x21` (P0–P3, entradas con
  `0xFF`). El sensor se alimenta a 12 V y su señal entra por un divisor
  **10 kΩ en serie + 3.3 kΩ a GND** (≈2.97 V en el pin). `home = LOW`
  (`BOARD_SENSOR_HOME_LEVEL = 0`), pendiente de confirmar con multímetro.
- **M5 y M6** siguen en **lazo abierto** con pulso de **3000 ms**
  (`MOTOR_TIMED_PULSE_MS`). Si `0x21` no responde al arrancar, los 6 canales
  caen automáticamente a lazo abierto.
- El expansor `0x21` debe alimentarse a **3.3 V** (Vih = 2.31 V).

### 2.3 MicroSD (SPI)

| Señal | GPIO |
| :--- | :--- |
| CS | IO5 |
| CLK | IO18 |
| MISO | IO19 |
| MOSI | IO23 |

- **VSPI confirmado** (`SPI3_HOST`), montaje FAT en `/sdcard`.
- **Desactivada en el firmware actual** (`BOARD_SD_ENABLED = 0`): las imágenes
  van en flash. El código de montaje con `esp_vfs_fat_sdspi_mount()` se conserva
  para poder reactivarla con `BOARD_SD_ENABLED = 1`.

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
maquina_expendedora.bin → 588 560 bytes (0x8fb10)
Partición app 0x100000 → 44 % libre
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
├── imagenes_productos/         # p1..p6 (bmp/png/jpg) fuente de las imágenes
├── herramientas/
│   └── generar_imagenes.ps1    # Genera main/product_images.c/.h
└── main/
    ├── CMakeLists.txt          # Dependencias + sensor_input + product_images
    ├── idf_component.yml       # LVGL, esp_lcd_st7796, esp_lcd_touch_xpt2046
    ├── board_config.h          # Pines, direcciones I2C, polaridades
    ├── main.c                  # app_main: I2C, SD opcional, tareas
    ├── motor_control.h/.c      # Relés (0x20), cola, Mutex, eventos
    ├── sensor_input.h/.c       # PCF8574 0x21: sensores M1–M4
    ├── coin_acceptor.h/.c      # ISR IO35 y decodificación de ráfagas
    ├── product_images.h/.c     # Imágenes embebidas (generado, no editar)
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
4. `motor_control_init(i2c_bus)` → configura el PCF8574 `0x20`, la cola y
   deja todos los relés apagados.
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
    (HIGH). Si la línea se queda en el nivel activo más de **1 s**, se
    **desarma** y vuelve a exigir reposo estable.
  - Al cerrar la ráfaga la línea debe haber vuelto al reposo; si no, se descarta.
  - El log avisa al arrancar si IO35 no está en reposo:
    `La linea arranco en nivel activo: revise el pull-up externo...`

### 5.3 `motor_control` — despacho

- Registro sombra de relés para no alterar los otros canales al escribir el
  PCF8574.
- Mutex interno `s_i2c_mutex` para serializar el bus I2C.
- Cola FIFO de despacho (`motor_request_t { channel, quantity, admin_test }`,
  longitud 8).
- `motor_control_dispatch_one()`:
  1. Toma `g_dispatch_mutex` (timeout configurable).
  2. Energiza el relé del canal: bit lógico ON → **LOW (0 V)** en `0x20`.
  3. Mantiene el pulso `MOTOR_TIMED_PULSE_MS` (**3000 ms**).
  4. **Apaga el relé siempre** (`cleanup`), incluso con error. OFF → **HIGH
     (3.3 V)** en `0x20`.
  5. Pausa de asentamiento **250 ms** y libera el Mutex.
- `motor_task()` procesa la cola en orden; entre unidades del mismo canal espera
  **400 ms**.
- Emite eventos al callback registrado (desde el contexto de `motor_task`):
  - `MOTOR_EVENT_UNIT_DONE` tras cada unidad entregada (pulso completado).
  - `MOTOR_EVENT_REQUEST_FAILED` si una unidad falla; incluye el código de error
    y cuántas unidades quedaron sin entregar.
  - `MOTOR_EVENT_IDLE` cuando la cola queda vacía.
- Ante un fallo, `motor_task` **descarta el resto de la cola** (el pedido se
  cancela) y luego emite `IDLE`. Las unidades ya entregadas permanecen cobradas.
- `motor_control_cancel_pending()` permite a la GUI cancelar lo que aún no se
  despacha.
- `motor_control_is_busy()` indica si hay despacho en curso o cola pendiente.

### 5.4 `gui_display` — interfaz real (ST7796 + XPT2046 + LVGL)

- Modelo de 6 productos: nombre, imagen embebida (`product_images.c`), precio,
  stock y cantidad seleccionada.
- Valores actuales: **stock inicial = 10** (persistido en NVS) y precios
  **mutables** en `s_products[i].price_cents`, persistidos en NVS. Valores de
  fábrica: **RD$ 50/50/50/70/80/90** (5000/5000/5000/7000/8000/9000 centavos).
  P4–P6 son provisionales. Ya no existe `k_product_prices` ni
  `GUI_USE_DEMO_PRICES`.
- Layout:
  - Layout Flex **2 columnas × 3 filas** con `LV_FLEX_FLOW_ROW_WRAP` en el 83 %
    superior (398 px). Cada celda mide **48 %** del ancho útil (≈150 px) y
    **127 px** de alto, lo que obliga a que solo entren dos celdas por fila.
  - Cada celda es un Flex en columna centrado: cabecera `Producto N\nRD$ XX.00`,
    recuadro de 52×52 con la imagen embebida (o `[Espacio Imagen]` si falta), y
    fila Flex con `[-]`, `Cant: N` y `[+]` (botones de 34×34 px).
  - Panel inferior fijo de **82 px** con Flex en fila: `Saldo: RD$ 0.00`,
    `Total: RD$ 0.00` (reactivo, una sola línea), botón `ADM` de 40×40 y
    `Confirmar` de 110×60 px.
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
  - Namespace NVS `vending`: `balance` (int64), `stock` (blob de 6 bytes),
    `prices` (blob de 6 × `uint32_t` = 24 bytes) y `prices_v` (uint8, versión
    del esquema de precios; actual **3**).
  - Se guarda al acreditar monedas, al cambiar stock, al guardar precios y en
    cada unidad cobrada o entregada por un test
    (`gui_display_on_unit_dispatched()`).
- **Modo administración** (botón `ADM` de 40×40 px junto a `Confirmar` +
  contraseña **20250456**, 8 dígitos): el teclado numérico a pantalla completa
  se valida antes de construir el panel del operador. El octavo dígito se envía
  solo; `C` limpia y `OK` reintenta. El panel tiene un contenedor
  Flex vertical desplazable con tres secciones:
  - **Saldo:** saldo actual y ajuste manual `-1/-5/-10/-25` y `+1/+5/+10/+25`;
    cada pulsación persiste en NVS y refresca la pantalla principal.
  - **Inventario:** stock de los 6 canales y `Rellenar Stock (Reset a 10)`.
  - **Precios:** filas con `[-]`/`[+]` (paso RD$ 5) y `Guardar Precios`. El
    borrador se aplica al estado y a NVS solo al guardar; luego se refresca la
    pantalla principal.
  - No incluye prueba de motores: la GUI ya no encola solicitudes `admin_test`.
- `task_gui` inyecta `lv_tick_inc()`, llama a `lv_timer_handler()` y refresca
  los widgets solo cuando el estado cambia (notificación + snapshot).
- Concurrencia: `s_state_mutex` protege saldo, stock, precios, cantidades, modal
  y pedido. `task_monedero` y `motor_task` solo mutan estado y despiertan a
  `task_gui`; **ninguna** llamada a LVGL ocurre fuera de `task_gui`.
- `gui_display_confirm_purchase()`:
  - Valida modal abierto, stock, total > 0 y saldo suficiente.
  - Pone a cero las cantidades de los 6 productos y registra el pedido en curso.
  - **Solo encola** en `task_despacho`; **no** descuenta saldo ni stock.
  - Si el encolado falla a mitad, cancela las unidades no encoladas y no las
    cobra.
- `order_finalize_locked()` vuelve a forzar las 6 cantidades a cero al cerrar el
  pedido (compra completada, cancelada o fallida), de modo que el carrito
  siempre queda vacío.
- `gui_display_on_unit_dispatched(channel, admin_test)`:
  - Venta real: **único punto de cobro**; resta el precio al saldo y reduce el
    stock en 1 por cada unidad cuyo pulso de 3 s se completó.
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
      → por cada pulso de 3 s completado: MOTOR_EVENT_UNIT_DONE
      → gui_display_on_unit_dispatched(): saldo -= precio, stock -= 1
      → si un motor falla: MOTOR_EVENT_REQUEST_FAILED
         → se descarta la cola; lo no entregado no se cobra ni baja stock

Administrador → botón "ADM" → contraseña 20250456 → panel → sección Saldo
      → ajuste manual +/- RD$ 1/5/10/25 → NVS + refresco de la GUI
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
    RD$ 50/50/50/70/80/90 y persistencia en NVS (`prices` + `prices_v`).
  - El modo administración permite ajustarlos en pasos de RD$ 5.
  - Stock inicial de 10 por producto y persistencia en NVS.
  - *Criterio:* los 6 productos tienen nombre y precio editables en un único
    sitio (`gui_display.c`), y precio + stock sobreviven a un reinicio.

- [ ] **Validación en la placa real (checklist §8)**
  - Flashear y confirmar en el log: `ST7796 listo`, `Tactil XPT2046 listo`,
    `Sensores listos en 0x21` (si está montado) y `Control de motores listo`.
  - Probar: monedas (1/3/5/7 pulsos), compra de 1 unidad, sensores M1–M4,
    temporización en M5–M6, cobro diferido y persistencia NVS tras reinicio.
  - *Criterio:* se completa el checklist de §8 sin fallos.

- [x] **Calibrar el táctil y los colores**
  - `BOARD_TFT_MIRROR_X = 1` y `BOARD_TFT_INVERT_COLOR = 0`.
  - `BOARD_TOUCH_MIRROR_X = 0` y `BOARD_TOUCH_MIRROR_Y = 1` para compensar la
    rotación de 180° que entrega el XPT2046 en esta placa.
  - Validado en placa: cada `+`/`−` afecta su propio canal y los colores
    verde/rojo se muestran correctamente.

### P1 — Robustez y consistencia

- [x] **Rutina de *homing***
  - Reintroducida junto con `sensor_input.c` para **M1–M4**. Al arrancar se
    puede llevar cada mecanismo a `home` antes del primer despacho.
  - Está **desactivada por seguridad** (`BOARD_SENSOR_HOMING_ENABLED = 0`)
    hasta confirmar la polaridad real del sensor con multímetro.
  - *Criterio:* activarla solo después de validar `home = LOW` y el divisor.

- [x] **Descontar stock/saldo solo al confirmar el despacho**
  - Implementado **por unidad**: `motor_task` emite `MOTOR_EVENT_UNIT_DONE`
    cuando se completa el pulso temporizado de 3 s, y solo entonces la GUI
    cobra.
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
  - Namespace `vending`: saldo (`balance`), stock de los 6 productos (`stock`),
    precios (`prices`, 6 × `uint32_t`) y versión del esquema (`prices_v`).
  - Se carga en `gui_display_init()` y se guarda al acreditar monedas, al cobrar
    o entregar una unidad y al guardar precios desde el modo administración.
  - *Criterio:* apagar y encender conserva saldo, stock y precios.

- [x] **Modo administración completo**
  - Botón `ADM` de 40×40 px en el panel inferior, junto a `Confirmar`.
  - Abre un teclado numérico con contraseña **20250456** (8 dígitos); el octavo
    dígito se valida solo. Si la clave es incorrecta se muestra
    `Contrasena incorrecta` y se limpia el campo. Solo tras el acceso correcto
    se construye el panel.
  - **Saldo:** saldo actual y botones `-1/-5/-10/-25` y `+1/+5/+10/+25`. Cada
    pulsación actualiza `s_balance_cents`, guarda en NVS y refresca la GUI.
  - **Inventario:** muestra el stock de los 6 canales y el botón
    `Rellenar Stock (Reset a 10)`, que pone todo a 10, limpia cantidades y
    persiste en NVS sin cerrar el panel.
  - **Precios:** filas con nombre, precio actual, `[-]`/`[+]` (paso RD$ 5) y
    `Guardar Precios`. Los cambios son un borrador local; al guardar se aplican
    a `s_products[].price_cents`, se persisten y se refresca la pantalla
    principal. Rango válido: RD$ 5.00–1000.00.
  - Sin prueba de motores: el panel ya no encola solicitudes `admin_test`.
  - Guardas: el ajuste de saldo, el reset y el guardado de precios se rechazan
    con `Espere a que termine el despacho` si hay una compra activa.
  - *Pendiente:* historial de ventas/recaudación.

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
| 5 | ~~Sin *homing*~~ **Reintroducido** | Homing opcional para M1–M4 (desactivado hasta validar polaridad) | `motor_control.c` |
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
- **`g_dispatch_mutex` debe existir antes de crear `task_despacho`.** Se crea en
  `app_main()` antes de `motor_control_init()`.
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
.\compilar.ps1 -Flash -Port COM6 -Monitor
```

> **Estado 2026-11-05:** flasheo verificado por **COM6**. El puerto puede cambiar
> entre sesiones: comprobarlo con
> `[System.IO.Ports.SerialPort]::GetPortNames()` y usar el que aparezca.

Salir del monitor: `Ctrl + ]`.

Al arrancar, el log esperado es similar a:

```
app_main: Maquina Expendedora - 6 canales (ESP32-32E 4.0")
app_main: Mutex de despacho creado (un motor a la vez)
app_main: Bus I2C0 listo (SDA=IO32, SCL=IO25, 100000 Hz)
motor_control: PCF8574 de relees detectado en 0x20
sensor_input: Sensores listos en 0x21: M1-M4 = P0-P3 (nivel inicial 0xFF; home = LOW)
motor_control: Control de motores listo (relees 0x20; sensores 0x21 en M1-M4; M5-M6 por tiempo)
coin_acceptor: Monedero listo en GPIO35 (activo en LOW, pulso 10-150 ms, ventana 300 ms, gracia 2500 ms)
app_main: MicroSD desactivada; las imagenes van en flash
gui_display: Precios migrados a v3: RD$ 50 en P1-P3 y RD$ 70/80/90 en P4-P6
gui_display: Tactil XPT2046 listo (CS=IO33, 1000000 Hz)
gui_display: ST7796 listo (320x480, SPI1, 40 MHz)
gui_display: GUI lista: ST7796 320x480 + XPT2046 + LVGL 8.4
app_main: Arranque completado. Sistema en espera de monedas.
```

Si la pantalla no está conectada, se emite un error y el sistema no arranca la
GUI; si el `0x21` no responde, los 6 canales pasan a lazo abierto por tiempo.

### Checklist de puesta en marcha en la placa real

1. **Flashear** con `.\compilar.ps1 -Flash -Port COMx -Monitor`.
2. **Layout:** confirmar visualmente 6 celdas en 2 columnas × 3 filas, sin
   productos agrupados en una sola fila ni solapados con el panel inferior.
3. **Confirmar el log:** debe aparecer `ST7796 listo`, `Tactil XPT2046 listo`,
   `MicroSD desactivada` y `Control de motores listo`. Si `0x21` está montado,
   también `Sensores listos en 0x21`.
4. **Colores y orientación:** si los colores salen invertidos, cambiar
   `BOARD_TFT_INVERT_COLOR`; si rojo/azul están cambiados, cambiar
   `BOARD_TFT_USE_BGR_ORDER`; si la imagen está girada/espejada, ajustar
   `BOARD_TFT_SWAP_XY/MIRROR_X/MIRROR_Y`.
5. **Táctil:** pulsar cada esquina. Si el eje X/Y está intercambiado o invertido,
   ajustar `BOARD_TOUCH_SWAP_XY/MIRROR_X/MIRROR_Y`. Repetir hasta que el botón
   pulsado coincida con el dibujado.
6. **Imágenes:** ya van embebidas. Para cambiarlas, copiar `pN.(bmp|png|jpg)` a
   `Firmware/imagenes_productos/` y ejecutar
   `herramientas/generar_imagenes.ps1`; si falta una, se muestra
   `[Espacio Imagen]`.
7. **Motores:** M1–M4 deben girar hasta que el sensor detecte el paso y volver a
   `home`; M5–M6 giran 3 s. Si `0x21` no responde, todos giran 3 s.
8. **Sensores:** con los mecanismos en reposo, el log debe mostrar `home
   (reposo)` en M1–M4. Si muestran `fuera de home`, revisar el divisor y la
   polaridad (`BOARD_SENSOR_HOME_LEVEL`).
9. **Monedero:** insertar RD$ 1/5/10/25 y verificar que el saldo sube en pantalla
   con el valor correcto.
10. **Compra de prueba:** seleccionar 1 unidad, confirmar y comprobar que el
    saldo baja **solo cuando la unidad se entrega**.
11. **Carrito:** al terminar la compra, todas las cantidades deben volver a 0.
12. **Persistencia:** apagar y encender; el saldo, el stock y los precios deben
    conservarse.
13. **Fallo controlado:** desconectar el bus I2C o bloquear un motor y verificar
    que el pedido se cancela sin cobrar las unidades no entregadas.
14. **Modo administración:** pulsar `ADM` junto a `Confirmar`; debe abrirse el
    teclado numérico. Probar:
    - Clave incorrecta: muestra `Contrasena incorrecta` y limpia el campo.
    - Clave `20250456`: abre el panel con scroll.
    - Saldo: `+1/+5/+10/+25` y `-1/-5/-10/-25` ajustan el saldo, refrescan la
      pantalla principal y sobreviven a un reinicio.
    - Inventario: `Rellenar Stock (Reset a 10)` deja los 6 productos a 10 y
      sobrevive a un reinicio.
    - Precios: `[-]`/`[+]` cambian el borrador en RD$ 5; `Guardar Precios`
      actualiza la pantalla principal y persiste tras reiniciar.
    - El panel **no** debe mostrar `Test M1`…`Test M6`.
15. **Encolado atómico:** con la UI normal es difícil llenar la cola (máximo 6
    líneas por pedido, cola de 8 y un solo pedido a la vez). Para probar el
    rechazo, reducir temporalmente `MOTOR_QUEUE_LENGTH` a 1 o añadir una prueba
    de host; el pedido debe rechazarse entero con el aviso
    "Cola ocupada: reduzca la cantidad".

---

## 9. Preguntas abiertas (datos que faltan)

> Las dos decisiones **inmediatas** (push y flasheo) están en la
> [§0](#0-preguntas-para-ti-responde-debajo-de-cada-una).

1. ~~¿El módulo de relé conmuta de forma fiable con el jumper en HIGH y entradas
   de 3.3 V?~~ **Resuelto (2026-11-04):** no era fiable. Se pasó el jumper a
   **LOW** y se añadió un **NPN inversor** por canal; el PCF8574 enciende con
   HIGH. Los 6 motores quedaron validados.
2. ¿Se requiere otro método de acceso al modo administración (SD, Wi-Fi, PIN)?
   Hoy es un botón `ADM` en el panel inferior con contraseña **20250456**.
3. ¿La versión 1.0 debe incluir telemetría Wi-Fi o se deja para V2.0?
4. ¿Un pedido parcialmente entregado debe reembolsarse completo o solo cobrar
   las unidades que salieron? (Hoy se cobran solo las entregadas).
5. ¿Qué producto, precio e imagen van en **P4–P6**? Hoy usan “Producto 4/5/6”
   con RD$ 70/80/90 **provisionales** para pruebas de electrónica.

---

## 10. Definición de "MVP terminado"

Se considerará la versión 1.0 funcional cuando:

1. La pantalla muestra el grid 2×3 con las imágenes embebidas en flash.
2. El táctil permite ajustar cantidades y confirmar con modal.
3. Los precios son editables desde el modo administración, persisten en NVS y
   el stock también sobrevive a un reinicio.
4. Insertar monedas actualiza el saldo en pantalla en < 1 s.
5. Confirmar una compra despacha los productos **uno a uno**, sin reinicios.
6. Si un despacho falla, **no se cobra** lo no entregado (hoy ya se cumple por
   unidad; falta definir el reembolso del pedido parcial).
7. Tras un corte de energía, el sistema arranca con los relés apagados y vuelve a
   operar (M1–M4 con sensor si responde; M5–M6 por tiempo).
8. Stock y recaudación sobreviven a un reinicio (NVS).

---

## 11. Instrucciones para la IA que continúe

- **Lee primero** el documento de arquitectura
  (`../Documentacion/Documentacion Tecnica/documento_de_arquitectura_tecnico.md`)
  y este archivo.
- **Respeta** las restricciones de §1 (C nativo, ESP-IDF, un motor a la vez,
  imágenes en flash, IO35 con pull-up externo).
- **No compiles directamente** en la ruta de OneDrive: usa `compilar.ps1`.
- **No edites** `C:\Users\asus\esp\maquina_expendedora` (copia temporal).
- Los pines de `board_config.h` están **confirmados físicamente** (I2C 32/25 y
  TFT/táctil). No los cambies sin verificar de nuevo la placa.
- La GUI ya está implementada: **no bloquees `task_gui`** con esperas largas y
  no llames a LVGL desde `task_monedero` ni desde `motor_task`.
- Si tocas el flujo de compra, mantén el cobro **solo** en
  `gui_display_on_unit_dispatched()`.
- La guía de despacho es única: `motor_control_dispatch_one()` toma
  `g_dispatch_mutex`, pone el bit en **HIGH** (NPN satura → `IN` a 0 V) y lo
  devuelve a **LOW** siempre. `BOARD_RELAY_ACTIVE_LOW = 0` es obligatorio con el
  NPN.
- **Sensores:** `sensor_input.c` es el único módulo que conoce el `0x21`. M1–M4
  usan lazo cerrado; M5–M6, tiempo. Si `0x21` no responde, todo cae a tiempo.
  La polaridad real (`BOARD_SENSOR_HOME_LEVEL`) sigue sin confirmarse en físico.
- **Imágenes:** `main/product_images.c/.h` es generado por
  `herramientas/generar_imagenes.ps1`; no lo edites a mano. La MicroSD está
  desactivada (`BOARD_SD_ENABLED = 0`).
- La marca `admin_test` viaja en `motor_request_t` y `motor_event_t`: si añades
  una ruta de despacho nueva, propágala para que un test nunca cobre saldo.
- Mantén el patrón actual: un `.h` con API documentada y un `.c` con
  implementación; logs con `ESP_LOGI/W/E` y `TAG` por módulo.
- Cualquier función nueva que energice un motor **debe** tomar
  `g_dispatch_mutex`.
- Si añades archivos, actualiza `main/CMakeLists.txt` y este documento.
