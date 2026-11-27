# probar_activex.ps1
# Prueba del ActiveX del DNIe contra la tarjeta insertada, sin navegador.
#
#   .\probar_activex.ps1                         # sin PIN: conexión, certificados y claves
#   .\probar_activex.ps1 -Firma                  # además: PIN, firma y verificación
#   .\probar_activex.ps1 -ProgID DLL_ADMINISTRATIVA_DNIE.DLL_ADMIN   # el ActiveX publicado, para comparar
#
# Hay que registrar antes el control (scripts\registrar.cmd) y usar un PowerShell normal, no "como
# administrador", de la misma arquitectura que la DLL (64 bits -> x64).
#
# No muestra datos personales. Con -Firma el PIN se pide oculto (o se toma de PKCS11_PIN) y la
# firma se comprueba en este equipo con el certificado de la tarjeta, sin enviarlo a ningún sitio.
param(
    [string]$ProgID = 'FNMT.ActiveXDNIe',
    [switch]$Firma
)
$ErrorActionPreference = 'Stop'

# Datos binarios en BSTR: el ActiveX nuevo usa un caracter por byte; el publicado convierte con la
# pagina de codigos ANSI (1252), asi que los bytes 0x80-0x9F llegan como otros caracteres (0x80 -> euro).
function Bytes($s) {
    if ($null -eq $s) { return ,[byte[]]@() }
    if (($s.ToCharArray() | Where-Object { [int]$_ -gt 255 }).Count -gt 0) { return ,[Text.Encoding]::GetEncoding(1252).GetBytes($s) }
    ,[byte[]]($s.ToCharArray() | ForEach-Object { [byte]([int]$_ -band 0xFF) })
}
function Hex($s) { ((Bytes $s) | ForEach-Object { '{0:X2}' -f $_ }) -join '' }
function Paso($nombre, [scriptblock]$accion) {
    try {
        & $accion
        $rv = $ax.lastresultOut
        Write-Host ("{0,-38} lastresultOut = {1} (0x{1:X})" -f $nombre, $rv)
        return ($rv -eq 0)
    } catch {
        Write-Host ("{0,-38} EXCEPCION: {1}" -f $nombre, $_.Exception.Message)
        return $false
    }
}
function Etiquetas($metodo) {
    $l = @()
    if (Paso $metodo { $ax.$metodo() }) {
        for ($i = 0; $i -lt $ax.ArrayEtiquetasLenOut; $i++) { $l += $ax.EtiquetaOut($i) }
        $l | ForEach-Object { Write-Host "   $_" }
    }
    return ,$l
}

Write-Host "PowerShell de $([IntPtr]::Size * 8) bits - ProgID $ProgID"
$ax = New-Object -ComObject $ProgID
Write-Host "Objeto ActiveX creado`n"

if (-not (Paso 'InicializarConexion' { $ax.InicializarConexion() })) { return }

if (Paso 'ObtenerVersionDNIe' { $ax.ObtenerVersionDNIe() }) { Write-Host "   version: $($ax.DatosOut)" }
if (Paso 'ObtenerTipoDocumento' { $ax.ObtenerTipoDocumento() }) { Write-Host "   tipo: $(Hex $ax.DatosOut)" }
if (Paso 'ObtenerNumeroSerie' { $ax.ObtenerNumeroSerie() }) { Write-Host "   n. serie: $($ax.ulNumeroSerieLen) bytes" }
if (Paso 'ObtenerVersionesLibrerias' { $ax.ObtenerVersionesLibrerias() }) {
    for ($i = 0; $i -lt $ax.ArrayVersionLibreriasLenOut; $i++) { Write-Host "   $($ax.NombreLibreriaOut($i)) $($ax.VersionLibreriaOut($i))" }
}
$certs = Etiquetas 'ObtenerEtiquetasTodosCertificados'
foreach ($c in $certs) {
    $ax.EtiquetaCertificadoIn = $c
    if (Paso "ObtenerCertificado($c)" { $ax.ObtenerCertificado() }) { Write-Host "   $((Bytes $ax.DatosOut).Length) bytes" }
}
$pubs  = Etiquetas 'ObtenerEtiquetasClavesPublicas'
function ClavesPublicas {
foreach ($p in $pubs) {
    $ax.EtiquetaKeyPubIn = $p
    if (Paso "ObtenerClavePublicaRSA($p)" { $ax.ObtenerClavePublicaRSA() }) { Write-Host "   RSA: modulo $((Bytes $ax.ModuloOut).Length) bytes" }
    if (Paso "ObtenerClavePublicaEC($p)" { $ax.ObtenerClavePublicaEC() }) { Write-Host "   EC: punto $((Bytes $ax.DatosOut).Length) bytes" }
}
}
ClavesPublicas

if ($Firma) {
    $pin = $env:PKCS11_PIN
    if (-not $pin) {
        $s = Read-Host 'PIN de la tarjeta' -AsSecureString
        $pin = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
    }
    $ax.PinIn = $pin
    $pin = $null
    if (Paso 'PresentarPin' { $ax.PresentarPin() }) {
        Write-Host '-- Claves publicas despues del PIN --'
        ClavesPublicas
        $privs = Etiquetas 'ObtenerEtiquetasClavesPrivadas'
        # Se usa la clave de AUTENTICACION para no pedir la confirmación de firma
        $priv = $privs | Where-Object { $_ -match 'Autentica' } | Select-Object -First 1
        $pub  = $pubs  | Where-Object { $_ -match 'Autentica' } | Select-Object -First 1
        $cert = $certs | Where-Object { $_ -match 'Autentica' } | Select-Object -First 1
        $datos = 'Prueba de firma del ActiveX DNIe'
        $ax.EtiquetaKeyPrivIn = $priv
        $ax.DatosIn = $datos
        if (Paso "Firmar($priv)" { $ax.Firmar() }) {
            $f = [string]$ax.FirmaOut
            $esHex = $f -match '^[0-9A-Fa-f]+$' -and ($f.Length % 2 -eq 0) -and ($f.Length -ge 128)
            Write-Host ("   FirmaOut: {0} caracteres, {1}" -f $f.Length, $(if ($esHex) { 'hexadecimal' } else { 'en bruto (un caracter por byte)' }))
            $fb = if ($esHex) { [byte[]]($f -split '(..)' | Where-Object { $_ } | ForEach-Object { [Convert]::ToByte($_, 16) }) } else { Bytes $f }

            # Comprobación local con el certificado: qué resumen y formato usa la firma
            $ax.EtiquetaCertificadoIn = $cert
            if (Paso "ObtenerCertificado($cert)" { $ax.ObtenerCertificado() }) {
                try { $x = [Security.Cryptography.X509Certificates.X509Certificate2]::new([byte[]](Bytes $ax.DatosOut)) }
                catch { Write-Host "   No se pudo leer el certificado: $($_.Exception.Message)"; $x = $null }
              if ($x) {
                $db = [Text.Encoding]::Default.GetBytes($datos)
                $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($x)
                $ec  = [Security.Cryptography.X509Certificates.ECDsaCertificateExtensions]::GetECDsaPublicKey($x)
                foreach ($h in 'SHA1', 'SHA256') {
                    $ha = [Security.Cryptography.HashAlgorithmName]::new($h)
                    if ($rsa) { Write-Host ("   RSA PKCS#1 v1.5 + {0}: {1}" -f $h, $rsa.VerifyData($db, $fb, $ha, [Security.Cryptography.RSASignaturePadding]::Pkcs1)) }
                    if ($ec)  { Write-Host ("   ECDSA + {0}: {1}" -f $h, $ec.VerifyData($db, $fb, $ha)) }
                }
                if ($rsa) {
                    # Descifrado en bruto (s^e mod n) para ver qué hay dentro del bloque firmado
                    $p = $rsa.ExportParameters($false)
                    $n = [Numerics.BigInteger]::new([byte[]]($p.Modulus[($p.Modulus.Length - 1)..0] + 0))
                    $e = [Numerics.BigInteger]::new([byte[]]($p.Exponent[($p.Exponent.Length - 1)..0] + 0))
                    $s = [Numerics.BigInteger]::new([byte[]]($fb[($fb.Length - 1)..0] + 0))
                    $m = [Numerics.BigInteger]::ModPow($s, $e, $n).ToByteArray()
                    [Array]::Reverse($m)
                    $bloque = (($m | ForEach-Object { '{0:X2}' -f $_ }) -join '').TrimStart('0')
                    $sha1 = (([Security.Cryptography.SHA1]::Create().ComputeHash($db)) | ForEach-Object { '{0:X2}' -f $_ }) -join ''
                    $sha256 = (([Security.Cryptography.SHA256]::Create().ComputeHash($db)) | ForEach-Object { '{0:X2}' -f $_ }) -join ''
                    $dhex = (($db) | ForEach-Object { '{0:X2}' -f $_ }) -join ''
                    $que = if ($bloque.EndsWith($sha256)) { 'termina en SHA-256(datos)' } elseif ($bloque.EndsWith($sha1)) { 'termina en SHA-1(datos)' } elseif ($bloque.EndsWith($dhex)) { 'contiene los datos SIN resumir' } else { 'no reconocido' }
                    Write-Host "   Bloque PKCS#1 descifrado: $que"
                    Write-Host "   (cola del bloque: ...$($bloque.Substring([Math]::Max(0, $bloque.Length - 80))))"
                }
              }
            }
            $ax.EtiquetaKeyPubIn = $pub
            $ax.FirmaIn = $ax.FirmaOut
            Paso "Verificar($pub)" { $ax.Verificar() } | Out-Null
        }
    }
}

Paso 'FinalizarConexion' { $ax.FinalizarConexion() } | Out-Null
