# ============================================================================
#  servir.ps1 - Servidor web minimo para la pagina de ejemplo
# ============================================================================
#  Sirve los archivos de esta carpeta en http://localhost:8081/ y abre el
#  navegador predeterminado. Solo escucha en "localhost": no se puede llegar
#  a el desde otro equipo.
#
#  Uso:  servir.cmd            (puerto 8081)
#        servir.cmd 9000       (otro puerto; hay que anadirlo tambien a la
#                               lista ORIGENES_PERMITIDOS de fondo.js)
#
#  HttpListener es el servidor HTTP que trae .NET; no necesita permisos de
#  administrador para "localhost".
# ============================================================================
param([int]$Puerto = 8081)

$ErrorActionPreference = "Stop"
$carpeta = $PSScriptRoot
$prefijo = "http://localhost:$Puerto/"

# Tipo de contenido de cada extension de archivo
$tipos = @{
    ".html" = "text/html; charset=utf-8"
    ".js"   = "text/javascript; charset=utf-8"
    ".css"  = "text/css; charset=utf-8"
    ".png"  = "image/png"
    ".ico"  = "image/x-icon"
    ".json" = "application/json; charset=utf-8"
}

$servidor = New-Object System.Net.HttpListener
$servidor.Prefixes.Add($prefijo)
try {
    $servidor.Start()
} catch {
    Write-Host "ERROR: no se pudo usar el puerto $Puerto (puede que ya este en uso)." -ForegroundColor Red
    exit 1
}
Write-Host "Sirviendo $carpeta en $prefijo"
Write-Host "Pulse Ctrl+C para detener."
Start-Process $prefijo

try {
    while ($servidor.IsListening) {
        # GetContextAsync + espera corta: asi Ctrl+C detiene el bucle.
        $tarea = $servidor.GetContextAsync()
        while (-not $tarea.AsyncWaitHandle.WaitOne(500)) { }
        $contexto = $tarea.GetAwaiter().GetResult()
        $respuesta = $contexto.Response

        $ruta = [Uri]::UnescapeDataString($contexto.Request.Url.AbsolutePath).TrimStart("/")
        if ($ruta -eq "") { $ruta = "index.html" }
        $archivo = [IO.Path]::GetFullPath((Join-Path $carpeta $ruta))

        # Solo archivos de esta carpeta (nada de "..\..\")
        if ($archivo.StartsWith($carpeta, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path $archivo -PathType Leaf)) {
            $bytes = [IO.File]::ReadAllBytes($archivo)
            $extension = [IO.Path]::GetExtension($archivo).ToLower()
            $respuesta.ContentType = if ($tipos.ContainsKey($extension)) { $tipos[$extension] } else { "application/octet-stream" }
            $respuesta.Headers.Add("Cache-Control", "no-store")
            $respuesta.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $respuesta.StatusCode = 404
        }
        $respuesta.Close()
        Write-Host ("{0} {1}" -f $respuesta.StatusCode, $contexto.Request.Url.AbsolutePath)
    }
} finally {
    $servidor.Stop()
}
