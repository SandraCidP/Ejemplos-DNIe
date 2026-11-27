# probar_datos.ps1
# Prueba de la lectura de datos del ActiveX (datos personales, MRZ, foto y firma manuscrita) SIN mostrar datos:
# solo el resultado de cada lectura, cuántos caracteres o bytes devuelve y el formato de las imágenes.
#
#   .\probar_datos.ps1                 # sin PIN (DNIe 4.0)
#   .\probar_datos.ps1 -Pin            # presentando antes el PIN (necesario con el DNIe 3.0)
#
# Ejecutar en una ventana de PowerShell nueva y con la tarjeta recién insertada.
param([string]$ProgID = 'FNMT.ActiveXDNIe', [switch]$Pin)
$ErrorActionPreference = 'Stop'

function Bytes($s) { if ($null -eq $s) { return ,[byte[]]@() }; ,[byte[]]($s.ToCharArray() | ForEach-Object { [byte]([int]$_ -band 0xFF) }) }
function Formato([byte[]]$b) {
    if ($b.Length -ge 12 -and $b[4] -eq 0x6A -and $b[5] -eq 0x50 -and $b[6] -eq 0x20) { return 'JPEG 2000 (JP2)' }
    if ($b.Length -ge 4 -and $b[0] -eq 0xFF -and $b[1] -eq 0x4F) { return 'JPEG 2000 (J2K)' }
    if ($b.Length -ge 3 -and $b[0] -eq 0xFF -and $b[1] -eq 0xD8) { return 'JPEG' }
    return 'no reconocido'
}

$ax = New-Object -ComObject $ProgID
$ax.InicializarConexion()
Write-Host ("InicializarConexion                lastresultOut = {0}" -f $ax.lastresultOut)
if ($ax.lastresultOut -ne 0) { return }
if ($Pin) {
    $s = Read-Host 'PIN del DNIe' -AsSecureString
    $ax.PinIn = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
    $ax.PresentarPin()
    Write-Host ("PresentarPin                       lastresultOut = {0}" -f $ax.lastresultOut)
    if ($ax.lastresultOut -ne 0) { return }
}

$t = [Diagnostics.Stopwatch]::StartNew()
foreach ($p in 'NombreOut', 'Apellido1Out', 'Apellido2Out', 'NumeroDNIOut', 'FechaNacimientoOut', 'NacionalidadOut',
               'SexoOut', 'FechaExpiracionOut', 'LocalidadNacimientoOut', 'ProvinciaNacimientoOut', 'NombrePadresOut',
               'DomicilioOut', 'LocalidadDomicilioOut', 'ProvinciaDomicilioOut') {
    $v = [string]$ax.$p
    Write-Host ("{0,-34} lastresultOut = {1,-4} {2,3} caracteres" -f $p, $ax.lastresultOut, $v.Length)
    $v = $null
}
$ax.ObtenerNumeroSerie_Soporte()
Write-Host ("{0,-34} lastresultOut = {1,-4} {2,3} caracteres" -f 'ObtenerNumeroSerie_Soporte', $ax.lastresultOut, ([string]$ax.DatosOut).Length)
$ax.ObtenerMRZ()
Write-Host ("{0,-34} lastresultOut = {1,-4} {2,3} caracteres" -f 'ObtenerMRZ', $ax.lastresultOut, ([string]$ax.DatosOut).Length)
foreach ($p in 'ImagenFacialOut', 'ImagenFirmaOut') {
    $b = Bytes $ax.$p
    Write-Host ("{0,-34} lastresultOut = {1,-4} {2,6} bytes  {3}" -f $p, $ax.lastresultOut, $b.Length, (Formato $b))
    $b = $null
}
Write-Host ("Tiempo total: {0:N1} s" -f $t.Elapsed.TotalSeconds)
$ax.FinalizarConexion()
