# Documento de Diseño y Arquitectura: Máquina Expendedora Vending ESP32

## 1. Descripción del Proyecto
Desarrollo de una máquina expendedora automatizada de 6 canales (espirales) de nivel industrial y bajo costo. El sistema utiliza una arquitectura de control distribuida centrada en un microcontrolador ESP32 con pantalla táctil de 4.0 pulgadas. Acepta pagos mediante un validador multimoneda y despacha productos usando motores reductores con retroalimentación mecánica de final de carrera aislado.

## 2. Interfaz Gráfica de Usuario (GUI)
La pantalla de 4.0 pulgadas se utilizará en **orientación vertical**. La interfaz principal consta de:
* **Matriz de Productos:** 6 espacios, uno por cada carril.
* **Selector por Producto:** 
  * Imagen representativa del producto.
  * Botón menos `[-]` a la izquierda.
  * Botón más `[+]` a la derecha.
  * Texto central: `Cantidad: 0`.
  * *Lógica:* No puede bajar de 0 ni superar el máximo de 10 unidades por selección.
* **Barra Inferior de Estado y Compra:**
  * **Indicador de Saldo:** Si el saldo es 0, muestra el texto `"Ingrese monedas"`. Si hay saldo, muestra la cantidad disponible.
  * **Botón de Acción:** Un botón de `"Confirmar compra"` siempre visible. Al presionarlo, despliega una pantalla secundaria emergente pidiendo al usuario confirmar la selección (`Sí` / `No`).

## 3. Lógica de Pago (Monedero Joyzan)
El monedero está configurado para emitir pulsos a 12V que son aislados a 3.3V mediante el módulo optoacoplador HiLetgo (PC817). 
La conversión de denominación a pulsos es la siguiente:
* **Moneda RD$ 5:** 1 pulso.
* **Moneda RD$ 10:** 3 pulsos.
* **Moneda RD$ 25:** 5 pulsos.
* *Nota de software:* Se utiliza una interrupción de hardware y la función `millis()` (con una ventana de tiempo) para agrupar los pulsos de una misma ráfaga antes de actualizar el saldo en la pantalla.

## 4. Hardware de Actuación y Sensado
* **Relés (12V DC):** 6 módulos de relé que gobiernan la potencia de los motores.
* **Motores (JGY370):** Con micro-switch de final de carrera integrado. *Modificación:* Las pistas de cobre de la placa del motor se cortan para aislar el botón de los 12V del motor, obteniendo un contacto seco puro.
* **Mapeo Lógico I2C:** Relación directa 1 a 1. 
  * Expansor `0x20` (P0) -> Enciende Relé 1 -> Gira Producto 1.
  * Expansor `0x21` (P0) -> Lee Micro-switch 1 -> Detiene Producto 1.

## 5. Mapa de Conexiones Principales (ESP32 4.0")
* **Alimentación General:** Fuente conmutada de 12V alimenta motores y monedero. Un módulo Step-Down LM2596 reduce de 12V a 5V para energizar el ESP32 por el puerto USB-C.
* **Bus I2C (Módulos NOYITO PCF8574):** Conectados al puerto dedicado `I2C Peripheral Interface` (3.3V, GND, SDA, SCL) en la esquina superior derecha de la placa.
* **Interrupción del Monedero:** La salida `OUT1` del optoacoplador se conecta al pin `IO35` del conector `Expand the input pin`.
* **Imágenes de Productos:** Se almacenan obligatoriamente en una memoria insertada en el `MicroSD Card Slot` para no saturar la memoria Flash del ESP32.

## 6. Escalabilidad y Futuras Expansiones (V2.0)
La placa actual posee puertos libres que permiten escalar comercialmente el proyecto:
1. **Aplicación Móvil (Telemetría):** Uso del Wi-Fi nativo del ESP32 para visualizar estadísticas de ventas e inventario a distancia desde un smartphone.
2. **Alertas Sonoras:** Uso del conector `Speaker Interface` para agregar una bocina que reproduzca sonidos al insertar monedas o despachar con éxito.
3. **Pagos Institucionales:** Uso del conector `SPI Peripheral Interface` para integrar un lector de tarjetas RFID, permitiendo cobros mediante carnets universitarios o empresariales.