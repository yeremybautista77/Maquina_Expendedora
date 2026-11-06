<#
.SYNOPSIS
    Convierte las imagenes de producto a arreglos C embebidos en flash.

.DESCRIPTION
    Lee p1..p6 (bmp/png/jpg/jpeg) de Firmware\imagenes_productos y genera
    Firmware\main\product_images.c y product_images.h con un lv_img_dsc_t por
    producto.

    - Ajusta cada imagen a un cuadro cuadrado de -Size px conservando la
      proporcion (rellena con -Background).
    - Las salida queda en RGB565 con los bytes intercambiados, que es el
      formato que espera LVGL con CONFIG_LV_COLOR_16_SWAP=y (el mismo que
      produce el decodificador BMP de LVGL en esta placa).
    - Si falta pN, genera un marcador de posicion con el numero de producto,
      de modo que el firmware compila y arranca igual.

    NO editar product_images.c a mano: se sobrescribe al ejecutar el script.

.EXAMPLE
    .\generar_imagenes.ps1
    .\generar_imagenes.ps1 -Size 96
#>
[CmdletBinding()]
param(
    [string]$SourceDir  = "",
    [string]$OutDir     = "",
    [int]$Size          = 52,
    [string]$Background = "#D5DBDB"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrEmpty($SourceDir)) {
    $SourceDir = Join-Path $ScriptDir "..\imagenes_productos"
}
if ([string]::IsNullOrEmpty($OutDir)) {
    $OutDir = Join-Path $ScriptDir "..\main"
}

Add-Type -AssemblyName System.Drawing

$ProductCount = 6

# Colores de los marcadores de posicion (uno por producto).
$PlaceholderColors = @(
    "#E74C3C", "#3498DB", "#2ECC71",
    "#F1C40F", "#9B59B6", "#1ABC9C"
)

function Convert-HexColor([string]$hex) {
    $h = $hex.TrimStart('#')
    return [System.Drawing.Color]::FromArgb(
        255,
        [Convert]::ToInt32($h.Substring(0, 2), 16),
        [Convert]::ToInt32($h.Substring(2, 2), 16),
        [Convert]::ToInt32($h.Substring(4, 2), 16))
}

function New-SquareCanvas {
    param(
        [System.Drawing.Image]$Image,
        [int]$Side,
        [System.Drawing.Color]$Fill
    )

    $bmp = New-Object System.Drawing.Bitmap($Side, $Side,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.Clear($Fill)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

        $scale = [Math]::Min($Side / $Image.Width, $Side / $Image.Height)
        $w = [Math]::Max(1, [int][Math]::Round($Image.Width * $scale))
        $h = [Math]::Max(1, [int][Math]::Round($Image.Height * $scale))
        $x = [int](($Side - $w) / 2)
        $y = [int](($Side - $h) / 2)

        $g.DrawImage($Image, $x, $y, $w, $h)
    }
    finally {
        $g.Dispose()
    }
    return $bmp
}

function New-Placeholder {
    param(
        [int]$Side,
        [string]$Label,
        [System.Drawing.Color]$Fill
    )

    $bmp = New-Object System.Drawing.Bitmap($Side, $Side,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $font = $null
    $brush = $null
    $fmt = $null
    try {
        $g.Clear($Fill)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

        $emSize = [Math]::Max(10, [int]($Side * 0.42))
        $font = New-Object System.Drawing.Font("Arial", $emSize,
            [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $fmt = New-Object System.Drawing.StringFormat
        $fmt.Alignment = [System.Drawing.StringAlignment]::Center
        $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center

        $rect = New-Object System.Drawing.RectangleF(0, 0, $Side, $Side)
        $g.DrawString($Label, $font, $brush, $rect, $fmt)
    }
    finally {
        if ($fmt) { $fmt.Dispose() }
        if ($brush) { $brush.Dispose() }
        if ($font) { $font.Dispose() }
        $g.Dispose()
    }
    return $bmp
}

function ConvertTo-CArray {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [string]$Name
    )

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t ${Name}_map[] = {")

    $width = $Bitmap.Width
    $height = $Bitmap.Height
    $line = New-Object System.Text.StringBuilder
    $count = 0

    for ($y = 0; $y -lt $height; $y++) {
        for ($x = 0; $x -lt $width; $x++) {
            $c = $Bitmap.GetPixel($x, $y)
            $r5 = ($c.R -shr 3) -band 0x1F
            $g6 = ($c.G -shr 2) -band 0x3F
            $b5 = ($c.B -shr 3) -band 0x1F
            $v = ($r5 -shl 11) -bor ($g6 -shl 5) -bor $b5

            # LV_COLOR_16_SWAP = 1 -> byte alto primero.
            $hi = ($v -shr 8) -band 0xFF
            $lo = $v -band 0xFF

            [void]$line.Append(("0x{0:x2},0x{1:x2}," -f $hi, $lo))
            $count += 2

            if (($count % 16) -eq 0) {
                [void]$sb.AppendLine("    " + $line.ToString())
                $line.Clear() | Out-Null
            }
        }
    }

    if ($line.Length -gt 0) {
        [void]$sb.AppendLine("    " + $line.ToString())
    }
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine()
    return $sb.ToString()
}

# --- Validacion de rutas -----------------------------------------------------

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

if (-not (Test-Path $SourceDir)) {
    New-Item -ItemType Directory -Force -Path $SourceDir | Out-Null
}
if (-not (Test-Path $OutDir)) {
    throw "No existe el directorio de salida: $OutDir"
}

$bg = Convert-HexColor $Background

# --- Generacion --------------------------------------------------------------

$c = New-Object System.Text.StringBuilder
[void]$c.AppendLine("/**")
[void]$c.AppendLine(" * @file product_images.c")
[void]$c.AppendLine(" * @brief Imagenes de los 6 productos embebidas en flash.")
[void]$c.AppendLine(" *")
[void]$c.AppendLine(" * ARCHIVO GENERADO: no editar a mano. Ejecuta")
[void]$c.AppendLine(" * herramientas/generar_imagenes.ps1 para regenerarlo.")
[void]$c.AppendLine(" *")
[void]$c.AppendLine(" * Formato: RGB565 con bytes intercambiados (LV_COLOR_16_SWAP = 1),")
[void]$c.AppendLine(" * el mismo que produce el decodificador BMP de LVGL en esta placa.")
[void]$c.AppendLine(" *")
[void]$c.AppendLine(" * SPDX-License-Identifier: MIT")
[void]$c.AppendLine(" */")
[void]$c.AppendLine()
[void]$c.AppendLine('#include "product_images.h"')
[void]$c.AppendLine()
[void]$c.AppendLine('#include "lvgl.h"')
[void]$c.AppendLine()
[void]$c.AppendLine("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
[void]$c.AppendLine("#define LV_ATTRIBUTE_MEM_ALIGN")
[void]$c.AppendLine("#endif")
[void]$c.AppendLine("#ifndef LV_ATTRIBUTE_LARGE_CONST")
[void]$c.AppendLine("#define LV_ATTRIBUTE_LARGE_CONST")
[void]$c.AppendLine("#endif")
[void]$c.AppendLine()

$summary = @()

for ($i = 1; $i -le $ProductCount; $i++) {
    $source = Get-ChildItem -Path $SourceDir -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.BaseName -eq "p$i" -and
            $_.Extension -match '^\.(bmp|png|jpg|jpeg)$'
        } |
        Sort-Object Extension |
        Select-Object -First 1

    $image = $null
    $canvas = $null
    $label = ""

    if ($null -ne $source) {
        try {
            $image = [System.Drawing.Image]::FromFile($source.FullName)
            $canvas = New-SquareCanvas -Image $image -Side $Size -Fill $bg
            $label = $source.Name
        }
        finally {
            if ($image) { $image.Dispose() }
        }
    }
    else {
        $color = Convert-HexColor $PlaceholderColors[($i - 1) % $PlaceholderColors.Count]
        $canvas = New-Placeholder -Side $Size -Label "P$i" -Fill $color
        $label = "(sin archivo: marcador de posicion)"
    }

    try {
        [void]$c.Append((ConvertTo-CArray -Bitmap $canvas -Name "product_img_$i"))

        [void]$c.AppendLine("const lv_img_dsc_t product_img_$i = {")
        [void]$c.AppendLine("    .header.cf = LV_IMG_CF_TRUE_COLOR,")
        [void]$c.AppendLine("    .header.always_zero = 0,")
        [void]$c.AppendLine("    .header.reserved = 0,")
        [void]$c.AppendLine("    .header.w = $Size,")
        [void]$c.AppendLine("    .header.h = $Size,")
        [void]$c.AppendLine("    .data_size = $Size * $Size * 2,")
        [void]$c.AppendLine("    .data = product_img_${i}_map,")
        [void]$c.AppendLine("};")
        [void]$c.AppendLine()
    }
    finally {
        if ($canvas) { $canvas.Dispose() }
    }

    $summary += [pscustomobject]@{ Producto = $i; Archivo = $label }
}

[void]$c.AppendLine("static const lv_img_dsc_t *const s_images[PRODUCT_IMAGE_COUNT] = {")
for ($i = 1; $i -le $ProductCount; $i++) {
    [void]$c.AppendLine("    &product_img_$i,")
}
[void]$c.AppendLine("};")
[void]$c.AppendLine()
[void]$c.AppendLine("const lv_img_dsc_t *product_images_get(uint8_t index)")
[void]$c.AppendLine("{")
[void]$c.AppendLine("    if (index >= PRODUCT_IMAGE_COUNT) {")
[void]$c.AppendLine("        return NULL;")
[void]$c.AppendLine("    }")
[void]$c.AppendLine("    return s_images[index];")
[void]$c.AppendLine("}")

$cPath = Join-Path $OutDir "product_images.c"
[System.IO.File]::WriteAllText($cPath, $c.ToString(),
    (New-Object System.Text.UTF8Encoding($false)))

# --- Encabezado --------------------------------------------------------------

$h = New-Object System.Text.StringBuilder
[void]$h.AppendLine("/**")
[void]$h.AppendLine(" * @file product_images.h")
[void]$h.AppendLine(" * @brief Imagenes de los 6 productos embebidas en flash.")
[void]$h.AppendLine(" *")
[void]$h.AppendLine(" * ARCHIVO GENERADO: no editar a mano. Ejecuta")
[void]$h.AppendLine(" * herramientas/generar_imagenes.ps1 para regenerarlo.")
[void]$h.AppendLine(" *")
[void]$h.AppendLine(" * SPDX-License-Identifier: MIT")
[void]$h.AppendLine(" */")
[void]$h.AppendLine()
[void]$h.AppendLine("#pragma once")
[void]$h.AppendLine()
[void]$h.AppendLine("#include <stddef.h>")
[void]$h.AppendLine("#include <stdint.h>")
[void]$h.AppendLine()
[void]$h.AppendLine('#include "lvgl.h"')
[void]$h.AppendLine()
[void]$h.AppendLine("#ifdef __cplusplus")
[void]$h.AppendLine('extern "C" {')
[void]$h.AppendLine("#endif")
[void]$h.AppendLine()
[void]$h.AppendLine("/** Cantidad de imagenes generadas (una por canal). */")
[void]$h.AppendLine("#define PRODUCT_IMAGE_COUNT     $ProductCount")
[void]$h.AppendLine()
[void]$h.AppendLine("/** Lado en pixeles de cada imagen generada. */")
[void]$h.AppendLine("#define PRODUCT_IMAGE_SIZE      $Size")
[void]$h.AppendLine()
[void]$h.AppendLine("/**")
[void]$h.AppendLine(" * @brief Devuelve la imagen del producto indicado.")
[void]$h.AppendLine(" *")
[void]$h.AppendLine(" * @param index Canal 0..PRODUCT_IMAGE_COUNT-1.")
[void]$h.AppendLine(" * @return Descriptor LVGL en flash, o NULL si el indice no es valido.")
[void]$h.AppendLine(" */")
[void]$h.AppendLine("const lv_img_dsc_t *product_images_get(uint8_t index);")
[void]$h.AppendLine()
[void]$h.AppendLine("#ifdef __cplusplus")
[void]$h.AppendLine("}")
[void]$h.AppendLine("#endif")

$hPath = Join-Path $OutDir "product_images.h"
[System.IO.File]::WriteAllText($hPath, $h.ToString(),
    (New-Object System.Text.UTF8Encoding($false)))

# --- Resumen -----------------------------------------------------------------

Write-Output ""
Write-Output "Imagenes generadas ($Size x $Size, RGB565):"
$summary | Format-Table -AutoSize
Write-Output "  $cPath"
Write-Output "  $hPath"
Write-Output ""
