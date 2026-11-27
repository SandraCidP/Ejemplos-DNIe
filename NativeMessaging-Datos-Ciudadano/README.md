# DNIe en Chrome, Edge y Firefox (Native Messaging)

Ejemplo de código de la FNMT-RCM para usar el **DNI electrónico (DNIe 3.0 y 4.0)** desde una página web en los navegadores actuales:

- **Lectura de los datos del ciudadano**: datos personales, MRZ, fotografía, firma manuscrita, certificados y claves públicas.
- **Firma y verificación**: firma de un texto, de un resumen SHA-256 o de un archivo del equipo, y verificación de la firma.

Tiene las mismas funciones que el ejemplo del **ActiveX del DNIe** ([`ActiveX-Datos-Ciudadano`](../ActiveX-Datos-Ciudadano/)), que solo funciona con el motor de Internet Explorer. Las dos versiones usan el driver PKCS#11 del DNIe de la FNMT-RCM.

```
Página web  ──▶  Extensión del navegador  ──▶  DNIeDatos.exe  ──▶  Driver PKCS#11  ──▶  DNIe
(dniedatos.js)   (contenido.js, fondo.js)       (anfitrión)
```

## Puesta en marcha

Con Visual Studio 2022 (o sus Build Tools, con C++), el driver del DNIe, un lector y un DNIe:

```bat
scripts\compilar.cmd      :: genera bin\<plataforma>\Release\DNIeDatos.exe
scripts\registrar.cmd     :: lo da a conocer a Chrome, Edge y Firefox (usuario actual)
```

Después, cargue la extensión en el navegador (`extension\chromium` en Chrome y Edge; `extension\firefox` en Firefox) y abra la página de ejemplo:

```bat
web\servir.cmd            :: http://localhost:8081/
```

Ninguno de estos pasos necesita permisos de administrador.

## Usarlo en su página

```html
<script src="dniedatos.js"></script>
<script>
  DNIeDatos.leer(["datos", "imagenes"]).then(function (r) {
      console.log(r.datos.nombre, r.imagenes.formatoFoto);
  }).catch(function (error) {
      if (error.codigo !== "cancelado") alert(error.message);
  });
</script>
```

El PIN lo pide el programa en una ventana suya: la página nunca lo ve. Antes de entregar datos personales, el programa pide permiso al usuario y le dice qué página los pide.

## Documentación

Toda la información está en la [Guía para desarrolladores](docs/GUIA-DESARROLLADORES.md): requisitos, compilación con Visual Studio y Visual Studio Code, referencia de las operaciones, verificación de la firma en el servidor, instalación en los equipos de los usuarios, seguridad y problemas frecuentes.

## Licencia

MIT, con copyright de la FNMT-RCM: ver [LICENSE](LICENSE). Los componentes de terceros tienen sus propias licencias (siguiente apartado).

## Componentes de terceros

La página de ejemplo usa `openjpeg.js` (OpenJPEG compilado a JavaScript, licencia BSD) para mostrar la foto y la firma manuscrita, y el proyecto incluye las cabeceras PKCS#11 de RSA. Sus avisos y licencias están en [AVISOS-TERCEROS.md](AVISOS-TERCEROS.md).

## Carpetas

| Carpeta | Contenido |
| --- | --- |
| `anfitrion` | El programa `DNIeDatos.exe` (C++), sus recursos y los manifiestos para los navegadores |
| `extension` | La extensión: `chromium` (Chrome y Edge) y `firefox` |
| `web` | La biblioteca `dniedatos.js` y la página de ejemplo |
| `scripts` | Compilar, registrar, desregistrar y probar el programa sin navegador |
| `docs` | Guía para desarrolladores |
| `.vscode` | Configuración de Visual Studio Code |

FNMT-RCM · Equipo DIT Drivers
