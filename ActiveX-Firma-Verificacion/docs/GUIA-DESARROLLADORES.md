# Guía para desarrolladores: firma con DNIe desde una página web (DNIe 3.0 y 4.0)

Versión 1.0 · 27 de noviembre de 2026

Esta guía acompaña al proyecto **DNIeActiveX**: un control ActiveX muy sencillo que permite a una página web **firmar un archivo con el DNI electrónico y verificar una firma**. Nació para el DNIe 4.0, pero sirve para cualquier versión del DNIe que su middleware admita (probado con DNIe 3.0 y 4.0). Está escrita para programadores que quieran:

1. **usar el control tal cual** en sus propias páginas, o
2. **tomar el código como base** y ampliarlo.

No hace falta saber COM ni criptografía de Windows para seguirla. El código fuente está comentado paso a paso. Esta guía da la visión de conjunto y dice dónde mirar.

## Contenido

1. [Qué es y qué no es](#1-qué-es-y-qué-no-es)
2. [Restricciones de uso](#2-restricciones-de-uso)
3. [Puesta en marcha en diez minutos](#3-puesta-en-marcha-en-diez-minutos)
4. [Importar y compilar en Visual Studio Code](#4-importar-y-compilar-en-visual-studio-code)
5. [Importar y compilar en Visual Studio](#5-importar-y-compilar-en-visual-studio)
6. [Usar el control en su página](#6-usar-el-control-en-su-página)
7. [Llevar la firma al servidor](#7-llevar-la-firma-al-servidor)
8. [Referencia del control](#8-referencia-del-control)
9. [Instalarlo en los equipos de los usuarios](#9-instalarlo-en-los-equipos-de-los-usuarios)
10. [Cómo funciona por dentro](#10-cómo-funciona-por-dentro)
11. [Mapa del código](#11-mapa-del-código)
12. [Cómo ampliarlo](#12-cómo-ampliarlo)
13. [Pruebas](#13-pruebas)
14. [Seguridad](#14-seguridad)
15. [Problemas frecuentes](#15-problemas-frecuentes)
16. [Glosario](#16-glosario)

## 1. Qué es y qué no es

### Qué hace

- Firma un archivo del disco con el certificado de FIRMA del DNIe y guarda la firma en un archivo aparte (`documento.pdf` → `documento.pdf.p7s`).
- Detecta el DNIe sea cual sea su versión, porque de esas diferencias se encarga el middleware, y también si sus certificados están caducados.
- Verifica que un archivo de firma corresponde a un documento y que el certificado del firmante es de confianza.
- Pide el PIN del DNIe con un cuadro de diálogo propio, que admite caracteres de cualquier tipo. El diálogo detecta la versión del DNIe y exige la longitud que le corresponde: de 8 a 16 caracteres en el DNIe 3.0 y de 12 a 16 en el 4.0. Tiene una casilla "Mostrar el PIN" para ver lo que se escribe.
- Dice a la página qué versión del DNIe hay en el lector (3.0 o 4.0), para que adapte sus textos.

**El DNIe solo se usa para firmar.** La verificación la hace Windows por software y no necesita tarjeta, lector ni PIN (ver el apartado 10).

### Datos técnicos

| Aspecto | Valor |
| --- | --- |
| Formato de firma | PKCS#7 / CMS **separada** (*detached*), codificación DER |
| Algoritmo de resumen | SHA-256 |
| Algoritmo de firma | El de la clave del DNIe, que Windows elige solo. Por ejemplo, ECDSA con la curva P-256 en un DNIe con claves de curva elíptica |
| Certificados dentro de la firma | El del firmante y los de sus autoridades intermedias. La raíz no va dentro |
| Acceso a la tarjeta | CNG de Windows + Card Module (minidriver) del DNIe |
| Versiones del DNIe | Las que admita el middleware instalado. Probado con 3.0 y 4.0 |
| Tecnología del control | COM en C++ puro, sin ATL ni MFC. DLL sin dependencias |
| Arquitecturas | 32 bits (x86) y 64 bits (x64) |
| Nombre para crearlo (ProgID) | `DNIeActiveX.Signer` |

**Limitación importante: el navegador.** Un ActiveX solo puede usarse desde el motor de Internet Explorer:

- Internet Explorer 11,
- Microsoft Edge en "modo Internet Explorer", o
- una aplicación HTA (una página que Windows ejecuta en ventana propia con `mshta.exe`).

En Chrome, Firefox o Edge normal **no funciona**: en ellos no existe `ActiveXObject`. Si su aplicación debe funcionar en esos navegadores, este control no es la solución. Sí le servirá la parte criptográfica (`Crypto.cpp`), que no depende de ActiveX.

El apartado 2, "Restricciones de uso", detalla la compatibilidad navegador por navegador.

**Qué no hace.** No consulta si el certificado está revocado, no añade sello de tiempo, no genera firmas CAdES/XAdES/PAdES y no firma texto ni datos en memoria, solo archivos. El apartado 12, "Cómo ampliarlo", indica por dónde empezar cada una de estas mejoras.

## 2. Restricciones de uso

El control tiene pocas restricciones, pero una es determinante: el navegador. Este apartado las reúne todas, para que pueda decidir si el control sirve para su caso antes de escribir una línea.

### Navegador mínimo

El navegador mínimo es **Internet Explorer 11**, o un programa que use su motor: Microsoft Edge en modo Internet Explorer o una aplicación HTA.

No sirve ninguna versión de Chrome, Firefox, Safari ni de otro navegador, ni antigua ni moderna. La restricción no es de versión, sino de tecnología: ActiveX solo existe en el motor de Internet Explorer.

La página de ejemplo solo usa HTML y JavaScript que entiende el modo de documento IE9, así que también se vería en Internet Explorer 9 y 10. Pero esas versiones no existen en Windows 10 ni en Windows 11, que son los sistemas que admite el control, y Microsoft ya no les da soporte.

### Compatibilidad por fabricante

| Fabricante | Navegador | ¿Utilizable? | Observaciones |
| --- | --- | --- | --- |
| Microsoft | Aplicación HTA (`mshta.exe`) | **Sí** | Viene con Windows 10 y 11 y no requiere configurar nada. Es la vía que usa el ejemplo. |
| Microsoft | Internet Explorer 11 | Sí, con condiciones | Es la versión mínima. Requiere las condiciones que se indican más abajo. Retirado de las ediciones habituales de Windows 10 en junio de 2022 y ausente de Windows 11. Sigue disponible en Windows 10 LTSC y en Windows Server. |
| Microsoft | Internet Explorer 10 y anteriores | **No** | Sin soporte de Microsoft y sin versión para Windows 10 ni 11. |
| Microsoft | Edge en modo Internet Explorer | Sí, con condiciones | Edge 77 o posterior. Hay que activar el modo IE para la página y cumplir las condiciones que se indican más abajo. Microsoft ha anunciado que lo mantendrá al menos hasta 2029. |
| Microsoft | Edge en modo normal | **No** | Usa el motor Chromium, que no tiene ActiveX. |
| Microsoft | Edge anterior a 2020 (motor EdgeHTML) | **No** | Nunca admitió ActiveX. Ya está retirado. |
| Google | Chrome, cualquier versión | **No** | No existe `ActiveXObject`. |
| Mozilla | Firefox, cualquier versión | **No** | No existe `ActiveXObject`. |
| Apple | Safari, cualquier versión | **No** | No existe `ActiveXObject`. Además, el control solo existe para Windows. |
| Otros | Opera, Brave, Vivaldi y demás navegadores basados en Chromium | **No** | Mismo motor que Chrome. |
| Cualquiera | Navegadores de Android, iOS, macOS y Linux | **No** | El control es una DLL de Windows. |

Existen extensiones de terceros que incrustan el motor de Internet Explorer en Chrome o en Firefox (por ejemplo, IE Tab). No son una vía admitida.

### Condiciones en Internet Explorer 11 y en Edge en modo IE

Que el navegador sea compatible no basta. Además:

- **El control debe estar ya registrado en el equipo.** La página no puede descargarlo ni instalarlo: no se distribuye como paquete `.cab` (ver el apartado 9).
- **La zona de seguridad de la página debe permitir ActiveX.** En concreto, las opciones "Ejecutar controles y complementos de ActiveX" y "Generar scripts de los controles ActiveX marcados como seguros para scripting". Es lo habitual en "Intranet local" y en "Sitios de confianza".
- **El "modo protegido" impide guardar la firma junto al documento.** Afecta sobre todo a la zona Internet.
- **El filtrado ActiveX debe estar desactivado** para ese sitio.
- **En Edge, el modo IE hay que activarlo.** Lo hace el administrador con una lista de sitios de empresa (ver el apartado 9), o el propio usuario en `edge://settings/defaultBrowser` (opción "Permitir que los sitios se vuelvan a cargar en el modo Internet Explorer"). Si lo activa el usuario, caduca a los 30 días para cada página.
- **El JavaScript de la página debe ser el que entiende Internet Explorer** (ver el apartado 6).

### Otras restricciones

- **Sistema operativo.** Solo Windows 10 u 11. No hay versión para macOS, Linux, Android ni iOS.
- **Arquitectura.** Hay DLL de 32 bits (x86) y de 64 bits (x64). En un Windows sobre ARM64 solo puede usarse con el motor de 32 bits (ver el apartado 15).
- **Tarjeta.** Hacen falta un lector, el DNIe y su middleware (Card Module). Sin el middleware, Windows no reconoce la tarjeta.
- **Qué firma.** Solo archivos del disco, y solo en formato PKCS#7 separado. No firma texto ni datos en memoria, y no genera firmas CAdES, XAdES ni PAdES.
- **Seguridad.** El control confía en la página que lo usa. Instálelo solo donde esas páginas sean de confianza (ver el apartado 14).
- **Licencia.** MIT (archivo `LICENSE`). Permite usar, modificar y redistribuir el código, también con fines comerciales, siempre que se conserve el aviso de copyright. Se entrega sin garantía.

Fuera de un navegador no hay restricción de programa. Puede usar el control cualquiera que hable COM: *Windows Script Host* (es lo que hacen las pruebas), VBA o PowerShell.

## 3. Puesta en marcha en diez minutos

### Requisitos

- Windows 10 u 11.
- Para compilar: Visual Studio 2022 o sus *Build Tools*, con las herramientas de C++ y el SDK de Windows. No hace falta ATL.
- Para firmar: lector de tarjetas, un DNIe y su middleware (Card Module) instalado.

Si no tiene Visual Studio, las *Build Tools* se instalan con esta orden. Descarga varios GB y pide permisos de administrador:

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

Sin ellas, `scripts\compilar.cmd` termina con "no se encuentra Visual Studio ni sus Build Tools (falta vswhere.exe)".

### Pasos

```bat
scripts\compilar.cmd      :: genera bin\Win32\Release y bin\x64\Release
scripts\registrar.cmd     :: registra el control para el usuario actual
web\abrir.cmd             :: abre la página de ejemplo
```

Ninguno de los tres necesita permisos de administrador. Para deshacer el registro: `scripts\desregistrar.cmd`.

Tras recompilar no hace falta registrar de nuevo (la DLL sigue en la misma ruta), pero sí cerrar antes la página o la HTA que la tuviera cargada.

Junto a los botones de firmar y verificar hay un botón **Limpiar**, que vacía las rutas y el resultado de la última operación. La página se limpia también sola al retirar el DNIe del lector (tarda hasta dos segundos en notarlo), para que los datos de una persona no queden a la vista de la siguiente.

La página de ejemplo tiene, al final, un botón **Salir** que cierra la ventana. En la HTA la cierra directamente. En Internet Explorer o Edge en modo IE lo habitual es que el navegador pida confirmación antes.

El título de la página nombra la versión del DNIe. Al abrirse dice "DNIe 4.0". Si detecta un DNIe 3.0 en el lector, cambia a "DNIe 3.0".

Estos pasos se dan desde una consola. Para trabajar desde un entorno de desarrollo, vea el apartado 4 (Visual Studio Code) o el apartado 5 (Visual Studio).

### Comprobar que funciona sin DNIe

```bat
powershell -ExecutionPolicy Bypass -File tests\ejecutar-pruebas.ps1
```

Debe terminar con `TODO CORRECTO`. Estas pruebas firman con un certificado temporal, así que no necesitan tarjeta.

## 4. Importar y compilar en Visual Studio Code

Visual Studio Code es un editor: no trae compilador y no entiende por sí mismo los proyectos `.vcxproj`. Por eso aquí no se "importa" un proyecto, sino que se abre su carpeta. Para compilar, VS Code llama a los mismos scripts del apartado 3. El proyecto ya trae esa configuración hecha, en la carpeta `.vscode`.

### Qué hace falta

- Windows 10 u 11. En otro sistema la carpeta se puede abrir y leer, pero no compilar.
- Visual Studio Code.
- Visual Studio 2022 o sus *Build Tools* (ver el apartado 3). Son las que compilan.
- La extensión **C/C++** de Microsoft (`ms-vscode.cpptools`). No hace falta para compilar, pero sí para el autocompletado y para depurar. VS Code propone instalarla al abrir la carpeta.

### Abrir el proyecto

1. En VS Code, elija **Archivo → Abrir carpeta…** y seleccione la carpeta raíz del proyecto, la que contiene `DNIeActiveX.sln`. No abra la carpeta `src` ni un archivo suelto: las tareas no aparecerían.
2. Si VS Code pregunta si confía en los autores de la carpeta, responda que sí. En "modo restringido" las tareas no se ejecutan.
3. Acepte instalar la extensión recomendada cuando VS Code lo proponga.

Desde una consola es lo mismo con `code .` en la carpeta del proyecto.

### Compilar

Pulse **Ctrl+Mayús+B**. Se ejecuta la tarea por defecto, que llama a `scripts\compilar.cmd` y genera las dos DLL de Release. El resultado se ve en el panel "Terminal". Los errores y avisos del compilador aparecen además en el panel "Problemas", con enlace a la línea del código.

Las demás tareas están en **Terminal → Ejecutar tarea…**:

| Tarea | Qué hace |
| --- | --- |
| Compilar (Release, x86 y x64) | Ejecuta `scripts\compilar.cmd`. Es la de `Ctrl+Mayús+B`. |
| Registrar el control | Ejecuta `scripts\registrar.cmd`. |
| Desregistrar el control | Ejecuta `scripts\desregistrar.cmd`. |
| Ejecutar las pruebas | Ejecuta `tests\ejecutar-pruebas.ps1`. No necesitan DNIe. |
| Abrir la página de ejemplo | Ejecuta `web\abrir.cmd`. |

También puede escribir esas órdenes en el terminal integrado (**Terminal → Nuevo terminal**). Las tareas no hacen otra cosa.

Antes de recompilar, cierre la página o la HTA que tenga cargado el control. Si no, la DLL está en uso y la compilación falla.

### Autocompletado

`.vscode\c_cpp_properties.json` indica a la extensión C/C++ dónde están las cabeceras y con qué opciones se compila. Solo sirve al editor: no interviene en la compilación.

Un detalle: la cabecera `DNIeActiveX_h.h` la genera el compilador de interfaces (MIDL) al compilar. Hasta la primera compilación no existe, y el editor subraya en rojo su `#include` en `Signer.h`. No es un error del código: compile una vez y desaparece.

### Depurar

Con el control compilado y registrado, ponga un punto de interrupción (por ejemplo, en `SignFileDetached`, en `Crypto.cpp`) y pulse **F5**. La configuración "Depurar el control en la HTA (32 bits)", de `.vscode\launch.json`, arranca la página de ejemplo con el `mshta.exe` de 32 bits. La ejecución se detiene cuando la página llama al control.

Se depura la DLL de Release, que es la registrada. Está optimizada, así que al avanzar paso a paso el depurador puede saltarse líneas. Para depurar con más comodidad, use Visual Studio con la configuración Debug (ver el apartado 5).

### Los archivos de la carpeta .vscode

| Archivo | Para qué |
| --- | --- |
| `tasks.json` | Las tareas: compilar, registrar, desregistrar, probar y abrir la página. |
| `c_cpp_properties.json` | Rutas y opciones para el autocompletado. |
| `launch.json` | La configuración de depuración. |
| `extensions.json` | La extensión que VS Code recomienda instalar. |

## 5. Importar y compilar en Visual Studio

El proyecto es una solución nativa de Visual Studio, así que no hay nada que importar ni que convertir: se abre y se compila. Visual Studio usa el mismo `DNIeActiveX.vcxproj` y el mismo MSBuild que `scripts\compilar.cmd`.

### Qué hace falta

- Visual Studio 2022, en cualquier edición. La Community, gratuita, es suficiente.
- La carga de trabajo **"Desarrollo para el escritorio con C++"**, que incluye el compilador (conjunto de herramientas v143) y el SDK de Windows. Se elige en el instalador de Visual Studio (*Visual Studio Installer* → Modificar).
- No hacen falta ATL ni MFC.

Con una versión posterior a la 2022, Visual Studio propone al abrir la solución "redirigir" el proyecto a su propio conjunto de herramientas. Puede aceptarlo, o instalar el conjunto v143 desde el instalador y dejar el proyecto como está.

### Abrir la solución

1. Abra Visual Studio y elija **Abrir un proyecto o una solución** (o **Archivo → Abrir → Proyecto o solución…**).
2. Seleccione `DNIeActiveX.sln`, en la carpeta raíz del proyecto. También vale hacer doble clic sobre ese archivo en el Explorador de Windows.
3. En el **Explorador de soluciones** aparece el proyecto `DNIeActiveX`, con sus archivos de código, de cabecera y de recursos.

### Compilar

1. En la barra de herramientas, elija la configuración (**Debug** o **Release**) y la plataforma (**x86** o **x64**).
2. Elija **Compilar → Compilar solución** (`Ctrl+Mayús+B`).
3. El resultado aparece en la ventana "Salida", y los errores, en la "Lista de errores".

La DLL queda en la misma carpeta que con los scripts:

| Configuración y plataforma | DLL generada |
| --- | --- |
| Release, x86 | `bin\Win32\Release\DNIeActiveX.dll` |
| Release, x64 | `bin\x64\Release\DNIeActiveX.dll` |
| Debug, x86 | `bin\Win32\Debug\DNIeActiveX.dll` |
| Debug, x64 | `bin\x64\Debug\DNIeActiveX.dll` |

La plataforma que Visual Studio llama x86 es la que el proyecto y las carpetas llaman Win32.

Para generar las dos DLL de una vez, use **Compilar → Compilación por lotes…**, marque las dos filas de Release (Win32 y x64) y pulse **Compilar**. Es lo mismo que hace `scripts\compilar.cmd`.

`scripts\registrar.cmd` registra las DLL de **Release**. Compile las dos en Release antes de registrar.

Antes de recompilar, cierre la página o la HTA que tenga cargado el control. Si no, la DLL está en uso y la compilación falla con el error LNK1104.

### Depurar

Una DLL no se ejecuta sola: hay que decir a Visual Studio qué programa la carga.

1. Compile en Release para x86 y registre el control.
2. En el Explorador de soluciones, pulse con el botón derecho sobre el proyecto y elija **Propiedades → Depuración**. Arriba, elija "Todas las configuraciones" y la plataforma Win32.
3. En **Comando**, escriba `C:\Windows\SysWOW64\mshta.exe`. En **Argumentos del comando**, escriba `"$(SolutionDir)web\DNIeFirma.hta"`, con las comillas.
4. Ponga un punto de interrupción y pulse **F5**.

Estos datos se guardan en `DNIeActiveX.vcxproj.user`, un archivo propio de cada equipo, que no forma parte del proyecto.

Para depurar la versión Debug, que no está optimizada, registre antes esa DLL:

```bat
%SystemRoot%\SysWOW64\regsvr32.exe /n /i:user bin\Win32\Debug\DNIeActiveX.dll
```

Al terminar, ejecute de nuevo `scripts\registrar.cmd` para dejar registrada la de Release.

## 6. Usar el control en su página

### El ejemplo mínimo

```html
<button onclick="firmar()">Firmar un archivo</button>

<script>
    function firmar() {
        // 1. Crear el control. Falla si el navegador no admite ActiveX
        //    o si el control no está registrado en el equipo.
        var firmador;
        try {
            firmador = new ActiveXObject("DNIeActiveX.Signer");
        } catch (e) {
            alert("No se puede usar el control de firma en este equipo.");
            return;
        }

        // 2. Que el usuario elija el archivo.
        var archivo = firmador.SelectFile("Seleccione el archivo que quiere firmar");
        if (!archivo) { return; }                    // El usuario canceló.

        // 3. Firmar. El control pide el PIN por su cuenta.
        if (firmador.SignFile(archivo, "")) {
            alert("Firmado por " + firmador.SignerName + "\nFirma: " + archivo + ".p7s");
        } else {
            alert("No se ha podido firmar:\n" + firmador.LastError);
        }
    }
</script>
```

Verificar es igual de corto:

```js
if (firmador.VerifyFile(archivo, "")) {              // Lee archivo + ".p7s"
    alert("Firma válida de " + firmador.SignerName);
} else {
    alert(firmador.LastError);
}
```

El ejemplo completo, con interfaz y mensajes, está en `web/index.html`.

### Seis cosas que conviene saber

**1. Los errores no son excepciones.** `SignFile` y `VerifyFile` devuelven `true` o `false`. Cuando devuelven `false`, la propiedad `LastError` contiene una explicación en español pensada para mostrársela al usuario ("El PIN introducido no es correcto…", "No se detecta el DNIe…"). No hace falta `try/catch` alrededor de esas llamadas.

Si la página tiene que distinguir una cancelación de un error, vea el final del apartado 8.

**2. Sí hace falta `try/catch` al crear el control.** `new ActiveXObject(...)` lanza una excepción si el navegador no admite ActiveX o si el control no está registrado en el equipo. Capture ese caso y explíqueselo al usuario. `web/index.html` muestra cómo.

**3. No use `<input type="file">` para elegir el archivo que se va a firmar.** Por seguridad, el navegador no entrega a JavaScript la ruta real del archivo (da algo como `C:\fakepath\documento.pdf`), y el control necesita la ruta para leerlo. Use `SelectFile`, que abre el diálogo "Abrir" de Windows y devuelve la ruta completa.

Para enviar después el archivo al servidor, en cambio, `<input type="file">` sí sirve (ver el apartado 7).

**4. Las llamadas son bloqueantes.** `SignFile` no vuelve hasta que el usuario ha escrito el PIN y la tarjeta ha firmado. Mientras tanto la página no se repinta. Si quiere mostrar un "Firmando…", escríbalo y llame al control dentro de un `setTimeout`, para dar tiempo al navegador a pintarlo:

```js
mostrar("Firmando…");
setTimeout(function () {
    var ok = firmador.SignFile(archivo, "");
    // ...
}, 50);
```

**5. Escriba JavaScript que entienda Internet Explorer.** Nada de `const`, `let`, funciones flecha, plantillas de texto ni `fetch`. Use `var` y `function`.

**6. La página puede saber qué DNIe hay en el lector.** La propiedad `DnieVersion` devuelve `"3.0"` o `"4.0"`, y una cadena vacía si no hay ningún DNIe insertado. Leerla no pide el PIN, así que puede consultarla al cargar la página y adaptar sus textos:

```js
var version = firmador.DnieVersion || "4.0";     // Sin tarjeta: versión por defecto
document.title = "Firma con DNIe " + version;
```

`web/index.html` lo hace así, y repite la consulta cada dos segundos para que el título cambie cuando el usuario inserta la tarjeta (y para limpiar la página cuando la retira).

## 7. Llevar la firma al servidor

El control firma y verifica en el equipo del usuario. En una aplicación web, lo normal es que el documento y su firma acaben en el servidor. Este apartado explica cómo enviarlos y cómo comprobar allí la firma.

### El recorrido completo

1. El usuario elige el documento con `SelectFile`.
2. La página llama a `SignFile`. El control pide el PIN y guarda la firma junto al documento (`documento.pdf.p7s`).
3. El usuario envía el documento y la firma con un formulario.
4. El servidor verifica la firma y comprueba quién ha firmado, por su NIF.
5. Si todo es correcto, el servidor guarda los dos archivos.

### Enviar el documento y la firma

Basta un formulario con dos campos de archivo:

```html
<form action="/firmas" method="post" enctype="multipart/form-data">
    <p>Documento: <input type="file" name="documento"></p>
    <p>Firma (.p7s): <input type="file" name="firma"></p>
    <button type="submit">Enviar</button>
</form>
```

`<input type="file">` no entrega a JavaScript la ruta del archivo (apartado 6), pero sí envía su contenido al servidor, que es lo que aquí hace falta.

El inconveniente es que el usuario elige el documento dos veces: una para firmarlo y otra para enviarlo. Hoy el control no entrega la firma a la página, así que no se puede evitar.

Muéstrele al menos dónde ha quedado la firma, como hace `web/index.html` al terminar. En el apartado 12 se propone una ampliación para enviar la firma sin que el usuario tenga que buscarla.

### Verificar la firma en el servidor

**No dé por buena la verificación del navegador.** Que `VerifyFile` devuelva `true` en el equipo del usuario no demuestra nada al servidor. La página se ejecuta en un equipo que usted no controla, y su resultado se puede falsear. El servidor tiene que verificar la firma por su cuenta.

Debe comprobar lo mismo que `VerifyFile`:

1. Que la firma corresponde al documento recibido.
2. Que el certificado del firmante encadena con la raíz del DNIe.

Y una tercera cosa que el control no hace: que quien ha firmado es quien debía firmar. Para eso, compare el NIF del certificado con el del usuario de su aplicación (ver "Quién ha firmado", más abajo).

El servidor necesita para ello el certificado raíz del DNIe (ver "La raíz del DNIe"). La firma trae el certificado del firmante y los de sus autoridades intermedias, pero no la raíz.

**Con OpenSSL.** En un servidor Linux, esta orden hace las dos comprobaciones:

```
openssl cms -verify -binary -inform DER \
    -in documento.pdf.p7s -content documento.pdf \
    -CAfile raiz-dnie.pem -out /dev/null
```

Si las dos son correctas, responde `CMS Verification successful`. Si la firma no corresponde al documento, responde `CMS Verification failure`. Si falta la raíz, o no es la de ese DNIe, el error termina en `unable to get issuer certificate`.

No olvide `-binary`. Sin esa opción, OpenSSL trata el documento como texto, cambia sus saltos de línea antes de calcular el resumen y la verificación falla.

**Con .NET.** Hace falta el paquete NuGet `System.Security.Cryptography.Pkcs`:

```csharp
using System.Security.Cryptography;
using System.Security.Cryptography.Pkcs;
using System.Security.Cryptography.X509Certificates;

static bool VerificarFirma(byte[] documento, byte[] firma, X509Certificate2 raizDnie)
{
    var cms = new SignedCms(new ContentInfo(documento), detached: true);
    cms.Decode(firma);                                // Firma en binario (DER)

    // 1. ¿La firma corresponde al documento?
    try { cms.CheckSignature(verifySignatureOnly: true); }
    catch (CryptographicException) { return false; }

    // 2. ¿El certificado de cada firmante encadena con la raíz del DNIe?
    foreach (SignerInfo firmante in cms.SignerInfos)
    {
        var cadena = new X509Chain();
        cadena.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
        cadena.ChainPolicy.CustomTrustStore.Add(raizDnie);
        cadena.ChainPolicy.ExtraStore.AddRange(cms.Certificates);   // Intermedias
        // Sin consultar la revocación, igual que el control:
        cadena.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
        if (!cadena.Build(firmante.Certificate!)) { return false; }
    }
    return true;
}
```

La raíz se carga, por ejemplo, con `X509Certificate2.CreateFromPem(File.ReadAllText("raiz-dnie.pem"))`.

**Versiones.** La orden de OpenSSL es para la versión 3.0 o posterior, y el código de .NET, para .NET 8 o posterior.

**Con otros lenguajes.** En Java, la biblioteca Bouncy Castle ofrece lo mismo. En PHP 8, la función `openssl_cms_verify`. Esta guía no incluye ejemplos para ellos.

**Con VALIDe.** La Administración General del Estado ofrece VALIDe (https://valide.redsara.es/valide/), un servicio web para validar firmas y certificados. Su página no indica si admite firmas separadas como las de este control. Compruébelo antes de recomendárselo a sus usuarios.

### La raíz del DNIe

La raíz que hace falta depende de la generación del DNIe. Para saber cuál es la de una firma, liste los certificados que trae:

```
openssl pkcs7 -inform DER -in documento.pdf.p7s -print_certs -noout
```

La línea `issuer` de la autoridad intermedia da el nombre de la raíz. Por ejemplo, en un DNIe emitido por *AC DNIE 007*, la cadena es *AC DNIE 007* → *AC RAIZ DNIE 3*, con claves de curva elíptica. El propio certificado de *AC DNIE 007* indica dónde se publica su raíz: `http://pki.policia.es/dnie/ecc/certs/ACRaiz3.crt`.

Las raíces anteriores, *AC RAIZ DNIE* y *AC RAIZ DNIE 2*, están en el portal oficial del DNIe (https://www.dnielectronico.es/PortalDNIe/), en "Área de descargas" → "Autoridades de Certificación y Validación". El portal publica también sus huellas.

Antes de confiar en una raíz descargada, compare su huella con una fuente oficial. Si viene en binario (DER), conviértala al formato PEM que usan los ejemplos:

```
openssl x509 -inform DER -in ACRaiz3.crt -out raiz-dnie.pem
```

Si su servidor recibe firmas de DNIe de distintas generaciones, ponga todas las raíces en el mismo archivo PEM, una detrás de otra. En .NET, añada cada una a `CustomTrustStore`.

### Quién ha firmado

Identifique al firmante por su NIF, no por su nombre: dos personas pueden llamarse igual. El control no devuelve el NIF (`SignerName` solo da el nombre), pero está en el certificado del firmante, dentro de la firma.

El NIF va en el atributo `serialNumber` del titular del certificado (OID 2.5.4.5). En los DNIe recientes tiene el formato `IDCES-99999999R`: el prefijo `IDCES-` seguido del NIF. El formato puede variar de una generación del DNIe a otra, así que no dé por supuesto el prefijo: si su servidor recibe firmas de varias generaciones, compruébelo con un certificado de cada una.

La orden `openssl pkcs7` del apartado anterior lo muestra. En .NET:

```csharp
static string? Nif(X509Certificate2 certificado)
{
    foreach (var rdn in certificado.SubjectName.EnumerateRelativeDistinguishedNames())
        if (rdn.GetSingleElementType().Value == "2.5.4.5")   // Atributo serialNumber
            return rdn.GetSingleElementValue();               // "IDCES-99999999R"
    return null;
}
```

Llámela con el certificado de cada firmante (`firmante.Certificate`).

### La hora de la firma

La firma lleva la fecha y la hora en que se hizo (atributo `signingTime`, en UTC). Es la del reloj del equipo que firmó, así que por sí sola no prueba nada (ver el apartado 14). Para mostrarla, en .NET:

```csharp
static DateTime? HoraDeFirma(SignerInfo firmante)
{
    foreach (CryptographicAttributeObject atributo in firmante.SignedAttributes)
        if (atributo.Values[0] is Pkcs9SigningTime hora) { return hora.SigningTime; }
    return null;
}
```

### Datos personales

Cada `.p7s` contiene el certificado del firmante, con su nombre y su NIF. Si guarda firmas en el servidor, está tratando datos personales sujetos al RGPD. Decida cuánto tiempo las conserva y quién puede verlas, y consúltelo con el delegado de protección de datos de su organización.

## 8. Referencia del control

Se crea con `new ActiveXObject("DNIeActiveX.Signer")`.

### Métodos

| Método | Devuelve | Descripción |
| --- | --- | --- |
| `SelectFile(titulo)` | texto | Muestra el diálogo "Abrir" con ese título. Devuelve la ruta completa, o `""` si el usuario cancela. Solo deja elegir archivos que ya existen. |
| `SignFile(archivo, firma)` | `true` / `false` | Firma `archivo` y guarda la firma en `firma`. Si `firma` es `""`, usa `archivo + ".p7s"`. Si ya existe, lo sobrescribe. |
| `VerifyFile(archivo, firma)` | `true` / `false` | Comprueba que `firma` es una firma válida de `archivo`. Si `firma` es `""`, usa `archivo + ".p7s"`. Acepta firmas en binario (DER) y en Base64. |

### Propiedades

| Propiedad | Tipo | Descripción |
| --- | --- | --- |
| `LastError` | texto, solo lectura | Motivo del último fallo de `SignFile` o `VerifyFile`, pensado para mostrárselo al usuario. Vacía si la última operación fue correcta. `SelectFile` no la modifica. |
| `SignerName` | texto, solo lectura | Titular del certificado de la última firma, o firmante de la última verificación. En el DNIe tiene la forma "APELLIDOS, NOMBRE (FIRMA)". Si hay varios firmantes, van separados por `"; "`. Puede tener valor aunque la operación haya fallado (por ejemplo, con una firma correcta cuyo certificado no es de confianza). No incluye el NIF (ver el apartado 7). |
| `CheckCertificateTrust` | `true` / `false` | Por defecto `true`: `VerifyFile` exige que el certificado del firmante sea de confianza para Windows. Con `false` solo se comprueba que la firma corresponde al archivo. |
| `CertificateThumbprint` | texto | Por defecto vacía: se firma con el DNIe. Si se indica la huella SHA-1 (40 dígitos hexadecimales) de un certificado del almacén personal, se firma con ese. Pensada para pruebas. |
| `DnieVersion` | texto, solo lectura | Versión del DNIe que hay en el lector en el momento de leer la propiedad: `"3.0"` o `"4.0"`. Vacía si no hay ningún DNIe insertado o si su versión no se reconoce. No pide el PIN. |

### Qué comprueba exactamente `VerifyFile`

1. Que el archivo de firma es un PKCS#7 firmado.
2. Que la firma de cada firmante corresponde al contenido del documento. Si el documento ha cambiado un solo bit, falla.
3. Si `CheckCertificateTrust` es `true`: que el certificado de cada firmante encadena con una autoridad raíz instalada en Windows y está dentro de su periodo de validez **en la fecha actual**.

No comprueba la revocación del certificado (ver el apartado 14, "Seguridad").

### Mensajes de error

`LastError` está pensado para mostrárselo al usuario tal cual. Muchos mensajes tienen tres partes: qué se estaba haciendo, la explicación y el código de error de Windows. Por ejemplo: "La tarjeta no aceptó el PIN: El PIN introducido no es correcto. Atención: varios intentos fallidos seguidos bloquean el DNIe. (0x8010006B)".

Por eso la tabla da un fragmento de cada mensaje, y no su comienzo.

| `LastError` contiene… | Qué ha pasado |
| --- | --- |
| "No se ha indicado el archivo…" | Se ha llamado a `SignFile` o a `VerifyFile` con el primer parámetro vacío. |
| "No se pudo abrir el archivo…" | El documento o el archivo de firma no existe o no se puede leer. Si no se indica la firma, se busca `archivo + ".p7s"`. |
| "No se detecta el DNIe…" | No hay ningún certificado de firma de DNIe en el almacén personal: nunca se ha insertado la tarjeta en ese equipo, o falta el lector o el Card Module. Los certificados caducados también cuentan. También aparece si se retira la tarjeta durante la firma. |
| "Operación cancelada…" | El usuario cerró el diálogo del PIN o el de selección de certificado. |
| "El PIN introducido no es correcto…" | La tarjeta rechazó el PIN. Varios fallos seguidos bloquean el DNIe. |
| "El PIN del DNIe está bloqueado…" | Se han agotado los intentos. El propio mensaje indica que se desbloquea en un Punto de Actualización del DNIe. |
| "…huella…" o "El certificado indicado no tiene clave privada…" | Solo con `CertificateThumbprint`. La huella está mal escrita o no corresponde a ningún certificado del almacén personal, o ese certificado no tiene clave privada. |
| "No se pudo crear el archivo de firma…" | La carpeta de destino no admite escritura (ver el apartado 15). |
| "…está vacío o es demasiado grande para ser una firma." | El archivo de firma está vacío o pasa de 16 MB. |
| "…no contiene una firma PKCS#7 válida." | El archivo indicado como firma no es una firma. Por ejemplo, se ha indicado el propio documento. |
| "…no contiene ningún firmante." o "…no incluye el certificado del firmante…" | La firma no la ha generado este control, que siempre incluye el firmante y su certificado. |
| "La firma NO es válida…" | El documento se modificó después de firmarse, o la firma es de otro documento. |
| "…no es de confianza…" | La firma es correcta, pero Windows no confía en quien firma: falta la autoridad raíz, o el certificado ha caducado. |

Si la página tiene que distinguir casos, por ejemplo para no presentar una cancelación como un error, busque un fragmento del mensaje:

```js
if (firmador.LastError.indexOf("Operación cancelada") >= 0) {
    // El usuario ha cancelado: no es un error.
}
```

Es frágil: si cambia el texto del mensaje, la comprobación deja de funcionar. En el apartado 12 se propone una alternativa.

## 9. Instalarlo en los equipos de los usuarios

Para que una página pueda usar el control, cada equipo necesita cuatro cosas.

### 9.1. Las DLL

Copie `bin\Win32\Release\DNIeActiveX.dll` y `bin\x64\Release\DNIeActiveX.dll` a una carpeta del equipo. No dependen de nada más: no hay que instalar el *Visual C++ Redistributable*.

### 9.2. El registro

Windows debe saber dónde están. Hay dos formas:

| Alcance | Orden | ¿Administrador? |
| --- | --- | --- |
| Solo el usuario actual | `regsvr32 /n /i:user DNIeActiveX.dll` | No |
| Todos los usuarios del equipo | `regsvr32 DNIeActiveX.dll` | Sí |

El registro por usuario tiene una trampa: **un programa ejecutado "como administrador" no lo ve**. Windows, por seguridad, hace que los programas elevados ignoren el registro COM del usuario. Si el control se va a usar desde programas elevados, regístrelo para todo el equipo.

Hay que registrar **las dos DLL, cada una con su `regsvr32`**: la de 32 bits con `%SystemRoot%\SysWOW64\regsvr32.exe` y la de 64 bits con `%SystemRoot%\System32\regsvr32.exe`. El motivo es que un programa de 32 bits solo puede cargar DLL de 32 bits, y uno de 64, de 64. Además, las pestañas de Internet Explorer suelen ser de 32 bits aunque Windows sea de 64. `scripts\registrar.cmd` hace exactamente esto y sirve de modelo para un instalador.

### 9.3. Un navegador que lo admita

Según el caso:

- **HTA.** Es lo que usa el ejemplo (`web\DNIeFirma.hta`). No requiere configurar nada.
- **Internet Explorer 11 o Edge en modo IE.** La página debe estar en una zona de seguridad que permita ejecutar controles ActiveX marcados como seguros (lo habitual en "Intranet local" y "Sitios de confianza"). El "modo protegido" de la zona Internet impide escribir archivos en las carpetas del usuario, así que la firma fallaría al guardar el `.p7s`.

El apartado 2, "Restricciones de uso", detalla qué navegadores sirven y las condiciones de cada uno.

### 9.4. El software del DNIe y un lector de tarjetas

El software, con el Card Module, se descarga del portal oficial del DNIe (https://www.dnielectronico.es/PortalDNIe/), en "Área de descargas" → "Windows". Hay instalador de 32 bits y de 64 bits, para Windows 10 y 11. Según la ayuda técnica de la Agencia Tributaria, Windows Update también puede instalarlo automáticamente.

Un equipo que solo verifica firmas no necesita lector ni software del DNIe. Pero sí necesita la raíz del DNIe entre las "Entidades de certificación raíz de confianza" de Windows: sin ella, la verificación responde "no es de confianza". El apartado 7 explica cómo obtenerla.

### Instalarlo en muchos equipos

Sirve cualquier herramienta de despliegue que copie archivos y ejecute órdenes: un script de inicio de sesión por directiva de grupo, Intune o un paquete MSI, por ejemplo. Tiene que hacer lo mismo que `scripts\registrar.cmd`: copiar las dos DLL y registrar cada una con su `regsvr32`.

Si los equipos no tienen el software del DNIe, el portal del DNIe indica cómo instalarlo sin preguntas al usuario (instalación desatendida):

```
Instalador_Tarjetas_DNIe_x64.exe /silent
Instalador_Tarjetas_DNIe_x86.exe /silent
```

### Publicar la página en un servidor web

El ejemplo abre una página local dentro de la HTA. En una aplicación real, la página la sirve un servidor web, y entonces cuentan las condiciones del apartado 2:

- **Internet Explorer 11.** Añada el sitio a "Sitios de confianza" o a "Intranet local": Opciones de Internet → Seguridad → elija la zona → Sitios. En una organización, el administrador puede asignar el sitio a una zona en todos los equipos por directiva de grupo.
- **Edge en modo IE.** El administrador publica una lista de sitios (un XML) en la que la página lleva `<open-in>IE11</open-in>`. La activa con las directivas "Configure Internet Explorer integration" y "Configure the Enterprise Mode Site List", explicadas en https://learn.microsoft.com/en-us/deployedge/edge-ie-mode-policies.
- **HTA.** El ejemplo carga en la HTA una página local. Si carga una página de un servidor, tenga en cuenta que, con `application="yes"`, la página del marco recibe los mismos permisos que la HTA (ver el comentario de `web/DNIeFirma.hta`).

## 10. Cómo funciona por dentro

### Las piezas

```text
 Página web (JavaScript)
        │  new ActiveXObject("DNIeActiveX.Signer")
        ▼
 ┌───────────────────────────── DNIeActiveX.dll ─────────────────────────────┐
 │  Signer.cpp      Fachada COM: recibe las llamadas del script y convierte  │
 │                  los tipos. No contiene criptografía.                     │
 │  Crypto.cpp      Firma y verificación. No contiene nada de COM.           │
 │  PinDialog.cpp   Cuadro de diálogo del PIN, adaptado a la versión del     │
 │                  DNIe (3.0 o 4.0).                                        │
 │  DllMain.cpp     Fábrica de objetos y registro en Windows.                │
 └───────────────────────────────────────────────────────────────────────────┘
        │  CryptoAPI (certificados, PKCS#7)  +  CNG (clave privada)
        ▼
 Windows: "Microsoft Smart Card Key Storage Provider"
        ▼
 Card Module (minidriver) del DNIe      ──►  lector  ──►  chip del DNIe
```

La idea central: **el control nunca habla con la tarjeta**. Pide las operaciones a Windows, y Windows las delega en el Card Module. Por eso el mismo código vale para cualquier tarjeta con minidriver.

Solo hay una consulta al lector, y es de lectura: antes de pedir el PIN, el control pregunta a Windows por el ATR de la tarjeta, unos bytes que la identifican y que se leen sin PIN. Con él sabe si es un DNIe 3.0 o 4.0 y qué longitud de PIN exigir (`ClassifyDnieAtr`, en `Crypto.cpp`). Si no lo consigue, el diálogo admite de 8 a 16 caracteres y la firma sigue adelante. La propiedad `DnieVersion` hace esa misma consulta cada vez que la página la lee.

### Dónde están los certificados y dónde la clave

Al insertar el DNIe, Windows copia sus **certificados** (la parte pública) al almacén personal del usuario, el mismo que se ve en `certmgr.msc` → Personal → Certificados. La **clave privada** no sale nunca del chip: junto a cada certificado, Windows anota en qué proveedor y tarjeta está su clave.

Un detalle con consecuencias: **los certificados se quedan en el almacén aunque se retire la tarjeta**. El control busca el certificado en el almacén, así que:

- "No se detecta el DNIe" significa que ese usuario no ha insertado nunca un DNIe en ese equipo (o que falta el Card Module), no solo que ahora no haya tarjeta.
- Si el certificado está en el almacén pero la tarjeta no está puesta, es Windows quien pide insertarla al solicitar la clave.
- Si en el equipo se han usado varios DNIe (familiares, un DNI renovado), el control firma con el que esté en el lector en ese momento. Solo pregunta con cuál firmar si no puede saberlo (ninguno insertado, o varios a la vez en distintos lectores). En esa lista aparecen también los caducados, con su fecha.

**Los certificados caducados cuentan.** El control no descarta un certificado de FIRMA por estar caducado: un DNIe con los certificados caducados sigue siendo un DNIe, y el chip firma igualmente. La caducidad se comprueba al verificar (`VerifyFile` con `CheckCertificateTrust = true` rechaza esa firma). Si los descartara, el resultado sería un "No se detecta el DNIe" con la tarjeta puesta.

El DNIe contiene dos certificados del ciudadano:

| Certificado | Para qué | Uso de clave |
| --- | --- | --- |
| AUTENTICACIÓN | Identificarse en webs | Firma digital |
| FIRMA | Firmar documentos | No repudio |

El control usa siempre el de FIRMA. Lo reconoce porque su emisor contiene "DNIE" (por ejemplo "AC DNIE 004" o "AC DNIE 006") y su uso de clave incluye "no repudio". Ninguno de los dos datos depende de la versión del DNIe.

### Qué ocurre al llamar a `SignFile`

Los ocho pasos están numerados igual en `SignFileDetached` (`Crypto.cpp`):

1. Abre el almacén personal.
2. Elige el certificado: el de FIRMA del DNIe que haya en el almacén, esté vigente o caducado. Si no hay ninguno, error. Si hay varios, pregunta.
3. Pide a Windows la clave privada de ese certificado. No obtiene la clave, sino un "asa" para pedir operaciones con ella a través de CNG.
4. Si la clave está en una tarjeta, averigua la versión del DNIe, muestra el diálogo del PIN adaptado a ella y entrega el PIN a la clave (propiedad `NCRYPT_PIN_PROPERTY`), para que Windows no muestre el suyo.
5. Prepara los datos del firmante: certificado, clave, SHA-256 y la hora de la firma como atributo firmado.
6. Reúne los certificados que irán dentro de la firma: el del firmante y los de las autoridades intermedias, sin la raíz.
7. Crea el mensaje PKCS#7 y le pasa el documento a trozos de 64 KB. Al llegar al último trozo, la tarjeta firma. Un PIN incorrecto puede detectarse aquí o ya en el paso 4, según cuándo lo compruebe el proveedor. El mensaje para el usuario es el mismo.
8. Recoge la firma terminada y la guarda en el `.p7s`.

Leer el documento a trozos permite firmar archivos de cualquier tamaño sin cargarlos en memoria.

### Qué ocurre al llamar a `VerifyFile`

1. Lee el `.p7s` (y lo convierte desde Base64 si hace falta).
2. Abre el mensaje PKCS#7 y le carga la firma.
3. Le pasa el documento a trozos para que calcule su resumen.
4. Por cada firmante: localiza su certificado dentro de la firma, comprueba la firma con su clave pública y, si procede, comprueba que el certificado es de confianza.

**En la verificación no interviene el DNIe.** La hace entera Windows por software, con su biblioteca criptográfica (CryptoAPI, `crypt32.dll`). La clave pública que hace falta está en el certificado del firmante, que va dentro del propio `.p7s`. Por eso se puede verificar sin tarjeta, sin lector y sin PIN, y en un equipo distinto del que firmó.

| Operación | Quién hace el cálculo | ¿Hace falta el DNIe? |
| --- | --- | --- |
| Firmar (`SignFile`) | El chip del DNIe, con su clave privada. Windows se lo pide a través de CNG y del Card Module. | **Sí**: tarjeta, lector y PIN. |
| Verificar (`VerifyFile`) | Windows, por software (CryptoAPI), con la clave pública del certificado incluido en la firma. | **No**. |

Lo único que la verificación necesita del DNIe es su autoridad raíz, que se instala en Windows con el middleware. Sin ella, la firma se comprueba igual, pero el certificado del firmante no se da por fiable (ver el apartado 15).

### El objeto COM, en pocas palabras

`Signer.h` lo explica con detalle. Lo esencial:

- La página solo conoce el **ProgID** `DNIeActiveX.Signer`. Windows lo traduce a un identificador único (CLSID), busca en el registro qué DLL lo implementa, la carga y le pide el objeto.
- JavaScript llama a los métodos **por nombre** a través de la interfaz `IDispatch`. El control no implementa esa traducción a mano: la resuelve la **biblioteca de tipos** que se genera a partir de `DNIeActiveX.idl` y va incrustada en la DLL.
- Con `IObjectSafety` el control declara al navegador que es "seguro para scripting". Es una declaración del programador, no algo que Windows verifique (ver el apartado 14, "Seguridad").

## 11. Mapa del código

Orden de lectura recomendado, de lo que se ve desde fuera a lo de dentro:

| Orden | Archivo | Qué contiene |
| --- | --- | --- |
| 1 | `src/DNIeActiveX/DNIeActiveX.idl` | El "contrato": métodos, propiedades y GUID. |
| 2 | `web/index.html` | Cómo se usa el control desde JavaScript. |
| 3 | `src/DNIeActiveX/Signer.h` y `.cpp` | La clase COM. Introducción a COM en la cabecera. |
| 4 | `src/DNIeActiveX/Crypto.h` y `.cpp` | Firma y verificación, paso a paso. |
| 5 | `src/DNIeActiveX/PinDialog.h` y `.cpp` | El diálogo del PIN. |
| 6 | `src/DNIeActiveX/DllMain.cpp` | Fábrica de objetos y claves del registro. |

Archivos de apoyo:

| Archivo | Para qué |
| --- | --- |
| `src/DNIeActiveX/DNIeActiveX.rc`, `resource.h`, `res/seguro.ico` | Recursos incrustados: biblioteca de tipos, diálogo del PIN e imagen. |
| `src/DNIeActiveX/DNIeActiveX.def` | Funciones que exporta la DLL. |
| `src/DNIeActiveX/Module.h` | Variables globales compartidas. |
| `src/DNIeActiveX/DNIeActiveX.vcxproj`, `DNIeActiveX.sln` | Proyecto de Visual Studio. |
| `.vscode/*.json` | Configuración de Visual Studio Code: tareas, autocompletado y depuración. |
| `web/DNIeFirma.hta`, `web/abrir.cmd` | La página como aplicación de escritorio. |
| `scripts/*.cmd` | Compilar, registrar y desregistrar. |
| `tests/prueba.js`, `tests/ejecutar-pruebas.ps1` | Pruebas automáticas. |

## 12. Cómo ampliarlo

### Añadir un método o una propiedad

Son siempre tres pasos. Ejemplo: una propiedad `Version` de solo lectura.

**1. Declararla en `DNIeActiveX.idl`**, dentro de `interface IDNIeSigner`, con un `id` que no esté usado:

```idl
[propget, id(9), helpstring("Version del control")]
HRESULT Version([out, retval] BSTR* version);
```

**2. Declararla en `Signer.h`**, junto a las demás:

```cpp
STDMETHODIMP get_Version(BSTR* version) override;
```

**3. Implementarla en `Signer.cpp`:**

```cpp
STDMETHODIMP DNIeSigner::get_Version(BSTR* version)
{
    return ToBstr(L"1.0", version);
}
```

Recompile y ya puede escribir `firmador.Version` en JavaScript. No hay que tocar nada más: la biblioteca de tipos se regenera sola y `Invoke` encuentra el método nuevo.

Reglas que conviene respetar:

- En el `.idl`, una propiedad de lectura se llama `get_Nombre` en C++, y una de escritura, `put_Nombre`.
- Use solo tipos de Automatización: `BSTR` (texto), `VARIANT_BOOL` (booleano), `LONG`, `DOUBLE`. Son los que entienden los lenguajes de script.
- Devuelva siempre `S_OK` y comunique los fallos "normales" con un valor de retorno y `LastError`. Un `HRESULT` de error se convierte en una excepción de JavaScript.
- Una vez publicado el control, **no cambie los `id` ni los parámetros de lo que ya existe**: rompería las páginas que lo usan. Añada miembros nuevos.

### Crear su propio control a partir de este

Si copia el proyecto para hacer **otro** control, cambie obligatoriamente:

- Los **tres GUID** de `DNIeActiveX.idl` (biblioteca, interfaz y clase). Genere nuevos con `[guid]::NewGuid()` en PowerShell. Dos controles con el mismo GUID se pisan en el registro.
- El **ProgID** (`PROG_ID` y `VERSIONED_PROG_ID` en `DllMain.cpp`).
- El GUID del proyecto en el `.vcxproj` y el `.sln`.

### Reutilizar solo la firma, sin ActiveX

`Crypto.h`, `Crypto.cpp`, `PinDialog.*`, `Module.h` y `resource.h` no dependen de COM. Puede incluirlos en un programa de consola, un servicio o una extensión de otro tipo y llamar directamente a `SignFileDetached` y `VerifyFileDetached`. Necesita además:

- El diálogo `IDD_PIN` y el icono `IDI_PIN` de `DNIeActiveX.rc`, con `res\seguro.ico`.
- Definir la variable `g_hInstance` que declara `Module.h`, y darle el módulo que contiene esos recursos (en un `.exe`, `GetModuleHandleW(nullptr)`).
- Enlazar con `crypt32.lib`, `cryptui.lib`, `ncrypt.lib` y `winscard.lib`.

### Ideas de ampliación

| Mejora | Por dónde empezar |
| --- | --- |
| Comprobar la revocación (OCSP/CRL) | `IsCertificateTrusted` en `Crypto.cpp`: añadir `CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT` a `CertGetCertificateChain`. Necesita conexión a Internet. |
| Validar con la fecha de la firma en vez de la actual | Leer el atributo de hora de firma del mensaje y pasarlo como `pTime` a `CertGetCertificateChain`. |
| Sello de tiempo (firma longeva) | Añadir un atributo no firmado con la respuesta de una autoridad de sellado (`CryptRetrieveTimeStamp`). |
| Firma con el documento dentro (*attached*) | Quitar `CMSG_DETACHED_FLAG` en `SignFileDetached` y adaptar la verificación. |
| Firmar texto o datos en memoria | Nuevo método que alimente el mensaje desde un búfer en lugar de `FeedFileToMessage`. |
| Limitar qué páginas pueden usar el control | Implementar `IObjectWithSite` para conocer la URL de la página y rechazar las no autorizadas. |
| Otro aspecto para el diálogo del PIN | `IDD_PIN` en `DNIeActiveX.rc` (diseño) y `PinDialog.cpp` (comportamiento). |
| Otra longitud de PIN | `PIN_MIN_LENGTH_DNIE3`, `PIN_MIN_LENGTH_DNIE4` y `PIN_MAX_LENGTH` en `PinDialog.h`. Hoy valen 8, 12 y 16. |
| Reconocer otra versión del DNIe | `ClassifyDnieAtr` en `Crypto.cpp` (lee la versión en el ATR de la tarjeta), el tipo `DnieVersion` en `PinDialog.h` y `GetInsertedDnieVersion` en `Crypto.cpp` (da el texto de la propiedad `DnieVersion`). |
| Rechazar o avisar al firmar con un certificado caducado | `IsUsableForSigning` en `Crypto.cpp` (`CertVerifyTimeValidity` dice si está en plazo). |
| Entregar la firma a la página | Nuevo método que devuelva el `.p7s` en Base64, para enviarlo con `XMLHttpRequest` sin que el usuario tenga que buscarlo (ver el apartado 7). |
| Devolver el NIF del firmante | Nueva propiedad junto a `SignerName`. `CertGetNameStringW` con `CERT_NAME_ATTR_TYPE` y el OID `2.5.4.5` (`szOID_DEVICE_SERIAL_NUMBER`), como en `GetCertificateName` (`Crypto.cpp`). |
| Devolver la hora de la firma | Nueva propiedad con el atributo `signingTime` del firmante: `CryptMsgGetParam` con `CMSG_SIGNER_AUTH_ATTR_PARAM`, en `VerifyFileDetached`. |
| Código de error para la página | Propiedad `LastErrorCode` con el código de Windows que hoy va entre paréntesis al final de `LastError` (`DescribeError` en `Crypto.cpp`). Así la página distingue casos sin buscar texto. |
| Diálogo "Guardar como" para la firma | Nuevo método con `GetSaveFileNameW`, junto a `SelectFile` en `Signer.cpp`. |
| Saber qué versión está instalada | Propiedad de solo lectura `Version`. Útil cuando hay equipos con versiones distintas del control. |

## 13. Pruebas

`tests\ejecutar-pruebas.ps1` crea un certificado temporal, ejecuta `tests\prueba.js` con las DLL de 32 y de 64 bits y lo borra todo al terminar. Comprueba:

- Firma y verificación de un documento, de un archivo de varios trozos y de un archivo vacío.
- Rechazo de un documento modificado.
- Rechazo de un certificado que no es de confianza, con su mensaje.
- Lectura de una firma en Base64.
- Errores de uso: archivo inexistente, firma que no es un PKCS#7, huella mal escrita.
- Que `DnieVersion` responde con uno de sus tres valores.
- Que sin ningún DNIe conocido el control avisa en lugar de ofrecer otros certificados. Esta prueba se omite si el usuario ya tiene certificados de DNIe en su almacén, porque entonces el control intentaría firmar con ellos.

Ejecútelas desde una consola normal, no "como administrador" (ver el apartado 9).

`tests\prueba.js` es además un ejemplo de uso del control desde *Windows Script Host*: se maneja igual que desde una página.

**Si añade un método, añada su prueba** en `prueba.js` con la función `comprobar`.

Lo que estas pruebas no pueden cubrir, porque depende de la tarjeta: la detección del certificado de FIRMA del DNIe, la de la versión de la tarjeta y la entrega del PIN al Card Module. Tampoco abren el diálogo del PIN ni la página, así que la casilla "Mostrar el PIN", los botones "Limpiar" y "Salir", la limpieza automática al retirar el DNIe y el cambio del título según la versión del DNIe hay que comprobarlos a mano.

## 14. Seguridad

Lea este apartado antes de instalar el control fuera de un entorno de pruebas.

- **Verifique siempre en el servidor.** La verificación en el navegador sirve para informar al usuario, no como prueba: la página se ejecuta en un equipo que usted no controla (ver el apartado 7).
- **Las firmas contienen datos personales.** Cada `.p7s` lleva el certificado del firmante, con su nombre y su NIF. Por eso el `.gitignore` del proyecto excluye los `.p7s`. Si los guarda, trátelos como datos personales (ver el apartado 7).
- **El control confía en la página.** Se declara "seguro para scripting" y lee y escribe archivos en las rutas que le indica el JavaScript. Cualquier página que el navegador permita ejecutar podría pedirle que firme un archivo o que escriba un `.p7s` en una ruta. La firma siempre exige que el usuario escriba el PIN, pero la escritura de archivos no. Instálelo solo en equipos donde las páginas que usan ActiveX sean de confianza. Para un uso real, limite qué páginas pueden usarlo (ver "Ideas de ampliación" en el apartado 12).
- **Sin comprobación de revocación.** Un certificado robado y revocado seguiría verificándose como válido.
- **Validación con la fecha actual.** Una firma hecha con un certificado que entonces era válido pero ya ha caducado se dará por no fiable.
- **La hora de la firma no es un sello de tiempo.** Es la del reloj del equipo que firmó.
- **Se puede firmar con un certificado caducado.** El control no lo impide. Quien recibe la firma lo detecta al verificar con `CheckCertificateTrust = true`.
- **El PIN.** El control lo mantiene en memoria el tiempo imprescindible y lo borra al terminar. No lo escribe en disco ni lo devuelve a la página. Con la casilla "Mostrar el PIN" marcada queda a la vista en pantalla: está desmarcada por defecto y es el usuario quien decide.
- **La DLL no está firmada digitalmente.** Para distribuirla conviene firmarla con un certificado de firma de código.

## 15. Problemas frecuentes

### "No se puede usar el control de firma" al abrir la página

- El control no está registrado: ejecute `scripts\registrar.cmd`.
- Está abriendo la página en un navegador sin ActiveX (Chrome, Firefox, Edge normal).
- Solo registró una de las dos DLL y el programa es de la otra arquitectura.
- El programa se ejecuta "como administrador" y el control está registrado solo para el usuario.

### "No se detecta el DNIe"

Abra `certmgr.msc` → Personal → Certificados después de haber insertado el DNIe. Debe aparecer un certificado a su nombre emitido por una "AC DNIE". Si no aparece:

- El lector no está conectado o Windows no lo ve (Administrador de dispositivos → Lectores de tarjetas inteligentes).
- No está instalado el Card Module del DNIe: Windows ve la tarjeta pero la muestra como "Tarjeta inteligente desconocida".
- El servicio "Propagación de certificados" de Windows está deshabilitado.

Si el certificado **sí aparece** y el error persiste, compruebe que es el de FIRMA (en sus detalles, "Uso de la clave" incluye "Sin repudio") y que Windows le asocia una clave privada: al abrirlo, debe indicar "Tiene una clave privada correspondiente a este certificado".

### En un Windows sobre ARM64 no se crea el control

Las DLL son x86 y x64. Un programa ARM64 no puede cargar ninguna. Use el motor de 32 bits, que funciona en todos los equipos: es lo que hace `web\abrir.cmd`. Además, el Card Module debe tener versión ARM64 para que Windows reconozca la tarjeta.

### Aparece el diálogo de PIN de Windows además del del control, o la tarjeta rechaza el PIN

El punto a revisar es `GivePinToKey` en `Crypto.cpp`, que entrega el PIN al proveedor de Windows para que no muestre su propio diálogo.

### No se puede pulsar "Aceptar" en el diálogo del PIN

El botón se activa al llegar a la longitud mínima del PIN: 8 caracteres en el DNIe 3.0 y 12 en el 4.0. Si el control no reconoce la versión de la tarjeta, exige 8. El propio diálogo indica el intervalo y cuántos caracteres lleva escritos. Si su tarjeta tiene un PIN más corto, baje `PIN_MIN_LENGTH_DNIE3` o `PIN_MIN_LENGTH_DNIE4` en `PinDialog.h` y recompile.

### La verificación falla con "no es de confianza"

La firma es correcta, pero Windows no confía en el certificado del firmante. Dos causas:

- En ese equipo falta la autoridad raíz del firmante. Para el DNIe se instala con su middleware. En un equipo sin el software del DNIe, hay que importarla (ver el apartado 7).
- Con el DNIe 4.0, la cadena es *AC DNIE 007* → *AC RAIZ DNIE 3*, distinta de la de las versiones anteriores (ver "La raíz del DNIe" en el apartado 7). El código `0x800B010A` indica que Windows no ha podido completar la cadena; `0x800B0109`, que la completa pero la raíz no está entre las de confianza del equipo.
- El certificado ha caducado (el mensaje lo indica). Ocurre siempre con las firmas hechas con un DNIe caducado.

Para comprobar solo que la firma corresponde al archivo, use `CheckCertificateTrust = false`.

### La ventana de la HTA sale con el icono genérico

La HTA debe declarar el modo de documento IE9. Con IE10 o superior, `mshta` ignora el atributo `ICON`. Está explicado en `web\DNIeFirma.hta`.

### "No se pudo crear el archivo de firma"

La carpeta del documento no admite escritura para ese proceso (por ejemplo, una carpeta de solo lectura, o Internet Explorer en modo protegido). Indique en el segundo parámetro de `SignFile` una ruta donde sí pueda escribir.

La página no puede pedir esa ruta con un diálogo, porque `SelectFile` solo elige archivos que ya existen. Puede ofrecer un campo de texto para escribirla, como hace `web/index.html`.

### El servidor rechaza una firma que el control da por buena

- **El documento ha cambiado por el camino.** Cualquier cambio invalida la firma, también un cambio en los saltos de línea. Pasa, por ejemplo, si el archivo atraviesa un FTP en modo texto o Git con conversión de finales de línea.
- **Falta `-binary` en la orden de OpenSSL** (ver el apartado 7).
- **El servidor no tiene la raíz de ese DNIe**, o tiene la de otra generación.
- **La firma llega en Base64.** `VerifyFile` acepta firmas en binario (DER) y en Base64, pero los ejemplos del apartado 7 esperan binario. Decodifíquela antes.

## 16. Glosario

Términos que aparecen en esta guía, explicados para quien empieza.

| Término | Qué es |
| --- | --- |
| Almacén personal | Lugar de Windows donde están los certificados del usuario (en `certmgr.msc`, "Personal"). Al insertar el DNIe, Windows copia en él sus certificados. Las claves privadas no salen de la tarjeta. |
| Autoridad de certificación (AC) | Entidad que emite certificados y responde de ellos. La AC raíz está en lo alto de la cadena. Las intermedias, como *AC DNIE 007*, emiten los certificados de los ciudadanos. |
| Base64 | Forma de escribir datos binarios como texto. |
| Cadena de certificación | Secuencia que va del certificado del firmante a una AC raíz de confianza, pasando por las intermedias. |
| Card Module (minidriver) | Software del DNIe que permite a Windows usar la tarjeta. Se instala con el software del portal del DNIe. |
| Certificado | Documento electrónico que une una clave pública con una persona. El DNIe tiene dos: el de autenticación y el de firma. |
| CMS / PKCS#7 | Formato estándar de firma electrónica. CMS es la evolución de PKCS#7. Es el formato del archivo `.p7s`. |
| DER | Codificación binaria de certificados y firmas. Es la que usa el control para el `.p7s`. |
| Firma separada (*detached*) | Firma que va en un archivo aparte y no contiene el documento. Para verificarla hacen falta los dos archivos. |
| HTA | Página web que Windows ejecuta como aplicación, con `mshta.exe` y el motor de Internet Explorer. |
| Huella (*thumbprint*) | Resumen SHA-1 de un certificado, que lo identifica. Es lo que espera `CertificateThumbprint`. |
| NIF | Número de identificación fiscal. En el certificado del DNIe identifica al titular (ver el apartado 7). |
| PIN | Clave secreta del DNIe. Sin ella, la tarjeta no firma. |
| ProgID | Nombre con el que se crea un objeto COM, como `DNIeActiveX.Signer`. |
| Revocación | Anulación de un certificado antes de que caduque, por ejemplo por pérdida de la tarjeta. Se consulta por OCSP o en listas de revocación (CRL). |
| Sello de tiempo | Prueba de la hora de una firma, emitida por un tercero de confianza. Este control no lo añade. |
| Zona de seguridad | Clasificación de los sitios en Internet Explorer: Internet, Intranet local, Sitios de confianza… Cada zona permite o bloquea los ActiveX. |
