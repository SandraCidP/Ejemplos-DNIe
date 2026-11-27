# ============================================================================
#  ejecutar-pruebas.ps1  -  Prueba el control SIN necesidad de un DNIe
# ============================================================================
#  Qué hace:
#    1. Crea un certificado de firma "de mentira" (autofirmado) en el almacén
#       personal del usuario. Su clave es software, así que no pide PIN.
#    2. Ejecuta tests\prueba.js con el motor de scripts de 32 bits y con el
#       de 64 bits, para probar las dos versiones de la DLL.
#    3. Borra el certificado y los archivos temporales, pase lo que pase.
#
#  Requisito: haber ejecutado antes scripts\compilar.cmd y scripts\registrar.cmd
#  Ejecútelo desde una consola normal, NO "como administrador": un programa
#  elevado no ve el registro por usuario del control (ver DllMain.cpp).
#
#  Uso:  powershell -ExecutionPolicy Bypass -File tests\ejecutar-pruebas.ps1
#
#  Lo que estas pruebas NO cubren (hace falta un DNIe real): la detección
#  automática del certificado de FIRMA del DNIe y el diálogo del PIN.
# ============================================================================
$ErrorActionPreference = 'Stop'

$carpeta = Join-Path $env:TEMP ("DNIeActiveX-pruebas-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $carpeta | Out-Null

# KeyUsage: DigitalSignature + NonRepudiation, como un certificado de firma.
# El nombre NO contiene "DNIE" a propósito: el control reconoce un DNIe por
# su EMISOR, y en un certificado autofirmado el emisor es el propio titular.
# Así no lo confunde con un DNIe (se le indica por su huella).
$certificado = New-SelfSignedCertificate `
    -Subject 'CN=Prueba firma ActiveX' `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyUsage DigitalSignature, NonRepudiation `
    -KeyAlgorithm RSA -KeyLength 2048 `
    -NotAfter (Get-Date).AddDays(1)

$fallos = 0
try {
    # Cada DLL solo puede cargarla un programa de su misma arquitectura, así
    # que lanzamos el motor de scripts (cscript) una vez por arquitectura:
    #   - 32 bits (x86): el cscript de SysWOW64 (sí: en un Windows de 64 bits,
    #     la carpeta SysWOW64 es la de los programas de 32 bits).
    #   - 64 bits (x64): el cscript de System32.
    # Caso especial, Windows sobre ARM64: allí System32 es ARM64 y no puede
    # cargar una DLL x64 directamente, pero "start /machine amd64" arranca el
    # mismo programa en modo de emulación x64.
    $cscript32 = '"' + (Join-Path $env:WINDIR 'SysWOW64\cscript.exe') + '"'
    $cscript64 = '"' + (Join-Path $env:WINDIR 'System32\cscript.exe') + '"'
    $arquitectura = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    if ($arquitectura -eq 'Arm64') {
        $cscript64 = 'start "" /machine amd64 /b /wait ' + $cscript64
    }
    $motores = [ordered]@{ '32 bits (x86)' = $cscript32; '64 bits (x64)' = $cscript64 }

    # ¿Hay ya certificados de DNIe en el almacén? Windows los deja ahí aunque
    # se retire la tarjeta. Si los hay, una de las pruebas no puede hacerse
    # (ver la prueba 8 de prueba.js) y se lo indicamos al script.
    $hayDnie = 'no'
    if (Get-ChildItem 'Cert:\CurrentUser\My' | Where-Object { $_.Issuer -match 'DNIE' }) { $hayDnie = 'si' }

    $argumentos = ' //nologo "{0}" "{1}" {2} {3}' -f (Join-Path $PSScriptRoot 'prueba.js'), $carpeta, $certificado.Thumbprint, $hayDnie
    foreach ($nombre in $motores.Keys) {
        Write-Host "=== DLL de $nombre ==="
        $salida = & cmd /c ($motores[$nombre] + $argumentos) 2>&1
        $salida | ForEach-Object { Write-Host $_ }
        # No nos fiamos del código de salida: si el script se interrumpe por
        # un error (p. ej. control sin registrar), cscript devuelve 0 igual.
        if (-not (($salida -join "`n") -match 'RESULTADO: todas las pruebas correctas')) { $fallos++ }
    }
}
finally {
    Remove-Item -Path ("Cert:\CurrentUser\My\" + $certificado.Thumbprint) -DeleteKey -ErrorAction SilentlyContinue
    Remove-Item -Path $carpeta -Recurse -Force -ErrorAction SilentlyContinue
}

if ($fallos -gt 0) { Write-Host 'HAY PRUEBAS CON FALLO'; exit 1 }
Write-Host 'TODO CORRECTO'
