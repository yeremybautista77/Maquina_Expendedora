# Referencia mecánica 2D — módulo PCF8574 (38 × 16 mm)

Geometría 2D del módulo comercial **PCF8574 IO Expander** usada como referencia
de posicionamiento de las dos huellas hembra (`1x04` y `1x09`) en el editor de PCB.

## Archivos

| Archivo | Descripción |
|---|---|
| `PCF8574_modulo_38x16.dxf` | Contorno 1:1 en mm (DXF R12, `$INSUNITS` = 4). Importable en KiCad, LibreCAD, AutoCAD, Fusion, etc. |
| `PCF8574_modulo_38x16.svg` | Misma geometría en SVG con `width`/`height` en mm (1:1). |
| `PCF8574_modulo_38x16.png` | Vista previa de la referencia. |
| `pcf8574_ref_mecanica.py` | Generador del DXF/SVG y del bloque KiCad; permite reinsertar la geometría. |

## Dimensiones utilizadas (1:1)

| Elemento | Medida |
|---|---|
| Contorno de la PCB | 38.0 × 16.0 mm |
| Cabezal `1x9` (`P0`–`P7`, `INT`) | 9 pines, paso 2.54 mm → 20.32 mm, sobre un borde largo |
| Cabezal `1x4` (`VCC`, `GND`, `SDA`, `SCL`) | 4 pines, paso 2.54 mm → 7.62 mm, en un extremo corto |
| Separación del centro de fila/columna al borde | 2.54 mm |
| Cuerpo del cabezal | 2.54 mm de ancho (1.27 mm por lado respecto a la fila) |
| Taladro de pin dibujado | Ø 1.0 mm |

Los dos cabezales quedan en **bordes perpendiculares** y se juntan en una esquina,
tal como en el módulo real.

## Procedencia de las medidas

No existe un DXF/STEP público y exacto de esta variante de 38 × 16 mm:

- **GrabCAD**: los modelos de PCF8574 requieren cuenta para descargar y no están
  disponibles como DXF anónimo.
- **SnapEDA / Ultra Librarian**: publican símbolo/huella del **circuito integrado**
  PCF8574 (SOIC-16), no del módulo.
- **Fritzing** (part `pcf8574-io-expansion-board`, descargado y revisado): su SVG de
  PCB mide 47.88 × 15.49 mm — corresponde a la variante Waveshare, **no** a esta.
- **Fotografías cenitales del módulo clásico**: usadas para verificar la disposición
  (9 pines en un borde largo, 4 pines en un extremo corto) y la proporción de la fila
  de 9 pines, que está centrada a lo largo.

Por eso la geometría se reconstruye a partir de las dimensiones nominales
publicadas y verificadas contra foto, y queda documentada en el script. Si aparece
un DXF oficial del módulo exacto, basta reemplazar las constantes de la sección
`Geometría` del script y volver a ejecutarlo.

## Inserción en el `.kicad_pcb`

El script agrega 56 primitivas (`gr_line`, `gr_circle`, `gr_text`) en la capa
**`Dwgs.User`**, a escala 1:1, en dos instancias (una por expansor) colocadas
justo **debajo del contorno de la PCB** (el contorno es el rectángulo de
`Edge.Cuts` 188.6–288.6 × 115–215 mm; las referencias van en y = 222–238 mm).

```powershell
# regenerar DXF/SVG
python cad\pcf8574_ref_mecanica.py

# reinsertar en la PCB (idempotente: reemplaza las primitivas anteriores)
python cad\pcf8574_ref_mecanica.py --insert "Diseño_electronico\PCB\PCB\PCB.kicad_pcb"
```

La inserción crea una copia de seguridad `PCB.kicad_pcb.bak_pcf8574` y conserva los
saltos de línea CRLF del archivo original. Cada primitiva usa un UUID fijo derivado
de una semilla, por lo que volver a ejecutar el comando **no duplica** geometría.

## Cómo usarla

1. Abrir el editor de PCB y mostrar la capa `Dwgs.User`.
2. Seleccionar una referencia completa (arrastrando un rectángulo) y moverla sobre
   las huellas `1x04` / `1x09` del expansor correspondiente.
3. Alinear el cabezal de 9 pines de la referencia con la huella `1x09` y el de
   4 pines con la huella `1x04` (las huellas hembra se mueven para coincidir).
4. Agrupar la referencia con sus dos huellas y **bloquear** el conjunto.

Las referencias se dejan sin agrupar y sin bloquear a propósito.

## Uso de pines en esta placa

El módulo tiene 9 pines en el cabezal largo, pero la placa **solo cablea 6**:

- `0x20` (salidas): `P0`–`P5` → 6 etapas NPN. `P6`, `P7` e `INT` sin conectar.
- `0x21` (entradas): `P0`–`P5` → 6 divisores (4 sensores en uso + 2 de reserva).
  `P6`, `P7` e `INT` sin conectar.

Ver `Firmware/IDEA_PCB_CONEXIONES.md` para el mapa de pines completo.
