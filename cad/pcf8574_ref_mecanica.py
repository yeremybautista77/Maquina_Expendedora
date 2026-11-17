# -*- coding: utf-8 -*-
"""
Referencia mecanica 2D del modulo comercial PCF8574 (IO Expander), 38 x 16 mm.

Origen de las medidas
---------------------
No existe un DXF/STEP publico oficial de este modulo (GrabCAD exige cuenta y los
modelos publicados son de la variante Waveshare de 47.9 x 15.5 mm).  La geometria
se reconstruye 1:1 a partir de las dimensiones nominales publicadas del modulo
clasico y verificadas contra fotografias cenitales:

  * Contorno de la PCB ......... 38.0 x 16.0 mm
  * Cabezal 1x9 (P0..P7, INT) .. 9 pines, paso 2.54 mm -> 20.32 mm, sobre un borde largo
  * Cabezal 1x4 (VCC,GND,SDA,SCL) 4 pines, paso 2.54 mm -> 7.62 mm, en un extremo corto
  * Separacion del primer pin al borde ... 2.54 mm (centro de la fila/columna)
  * Cuerpo del cabezal ..... 2.54 mm de ancho (pin + cuerpo de 1.27 mm por lado)

Sistema de coordenadas local (mm, Y hacia arriba, origen en la esquina inferior
izquierda del modulo):
  * Cabezal 1x9 en el borde largo SUPERIOR  (y = 16.0 - 2.54 = 13.46)
  * Cabezal 1x4 en el extremo corto IZQUIERDO (x = 2.54)
Esta orientacion reproduce la del modulo real: los dos cabezales quedan en bordes
perpendiculares y se juntan en una esquina.

Uso
---
  python pcf8574_ref_mecanica.py                    # genera .dxf y .svg
  python pcf8574_ref_mecanica.py --insert <pcb>     # ademas inserta en Dwgs.User

La insercion es idempotente: cada primitiva usa un UUID fijo derivado de una
semilla, de modo que al volver a ejecutar se reemplazan las anteriores.
"""

import argparse
import os
import re
import shutil
import sys
import uuid

# ----------------------------------------------------------------------------
# Geometria
# ----------------------------------------------------------------------------
MOD_L = 38.0          # largo (mm)
MOD_W = 16.0          # ancho (mm)
PITCH = 2.54          # paso de los cabezales (mm)
EDGE_OFF = 2.54       # centro de la fila/columna respecto al borde (mm)
HOLE_R = 0.5          # radio de la perforacion del pin (mm)
BODY_HALF = 1.27      # semiancho del cuerpo del cabezal (mm)

N9 = 9                # pines del cabezal P0..P7 + INT
N4 = 4                # pines del cabezal VCC, GND, SDA, SCL

# Cabezal 1x9: a lo largo del borde superior, centrado en el largo
P9_X0 = (MOD_L - (N9 - 1) * PITCH) / 2.0          # 8.84
P9_Y = MOD_W - EDGE_OFF                           # 13.46
PINS9 = [(P9_X0 + i * PITCH, P9_Y) for i in range(N9)]

# Cabezal 1x4: en el extremo izquierdo, centrado en el ancho
P4_X = EDGE_OFF                                   # 2.54
P4_Y0 = (MOD_W - (N4 - 1) * PITCH) / 2.0          # 4.19
PINS4 = [(P4_X, P4_Y0 + i * PITCH) for i in range(N4)]

# Cuerpos de los cabezales (rectangulos envolventes)
BODY9 = (PINS9[0][0] - BODY_HALF, P9_Y - BODY_HALF,
         PINS9[-1][0] + BODY_HALF, P9_Y + BODY_HALF)
BODY4 = (P4_X - BODY_HALF, PINS4[0][1] - BODY_HALF,
         P4_X + BODY_HALF, PINS4[-1][1] + BODY_HALF)

TEXTS = [
    ("PCF8574  38 x 16 mm", MOD_L / 2.0, 8.6, 1.4),
    ("1x9  P0..P7 + INT", MOD_L / 2.0, 6.6, 0.9),
    ("1x4  VCC GND SDA SCL", MOD_L / 2.0, 5.2, 0.9),
]


def _seg(x1, y1, x2, y2):
    return ("line", x1, y1, x2, y2)


def rect(x1, y1, x2, y2):
    return [_seg(x1, y1, x2, y1), _seg(x2, y1, x2, y2),
            _seg(x2, y2, x1, y2), _seg(x1, y2, x1, y1)]


def entities():
    """Devuelve la lista de primitivas en coordenadas locales (mm, Y arriba)."""
    out = []
    out += rect(0.0, 0.0, MOD_L, MOD_W)                      # contorno
    out += rect(*BODY9)                                      # cuerpo 1x9
    out += rect(*BODY4)                                      # cuerpo 1x4
    for (x, y) in PINS9 + PINS4:                             # taladros
        out.append(("circle", x, y, HOLE_R))
    for (txt, x, y, h) in TEXTS:                             # rotulos
        out.append(("text", x, y, txt, h))
    return out


# ----------------------------------------------------------------------------
# DXF R12 (mm)
# ----------------------------------------------------------------------------
LAYERS = [("CONTORNO", 7), ("CABEZALES", 3), ("ROTULOS", 2)]


def _f(v):
    return ("%.4f" % v).rstrip("0").rstrip(".") if v != int(v) else "%d" % int(v)


def write_dxf(path):
    l = []
    a = l.append
    a("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$INSUNITS\n70\n4")
    a("0\nENDSEC")
    # tabla de capas minima (algunos visores la necesitan)
    a("0\nSECTION\n2\nTABLES")
    a("0\nTABLE\n2\nLAYER\n70\n%d" % len(LAYERS))
    for name, color in LAYERS:
        a("0\nLAYER\n2\n%s\n70\n0\n62\n%d\n6\nCONTINUOUS" % (name, color))
    a("0\nENDTAB\n0\nENDSEC")
    a("0\nSECTION\n2\nENTITIES")
    for e in entities():
        if e[0] == "line":
            _, x1, y1, x2, y2 = e
            lay = "CONTORNO" if _is_outline(x1, y1, x2, y2) else "CABEZALES"
            a("0\nLINE\n8\n%s\n10\n%s\n20\n%s\n30\n0\n11\n%s\n21\n%s\n31\n0"
              % (lay, _f(x1), _f(y1), _f(x2), _f(y2)))
        elif e[0] == "circle":
            _, x, y, r = e
            a("0\nCIRCLE\n8\nCABEZALES\n10\n%s\n20\n%s\n30\n0\n40\n%s"
              % (_f(x), _f(y), _f(r)))
        else:
            _, x, y, txt, h = e
            a("0\nTEXT\n8\nROTULOS\n10\n%s\n20\n%s\n30\n0\n40\n%s\n1\n%s"
              "\n72\n1\n73\n2\n11\n%s\n21\n%s\n31\n0"
              % (_f(x), _f(y), _f(h), txt, _f(x), _f(y)))
    a("0\nENDSEC\n0\nEOF")
    with open(path, "w", encoding="ascii", newline="\r\n") as fh:
        fh.write("\n".join(l) + "\n")


def _is_outline(x1, y1, x2, y2):
    on_x = (x1 in (0.0, MOD_L) and x2 in (0.0, MOD_L))
    on_y = (y1 in (0.0, MOD_W) and y2 in (0.0, MOD_W))
    return on_x or on_y


# ----------------------------------------------------------------------------
# SVG 1:1 (mm)
# ----------------------------------------------------------------------------
def write_svg(path):
    def Y(y):
        return MOD_W - y

    s = []
    a = s.append
    a('<?xml version="1.0" encoding="UTF-8"?>')
    a('<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
      'viewBox="0 0 %s %s">' % (_f(MOD_L), _f(MOD_W), _f(MOD_L), _f(MOD_W)))
    a('  <title>Modulo PCF8574 38 x 16 mm (referencia mecanica 1:1)</title>')
    a('  <g fill="none" stroke-linecap="round">')
    for e in entities():
        if e[0] == "line":
            _, x1, y1, x2, y2 = e
            col = "#c00000" if _is_outline(x1, y1, x2, y2) else "#0060c0"
            a('    <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
              'stroke-width="0.15"/>' % (_f(x1), _f(Y(y1)), _f(x2), _f(Y(y2)), col))
        elif e[0] == "circle":
            _, x, y, r = e
            a('    <circle cx="%s" cy="%s" r="%s" stroke="#0060c0" '
              'stroke-width="0.15"/>' % (_f(x), _f(Y(y)), _f(r)))
    a('  </g>')
    for (txt, x, y, h) in TEXTS:
        a('  <text x="%s" y="%s" font-family="monospace" font-size="%s" '
          'text-anchor="middle" dominant-baseline="middle" fill="#008000">%s</text>'
          % (_f(x), _f(Y(y)), _f(h), txt))
    a('</svg>')
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(s) + "\n")


# ----------------------------------------------------------------------------
# Insercion en Dwgs.User del .kicad_pcb
# ----------------------------------------------------------------------------
SEED = "https://pcf8574-ref-mecanica-38x16/"


def _uid(tag):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, SEED + tag))


def kicad_blocks(ox, oy):
    """Primitivas KiCad para un modulo con esquina sup-izq en (ox, oy) mm.

    En el editor de PCB el eje Y crece hacia abajo, por eso se invierte:
        y_kicad = oy + (MOD_W - y_local)
    """
    def P(x, y):
        return ox + x, oy + (MOD_W - y)

    blk = []
    n = 0
    for e in entities():
        tag = "%s-%d" % (("%.2f_%.2f" % (ox, oy)).replace(".", "p"), n)
        n += 1
        if e[0] == "line":
            _, x1, y1, x2, y2 = e
            X1, Y1 = P(x1, y1)
            X2, Y2 = P(x2, y2)
            blk.append(
                "\t(gr_line\n\t\t(start %s %s)\n\t\t(end %s %s)\n"
                "\t\t(stroke\n\t\t\t(width 0.15)\n\t\t\t(type solid)\n\t\t)\n"
                "\t\t(layer \"Dwgs.User\")\n\t\t(uuid \"%s\")\n\t)"
                % (_f(X1), _f(Y1), _f(X2), _f(Y2), _uid(tag)))
        elif e[0] == "circle":
            _, x, y, r = e
            X, Y = P(x, y)
            Xe, Ye = P(x + r, y)
            blk.append(
                "\t(gr_circle\n\t\t(center %s %s)\n\t\t(end %s %s)\n"
                "\t\t(stroke\n\t\t\t(width 0.15)\n\t\t\t(type solid)\n\t\t)\n"
                "\t\t(fill no)\n\t\t(layer \"Dwgs.User\")\n\t\t(uuid \"%s\")\n\t)"
                % (_f(X), _f(Y), _f(Xe), _f(Ye), _uid(tag)))
        else:
            _, x, y, txt, h = e
            X, Y = P(x, y)
            blk.append(
                "\t(gr_text \"%s\"\n\t\t(at %s %s)\n\t\t(layer \"Dwgs.User\")\n"
                "\t\t(uuid \"%s\")\n\t\t(effects\n\t\t\t(font\n"
                "\t\t\t\t(size %s %s)\n\t\t\t\t(thickness %s)\n\t\t\t)\n\t\t)\n\t)"
                % (txt, _f(X), _f(Y), _uid(tag), _f(h), _f(h), _f(round(h / 6.0, 3))))
    return blk


# Los dos modulos se colocan en zona libre, justo debajo del contorno de la PCB
# (Edge.Cuts = 188.6..288.6 x 115..215 mm).  El usuario los arrastra luego.
INSTANCES = [(188.6, 222.0), (234.0, 222.0)]


def all_blocks():
    out = []
    for (ox, oy) in INSTANCES:
        out += kicad_blocks(ox, oy)
    return out


def _our_uuids():
    ids = set()
    for (ox, oy) in INSTANCES:
        for b in kicad_blocks(ox, oy):
            m = re.search(r'uuid "([0-9a-f-]+)"', b)
            ids.add(m.group(1))
    return ids


def _block_end(text, start):
    """Devuelve el indice posterior al ')' que cierra el bloque que abre en `start`."""
    depth = 0
    j = start
    while j < len(text):
        c = text[j]
        if c == '"':
            j += 1
            while j < len(text) and text[j] != '"':
                if text[j] == "\\":
                    j += 1
                j += 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    raise SystemExit("Bloque sin cerrar en el kicad_pcb")


def _strip_blocks(text, ids):
    """Elimina los bloques top-level que contienen alguno de los UUID dados."""
    removed = 0
    for i in ids:
        marker = '"%s"' % i
        while True:
            k = text.find(marker)
            if k < 0:
                break
            s = text.rfind("\n\t(", 0, k)
            if s < 0:
                raise SystemExit("No se pudo delimitar el bloque %s" % i)
            e = _block_end(text, s + 1)
            text = text[:s] + text[e:]
            removed += 1
    return text, removed


def insert(pcb_path, dry_run=False):
    with open(pcb_path, encoding="utf-8", newline="") as fh:
        raw = fh.read()
    crlf = "\r\n" in raw
    text = raw.replace("\r\n", "\n")

    text, removed = _strip_blocks(text, _our_uuids())
    # se insertan antes de (embedded_fonts ...) para respetar el orden canonico
    pos = text.find("\n\t(embedded_fonts")
    if pos < 0:
        pos = text.rfind("\n)")
    if pos < 0:
        raise SystemExit("No se encontro el cierre del archivo kicad_pcb")
    if not text.rstrip().endswith(")"):
        raise SystemExit("Formato inesperado del kicad_pcb")

    head, tail = text[:pos], text[pos:]
    new = "\n".join(all_blocks()) + "\n"
    out = head + "\n" + new + tail.lstrip("\n")

    if dry_run:
        print(new)
        return
    bak = pcb_path + ".bak_pcf8574"
    if not os.path.exists(bak):
        shutil.copy2(pcb_path, bak)
    with open(pcb_path, "w", encoding="utf-8", newline="\r\n" if crlf else "\n") as fh:
        fh.write(out)
    print("Insertadas %d primitivas en Dwgs.User (%d bloques previos reemplazados, "
          "saltos de linea %s)" % (len(all_blocks()), removed, "CRLF" if crlf else "LF"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--insert", metavar="PCB.kicad_pcb")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    dxf = os.path.join(here, "PCF8574_modulo_38x16.dxf")
    svg = os.path.join(here, "PCF8574_modulo_38x16.svg")
    write_dxf(dxf)
    write_svg(svg)
    print("Generado: %s" % dxf)
    print("Generado: %s" % svg)
    if args.insert:
        insert(args.insert, args.dry_run)


if __name__ == "__main__":
    main()
