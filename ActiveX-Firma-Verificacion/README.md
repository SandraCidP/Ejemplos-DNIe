# ActiveX para DNIe (DNIe 3.0 y 4.0)

Control ActiveX/COM sencillo, escrito en C++, que **firma archivos y verifica firmas con el DNI electrónico** (DNIe 3.0 y 4.0), y una página web de ejemplo que lo usa.

Es un proyecto **didáctico**: el código está comentado paso a paso para que otros desarrolladores puedan leerlo, copiarlo y ampliarlo. La explicación completa está en la **[Guía para desarrolladores](docs/GUIA-DESARROLLADORES.md)**.

- Firma en formato **PKCS#7 / CMS separada** (`.p7s`), con SHA-256.
- Accede al DNIe a través de **CNG** y del **Card Module** (minidriver) del DNIe; no habla directamente con la tarjeta. Por eso sirve para **cualquier versión del DNIe** que el middleware admita (probado con DNIe 3.0 y 4.0).
- Detecta el DNIe **aunque sus certificados estén caducados** y firma con ellos; la caducidad se señala al verificar.
- Pide el **PIN** con un cuadro de diálogo propio (caracteres de cualquier tipo), que detecta la versión del DNIe y ajusta el texto y la longitud exigida: de 8 a 16 caracteres en el DNIe 3.0 y de 12 a 16 en el 4.0, con una casilla **"Mostrar el PIN"**.
- Dice a la página **qué versión del DNIe hay en el lector**, para que adapte sus textos: la página de ejemplo lo muestra en su título.
- COM "a mano", **sin ATL ni MFC**: la DLL no tiene dependencias que instalar.

> **Aviso.** ActiveX solo funciona con el motor de Internet Explorer (IE11, Edge en modo IE o una aplicación HTA). No funciona en Chrome, Firefox ni Edge normal. La compatibilidad por navegador y el resto de restricciones están en el apartado 2 de la guía.

## Puesta en marcha

Requisitos: Windows 10/11, Visual Studio 2022 o sus Build Tools (herramientas de C++ y SDK de Windows), el middleware del DNIe y un lector de tarjetas.

Si `compilar.cmd` responde "no se encuentra Visual Studio ni sus Build Tools", instálelas (ver el apartado 3 de la guía).

```bat
scripts\compilar.cmd      :: genera bin\Win32\Release y bin\x64\Release
scripts\registrar.cmd     :: registra el control para el usuario actual (sin ser administrador)
web\abrir.cmd             :: abre la página de ejemplo
```

Para quitar el registro: `scripts\desregistrar.cmd`.

Para abrir y compilar el proyecto desde **Visual Studio Code** o desde **Visual Studio**, vea los apartados 4 y 5 de la guía.

## Uso desde JavaScript

```js
var firmador = new ActiveXObject("DNIeActiveX.Signer");

var archivo = firmador.SelectFile("Seleccione el archivo");   // diálogo "Abrir"

if (firmador.SignFile(archivo, ""))                           // crea archivo + ".p7s"
    alert("Firmado por " + firmador.SignerName);
else
    alert(firmador.LastError);

if (firmador.VerifyFile(archivo, ""))                         // lee archivo + ".p7s"
    alert("Firma válida de " + firmador.SignerName);
else
    alert(firmador.LastError);
```

| Miembro | Descripción |
| --- | --- |
| `SelectFile(titulo)` | Muestra el diálogo "Abrir" y devuelve la ruta elegida (`""` si se cancela). |
| `SignFile(archivo, firma)` | Firma el archivo. Si `firma` es `""`, guarda en `archivo + ".p7s"`. Devuelve `true`/`false`. |
| `VerifyFile(archivo, firma)` | Verifica la firma. Si `firma` es `""`, lee `archivo + ".p7s"`. Devuelve `true`/`false`. |
| `LastError` | Motivo del último fallo, en español. |
| `SignerName` | Titular del certificado de la última firma o verificación. |
| `CertificateThumbprint` | Opcional: huella SHA-1 del certificado con el que firmar, en vez de buscar el del DNIe. |
| `CheckCertificateTrust` | `true` por defecto: al verificar exige que el certificado del firmante sea de confianza en Windows. |
| `DnieVersion` | Solo lectura: versión del DNIe insertado en el lector, `"3.0"` o `"4.0"` (`""` si no hay ninguno). No pide el PIN. |

## Estructura

```text
src/DNIeActiveX/
    DNIeActiveX.idl     Definición de la interfaz COM (métodos y GUID)
    Signer.h / .cpp     La clase COM: lo que ve JavaScript
    Crypto.h / .cpp     Firma y verificación (sin nada de COM; reutilizable)
    PinDialog.h / .cpp  Cuadro de diálogo del PIN
    DllMain.cpp         Fábrica de objetos y registro en Windows
    DNIeActiveX.rc      Recursos: biblioteca de tipos y diálogo del PIN
    DNIeActiveX.def     Funciones exportadas por la DLL
    res/seguro.ico      Imagen del candado del diálogo del PIN
web/
    index.html          Página de ejemplo (el uso del control, comentado)
    DNIeFirma.hta       La misma página como aplicación de escritorio
    abrir.cmd           Abre la HTA con el motor de 32 bits
    logoFNMT.ico/.png   Icono de la página y logo de la cabecera
scripts/                compilar, registrar y desregistrar
.vscode/                Tareas, autocompletado y depuración para Visual Studio Code
tests/                  Pruebas automáticas (no necesitan DNIe)
docs/                   Guía para desarrolladores
```

Orden de lectura recomendado: `DNIeActiveX.idl` → `web/index.html` → `Signer.h` → `Crypto.cpp`.

## Pruebas

```bat
powershell -ExecutionPolicy Bypass -File tests\ejecutar-pruebas.ps1
```

Crean un certificado de prueba temporal, firman y verifican con las DLL de 32 y 64 bits, y lo borran todo al terminar. No cubren lo que depende de la tarjeta (detección del certificado de firma y de la versión del DNIe, y diálogo del PIN): eso hay que probarlo con un DNIe real.

## Limitaciones conocidas

- **Seguridad.** El control se declara "seguro para scripting" y lee/escribe en las rutas que le indica la página. Instálelo solo donde las páginas que lo usan sean de confianza; para un uso real conviene restringir desde qué sitios puede usarse.
- La verificación valida el certificado con la fecha actual y **no consulta la revocación** (OCSP/CRL).
- Con un certificado **caducado** se puede firmar, pero la verificación con `CheckCertificateTrust = true` rechazará esa firma.
- La hora de la firma es la del reloj del equipo, no un sello de tiempo.
- En equipos **ARM64**, usar siempre el motor de 32 bits (`web\abrir.cmd` ya lo hace).

## Autoría y licencia

Desarrollado por la **FNMT-RCM**. Licencia MIT: ver [LICENSE](LICENSE).
