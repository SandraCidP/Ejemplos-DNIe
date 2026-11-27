# ============================================================================
#  probar-anfitrion.ps1  -  Habla con DNIeAnfitrion.exe sin navegador
# ============================================================================
#  Hace lo mismo que el navegador: arranca el programa, le envía una petición
#  por su entrada estándar y muestra la respuesta que llega por su salida
#  estándar. Sirve para dos cosas:
#
#    - Ver el formato de los mensajes funcionando (4 bytes de longitud y
#      después el JSON; está explicado en Main.cpp).
#    - Saber, si algo falla, si el problema está en el programa o en el
#      navegador y la extensión.
#
#  Uso:
#    powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1
#    powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1 firmar
#    powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1 version ARM64
#
#  Primer argumento: la operación ("version", "firmar" o "verificar"). Sin
#  argumentos se pide "version", que no muestra diálogos ni necesita PIN.
#  Segundo argumento: la plataforma del .exe (por defecto, x64).
# ============================================================================
param(
    [string]$Operacion = "version",
    [string]$Plataforma = "x64"
)

# Que los acentos de la respuesta se vean bien en la consola.
[Console]::OutputEncoding = [Text.Encoding]::UTF8

# Imprescindible si la consola está en UTF-8 (chcp 65001): en ese caso,
# Windows PowerShell escribe por su cuenta 3 bytes de "marca" (BOM) en la
# entrada del programa nada más arrancarlo. El programa los tomaría por el
# principio de la longitud del mensaje, la vería disparatada y terminaría
# sin responder. Con un UTF-8 "sin marca" ($false) no se escribe nada.
[Console]::InputEncoding = New-Object Text.UTF8Encoding $false

$programa = Join-Path $PSScriptRoot "..\bin\$Plataforma\Release\DNIeAnfitrion.exe"
if (-not (Test-Path -LiteralPath $programa)) {
    Write-Host "ERROR: no existe $programa. Ejecute antes scripts\compilar.cmd"
    exit 1
}

# Arrancamos el programa quedándonos con su entrada y su salida estándar.
$arranque = New-Object System.Diagnostics.ProcessStartInfo
$arranque.FileName = $programa
$arranque.UseShellExecute = $false
$arranque.RedirectStandardInput = $true
$arranque.RedirectStandardOutput = $true
$proceso = [Diagnostics.Process]::Start($arranque)

# "BaseStream" da acceso a los bytes tal cual, sin conversiones de texto.
$entrada = $proceso.StandardInput.BaseStream
$salida = $proceso.StandardOutput.BaseStream

# Lee exactamente $cantidad bytes de la salida del programa.
function Leer-Bytes([int]$cantidad) {
    $bytes = New-Object byte[] $cantidad
    $total = 0
    while ($total -lt $cantidad) {
        $leidos = $salida.Read($bytes, $total, $cantidad - $total)
        if ($leidos -le 0) { throw "El programa ha terminado sin responder." }
        $total += $leidos
    }
    # La coma evita que PowerShell "desmonte" la matriz al devolverla.
    return ,$bytes
}

# --- Petición: 4 bytes con la longitud y después el JSON en UTF-8 ----------
$peticion = '{"operacion":"' + $Operacion + '"}'
$json = [Text.Encoding]::UTF8.GetBytes($peticion)
$entrada.Write([BitConverter]::GetBytes([int]$json.Length), 0, 4)
$entrada.Write($json, 0, $json.Length)
$entrada.Flush()
Write-Host "Petición:  $peticion"

# --- Respuesta: mismo formato ----------------------------------------------
$longitud = [BitConverter]::ToInt32((Leer-Bytes 4), 0)
$respuesta = [Text.Encoding]::UTF8.GetString((Leer-Bytes $longitud))
Write-Host "Respuesta: $respuesta"

# Al cerrar su entrada, el programa entiende que no hay más peticiones y
# termina.
$entrada.Close()
$proceso.WaitForExit()
