# PCB de interconexión — 6 salidas + 6 entradas

> **Estado:** **en KiCad** — esquemático terminado (ERC limpio) con **6 salidas + 6 entradas**, y PCB con las huellas colocadas.
> **Pendiente:** sincronizar los nets del PCB desde el esquema y rutear (§8).
> **Proyecto:** `Diseño_electronico/PCB/PCB/`.
> **Última actualización:** 2026-11-17.
> **Fuente de verdad de los pines:** `Firmware/main/board_config.h`.

---

## 1. Qué es esta placa

Placa de **interconexión** entre la placa de pantalla **ESP32-32E 4.0"** y el hardware de la máquina (módulos de relé, motores, sensores y monedero).

**No es una placa de potencia.** No genera alimentación, no lleva 12 V y no conmuta los motores. Solo concentra:

- El bus I2C y la alimentación de los dos expansores PCF8574.
- Las **6 etapas NPN** que atacan los `IN` de los módulos de relé.
- Los **6 divisores de tensión** de los sensores (4 en uso + 2 de reserva).
- El acondicionamiento de la señal del monedero (pull-up a 3.3 V).
- La distribución de señales y GND por borneras.

Todo lo que es potencia (12 V, bobinas de relé, contactos COM/NO y cables de motor) va **por cable**, fuera de la placa.

---

## 2. Alcance cerrado

### 2.1 Incluye

| Bloque | Contenido |
| :--- | :--- |
| I2C | Conector `J4` de 4 pines: `3V3 · SDA · SCL · GND` |
| Expansor de salidas | PCF8574 en `0x20`, en tira hembra `1x04` + `1x09` |
| Expansor de entradas | PCF8574 en `0x21`, en tira hembra `1x04` + `1x09` |
| Etapas de relé | **6** × NPN `PN2222A` + base 1 kΩ + pull-down 10 kΩ |
| Entradas de sensor | **6** × divisor 10 kΩ / 3.3 kΩ (4 usados, 2 de reserva) |
| Monedero | `J3` de 2 pines + pull-up de 10 kΩ a 3.3 V |
| Tierra | GND común en estrella, con pin dedicado en `J2` |
| Montaje | 4 agujeros M4 (`H1`–`H4`) |

### 2.2 No incluye (va por cable, fuera de la PCB)

- Fuente de 12 V.
- Fuente o regulador de 5 V / 3.3 V (el 3.3 V llega desde la placa de pantalla por `J4`).
- Alimentación `VCC` / `JD-VCC` de los módulos de relé.
- Contactos de potencia `COM` / `NO` / `NC` de los relés.
- Cables de los motores.
- Fusible, protección de polaridad inversa y capacitores de bulk para las bobinas.
- Pantalla, táctil, MicroSD y microcontrolador (van en la placa de pantalla).

---

## 3. Mapa de pines

La placa **no cablea los 8 pines** de cada expansor: solo los que se van a usar.

- **`0x20` (salidas):** se cablean **6 canales** `P0`–`P5` → 6 motores.
- **`0x21` (entradas):** se cablean **6 canales** `P_0`–`P_5` → 4 sensores en uso + 2 de reserva.

Los pines sobrantes (`P6`, `P7`, `INT`) quedan **sin conectar** en ambos expansores.

### 3.1 Expansor de salidas — `0x20` (6 de 8 pines)

| PCF8574 | Pin del módulo | Net | Etapa | Bornera | Motor (firmware) | Uso |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| P0 | 1 | `/P0` | Q1 | `J1.1` (`M0`) | M1 | En uso |
| P1 | 2 | `/P1` | Q2 | `J1.2` (`M1`) | M2 | En uso |
| P2 | 3 | `/P2` | Q3 | `J1.3` (`M2`) | M3 | En uso |
| P3 | 4 | `/P3` | Q4 | `J1.4` (`M3`) | M4 | En uso |
| P4 | 5 | `/P4` | Q5 | `J1.5` (`M4`) | M5 | En uso |
| P5 | 6 | `/P5` | Q6 | `J1.6` (`M5`) | M6 | En uso |
| P6 | 7 | — | — | — | — | **Sin uso** |
| P7 | 8 | — | — | — | — | **Sin uso** |
| INT | 9 | — | — | — | — | **Sin conectar** |

> La serigrafía de la placa rotula `M0`…`M5`; el firmware numera `M1`…`M6`. La equivalencia es **`M0` (placa) = `M1` (firmware)**, `M1` (placa) = `M2` (firmware), …, `M5` (placa) = `M6` (firmware).

- Dirección `0x20` = **A2 A1 A0 = 000**. Configurar los DIP **antes** de soldar.
- El firmware escribe **HIGH** para encender (por el NPN inversor).
- `BOARD_RELAY_ACTIVE_LOW = 0`; no cambiar el firmware.

### 3.2 Expansor de entradas — `0x21` (6 de 8 pines)

| PCF8574 | Pin del módulo | Net | Divisor (serie / a GND) | Bornera | Sensor | Uso |
| :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| P_0 | 1 | `/P_0` | 10 kΩ / 3.3 kΩ | `J2.1` (`S0`) | M1 | En uso |
| P_1 | 2 | `/P_1` | 10 kΩ / 3.3 kΩ | `J2.2` (`S1`) | M2 | En uso |
| P_2 | 3 | `/P_2` | 10 kΩ / 3.3 kΩ | `J2.3` (`S2`) | M3 | En uso |
| P_3 | 4 | `/P_3` | 10 kΩ / 3.3 kΩ | `J2.4` (`S3`) | M4 | En uso |
| P_4 | 5 | `/P_4` | 10 kΩ / 3.3 kΩ | `J2.5` (`S4`) | — | **Reserva** |
| P_5 | 6 | `/P_5` | 10 kΩ / 3.3 kΩ | `J2.6` (`S5`) | — | **Reserva** |
| — | — | `GND` | — | `J2.7` (`GND`) | — | Referencia de sensores |
| P_6 | 7 | — | — | — | — | **Sin uso** |
| P_7 | 8 | — | — | — | — | **Sin uso** |
| INT | 9 | — | — | — | — | **Sin conectar** |

**Por qué 6 entradas y no 4.** Hoy solo hay 4 sensores (M1–M4). Los dos divisores extra (`S4`, `S5`) se dejan **montados y cableados** para el caso de cambiar M5/M6 por motores que **sí** traigan sensor: en ese momento solo hay que conectar el sensor a la bornera y habilitar el lazo cerrado en el firmware, sin rediseñar ni volver a fabricar la PCB.

- `home = LOW` (`BOARD_SENSOR_HOME_LEVEL = 0`), pendiente de confirmar con multímetro.
- `BOARD_SENSOR_CHANNEL_COUNT = 4` en el firmware actual: `P_4` y `P_5` se leen como entradas pero **no se usan** todavía.

### 3.3 Bus I2C — `J4`

| Pin | Señal | Destino |
| :---: | :--- | :--- |
| 1 | `SCL` | ESP32 IO25 · `0x20` pin 4 · `0x21` pin 4 |
| 2 | `SDA` | ESP32 IO32 · `0x20` pin 3 · `0x21` pin 3 |
| 3 | `GND` | Referencia de los dos expansores |
| 4 | `3V3` | Alimentación de los dos expansores |

> El orden físico de `J4` es el que tiene hoy el esquema. **Rotular la serigrafía en ese mismo orden** para no invertir `3V3` y `GND` al conectar el arnés.

### 3.4 Monedero — `J3`

| Pin | Señal | Observaciones |
| :---: | :--- | :--- |
| 1 | `GND` | Referencia del CH-926 |
| 2 | `IO39 y V1` | Nodo del pulso, con pull-up de 10 kΩ (`R15`) a 3.3 V |

- `V1` del CH-926 es **colector abierto**: en reposo flota y en cada pulso hunde a GND.
- En `J3.2` se atornillan **juntos** el hilo `V1` del monedero y el hilo que va a la entrada `IO39` de la placa de pantalla: el borne es el punto de unión, no hace falta otra pista en la PCB.
- El pull-up de **10 kΩ a 3.3 V** define el nivel alto. `BOARD_COIN_ACTIVE_LOW = 1`.
- ⚠️ Ver **§8**: el firmware lee `GPIO35`, no `IO39`.

---

## 4. Diseño por bloque

### 4.1 Alimentación y bus I2C

```
J4.4 (3V3) ──┬── U1 VCC ── C3 100 nF ── GND
             ├── U2 VCC ── C2 100 nF ── GND
             └── C1 10 µF (electrolítico) ── GND
J4.1 (SCL) ──┬── U1 SCL
             └── U2 SCL
J4.2 (SDA) ──┬── U1 SDA
             └── U2 SDA
```

- **3.3 V obligatorio.** Nunca 5 V en el PCF8574: subiría SDA/SCL y podría dañar el ESP32.
- **100 nF cerámico** entre VCC y GND en **cada** PCF8574 (`C3` junto a `0x20`, `C2` junto a `0x21`).
- **10 µF** (`C1`) en el riel de 3.3 V de la placa.
- El bus corre a **100 kHz**. Los dos módulos traen pull-ups de 10 kΩ: en paralelo quedan ~5 kΩ, correcto. **No agregar más pull-ups.**
- Riel de 3.3 V: 0.4–0.5 mm. SDA/SCL: 0.25 mm.
- Mantener SDA/SCL cortos y lejos de los nodos de los sensores y de los colectores de los NPN.

### 4.2 Etapa NPN (una por canal, 6 en total)

```
U1 Pn ──[ R 1 kΩ ]──┬── BASE (Qn)
                    └──[ R 10 kΩ ]── GND   (pull-down)
        EMISOR ── GND
        COLECTOR ── J1.n ── IN del módulo de relé
```

- Transistor: **PN2222A** (TO-92).
- Base: **1 kΩ**. Pull-down de base: **10 kΩ** a GND (mantiene el NPN al corte si el PCF queda sin alimentar).
- Colector directo a la bornera `J1`.
- El `IN` del módulo queda a su `VCC` (5 V o 12 V) cuando el NPN está al corte; el transistor soporta esa tensión.
- El módulo de relé mantiene el **jumper `H/L` en LOW**: `IN` a 0 V activa.
- **Punto crítico:** el GND de los módulos de relé debe estar unido al GND de la placa. Si no, el NPN no puede hundir el `IN` y los relés no conmutan.

### 4.3 Divisor de cada sensor (6 en total)

```
J2.n (SIG, 0/12 V) ──[ 10 kΩ ]──┬── U2 Pn
                                │
                            [ 3.3 kΩ ]
                                │
                               GND
```

- Con 12 V en la entrada: `Vpin = 12 × 3.3 / (10 + 3.3) = 2.97 V`.
- `Vih` del PCF8574 a 3.3 V = `0.7 × 3.3 = 2.31 V` → margen de 0.66 V. Correcto.
- En reposo (`home`) el sensor entrega 0 V y el pin lee LOW.
- El **GND del sensor** debe ser el mismo nodo que el GND de la placa.

> El divisor asume que el sensor entrega **0 V / 12 V en push-pull**. Si fuera **open-collector** (solo hunde a GND), haría falta un pull-up a 12 V antes del divisor. Verificar con multímetro.

### 4.4 Tierra común (punto estrella)

```
J2.7 (GND sensores) ──┬── U1 GND
                      ├── U2 GND
                      ├── Q1…Q6 emisores
                      ├── resistencias 3.3 kΩ de los divisores
                      ├── C1 / C2 / C3
                      ├── J3.1 (GND del monedero)
                      └── J4.3 (GND del arnés I2C)
```

- El GND de sensores va por un **pin dedicado** en `J2`, no compartido con las señales.
- No hacer cadena de tierras por los módulos: todas las tierras lógicas vuelven a este punto.
- La corriente de motores **no** pasa por la PCB; por la placa solo circulan lógica, optos de relé y divisores.

### 4.5 Referencia mecánica de los módulos

En `cad/` están el DXF/SVG 1:1 del módulo PCF8574 (38 × 16 mm, cabezal `1x9` en un borde largo y `1x4` en un extremo corto) y la geometría ya insertada en la capa `Dwgs.User` de la PCB, para alinear las tiras hembra antes de rutear. Ver `cad/README.md`.

---

## 5. Conectores de la placa

| Ref. | Tipo | Pines | Señales | Observaciones |
| :---: | :--- | :---: | :--- | :--- |
| `J1` | Phoenix MKDS 1,5 · 5.08 mm | 6 | `M0`–`M5` | Colectores de Q1–Q6 → `IN` de los 6 módulos de relé |
| `J2` | Phoenix MKDS 1,5 · 5.08 mm | 7 | `S0`–`S5`, `GND` | Señales de sensores; `GND` en el pin 7 (último) |
| `J3` | Phoenix MKDS 1,5 · 5.08 mm | 2 | `GND`, `IO39 y V1` | Monedero CH-926 |
| `J4` | Phoenix MKDS 1,5 · 5.08 mm | 4 | `SCL`, `SDA`, `GND`, `3V3` | Desde la placa de pantalla |
| `H1`–`H4` | Agujero M4 | 1 c/u | — | Montaje de la placa |

> `J1` y `J2` ya son de **6 y 7 pines** en el esquema y en el PCB (huellas `MKDS-1,5-6` y `MKDS-1,5-7`). El `GND` de los sensores va en el **pin 7**, al final de la bornera.

### 5.1 Conexión de los motores (fuera de la placa)

La placa **no conmuta los motores**: `J1` solo lleva la señal de cada canal a la
entrada `IN` de su módulo de relé. La potencia se arma aparte, en la bornera del
módulo:

```
+12 V → fusible → COM del relé → NO → motor (+)
motor (−) → V− / COM de la fuente de 12 V
```

Cada motor trae su propio conector en el arnés. **Antes de comprar nada, medir el
paso entre pines** del conector del motor (centro a centro):

| Paso | Familia | Corriente | ¿Sirve? |
| :---: | :--- | :---: | :--- |
| 2.54 mm | Molex KK 254 / JST XH | 3–4 A | Al límite: el arranque llega a 2.5 A |
| 3.0 mm | Molex Micro-Fit 3.0 | 5 A | Sí |
| 3.96 mm | Molex KK 396 | 7 A | Sí |
| 4.2 mm | Molex Mini-Fit Jr | 9 A | Sí, la opción recomendada |
| 2.0 mm | JST PH | 2 A | **No** |

Si se puede elegir libremente:

| Cant. | Pieza | Para |
| :---: | :--- | :--- |
| 4 | Conector de 3 pines (pareja macho/hembra) | M1–M4, con sensor |
| 2 | Conector de 2 pines (pareja macho/hembra) | M5–M6, sin sensor |
| — | Terminales de repuesto y crimpadora | Armado del arnés |

Pinout de los conectores de 3 pines:

| Pin | Señal | Destino |
| :---: | :--- | :--- |
| 1 | Motor **+12 V** | Contacto **NO** del relé del canal |
| 2 | Motor **GND** | Borne **V− / COM** de la fuente |
| 3 | Sensor de posición | Borne `S0`–`S3` de `J2` |

Cable: **18 AWG** para los dos hilos del motor y **22–24 AWG** para el sensor.

> **El hilo del sensor sale a 12 V.** Entra por `J2`, donde está el divisor
> 10 kΩ / 3.3 kΩ. Conectarlo directo a un GPIO del ESP32 lo destruye.
>
> **Polaridad.** Si se invierten los pines 1 y 2, el motor gira al revés. Usar
> conectores con llave para que no se puedan enchufar invertidos.

Los motores **M5 y M6 no llevan sensor**: su conector es de 2 pines y el despacho
es por tiempo (3 s).

---

## 6. Lista de materiales de la placa

| Cant. | Componente | Valor / tipo | Notas |
| :---: | :--- | :--- | :--- |
| 1 | PCB de interconexión | 100 × 100 mm, 2 capas | 4 agujeros M4 |
| 2 | Tira hembra `1x04` | 2.54 mm | Cabezal I2C de cada módulo |
| 2 | Tira hembra `1x09` | 2.54 mm | Cabezal `P0–P7 + INT` de cada módulo |
| 6 | Transistor NPN | `PN2222A` (TO-92) | Etapa de relé |
| 6 | Resistencia | 1 kΩ, 1/4 W | Base de cada NPN |
| 6 | Resistencia | 10 kΩ, 1/4 W | Pull-down de cada base |
| 6 | Resistencia | 10 kΩ, 1/4 W | Serie del divisor |
| 6 | Resistencia | 3.3 kΩ, 1/4 W | Parte baja del divisor |
| 1 | Resistencia | 10 kΩ, 1/4 W | Pull-up del monedero |
| 1 | Capacitor | 10 µF / 16 V electrolítico | Riel de 3.3 V |
| 2 | Capacitor | 100 nF cerámico | Desacople de cada PCF8574 |
| 1 | Bornera | Phoenix 1x06 · 5.08 mm | `J1` motores |
| 1 | Bornera | Phoenix 1x07 · 5.08 mm | `J2` sensores |
| 1 | Bornera | Phoenix 1x02 · 5.08 mm | `J3` monedero |
| 1 | Bornera | Phoenix 1x04 · 5.08 mm | `J4` I2C |

> Esta lista cubre solo la placa de interconexión. Los módulos de relé y los
> conectores del arnés de motores no van soldados aquí: ver §5.1 y §3.4.

---

## 7. Reglas de diseño para KiCad

1. **Ancho de pistas:**
   - 3.3 V y GND: 0.4–0.5 mm.
   - SDA/SCL, sensores y bases: 0.25 mm.
   - Colectores / `IN`: 0.3 mm.
   - No hay pistas de 12 V en la placa.
2. **Plano de GND** en la cara inferior si es posible; si no, tierra en estrella.
3. **Separación:** mantener los nodos de sensores y SDA/SCL alejados de los colectores y de `J1`.
4. **Desacople** de 100 nF lo más cerca posible de cada PCF8574.
5. **Serigrafía:** rotular cada bornera (`SCL`, `SDA`, `GND`, `3V3`, `M0`…`M5`, `S0`…`S5`, `COIN`) y marcar el **pin 1** de cada PCF8574.
6. **Orientación:** los módulos van en tiras hembra, así que dejar acceso a los **DIP de dirección** y espacio para retirarlos.
7. **Agujeros de montaje** y test points: `3V3`, `GND`, `SDA`, `SCL`, los 6 `IN`, los 6 nodos de sensor y el nodo del monedero.
8. **ERC/DRC** limpios antes de fabricar. Revisar que ningún pin del PCF quede flotando sin propósito.

---

## 8. Pendientes y discrepancias antes de fabricar

- [ ] **Sincronizar los nets del PCB.** El `.kicad_pcb` ya tiene las **46 huellas** colocadas (6 NPN, 25 resistencias, 3 capacitores, `J1` 1x06, `J2` 1x07, `J3` 1x02, `J4` 1x04, `H1`–`H4` y las dos parejas `1x04`/`1x09`), pero **sin nets**: falta «Actualizar PCB desde el esquema» y luego rutear.
- [ ] **Monedero: `IO39` vs `GPIO35`.** El nodo se rotula `IO39 y V1` y en `J3.2` se atornillan juntos el `V1` del CH-926 y el hilo hacia la placa de pantalla. `board_config.h` define `BOARD_COIN_GPIO = GPIO_NUM_35`: unificar el pin antes de armar el arnés.
- [ ] **Conector del motor:** medir el paso del conector que trae cada motor y comprar la pareja (ver §5.1). Sin ese dato no se puede armar el arnés.
- [ ] **Serigrafía:** confirmar si se rotula `M0`–`M5` / `S0`–`S5` (como el esquema) o `M1`–`M6` (como el firmware).
- [ ] **Pines 7, 8 y 9 de ambos expansores** (`P6`, `P7`, `INT`) salen como `unconnected` en el netlist: es intencional. Si el ERC los marca, ponerles bandera de «no conectar».
- [ ] **Polaridad del sensor:** medir con multímetro la salida en reposo y en movimiento. Confirmar `home = 0 V`.
- [ ] **Tipo de salida del sensor:** push-pull 0/12 V u open-collector.
- [ ] **`V1` del CH-926:** medir en reposo. Debe quedar flotando (open-collector) y pulsar a 0 V.
- [ ] **Direcciones I2C:** `0x20` (000) y `0x21` (001) configuradas en los DIP antes de soldar.
- [ ] **Nivel en el pin del `0x21`:** con el sensor en 12 V debe medir ~2.97 V (no más de 3.3 V).
- [ ] **Pulso de arranque:** el PCF8574 arranca con `P0`–`P7` en HIGH y los 6 NPN conducen ~0.4 s hasta que el firmware escribe 0.

---

## 9. Decisiones cerradas (no reabrir sin justificación)

1. Solo **I2C, expansores, NPN, divisores, monedero y GND** en la placa.
2. **Sin** fuente de 12 V, **sin** alimentación de relés y **sin** contactos de potencia.
3. PCF8574 a **3.3 V**, nunca a 5 V.
4. Relés con **NPN inversor**: PCF en HIGH enciende (`BOARD_RELAY_ACTIVE_LOW = 0`).
5. Divisor de sensores: **10 kΩ / 3.3 kΩ**, `home = LOW`.
6. Monedero: **10 kΩ a 3.3 V**, activo en LOW.
7. **Un solo** punto de GND hacia la fuente de 12 V.
8. **6 salidas** (`P0`–`P5`) y **6 entradas** (`P_0`–`P_5`): los pines `P6`, `P7` e `INT` de ambos expansores quedan sin conectar.
9. Las **2 entradas de reserva** (`S4`, `S5`) se montan ya cableadas, para futuros motores con sensor.
10. M1–M4 con sensor; **M5–M6 por tiempo** (no llevan sensor).
11. Los módulos PCF8574 van en **tiras hembra**, no soldados.

---

## 10. Historial

| Fecha | Cambio |
| :--- | :--- |
| 2026-11-11 | Creación del documento con el alcance cerrado de la PCB de interconexión. |
| 2026-11-17 | Actualizado al diseño implementado en KiCad: **6 salidas + 6 entradas**, conectores reales (`J1` 1x06, `J2` 1x07, `J3` 1x02, `J4` 1x04), BOM de la placa y mapa de pines verificado contra el netlist del esquema. `GND` de sensores en el pin 7 de `J2`. |
| 2026-11-24 | Añadida la §5.1 con la conexión física de los motores: potencia por los contactos del relé, conector del arnés y pinout. Nota en el BOM sobre lo que no va en la placa. |
