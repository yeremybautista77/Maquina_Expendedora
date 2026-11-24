# Diseño electrónico

Documentación eléctrica del sistema y proyecto de la **PCB de interconexión**.

## Contenido

| Ruta | Contenido |
| :--- | :--- |
| `ARQUITECTURA_HARDWARE.md` | Dominios de voltaje (12 V / 5 V / 3.3 V), topología, protecciones, calibres de conductor, mapa de pines del ESP32 y criterios de armado |
| `img/` | Figuras referenciadas por el documento de arquitectura |
| `PCB/` | Proyecto KiCad de la placa de conectores y PDFs de fabricación |

## PCB de interconexión

Placa de 100 × 100 mm, 2 capas, que concentra el bus I2C, los dos expansores
PCF8574, las seis etapas NPN de relé, los divisores de los sensores y el
acondicionamiento del monedero. **No lleva 12 V ni contactos de potencia**: eso queda
en la bornera de los módulos de relé.

| Archivo | Uso |
| :--- | :--- |
| `PCB/PCB/PCB.kicad_sch` | Esquema eléctrico |
| `PCB/PCB/PCB.kicad_pcb` | Editor de PCB |
| `PCB/PCB/PCB.kicad_pro` | Proyecto KiCad |
| `PCB/Pistas.pdf` | Impresión de la capa de pistas |
| `PCB/Silkscreen.pdf` | Serigrafía para revisión antes de fabricar |

> `PCB/PCB.zip` es el respaldo del proyecto completo y se conserva **fuera de Git**
> (2.2 MB). Los archivos `.kicad_sch`, `.kicad_pcb` y `.kicad_pro` sí están
> versionados y bastan para reabrir el proyecto.

**Estado:** huellas colocadas; falta sincronizar los nets desde el esquema y rutear.
El mapa de pines, el BOM y las reglas de diseño están en
`../Firmware/IDEA_PCB_CONEXIONES.md`.

> La placa se rotula con `M0`–`M5` y el firmware numera los canales `M1`–`M6`: es el
> mismo canal desplazado en uno. Está anotado en `../Firmware/IDEA_PCB_CONEXIONES.md` §8.

## Pendiente

- [ ] Sincronizar el PCB desde el esquema y rutear.
- [ ] Verificar ERC/DRC sin errores antes de mandar a fabricar.
- [ ] Unificar el pin del monedero: la placa dice `IO39` y el firmware lee `GPIO35`.
