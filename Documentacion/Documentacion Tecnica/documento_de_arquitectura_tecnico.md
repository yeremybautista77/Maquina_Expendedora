# Arquitectura y Plan de Proyecto: Máquina Expendedora Automática (6 Canales)

## 1. Plataforma de Desarrollo y Firmware
El desarrollo del firmware NO utilizará Arduino IDE debido a sus limitaciones en entornos industriales complejos. Se ejecutará utilizando **ESP-IDF (Espressif IoT Development Framework)**, el estándar de la industria para microcontroladores ESP32, programando en **C nativo**.
*   **Sistema Operativo en Tiempo Real (FreeRTOS):** Se implementará FreeRTOS para gestionar el sistema mediante tareas concurrentes (Tasks). Esto permite aislar en diferentes hilos la lectura rápida del monedero, el control I2C de los motores y el renderizado de la interfaz gráfica sin bloqueos.
*   **Control de Versiones:** El código fuente y la documentación se gestionarán de forma profesional a través de un repositorio en **Git/GitHub**.

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

## 4. Control de Actuadores y Sensores (Bus I2C)
Se emplean dos módulos expansores PCF8574 en paralelo al puerto *I2C Peripheral Interface* (SDA, SCL, 3.3V, GND). 

*Seguridad Eléctrica (Contacto Seco):* Para evitar quemar el expansor de sensores (3.3V) con la corriente de los motores (12V), se cortarán físicamente con un bisturí las pistas de cobre de las placas LK20052 en cada motor. Esto aísla el micro-switch, permitiendo que actúe únicamente conectando el pin del expansor `0x21` a Tierra (GND) para señalar la vuelta completa (estado lógico LOW).

| Canal | Relé de Potencia (Módulo 0x20) | Sensor de Fin de Carrera (Módulo 0x21) |
| :--- | :--- | :--- |
| **Producto 1** | Pin P0 | Pin P0 |
| **Producto 2** | Pin P1 | Pin P1 |
| **Producto 3** | Pin P2 | Pin P2 |
| **Producto 4** | Pin P3 | Pin P3 |
| **Producto 5** | Pin P4 | Pin P4 |
| **Producto 6** | Pin P5 | Pin P5 |

## 5. Diseño de la Interfaz Gráfica (GUI)
La pantalla se dividirá visualmente en una cuadrícula (grid) de **2 columnas por 3 filas**.
*   **Grid de Productos (Canales 1 al 6):** 
    *   Cada celda mostrará la foto del producto.
    *   Justo debajo de la foto habrá un control de inventario/selección.
    *   Botones laterales: `[-]` y `[+]`.
    *   Centro: `Cantidad: 0` (Este número aumenta o disminuye al pulsar).
    *   *Regla:* El valor no puede ser inferior a 0 ni superior al stock máximo (ej. 10).
*   **Panel Inferior:**
    *   **Display de Saldo:** Si el acumulado es 0, muestra `"Ingrese monedas"`. Si hay saldo, muestra `RD$ XX.00`.
    *   **Botón de Acción:** `"Confirmar compra"`. Al accionarlo, aparece una pantalla emergente (Modal) con el resumen de los productos seleccionados y la pregunta *"¿Confirmar compra? [Sí] [No]"*.

## 6. Lógica de Seguridad de Despacho (Mutex)
Para proteger la fuente conmutada de picos de corriente y caídas de tensión (lo que podría reiniciar el ESP32), el firmware implementará un mecanismo de bloqueo llamado **Mutex** dentro de FreeRTOS.
*   Si el usuario compra múltiples productos, el ESP32 bloqueará el recurso de despacho para que **solo gire un motor a la vez**.
*   El motor 1 girará, se detendrá al recibir la señal del sensor en `0x21`, liberará el Mutex, y luego arrancará el motor 2, operando en una cola secuencial segura.

## 7. Escalabilidad (Hardware V2.0)
El mapa de pines deja libre varios puertos estratégicos para futuras versiones:
1.  **Telemetría y App:** Uso del Wi-Fi interno del ESP32 para enviar reportes de ganancias y niveles de stock a una base de datos/App celular.
2.  **Audio:** Puerto *Speaker Interface* libre para conectar un altavoz para feedback sonoro (sonido de caída de moneda o alertas de error).
3.  **Lector RFID:** Puerto *SPI Peripheral Interface* libre para añadir cobros mediante tarjetas corporativas sin contacto.