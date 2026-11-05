# Máquina Expendedora Vending ESP32

## 📌 Descripción del Proyecto

Nuestra **Máquina Expendedora** es un dispensador automático de productos de consumo (6 canales o espirales), diseñado con un enfoque de **industrial y bajo costo**. El sistema está controlado por un **microcontrolador ESP32** junto con una **pantalla táctil integrada de 4.0 pulgadas**, que actúa como cerebro y como interfaz gráfica. El firmware se desarrolla con **ESP-IDF** en **C nativo**.

El funcionamiento es el siguiente:

1. **Pago:** El diseño admite un **validador multimoneda** (Joyzan CH-926), que registra pulsos por valor (RD$ 1 = 1 pulso, RD$ 5 = 3 pulsos, RD$ 10 = 5 pulsos, RD$ 25 = 7 pulsos) a través de un **optoacoplador** de seguridad. En el montaje actual del banco el monedero está **deshabilitado** (`BOARD_COIN_ACCEPTOR_ENABLED = 0`) y el saldo se carga desde el panel de administración `ADM`.
2. **Selección:** El usuario navega por la pantalla táctil (diseñada en una cuadrícula de 2 columnas por 3 filas) para elegir el producto y la cantidad (máximo 10 unidades por canal).
3. **Despacho:** El ESP32 activa el relé correspondiente del motor reductor del canal seleccionado mediante el bus **I2C**. Los **6 canales (M1–M6)** operan en **lazo abierto por tiempo**: el relé se mantiene activo exactamente **3 segundos** y luego se apaga. Cada canal usa un **transistor NPN como inversor** entre el PCF8574 y el módulo de relé. Si se compra más de un producto, el firmware aplica un **Mutex** en FreeRTOS para que solo un motor rote a la vez (despacho secuencial). El pedido se encola de forma **atómica**: se acepta completo o se rechaza entero.

---

## 📊 Estado del Proyecto

**Fase P1 (Firmware v1.0) — validada en hardware el 2026-11-04.** El firmware compila sin errores ni warnings y los **6 motores (M1–M6) se probaron físicamente**: cada compra enciende el relé, gira el motor y lo detiene exactamente a los 3 s. El cobro se descuenta después del apagado.

**Implementado y verificado en la placa real:**
- GUI táctil 2×3 con modal de confirmación y panel de saldo (las imágenes se leen de la MicroSD; si falla, se muestra `[Espacio Imagen]`).
- Despacho secuencial de 6 motores con Mutex. Todos los canales en lazo abierto por temporización (pulso de 3 s).
- Canal de potencia: PCF8574 `0x20` → transistor NPN inversor → módulo de relé de 1 canal con jumper `H/L` en **LOW**.
- Encolado atómico todo-o-nada.
- Cobro diferido: el saldo y el stock solo se descuentan cuando se completa el pulso de la unidad.
- Persistencia en NVS de saldo, stock y precios.
- Modo administración por botón `ADM` + contraseña (`0000`): ajuste de saldo, inventario y precios.
- Monedero deshabilitado en el banco actual: el saldo se carga desde `ADM`.

**Pendiente:**
- Reintroducir la lectura de sensores de posición por canal con un segundo PCF8574 (`0x21`) y divisores de tensión (planificado para la siguiente etapa).
- MicroSD: el montaje falla con `sdmmc_card_init failed (0x107)`; solo afecta la carga de imágenes.
- Pruebas automatizadas/CI y extras V2.0 (telemetría Wi-Fi, audio, RFID).

**Build de referencia:** `maquina_expendedora.bin` = **611 600 B** (42 % de la partición de aplicación libre).

---

## 🛠️ Materiales

### Control e Interfaz
- **1x Pantalla Táctil TFT (4.0 pulgadas):** Integra el ESP32-WROOM-32E, Wi-Fi/BT y puertos de expansión JST. Activa como la interfaz gráfica (GUI) del sistema.

### Sistema de Pago y Aislamiento
- **1x Monedero Multimoneda Joyzan (CH-926):** Lee monedas de RD$ 1, RD$ 5, RD$ 10 y RD$ 25 (operación a 12V).
- **1x Módulo Optoacoplador HiLetgo (PC817, 2 canales):** Aísla la señal de 12V del monedero y la transforma en una señal segura de 3.3V para el ESP32.

### Actuadores y Expansión I/O
- **4x Motores Reductores 12V DC (LK20052, 3 hilos):** Canales M1–M4, despachados por tiempo.
- **2x Motores Reductores 12V DC (LK20052 Fy-007, 2 hilos):** Canales M5–M6, despachados por tiempo.
- **1x Módulo Expansor I2C PCF8574 (NOYITO - Rojo):** Configurado con sus DIP switches en la dirección `0x20`.
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
- **Expansor de sensores (dirección 0x21):** reservado para la siguiente etapa (lectura de sensores de posición con divisores de tensión). Todavía no está montado.

*Nota de polaridad:* el módulo de relé queda con el **jumper `H/L` en LOW** (su entrada `IN` se activa con 0 V). Como el transistor NPN **invierte** la señal, el PCF8574 debe sacar **HIGH (3.3 V)** para encender el motor y **LOW (0 V)** para apagarlo. Por eso `BOARD_RELAY_ACTIVE_LOW = 0` y el registro sombra se escribe sin invertir. En el log se ve `0x20 = 0x01` al encender M1 y `0x20 = 0x00` al apagar.

---

## 🔐 Modo Administración (operador)

El panel está protegido con contraseña y se usa para mantenimiento de la máquina.

1. **Acceso:** pulsa el botón pequeño **`ADM`** que está junto a `Confirmar`, en el panel inferior.
2. Introduce la contraseña **`0000`** en el teclado numérico. Al teclear el cuarto dígito se valida sola; `C` borra y `OK` reintenta el envío.
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
