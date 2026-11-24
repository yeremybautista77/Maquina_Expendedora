# Diseño mecánico

Piezas y ensamblajes del chasis en **SolidWorks** (`.SLDPRT` / `.SLDASM`). El registro
fotográfico del armado está en `../Documentacion/Proceso de manufactura/`.

## Estructura

```
Diseño_mecanico/
├── Chasis/       gabinete, panel de control y soportes
├── motor dc/     conjunto motor–espiral
└── README.md
```

## Chasis

| Archivo | Elemento |
| :--- | :--- |
| `Ensamblaje1.SLDASM` | Ensamblaje completo del gabinete |
| `pared principal.SLDPRT` | Pared frontal |
| `pared final.SLDPRT` | Pared posterior |
| `pared exterior1.SLDPRT` | Pared lateral exterior |
| `pared exterior 2.SLDPRT` | Segunda pared lateral exterior |
| `PARED INTERIOR.SLDPRT` | Pared interior |
| `DIVISOR INTERIOR.SLDPRT` | Divisor de los seis compartimentos |
| `pared superior e inferior.SLDPRT` | Tapa y base |
| `ultimas paredes.SLDPRT` | Piezas de cierre del gabinete |
| `fondo.SLDPRT` | Fondo |
| `PANEL DE CONTROL.SLDPRT` | Panel frontal de la interfaz |
| `pantalla.SLDPRT` | Alojamiento de la pantalla TFT de 4.0" |
| `4 Inch TFT SPI 480x320 V1.2.SLDPRT` | Modelo de referencia del módulo de pantalla |
| `Cristal.SLDPRT` | Ventana de la puerta |
| `Base de los motores.SLDPRT` | Soporte de los motores |
| `coin acceptor.SLDPRT` | Alojamiento del monedero |

## Conjunto motor–espiral

| Archivo | Elemento |
| :--- | :--- |
| `Ensamblaje de motor.SLDASM` | Ensamblaje del motor con su base y espiral |
| `motor main.SLDPRT` | Motor principal |
| `motor 2.SLDPRT` | Variante del motor |
| `espiral.SLDPRT` | Espiral de despacho |
| `eje de base del motor.SLDPRT` | Eje de acople |
| `base de resorte.SLDPRT` | Base y resorte de sujeción |

## Pendiente de organización

- `coin acceptor.SLDPRT` existe **dos veces**: en la raíz de esta carpeta y en
  `Chasis/`. Son archivos distintos (no son copias idénticas). Hay que consolidar una
  sola versión y eliminar la otra; por ahora se conservan ambos para no perder
  historial.

## Notas

- Los archivos son binarios de SolidWorks: Git guarda la versión, pero **no permite
  comparar cambios**. Conviene anotar en el mensaje de commit qué pieza se modificó.
- Las medidas oficiales son las de estos archivos. Si una foto del proceso de
  manufactura no coincide, manda el modelo.
