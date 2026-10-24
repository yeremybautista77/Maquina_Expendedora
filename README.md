# Máquina Expendedora Vending ESP32

## 📌 Descripción del Proyecto

Desarrollo de una **máquina expendedora de 6 canales (espirales) de nivel industrial y bajo costo**. El sistema utiliza una **arquitectura de control distribuida centrada en un microcontrolador ESP32** con pantalla táctil integrada. Acepta pagos mediante un validador multimoneda y despacha productos usando motores reductores con retroalimentación mecánica de final de carrera, garantizando precisión sin necesidad de sensores infrarrojos adicionales.

---

## 🛠️ Plataforma de Desarrollo y Firmware

El desarrollo del firmware se ejecutará utilizando **ESP-IDF (Espressif IoT Development Framework)**, el estándar industrial para programación de microcontroladores ESP32 en **C nativo**.

- **Sistema Operativo en Tiempo Real (FreeRTOS):** Se implementará FreeRTOS para gestionar el sistema mediante tareas concurrentes (Tasks). Esto permite aislar en diferentes hilos la lectura de interrupciones del monedero, el control I2C de los motores y el renderizado de la interfaz gráfica sin bloqueos.
- **Control de Versiones:** El código fuente, la documentación y los esquemas se gestionarán a través de un repositorio en **Git/GitHub**.

---

## 📋 Lista de Materiales (BOM)

### Control e Interfaz
- **1x Pantalla Táctil TFT (4.0 pulgadas):** Integra el ESP32-WROOM-32E, Wi-Fi/BT, y puertos de expansión JST. Activa como el cerebro y la interfaz gráfica (GUI) del sistema.

### Sistema de Pago y Aislamiento
- **1x Monedero Multimoneda Joyzan (CH-926):** Configurado para leer monedas de RD$ 5 (1 pulso), RD$ 10 (3 pulsos) y RD$ 25 (5 pulsos). Opera a 12V.
- **1x Módulo Optoacoplador HiLetgo (PC817 de 2 canales):** Aísla físicamente la señal de 12V del monedero, convirtiéndola en un pulso seguro de 3.3V para el ESP32.

### Actuadores y Expansión I/O
- **6x Motores Reductores 12V DC (JGY370/JGA25-370):** Equipados con leva y *micro-switch* trasero. Una revolución exacta de 360° = 1 evento de activación del switch.
- **2x Módulos Expansores I2C PCF8574 (NOYITO - Rojos):** Poseen DIP switches para configurar la dirección en el bus I2C.
- **6x Módulos de Relé (1 Canal, 5V):** Controlan el paso de la corriente de 12V hacia los motores.

### Potencia
- **1x Fuente Conmutada Principal:** 110V AC a 12V DC (Mínimo 10 Amperios).
- **1x Módulo Step-Down DC-DC (LM2596):** Reduce los 12V a 5V/3.3V para alimentar la lógica y los relés.

---

## 🗺️ Mapa de Conexiones y Distribución Eléctrica

### A. Líneas de Alimentación
- **Línea 12V:** Alimenta directamente al Monedero Joyzan y a las terminales de carga (COM/NO) de los 6 relés que van a los motores.
- **Línea 5V (Desde el Step-Down):** Alimenta las bobinas de los 6 relés, los módulos PCF8574 y, de ser necesario, el puerto de entrada de poder de la pantalla ESP32.
- **Línea 3.3V:** Generada por el propio ESP32 para sus señales lógicas.

### B. Conexión del Monedero (Vía Optoacoplador HiLetgo)
El objetivo es proteger el pin de entrada de la pantalla (`Expand the input pin`).
- **Lado 12V (Entrada):**
  - `IN1` <- Cable de señal (pulso) del monedero.
  - `G` <- GND de la fuente de 12V.
- **Lado 3.3V (Salida al ESP32):**
  - `V1` <- Pin de 3.3V del puerto `Expand the input pin` de la pantalla.
  - `G` <- Pin GND del mismo puerto de la pantalla.
  - `OUT1/O1` <- Pin de señal (GPIO) del puerto de la pantalla.

### C. Bus I2C y Expansores (PCF8574)
Se conectan en paralelo al puerto `I2C Peripheral Interface` de la pantalla (Pines 3.3V, GND, IO21 para SDA y IO22 para SCL).
- **Módulo 1 (Dirección 0x20):** DIP switches en OFF. Sus pines P0-P5 se conectan a las entradas `IN` de los 6 relés (Activación).
- **Módulo 2 (Dirección 0x21):** DIP switch 1 en ON, 2 y 3 en OFF. Sus pines P0-P5 se conectan a los pines de retorno de señal de los 6 micro-switches de los motores (Sensado).

---

## 💻 Lógica de Programación (Firmware C++)

### Módulo de Interrupciones (Cobro)
- **Función:** `attachInterrupt()` configurada en modo `FALLING` leyendo el pin de salida del optoacoplador.
- **Algoritmo:** Cada pulso recibido incrementa un contador. Se utiliza la función `millis()` como temporizador no bloqueante (ej. 300ms de inactividad) para detectar el fin de la ráfaga de pulsos, multiplicar el contador por 5 (unidad mínima) y sumar el resultado al saldo global en la pantalla.

### Máquina de Estados de Despacho (Motor)
Por cada unidad solicitada, se ejecuta una secuencia estrictamente serial para evitar colisiones y lecturas falsas:
1. **Arranque:** El ESP32 envía comando vía I2C (Módulo `0x20`) para encender el relé del motor seleccionado.
2. **Debounce Mecánico (Escape):** El código ejecuta `delay(200);` (el motor avanza) para permitir que la leva se aleje y libere el micro-switch.
3. **Monitoreo (Espera Activa):** El código entra en un bucle `while`, consultando constantemente vía I2C (Módulo `0x21`) el estado del micro-switch.
4. **Corte:** Cuando el micro-switch vuelve a estar presionado (360° completados), el bucle se rompe y el ESP32 envía el comando I2C para apagar el relé, frenando el motor en su posición de inicio.
5. **Asentamiento:** Se ejecuta un `delay(1500);` con el motor apagado para que el producto caiga a la bandeja. El ciclo puede repetirse si el usuario solicita múltiples unidades.

---

## 🖥️ Diseño de la Interfaz Gráfica (GUI)

La pantalla de 4.0" opera en orientación vertical.

- **Grid de Productos:** Seis bloques de selección. Cada uno contiene:
  - Imagen del producto (JPG/BMP leída desde la MicroSD).
  - Selector de cantidades en la parte inferior: Botón `[-]`, Texto dinámico `Cantidad: 0`, Botón `[+]`.
  - Restricciones lógicas: Mínimo 0, Máximo 10 por producto.
- **Panel Inferior de Control:**
  - **Estado de Cuenta:** Refleja el saldo en tiempo real. Si es igual a 0, el texto muestra la instrucción *"Ingrese monedas"*.
  - **Acción:** Botón de *"Confirmar compra"*. Al ser accionado, invoca un cuadro de diálogo secundario (Modal) superpuesto solicitando validación: *"¿Confirmar selección? [Sí] [No]"*.

---

## 🔮 Escalabilidad Futura

El diseño mecatrónico permite la expansión modular sin alterar el núcleo:

- **Speaker Interface:** Puerto habilitado para un altavoz que provea retroalimentación auditiva (ej. ingreso de monedas o errores).
- **SPI Peripheral Interface:** Puerto de alta velocidad disponible para integrar sistemas de pago cerrado corporativo (lectores RFID/NFC).
- **IoT y Telemetría:** Uso del transceptor Wi-Fi nativo del ESP32 para desarrollar una aplicación móvil o *Dashboard* remoto, permitiendo monitorizar métricas de ventas y estado del inventario en la nube.

---

## 👥 Desarrolladores del Proyecto

Los estudiantes que elaboraron el proyecto son:

- **Sebastian Josue Diaz Encarnación** (2025-0533)
- **Lia Camille Figuereo Sanchez** (2025-0443)
- **Wilmalis Perez Vargas** (2025-0461)
- **Lia Marie Hernandez Corsino** (2025-0452)
- **Yeremy Manuel Bautista Portes** (2025-0456)

---

## 📁 Estructura del Repositorio

```
Maquina_Expendedora/
├── Codigo/
├── Diseño_electronico/
├── Diseño_mecanico/
├── Documentacion/
│   ├── Documentacion Tecnica/
│   │   └── documento_de_arquitectura_tecnico.md
│   └── Imagenes de manufactura/
└── Documentacion Proyecto Terminado/
```
