# Respaldo de la flash — placa ESP32 (COM5)

> **Fecha:** 2026-11-21
> **Placa:** ESP32-D0WD-V3 (revisión v3.1), 4 MB de flash
> **MAC:** `04:b2:47:88:f2:30`
> **Puerto usado:** COM5 (USB-Serial CH340)
> **Motivo:** la placa se va a reutilizar con otro proyecto; este respaldo conserva el
> firmware y los datos que solo existían en la placa.

---

## Archivos

| Archivo | Tamaño | Contenido |
| :--- | ---: | :--- |
| `flash_esp32_com5_2026-11-21_full4mb.bin` | 4 194 304 B | Imagen **completa** de la flash (bootloader + tabla + app + NVS + calibración RF) |
| `nvs_2026-11-21.bin` | 24 576 B | Solo la partición **NVS** (precios, stock, saldo, contraseña de administración) |

**SHA-256 de la imagen completa:**

```
F3233D01144D06160F9E148618990282BD5E7BDB82DD1FB7DBC953C0B1B863E4
```

Verificación rápida:

```powershell
Get-FileHash .\flash_esp32_com5_2026-11-21_full4mb.bin -Algorithm SHA256
```

---

## Mapa de particiones del respaldo

| Partición | Tipo | Offset | Tamaño |
| :--- | :--- | :--- | :--- |
| `nvs` | datos | `0x009000` | 24 KB |
| `phy_init` | datos | `0x00F000` | 4 KB |
| `factory` | app | `0x010000` | 1 MB |

---

## Cómo restaurar

### Todo (misma placa)

```powershell
& "C:\Users\asus\esp\espressif\python_env\idf5.5_py3.12_env\Scripts\esptool.exe" `
    --port COM5 write_flash 0x0 .\flash_esp32_com5_2026-11-21_full4mb.bin
```

### Solo los datos (NVS) sobre un firmware recién flasheado

```powershell
& "C:\Users\asus\esp\espressif\python_env\idf5.5_py3.12_env\Scripts\esptool.exe" `
    --port COM5 write_flash 0x9000 .\nvs_2026-11-21.bin
```

### Solo el firmware desde el código fuente

```powershell
cd "..\Firmware"
.\compilar.ps1 -Flash -Port COM5
```

---

## Notas

- Restaurar la imagen completa en **esta misma placa** deja el equipo exactamente como estaba.
- En **otra placa**, flashea solo el firmware desde el código fuente y usa una NVS limpia:
  los datos de calibración de RF (`phy_init`) son propios de cada chip.
- Este directorio **no está en Git** (son binarios de 4 MB). El código fuente sí está, en:
  `https://github.com/yeremybautista77/Maquina_Expendedora` — commit `8cfe96f`.
