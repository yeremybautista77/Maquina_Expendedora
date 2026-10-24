# Máquina Expendedora Vending ESP32

## 📌 Descripción del Proyecto

Nuestra **Máquina Expendedora** es un dispensador automático de productos de consumo (6 canales o espirales), diseñado con un enfoque de **industrial y bajo costo**. El sistema está controlado por un **microcontrolador ESP32** junto con una **pantalla táctil integrada de 4.0 pulgadas**, que actúa como cerebro y como interfaz gráfica. El firmware se desarrolla con **ESP-IDF** en **C nativo**.

El funcionamiento es el siguiente:

1. **Pago:** El usuario ingresa monedas a través de un **validador multimoneda** (Joyzan CH-926), que registra pulsos por valor (RD$ 1 = 1 pulso, RD$ 5 = 3 pulsos, RD$ 10 = 5 pulsos, RD$ 25 = 7 pulsos). Estos pulsos pasan por un **optoacoplador** de seguridad para llegar de forma aislada y limpia al ESP32.
2. **Selección:** El usuario navega por la pantalla táctil (diseñada en una cuadrícula de 2 columnas por 3 filas) para elegir el producto y la cantidad (máximo 10 unidades por canal).
3. **Despacho:** El ESP32 activa el relé correspondiente del motor reductore del canal seleccionado mediante el bus **I2C**. El motor avanza exactamente 360° (una revolución = 1 evento), detectado mediante el **final de carrera** (micro-switch) del propio motor. Si se compra más de un producto, el firmware aplica un **Mutex** en FreeRTOS para que solo un motor rote a la vez (despacho secuencial).

---

## 🛠️ Materiales

### Control e Interfaz
- **1x Pantalla Táctil TFT (4.0 pulgadas):** Integra el ESP32-WROOM-32E, Wi-Fi/BT y puertos de expansión JST. Activa como la interfaz gráfica (GUI) del sistema.

### Sistema de Pago y Aislamiento
- **1x Monedero Multimoneda Joyzan (CH-926):** Lee monedas de RD$ 1, RD$ 5, RD$ 10 y RD$ 25 (operación a 12V).
- **1x Módulo Optoacoplador HiLetgo (PC817, 2 canales):** Aísla la señal de 12V del monedero y la transforma en una señal segura de 3.3V para el ESP32.

### Actuadores y Expansión I/O
- **6x Motores Reductores 12V DC (JGY370 / JGA25-370):** Con leva y micro-switch trasero; una revolución de 360° equivale a un evento de activación.
- **2x Módulos Expansores I2C PCF8574 (NOYITO - Rojos):** Configurados con sus DIP switches para la dirección del bus I2C.
- **6x Módulos de Relé (1 canal, 5V):** Controlan el paso de la corriente de 12V hacia los motores.

### Potencia
- **1x Fuente Conmutada Principal:** 110V AC a 12V DC (mínimo 10 Amperios).
- **1x Módulo Step-Down DC-DC (LM2596):** Reduce los 12V a 5V/3.3V para alimentar la lógica y los relés.

---

## 🔌 Conexiones

### Alimentación
- **Línea 12V:** Alimenta directamente al monedero y a las terminales de carga (COM/NO) de los 6 relés que van a los motores.
- **Línea 5V (desde el Step-Down):** Alimenta las bobinas de los relés, los módulos PCF8574 y, de ser necesario, la alimentación del puerto de la pantalla ESP32.
- **Línea 3.3V:** Generada por el propio ESP32 para sus señales lógicas.

### Monedero (vía Optoacoplador PC817)
- **Lado 12V (entrada):** `IN1` recibe la señal (pulso) del monedero y cierra el circuito al `GND` de la fuente de 12V.
- **Lado 3.3V (salida al ESP32):** `VCC` (3.3V) y `GND` provienen de la pantalla; el pin de señal `OUT1` se conecta al pin digital **IO35** del puerto *Expand the input pin*.

### Bus I2C y Expansores (PCF8574)
- Los dos módulos expansores se conectan en paralelo al puerto *I2C Peripheral Interface* de la pantalla (SDA, SCL, 3.3V, GND).
- **Módulo 1 (dirección 0x20):** Los pines P0–P5 se conectan a las entradas `IN` de los 6 relés (activación).
- **Módulo 2 (dirección 0x21):** Los pines P0–P5 se conectan a los pines de retorno de los micro-switches de los motores (sensores).

*Nota de seguridad (contacto seco):* Para evitar que la corriente de 12V de los motores pase por el expansor de sensores (3.3V), se realizan cortes físicos en las pistas de cobre de las placas de los motores (LK20052) con un bisturí, aislando el micro-switch para que solo actúe conectando el pin `0x21` a tierra (GND).

---

## ✍️ Autores

- Sebastian Josue Diaz Encarnación (2025-0533)
- Lia Camille Figuereo Sanchez (2025-0443)
- Wilmalis Perez Vargas (2025-0461)
- Lia Marie Hernandez Corsino (2025-0452)
- Yeremy Manuel Bautista Portes (2025-0456)

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
