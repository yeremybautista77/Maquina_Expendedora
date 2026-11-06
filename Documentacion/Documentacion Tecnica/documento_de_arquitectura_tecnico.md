# Arquitectura y Plan de Proyecto: Máquina Expendedora Automática (6 Canales)

> **Estado de implementación:** **Firmware v1.0 (2026-11-05).**
> Build limpio (`maquina_expendedora.bin` = **588 560 B / 0x8fb10**, 44 % de la
> partición libre). Los 6 motores (M1–M6), la pantalla, el táctil y la NVS se
> validaron físicamente. Los **sensores de posición M1–M4** con el segundo
> PCF8574 (`0x21`) y el **monedero CH-926** están implementados y pendientes de
> prueba física; las imágenes van embebidas en flash y la MicroSD está
> desactivada.

## 1. Plataforma de Desarrollo y Firmware
El desarrollo del firmware NO utiliza Arduino IDE debido a sus limitaciones en entornos industriales complejos. Se ejecuta utilizando **ESP-IDF (Espressif IoT Development Framework)**, el estándar de la industria para microcontroladores ESP32, programando en **C nativo**.
*   **Sistema Operativo en Tiempo Real (FreeRTOS):** Se implementa FreeRTOS para gestionar el sistema mediante tareas concurrentes (Tasks). Esto permite aislar en diferentes hilos la lectura rápida del monedero, el control I2C de los motores y el renderizado de la interfaz gráfica sin bloqueos.
*   **Control de Versiones:** El código fuente y la documentación se gestionan de forma profesional a través de un repositorio en **Git/GitHub**.

## 2. Hardware Central y Conexiones (ESP32-32E 4.0")
*   **Pantalla Táctil TFT (4.0 pulgadas):** Actúa como la unidad central de procesamiento e interfaz (orientación vertical).
*   **Aislamiento del Monedero (Optoacoplador HiLetgo PC817):** 
    *   *Entrada (12V):* Recibe la señal del monedero (IN1) y cierra circuito al GND de la fuente de 12V.
    *   *Salida (3.3V limpia):* Recibe VCC y GND desde la pantalla. El pin de señal (OUT1) se conecta al pin digital **IO35** (Ubicado en el puerto *Expand the input pin*).
*   **Almacenamiento gráfico (flash interna):** las imágenes de los productos van **embebidas en el firmware** (`main/product_images.c`, 52×52 RGB565) y se generan con `herramientas/generar_imagenes.ps1`. La **MicroSD está desactivada** (`BOARD_SD_ENABLED = 0`) porque ya no se usa.

## 3. Lógica del Monedero Multimoneda (Pesos Dominicanos)
El monedero emite ráfagas secuenciales que son leídas por el pin **IO35** configurado con interrupciones de hardware. La asignación de pulsos es:
*   **RD$ 1** = 1 pulso
*   **RD$ 5** = 3 pulsos
*   **RD$ 10** = 5 pulsos
*   **RD$ 25** = 7 pulsos
*   *Lógica del Firmware:* Una tarea en FreeRTOS abre una "ventana de tiempo" al detectar el primer pulso. Utilizando contadores de tiempo (milisegundos), el sistema espera hasta que pasen ~300ms sin nuevos pulsos para asumir que la ráfaga de la moneda insertada terminó, sumar el valor total y actualizar el saldo en la pantalla.
*   *Protección contra saldo fantasma:* IO35 no tiene pull-up interno y, si el monedero no está conectado, la línea flota. El firmware solo **arma** el monedero cuando la línea lleva 500 ms en reposo (HIGH), descarta pulsos de ancho fuera de 10–150 ms y ignora los primeros 2.5 s tras el arranque. Si la línea se queda en nivel activo, el monedero se desarma y el log pide revisar el pull-up externo de 10 kΩ a 3.3 V.

## 4. Control de Actuadores (Bus I2C)

Se emplean **dos módulos expansores PCF8574** en el puerto *I2C Peripheral Interface* (SDA, SCL, 3.3V, GND): `0x20` para los relés y `0x21` para los sensores de posición.

### 4.1 Estrategia de motores: lazo cerrado en M1–M4 y tiempo en M5–M6

| Grupo | Canales | Modelo | Hilos | Retroalimentación | Control |
| :--- | :---: | :--- | :---: | :--- | :--- |
| **M1–M4** | 1–4 | LK20052 | 3 | Sensor de posición (`0x21`, P0–P3) | Lazo cerrado · salir de `home` + volver a `home` |
| **M5–M6** | 5–6 | LK20052 Fy-007 | 2 | Ninguna | Lazo abierto · pulso de 3.0 s |

Secuencia para **M1–M4** (lazo cerrado):

1.  El firmware toma el Mutex de despacho (solo un motor a la vez).
2.  Escribe el bit del canal en `0x20` en **HIGH (3.3 V)**: el NPN satura y lleva el `IN` del relé a **0 V**; el motor recibe 12 V.
3.  Espera a que el sensor **salga de `home`** y luego a que **vuelva a `home`**.
4.  Escribe el bit en **LOW (0 V)** y apaga el relé siempre en `cleanup`.
5.  Libera el Mutex tras una pausa de asentamiento de 250 ms.

Si `0x21` no responde al arrancar, **los 6 canales** usan el pulso fijo de **3000 ms** (`MOTOR_TIMED_PULSE_MS`). Si el sensor falla durante el giro, el relé se apaga y se reporta el error sin cobrar la unidad.

| Paso | Bit en `0x20` | P0…P5 | NPN | Nivel en `IN` | Relé | Motor |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| Reposo | 0 | LOW ≈ 0 V | Corte | Alto | Desenergizado | Detenido |
| Despacho | 1 | HIGH ≈ 3.3 V | Saturado | LOW ≈ 0 V | Energizado | Girando |
| Fin | 0 | LOW ≈ 0 V | Corte | Alto | Desenergizado | Detenido |

> **Nota de calibración.** En M5–M6 los 3000 ms deben ajustarse con el espiral cargado y la tensión real de la fuente. En M1–M4 el sensor define la parada; el timeout del firmware evita que el motor gire indefinidamente.

**Sensores M1–M4.** Cada sensor se alimenta a 12 V y su señal entra al `0x21` por un divisor **10 kΩ en serie + 3.3 kΩ a GND** (≈2.97 V en el pin). `home = LOW` (`BOARD_SENSOR_HOME_LEVEL = 0`), pendiente de confirmar con multímetro. El `0x21` se alimenta a **3.3 V** (Vih = 2.31 V). El *homing* de arranque existe pero está desactivado (`BOARD_SENSOR_HOMING_ENABLED = 0`) hasta validar la polaridad.

**Configuración física de los relés.** Se usan **6 módulos de 1 canal** (SRD-12VDC-SL-C) con el **jumper `H/L` en LOW**: la entrada `IN` se activa con **LOW (0 V)**. Como el PCF8574 solo hunde corriente de forma fiable, cada canal lleva un **transistor NPN inversor** (`EMISOR`→GND, `BASE`→1 kΩ→P0…P5, `COLECTOR`→`IN`). Así el PCF8574 enciende con **HIGH** y apaga con **LOW**. `BOARD_RELAY_ACTIVE_LOW = 0` y el registro sombra se escribe sin invertir.

### 4.2 Matriz de canales

| Canal | Motor | Relé (0x20) | Sensor (0x21) | Estrategia |
| :--- | :---: | :--- | :--- | :--- |
| **Producto 1** | M1 | Pin P0 | Pin P0 | Lazo cerrado |
| **Producto 2** | M2 | Pin P1 | Pin P1 | Lazo cerrado |
| **Producto 3** | M3 | Pin P2 | Pin P2 | Lazo cerrado |
| **Producto 4** | M4 | Pin P3 | Pin P3 | Lazo cerrado |
| **Producto 5** | M5 | Pin P4 | — | Tiempo · 3.0 s |
| **Producto 6** | M6 | Pin P5 | — | Tiempo · 3.0 s |

*P6 y P7 de `0x20` no se usan; P4–P7 de `0x21` quedan libres.*

## 5. Diseño de la Interfaz Gráfica (GUI)

La pantalla se divide visualmente en una cuadrícula (grid) de **2 columnas por 3 filas**, implementada con **LVGL 8.4** y un contenedor Flex `ROW_WRAP`.

*   **Grid de Productos (Canales 1 al 6):**
    *   Cada celda muestra la imagen del producto **embebida en flash** (`product_images.c`, 52×52 RGB565) o el marcador `[Espacio Imagen]`.
    *   Cabecera con nombre y precio: `Galletas chocochip`, `Galletas Saladas`, `Brownies` y `Producto 4/5/6` con su precio.
    *   Botones laterales `[-]` y `[+]`; centro: `Cant: N`.
    *   *Regla:* la cantidad no puede ser inferior a 0 ni superior al stock disponible (máximo 10 por producto).
*   **Panel Inferior (82 px):**
    *   `Saldo: RD$ XX.00`, reactivo a las monedas insertadas.
    *   `Total: RD$ XX.00`, calculado como Σ(cantidad × precio) y mostrado en una sola línea.
    *   Botón `Confirmar`. Al accionarlo aparece un modal con el resumen y la pregunta *"¿Confirmar compra? [Sí] [No]"*.
    *   Avisos breves (*toast*) para errores de saldo, stock, cola llena o resultado del despacho.
    *   Al terminar la compra, todas las cantidades se reinician a cero.
*   **Concurrencia:** `s_state_mutex` protege saldo, stock, precios, cantidades, modal y pedido. `task_monedero` y `task_despacho` solo mutan estado protegido y despiertan a `task_gui`; **todas** las llamadas a LVGL ocurren en `task_gui`.

## 6. Encolado Atómico y Seguridad de Despacho

Para proteger la fuente conmutada de picos de corriente y caídas de tensión (lo que podría reiniciar el ESP32), el firmware usa **dos Mutex con responsabilidades distintas**:

*   **`g_dispatch_mutex` (recurso físico):** garantiza que **solo gire un motor a la vez**. Se toma antes de energizar un relé y se libera al terminar el pulso temporizado o ante un error. Es compartido por el despacho normal y la prueba de bring-up.
*   **`s_queue_mutex` (cola de despacho):** serializa la validación de capacidad y el encolado del pedido completo.

El encolado es **todo-o-nada**:

1.  La GUI construye una solicitud por producto y llama a `motor_control_check_capacity(count)`.
2.  `motor_control_enqueue_batch(requests, count)` vuelve a validar bajo `s_queue_mutex` que haya huecos para **todas** las solicitudes.
3.  Si no hay espacio suficiente, **no se encola ninguna** y se devuelve `ESP_ERR_TIMEOUT`. La GUI mantiene el modal abierto y muestra `Cola ocupada: reduzca la cantidad`.
4.  Si hay espacio, se encolan todas las solicitudes y se devuelve `ESP_OK`.

`motor_control_enqueue()` queda como envoltorio de una sola solicitud. Como solo la GUI produce solicitudes y `motor_task` únicamente consume, no existe otro productor que pueda ocupar los huecos entre la comprobación y el encolado. Si un motor falla durante el despacho, `motor_task` descarta el resto de la cola: las unidades ya entregadas quedan cobradas y las no entregadas no se cobran ni reducen stock.

## 7. Persistencia en NVS

El estado operativo sobrevive a cortes de energía gracias a **NVS** (flash interna del ESP32), independiente de la MicroSD (que está desactivada).

*   **Namespace:** `vending`.
*   **Claves:**

| Clave | Tipo | Contenido |
| :--- | :--- | :--- |
| `balance` | `int64` | Saldo acumulado en centavos. |
| `stock` | blob de 6 bytes | Existencias de los 6 productos (`uint8_t`). |
| `prices` | blob de 24 bytes | Precios de los 6 productos (6 × `uint32_t`, centavos). |
| `prices_v` | `uint8` | Versión del esquema de precios (actual `3`). |

*   **Valores por defecto** si la NVS está vacía o el blob es inválido: saldo `0`, stock `10` por producto y precios **RD$ 50/50/50/70/80/90** (`5000/5000/5000/7000/8000/9000` centavos). P4–P6 son provisionales.
*   **Migración:** si `prices_v` es menor que la versión actual, se aplican los precios definidos en el firmware una sola vez y se graba la versión nueva.
*   **Carga:** `gui_display_init()` llama a `storage_load_locked()` antes de crear `task_gui`.
*   **Guardado:** al acreditar monedas, al cobrar o entregar una unidad, al restablecer stock y al guardar precios desde el modo administración.
*   La escritura NVS se hace con `s_state_mutex` tomado y nunca desde una ISR; es una operación corta, pero no deben añadirse esperas largas dentro del lock.

## 8. Modo Administración y Seguridad

El modo administración es una herramienta de mantenimiento para el operador. No forma parte del flujo de venta.

*   **Acceso por botón + contraseña:** el panel inferior tiene un botón pequeño `ADM` (40×40 px) junto a `Confirmar`. Al pulsarlo se abre un teclado numérico a pantalla completa que bloquea los productos y el panel inferior.
    *   La contraseña es **20250456** (8 dígitos, `GUI_ADMIN_PASSWORD`). Se valida en el firmware y no se persiste en NVS.
    *   Al introducir el octavo dígito se valida automáticamente; `OK` permite reintentar el envío y `C` borra lo tecleado.
    *   Con la clave incorrecta se muestra `Contrasena incorrecta` y se limpia el campo. La `X` cancela el acceso.
    *   Solo si la clave es correcta se construye el panel del operador. Ya no existe el atajo por pulsación larga ni el botón invisible de la esquina superior izquierda.
*   **Panel con scroll vertical**, compuesto por tres secciones:
    1.  **Saldo:** muestra el saldo actual y permite ajustarlo manualmente con botones `-1`, `-5`, `-10`, `-25` (rojo) y `+1`, `+5`, `+10`, `+25` (verde). Cada pulsación actualiza `s_balance_cents`, se persiste en NVS al instante y refresca la pantalla principal. El saldo se acota a **RD$ 0.00–10 000.00**.
    2.  **Inventario:** muestra el stock de los 6 canales y el botón `Rellenar Stock (Reset a 10)`. El reset limpia las cantidades seleccionadas y persiste en NVS.
    3.  **Precios:** una fila por producto con el precio actual, botones `[-]`/`[+]` en pasos de **500 centavos (RD$ 5)** y `Guardar Precios`. Los cambios son un borrador local hasta pulsar guardar; el rango válido es **RD$ 5.00–1000.00**. Al guardar se actualiza `s_products[].price_cents`, se persiste en NVS y se refresca la pantalla principal.
*   **Sin prueba de motores:** el panel de administración ya no incluye `Test M1`…`Test M6`. El soporte `admin_test` sigue existiendo en `motor_control.c` y `main.c` por compatibilidad, pero la GUI no lo utiliza.
*   **Guardas:** el ajuste de saldo, el reset de stock y el guardado de precios se rechazan con `Espere a que termine el despacho` si hay una compra en curso. El botón `ADM` tampoco abre el teclado mientras haya un pedido activo o un modal abierto.

## 9. Despacho y Protección

El despacho de cada unidad se ejecuta íntegramente en `motor_control_dispatch_one()`:

1.  Toma `g_dispatch_mutex` (solo un motor a la vez).
2.  Energiza el relé del canal escribiendo **HIGH (3.3 V)** en el bit correspondiente de `0x20` (satura el NPN y el `IN` del relé va a 0 V).
3.  **M1–M4:** espera a que el sensor los saque de `home` y luego a que vuelvan a `home`. **M5–M6:** mantiene el pulso **3000 ms** (`MOTOR_TIMED_PULSE_MS`).
4.  Apaga el relé **siempre** en el bloque `cleanup`, incluso si hubo un error de I2C o del sensor, escribiendo **LOW (0 V)**.
5.  Espera 250 ms de asentamiento y libera el Mutex.

No existe *homing* activo en la versión actual (`BOARD_SENSOR_HOMING_ENABLED = 0`): tras un corte de energía el sistema arranca con todos los relés apagados y el despacho vuelve a empezar desde la posición en que quedó el mecanismo. Si el bus I2C o el sensor fallan, `motor_control` reporta el error a la GUI y el relé no queda activo de forma indefinida.

Al cerrar cualquier pedido (completado o fallido), la GUI vuelve a poner en cero las cantidades de los 6 productos.

## 10. Escalabilidad (Hardware V2.0)

El mapa de pines deja libre varios puertos estratégicos para futuras versiones:

1.  **Telemetría y App:** uso del Wi-Fi interno del ESP32 para enviar reportes de ganancias y niveles de stock a una base de datos/App celular.
2.  **Audio:** puerto *Speaker Interface* libre para conectar un altavoz para feedback sonoro (sonido de caída de moneda o alertas de error).
3.  **Lector RFID:** puerto *SPI Peripheral Interface* libre para añadir cobros mediante tarjetas corporativas sin contacto.
