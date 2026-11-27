# Guía para desarrolladores: DNIe en Chrome, Edge y Firefox con Native Messaging (DNIe 3.0 y 4.0)

Versión 1.0 · 25 de noviembre de 2026 · Equipo DIT Drivers, FNMT-RCM

Esta guía acompaña al proyecto **DNIe con Native Messaging**: un programa, una extensión de navegador y una biblioteca de JavaScript que permiten a una página web **leer los datos del ciudadano del DNI electrónico** (datos personales, MRZ, foto y firma manuscrita), leer sus certificados y claves públicas, y **firmar y verificar** con sus claves. Funciona en **Chrome, Edge y Firefox** y tiene las mismas funciones que el **ActiveX del DNIe**, que solo funciona con el motor de Internet Explorer. Los dos usan el driver PKCS#11 del DNIe de la FNMT-RCM. Está escrita para programadores que quieran:

1. **usar el ejemplo tal cual** en sus propias páginas, o
2. **tomar el código como base** y ampliarlo.

No hace falta saber PKCS#11 ni haber escrito antes una extensión. El código fuente está comentado paso a paso. Esta guía da la visión de conjunto y dice dónde mirar.

## Contenido

1. [Qué es y qué no es](#1-qué-es-y-qué-no-es)
2. [Restricciones de uso](#2-restricciones-de-uso)
3. [Puesta en marcha en diez minutos](#3-puesta-en-marcha-en-diez-minutos)
4. [Importar y compilar en Visual Studio Code](#4-importar-y-compilar-en-visual-studio-code)
5. [Importar y compilar en Visual Studio](#5-importar-y-compilar-en-visual-studio)
6. [Usarlo en su página](#6-usarlo-en-su-página)
7. [Llevar los datos y la firma al servidor](#7-llevar-los-datos-y-la-firma-al-servidor)
8. [Referencia de la biblioteca y de los mensajes](#8-referencia-de-la-biblioteca-y-de-los-mensajes)
9. [Instalarlo en los equipos de los usuarios](#9-instalarlo-en-los-equipos-de-los-usuarios)
10. [Cómo funciona por dentro](#10-cómo-funciona-por-dentro)
11. [Mapa del código](#11-mapa-del-código)
12. [Cómo ampliarlo](#12-cómo-ampliarlo)
13. [Pruebas](#13-pruebas)
14. [Seguridad](#14-seguridad)
15. [Problemas frecuentes](#15-problemas-frecuentes)
16. [Glosario](#16-glosario)

## 1. Qué es y qué no es

### El problema

Una página web no puede usar el DNIe: el navegador la encierra en una "caja de arena" y no la deja hablar con el lector de tarjetas ni con los programas del equipo. Internet Explorer lo permitía con controles ActiveX, pero Chrome, Edge y Firefox no tienen ActiveX.

Lo que sí ofrecen los tres es **Native Messaging**: una **extensión** del navegador puede arrancar un **programa instalado en el equipo** (el "anfitrión nativo") y hablar con él. Este proyecto usa ese camino:

```
Página web  ──▶  Extensión  ──────────────────▶  DNIeDatos.exe  ──▶  Driver PKCS#11  ──▶  DNIe
(dniedatos.js)   (contenido.js y fondo.js)       (anfitrión)         del DNIe
```

### Qué hace

- Conecta con el DNIe 3.0 o 4.0 a través del driver PKCS#11 del DNIe instalado en Windows.
- Lee los datos del chip: nombre, apellidos, número de DNI, fechas, nacionalidad, sexo, lugar de nacimiento, padres y domicilio (grupo DG13), la MRZ (DG1), la foto (DG2) y la firma manuscrita (DG7).
- Lee los certificados y las claves públicas.
- Firma un texto, un resumen SHA-256 o un archivo del equipo con la clave de firma o con la de autenticación, y verifica firmas.
- Pide el **PIN** en una ventana propia cuando una operación lo necesita: la página nunca lo ve. La ventana exige la longitud de cada versión (de 12 a 16 caracteres en el DNIe 4.0 y de 8 a 16 en el 3.0) y dice qué página lo solicita.
- Pide **permiso al usuario** antes de entregar sus datos personales o sus certificados a una página. La ventana dice qué se va a leer y qué página lo pide.
- Antes de firmar un texto, **lo enseña al usuario** y pide confirmación.

### Datos técnicos

| Aspecto | Valor |
| --- | --- |
| Navegadores | Chrome, Edge y Firefox para Windows |
| Programa (anfitrión) | `DNIeDatos.exe`, C++ sin ATL ni MFC, biblioteca de C++ enlazada de forma estática. Sin dependencias |
| Nombre del anfitrión | `es.fnmt.dniedatos` |
| Extensión | Manifest V3. Identificador en Chrome y Edge: `ieggaeldkkiddkjmlhlankaheebigmeb`. En Firefox: `dniedatos@fnmt.es` |
| Biblioteca para la página | `dniedatos.js`, objeto global `DNIeDatos`, funciones con promesas |
| Acceso a la tarjeta | Driver PKCS#11 del DNIe (`DNIe_P11*.dll`), cargado solo desde la carpeta del sistema |
| Versiones del DNIe | 3.0 y 4.0 |
| Firma | SHA-256. RSA PKCS#1 v1.5 (DNIe 3.0) o ECDSA P-256 en bruto, `r‖s` (DNIe 4.0) |
| Arquitecturas | x86, x64 y ARM64 (`bin\Win32`, `bin\x64` y `bin\ARM64`) |

### Qué no hace

- **No genera firmas de documentos** en formato CAdES, XAdES ni PAdES. Firma un resumen SHA-256 y devuelve la firma "en bruto" con el certificado. Para firmar documentos con esos formatos está AutoFirma.
- **No prueba la identidad del usuario ante el servidor.** Los datos que lee la página los podría falsear quien controle el equipo (ver el apartado 7).
- **No comprueba la revocación** de los certificados.
- **No cambia ni desbloquea el PIN.**

### El proyecto hermano

El ejemplo del **ActiveX del DNIe** tiene las mismas funciones para el motor de Internet Explorer (Internet Explorer 11, Edge en modo IE y aplicaciones HTA). Las dos versiones comparten la clase que habla con la tarjeta (`Tarjeta.cpp`), la interpretación de los grupos de datos y la ventana del PIN. Lo que cambia es cómo llega la petición de la página: en el ActiveX, por COM; aquí, por la extensión.

| | ActiveX | Native Messaging |
| --- | --- | --- |
| Navegadores | Motor de Internet Explorer | Chrome, Edge y Firefox |
| Qué se instala | Una DLL registrada en COM | Un programa, su registro y una extensión |
| Llamadas | Síncronas: la página espera | Asíncronas: promesas |
| Estado | Conexión abierta entre llamadas | Cada petición es independiente |
| Quién puede usarlo | Cualquier página en modo IE (seguro para scripting) | Solo las páginas de la lista de la extensión |

## 2. Restricciones de uso

El ejemplo tiene pocas restricciones, pero conviene conocerlas todas antes de escribir una línea. Este apartado las reúne.

### Navegador mínimo

| Navegador | Versión mínima | Por qué |
| --- | --- | --- |
| Chrome | 105 | Desde esa versión, la conexión abierta con el programa mantiene despierto el guion de fondo de la extensión mientras el usuario escribe el PIN. |
| Edge | 105 | Mismo motor que Chrome. |
| Firefox | 128 | Es la que declara `strict_min_version` en `extension/firefox/manifest.json`. |

En la práctica, cualquier versión actual sirve: los tres navegadores se actualizan solos.

### Compatibilidad por fabricante

| Fabricante | Navegador | ¿Utilizable? | Observaciones |
| --- | --- | --- | --- |
| Google | Chrome para Windows | **Sí** | Con la extensión de la carpeta `extension\chromium`. |
| Microsoft | Edge para Windows, en modo normal | **Sí** | Con la misma extensión que Chrome. |
| Mozilla | Firefox para Windows | **Sí** | Con la extensión de la carpeta `extension\firefox`. |
| Microsoft | Edge en modo Internet Explorer | **No** | En modo IE las extensiones no actúan. Use el ActiveX del DNIe. |
| Microsoft | Internet Explorer 11 | **No** | No tiene este tipo de extensiones. Use el ActiveX del DNIe. |
| Otros | Opera, Brave, Vivaldi y demás navegadores basados en Chromium | No previsto | Tienen Native Messaging, pero `scripts\registrar.cmd` no escribe sus claves del registro y el ejemplo no se ha probado en ellos. |
| Apple | Safari | **No** | El programa solo existe para Windows. |
| Cualquiera | Navegadores de Android e iOS | **No** | No admiten Native Messaging con un programa de Windows. |
| Cualquiera | Chrome, Edge y Firefox en macOS y Linux | **No** | El navegador tiene Native Messaging, pero este programa es de Windows. Ver "Ideas de ampliación" en el apartado 12. |

### Condiciones en Chrome, Edge y Firefox

Que el navegador sea compatible no basta. Además:

- **El programa debe estar ya registrado en el equipo.** Ni la página ni la extensión pueden descargarlo o instalarlo (ver el apartado 9).
- **La extensión debe estar instalada y activa.** En este ejemplo se carga a mano, en modo de desarrollador. Para los usuarios se distribuye por la tienda del navegador o por directivas de empresa (ver el apartado 9).
- **La página debe servirse desde un origen de la lista de la extensión.** La del ejemplo solo contiene `http://localhost:8081`. Una página abierta con doble clic (`file://…`) no funciona.
- **Ventanas privadas.** En las ventanas de incógnito de Chrome, InPrivate de Edge y privadas de Firefox, las extensiones no actúan si el usuario no lo permite en la ficha de la extensión.
- **Equipos administrados.** Una organización puede bloquear por directiva las extensiones o los anfitriones registrados por el usuario. En ese caso, el administrador tiene que autorizar la extensión y registrar el programa para todo el equipo (ver el apartado 9).

### Otras restricciones

- **Sistema operativo.** Solo Windows 10 u 11.
- **Arquitectura.** Hay programa para x86, x64 y ARM64. Cada programa carga el driver del DNIe de su misma arquitectura (`DNIe_P11.dll`, `DNIe_P11_x64.dll` o `DNIe_P11_arm64.dll`). La arquitectura del programa no tiene que coincidir con la del navegador: es un proceso aparte.
- **Tarjeta.** Hacen falta un lector, el DNIe y el driver PKCS#11 del DNIe de la FNMT-RCM (2.0.0.13 o posterior).
- **Qué firma.** Un texto de hasta 64 KB, un resumen SHA-256 o un archivo de cualquier tamaño (el programa calcula su resumen). La firma es en bruto, no CAdES, XAdES ni PAdES.
- **Tamaño de las respuestas.** El navegador no acepta mensajes del programa de más de 1 MB. La respuesta más grande, con la foto, la firma manuscrita y los certificados, ocupa unas decenas de KB.
- **Una operación cada vez.** La extensión atiende las peticiones en orden, de una en una (ver el apartado 6).
- **Seguridad.** La extensión confía en las páginas de su lista de orígenes. Incluya solo orígenes que usted controle (ver el apartado 14).

Fuera de un navegador no hay restricción de programa. Puede usar `DNIeDatos.exe` cualquier programa capaz de arrancarlo y de hablarle por la entrada y la salida estándar, como hace `scripts\probar.ps1`.

## 3. Puesta en marcha en diez minutos

### Requisitos

- Windows 10 u 11.
- Chrome, Edge o Firefox.
- Para compilar: Visual Studio 2022 o sus *Build Tools*, con las herramientas de C++ y el SDK de Windows. No hace falta ATL, MFC ni el driver: las cabeceras PKCS#11 van en el proyecto (`anfitrion\pkcs11`).
- Para usarlo: un lector de tarjetas, un DNIe y el driver PKCS#11 del DNIe instalado.

Si no tiene Visual Studio, las *Build Tools* se instalan con esta orden. Descarga varios GB y pide permisos de administrador:

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

### Pasos

```bat
scripts\compilar.cmd      :: genera bin\<plataforma>\Release\DNIeDatos.exe
scripts\registrar.cmd     :: da a conocer el programa a Chrome, Edge y Firefox
web\servir.cmd            :: sirve la página de ejemplo y la abre
```

Ninguno necesita permisos de administrador. Entre el segundo y el tercero hay que cargar la extensión en el navegador, que es el único paso manual.

**Paso 1. Compilar.** `scripts\compilar.cmd` genera el programa para x86, x64 y ARM64. ARM64 solo si están instaladas sus herramientas de C++ ("MSVC v143: herramientas de compilación de C++ para ARM64/ARM64EC", en "Componentes individuales" del instalador de Visual Studio); si no, la omite con un aviso. Para una sola plataforma: `scripts\compilar.cmd x64`.

**Paso 2. Registrar el programa.** `scripts\registrar.cmd` escribe las claves del registro de Chrome, Edge y Firefox para el usuario actual. Elige el `.exe` del procesador del equipo; para forzar otro: `scripts\registrar.cmd x64`. Para deshacerlo: `scripts\desregistrar.cmd`. Tras recompilar no hace falta registrar de nuevo; si mueve la carpeta del proyecto, sí.

**Paso 3. Cargar la extensión.**

| Navegador | Dónde | Qué elegir |
| --- | --- | --- |
| Chrome | `chrome://extensions` → activar "Modo de desarrollador" → "Cargar descomprimida" | La carpeta `extension\chromium` |
| Edge | `edge://extensions` → activar "Modo de desarrollador" → "Cargar desempaquetada" | La carpeta `extension\chromium` |
| Firefox | `about:debugging#/runtime/this-firefox` → "Cargar complemento temporal…" | El archivo `extension\firefox\manifest.json` |

En Chrome y en Edge, el identificador que aparece bajo el nombre de la extensión debe ser `ieggaeldkkiddkjmlhlankaheebigmeb`. Es siempre el mismo, en cualquier equipo, porque lo fija el campo `key` de `manifest.json` (ver el apartado 12). En Firefox hay dos cosas más: la extensión no actúa en las páginas hasta que se le da permiso (icono de extensiones → la extensión → «Permitir siempre en localhost», o `about:addons` → la extensión → «Permisos»), y un complemento temporal se descarga al cerrar el navegador, así que hay que cargarlo de nuevo en cada sesión.

**Paso 4. Abrir la página de ejemplo.** `web\servir.cmd` arranca un servidor web mínimo en `http://localhost:8081/` y abre la página en el navegador predeterminado. Si su navegador predeterminado no es el que tiene la extensión, copie la dirección en el otro. Para detener el servidor, pulse Ctrl+C en su ventana.

### La página de ejemplo

Arriba, una línea de estado dice si se detecta la extensión y qué DNIe hay en el lector. Debajo, una pestaña por grupo de funciones:

| Pestaña | Qué hace | Ventanas |
| --- | --- | --- |
| Chip | Versión, número de serie, versiones del chip, del programa y del driver. "Leer todo" lee datos, MRZ, imágenes y certificados con un solo permiso. | Ninguna (permiso con "Leer todo") |
| Datos personales | Datos del DG13 y MRZ | Permiso; PIN con el DNIe 3.0 |
| Imágenes | Foto y firma manuscrita | Permiso; PIN con el DNIe 3.0 |
| Certificados | Certificados (permiso) y claves públicas (sin permiso) | Permiso para los certificados |
| Firma | Firma y verificación de un texto, y su SHA-256 | Confirmación y PIN para firmar |
| Firmar archivo | Firma de un archivo elegido en el diálogo "Abrir" de Windows | "Abrir" y PIN |

Si cambia el DNIe del lector entre dos operaciones, la página borra todo lo que mostraba del anterior.

Para ver la foto y la firma manuscrita, que vienen en JPEG 2000, la página usa el decodificador `openjpeg.js` de su misma carpeta (los navegadores no muestran JPEG 2000 por sí solos).

### Comprobar que funciona sin navegador

```bat
powershell -ExecutionPolicy Bypass -File scripts\probar.ps1
```

Arranca el programa, le envía `{"operacion":"version"}` igual que haría el navegador y muestra la respuesta. Sin tarjeta, la versión llega vacía, y la prueba es igualmente válida. Si aquí funciona y en el navegador no, el problema está en el registro, el manifiesto o la extensión (ver el apartado 13).

## 4. Importar y compilar en Visual Studio Code

Visual Studio Code sirve para editar, compilar y depurar el proyecto, pero no trae compilador: usa el de Visual Studio 2022 o el de sus *Build Tools*.

### Qué hace falta

- Visual Studio Code.
- Visual Studio 2022 o sus *Build Tools*, con las herramientas de C++ (apartado 3).
- La extensión **C/C++** de Microsoft (`ms-vscode.cpptools`). VS Code la propone al abrir la carpeta.

### Abrir el proyecto

"Archivo" → "Abrir carpeta…" y elija la carpeta raíz del proyecto (la que contiene `DNIeDatos.sln`). La configuración está en la carpeta `.vscode`.

### Compilar

Ctrl+Mayús+B lanza la tarea por defecto, "Compilar (Release, x86, x64 y ARM64)", que ejecuta `scripts\compilar.cmd`. Los errores aparecen en el panel "Problemas", con enlace a la línea.

Las demás tareas están en "Terminal" → "Ejecutar tarea…":

| Tarea | Qué hace |
| --- | --- |
| Compilar solo x64 | `scripts\compilar.cmd x64` |
| Registrar el programa en los navegadores | `scripts\registrar.cmd` |
| Desregistrar el programa | `scripts\desregistrar.cmd` |
| Probar el programa sin navegador | `scripts\probar.ps1` con la operación `informacion` |
| Servir la página de ejemplo | `web\servir.cmd` |

### Autocompletado

`c_cpp_properties.json` indica a la extensión C/C++ dónde están las cabeceras y con qué macros se compila. No interviene en la compilación.

### Depurar

El navegador arranca `DNIeDatos.exe` en cada petición y lo cierra al recibir la respuesta, así que no se arranca con F5 como un programa normal: hay que **adjuntarse** a él mientras vive. El momento cómodo es cuando muestra una ventana:

1. Ponga los puntos de interrupción (por ejemplo, en `Operaciones.cpp`, después de la ventana).
2. En la página de ejemplo, lance una operación con ventana (leer datos, firmar…).
3. Con la ventana abierta, pulse F5, elija "Adjuntar a DNIeDatos.exe" y el proceso `DNIeDatos.exe` en la lista.
4. Conteste a la ventana: el programa se detendrá en el punto.

El `.exe` de Release está optimizado: al avanzar paso a paso, el depurador puede saltarse líneas. Los símbolos (`.pdb`) se generan junto al `.exe`.

### Los archivos de la carpeta .vscode

| Archivo | Para qué |
| --- | --- |
| `tasks.json` | Las tareas de compilar, registrar, probar y servir |
| `launch.json` | La configuración de depuración (adjuntarse al proceso) |
| `c_cpp_properties.json` | Autocompletado y análisis del código |
| `extensions.json` | Recomienda instalar la extensión C/C++ |

## 5. Importar y compilar en Visual Studio

### Qué hace falta

Visual Studio 2022 (cualquier edición) con la carga de trabajo "Desarrollo para el escritorio con C++". Para ARM64, además, el componente "MSVC v143: herramientas de compilación de C++ para ARM64/ARM64EC".

### Abrir la solución

Abra `DNIeDatos.sln`, en la raíz del proyecto. Contiene un solo proyecto, `anfitrion\DNIeDatos.vcxproj`.

### Compilar

Elija la configuración (`Release`) y la plataforma (`x86`, `x64` o `ARM64`) en la barra de herramientas y pulse "Compilar" → "Compilar solución". El resultado queda en `bin\<plataforma>\Release\DNIeDatos.exe` (para x86, la carpeta es `bin\Win32`).

La configuración del proyecto que conviene conocer:

| Opción | Valor | Por qué |
| --- | --- | --- |
| Subsistema | Windows | El programa no abre una ventana de consola cuando lo arranca el navegador |
| Biblioteca de C++ | Estática (`/MT`) | El `.exe` no necesita el *Visual C++ Redistributable* |
| `/utf-8` | Sí | Los fuentes están en UTF-8 |
| Reconocimiento de PPP | Sí | Las ventanas se ven nítidas con la pantalla al 125 % o 150 % |
| `/PDBALTPATH:%_PDB%` | Sí | El `.exe` no guarda la carpeta (ni el usuario) del equipo donde se compiló |

### Depurar el programa

"Depurar" → "Asociar al proceso…" y elija `DNIeDatos.exe` mientras muestra una ventana, como se explica en el apartado 4. Para depurar desde el principio de una petición, use `scripts\probar.ps1` con una operación que abra una ventana y asóciese entonces.

### Depurar la extensión

| Navegador | Guion de fondo (`fondo.js`) | Guion de contenido (`contenido.js`) |
| --- | --- | --- |
| Chrome y Edge | Página de extensiones → la extensión → "service worker" | Herramientas de desarrollo de la página (F12) → "Fuentes" → "Scripts de contenido" |
| Firefox | `about:debugging` → la extensión → "Inspeccionar" | Herramientas de desarrollo de la página (F12) |

Después de cambiar un archivo de la extensión, pulse "Recargar" en su ficha y recargue la página.

## 6. Usarlo en su página

### El ejemplo mínimo

```html
<script src="dniedatos.js"></script>
<button id="leer">Rellenar con mi DNIe</button>
<script>
document.getElementById("leer").addEventListener("click", async function () {
    try {
        const r = await DNIeDatos.datos();
        document.getElementById("nombre").value = r.datos.nombre;
        document.getElementById("apellidos").value = r.datos.apellido1 + " " + r.datos.apellido2;
        document.getElementById("dni").value = r.datos.numero;
    } catch (error) {
        if (error.codigo !== "cancelado") {
            alert(error.message);
        }
    }
});
</script>
```

Y para firmar:

```js
const r = await DNIeDatos.firmar({ texto: textoDelServidor });
// r.firma, r.algoritmo, r.hash, r.certificado: todo listo para enviar al servidor (apartado 7)
```

### Seis cosas que conviene saber

1. **Todo es asíncrono.** Cada función devuelve una promesa: use `await` o `.then()`. La página no se bloquea mientras la tarjeta trabaja o el usuario escribe el PIN.
2. **Cancelar no es un error.** Si el usuario cancela el PIN o no da permiso, la promesa se rechaza con `codigo: "cancelado"`. No muestre un mensaje de error en ese caso.
3. **Lea todo de una vez.** `DNIeDatos.leer(["datos", "imagenes"])` pide permiso una sola vez. `datos()` seguido de `imagenes()` lo pide dos veces, porque cada petición es independiente.
4. **El PIN se pide en cada firma.** El programa no guarda el PIN entre peticiones: cada `firmar()` pide confirmación y PIN.
5. **Una operación cada vez.** Si la página pide una operación con ventanas mientras otra espera al usuario, la segunda se rechaza con `codigo: "ocupado"`. Las que no abren ventanas esperan su turno.
6. **Los datos binarios van en Base64.** Firmas, certificados, imágenes y claves. `DNIeDatos.base64ABytes()` los convierte en `Uint8Array`, y `DNIeDatos.hex()`, en hexadecimal para mostrarlos.

### La primera visita de un usuario

Antes de nada, la página debe saber si el usuario tiene lo necesario. Hay tres situaciones, y cada una tiene su código:

```js
async function Preparado() {
    if (!DNIeDatos.extensionDisponible()) {
        return "Instale la extensión del DNIe: <enlace a la tienda de su navegador>";
    }
    try {
        const r = await DNIeDatos.version();
        return r.version ? "" : "Inserte su DNIe en el lector.";
    } catch (e) {
        if (e.codigo === "sin_anfitrion") return "Instale el programa del DNIe: <enlace al instalador>";
        if (e.codigo === "sin_driver") return "Instale el driver del DNIe.";
        return e.message;
    }
}
```

`version()` no muestra ninguna ventana, así que se puede llamar al cargar la página. Cada llamada arranca el programa: no la repita en un bucle.

### El cambio de tarjeta

Cada respuesta que toca la tarjeta trae su número de serie (`serie`). Si el número cambia entre dos respuestas, el usuario ha puesto otro DNIe: borre lo que muestre del anterior para no mezclar datos de dos personas. Es lo que hace `RevisarSerie` en la página de ejemplo.

### Sobre el JavaScript

`dniedatos.js` está escrito con funciones y `var` para que sirva en cualquier página, sin herramientas de compilación. La página de ejemplo usa JavaScript actual (`async`/`await`, funciones flecha), que entienden los tres navegadores.

## 7. Llevar los datos y la firma al servidor

El programa trabaja en el equipo del usuario. En una aplicación web, lo normal es que los datos o la firma acaben en el servidor. Este apartado explica cómo enviarlos y qué puede comprobar el servidor.

### Lo primero: el servidor no puede fiarse de la página

La página se ejecuta en un equipo que usted no controla, y lo que envía se puede falsear. Eso tiene dos consecuencias:

- **Los datos personales que lee la página no prueban la identidad de nadie.** Sirven para rellenar un formulario y ahorrar trabajo al usuario, pero el servidor no puede comprobar que vienen de un DNIe auténtico. Si necesita saber quién es el usuario, use su certificado: la autenticación con certificado de cliente del servidor web, o una firma que el servidor verifique (más abajo).
- **La verificación del navegador no vale para el servidor.** Que `verificar()` diga `valida: true` en el equipo del usuario no demuestra nada al servidor. El servidor tiene que verificar la firma por su cuenta.

### El recorrido completo de una firma

1. La página pide al servidor un texto que firmar. Si cambia en cada petición (por ejemplo, con un número aleatorio y la fecha), una firma antigua no sirve para hacerse pasar por el usuario.
2. La página firma el texto con `firmar({ texto })`. El programa enseña el texto, pide confirmación y el PIN.
3. La página envía al servidor el texto, la firma y el certificado.
4. El servidor comprueba que el texto es el que dio, verifica la firma con el certificado, comprueba que el certificado encadena con la raíz del DNIe y comprueba quién ha firmado, por su NIF.

### Enviar la firma

`firmar()` devuelve todo lo necesario, ya en Base64:

```js
const r = await DNIeDatos.firmar({ texto: textoDelServidor });
await fetch("/firmas", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ texto: textoDelServidor, firma: r.firma, certificado: r.certificado })
});
```

El programa firma el SHA-256 del texto **codificado en UTF-8**, que es como lo envía `fetch`. El servidor debe calcular el resumen sobre esos mismos bytes.

Para firmar un documento que está en el servidor sin enviarlo a la página, el servidor calcula su SHA-256 y la página firma el resumen: `firmar({ hash: resumenEnBase64 })`. En ese caso el programa avisa al usuario de que no puede enseñarle el documento.

### Verificar la firma en el servidor

La firma es "en bruto": RSA PKCS#1 v1.5 con SHA-256 en el DNIe 3.0, y ECDSA P-256 con SHA-256, en formato `r‖s` (64 bytes), en el DNIe 4.0. El campo `algoritmo` de la respuesta lo indica. El servidor debe comprobar:

1. Que la firma corresponde al texto y a la clave pública del certificado.
2. Que el certificado encadena con la raíz del DNIe.

**Con .NET** (.NET 8 o posterior):

```csharp
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

static bool VerificarFirma(byte[] texto, byte[] firma, X509Certificate2 certificado,
                           X509Certificate2 raizDnie)
{
    // 1. ¿La firma corresponde al texto?
    bool correcta;
    using (ECDsa? ec = certificado.GetECDsaPublicKey())
    using (RSA? rsa = certificado.GetRSAPublicKey())
    {
        if (ec != null)        // DNIe 4.0: r‖s, que es el formato por defecto de .NET
            correcta = ec.VerifyData(texto, firma, HashAlgorithmName.SHA256);
        else if (rsa != null)  // DNIe 3.0
            correcta = rsa.VerifyData(texto, firma, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        else
            correcta = false;
    }
    if (!correcta) { return false; }

    // 2. ¿El certificado encadena con la raíz del DNIe?
    var cadena = new X509Chain();
    cadena.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
    cadena.ChainPolicy.CustomTrustStore.Add(raizDnie);
    cadena.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;   // ver el apartado 14
    return cadena.Build(certificado);
}

// texto = Encoding.UTF8.GetBytes(textoRecibido); firma y certificado, de Convert.FromBase64String
```

Si firmó un resumen (`hash`) en vez de un texto, use `VerifyHash` en lugar de `VerifyData`, con el resumen.

El certificado de la autoridad intermedia (por ejemplo, *AC DNIE 007*) no viaja con la firma. Si la cadena no se completa, añádalo a `cadena.ChainPolicy.ExtraStore`. Está en el propio DNIe (`leer(["certificados"])`, etiqueta `CertCAIntermediaDGP`) y en el portal del DNIe.

**Con OpenSSL.** Para una firma RSA:

```
openssl dgst -sha256 -verify clave.pem -signature firma.bin texto.txt
```

Donde `clave.pem` es la clave pública del certificado (`openssl x509 -inform DER -in certificado.cer -pubkey -noout > clave.pem`) y `firma.bin` es la firma decodificada de Base64. Para ECDSA, OpenSSL espera la firma en DER y no en `r‖s`, así que hay que convertirla antes. En un servidor es más cómodo hacerlo con una biblioteca, como en el ejemplo de .NET.

**Con otros lenguajes.** En Java, `Signature.getInstance("SHA256withRSA")` o `"SHA256withECDSAinP1363Format"` verifican directamente los dos formatos. En JavaScript (Node.js o el navegador), `crypto.subtle.verify` con `{ name: "ECDSA", hash: "SHA-256" }` admite `r‖s` tal cual. Esta guía no incluye ejemplos completos para ellos.

### La raíz del DNIe

La raíz que hace falta depende de la generación del DNIe:

| DNIe | Cadena | Dónde está la raíz |
| --- | --- | --- |
| 4.0 (curva elíptica) | *AC DNIE 007* → *AC RAIZ DNIE 3* | La indica el propio certificado de *AC DNIE 007*: `http://pki.policia.es/dnie/ecc/certs/ACRaiz3.crt` |
| 3.0 y anteriores (RSA) | *AC DNIE 00x* → *AC RAIZ DNIE 2* o *AC RAIZ DNIE* | Portal oficial del DNIe (https://www.dnielectronico.es/PortalDNIe/), en "Área de descargas" → "Autoridades de Certificación y Validación" |

Antes de confiar en una raíz descargada, compare su huella con una fuente oficial. Si viene en binario (DER), conviértala a PEM:

```
openssl x509 -inform DER -in ACRaiz3.crt -out raiz-dnie.pem
```

Si su servidor recibe firmas de DNIe de distintas generaciones, añada todas las raíces a `CustomTrustStore`.

### Quién ha firmado

Identifique al firmante por su NIF, no por su nombre: dos personas pueden llamarse igual. El NIF va en el atributo `serialNumber` del titular del certificado (OID 2.5.4.5). En los DNIe recientes tiene el formato `IDCES-99999999R`: el prefijo `IDCES-` seguido del NIF. El formato puede variar de una generación del DNIe a otra, así que no dé por supuesto el prefijo.

```csharp
static string? Nif(X509Certificate2 certificado)
{
    foreach (var rdn in certificado.SubjectName.EnumerateRelativeDistinguishedNames())
        if (rdn.GetSingleElementType().Value == "2.5.4.5")   // Atributo serialNumber
            return rdn.GetSingleElementValue();               // "IDCES-99999999R"
    return null;
}
```

### Datos personales

Los datos del chip, la foto, la firma manuscrita y los certificados (con el nombre y el NIF del titular) son datos personales sujetos al RGPD. Envíe al servidor solo lo que necesite, decida cuánto tiempo lo conserva y quién puede verlo, y consúltelo con el delegado de protección de datos de su organización. La foto y la firma manuscrita merecen especial cuidado.

## 8. Referencia de la biblioteca y de los mensajes

### Funciones

Todas, salvo `extensionDisponible`, `base64ABytes` y `hex`, devuelven una promesa. El resultado es la respuesta del programa sin el campo `correcto`.

| Función | Qué hace | Ventanas | Resultado |
| --- | --- | --- | --- |
| `extensionDisponible()` | ¿Está la extensión activa en esta página? | — | `true` / `false` |
| `version()` | Qué DNIe hay en el lector | Ninguna | `version`, `serie` |
| `informacion()` | Chip y programas | Ninguna | `version`, `serie`, `tipo`, `hardware`, `firmware`, `programas` |
| `leer(que)` | Lee lo indicado: `"datos"`, `"mrz"`, `"imagenes"`, `"certificados"` | Permiso; PIN con el DNIe 3.0 (salvo solo certificados) | `serie` y lo pedido |
| `datos()`, `mrz()`, `imagenes()`, `certificados()` | `leer` con un solo elemento | Como `leer` | Como `leer` |
| `claves()` | Claves públicas de los dos certificados | Ninguna | `serie`, `claves` |
| `firmar(opciones)` | Firma un texto o un resumen | Confirmación y PIN | `firma`, `algoritmo`, `hash`, `certificado` |
| `firmarArchivo(opciones)` | Firma un archivo que elige el usuario | "Abrir" y PIN | Lo de `firmar`, más `archivo` y `tamano` |
| `verificar(opciones)` | Comprueba una firma con el certificado del DNIe | Ninguna | `valida` |
| `hash(texto)` | SHA-256 de un texto (sin tarjeta) | Ninguna | `hash` |
| `base64ABytes(texto)` | Base64 → `Uint8Array` | — | `Uint8Array` |
| `hex(bytes)` | `Uint8Array` o Base64 → hexadecimal | — | Texto |

**Opciones** de `firmar`, `firmarArchivo` y `verificar`:

| Opción | Qué es | En |
| --- | --- | --- |
| `texto` | Texto que se firma (hasta 64 KB en UTF-8). Se firma su SHA-256 | `firmar`, `verificar` |
| `hash` | Resumen SHA-256 ya calculado, en Base64 (en lugar de `texto`) | `firmar`, `verificar` |
| `uso` | `"firma"` (por defecto) o `"autenticacion"`: qué certificado y qué clave | Las tres |
| `firma` | La firma que se comprueba, en Base64 | `verificar` |

### Campos del resultado

| Campo | Contenido |
| --- | --- |
| `version` | `"4.0"`, `"3.0"` o `""` (sin DNIe en el lector) |
| `serie` | Número de serie del chip. Cambia si cambia el DNIe |
| `tipo` | `"DNI"` |
| `hardware`, `firmware` | Versiones del chip que da el driver (`"1.00"`) |
| `programas` | `[{ nombre, version }]` del programa y del driver del DNIe |
| `datos` | `nombre`, `apellido1`, `apellido2`, `numero`, `sexo`, `nacionalidad`, `fechaNacimiento`, `localidadNacimiento`, `provinciaNacimiento`, `padres`, `domicilio`, `localidadDomicilio`, `provinciaDomicilio`, `fechaCaducidad`, `soporte`. Las fechas, como las guarda el DNIe (`"DD MM AAAA"`) |
| `mrz` | Las tres líneas de la MRZ seguidas (90 caracteres) |
| `imagenes` | `foto` y `firma` en Base64, con `formatoFoto` y `formatoFirma`: `"jp2"`, `"j2k"` o `"jpg"` |
| `certificados` | `[{ etiqueta, certificado }]`, el certificado en Base64 del DER. Etiquetas: `CertAutenticacion`, `CertFirmaDigital`, `CertCAIntermediaDGP` |
| `claves` | `[{ uso, tipo, ... }]`. `tipo: "EC"`: `punto` (Base64 de `04‖X‖Y`). `tipo: "RSA"`: `modulo` y `exponente` (Base64) |
| `firma` | Base64. ECDSA: `r‖s`, 64 bytes. RSA: 256 bytes |
| `algoritmo` | `"ECDSA-SHA256"` (DNIe 4.0) o `"RSA-PKCS1-SHA256"` (DNIe 3.0) |
| `hash` | El SHA-256 firmado o calculado, en Base64 |
| `certificado` | Certificado del firmante, Base64 del DER |
| `archivo`, `tamano` | Nombre (sin carpeta) y tamaño en bytes del archivo firmado |
| `valida` | `true` si la firma corresponde a los datos y al certificado; `false` si no |

### Códigos de error

Cuando algo falla, la promesa se rechaza con un `Error`. Su `message` es la explicación en español, lista para mostrar. Su `codigo` dice qué ha pasado:

| `codigo` | Qué ha pasado | Lo pone |
| --- | --- | --- |
| `cancelado` | El usuario canceló una ventana o no dio permiso. No es un fallo | Programa |
| `sin_tarjeta` | No hay DNIe en el lector, o la tarjeta no es un DNIe | Programa |
| `pin_incorrecto` | PIN incorrecto. El DNIe descuenta un intento | Programa |
| `pin_bloqueado` | PIN bloqueado | Programa |
| `sin_driver` | No está instalado el driver del DNIe | Programa |
| `error` | Cualquier otro fallo, también parámetros incorrectos | Programa o extensión |
| `ocupado` | Ya hay una operación esperando al usuario | Extensión |
| `origen_no_permitido` | La página no está en la lista de la extensión | Extensión |
| `sin_anfitrion` | El programa no está instalado o no responde | Extensión |
| `sin_extension` | La extensión no está instalada o no está activa en la página | Biblioteca o `contenido.js` |

Si el fallo viene del driver, el error lleva además `pkcs11`, con su código en hexadecimal (por ejemplo, `"0x000000A0"`, `CKR_PIN_INCORRECT`). Sirve para diagnosticar; para decidir qué hacer, use `codigo`.

### Formato de los mensajes

**Página ↔ extensión.** La página y `contenido.js` se hablan con `window.postMessage`:

```js
// Petición (página → contenido.js)
{ canal: "dnie-datos", sentido: "peticion", id: 7, operacion: "leer", datos: { que: ["datos"] } }
// Respuesta (contenido.js → página)
{ canal: "dnie-datos", sentido: "respuesta", id: 7, respuesta: { correcto: true, serie: "...", datos: {...} } }
```

**Extensión ↔ programa.** `fondo.js` construye un mensaje nuevo con los campos comprobados y el origen de la página:

```json
{"operacion":"firmar","origen":"https://sede.ejemplo.es","texto":"Solicito el alta…","uso":"firma"}
```

El programa responde con un objeto JSON, siempre con `correcto`:

```json
{"correcto":true,"firma":"MEUCIQ…","algoritmo":"ECDSA-SHA256","hash":"n4bQ…","certificado":"MIIF…"}
{"correcto":false,"codigo":"pin_incorrecto","error":"El PIN no es correcto.","pkcs11":"0x000000A0"}
```

Por la entrada y la salida estándar, cada mensaje va precedido de su longitud en 4 bytes (ver el apartado 10).

| Operación | Campos de la petición |
| --- | --- |
| `version`, `informacion`, `claves` | — |
| `leer` | `que`: lista con `datos`, `mrz`, `imagenes` y/o `certificados` |
| `firmar` | `texto` o `hash`; `uso` (opcional) |
| `firmarArchivo` | `uso` (opcional) |
| `verificar` | `texto` o `hash`; `firma`; `uso` (opcional) |
| `hash` | `texto` |

Además, todas llevan `operacion` y `origen`, que pone la extensión.

## 9. Instalarlo en los equipos de los usuarios

`compilar.cmd` y `registrar.cmd` son para el equipo del desarrollador. Para que una página pueda usar el DNIe, cada equipo de usuario necesita cuatro cosas.

### 9.1. El programa

Copie a una carpeta del equipo `DNIeDatos.exe` y los dos manifiestos (`es.fnmt.dniedatos.chromium.json` y `es.fnmt.dniedatos.firefox.json`), los tres juntos. El `.exe` no depende de nada más.

| Equipo | Programa |
| --- | --- |
| Intel o AMD de 64 bits | `bin\x64\Release` |
| ARM de 64 bits | `bin\ARM64\Release` |
| Windows de 32 bits | `bin\Win32\Release` |

El programa carga el driver del DNIe de su misma arquitectura. En un equipo ARM64 con el driver ARM64 instalado, use el programa ARM64.

Firme el `.exe` (Authenticode) antes de distribuirlo.

### 9.2. El registro

El navegador debe saber dónde está el programa. Cada uno lo busca en su propia clave del registro, cuyo valor predeterminado es la ruta completa del manifiesto:

| Navegador | Clave | Manifiesto |
| --- | --- | --- |
| Chrome | `Software\Google\Chrome\NativeMessagingHosts\es.fnmt.dniedatos` | `es.fnmt.dniedatos.chromium.json` |
| Edge | `Software\Microsoft\Edge\NativeMessagingHosts\es.fnmt.dniedatos` | `es.fnmt.dniedatos.chromium.json` |
| Firefox | `Software\Mozilla\NativeMessagingHosts\es.fnmt.dniedatos` | `es.fnmt.dniedatos.firefox.json` |

| Alcance | Dónde se escriben las claves | ¿Administrador? |
| --- | --- | --- |
| Solo el usuario actual | `HKEY_CURRENT_USER` | No |
| Todos los usuarios del equipo | `HKEY_LOCAL_MACHINE` | Sí |

`scripts\registrar.cmd` hace el registro por usuario y sirve de modelo para un instalador. Tras registrar no hace falta reiniciar el navegador.

En el manifiesto, la ruta del `.exe` es solo su nombre (`"path": "DNIeDatos.exe"`). En Windows, los navegadores la entienden como relativa a la carpeta del manifiesto: por eso los tres archivos van juntos.

### 9.3. La extensión

"Cargar descomprimida" y los complementos temporales son para desarrollar. Para los usuarios, la extensión se distribuye:

- **publicándola en la tienda de cada navegador**: Chrome Web Store, Complementos de Microsoft Edge y addons.mozilla.org; o
- **en una organización, por directivas de empresa**, que la instalan sin intervención del usuario (ver "Instalarlo en muchos equipos").

Firefox exige además que la extensión esté firmada por Mozilla, también si no se publica en su tienda.

**La extensión que se publica es la de cada sitio.** La lista de orígenes permitidos va escrita dentro de la extensión, así que cada sitio web publica su propia copia, con su dominio en la lista y con su propia identidad (ver "Crear su propio proyecto a partir de este", en el apartado 12).

**Después de publicar, actualice el manifiesto del anfitrión.** Las tiendas de Chrome y de Edge asignan a la extensión un identificador propio, distinto en cada una. Póngalos en `allowed_origins` de `es.fnmt.dniedatos.chromium.json`, o el navegador se negará a arrancar el programa. En Firefox, el identificador es el de `browser_specific_settings.gecko.id`.

### 9.4. El driver del DNIe y un lector de tarjetas

El driver PKCS#11 del DNIe de la FNMT-RCM, en su versión para Windows, y un lector de tarjetas compatible con PC/SC. Un equipo que solo verifica firmas en el navegador necesita el driver (para leer el certificado del DNIe), pero la verificación de verdad se hace en el servidor (apartado 7).

### Instalarlo en muchos equipos

Sirve cualquier herramienta de despliegue que copie archivos y escriba en el registro: un script de inicio de sesión por directiva de grupo, Intune o un paquete MSI. Tiene que hacer lo mismo que `scripts\registrar.cmd`: copiar el `.exe` con sus dos manifiestos y escribir las tres claves.

La extensión se instala por directiva de cada navegador:

| Navegador | Directiva |
| --- | --- |
| Chrome y Edge | `ExtensionInstallForcelist`, con el identificador de la extensión y la dirección de actualización de su tienda |
| Firefox | `ExtensionSettings`, con el identificador de la extensión y la dirección de su archivo `.xpi` firmado |

Si la organización restringe los anfitriones nativos, tenga en cuenta dos directivas de Chrome y de Edge: `NativeMessagingBlocklist`, que puede bloquearlos todos salvo los de una lista de permitidos (`NativeMessagingAllowlist`), y `NativeMessagingUserLevelHosts`, que, desactivada, hace que el navegador ignore los registrados en `HKEY_CURRENT_USER`.

### Publicar la página en su servidor web

El ejemplo viene preparado para `http://localhost:8081`. En una aplicación real, la página la sirve su servidor web, y la extensión tiene que conocer su origen. Está en dos lugares, y hay que cambiar los dos, en las carpetas `chromium` y `firefox`:

1. **`fondo.js`**, la constante `ORIGENES_PERMITIDOS`. Es la comprobación que decide. Los orígenes se escriben completos y sin barra final:

    ```js
    const ORIGENES_PERMITIDOS = [
        "https://www.ejemplo.es"
    ];
    ```

2. **`manifest.json`**, el campo `matches`. Indica al navegador en qué páginas cargar `contenido.js`:

    ```json
    "matches": ["https://www.ejemplo.es/*"]
    ```

El patrón de `matches` no lleva puerto, porque Firefox no lo admite ahí. Por eso el ejemplo tiene `http://localhost/*` en `matches` y `http://localhost:8081` en `ORIGENES_PERMITIDOS`: el primero es amplio y el segundo, exacto.

Fuera de las pruebas en `localhost`, use solo orígenes `https://`.

Copie también `dniedatos.js` a su servidor, junto a su página, y `openjpeg.js` si va a mostrar la foto o la firma manuscrita. `openjpeg.js` es OpenJPEG compilado a JavaScript, con licencia BSD: si lo distribuye, incluya también su aviso (está en `AVISOS-TERCEROS.md`).

## 10. Cómo funciona por dentro

### Las piezas

| Pieza | Dónde se ejecuta | Qué hace |
| --- | --- | --- |
| `dniedatos.js` | En la página | Envía las peticiones con `window.postMessage` y convierte las respuestas en promesas |
| `contenido.js` | En la página, pero es de la extensión | Lleva los mensajes entre la página y `fondo.js`. No decide nada |
| `fondo.js` | En la extensión | Comprueba el origen y los parámetros, construye el mensaje, arranca el programa y pone las peticiones en cola |
| `DNIeDatos.exe` | En el equipo, fuera del navegador | Muestra las ventanas, habla con el driver e interpreta los datos |
| Driver PKCS#11 | Dentro de `DNIeDatos.exe` | Habla con el DNIe: canal seguro, lectura, firma |

### Por qué hacen falta tantos saltos

- **La página no puede hablar con la extensión** directamente: por eso existe `contenido.js`, que el navegador carga dentro de la página y que sí ve las dos orillas.
- **`contenido.js` no puede arrancar programas**: solo el guion de fondo tiene el permiso `nativeMessaging`.
- **El navegador no puede usar la tarjeta**: por eso existe el programa.

Cada salto es también una barrera: la página solo ve lo que `contenido.js` le devuelve, el programa solo recibe lo que `fondo.js` le construye, y solo `fondo.js` decide qué páginas pueden pedir algo.

### Qué ocurre al pulsar "Firmar"

1. La página llama a `DNIeDatos.firmar({ texto })`. `dniedatos.js` envía `{ canal, sentido: "peticion", id, operacion: "firmar", datos: { texto } }` con `postMessage`.
2. `contenido.js` lo recibe y lo reenvía a `fondo.js` con `chrome.runtime.sendMessage`.
3. `fondo.js` comprueba que el origen de la página (que pone el navegador, no la página) está en la lista, y que `texto` es una cadena de hasta 64 KB. Construye `{ operacion: "firmar", texto, origen }`.
4. `fondo.js` arranca `DNIeDatos.exe` con `connectNative` y le envía el mensaje.
5. El programa conecta con el DNIe, enseña el texto y pide confirmación, pide el PIN y lo presenta (`C_Login`), busca la clave privada de firma y firma el SHA-256 del texto (`C_Sign`). Lee el certificado asociado.
6. El programa responde `{ correcto: true, firma, algoritmo, hash, certificado }` y, al cerrar el navegador la conexión, termina. Al terminar se desconecta de la tarjeta, y con ello se olvida el PIN.
7. La respuesta vuelve por el mismo camino y la promesa se cumple.

### Cómo encuentra el navegador el programa

`connectNative("es.fnmt.dniedatos")` hace que el navegador busque ese nombre en su clave del registro (apartado 9.2). La clave apunta al manifiesto del anfitrión, que dice qué `.exe` arrancar y qué extensión puede hacerlo (`allowed_origins` en Chrome y Edge, `allowed_extensions` en Firefox). Si la extensión que llama no está en la lista, el navegador se niega.

### El formato de los mensajes con el programa

El navegador arranca el `.exe` y le habla por la entrada y la salida estándar. Cada mensaje, en los dos sentidos, es:

```
┌───────────────────┬─────────────────────────────────────┐
│ 4 bytes: longitud │ texto JSON en UTF-8 (esa longitud)  │
└───────────────────┴─────────────────────────────────────┘
```

La longitud es un entero de 32 bits sin signo, con el byte bajo primero. El programa admite peticiones de hasta 1 MB, y el navegador, respuestas de hasta 1 MB.

### El programa, en pocas palabras

`wWinMain` (en `Main.cpp`) lee la petición, la analiza (`Json.cpp`) y se la pasa a `AtenderPeticion` (`Operaciones.cpp`), que llama a la función de la operación. Cada operación crea un `Contexto` con la conexión con la tarjeta (`CTarjeta`), el origen de la página y la ventana dueña de los diálogos. Al terminar, el `Contexto` se destruye: se cierran las ventanas y se desconecta de la tarjeta.

El programa es "de ventanas" (subsistema Windows), no de consola, para que no aparezca una ventana negra cada vez que el navegador lo arranca. Sus diálogos cuelgan de una ventana invisible, siempre encima, que se crea en el centro de la ventana que el usuario tiene delante: así no quedan detrás del navegador.

### Dónde están los datos, los certificados y las claves

El driver del DNIe publica el contenido del chip como objetos PKCS#11:

| Qué | Clase PKCS#11 | Etiqueta |
| --- | --- | --- |
| Certificados | `CKO_CERTIFICATE` | `CertAutenticacion`, `CertFirmaDigital`, `CertCAIntermediaDGP` |
| Claves privadas (solo con PIN) | `CKO_PRIVATE_KEY` | `KprivAutenticacion`, `KprivFirmaDigital` (en el DNIe 4.0, con `EC` al final) |
| Grupos de datos | `CKO_DATA` | `DG1`, `DG2`, `DG7`, `DG13`… |

Las claves privadas nunca salen del chip: para firmar, se le pide a la tarjeta que firme y devuelve solo la firma.

### Qué ocurre al leer los datos personales

1. El programa conecta con el DNIe y pide permiso con una ventana que dice qué se va a leer y qué página lo pide.
2. Con el DNIe 3.0, pide el PIN: ese DNIe solo deja leer los grupos de datos con el PIN presentado.
3. Lee el objeto de datos `DG13` y lo interpreta (`GruposDatos.cpp`): es una secuencia de 16 cadenas de texto, y cada dato se identifica por su posición.
4. Si se piden varios grupos, el programa reinicia la tarjeta en el lector antes de cada lectura, para que cada una empiece con el canal seguro recién abierto. Con el PIN presentado no se reinicia por adelantado, para no perderlo.
5. Los grupos leídos se borran de la memoria del programa en cuanto se han interpretado.

### Qué ocurre dentro de la firma

`CTarjeta::Firmar` (en `Tarjeta.cpp`) lee el tipo de la clave privada. Con RSA (DNIe 3.0) firma con `CKM_RSA_PKCS` el `DigestInfo` del resumen: una cabecera DER fija que identifica SHA-256, seguida del resumen. Con curva elíptica (DNIe 4.0) firma con `CKM_ECDSA` los 32 bytes del resumen, y la tarjeta devuelve `r‖s`.

### Qué ocurre dentro de la verificación

`CTarjeta::Verificar` lee el certificado del uso pedido y comprueba la firma con su clave pública mediante las funciones criptográficas de Windows (`BCryptVerifySignature`). No necesita PIN. No comprueba la cadena de certificación ni la revocación: eso es trabajo del servidor.

## 11. Mapa del código

```
DNIeDatos.sln
anfitrion\
    Main.cpp              Punto de entrada; leer y escribir los mensajes del navegador
    Operaciones.cpp/.h    Las operaciones, los errores, el PIN y el permiso
    Tarjeta.cpp/.h        Acceso al DNIe a través del driver PKCS#11 (la clase del ActiveX)
    GruposDatos.cpp/.h    DG13 (datos personales), DG1 (MRZ), DG2 y DG7 (imágenes)
    PinDialog.cpp/.h      La ventana del PIN
    Dialogos.cpp/.h       Ventana dueña, confirmaciones (TaskDialog) y diálogo "Abrir"
    Json.cpp/.h           JSON mínimo, Base64 y conversiones de texto
    Modulo.h              g_hInstance
    stdafx.h              Cabeceras de Windows comunes
    cryptoki.h            Macros de Windows para las cabeceras PKCS#11
    pkcs11\               Cabeceras PKCS#11 estándar (v2.20)
    DNIeDatos.rc          Versión, icono y diálogo del PIN
    resource.h            Números de los recursos
    res\candado.ico       Icono
    manifiestos\          Manifiestos del anfitrión para Chrome/Edge y Firefox
    DNIeDatos.vcxproj     Proyecto
extension\
    chromium\             manifest.json, fondo.js, contenido.js (Chrome y Edge)
    firefox\              manifest.json, fondo.js, contenido.js (Firefox)
web\
    dniedatos.js          La biblioteca para las páginas
    index.html            La página de ejemplo
    openjpeg.js           Decodificador de JPEG 2000 para la foto y la firma manuscrita
                          (de terceros, licencia BSD: ver AVISOS-TERCEROS.md)
    servir.cmd / .ps1     Servidor web mínimo para probar
scripts\
    compilar.cmd          Compilar sin abrir Visual Studio
    registrar.cmd         Registrar el programa en los navegadores (usuario actual)
    desregistrar.cmd      Deshacer el registro
    probar.ps1            Hablar con el programa sin navegador
docs\
    GUIA-DESARROLLADORES.md   Esta guía
.vscode\                  Tareas, depuración y autocompletado de Visual Studio Code
AVISOS-TERCEROS.md        Avisos y licencias de los componentes de terceros
```

Por dónde empezar a leer: `fondo.js` (qué se puede pedir), `Operaciones.cpp` (qué hace cada operación) y `Tarjeta.h` (PKCS#11 en cinco ideas).

## 12. Cómo ampliarlo

### Añadir una operación

Hay que tocar tres sitios, y en este orden:

1. **El programa.** En `Operaciones.cpp`, escriba la función (por ejemplo, `OperacionNueva(Contexto& c, const ValorJson& p)`) y añádala al reparto de `AtenderPeticion`. Compruebe en ella los parámetros, aunque ya los compruebe la extensión.
2. **La extensión.** En `fondo.js`, añada un `case` a `construirMensaje` que copie solo los campos que necesita, con su tipo comprobado. Si abre ventanas, añádala a `ABREN_VENTANAS`. Copie el archivo a la otra carpeta (`chromium` y `firefox`).
3. **La biblioteca.** En `dniedatos.js`, añada una función que llame a `pedir("nueva", datos)`.

Una operación que no está en `construirMensaje` no llega nunca al programa, aunque la página la pida.

### Crear su propio proyecto a partir de este

Para publicar su propia versión, cambie su identidad para que no choque con este ejemplo ni con otros sitios:

1. **El nombre del anfitrión** (`es.fnmt.dniedatos`): en los dos manifiestos (`name` y nombre de archivo), en `NOMBRE_DEL_ANFITRION` de `fondo.js` y en `registrar.cmd`/`desregistrar.cmd`. Use un nombre propio con su dominio al revés (`es.ejemplo.dnie`).
2. **El identificador de la extensión.** En Chrome y Edge lo fija el campo `key` de `manifest.json`: el identificador se calcula a partir de esa clave pública. Para desarrollar, genere un par de claves propio (por ejemplo, `openssl genrsa 2048`), ponga la clave pública en `key` y calcule el identificador. Al publicar en una tienda, quite `key`: la tienda asigna el suyo. En Firefox, cambie `gecko.id` por uno de su dominio.
3. **`allowed_origins` y `allowed_extensions`** de los manifiestos del anfitrión, con los identificadores nuevos.
4. **El canal** (`dnie-datos`) de `contenido.js` y `dniedatos.js`, y la marca `data-dnie-datos`, si van a convivir varias extensiones en las mismas páginas.
5. **Los orígenes permitidos** (apartado 9).

No guarde nunca la clave privada de la extensión en el repositorio.

### Reutilizar solo el acceso a la tarjeta, sin navegador

`Tarjeta.cpp` y `GruposDatos.cpp` no dependen del navegador ni de las ventanas: sirven tal cual en cualquier programa de Windows en C++. `PinDialog.cpp` solo necesita el recurso del diálogo (`IDD_PIN`).

### Ideas de ampliación

- **Firma de documentos con formato** (CAdES, PAdES): el programa firmaría el resumen y el servidor montaría la firma con una biblioteca de firma.
- **macOS y Linux**: los navegadores tienen Native Messaging allí también. Habría que sustituir las ventanas de Windows y la carga del driver (`libpkcs11-dnie.so`, `libpkcs11-dnie.dylib`) y registrar los manifiestos en las carpetas de cada navegador.
- **Otros navegadores basados en Chromium** (Opera, Brave, Vivaldi): añadir sus claves del registro en `registrar.cmd`.
- **Un instalador** (MSI) que haga lo de `registrar.cmd` y firme el programa.

## 13. Pruebas

### Probar el programa sin navegador

`scripts\probar.ps1` hace lo mismo que el navegador: arranca el `.exe`, le escribe una petición y lee la respuesta. Acepta la petición como parámetro:

```bat
powershell -ExecutionPolicy Bypass -File scripts\probar.ps1
powershell -ExecutionPolicy Bypass -File scripts\probar.ps1 "{\"operacion\":\"informacion\"}"
powershell -ExecutionPolicy Bypass -File scripts\probar.ps1 "{\"operacion\":\"leer\",\"origen\":\"prueba\",\"que\":[\"datos\"]}"
```

La respuesta se muestra con sangría, y los campos largos en Base64 se acortan. Ojo: por aquí la petición no pasa por la extensión, así que no se comprueban el origen ni los parámetros.

### Qué comprobar a mano

Con un DNIe 4.0 y con un DNIe 3.0, en Chrome, Edge y Firefox:

| Prueba | Resultado esperado |
| --- | --- |
| Abrir la página sin DNIe | "Inserte el DNIe en el lector." |
| Abrir la página con DNIe | "DNIe 4.0 en el lector." (o 3.0) |
| Información | Versión, serie y versiones del programa y del driver |
| Leer datos, "No permitir" | "Operación cancelada", sin datos |
| Leer datos, "Permitir" | Datos del DNIe; con el DNIe 3.0, antes pide el PIN |
| Leer todo | Un solo permiso para todo |
| Imágenes | Foto y firma manuscrita dibujadas |
| Firmar, "Cancelar" en la confirmación o en el PIN | "Operación cancelada" |
| Firmar con PIN incorrecto | "El PIN no es correcto." (cuidado: gasta un intento) |
| Firmar y verificar | "La firma es válida." |
| Cambiar una letra del texto y verificar | "La firma NO es válida." |
| Verificar con el otro certificado | "La firma NO es válida." |
| Firmar archivo, cancelar el "Abrir" | "Operación cancelada" |
| Dos pulsaciones seguidas en "Firmar" | La segunda responde "ocupado" |
| Cambiar de DNIe y pulsar "Comprobar" | La página borra los datos del anterior |
| Servir en otro puerto | "Esta página no está autorizada" |
| Sin registrar (`desregistrar.cmd`) | "No se pudo conectar con el programa del DNIe" |
| Sin la extensión | "No se detecta la extensión" |

Las firmas hechas aquí se pueden verificar en el servidor con el código del apartado 7.

### Lo que no se puede probar sin tarjeta

Sin DNIe solo se pueden probar `version`, `informacion` y `hash`, y los errores de la extensión. El resto necesita un DNIe y su PIN.

## 14. Seguridad

Lea este apartado antes de publicar la extensión.

- **Solo las páginas de la lista pueden usar el DNIe.** El origen lo comprueba `fondo.js` con el dato que pone el navegador (no la página). Incluya solo orígenes que usted controle, y solo `https://` fuera de las pruebas. Una página de la lista con un fallo de seguridad (por ejemplo, que permita inyectar JavaScript) podría pedir lo mismo que la página legítima.
- **La extensión nunca reenvía lo que manda la página.** `fondo.js` construye un mensaje nuevo con los campos conocidos de cada operación, con su tipo comprobado. El programa vuelve a comprobarlos.
- **El PIN** lo pide siempre el programa en su propia ventana; la página no lo ve. Se borra de memoria en cuanto se presenta (`SecureZeroMemory`) y no se guarda entre peticiones. Con la casilla "Mostrar el PIN" marcada queda a la vista en pantalla: está desmarcada por defecto y es el usuario quien decide.
- **Los datos personales y los certificados** se entregan solo si el usuario lo permite. La ventana dice qué se va a leer y qué página lo pide. El permiso vale para esa petición y nada más.
- **Antes de firmar, el usuario ve lo que firma.** Si la página pide firmar un texto, el programa lo enseña; si es largo, enseña el principio y avisa de cuántos caracteres tiene en total. Los caracteres de control que cambian la dirección del texto se muestran como «�», para que la página no pueda hacer que se vea en otro orden del que se firma. Si pide firmar un resumen, avisa de que no puede enseñar el documento. Tenga en cuenta que la clave de firma del DNIe produce firmas con validez legal.
- **El archivo lo elige el usuario.** La página no puede indicar una ruta y solo recibe el nombre del archivo, no su carpeta.
- **Un programa por petición.** No queda nada en marcha entre una petición y otra: ni datos, ni permisos, ni PIN. Los grupos de datos leídos se borran de memoria al terminar con ellos.
- **El driver se carga solo desde la carpeta del sistema**, nunca desde el PATH ni desde la carpeta del programa.
- **Verifique siempre en el servidor.** La verificación en el navegador sirve para informar al usuario, no como prueba (ver el apartado 7).
- **Los datos leídos por la página no prueban la identidad** (ver el apartado 7).
- **La verificación no valida la confianza ni la revocación** del certificado. Un certificado revocado seguiría verificándose como válido.
- **Firme el programa** (Authenticode) antes de distribuirlo, y **no publique la clave privada** de la extensión.

## 15. Problemas frecuentes

### "No se detecta la extensión del DNIe"

- La extensión no está cargada o está desactivada. Revise la página de extensiones del navegador. En Firefox, un complemento temporal desaparece al cerrar el navegador.
- `web\servir.cmd` abre la página en el navegador predeterminado, y la extensión está cargada en otro. Copie la dirección en el navegador que la tiene.
- La página no se ha abierto desde `http://localhost:8081/`. Abierta como archivo (`file://…`) la extensión no actúa.
- La extensión se cargó después de abrir la página. Recargue la página.
- En Firefox, compruebe en `about:addons` → la extensión → "Permisos" que tiene acceso a `localhost`.
- Está en una ventana privada o de incógnito, donde la extensión no está permitida.

### "No se pudo conectar con el programa del DNIe"

El mensaje termina con el detalle que da el navegador, en inglés.

- *"Specified native messaging host not found"* o *"No such native application"*: falta el registro. Ejecute `scripts\registrar.cmd`. Si ha movido la carpeta del proyecto, ejecútelo de nuevo: el registro guarda la ruta completa.
- *"Access to the specified native messaging host is forbidden"*: el identificador de la extensión no coincide con el del manifiesto del anfitrión. En Chrome y Edge debe ser `ieggaeldkkiddkjmlhlankaheebigmeb`; si es otro, el campo `key` de `manifest.json` se ha modificado o eliminado.
- *"Native host has exited"* o *"Error when communicating with the native messaging host"*: el programa arranca pero no responde bien. Pruébelo con `scripts\probar.ps1`.

### "Esta página no está autorizada a usar el DNIe"

El origen de la página no está en `ORIGENES_PERMITIDOS` de `fondo.js`. Ocurre, por ejemplo, al servir la página en otro puerto. Tras cambiar la lista, recargue la extensión en la página de extensiones y después la página.

### "No está instalado el driver del DNIe en este equipo"

El programa no encuentra `DNIe_P11_x64.dll` (o la de su arquitectura) en la carpeta del sistema. Instale el driver del DNIe. En un equipo ARM64, compruebe que el programa registrado es el ARM64 y que está instalado el driver ARM64.

### "No hay ningún DNIe en el lector"

- El lector no está conectado o Windows no lo ve (Administrador de dispositivos → Lectores de tarjetas inteligentes).
- La tarjeta está mal insertada.
- Otro programa tiene la tarjeta en uso de forma exclusiva.

### Error con `"pkcs11":"0x00000005"` al usar el DNIe 4.0

Otro programa está usando el DNIe a la vez: por ejemplo, Firefox con el driver del DNIe cargado como dispositivo de seguridad, una aplicación con el ActiveX del DNIe abierta o una prueba anterior que sigue en marcha. Con el DNIe 4.0, cada programa abre su propio canal seguro con la tarjeta y no pueden convivir. Cierre los demás programas que usen el DNIe, saque la tarjeta, vuelva a insertarla y repita la operación.

### La ventana del permiso o del PIN no aparece

Puede haber quedado detrás de otra ventana. Búsquela con Alt+Tab. Mientras siga abierta, la página responde "ocupado" a otra operación con ventanas.

### No se puede pulsar "Aceptar" en la ventana del PIN

El botón se activa al llegar a la longitud mínima del PIN: 12 caracteres en el DNIe 4.0 y 8 en el 3.0. La ventana indica el intervalo y cuántos caracteres lleva escritos.

### Los datos personales del DNIe 3.0 piden el PIN

Es lo esperado: el DNIe 3.0 solo deja leer los grupos de datos con el PIN presentado.

### No se ven la foto ni la firma manuscrita

Falta `openjpeg.js` en la carpeta de la página. Cópielo allí y recárguela.

### `probar.ps1` dice "El programa ha terminado sin responder"

El programa ha recibido algo que no es una petición y ha terminado. Compruebe que la petición es JSON válido (desde `cmd`, las comillas interiores se escriben con barra: `\"operacion\"`).

### El servidor rechaza una firma que la página da por buena

- **El texto ha cambiado por el camino.** Cualquier cambio invalida la firma, también un cambio en los saltos de línea o en la codificación. El programa firma el texto en UTF-8.
- **El servidor no tiene la raíz de ese DNIe**, o tiene la de otra generación (apartado 7).
- **La firma llega en Base64.** Decodifíquela antes de verificarla.
- **Firma ECDSA con OpenSSL.** OpenSSL espera DER, no `r‖s` (apartado 7).

### `compilar.cmd` dice que ARM64 se ha omitido

Faltan las herramientas de C++ para ARM64 (apartado 3). No es un error si su equipo es x64.

### La compilación falla con el error LNK1104

El `.exe` está en uso: hay una operación a medias, con una ventana abierta. Termínela o cancélela y compile de nuevo.

### `servir.cmd` dice que no pudo usar el puerto

Otro programa usa el puerto 8081, o ya hay otro `servir.cmd` en marcha. Ciérrelo, o use otro puerto (`web\servir.cmd 9000`) y añada su origen a `ORIGENES_PERMITIDOS`.

### He cambiado `fondo.js` o `contenido.js` y no se nota

El navegador no vuelve a leer los archivos de la extensión por sí solo. Pulse "Recargar" en la ficha de la extensión y recargue después la página. Y recuerde copiar el cambio a la otra carpeta (`chromium` o `firefox`).

## 16. Glosario

Términos que aparecen en esta guía, explicados para quien empieza.

| Término | Qué es |
| --- | --- |
| Anfitrión nativo | Programa instalado en el equipo que una extensión puede arrancar con Native Messaging. Aquí, `DNIeDatos.exe`. |
| Autoridad de certificación (AC) | Entidad que emite certificados y responde de ellos. La AC raíz está en lo alto de la cadena. Las intermedias, como *AC DNIE 007*, emiten los certificados de los ciudadanos. |
| Base64 | Forma de escribir datos binarios como texto. Es como viajan firmas, certificados e imágenes en los mensajes. |
| Cadena de certificación | Secuencia que va del certificado del firmante a una AC raíz de confianza, pasando por las intermedias. |
| Certificado | Documento electrónico que une una clave pública con una persona. El DNIe tiene dos: el de autenticación y el de firma. |
| Driver PKCS#11 | DLL que ofrece la interfaz PKCS#11 de una tarjeta. La del DNIe es `DNIe_P11*.dll`. |
| DER | Codificación binaria de certificados. |
| DG (grupo de datos) | Cada bloque de información del chip: DG1 (MRZ), DG2 (foto), DG7 (firma manuscrita), DG13 (datos personales). |
| ECDSA | Algoritmo de firma con claves de curva elíptica. Es el del DNIe 4.0. |
| Extensión | Complemento del navegador. Aquí, la pieza que comprueba quién pide qué y arranca el programa. |
| Guion de contenido | Parte de la extensión que el navegador carga dentro de las páginas (`contenido.js`). |
| Guion de fondo | Parte de la extensión que vive fuera de las páginas y puede usar Native Messaging (`fondo.js`). En Chrome y Edge es un *service worker*. |
| JPEG 2000 | Formato de imagen de la foto y la firma manuscrita del chip. Los navegadores no lo muestran solos: el ejemplo usa `openjpeg.js`. |
| Manifest V3 | Versión actual del formato de las extensiones de Chrome, Edge y Firefox (`manifest.json`). |
| Manifiesto del anfitrión | Archivo JSON que dice al navegador dónde está el programa y qué extensión puede arrancarlo. |
| MRZ | Zona de lectura mecánica: las líneas de caracteres del reverso del DNI. |
| Native Messaging | Mecanismo de Chrome, Edge y Firefox para que una extensión hable con un programa del equipo por su entrada y su salida estándar. |
| NIF | Número de identificación fiscal. En el certificado del DNIe identifica al titular (ver el apartado 7). |
| Origen | Protocolo, servidor y puerto de una página: `https://www.ejemplo.es`. |
| PC/SC | Interfaz estándar de Windows para hablar con lectores de tarjetas. |
| PIN | Clave secreta del DNIe. Sin ella, la tarjeta no firma. |
| PKCS#11 | Interfaz estándar para usar tarjetas criptográficas. El driver del DNIe la ofrece, y el programa la usa. |
| Promesa | Objeto de JavaScript que representa un resultado que llegará más tarde (`then`, `await`). |
| Revocación | Anulación de un certificado antes de que caduque, por ejemplo por pérdida de la tarjeta. Se consulta por OCSP o en listas de revocación (CRL). |
| RSA | Algoritmo de firma de clave pública. Es el del DNIe 3.0. |
| SHA-256 | Algoritmo de resumen. La firma se hace sobre el resumen de los datos, no sobre los datos. |
