# Arquitectura y Plan de Proyecto: Máquina Expendedora Automática (6 Canales)

## 1. Plataforma de Desarrollo y Firmware
El desarrollo del firmware se ejecutará utilizando **ESP-IDF (Espressif IoT Development Framework)**, el estándar industrial para programación de microcontroladores ESP32 en **C nativo**.
*   **Sistema Operativo en Tiempo Real (FreeRTOS):** Se implementará FreeRTOS para gestionar el sistema mediante tareas concurrentes (Tasks). Esto permite aislar en diferentes hilos la lectura de interrupciones del monedero, el control I2C de los motores y el renderizado de la interfaz gráfica sin bloqueos.
*   **Control de Versiones:** El código fuente, la documentación y los esquemas se gestionarán a través de un repositorio en **Git/GitHub**.

## 2. Hardware y Conexiones Principales (ESP32-32E 4.0")
*   **Pantalla Táctil TFT (4.0 pulgadas):** Actúa como la interfaz gráfica (GUI) e integra el microcontrolador central.
*   **Módulo Optoacoplador (HiLetgo PC817 - 2 Canales):** Aislamiento galvánico para proteger al ESP32.
    *   *Entrada (12V):* Señal del monedero (IN1) y GND de la fuente principal de 12V.
    *   *Salida (3.3V):* VCC (3.3V), GND y OUT1 conectado al pin digital **IO35** del puerto *Expand the input pin* del ESP32.
*   **Almacenamiento Gráfico (MicroSD):** Es mandatorio el uso de la ranura MicroSD para almacenar las imágenes (fotos reales) de los productos. El ESP32 leerá estos archivos bajo demanda para renderizar la interfaz, manteniendo la memoria RAM libre para los procesos de FreeRTOS.

## 3. Lógica del Monedero Multimoneda
El conteo se realiza decodificando ráfagas de pulsos enviadas al pin **IO35**, configurado con interrupciones de hardware:
*   Moneda de **RD$ 5** = 1 pulso
*   Moneda de **RD$ 10** = 3 pulsos
*   Moneda de **RD$ 25** = 5 pulsos
*   *Lógica de software:* Una tarea de FreeRTOS detecta el pulso inicial, acumula los consecutivos y, al detectar un tiempo de inactividad de la señal (ej. 300 ms), procesa el valor total y actualiza la variable de saldo global.

## 4. Mapeo de Expansión I2C (Motores y Sensores)
Se emplean dos módulos expansores de bus PCF8574 conectados en paralelo al puerto *I2C Peripheral Interface* (SDA, SCL, 3.3V, GND). Los actuadores son relés mecánicos de 12V DC.

| Producto | Relé de Activación (Módulo 0x20) | Sensor de Fin de Carrera (Módulo 0x21) |
| :--- | :--- | :--- |
| **Producto 1** | Pin P0 | Pin P0 |
| **Producto 2** | Pin P1 | Pin P1 |
| **Producto 3** | Pin P2 | Pin P2 |
| **Producto 4** | Pin P3 | Pin P3 |
| **Producto 5** | Pin P4 | Pin P4 |
| **Producto 6** | Pin P5 | Pin P5 |

*(Nota de Integración: Los micro-switches integrados en los motores JGY370 son modificados físicamente realizando cortes en las pistas de cobre de su placa base [LK20052], para aislarlos de los 12V de potencia. Proveen un cierre de contacto limpio a GND para el expansor 0x21).*

## 5. Diseño de la Interfaz Gráfica (GUI)
La pantalla de 4.0" opera en orientación vertical.
*   **Grid de Productos:** Seis bloques de selección. Cada uno contiene:
    *   Imagen del producto (JPG/BMP leída desde la MicroSD).
    *   Selector de cantidades en la parte inferior: Botón `[-]`, Texto dinámico `Cantidad: 0`, Botón `[+]`.
    *   Restricciones lógicas: Mínimo 0, Máximo 10 por producto.
*   **Panel Inferior de Control:**
    *   **Estado de Cuenta:** Refleja el saldo en tiempo real. Si es igual a 0, el texto muestra la instrucción *"Ingrese monedas"*.
    *   **Acción:** Botón de *"Confirmar compra"*. Al ser accionado, invoca un cuadro de diálogo secundario (Modal) superpuesto solicitando validación: *"¿Confirmar selección? [Sí] [No]"*.

## 6. Escalabilidad Futura
El diseño mecatrónico permite la expansión modular sin alterar el núcleo:
*   **Speaker Interface:** Puerto habilitado para un altavoz que provea retroalimentación auditiva (ej. ingreso de monedas o errores).
*   **SPI Peripheral Interface:** Puerto de alta velocidad disponible para integrar sistemas de pago cerrado corporativo (lectores RFID/NFC).
*   **IoT y Telemetría:** Uso del transceptor Wi-Fi nativo del ESP32 para desarrollar una aplicación móvil o *Dashboard* remoto, permitiendo monitorizar métricas de ventas y estado del inventario en la nube.