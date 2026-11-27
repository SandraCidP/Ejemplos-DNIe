# ============================================================================
#  servir.ps1  -  Servidor web mínimo para la página de ejemplo
# ============================================================================
#  Sirve los archivos de esta carpeta en http://localhost:8080/ y abre esa
#  dirección en el navegador predeterminado. Se arranca con servir.cmd.
#
#  Es solo para probar el ejemplo en el propio equipo:
#    - Solo atiende a "localhost": no es accesible desde otros equipos.
#    - Solo entrega los tipos de archivo de la tabla $tipos.
#  En un caso real, la página estará en su servidor web, con HTTPS.
#
#  Si cambia el puerto, cambie también el origen en la lista de la extensión
#  (ORIGENES_PERMITIDOS, en fondo.js).
# ============================================================================
$puerto = 8080
$carpeta = $PSScriptRoot
$direccion = "http://localhost:$puerto/"

# Tipos de archivo que se sirven, con el "tipo de contenido" que el
# navegador necesita conocer para tratarlos bien.
$tipos = @{
    ".html" = "text/html; charset=utf-8"
    ".js"   = "text/javascript; charset=utf-8"
    ".png"  = "image/png"
    ".ico"  = "image/x-icon"
}

# HttpListener es el servidor HTTP que trae .NET. Con una dirección de
# "localhost" no hacen falta permisos de administrador.
$servidor = New-Object System.Net.HttpListener
$servidor.Prefixes.Add($direccion)
try {
    $servidor.Start()
}
catch {
    Write-Host "ERROR: no se pudo abrir $direccion. Puede que el puerto $puerto esté ocupado por otro programa."
    exit 1
}

Write-Host "Página de ejemplo disponible en $direccion"
Write-Host "Pulse Ctrl+C para detener el servidor."
Start-Process $direccion      # Abre el navegador predeterminado.

try {
    while ($servidor.IsListening) {
        # Esperamos la siguiente petición en tramos de 200 ms (y no con una
        # espera indefinida) para que Ctrl+C funcione en cualquier momento.
        $espera = $servidor.GetContextAsync()
        while (-not $espera.AsyncWaitHandle.WaitOne(200)) { }
        $contexto = $espera.GetAwaiter().GetResult()
        $respuesta = $contexto.Response

        # De la dirección pedida nos quedamos con la ruta: "/dniefirma.js".
        # La dirección raíz ("/") equivale a index.html.
        $ruta = [Uri]::UnescapeDataString($contexto.Request.Url.AbsolutePath).TrimStart('/')
        if ($ruta -eq '') { $ruta = 'index.html' }

        # Ruta completa en disco. Comprobamos que sigue dentro de esta
        # carpeta (una petición con ".." podría intentar salir de ella), que
        # el archivo existe y que su tipo es de los que servimos.
        $archivo = [IO.Path]::GetFullPath((Join-Path $carpeta $ruta))
        $tipo = $tipos[[IO.Path]::GetExtension($archivo).ToLower()]
        $dentro = $archivo.StartsWith($carpeta + [IO.Path]::DirectorySeparatorChar)

        if ($dentro -and $tipo -and (Test-Path -LiteralPath $archivo -PathType Leaf)) {
            $datos = [IO.File]::ReadAllBytes($archivo)
            $respuesta.ContentType = $tipo
            # Que el navegador no guarde copia: así, al cambiar un archivo
            # basta con recargar la página para ver el cambio.
            $respuesta.Headers.Add("Cache-Control", "no-store")
            $respuesta.OutputStream.Write($datos, 0, $datos.Length)
        }
        else {
            $respuesta.StatusCode = 404      # "No encontrado".
        }
        Write-Host "$($respuesta.StatusCode)  /$ruta"
        $respuesta.Close()
    }
}
finally {
    $servidor.Stop()
}
