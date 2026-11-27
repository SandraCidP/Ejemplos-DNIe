# Native Messaging para DNIe (DNIe 3.0 y 4.0)

Ejemplo sencillo para **firmar archivos con el DNI electrónico (DNIe 3.0 y 4.0) desde una página web en Chrome, Edge y Firefox** (Windows).

Un navegador moderno no deja que una página use el DNIe. Este proyecto lo resuelve con el mecanismo que esos navegadores ofrecen para ello, *Native Messaging*: la página habla con una extensión, y la extensión con un pequeño programa instalado en el equipo, que es quien firma.

```text
Página web  ──▶  Extensión del navegador  ──▶  DNIeAnfitrion.exe  ──▶  Windows / DNIe
(dniefirma.js)   (contenido.js y fondo.js)     (C++)
```

Es un proyecto **didáctico**: el código está comentado paso a paso para que otros desarrolladores puedan leerlo, copiarlo y ampliarlo. La explicación completa está en la **[Guía para desarrolladores](docs/GUIA-DESARROLLADORES.md)**.

- Firma en formato **PKCS#7 / CMS separada** (`.p7s`), con SHA-256.
- Accede al DNIe a través de **CNG** y del **Card Module** (minidriver) del DNIe; no habla directamente con la tarjeta.
- Pide el **PIN** con un cuadro de diálogo propio, que detecta la versión del DNIe y ajusta el texto y la longitud exigida.
- **El archivo lo elige el usuario** en un diálogo de Windows. La página no indica ni recibe rutas del disco.
- La extensión **solo atiende a las páginas de una lista de orígenes permitidos**.
- El programa es un `.exe` sin dependencias, para **x64 y ARM64**.

## Puesta en marcha

Requisitos: Windows 10/11, Visual Studio 2022 o sus Build Tools (herramientas de C++ y SDK de Windows), el software del DNIe y un lector de tarjetas.

```bat
scripts\compilar.cmd      :: genera bin\x64\Release\DNIeAnfitrion.exe (y ARM64 si hay herramientas)
scripts\registrar.cmd     :: da a conocer el programa a Chrome, Edge y Firefox (sin ser administrador)
```

Después, cargue la extensión en el navegador:

| Navegador | Dónde | Qué elegir |
| --- | --- | --- |
| Chrome | `chrome://extensions` → activar "Modo de desarrollador" → "Cargar descomprimida" | la carpeta `extension\chromium` |
| Edge | `edge://extensions` → activar "Modo de desarrollador" → "Cargar desempaquetada" | la carpeta `extension\chromium` |
| Firefox | `about:debugging#/runtime/this-firefox` → "Cargar complemento temporal…" | el archivo `extension\firefox\manifest.json` |

Y abra la página de ejemplo:

```bat
web\servir.cmd            :: sirve la página en http://localhost:8080/ y la abre
```

Para quitar el registro: `scripts\desregistrar.cmd`.

## Uso desde JavaScript

```html
<script src="dniefirma.js"></script>
```

```js
try {
    const resultado = await DNIeFirma.firmar();      // diálogo "Abrir" + PIN
    alert("Firmado por " + resultado.firmante);
} catch (error) {
    if (error.codigo !== "cancelado") alert(error.message);
}
```

| Función | Descripción |
| --- | --- |
| `DNIeFirma.firmar()` | El usuario elige un archivo y lo firma. Promesa de `{ archivo, archivoFirma, firmante, firma }`; `firma` es el `.p7s` en Base64. |
| `DNIeFirma.verificar()` | El usuario elige un archivo y se comprueba su firma. Promesa de `{ archivo, archivoFirma, firmante }`; se rechaza si la firma no es válida. |
| `DNIeFirma.version()` | Promesa de la versión del DNIe insertado: `"3.0"`, `"4.0"` o `""`. No pide el PIN. |
| `DNIeFirma.extensionDisponible()` | `true` si la extensión está activa en la página. |

Si algo falla, la promesa se rechaza con un `Error`: `message` es la explicación en español y `codigo` indica el tipo (`cancelado`, `error`, `ocupado`, `sin_extension`, `sin_anfitrion`, `origen_no_permitido`).

## Estructura

```text
anfitrion/                  El programa de firma (DNIeAnfitrion.exe)
    Main.cpp                Mensajes con el navegador y las tres operaciones
    Crypto.h / .cpp         Firma y verificación con el DNIe
    PinDialog.h / .cpp      Cuadro de diálogo del PIN
    DNIeAnfitrion.rc        Recursos: diálogo del PIN e icono
    manifiestos/            Manifiestos del anfitrión (Chrome/Edge y Firefox)
extension/
    chromium/               Extensión para Chrome y Edge (Manifest V3)
    firefox/                La misma extensión, con el manifest.json de Firefox
web/
    dniefirma.js            Biblioteca para la página (promesas)
    index.html              Página de ejemplo
    servir.cmd / .ps1       Servidor local para probar la página
scripts/                    compilar, registrar, desregistrar y probar-anfitrion
.vscode/                    Tareas y autocompletado para Visual Studio Code
docs/                       Guía para desarrolladores
```

Orden de lectura recomendado: `web/index.html` → `web/dniefirma.js` → `extension/chromium/contenido.js` → `extension/chromium/fondo.js` → `anfitrion/Main.cpp` → `anfitrion/Crypto.cpp`.

## Limitaciones conocidas

- Solo **Windows**.
- La lista de orígenes permitidos y el identificador de la extensión son los del ejemplo: hay que adaptarlos antes de usarlo en un sitio real (apartados 9 y 12 de la guía).
- La verificación valida el certificado con la fecha actual y **no consulta la revocación** (OCSP/CRL).
- La hora de la firma es la del reloj del equipo, no un sello de tiempo.
- El programa y la extensión no están firmados ni publicados en las tiendas de los navegadores.

## Autoría y licencia

Desarrollado por la **FNMT-RCM**. Licencia MIT: ver [LICENSE](LICENSE).
