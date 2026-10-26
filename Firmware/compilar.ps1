<#
.SYNOPSIS
    Compila (y opcionalmente flashea) el firmware de la maquina expendedora.

.DESCRIPTION
    ESP-IDF no tolera bien las rutas con acentos ni espacios largos: ccache y
    kconfiglib fallan con "Illegal byte sequence" / "Could not open sdkconfig".
    Por eso este script sincroniza Firmware/ a una ruta ASCII corta
    (C:\Users\asus\esp\maquina_expendedora) y compila alli, dejando el build
    fuera de OneDrive.

.EXAMPLE
    .\compilar.ps1
    .\compilar.ps1 -Clean
    .\compilar.ps1 -Flash -Port COM3
    .\compilar.ps1 -Flash -Port COM3 -Monitor
#>
param(
    [string]$IdfPath      = "C:\Users\asus\esp\esp-idf",
    [string]$IdfToolsPath = "C:\Users\asus\esp\espressif",
    [string]$PythonPath   = "C:\Users\asus\AppData\Local\Programs\Python\Python312",
    [string]$WorkDir      = "C:\Users\asus\esp\maquina_expendedora",
    [string]$BuildDir     = "C:\Users\asus\esp\build_maquina",
    [switch]$Clean,
    [switch]$Flash,
    [string]$Port = "",
    [switch]$Monitor
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $IdfPath))      { throw "No se encontro ESP-IDF en $IdfPath" }
if (-not (Test-Path $IdfToolsPath)) { throw "No se encontro IDF_TOOLS_PATH en $IdfToolsPath" }
if (-not (Test-Path "$PythonPath\python.exe")) { throw "No se encontro Python en $PythonPath" }

# 1) Sincronizar el codigo fuente a la ruta ASCII.
#    managed_components/ y dependencies.lock se conservan: son generados por el
#    IDF Component Manager y re-descargarlos en cada build es lento (LVGL pesa
#    ~26 MB). sdkconfig NO se conserva, para que sdkconfig.defaults se aplique.
Write-Host "Sincronizando fuentes a $WorkDir ..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force $WorkDir | Out-Null
robocopy $PSScriptRoot $WorkDir /MIR /XD build .git managed_components /XF dependencies.lock /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy fallo con codigo $LASTEXITCODE" }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Limpiando $BuildDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# 2) Preparar el entorno de ESP-IDF (Python 3.12 primero en el PATH).
$env:IDF_TOOLS_PATH = $IdfToolsPath
$env:Path = "$PythonPath;$PythonPath\Scripts;" + $env:Path

$idfArgs = "idf.py -B `"$BuildDir`""
if ($Port) { $idfArgs += " -p $Port" }
$idfArgs += " build"
if ($Flash)   { $idfArgs += " flash" }
if ($Monitor) { $idfArgs += " monitor" }

$cmd = "cd /d `"$IdfPath`" && export.bat >nul 2>&1 && cd /d `"$WorkDir`" && $idfArgs"
Write-Host "Ejecutando: $idfArgs" -ForegroundColor Cyan

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "idf.py fallo con codigo $LASTEXITCODE" }

if (-not $Flash) {
    Write-Host ""
    Write-Host "Build OK: $BuildDir\maquina_expendedora.bin" -ForegroundColor Green
    Write-Host "Para flashear: .\compilar.ps1 -Flash -Port COMx" -ForegroundColor Green
}
