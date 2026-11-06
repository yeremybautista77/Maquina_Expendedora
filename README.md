# Máquina Expendedora Vending ESP32

## 📌 Descripción del Proyecto

Nuestra **Máquina Expendedora** es un dispensador automático de productos de consumo (6 canales o espirales), diseñado con un enfoque de **industrial y bajo costo**. El sistema está controlado por un **microcontrolador ESP32** junto con una **pantalla táctil integrada de 4.0 pulgadas**, que actúa como cerebro y como interfaz gráfica. El firmware se desarrolla con **ESP-IDF** en **C nativo**.

El funcionamiento es el siguiente:

1. **Pago:** El diseño admite un **validador multimoneda** (Joyzan CH-926), que registra pulsos por valor (RD$ 1 = 1 pulso, RD$ 5 = 3 pulsos, RD$ 10 = 5 pulsos, RD$ 25 = 7 pulsos) a través de un **optoacoplador** de seguridad. El monedero está **habilitado** (`BOARD_COIN_ACCEPTOR_ENABLED = 1`) y el saldo también puede cargarse desde el panel de administración `ADM`.
2. **Selección:** El usuario navega por la pantalla táctil (diseñada en una cuadrícula de 2 columnas por 3 filas) para elegir el producto y la cantidad (máximo 10 unidades por canal).
3. **Despacho:** El ESP32 activa el relé correspondiente del motor reductor del canal seleccionado mediante el bus **I2C**. **M1–M4** usan un **sensor de posición** por un segundo PCF8574 (`0x21`): el motor gira hasta salir de `home`, avanza y se detiene al volver a `home`. **M5 y M6** operan en **lazo abierto por tiempo** (3 segundos). Si el `0x21` no responde, los 6 canales caen automáticamente a lazo abierto. Cada canal usa un **transistor NPN como inversor** entre el PCF8574 y el módulo de relé. Si se compra más de un producto, el firmware aplica un **Mutex** en FreeRTOS para que solo un motor rote a la vez (despacho secuencial). El pedido se encola de forma **atómica**: se acepta completo o se rechaza entero.

---

## 📊 Estado del Proyecto

**Firmware v1.0 (2026-11-05).** Compila sin errores ni warnings
(`maquina_expendedora.bin` = **588 560 B**, 44 % de la partición libre). Los **6
motores (M1–M6)**, la pantalla, el táctil y la NVS se validaron físicamente en
etapas anteriores.

**Implementado:**
- GUI táctil 2×3 con modal de confirmación y panel inferior de saldo/total. Las
  imágenes de producto van **embebidas en flash** (52×52 RGB565); si falta una,
  se muestra `[Espacio Imagen]`.
- Despacho secuencial de 6 motores con Mutex. **M1–M4 con sensor de posición**
  (`0x21`); **M5–M6 por temporización** (3 s) y fallback a tiempo si el sensor no
  responde.
- Canal de potencia: PCF8574 `0x20` → transistor NPN inversor → módulo de relé de
  1 canal con jumper `H/L` en **LOW**.
- Encolado atómico todo-o-nada.
- Cobro diferido: el saldo y el stock solo se descuentan cuando se entrega la
  unidad. Las cantidades del carrito vuelven a cero al cerrar el pedido.
- Persistencia en NVS de saldo, stock y precios.
- Modo administración por botón `ADM` + contraseña (`20250456`): ajuste de
  saldo, inventario y precios.
- Monedero CH-926 habilitado por IO35 (1/3/5/7 pulsos).
- MicroSD desactivada (`BOARD_SD_ENABLED = 0`).

**Pendiente de validación física:**
- Montar los sensores M1–M4, medir el divisor 10 kΩ/3.3 kΩ y confirmar que
  `home = LOW`; el *homing* de arranque sigue desactivado por seguridad.
- Probar el monedero CH-926 con monedas reales.
- Definir nombre, precio e imagen de **P4–P6** (hoy provisionales RD$ 70/80/90).
- Pruebas automatizadas/CI y extras V2.0 (telemetría Wi-Fi, audio, RFID).

**Build de referencia:** `maquina_expendedora.bin` = **0x8fb10** (588 560 B),
**44 %** de la partición de aplicación libre.

---

## 🛠️ Materiales

### Control e Interfaz
- **1x Pantalla Táctil TFT (4.0 pulgadas):** Integra el ESP32-WROOM-32E, Wi-Fi/BT y puertos de expansión JST. Activa como la interfaz gráfica (GUI) del sistema.

### Sistema de Pago y Aislamiento
- **1x Monedero Multimoneda Joyzan (CH-926):** Lee monedas de RD$ 1, RD$ 5, RD$ 10 y RD$ 25 (operación a 12V).
- **1x Módulo Optoacoplador HiLetgo (PC817, 2 canales):** Aísla la señal de 12V del monedero y la transforma en una señal segura de 3.3V para el ESP32.

### Actuadores y Expansión I/O
- **4x Motores Reductores 12V DC (LK20052, 3 hilos):** Canales M1–M4, con sensor de posición.
- **2x Motores Reductores 12V DC (LK20052 Fy-007, 2 hilos):** Canales M5–M6, despachados por tiempo.
- **1x Módulo Expansor I2C PCF8574 (NOYITO - Rojo):** Configurado con sus DIP switches en la dirección `0x20` (relés).
- **1x Módulo Expansor I2C PCF8574 adicional:** Dirección `0x21`, entradas P0–P3 para los sensores de posición M1–M4 (alimentado a 3.3 V).
- **4x Sensores de posición** (ópticos o micro-switch) con **divisor 10 kΩ en serie + 3.3 kΩ a GND** desde 12 V.
- **6x Módulos de Relé de 1 canal (SRD-12VDC-SL-C, bobina 12V, jumper `H/L` en LOW):** Uno por motor. La entrada `IN` se activa en LOW (0 V) y se desactiva en HIGH.
- **6x Transistores NPN + 6x Resistencias de 1 kΩ:** Inversor por canal entre el PCF8574 y la entrada `IN` del relé (`EMISOR`→GND, `BASE`→1 kΩ→P0…P5, `COLECTOR`→`IN`). El PCF8574 saca HIGH para encender.

### Potencia
- **1x Fuente Conmutada Principal:** 110V AC a 12V DC (mínimo 10 Amperios).
- **1x Módulo Step-Down DC-DC (LM2596):** Reduce los 12V a 5V exclusivamente para la entrada de la placa Hosyond.

---

## 🔌 Conexiones

### Alimentación
- **Línea 12V:** Alimenta el monedero, las bobinas de los 6 relés, las terminales de carga (COM/NO) y la entrada del Step-Down.
- **Línea 5V (desde el Step-Down):** Exclusiva de la entrada de 5V de la placa Hosyond. No alimenta relés ni motores.
- **Línea 3.3V:** Generada por el regulador de la placa Hosyond para el ESP32, el PCF8574 y los periféricos lógicos.
- **Tierras:** el GND lógico del ESP32, el GND del Step-Down y el GND del módulo de relés se atornillan juntos al borne **V− / COM** de la fuente de 12V (punto estrella).

### Monedero (vía Optoacoplador PC817)
- **Lado 12V (entrada):** `IN1` recibe la señal (pulso) del monedero y cierra el circuito al `GND` de la fuente de 12V.
- **Lado 3.3V (salida al ESP32):** `VCC` (3.3V) y `GND` provienen de la pantalla; el pin de señal `OUT1` se conecta al pin digital **IO35** del puerto *Expand the input pin*.

### Bus I2C y Expansor (PCF8574)
- El módulo expansor se conecta al puerto *I2C Peripheral Interface* de la pantalla (SDA, SCL, 3.3V, GND).
- **Expansor de relés (dirección 0x20):** los pines P0–P5 van a la **base** de cada transistor NPN a través de 1 kΩ; el colector va al `IN` de cada módulo de relé y el emisor a GND.
- **Expansor de sensores (dirección 0x21):** P0–P3 reciben la señal de los sensores de posición **M1–M4** a través de los divisores 10 kΩ / 3.3 kΩ. El módulo se alimenta a 3.3 V. `home = LOW` (`BOARD_SENSOR_HOME_LEVEL = 0`), pendiente de confirmar con multímetro.

*Nota de polaridad:* el módulo de relé queda con el **jumper `H/L` en LOW** (su entrada `IN` se activa con 0 V). Como el transistor NPN **invierte** la señal, el PCF8574 debe sacar **HIGH (3.3 V)** para encender el motor y **LOW (0 V)** para apagarlo. Por eso `BOARD_RELAY_ACTIVE_LOW = 0` y el registro sombra se escribe sin invertir. En el log se ve `0x20 = 0x01` al encender M1 y `0x20 = 0x00` al apagar.

---

## 🔐 Modo Administración (operador)

El panel está protegido con contraseña y se usa para mantenimiento de la máquina.

1. **Acceso:** pulsa el botón pequeño **`ADM`** que está junto a `Confirmar`, en el panel inferior.
2. Introduce la contraseña **`20250456`** (8 dígitos) en el teclado numérico. Al teclear el octavo dígito se valida sola; `C` borra y `OK` reintenta el envío.
3. Se abre un panel con scroll y tres secciones:
   - **Saldo:** muestra el saldo actual y permite subirlo o bajarlo con `+1/+5/+10/+25` y `-1/-5/-10/-25`. Cada cambio se guarda en NVS al instante.
   - **Inventario:** muestra el stock de los 6 canales y el botón `Rellenar Stock (Reset a 10)`.
   - **Precios:** cada producto tiene `[-]`/`[+]` en pasos de **RD$ 5**. Pulsa `Guardar Precios` para aplicar los cambios y persistirlos en NVS. Rango válido: RD$ 5.00–1000.00.
4. Cierra el panel con el botón **X**.

**Seguridad:** el acceso, el ajuste de saldo, el reset y el guardado de precios se bloquean mientras hay una compra activa. La contraseña es provisional y se valida en el firmware.

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
├── Firmware/
├── Diseño_electronico/
├── Diseño_mecanico/
├── Documentacion/
│   ├── Documentacion Tecnica/
│   │   └── documento_de_arquitectura_tecnico.md
│   └── Imagenes de manufactura/
└── Documentacion Proyecto Terminado/
```
