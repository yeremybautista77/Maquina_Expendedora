# Arquitectura de Hardware — Máquina Expendedora Automática de 6 Canales

**Versión 1.5 · 4 de noviembre de 2026 · Placa base Hosyond 4.0" (ESP32-32E)**

> **Nota de alcance.** Este documento describe exclusivamente el dominio físico del sistema: voltajes, corrientes, estados lógicos, protecciones, conectores y topología de interconexión. El comportamiento del sistema y la lógica de control se documentan por separado.

## 1. Mapa Conceptual del Hardware (Diagrama de Bloques)

```mermaid
graph TD

    AC["Red eléctrica<br/>110 V AC · 60 Hz"]
    PSU["Fuente conmutada<br/>12 V DC · 10 A"]
    FUSE["Fusible principal<br/>10 A"]
    BUCK["Step-Down LM2596<br/>12 V → 5 V · 3 A"]
    STAR(("GND común · V− / COM<br/>punto estrella"))

    subgraph POT["DOMINIO DE POTENCIA · 12 V"]
        R12["Riel 12 V"]
        COIN["Monedero CH-926<br/>12 V · salida open-collector"]
        RELAYS["6 módulos de relé de 1 canal<br/>bobina 12 V · contactos COM / NO"]
        MOTORS["M1–M6 · LK20052<br/>12 V · despacho por tiempo"]
    end

    subgraph MID["DOMINIO INTERMEDIO · 5 V"]
        R5["Riel 5 V"]
    end

    subgraph LOG["DOMINIO LÓGICO · 3.3 V"]
        ESP["Placa Hosyond 4.0<br/>ESP32-32E + LDO 3.3 V"]
        P33["Riel 3.3 V"]
        M20["PCF8574 · 0x20<br/>relees de motor"]
    end

    subgraph ISO["INTERFAZ AISLADA"]
        OPTO["Optoacoplador PC817<br/>+ pull-up 10 kΩ a 3.3 V"]
    end

    subgraph PER["PERIFÉRICOS SPI"]
        TFT["TFT ST7796<br/>320 × 480"]
        TOUCH["Táctil XPT2046"]
        SD["Lector MicroSD"]
    end

    AC --> PSU
    PSU --> FUSE
    FUSE --> R12
    R12 --> BUCK
    BUCK --> R5
    R5 -->|"5 V"| ESP
    ESP -->|"3.3 V"| P33

    R12 -->|"12 V"| COIN
    R12 -->|"bobinas 12 V"| RELAYS
    R12 -->|"contactos COM"| RELAYS
    RELAYS -->|"12 V conmutado"| MOTORS
    M20 -->|"P0…P5 · ON = HIGH (3.3 V)"| NPN["6 × transistor NPN<br/>inversor"] 
    NPN -->|"IN = 0 V al encender"| RELAYS

    COIN -->|"pulso 12 V"| OPTO
    OPTO -->|"IO35 · activo LOW"| ESP

    ESP -->|"I2C · SDA IO32 / SCL IO25"| M20
    ESP -->|"SPI2 · 40 MHz"| TFT
    ESP -->|"SPI2 · CS IO33"| TOUCH
    ESP -->|"SPI3"| SD

    P33 --> M20
    P33 --> TFT
    P33 --> TOUCH
    P33 --> SD

    PSU --- STAR
    R5 --- STAR
    P33 --- STAR
    RELAYS --- STAR

    classDef power fill:#fdecea,stroke:#c0392b,stroke-width:2px,color:#111
    classDef mid fill:#fff4e5,stroke:#e67e22,stroke-width:2px,color:#111
    classDef logic fill:#e8f1fb,stroke:#2471a3,stroke-width:2px,color:#111
    classDef iso fill:#f4ecf7,stroke:#7d3c98,stroke-width:2px,color:#111
    classDef periph fill:#e9f7ef,stroke:#1e8449,stroke-width:2px,color:#111
    classDef cond fill:#fef9e7,stroke:#b7950b,stroke-width:2px,color:#111
    classDef neutral fill:#f2f3f4,stroke:#566573,stroke-width:2px,color:#111

    class AC,PSU,FUSE,R12,COIN,RELAYS,MOTORS power
    class BUCK,R5 mid
    class ESP,P33,M20 logic
    class OPTO iso
    class TFT,TOUCH,SD periph
    class STAR neutral
```

### 1.1 Dominios funcionales

| Dominio | Tensión | Función |
| :--- | :--- | :--- |
| **Potencia** | 12 V DC | Monedero, bobinas de relé, contactos de relé, motores y entrada del buck converter |
| **Intermedio** | 5 V DC | Únicamente la entrada de 5 V de la placa base Hosyond |
| **Lógico** | 3.3 V DC | ESP32-32E, expansor I2C y lógica de pantalla/táctil/SD |
| **Interfaz aislada** | 12 V → 3.3 V | Adaptación del pulso del monedero mediante optoacoplador |
| **Periféricos SPI** | 3.3 V DC | Panel TFT, táctil resistivo y almacenamiento MicroSD |

### 1.2 Lectura del diagrama

1. **El cerebro del sistema** es la placa Hosyond 4.0" con módulo **ESP32-32E**. Integra el regulador que genera el riel lógico de 3.3 V, el conector de pantalla y los puertos de expansión JST.
2. **La potencia no pasa por el ESP32.** El microcontrolador solo entrega señales de control de baja corriente al expansor I2C; la conmutación de los 12 V la realizan los contactos de los relés.
3. **El monedero está eléctricamente separado** de la lógica mediante un optoacoplador PC817. El pulso de 12 V se convierte en una señal de 3.3 V compatible con el pin IO35.
4. **Expansor `0x20` para los seis relés.** Cada salida P0–P5 pasa por un **transistor NPN inversor** (base 1 kΩ, emisor a GND, colector al `IN` del relé) porque el PCF8574 solo hunde corriente de forma fiable. El bus I2C solo transporta la orden; la conmutación de los 12 V la realizan los contactos de los relés.
5. **Expansor `0x21` para los sensores de posición M1–M4.** P0–P3 leen la señal de cada sensor a través de un divisor 10 kΩ / 3.3 kΩ desde 12 V. El módulo se alimenta a 3.3 V.
6. **Los periféricos SPI no comparten bus entre sí**: la pantalla y el táctil usan SPI2; la MicroSD usa SPI3 (desactivada en el firmware actual).
7. **El riel de 5 V es exclusivo de la placa base.** El LM2596 no alimenta relés ni motores; solo entrega los 5 V que la placa Hosyond convierte internamente a 3.3 V.
8. **M1–M4 usan lazo cerrado** con sensor de posición; **M5–M6** se despachan por tiempo (3 s). Si el `0x21` no responde, los seis canales caen a lazo abierto.

### 1.3 Identificación de la placa base

**Figura 1 — Placa base Hosyond 4.0" (ESP32-32E)**

![Placa Hosyond ESP32-32E](./img/placa_hosyond_esp32_32e.png)

*Descripción técnica:* placa integrada que combina el módulo ESP32-32E, el panel TFT ST7796 de 4.0 pulgadas, el controlador táctil XPT2046, el lector MicroSD y los puertos de expansión. Es la única fuente del riel de 3.3 V del sistema.

*Qué debe mostrar la fotografía:* vista frontal y posterior de la placa, identificación serigrafiada de los puertos **I2C Peripheral Interface** y **Expand the input pin**, ubicación del conector de alimentación y del regulador lineal.

**Figura 2 — Cableado general del conjunto**

![Cableado general](./img/cableado_general.png)

*Descripción técnica:* vista general del mazo de cables con separación física entre el recorrido de potencia (12 V) y el recorrido de señales (I2C, SPI, pulso de monedero).

*Qué debe mostrar la fotografía:* canaleta o amarres que separen los conductores de 12 V de los de señal, borneras de distribución, punto de unión de tierras y ubicación de los fusibles.

---

## 2. Árbol de Alimentación (Power Tree)

### 2.1 Topología de distribución

```mermaid
graph LR
    AC["110 V AC<br/>60 Hz"] --> PSU["Fuente conmutada<br/>12 V · 10 A"]
    PSU --> FUSE["Fusible<br/>10 A"]
    FUSE --> R12["Riel 12 V"]
    R12 --> COILS["Bobinas 12 V<br/>de los 6 relés"]
    R12 --> COM["Contactos COM / NO<br/>de los 6 relés"]
    COM --> MOT["M1–M6 · DC 12 V<br/>despacho por tiempo"]
    R12 --> COIN["Monedero<br/>CH-926"]
    R12 --> BUCK["LM2596<br/>12 V → 5 V"]
    BUCK --> R5["Riel 5 V"]
    R5 --> BOARD["Placa Hosyond<br/>entrada 5 V"]
    BOARD --> LDO["LDO 3.3 V<br/>integrado"]
    LDO --> R33["Riel 3.3 V"]
    R33 --> LOADS["ESP32-32E · PCF8574<br/>TFT · táctil · MicroSD<br/>lado lógico del PC817"]
```

### 2.2 Definición de rieles

| Riel | Origen | Tolerancia | Consumidores | Corriente estimada | Observaciones |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **12 V** | Fuente conmutada 110 V AC → 12 V DC | ±5 % | Monedero CH-926; bobinas de los 6 relés; contactos COM/NO; entrada del LM2596 | 0.2 A en reposo; 0.3–1.0 A por motor; 2–3 A de arranque | Riel inductivo y ruidoso. Requiere desacoplo y protección contra picos. |
| **5 V** | Step-Down LM2596 | ±3 % | Entrada de 5 V de la placa Hosyond | 0.35–0.55 A | Exclusivo de la placa base; no alimenta relés ni motores. |
| **3.3 V** | Regulador lineal de la placa Hosyond | ±2 % | ESP32-32E; PCF8574; TFT; táctil; MicroSD; salida del PC817 | 0.3–0.6 A | **No** alimentar los PCF8574 a 5 V. |

> **⚠️ Nota de diseño — Tres niveles en los módulos de relé.** Las **bobinas** y los **contactos** operan a **12 V**; las **entradas de control `IN`** son de nivel lógico y se manejan con un **transistor NPN a 3.3 V**. La bobina no se alimenta desde el riel de 5 V ni desde el ESP32.

### 2.3 Criterios de distribución

- **Separación de dominios.** El riel de 12 V alimenta el monedero, las bobinas y contactos de los relés, los motores y la entrada del buck converter. Ningún componente lógico se alimenta directamente de 12 V.
- **Conversión en cascada.** 12 V → 5 V (LM2596) → 3.3 V (LDO de la placa). El riel de 5 V se reserva exclusivamente para la entrada de la placa base.
- **Unión de tierras en punto estrella.** "Unir las tierras" significa llevar al mismo punto físico el **GND lógico del ESP32**, el **GND de salida del buck converter** y el **GND del módulo de relés de 12 V**. Los tres conductores se empalman y se atornillan juntos al **terminal negativo (V− / COM) de la fuente principal de 12 V**. Así, todas las señales de control (I2C, pulso del monedero y entradas de relé) comparten exactamente la misma referencia de 0 V y no aparecen diferencias de potencial entre módulos.

| Elemento | Conductor | Punto de unión |
| :--- | :--- | :--- |
| GND lógico del ESP32 / placa Hosyond | 22 AWG, negro | Borne V− / COM de la fuente |
| GND de salida del LM2596 | 22 AWG, negro | Borne V− / COM de la fuente |
| GND del módulo de relés (12 V) | 18–20 AWG, negro | Borne V− / COM de la fuente |
| GND del monedero CH-926 | 20 AWG, negro | Borne V− / COM de la fuente |
| Retorno de los motores | 18 AWG, negro | Borne V− / COM de la fuente |

> **Cómo se arma físicamente.** Los conductores de retorno se pelan, se empalman en una bornera o terminal de ojo y se atornillan al borne **V− / COM** de la fuente. No se usa el chasis metálico como conductor de retorno ni se encadenan las tierras de módulo en módulo: todos los retornos convergen en el mismo tornillo.

- **Desacoplo.** Se recomienda un capacitor de **1000 µF / 25 V** a la salida de la fuente, **470 µF / 25 V** junto al banco de relés y **100 nF** entre VCC y GND de cada PCF8574.
- **Calibre de conductores.** 18 AWG para 12 V de motores y bobinas; 20–22 AWG para 5 V y 3.3 V; 24 AWG para señales I2C, SPI y pulso de monedero.
- **Protección.** Fusible de 10 A en la línea principal de 12 V. Se recomienda además protección contra inversión de polaridad en la entrada de la fuente.

> **⚠️ Nota de diseño — Aislamiento funcional, no de seguridad.** El PC817 separa el pulso del monedero del pin del ESP32 y elimina ruido de modo común, pero ambos lados comparten el mismo negativo de la fuente de 12 V. Por tanto, **no constituye aislamiento galvánico de seguridad**; no debe usarse como barrera contra contacto eléctrico.

### 2.4 Presupuesto de corriente

| Riel | Carga | Corriente típica | Pico estimado | Base del cálculo |
| :--- | :--- | ---: | ---: | :--- |
| 12 V | Monedero CH-926 | 0.10 A | 0.20 A | Consumo en reposo y durante aceptación |
| 12 V | Motor LK20052 (1 activo) | 0.40 A | 2.50 A | Corriente de arranque con carga mecánica |
| 12 V | Bobina de relé 12 V (1 activa) | 0.04 A | 0.05 A | Relé tipo SRD-12VDC |
| 12 V | Entrada del LM2596 | 0.20 A | 0.30 A | 5 V × 0.55 A ÷ 0.80 ÷ 12 V ≈ 0.29 A |
| **12 V** | **Total del riel** | **≈ 0.75 A** | **≈ 3.1 A** | Fuente de 10 A: margen > 3.2× |
| 5 V | Placa Hosyond | 0.35 A | 0.55 A | Lógica + retroiluminación |
| **5 V** | **Total del riel** | **≈ 0.35 A** | **≈ 0.55 A** | LM2596 de 3 A: margen > 5× |
| 3.3 V | ESP32-32E | 0.12 A | 0.24 A | Pico de radio deshabilitado |
| 3.3 V | PCF8574 | 0.001 A | 0.002 A | Consumo cuasiestático |
| 3.3 V | TFT + táctil + MicroSD | 0.15 A | 0.25 A | Panel y retroiluminación |
| **3.3 V** | **Total del riel** | **≈ 0.30 A** | **≈ 0.50 A** | LDO: margen térmico según encapsulado |

*Los valores son estimaciones de diseño basadas en hojas de datos típicas de los componentes seleccionados.*

**Figura 3 — Fuente de alimentación principal**

![Fuente conmutada 12 V](./img/fuente_12v.png)

*Descripción técnica:* fuente conmutada de 110 V AC a 12 V DC con capacidad mínima de 10 A, responsable del dominio de potencia.

*Qué debe mostrar la fotografía:* placa de características de la fuente, borneras de salida, fusible instalado y punto de unión del negativo con el resto del sistema.

**Figura 4 — Conversión a 5 V**

![Step-Down LM2596](./img/stepdown_lm2596.png)

*Descripción técnica:* módulo reductor 12 V → 5 V basado en LM2596, con potenciómetro de ajuste y disipador. Su salida alimenta únicamente la entrada de 5 V de la placa base.

*Qué debe mostrar la fotografía:* potenciómetro de ajuste, disipador instalado y borneras de entrada/salida identificadas.

---

## 3. Acondicionamiento de Señales y Lógica de Hardware

### 3.1 Convención de niveles

El **módulo de relé** trabaja con **entrada activa en LOW**: 0 V en `IN` enciende. Sin embargo, el **PCF8574 solo hunde corriente de forma fiable**, así que cada canal incorpora un **transistor NPN inversor**. La señal que maneja el firmware en el PCF8574 queda **activa en HIGH**.

| Señal | Estado inactivo | Estado activo | Mecanismo físico |
| :--- | :--- | :--- | :--- |
| **Pulso de monedero (IO35)** | HIGH ≈ 3.3 V | LOW ≤ 0.4 V | Optoacoplador conduce y hunde la línea |
| **Salida del PCF8574 (P0…P5)** | LOW ≈ 0 V | HIGH ≈ 3.3 V | NPN saturado |
| **Entrada `IN` del relé** | Alto (flotante) | LOW ≤ 0.4 V | El NPN lleva el `IN` a GND; el opto interno conduce |

> **⚠️ Por qué el NPN.** Los módulos de relé de 12 V tienen el LED del opto referenciado a 12 V: un HIGH de 3.3 V no alcanza a apagarlo y el relé queda pegado (síntoma observado en banco). Con el NPN, el PCF8574 satura el transistor y el `IN` baja a 0 V; al corte, la entrada queda flotante y el relé suelta. Por eso `BOARD_RELAY_ACTIVE_LOW = 0`.

**Ventajas de esta convención:**

- **Inmunidad al ruido.** Las líneas con pull-up (monedero y entradas de relé) tienden a quedar en alto si el cable se desconecta, no a generar activaciones falsas.
- **Compatibilidad con salidas open-collector / open-drain.** El monedero y el expansor PCF8574 hunden corriente a GND; no necesitan entregar tensión.
- **Seguridad ante fallo.** La pérdida de la señal de control deja la bobina desenergizada y el motor apagado.
- **Estandarización.** Todo el sistema se diagnostica con la misma regla: **0 V = activo**.

### 3.2 Monedero CH-926 con optoacoplador PC817

**Cadena de señal:**

> **Monedero CH-926** (12 V) → **PC817** (aislado) → **Pull-up 10 kΩ** (a 3.3 V) → **IO35** (entrada)

| Etapa | Componente | Estado inactivo | Estado activo |
| :--- | :--- | :--- | :--- |
| Salida del monedero | Transistor NPN open-collector | Abierto (no conduce) | Cierra a GND de 12 V |
| Entrada del opto | LED infrarrojo + resistencia limitadora | Apagado | Encendido, I<sub>F</sub> ≈ 5–10 mA |
| Salida del opto | Fototransistor | Corte | Saturación, V<sub>CE</sub> ≈ 0.2 V |
| Línea de señal | Pull-up 10 kΩ a 3.3 V | 3.3 V (HIGH) | ≈ 0.2–0.4 V (LOW) |
| Pin del ESP32 | IO35, solo entrada | Sin evento | Flanco de bajada |

**Dimensionamiento de la resistencia de entrada.** Para una fuente de 12 V, una caída directa del LED de ≈ 1.2 V y una corriente objetivo de 8 mA:

> **R = (12 V − 1.2 V − 0.2 V) / 8 mA ≈ 1.3 kΩ** → valor comercial: **1.2 kΩ**

El módulo HiLetgo integra esta resistencia; el cálculo se incluye como referencia de diseño.

**Pull-up de salida.** El pin **IO35 es de solo entrada y no dispone de pull-up interno**. La resistencia física de **10 kΩ a 3.3 V** es obligatoria para definir el nivel alto y evitar lecturas flotantes. Con 10 kΩ y la capacidad típica del cableado, el tiempo de subida es del orden de microsegundos, muy inferior a la duración de los pulsos del monedero (decenas de milisegundos).

**Asignación de pulsos por moneda:**

| Moneda | Pulsos | Valor |
| :--- | ---: | ---: |
| RD$ 1 | 1 | RD$ 1.00 |
| RD$ 5 | 3 | RD$ 5.00 |
| RD$ 10 | 5 | RD$ 10.00 |
| RD$ 25 | 7 | RD$ 25.00 |

**Figura 5 — Acondicionamiento del monedero**

![Optoacoplador PC817](./img/optoacoplador_pc817.png)

*Descripción técnica:* módulo optoacoplador HiLetgo de 2 canales basado en PC817. El canal 1 se usa para el pulso del monedero; el canal 2 queda de reserva.

*Qué debe mostrar la fotografía:* identificación de IN1/OUT1, resistencia limitadora de entrada, bornera de 3.3 V/GND del lado lógico y la resistencia de 10 kΩ conectada entre OUT1 y 3.3 V.

**Figura 6 — Monedero CH-926**

![Monedero CH-926](./img/monedero_ch926.png)

*Descripción técnica:* validador multimoneda configurable, alimentado a 12 V, con salida de pulsos por transistor open-collector.

*Qué debe mostrar la fotografía:* bornera de alimentación (12 V/GND), línea de salida de pulsos, selección de monedas y sentido de inserción.

### 3.3 Control de motores por temporización

Los seis canales usan la misma estrategia: **lazo abierto por tiempo**. El único elemento de actuación es el PCF8574 `0x20`, que conmuta los relés del banco.

| Grupo | Canales | Modelo | Hilos | Modo de despacho |
| :--- | :---: | :--- | :---: | :--- |
| **M1–M6** | 1–6 | LK20052 / LK20052 Fy-007 | 2–3 | Lazo abierto · pulso de 3.0 s |

#### 3.3.1 Secuencia de despacho

1. El firmware toma el Mutex de despacho (solo un motor a la vez).
2. Escribe en `0x20` el bit del canal en **HIGH (3.3 V)**: el NPN satura, el `IN` del relé va a 0 V, el relé se energiza y el motor recibe 12 V.
3. Mantiene el pulso durante **3000 ms** (`MOTOR_TIMED_PULSE_MS`).
4. Escribe el bit en **LOW (0 V)**: el NPN queda al corte, el `IN` sube y el motor se detiene.
5. Libera el Mutex tras una pausa de asentamiento de 250 ms.

| Paso | Bit en `0x20` | P0…P5 | NPN | `IN` del relé | Relé | Motor |
| :--- | :---: | :---: | :---: | :---: | :--- | :--- |
| Reposo | 0 | LOW ≈ 0 V | Corte | Alto | Desenergizado | Detenido |
| Despacho | 1 | HIGH ≈ 3.3 V | Saturado | LOW ≈ 0 V | Energizado | Girando |
| Fin del pulso | 0 | LOW ≈ 0 V | Corte | Alto | Desenergizado | Detenido |

> **⚠️ Nota de diseño — Calibración del pulso.** Los 3000 ms deben ajustarse con el espiral cargado y la tensión real de la fuente. Un pulso demasiado corto no entrega el producto; uno demasiado largo puede forzar el mecanismo. Al no haber retroalimentación, la repetibilidad depende de la calibración mecánica y de mantener constante la tensión de 12 V.

#### 3.3.2 Protección del pulso

El relé se apaga siempre al terminar el tiempo. Si el bus I2C falla, `motor_control` reporta el error a la GUI y no deja el canal activo de forma indefinida.

### 3.4 Módulos de relé (6 × 1 canal, 0x20)

Se usan **6 módulos de 1 canal** (SRD-12VDC-SL-C) con bobina de 12 V y entrada optoacoplada. El **jumper `H/L` va en LOW**: 0 V en `IN` energiza la bobina y el estado alto la desenergiza. Como el LED del opto está referenciado a 12 V, el PCF8574 no puede apagarlo con su HIGH débil de 3.3 V; por eso cada canal lleva un **transistor NPN inversor**:

> **P0…P5** → 1 kΩ → **BASE** · **EMISOR** → GND · **COLECTOR** → `IN` del módulo

El PCF8574 satura el NPN para encender (`IN` = 0 V) y lo deja al corte para apagar (`IN` flotante → alto). La bobina se alimenta desde el riel de 12 V a través de `DC+` del módulo; el PCF8574 no alimenta la bobina.

| P0…P5 (0x20) | NPN | `IN` del módulo | LED interno | Bobina 12 V | Contacto COM–NO | Motor |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| LOW ≈ 0 V | Corte | Alto | Apagado | Desenergizada | Abierto | Detenido |
| HIGH ≈ 3.3 V | Saturado | ≈ 0 V | Encendido | Energizada | Cerrado | Alimentado a 12 V |

**Ruta de potencia por canal:**

> **Riel 12 V** → **Fusible** → **COM del relé** → **NO** → **Motor (+)** → **GND de potencia**
>
> **Riel 12 V** → **VCC del módulo** → **Bobina del relé** → **GND del módulo**

**Protecciones recomendadas:**

- **Diodo de rueda libre** en paralelo con cada motor (1N5408 o equivalente, cátodo al positivo) para recortar el pico inductivo al desenergizar.
- **Red RC** de 100 Ω + 100 nF entre contactos COM y NO para amortiguar el arco eléctrico y prolongar la vida del relé.
- **Fusible por canal** o protección térmica si el cableado de motor es de sección reducida.

**Capacidad de contacto.** La corriente de arranque del motor (≈ 2–3 A) debe quedar por debajo de la corriente nominal del contacto del relé. Los módulos comerciales suelen declarar 10 A / 250 V AC, pero el margen real en DC inductivo es menor.

> **⚠️ Nota de diseño — Entrada de control.** El jumper `H/L` de los 6 módulos debe quedar en **LOW**. El PCF8574 enciende escribiendo **HIGH (3.3 V)** y apaga con **LOW (0 V)**, porque el NPN invierte. Si se cambia el jumper a HIGH, el sistema deja de funcionar: ese modo necesita que el pin entregue corriente y el PCF8574 solo hunde.

**Figura 7 — Módulo de relé de 1 canal**

![Módulo de Relés 0x20](./img/modulo_reles.png)

*Descripción técnica:* módulo de 1 relé SRD-12VDC-SL-C con bobina de 12 V, entrada optoacoplada y jumper `H/L`. Se usan 6 unidades, una por motor, con un NPN inversor por canal.

*Qué debe mostrar la fotografía:* bornera `IN / DC- / DC+`, jumper `H/L` en LOW, jumper `VCC` y numeración de los contactos `NO / COM / NC`.

### 3.5 Capa física del bus I2C

| Parámetro | Valor | Justificación |
| :--- | :--- | :--- |
| Frecuencia | 100 kHz | Modo estándar; robusto con cableado largo |
| Nivel lógico | 3.3 V | Compatible con el ESP32-32E sin adaptador |
| SDA | IO32 | Línea de datos, drenador abierto |
| SCL | IO25 | Línea de reloj, drenador abierto |
| Pull-ups | 10 kΩ | Un único módulo PCF8574 en el bus |
| Capacidad máxima de bus | 400 pF | Límite del modo estándar |

**Direccionamiento:**

| Módulo | A2 | A1 | A0 | Dirección de 7 bits | Función |
| :--- | :---: | :---: | :---: | :--- | :--- |
| PCF8574 de actuadores | 0 | 0 | 0 | `0x20` | Escritura de relés |

> **⚠️ Nota de diseño — Variante del circuito integrado.** El **PCF8574** tiene dirección base `0x20`; el **PCF8574A** tiene dirección base `0x38`. Si los módulos montan la variante "A", las direcciones son `0x38` y `0x39`, y los DIP switches se ajustan en consecuencia.

**Buenas prácticas de cableado:**

- Trenzar SDA y SCL con un retorno de GND y mantener el par alejado de los conductores de motor.
- No superar 1 m de bus sin repetidor; para distancias mayores, usar un buffer I2C o reducir la frecuencia.
- Un capacitor de 100 nF entre VCC y GND en cada módulo, lo más cerca posible del integrado.
- No conectar ni desconectar módulos con el sistema energizado.

**Figura 8 — Bus I2C y expansor**

![Bus I2C](./img/bus_i2c.png)

*Descripción técnica:* conexión del módulo PCF8574 al puerto I2C de la placa, con SDA en IO32 y SCL en IO25.

*Qué debe mostrar la fotografía:* recorrido del par SDA/SCL, ubicación de las resistencias de pull-up, configuración de los DIP switches y conexión de VCC a 3.3 V.

---

## 4. Matriz de Conexiones (Pinout Base)

### 4.1 Resumen de pines utilizados del ESP32-32E

| GPIO | Función | Dirección | Riel | Observaciones |
| :--- | :--- | :--- | :--- | :--- |
| **IO2** | TFT — Data/Command (DC) | Salida | 3.3 V | Pin de arranque (*strapping*) |
| **IO5** | MicroSD — CS | Salida | 3.3 V | Pin de arranque (*strapping*) |
| **IO12** | TFT — MISO | Entrada | 3.3 V | Pin de arranque (*MTDI*); no añadir pull-up externo |
| **IO13** | TFT — MOSI | Salida | 3.3 V | — |
| **IO14** | TFT — SCLK | Salida | 3.3 V | Reloj SPI2 a 40 MHz |
| **IO15** | TFT — CS | Salida | 3.3 V | Pin de arranque (*strapping*) |
| **IO18** | MicroSD — CLK | Salida | 3.3 V | SPI3 |
| **IO19** | MicroSD — MISO | Entrada | 3.3 V | SPI3 |
| **IO23** | MicroSD — MOSI | Salida | 3.3 V | SPI3 |
| **IO25** | I2C — SCL | Drenador abierto | 3.3 V | Requiere pull-up |
| **IO27** | TFT — Retroiluminación | Salida PWM | 3.3 V | Control de brillo |
| **IO32** | I2C — SDA | Drenador abierto | 3.3 V | Requiere pull-up |
| **IO33** | Táctil — CS | Salida | 3.3 V | Comparte SPI2 con la TFT |
| **IO35** | Pulso de monedero | **Solo entrada** | 3.3 V | Sin pull-up interno; requiere 10 kΩ externo |
| **EN** | Reset de la placa | Entrada | 3.3 V | Compartido con el reset de la TFT |
| **3V3** | Salida del regulador | Alimentación | 3.3 V | Alimenta el expansor y periféricos |
| **5V** | Entrada de alimentación | Alimentación | 5 V | Desde el LM2596 |
| **GND** | Referencia común | — | 0 V | Punto de unión con el retorno de potencia |

> **⚠️ Nota de diseño — Pines de arranque.** IO2, IO5, IO12 y IO15 determinan el modo de arranque del módulo ESP32. La placa Hosyond ya integra el hardware de pantalla y MicroSD sobre estos pines; **no deben añadirse resistencias externas** que alteren su estado durante el encendido.

### 4.2 Bus I2C y expansor

| Señal | ESP32-32E | PCF8574 `0x20` | Nivel |
| :--- | :--- | :--- | :--- |
| SDA | IO32 | SDA | 3.3 V |
| SCL | IO25 | SCL | 3.3 V |
| VCC | 3V3 | VCC | 3.3 V |
| GND | GND | GND | 0 V |
| A0 | — | 0 | — |
| A1 | — | 0 | — |
| A2 | — | 0 | — |

### 4.3 Monedero y optoacoplador

| Origen | Terminal | Destino | Nivel / Nota |
| :--- | :--- | :--- | :--- |
| CH-926 | +12 V | Riel 12 V | 12 V DC |
| CH-926 | GND | GND de potencia | 0 V |
| CH-926 | Salida de pulsos (COIN) | IN1 del PC817 | Open-collector a GND |
| PC817 | VCC (lado lógico) | 3V3 de la placa | 3.3 V |
| PC817 | GND (lado lógico) | GND lógico | 0 V |
| PC817 | OUT1 | IO35 | Activo LOW |
| Resistencia 10 kΩ | Extremo 1 | OUT1 / IO35 | Pull-up obligatorio |
| Resistencia 10 kΩ | Extremo 2 | 3V3 | 3.3 V |

### 4.4 Pantalla TFT ST7796 y táctil XPT2046

| Función | GPIO | Señal | Observaciones |
| :--- | :--- | :--- | :--- |
| Reloj SPI | IO14 | SCLK | Bus SPI2 a 40 MHz |
| Datos MOSI | IO13 | MOSI | Compartido con el táctil |
| Datos MISO | IO12 | MISO | Compartido con el táctil |
| Chip Select TFT | IO15 | CS | Exclusivo del panel |
| Data/Command | IO2 | DC | Alto = datos, bajo = comando |
| Reset TFT | EN | RST | Atado al reset de la placa |
| Retroiluminación | IO27 | BL | PWM de brillo |
| Chip Select táctil | IO33 | CS | Exclusivo del XPT2046 |
| IRQ táctil | — | PENIRQ | No conectado; lectura por sondeo |

### 4.5 Lector MicroSD

> **Desactivada en el firmware actual** (`BOARD_SD_ENABLED = 0`): las imágenes van
> embebidas en flash. El conector se conserva en la placa, pero no se monta.

| Función | GPIO | Señal | Observaciones |
| :--- | :--- | :--- | :--- |
| Chip Select | IO5 | CS | Bus SPI3 |
| Reloj | IO18 | CLK | Bus SPI3 |
| Datos MOSI | IO23 | MOSI | Bus SPI3 |
| Datos MISO | IO19 | MISO | Bus SPI3 |
| Alimentación | 3V3 | VCC | 3.3 V |
| Referencia | GND | GND | 0 V |

### 4.5b Sensores de posición (expansor `0x21`)

| Sensor | Señal | Entrada | Acondicionamiento |
| :---: | :--- | :---: | :--- |
| M1 | Salida del sensor | P0 | Divisor 10 kΩ serie + 3.3 kΩ a GND |
| M2 | Salida del sensor | P1 | Divisor 10 kΩ serie + 3.3 kΩ a GND |
| M3 | Salida del sensor | P2 | Divisor 10 kΩ serie + 3.3 kΩ a GND |
| M4 | Salida del sensor | P3 | Divisor 10 kΩ serie + 3.3 kΩ a GND |

`home = LOW` (`BOARD_SENSOR_HOME_LEVEL = 0`), pendiente de confirmar con multímetro.
El PCF8574 `0x21` se alimenta a **3.3 V**.

### 4.6 Matriz de canales: relés y motores

| Canal | Motor | Tipo de motor | Relé `0x20` | Sensor `0x21` | Estrategia de despacho |
| :---: | :---: | :--- | :---: | :---: | :--- |
| **1** | M1 | LK20052 · 3 hilos | P0 | P0 | Lazo cerrado |
| **2** | M2 | LK20052 · 3 hilos | P1 | P1 | Lazo cerrado |
| **3** | M3 | LK20052 · 3 hilos | P2 | P2 | Lazo cerrado |
| **4** | M4 | LK20052 · 3 hilos | P3 | P3 | Lazo cerrado |
| **5** | M5 | LK20052 Fy-007 · 2 hilos | P4 | — | Tiempo · pulso 3.0 s |
| **6** | M6 | LK20052 Fy-007 · 2 hilos | P5 | — | Tiempo · pulso 3.0 s |
| — | — | — | P6 / P7 | — | No usado |

*El bit del canal en `0x20` se pone en **HIGH (3.3 V)** durante el despacho (el NPN lleva `IN` a 0 V) y vuelve a **LOW (0 V)** al terminar. P6 y P7 permanecen en nivel bajo.*

### 4.7 Distribución de borneras y conectores

| Conector | Tipo | Señales | Destino |
| :--- | :--- | :--- | :--- |
| Alimentación principal | Bornera atornillada 2 pines | 12 V / GND | Fuente conmutada |
| Entrada de placa | JST o bornera 2 pines | 5 V / GND | LM2596 |
| I2C Peripheral Interface | JST 4 pines | 3.3 V / GND / SDA / SCL | PCF8574 `0x20` (relés) y `0x21` (sensores) |
| Expand the input pin | JST | IO35 / 3.3 V / GND | Salida del PC817 |
| Salida de relés | Bornera por canal | COM / NO | Motores |
| Alimentación de bobinas | Bornera 2 pines | 12 V / GND | Módulo de relés |

**Figura 9 — Panel TFT ST7796**

![Pantalla ST7796](./img/pantalla_st7796.png)

*Descripción técnica:* panel TFT ST7796 de 320 × 480 px con táctil resistivo XPT2046 integrado y conector flexible.

*Qué debe mostrar la fotografía:* conector FPC, puerto de expansión de la placa, borneras de alimentación y etiquetado de los pines utilizados.

**Figura 10 — Borneras de conexión**

![Borneras de conexión](./img/borneras_conexion.png)

*Descripción técnica:* detalle del cableado en las borneras de salida, con numeración de canales y separación entre potencia y señal.

*Qué debe mostrar la fotografía:* etiquetas de cada canal, terminales de COM/NO, prensaestopas y alivio de tensión.

---

## 5. Marcadores de Imágenes para Componentes

Los marcadores siguientes utilizan rutas relativas a la carpeta `Diseño_electronico/`. Las imágenes deben colocarse en `./img/` con el nombre exacto indicado.

### 5.1 Índice de figuras

| Figura | Archivo | Sección | Contenido esperado |
| :---: | :--- | :--- | :--- |
| 1 | `img/placa_hosyond_esp32_32e.png` | 1.3 | Placa base y puertos |
| 2 | `img/cableado_general.png` | 1.3 | Mazo de cables y tierras |
| 3 | `img/fuente_12v.png` | 2.4 | Fuente conmutada |
| 4 | `img/stepdown_lm2596.png` | 2.4 | Módulo reductor |
| 5 | `img/optoacoplador_pc817.png` | 3.2 | Aislamiento del monedero |
| 6 | `img/monedero_ch926.png` | 3.2 | Validador de monedas |
| 7 | `img/modulo_reles.png` | 3.4 | Módulos de relé `0x20` |
| 8 | `img/bus_i2c.png` | 3.5 | Bus I2C y pull-ups |
| 9 | `img/pantalla_st7796.png` | 4.7 | Panel TFT |
| 10 | `img/borneras_conexion.png` | 4.7 | Borneras y etiquetado |

### 5.2 Convención para las fotografías

- **Formato:** PNG para fotografías y diagramas de bloque.
- **Resolución mínima:** 1920 × 1080 px, con iluminación uniforme y sin reflejos.
- **Encuadre:** incluir una referencia de escala (regla o moneda) y etiquetar los conectores visibles.
- **Nomenclatura:** minúsculas, sin acentos ni espacios; guion bajo como separador.
- **Trazabilidad:** cada figura debe corresponder a la versión física montada; si cambia el cableado, actualizar la imagen y la fecha del documento.

---

## Anexo A — Lista de materiales eléctricos

| Cantidad | Componente | Especificación | Función |
| :---: | :--- | :--- | :--- |
| 1 | Placa Hosyond 4.0" | ESP32-32E, TFT ST7796 320 × 480, táctil XPT2046, MicroSD | Control e interfaz |
| 1 | Fuente conmutada | 110 V AC → 12 V DC, ≥ 10 A | Alimentación principal |
| 1 | Step-Down LM2596 | 12 V → 5 V, ≥ 3 A, con disipador | Alimentación de la placa base |
| 1 | Monedero Joyzan CH-926 | 12 V, salida de pulsos open-collector | Aceptación de monedas |
| 1 | Módulo optoacoplador HiLetgo | PC817, 2 canales | Aislamiento del pulso |
| 1 | Resistencia | 10 kΩ, 1/4 W | Pull-up de IO35 |
| 1 | Módulo PCF8574 | I2C, 3.3 V, dirección `0x20` | Expansión de salidas (relés) |
| 6 | Módulos de relé de 1 canal | SRD-12VDC-SL-C, bobina 12 V, jumper `H/L` en LOW | Conmutación de motores |
| 6 | Transistores NPN | BC547 / 2N3904 / S8050 (Vceo ≥ 12 V) | Inversor PCF8574 → `IN` del relé |
| 6 | Resistencias de base | 1 kΩ, 1/4 W | Limitación de la corriente de base |
| 4 | Motores LK20052 | 12 V DC, 3 hilos | Canales M1–M4 (lazo abierto) |
| 2 | Motores LK20052 Fy-007 | 12 V DC, 2 hilos | Canales M5–M6 (lazo abierto) |
| 6 | Diodos 1N5408 | 3 A / 1000 V | Rueda libre de motores |
| 6 | Red RC | 100 Ω + 100 nF | Amortiguamiento de contactos |
| 1 | Fusible | 10 A con portafusible | Protección principal |
| — | Conductores | 18 AWG potencia, 24 AWG señal | Cableado |

## Anexo B — Riesgos y decisiones de diseño

| Riesgo | Impacto | Mitigación |
| :--- | :--- | :--- |
| Entrada de relé referenciada a 12 V | El relé no suelta con el HIGH débil (3.3 V) del PCF8574 | **Resuelto:** transistor NPN inversor por canal (`BOARD_RELAY_ACTIVE_LOW = 0`) |
| Pico inductivo del motor | Arco en contactos, reinicio del sistema | Diodo de rueda libre + red RC + desacoplo |
| Todos los canales sin retroalimentación | Producto no entregado o doble entrega | Calibrar el pulso de 3.0 s con el espiral cargado y mantener 12 V estable |
| Caída de tensión en el mazo de 12 V | Arranque débil del motor | 18 AWG y conexiones atornilladas |
| Ausencia de paro de emergencia | Riesgo mecánico durante mantenimiento | Añadir interruptor general accesible |
| Alimentación simultánea por USB y 5 V | Corriente inversa hacia el puerto USB | Diodo de OR en la placa o uso de una sola fuente |

## Anexo C — Glosario

| Término | Definición |
| :--- | :--- |
| **Active LOW** | Convención en la que la señal se considera activa cuando está en 0 V |
| **Contacto seco** | Contacto que no lleva tensión propia; solo cierra o abre un circuito |
| **Open-collector** | Salida que solo puede hundir corriente a GND, requiere pull-up externo |
| **Pull-up** | Resistencia que fija el nivel alto de una línea cuando nada la fuerza a bajo |
| **PCF8574** | Expansor de 8 bits por I2C con puertos cuasi-bidireccionales |
| **Rueda libre** | Diodo que recircula la corriente inductiva al desenergizar una carga |
| **Punto estrella** | Único punto de unión de todos los retornos, atornillado al borne V− / COM de la fuente |
| **V− / COM** | Terminal negativo de referencia de la fuente de 12 V |
| **Lazo abierto** | Control sin retroalimentación: la acción dura un tiempo calibrado (3.0 s) |

---

*Documento de hardware — versión 1.5. Las figuras 1 a 10 son marcadores pendientes de sustitución por material fotográfico definitivo.*
