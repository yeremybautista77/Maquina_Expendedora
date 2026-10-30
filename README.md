# Máquina Expendedora Vending ESP32

## 📌 Descripción del Proyecto

Nuestra **Máquina Expendedora** es un dispensador automático de productos de consumo (6 canales o espirales), diseñado con un enfoque de **industrial y bajo costo**. El sistema está controlado por un **microcontrolador ESP32** junto con una **pantalla táctil integrada de 4.0 pulgadas**, que actúa como cerebro y como interfaz gráfica. El firmware se desarrolla con **ESP-IDF** en **C nativo**.

El funcionamiento es el siguiente:

1. **Pago:** El usuario ingresa monedas a través de un **validador multimoneda** (Joyzan CH-926), que registra pulsos por valor (RD$ 1 = 1 pulso, RD$ 5 = 3 pulsos, RD$ 10 = 5 pulsos, RD$ 25 = 7 pulsos). Estos pulsos pasan por un **optoacoplador** de seguridad para llegar de forma aislada y limpia al ESP32.
2. **Selección:** El usuario navega por la pantalla táctil (diseñada en una cuadrícula de 2 columnas por 3 filas) para elegir el producto y la cantidad (máximo 10 unidades por canal).
3. **Despacho:** El ESP32 activa el relé correspondiente del motor reductor del canal seleccionado mediante el bus **I2C**. En **M1–M4** el motor avanza exactamente 360° y el **final de carrera** (micro-switch) confirma la vuelta. En **M5–M6** no hay sensor: el relé se mantiene activo un pulso calibrado de **~2.0 s**. Si se compra más de un producto, el firmware aplica un **Mutex** en FreeRTOS para que solo un motor rote a la vez (despacho secuencial). El pedido se encola de forma **atómica**: se acepta completo o se rechaza entero.

---

## 📊 Estado del Proyecto

**Fase P1 (Firmware v1.0) — completada en laboratorio.** El firmware compila sin errores ni warnings y queda a la espera de la validación en la placa real.

**Implementado y verificado en compilación:**
- GUI táctil 2×3 con imágenes desde MicroSD, modal de confirmación y panel de saldo.
- Monedero multimoneda por ISR con ventana de ráfaga y antirrebote.
- Despacho secuencial de 6 motores con Mutex. M1–M4 en bucle cerrado por sensor; M5–M6 en lazo abierto por temporización.
- *Homing* automático al arrancar solo para M1–M4 (10 s por canal) y encolado atómico todo-o-nada.
- Cobro diferido: el saldo y el stock solo se descuentan cuando el motor confirma la unidad.
- Persistencia en NVS de saldo, stock y precios.
- Modo administración oculto para inventario, precios y prueba de motores.

**Pendiente:**
- Validación en hardware real: táctil/calibración, motores, persistencia NVS tras reinicio y modo administración.
- Pruebas automatizadas/CI y extras V2.0 (telemetría Wi-Fi, audio, RFID).

**Build de referencia:** `maquina_expendedora.bin` = **613 472 B** (41 % de la partición de aplicación libre).

---

## 🛠️ Materiales

### Control e Interfaz
- **1x Pantalla Táctil TFT (4.0 pulgadas):** Integra el ESP32-WROOM-32E, Wi-Fi/BT y puertos de expansión JST. Activa como la interfaz gráfica (GUI) del sistema.

### Sistema de Pago y Aislamiento
- **1x Monedero Multimoneda Joyzan (CH-926):** Lee monedas de RD$ 1, RD$ 5, RD$ 10 y RD$ 25 (operación a 12V).
- **1x Módulo Optoacoplador HiLetgo (PC817, 2 canales):** Aísla la señal de 12V del monedero y la transforma en una señal segura de 3.3V para el ESP32.

### Actuadores y Expansión I/O
- **4x Motores Reductores 12V DC (LK20052, 3 hilos):** Con leva y micro-switch trasero; una revolución de 360° equivale a un evento de activación. Canales M1–M4.
- **2x Motores Reductores 12V DC (LK20052 Fy-007, 2 hilos):** Sin micro-switch; se despachan por tiempo. Canales M5–M6.
- **2x Módulos Expansores I2C PCF8574 (NOYITO - Rojos):** Configurados con sus DIP switches para la dirección del bus I2C.
- **6x Módulos de Relé (1 canal, bobina 12V):** Controlan el paso de la corriente de 12V hacia los motores. Entradas de control compatibles con 3.3V.
- **8x Resistencias (4× 10 kΩ + 4× 3.3 kΩ, 1/4 W):** Divisores pasivos para las señales de los micro-switches de M1–M4.

### Potencia
- **1x Fuente Conmutada Principal:** 110V AC a 12V DC (mínimo 10 Amperios).
- **1x Módulo Step-Down DC-DC (LM2596):** Reduce los 12V a 5V exclusivamente para la entrada de la placa Hosyond.

---

## 🔌 Conexiones

### Alimentación
- **Línea 12V:** Alimenta el monedero, las bobinas de los 6 relés, las terminales de carga (COM/NO) y la entrada del Step-Down.
- **Línea 5V (desde el Step-Down):** Exclusiva de la entrada de 5V de la placa Hosyond. No alimenta relés ni motores.
- **Línea 3.3V:** Generada por el regulador de la placa Hosyond para el ESP32, los PCF8574 y los periféricos lógicos.
- **Tierras:** el GND lógico del ESP32, el GND del Step-Down y el GND del módulo de relés se atornillan juntos al borne **V− / COM** de la fuente de 12V (punto estrella).

### Monedero (vía Optoacoplador PC817)
- **Lado 12V (entrada):** `IN1` recibe la señal (pulso) del monedero y cierra el circuito al `GND` de la fuente de 12V.
- **Lado 3.3V (salida al ESP32):** `VCC` (3.3V) y `GND` provienen de la pantalla; el pin de señal `OUT1` se conecta al pin digital **IO35** del puerto *Expand the input pin*.

### Bus I2C y Expansores (PCF8574)
- Los dos módulos expansores se conectan en paralelo al puerto *I2C Peripheral Interface* de la pantalla (SDA, SCL, 3.3V, GND).
- **Módulo 1 (dirección 0x20):** Los pines P0–P5 se conectan a las entradas `IN` de los 6 relés (activación).
- **Módulo 2 (dirección 0x21):** Solo los pines P0–P3 reciben la señal acondicionada de los micro-switches de M1–M4. P4 y P5 quedan **sin conexión** porque M5 y M6 no tienen sensor.

*Nota de acondicionamiento (M1–M4):* La salida de 12V de cada micro-switch se escala con un divisor pasivo de **10 kΩ en serie + 3.3 kΩ a GND**, lo que deja ≈2.97V seguros en el expansor `0x21`. La resistencia de 3.3 kΩ actúa además como pull-down. La placa LK20052 se conserva **intacta: no se cortan pistas**. La interpretación del nivel de reposo se ajusta por software con la macro `SENSOR_HOME_LEVEL` (1 = HIGH, 0 = LOW). Los canales M5–M6 se despachan en lazo abierto con un pulso calibrado de ~2.0 s.

---

## 🔐 Modo Administración (operador)

El panel está oculto para el usuario final y se usa para mantenimiento de la máquina.

1. **Acceso:** mantén pulsada la **esquina superior izquierda** de la pantalla durante **3 segundos**.
2. Se abre un panel con scroll y tres secciones:
   - **Inventario:** muestra el stock de los 6 canales y el botón `Rellenar Stock (Reset a 10)`.
   - **Precios:** cada producto tiene `[-]`/`[+]` en pasos de **RD$ 5**. Pulsa `Guardar Precios` para aplicar los cambios y persistirlos en NVS. Rango válido: RD$ 5.00–1000.00.
   - **Prueba de motores:** `Test M1`…`Test M6` despachan una unidad para comprobar el motor. La prueba **solo descuenta stock**; no cobra saldo ni altera el pedido del usuario.
3. Cierra el panel con el botón **X**.

**Seguridad:** el reset y las pruebas se bloquean mientras hay una compra activa o el motor/cola está ocupado. Los tests no modifican el saldo ni el flujo de venta.

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
