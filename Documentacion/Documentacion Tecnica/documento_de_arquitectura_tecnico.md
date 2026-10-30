# Arquitectura y Plan de Proyecto: Máquina Expendedora Automática (6 Canales)

> **Estado de implementación:** **Fase P1 (Firmware v1.0) completada en laboratorio.**
> Build limpio (`maquina_expendedora.bin` = **616 224 B**, 41 % de la partición libre).
> Pendiente: validación en la placa real (táctil, motores, NVS y modo administración).

## 1. Plataforma de Desarrollo y Firmware
El desarrollo del firmware NO utiliza Arduino IDE debido a sus limitaciones en entornos industriales complejos. Se ejecuta utilizando **ESP-IDF (Espressif IoT Development Framework)**, el estándar de la industria para microcontroladores ESP32, programando en **C nativo**.
*   **Sistema Operativo en Tiempo Real (FreeRTOS):** Se implementa FreeRTOS para gestionar el sistema mediante tareas concurrentes (Tasks). Esto permite aislar en diferentes hilos la lectura rápida del monedero, el control I2C de los motores y el renderizado de la interfaz gráfica sin bloqueos.
*   **Control de Versiones:** El código fuente y la documentación se gestionan de forma profesional a través de un repositorio en **Git/GitHub**.

## 2. Hardware Central y Conexiones (ESP32-32E 4.0")
*   **Pantalla Táctil TFT (4.0 pulgadas):** Actúa como la unidad central de procesamiento e interfaz (orientación vertical).
*   **Aislamiento del Monedero (Optoacoplador HiLetgo PC817):** 
    *   *Entrada (12V):* Recibe la señal del monedero (IN1) y cierra circuito al GND de la fuente de 12V.
    *   *Salida (3.3V limpia):* Recibe VCC y GND desde la pantalla. El pin de señal (OUT1) se conecta al pin digital **IO35** (Ubicado en el puerto *Expand the input pin*).
*   **Almacenamiento (MicroSD):** Uso obligatorio para cargar los activos gráficos. Las imágenes de los productos se leen desde la SD hacia la pantalla para evitar la saturación de la memoria Flash/RAM del ESP32.

## 3. Lógica del Monedero Multimoneda (Pesos Dominicanos)
El monedero emite ráfagas secuenciales que son leídas por el pin **IO35** configurado con interrupciones de hardware. La asignación de pulsos es:
*   **RD$ 1** = 1 pulso
*   **RD$ 5** = 3 pulsos
*   **RD$ 10** = 5 pulsos
*   **RD$ 25** = 7 pulsos
*   *Lógica del Firmware:* Una tarea en FreeRTOS abre una "ventana de tiempo" al detectar el primer pulso. Utilizando contadores de tiempo (milisegundos), el sistema espera hasta que pasen ~300ms sin nuevos pulsos para asumir que la ráfaga de la moneda insertada terminó, sumar el valor total y actualizar el saldo en la pantalla.
*   *Protección contra saldo fantasma:* IO35 no tiene pull-up interno y, si el monedero no está conectado, la línea flota. El firmware solo **arma** el monedero cuando la línea lleva 500 ms en reposo (HIGH), descarta pulsos de ancho fuera de 10–150 ms y ignora los primeros 2.5 s tras el arranque. Si la línea se queda en nivel activo, el monedero se desarma y el log pide revisar el pull-up externo de 10 kΩ a 3.3 V.

## 4. Control de Actuadores y Sensores (Bus I2C)

Se emplean dos módulos expansores PCF8574 en paralelo al puerto *I2C Peripheral Interface* (SDA, SCL, 3.3V, GND).

### 4.1 Estrategia híbrida de motores

| Grupo | Canales | Modelo | Hilos | Retroalimentación | Control |
| :--- | :---: | :--- | :---: | :--- | :--- |
| **M1–M4** | 1–4 | LK20052 | 3 | Micro-switch de fin de carrera | Bucle cerrado por sensor |
| **M5–M6** | 5–6 | LK20052 Fy-007 | 2 | Ninguna | Lazo abierto por tiempo |

**M1–M4 — acondicionamiento con divisor de tensión.** El micro-switch de estos motores está referenciado a los 12V del propio motor. Para proteger el expansor `0x21` sin cortar pistas ni añadir módulos, cada canal usa un divisor pasivo:

*   **10 kΩ en serie** entre la salida del switch y el puerto Pn del expansor.
*   **3.3 kΩ entre el puerto Pn y GND**, que actúa además como pull-down.

El divisor entrega **12 V × 3.3 kΩ / (10 kΩ + 3.3 kΩ) ≈ 2.97 V**, por debajo de los 3.3V del PCF8574 y por encima de su umbral de nivel alto. En reposo el switch cierra y la entrada se lee **HIGH**; cuando el mecanismo gira, el switch abre y la resistencia de 3.3 kΩ lleva la entrada a **LOW**. La placa **LK20052 se mantiene intacta: no se cortan pistas**.

La interpretación del nivel de reposo se centraliza en la macro **`SENSOR_HOME_LEVEL`** (definida en `board_config.h` y aplicada en `motor_control.c`). Con esta topología vale `1` (reposo HIGH); si el cableado invierte la señal, se cambia a `0` sin modificar el circuito.

**M5–M6 — lazo abierto.** Los motores LK20052 Fy-007 son de 2 hilos y no incorporan sensor. El firmware energiza el relé del canal en `0x20` durante un **pulso calibrado de ~2.0 s** y lo desenergiza. Las entradas **P4 y P5 del expansor `0x21` no reciben conexión física** y el firmware no las lee.

### 4.2 Matriz de canales

| Canal | Motor | Relé de Potencia (Módulo 0x20) | Sensor de Fin de Carrera (Módulo 0x21) | Estrategia |
| :--- | :---: | :--- | :--- | :--- |
| **Producto 1** | M1 | Pin P0 | Pin P0 (divisor) | Sensor |
| **Producto 2** | M2 | Pin P1 | Pin P1 (divisor) | Sensor |
| **Producto 3** | M3 | Pin P2 | Pin P2 (divisor) | Sensor |
| **Producto 4** | M4 | Pin P3 | Pin P3 (divisor) | Sensor |
| **Producto 5** | M5 | Pin P4 | **Sin conexión** | Tiempo ~2.0 s |
| **Producto 6** | M6 | Pin P5 | **Sin conexión** | Tiempo ~2.0 s |

## 5. Diseño de la Interfaz Gráfica (GUI)

La pantalla se divide visualmente en una cuadrícula (grid) de **2 columnas por 3 filas**, implementada con **LVGL 8.4** y un contenedor Flex `ROW_WRAP`.

*   **Grid de Productos (Canales 1 al 6):**
    *   Cada celda muestra la foto del producto (BMP de la MicroSD) o el marcador `[Espacio Imagen]`.
    *   Cabecera con nombre y precio: `Producto N` / `RD$ XX.00`.
    *   Botones laterales `[-]` y `[+]`; centro: `Cant: N`.
    *   *Regla:* la cantidad no puede ser inferior a 0 ni superior al stock disponible (máximo 10 por producto).
*   **Panel Inferior (80 px):**
    *   `Saldo: RD$ XX.00`, reactivo a las monedas insertadas.
    *   `Monto a Pagar: RD$ XX.00`, calculado como Σ(cantidad × precio).
    *   Botón `Confirmar`. Al accionarlo aparece un modal con el resumen y la pregunta *"¿Confirmar compra? [Sí] [No]"*.
    *   Avisos breves (*toast*) para errores de saldo, stock, cola llena o resultado del despacho.
*   **Concurrencia:** `s_state_mutex` protege saldo, stock, precios, cantidades, modal y pedido. `task_monedero` y `task_despacho` solo mutan estado protegido y despiertan a `task_gui`; **todas** las llamadas a LVGL ocurren en `task_gui`.

## 6. Encolado Atómico y Seguridad de Despacho

Para proteger la fuente conmutada de picos de corriente y caídas de tensión (lo que podría reiniciar el ESP32), el firmware usa **dos Mutex con responsabilidades distintas**:

*   **`g_dispatch_mutex` (recurso físico):** garantiza que **solo gire un motor a la vez**. Se toma antes de energizar un relé y se libera al terminar la vuelta, el pulso temporizado o el timeout. Es compartido por el despacho normal y el *homing*.
*   **`s_queue_mutex` (cola de despacho):** serializa la validación de capacidad y el encolado del pedido completo.

El encolado es **todo-o-nada**:

1.  La GUI construye una solicitud por producto y llama a `motor_control_check_capacity(count)`.
2.  `motor_control_enqueue_batch(requests, count)` vuelve a validar bajo `s_queue_mutex` que haya huecos para **todas** las solicitudes.
3.  Si no hay espacio suficiente, **no se encola ninguna** y se devuelve `ESP_ERR_TIMEOUT`. La GUI mantiene el modal abierto y muestra `Cola ocupada: reduzca la cantidad`.
4.  Si hay espacio, se encolan todas las solicitudes y se devuelve `ESP_OK`.

`motor_control_enqueue()` queda como envoltorio de una sola solicitud. Como solo la GUI produce solicitudes y `motor_task` únicamente consume, no existe otro productor que pueda ocupar los huecos entre la comprobación y el encolado. Si un motor falla durante el despacho, `motor_task` descarta el resto de la cola: las unidades ya entregadas quedan cobradas y las no entregadas no se cobran ni reducen stock.

## 7. Persistencia en NVS

El estado operativo sobrevive a cortes de energía gracias a **NVS** (flash interna del ESP32), independiente de la MicroSD usada para los activos gráficos.

*   **Namespace:** `vending`.
*   **Claves:**

| Clave | Tipo | Contenido |
| :--- | :--- | :--- |
| `balance` | `int64` | Saldo acumulado en centavos. |
| `stock` | blob de 6 bytes | Existencias de los 6 productos (`uint8_t`). |
| `prices` | blob de 24 bytes | Precios de los 6 productos (6 × `uint32_t`, centavos). |

*   **Valores por defecto** si la NVS está vacía o el blob es inválido: saldo `0`, stock `10` por producto y precios **RD$ 40/50/60/70/80/90** (`4000..9000` centavos).
*   **Carga:** `gui_display_init()` llama a `storage_load_locked()` antes de crear `task_gui`.
*   **Guardado:** al acreditar monedas, al cobrar o entregar una unidad, al restablecer stock y al guardar precios desde el modo administración.
*   La escritura NVS se hace con `s_state_mutex` tomado y nunca desde una ISR; es una operación corta, pero no deben añadirse esperas largas dentro del lock.

## 8. Modo Administración y Seguridad

El modo administración es una herramienta de mantenimiento para el operador. No forma parte del flujo de venta.

*   **Acceso por botón + contraseña:** el panel inferior tiene un botón pequeño `ADM` (46×40 px) junto a `Confirmar`. Al pulsarlo se abre un teclado numérico a pantalla completa que bloquea los productos y el panel inferior.
    *   La contraseña provisional es **0000** (`GUI_ADMIN_PASSWORD`). Se valida en el firmware y no se persiste en NVS.
    *   Al introducir el cuarto dígito se valida automáticamente; `OK` permite reintentar el envío y `C` borra lo tecleado.
    *   Con la clave incorrecta se muestra `Contrasena incorrecta` y se limpia el campo. La `X` cancela el acceso.
    *   Solo si la clave es correcta se construye el panel del operador. Ya no existe el atajo por pulsación larga ni el botón invisible de la esquina superior izquierda.
*   **Panel con scroll vertical**, compuesto por tres secciones:
    1.  **Saldo:** muestra el saldo actual y permite ajustarlo manualmente con botones `-1`, `-5`, `-10`, `-25` (rojo) y `+1`, `+5`, `+10`, `+25` (verde). Cada pulsación actualiza `s_balance_cents`, se persiste en NVS al instante y refresca la pantalla principal. El saldo se acota a **RD$ 0.00–10 000.00**.
    2.  **Inventario:** muestra el stock de los 6 canales y el botón `Rellenar Stock (Reset a 10)`. El reset limpia las cantidades seleccionadas y persiste en NVS.
    3.  **Precios:** una fila por producto con el precio actual, botones `[-]`/`[+]` en pasos de **500 centavos (RD$ 5)** y `Guardar Precios`. Los cambios son un borrador local hasta pulsar guardar; el rango válido es **RD$ 5.00–1000.00**. Al guardar se actualiza `s_products[].price_cents`, se persiste en NVS y se refresca la pantalla principal.
*   **Sin prueba de motores:** el panel de administración ya no incluye `Test M1`…`Test M6`. El soporte `admin_test` sigue existiendo en `motor_control.c` y `main.c` por compatibilidad, pero la GUI no lo utiliza.
*   **Guardas:** el ajuste de saldo, el reset de stock y el guardado de precios se rechazan con `Espere a que termine el despacho` si hay una compra en curso. El botón `ADM` tampoco abre el teclado mientras haya un pedido activo o un modal abierto.

## 9. Homing Secuencial de Arranque

Al final de `motor_control_init()` se ejecuta `motor_control_home_all()` **antes** de crear `task_despacho`:

*   Recorre únicamente los canales **M1–M4**, que son los que tienen sensor (`PCF8574` en `0x21`, pines P0–P3). **P4 y P5 no se leen** porque M5 y M6 no tienen retroalimentación.
*   Si el canal ya está en reposo (nivel igual a `SENSOR_HOME_LEVEL`, por defecto HIGH), no hace nada.
*   Si está fuera de reposo, toma `g_dispatch_mutex`, energiza su relé (`PCF8574` en `0x20`) y espera hasta que el sensor alcance el nivel de reposo o venza el **timeout de 10 s por canal**.
*   El relé se apaga **siempre**, incluso ante timeout o error, y se deja una pausa de asentamiento de 250 ms.
*   El proceso es **secuencial, canal por canal**: nunca hay dos motores alimentados a la vez. Si un canal no alcanza el reposo, se registra el error y se continúa con el siguiente para no bloquear el arranque.
*   Los canales **M5–M6** no participan del homing: se despachan en lazo abierto con un pulso de ~2.0 s cuando se solicita una unidad.

## 10. Escalabilidad (Hardware V2.0)

El mapa de pines deja libre varios puertos estratégicos para futuras versiones:

1.  **Telemetría y App:** uso del Wi-Fi interno del ESP32 para enviar reportes de ganancias y niveles de stock a una base de datos/App celular.
2.  **Audio:** puerto *Speaker Interface* libre para conectar un altavoz para feedback sonoro (sonido de caída de moneda o alertas de error).
3.  **Lector RFID:** puerto *SPI Peripheral Interface* libre para añadir cobros mediante tarjetas corporativas sin contacto.
