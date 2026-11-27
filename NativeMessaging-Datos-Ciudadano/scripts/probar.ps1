# ============================================================================
#  probar.ps1 - Prueba DNIeDatos.exe sin navegador ni extension
# ============================================================================
#  Hace lo mismo que el navegador: arranca el .exe, le escribe una peticion
#  por la entrada estandar (4 bytes de longitud + JSON en UTF-8) y lee la
#  respuesta de la salida estandar, con el mismo formato.
#
#  Uso (desde la carpeta del proyecto, en cmd):
#    powershell -ExecutionPolicy Bypass -File scripts\probar.ps1
#    powershell -ExecutionPolicy Bypass -File scripts\probar.ps1 "{\"operacion\":\"informacion\"}"
#    powershell -ExecutionPolicy Bypass -File scripts\probar.ps1 "{\"operacion\":\"leer\",\"origen\":\"prueba\",\"que\":[\"datos\"]}"
#
#  Sirve para comprobar el programa por separado: si aqui funciona y en el
#  navegador no, el problema esta en el registro, el manifiesto o la
#  extension.
#
#  OJO: las peticiones que pasan por aqui NO pasan por la extension, asi que
#  no se comprueba el origen ni los parametros: el "origen" lo pone usted.
# ============================================================================
param(
    [string]$Peticion = '{"operacion":"version"}',
    [string]$Plataforma = ""
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path $PSScriptRoot -Parent

if (-not $Plataforma) {
    $arq = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    $Plataforma = switch ($arq) { "AMD64" { "x64" } "ARM64" { "ARM64" } default { "Win32" } }
}
$exe = Join-Path $raiz "bin\$Plataforma\Release\DNIeDatos.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: no existe $exe. Ejecute antes scripts\compilar.cmd" -ForegroundColor Red
    exit 1
}

# Windows PowerShell crea la entrada del programa con la codificacion de la
# consola; si es UTF-8 con "marca" (BOM), la escribe al principio y el
# programa recibiria 3 bytes de mas antes de la longitud. Se usa UTF-8 sin
# marca mientras se arranca el programa.
$codificacionAnterior = $null
try {
    $codificacionAnterior = [Console]::InputEncoding
    [Console]::InputEncoding = New-Object System.Text.UTF8Encoding $false
} catch { }

$inicio = New-Object System.Diagnostics.ProcessStartInfo
$inicio.FileName = $exe
$inicio.UseShellExecute = $false
$inicio.RedirectStandardInput = $true
$inicio.RedirectStandardOutput = $true
$proceso = [System.Diagnostics.Process]::Start($inicio)
if ($codificacionAnterior) { try { [Console]::InputEncoding = $codificacionAnterior } catch { } }

# Peticion: longitud (4 bytes, little-endian) + JSON en UTF-8
$json = [Text.Encoding]::UTF8.GetBytes($Peticion)
$entrada = $proceso.StandardInput.BaseStream
$entrada.Write([BitConverter]::GetBytes([uint32]$json.Length), 0, 4)
$entrada.Write($json, 0, $json.Length)
$entrada.Flush()

# Respuesta: igual. Se lee hasta tener los bytes anunciados.
function LeerExacto($flujo, [int]$n) {
    $b = New-Object byte[] $n
    $leidos = 0
    while ($leidos -lt $n) {
        $r = $flujo.Read($b, $leidos, $n - $leidos)
        if ($r -le 0) { throw "El programa ha terminado sin responder." }
        $leidos += $r
    }
    return ,$b
}
$salida = $proceso.StandardOutput.BaseStream
$longitud = [BitConverter]::ToUInt32((LeerExacto $salida 4), 0)
$respuesta = [Text.Encoding]::UTF8.GetString((LeerExacto $salida ([int]$longitud)))

# Cerrar la entrada es lo que hace el navegador al terminar: el programa sale.
$entrada.Close()
$proceso.WaitForExit(5000) | Out-Null

# Respuesta con sangria, para leerla mejor. Los campos largos en Base64
# (imagenes, certificados) se acortan.
$objeto = $respuesta | ConvertFrom-Json
$texto = $objeto | ConvertTo-Json -Depth 6
$texto -replace '"([A-Za-z0-9+/=]{120})[A-Za-z0-9+/=]+"', '"$1..."'
