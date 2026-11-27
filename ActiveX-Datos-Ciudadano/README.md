# ActiveX del DNIe (DNIe 3.0 y 4.0)

Ejemplo de código de la FNMT-RCM para usar el **DNI electrónico** desde una página web con el motor de **Internet Explorer** (Internet Explorer 11, Edge en modo IE y aplicaciones HTA):

- **Lectura de los datos del ciudadano**: datos personales, MRZ, fotografía, firma manuscrita, certificados y claves públicas.
- **Firma y verificación** con las claves del DNIe (SHA-256; RSA en el DNIe 3.0 y ECDSA en el DNIe 4.0).

Tiene **la misma interfaz que el ActiveX publicado** en el portal del DNIe (`IDLL_ADMIN`, con los mismos nombres de métodos y propiedades), así que las páginas que ya lo usan funcionan sin cambios. Usa el driver PKCS#11 del DNIe de la FNMT-RCM instalado en el equipo. Su ProgID es `FNMT.ActiveXDNIe` y puede convivir con el ActiveX publicado.

Para Chrome, Edge y Firefox existe la versión con **Native Messaging**, con las mismas funciones: [`NativeMessaging-Datos-Ciudadano`](../NativeMessaging-Datos-Ciudadano/).

```
Página web  ──▶  ActiveXDNIe.dll (FNMT.ActiveXDNIe)  ──▶  Driver PKCS#11 del DNIe  ──▶  DNIe
```

## Puesta en marcha

Con Visual Studio 2022 (o sus Build Tools, con C++ y ATL), el driver del DNIe, un lector y un DNIe:

```bat
scripts\compilar.cmd      :: genera las DLL de x86, x64 y ARM64
scripts\registrar.cmd     :: registra el control para el usuario actual (sin administrador)
web\abrir.cmd         :: abre la página de ejemplo
```

## Usarlo en su página

```html
<script>
  var ax = new ActiveXObject("FNMT.ActiveXDNIe");
  ax.InicializarConexion();
  if (ax.lastresultOut == 0) {
      alert("Hola, " + ax.NombreOut);
  }
  ax.FinalizarConexion();
</script>
```

El PIN lo pide el control en una ventana suya: la página nunca lo ve. Antes de entregar datos personales, el control pide permiso al usuario y le dice qué página los pide.

## Documentación

Toda la información está en la [Guía para desarrolladores](docs/GUIA-DESARROLLADORES.md): requisitos, compilación con Visual Studio y Visual Studio Code, referencia de métodos y códigos de resultado, verificación de la firma en el servidor, instalación en los equipos de los usuarios, seguridad y problemas frecuentes.

## Licencia

MIT, con copyright de la FNMT-RCM: ver [LICENSE](LICENSE). Los componentes de terceros tienen sus propias licencias (siguiente apartado).

## Componentes de terceros

La página de ejemplo usa `openjpeg.js` (OpenJPEG compilado a JavaScript, licencia BSD) para mostrar la foto y la firma manuscrita, y el proyecto incluye las cabeceras PKCS#11 de RSA. Sus avisos y licencias están en [AVISOS-TERCEROS.md](AVISOS-TERCEROS.md).

## Carpetas

| Carpeta | Contenido |
| --- | --- |
| (raíz) | El control (C++ y ATL): interfaz, acceso a la tarjeta, ventanas del PIN y del permiso |
| `pkcs11` | Cabeceras PKCS#11 estándar |
| `web` | Página de ejemplo (`.html` y `.hta`) |
| `scripts` | Compilar, registrar y desregistrar |
| `tests` | Pruebas con tarjeta desde PowerShell, sin mostrar datos personales |
| `docs` | Guía para desarrolladores |
| `.vscode` | Configuración de Visual Studio Code |

FNMT-RCM · Equipo DIT Drivers
