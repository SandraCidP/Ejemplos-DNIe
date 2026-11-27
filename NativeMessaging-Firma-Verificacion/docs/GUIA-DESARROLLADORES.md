# Guía para desarrolladores: firma con DNIe desde Chrome, Edge y Firefox (DNIe 3.0 y 4.0)

Versión 1.0 · 27 de noviembre de 2026 · FNMT-RCM

Esta guía acompaña al proyecto **Native Messaging para DNIe**: un ejemplo muy sencillo que permite a una página web **firmar un archivo con el DNI electrónico y verificar una firma** en los navegadores actuales. Nació para el DNIe 4.0, pero sirve para cualquier versión del DNIe que su middleware admita (probado con DNIe 3.0 y 4.0). Está escrita para programadores que quieran:

1. **usar el ejemplo tal cual** en sus propias páginas, o
2. **tomar el código como base** y ampliarlo.

No hace falta saber de extensiones de navegador ni de criptografía de Windows para seguirla. El código fuente está comentado paso a paso. Esta guía da la visión de conjunto y dice dónde mirar.

## Contenido

1. [Qué es y qué no es](#1-qué-es-y-qué-no-es)
2. [Restricciones de uso](#2-restricciones-de-uso)
3. [Puesta en marcha en diez minutos](#3-puesta-en-marcha-en-diez-minutos)
4. [Importar y compilar en Visual Studio Code](#4-importar-y-compilar-en-visual-studio-code)
5. [Importar y compilar en Visual Studio](#5-importar-y-compilar-en-visual-studio)
6. [Usar la firma en su página](#6-usar-la-firma-en-su-página)
7. [Llevar la firma al servidor](#7-llevar-la-firma-al-servidor)
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

Una página web vive dentro de la "caja de arena" del navegador: no puede leer archivos del disco por su cuenta ni usar una tarjeta inteligente. Antes esto se resolvía con complementos como ActiveX, que los navegadores actuales ya no admiten.

Chrome, Edge y Firefox ofrecen hoy un único camino para que una página llegue a un programa del equipo: **Native Messaging**. Una extensión del navegador puede arrancar un programa instalado (el *anfitrión nativo*) e intercambiar mensajes con él. Este proyecto es el ejemplo mínimo de ese camino, aplicado a la firma con DNIe.

```text
Página web  ──▶  Extensión del navegador  ──▶  DNIeAnfitrion.exe  ──▶  Windows / DNIe
(dniefirma.js)   (contenido.js y fondo.js)     (C++)
```

### Qué hace

- Firma un archivo del disco con el certificado de FIRMA del DNIe y guarda la firma en un archivo aparte (`documento.pdf` → `documento.pdf.p7s`).
- Entrega a la página el nombre del firmante y la firma en Base64, para que pueda enviarla a su servidor.
- Detecta el DNIe sea cual sea su versión, porque de esas diferencias se encarga el middleware, y también si sus certificados están caducados.
- Verifica que un archivo de firma corresponde a un documento y que el certificado del firmante es de confianza.
- Pide el PIN del DNIe con un cuadro de diálogo propio, que admite caracteres de cualquier tipo. El diálogo detecta la versión del DNIe y exige la longitud que le corresponde: de 8 a 16 caracteres en el DNIe 3.0 y de 12 a 16 en el 4.0. Tiene una casilla "Mostrar el PIN" para ver lo que se escribe.
- Dice a la página qué versión del DNIe hay en el lector (3.0 o 4.0), para que adapte sus textos.

**El DNIe solo se usa para firmar.** La verificación la hace Windows por software y no necesita tarjeta, lector ni PIN (ver el apartado 10).

**El archivo lo elige siempre el usuario**, en un diálogo de Windows. La página no indica ni recibe rutas del disco: solo le llega el nombre del archivo.

### Datos técnicos

| Aspecto | Valor |
| --- | --- |
| Formato de firma | PKCS#7 / CMS **separada** (*detached*), codificación DER |
| Algoritmo de resumen | SHA-256 |
| Algoritmo de firma | El de la clave del DNIe, que Windows elige solo. Por ejemplo, ECDSA con la curva P-256 en un DNIe con claves de curva elíptica |
| Certificados dentro de la firma | El del firmante y los de sus autoridades intermedias. La raíz no va dentro |
| Acceso a la tarjeta | CNG de Windows + Card Module (minidriver) del DNIe |
| Versiones del DNIe | Las que admita el middleware instalado. Reconoce la versión 3.0 y la 4.0 |
| Navegadores | Chrome, Edge y Firefox, en Windows |
| Extensión | Manifest V3. Una carpeta para Chrome y Edge y otra para Firefox |
| Programa de firma | `DNIeAnfitrion.exe`, en C++ puro, sin ATL ni MFC. Sin dependencias |
| Arquitecturas del programa | x64 y ARM64 |
| Nombre del anfitrión | `es.fnmt.dniefirma` |

**Limitación importante: hay que instalar dos cosas.** Para que una página pueda firmar, el equipo del usuario necesita la **extensión** en el navegador y el **programa de firma** registrado en Windows. La página sola no basta, y tampoco puede instalarlas ella.

El apartado 2, "Restricciones de uso", detalla la compatibilidad navegador por navegador.

**Qué no hace.** No consulta si el certificado está revocado, no añade sello de tiempo, no genera firmas CAdES/XAdES/PAdES y no firma texto ni datos en memoria, solo archivos. No funciona en macOS ni en Linux: la parte criptográfica usa las funciones de Windows. El apartado 12, "Cómo ampliarlo", indica por dónde empezar cada una de estas mejoras.

### El proyecto hermano

Este proyecto tiene un hermano, **ActiveX para DNIe**, que resuelve lo mismo para Internet Explorer 11, Edge en modo Internet Explorer y las aplicaciones HTA. Los dos comparten, sin cambios, el código que firma y verifica (`Crypto.cpp`) y el diálogo del PIN (`PinDialog.cpp`). Lo que cambia es el camino por el que la página llega a ese código.

| | ActiveX para DNIe | Native Messaging para DNIe |
| --- | --- | --- |
| Navegadores | Internet Explorer 11, Edge en modo IE, HTA | Chrome, Edge y Firefox |
| Qué se instala | Una DLL registrada | Un programa registrado y una extensión |
| Cómo la usa la página | `new ActiveXObject(...)`, llamadas bloqueantes | `DNIeFirma.firmar()`, promesas |
| Quién elige el archivo | La página pasa la ruta al control | Siempre el usuario; la página no ve rutas |

## 2. Restricciones de uso

El ejemplo tiene pocas restricciones, pero conviene conocerlas todas antes de escribir una línea. Este apartado las reúne.

### Navegador mínimo

| Navegador | Versión mínima | Por qué |
| --- | --- | --- |
| Chrome | 105 | Desde esa versión, la conexión abierta con el programa de firma mantiene despierto el guion de fondo de la extensión mientras el usuario elige el archivo y escribe el PIN. |
| Edge | 105 | Mismo motor que Chrome. |
| Firefox | 128 | Es la que declara `strict_min_version` en `extension/firefox/manifest.json`. |

En la práctica, cualquier versión actual sirve: los tres navegadores se actualizan solos.

### Compatibilidad por fabricante

| Fabricante | Navegador | ¿Utilizable? | Observaciones |
| --- | --- | --- | --- |
| Google | Chrome para Windows | **Sí** | Con la extensión de la carpeta `extension\chromium`. |
| Microsoft | Edge para Windows, en modo normal | **Sí** | Con la misma extensión que Chrome. |
| Mozilla | Firefox para Windows | **Sí** | Con la extensión de la carpeta `extension\firefox`. |
| Microsoft | Edge en modo Internet Explorer | **No** | En modo IE las extensiones no actúan. Para ese caso está el proyecto hermano, ActiveX para DNIe. |
| Microsoft | Internet Explorer 11 | **No** | No tiene este tipo de extensiones. Use el proyecto hermano. |
| Otros | Opera, Brave, Vivaldi y demás navegadores basados en Chromium | No previsto | Tienen Native Messaging, pero `scripts\registrar.cmd` no escribe sus claves del registro y el ejemplo no se ha probado en ellos. |
| Apple | Safari, cualquier versión | **No** | No existe para Windows, y el programa de firma solo existe para Windows. |
| Cualquiera | Navegadores de Android e iOS | **No** | No admiten Native Messaging con un programa de Windows. |
| Cualquiera | Chrome, Edge y Firefox en macOS y Linux | **No** | El navegador sí tiene Native Messaging, pero el programa de firma usa las funciones criptográficas de Windows. |

### Condiciones en Chrome, Edge y Firefox

Que el navegador sea compatible no basta. Además:

- **El programa de firma debe estar ya registrado en el equipo.** Ni la página ni la extensión pueden descargarlo o instalarlo (ver el apartado 9).
- **La extensión debe estar instalada y activa.** En este ejemplo se carga a mano, en modo de desarrollador. Para los usuarios se distribuye por la tienda del navegador o por directivas de empresa (ver el apartado 9).
- **La página debe servirse desde un origen de la lista de la extensión.** La del ejemplo solo contiene `http://localhost:8080`. Una página abierta con doble clic (`file://…`) no funciona.
- **Ventanas privadas.** En las ventanas de incógnito de Chrome, InPrivate de Edge y privadas de Firefox, las extensiones no actúan si el usuario no lo permite expresamente en la ficha de la extensión.
- **Equipos administrados.** Una organización puede bloquear por directiva la instalación de extensiones o los anfitriones nativos registrados por el usuario. En ese caso, el administrador tiene que autorizar la extensión y registrar el programa para todo el equipo (ver el apartado 9).
- **En Firefox, el permiso sobre el sitio.** Si la extensión está cargada y la página no la detecta, compruebe en `about:addons` que tiene acceso al sitio (ver el apartado 15).

### Otras restricciones

- **Sistema operativo.** Solo Windows 10 u 11. No hay versión para macOS, Linux, Android ni iOS.
- **Arquitectura.** Hay programa para x64 y ARM64. No hay versión x86 (32 bits de Intel o AMD). La arquitectura del programa no tiene que coincidir con la del navegador, pero sí con la del Card Module instalado (ver el apartado 9).
- **Tarjeta.** Hacen falta un lector, el DNIe y su middleware (Card Module). Sin el middleware, Windows no reconoce la tarjeta.
- **Qué firma.** Solo archivos del disco, y solo en formato PKCS#7 separado. No firma texto ni datos en memoria, y no genera firmas CAdES, XAdES ni PAdES.
- **Dónde queda la firma.** Siempre junto al documento, con el mismo nombre terminado en `.p7s`. La carpeta del documento debe admitir escritura.
- **Tamaño de la firma.** El navegador no acepta mensajes del programa de más de 1 MB. Una firma con sus certificados ocupa unos pocos KB; el programa rechaza devolver una de más de 512 KB.
- **Seguridad.** La extensión confía en las páginas de su lista de orígenes. Incluya solo orígenes que usted controle (ver el apartado 14).
- **Licencia.** MIT (archivo `LICENSE`). Permite usar, modificar y redistribuir el código, también con fines comerciales, siempre que se conserve el aviso de copyright. Se entrega sin garantía.

Fuera de un navegador no hay restricción de programa. Puede usar `DNIeAnfitrion.exe` cualquier programa capaz de arrancarlo y de hablarle por la entrada y la salida estándar, como hace `scripts\probar-anfitrion.ps1`.

## 3. Puesta en marcha en diez minutos

### Requisitos

- Windows 10 u 11.
- Chrome, Edge o Firefox.
- Para compilar: Visual Studio 2022 o sus *Build Tools*, con las herramientas de C++ y el SDK de Windows. No hace falta ATL ni MFC.
- Para firmar: lector de tarjetas, un DNIe y su middleware (Card Module) instalado.

Si no tiene Visual Studio, las *Build Tools* se instalan con esta orden. Descarga varios GB y pide permisos de administrador:

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

Sin ellas, `scripts\compilar.cmd` termina con "no se encuentra Visual Studio ni sus Build Tools (falta vswhere.exe)".

### Pasos

```bat
scripts\compilar.cmd      :: genera bin\x64\Release\DNIeAnfitrion.exe
scripts\registrar.cmd     :: da a conocer el programa a Chrome, Edge y Firefox
web\servir.cmd            :: sirve la página de ejemplo y la abre
```

Ninguno de los tres necesita permisos de administrador. Entre el segundo y el tercero hay que cargar la extensión en el navegador, que es el único paso manual.

**Paso 1. Compilar.** `scripts\compilar.cmd` genera `bin\x64\Release\DNIeAnfitrion.exe`. Compila además la plataforma **ARM64** si encuentra instaladas sus herramientas de C++. Si no las encuentra, la omite con un aviso. Para añadirlas, abra el instalador de Visual Studio y marque, en "Componentes individuales", "MSVC v143: herramientas de compilación de C++ para ARM64/ARM64EC".

En un equipo corriente (Intel o AMD de 64 bits) basta con x64.

**Paso 2. Registrar el programa.** `scripts\registrar.cmd` escribe las claves del registro de Chrome, Edge y Firefox para el usuario actual. Elige el `.exe` del procesador del equipo; para forzar otro, indíquelo: `scripts\registrar.cmd x64`. Para deshacerlo: `scripts\desregistrar.cmd`.

Tras recompilar no hace falta registrar de nuevo: el `.exe` sigue en la misma ruta. Si mueve la carpeta del proyecto, sí.

**Paso 3. Cargar la extensión.**

| Navegador | Dónde | Qué elegir |
| --- | --- | --- |
| Chrome | `chrome://extensions` → activar "Modo de desarrollador" → "Cargar descomprimida" | La carpeta `extension\chromium` |
| Edge | `edge://extensions` → activar "Modo de desarrollador" → "Cargar desempaquetada" | La carpeta `extension\chromium` |
| Firefox | `about:debugging#/runtime/this-firefox` → "Cargar complemento temporal…" | El archivo `extension\firefox\manifest.json` |

En Chrome y en Edge, el identificador que aparece bajo el nombre de la extensión debe ser `amdchagegphbcmlidmjblabggpclhggj`. Es siempre el mismo, en cualquier equipo, porque lo fija el campo `key` de `manifest.json` (ver el apartado 12).

En Firefox, un complemento temporal se descarga al cerrar el navegador: hay que cargarlo de nuevo en cada sesión. Es la forma que ofrece Firefox de probar una extensión sin publicarla.

**Paso 4. Abrir la página de ejemplo.** `web\servir.cmd` arranca un servidor web mínimo en `http://localhost:8080/` y abre la página en el navegador predeterminado. Si su navegador predeterminado no es el que tiene la extensión, copie la dirección en el otro. Para detener el servidor, pulse Ctrl+C en su ventana o ciérrela.

La página no funciona abierta con doble clic (`file://…`): la extensión solo actúa en las páginas de su lista de orígenes, y la del ejemplo contiene `http://localhost:8080`.

### La página de ejemplo

Con el DNIe en el lector, pulse **Firmar un archivo…**, elija un archivo y escriba el PIN. La firma queda junto al archivo, con el mismo nombre terminado en `.p7s`. **Verificar una firma…** pide el archivo y comprueba su firma; no necesita el DNIe.

Junto a esos dos botones hay un botón **Limpiar**, que borra el resultado de la última operación, para que el nombre de un firmante no quede a la vista de la persona siguiente.

Encima de los botones hay una **línea de estado** que dice qué se ha detectado en el lector:

| Punto | Texto | Significado |
| --- | --- | --- |
| Verde | "DNIe 4.0 detectado en el lector." (o 3.0) | Hay un DNIe y se reconoce su versión. |
| Gris | "No se detecta ningún DNIe en el lector." | La extensión y el programa funcionan, pero no hay tarjeta, o su versión no se reconoce. |
| Rojo | "No se ha podido consultar el lector." | Falta la extensión o el programa de firma. El recuadro de resultado explica el motivo. |

El título de la página nombra la versión del DNIe. Al abrirse dice "DNIe 4.0". Si detecta un DNIe 3.0 en el lector, cambia a "DNIe 3.0". Sin tarjeta, el título muestra la versión por defecto: por eso existe la línea de estado.

La página consulta el lector al abrirse y cada vez que el usuario vuelve a ella desde otra ventana o pestaña.

Estos pasos se dan desde una consola. Para trabajar desde un entorno de desarrollo, vea el apartado 4 (Visual Studio Code) o el apartado 5 (Visual Studio).

### Comprobar que funciona sin navegador

```bat
powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1
```

Arranca el programa, le envía la petición `{"operacion":"version"}` igual que haría el navegador y muestra la respuesta:

```text
Petición:  {"operacion":"version"}
Respuesta: {"correcto":true,"version":"4.0"}
```

No necesita PIN ni muestra diálogos. Sin tarjeta en el lector, la versión llega vacía (`"version":""`), y la prueba es igualmente válida. Sirve para ver el formato de los mensajes y, si algo falla, para saber si el problema está en el programa o en el navegador y la extensión (ver el apartado 13).

## 4. Importar y compilar en Visual Studio Code

Visual Studio Code es un editor: no trae compilador y no entiende por sí mismo los proyectos `.vcxproj`. Por eso aquí no se "importa" un proyecto, sino que se abre su carpeta. Para compilar, VS Code llama a los mismos scripts del apartado 3. El proyecto ya trae esa configuración hecha, en la carpeta `.vscode`.

### Qué hace falta

- Windows 10 u 11. En otro sistema la carpeta se puede abrir y leer, pero no compilar.
- Visual Studio Code.
- Visual Studio 2022 o sus *Build Tools* (ver el apartado 3). Son las que compilan.
- La extensión **C/C++** de Microsoft (`ms-vscode.cpptools`). No hace falta para compilar, pero sí para el autocompletado. VS Code propone instalarla al abrir la carpeta.

Para el JavaScript de la página y de la extensión no hace falta nada: VS Code lo entiende sin extensiones.

### Abrir el proyecto

1. En VS Code, elija **Archivo → Abrir carpeta…** y seleccione la carpeta raíz del proyecto, la que contiene `DNIeNativeMessaging.sln`. No abra la carpeta `anfitrion` ni un archivo suelto: las tareas no aparecerían.
2. Si VS Code pregunta si confía en los autores de la carpeta, responda que sí. En "modo restringido" las tareas no se ejecutan.
3. Acepte instalar la extensión recomendada cuando VS Code lo proponga.

Desde una consola es lo mismo con `code .` en la carpeta del proyecto.

### Compilar

Pulse **Ctrl+Mayús+B**. Se ejecuta la tarea por defecto, que llama a `scripts\compilar.cmd`. El resultado se ve en el panel "Terminal". Los errores y avisos del compilador aparecen además en el panel "Problemas", con enlace a la línea del código.

Las demás tareas están en **Terminal → Ejecutar tarea…**:

| Tarea | Qué hace |
| --- | --- |
| Compilar (Release) | Ejecuta `scripts\compilar.cmd`. Es la de `Ctrl+Mayús+B`. |
| Registrar el programa de firma | Ejecuta `scripts\registrar.cmd`. |
| Desregistrar el programa de firma | Ejecuta `scripts\desregistrar.cmd`. |
| Probar el programa sin navegador | Ejecuta `scripts\probar-anfitrion.ps1`. Envía la petición "version" y muestra la respuesta. |
| Servir la página de ejemplo | Ejecuta `web\servir.cmd`. La tarea sigue en marcha hasta que se detiene el servidor. |

También puede escribir esas órdenes en el terminal integrado (**Terminal → Nuevo terminal**). Las tareas no hacen otra cosa.

Antes de recompilar, termine o cancele la firma que tenga a medias. Mientras hay un diálogo abierto, el `.exe` está en uso y la compilación falla.

### Autocompletado

`.vscode\c_cpp_properties.json` indica a la extensión C/C++ dónde están las cabeceras y con qué opciones se compila. Solo sirve al editor: no interviene en la compilación.

### Depurar

El proyecto no trae configuración de depuración (`launch.json`) para VS Code. El motivo es que el programa no se arranca solo: lo arranca el navegador, que además le prepara la entrada y la salida estándar. Para depurarlo, use Visual Studio y asócielo al proceso en marcha (ver el apartado 5).

El JavaScript se depura con las herramientas del propio navegador (ver "Depurar la extensión", en el apartado 5).

### Los archivos de la carpeta .vscode

| Archivo | Para qué |
| --- | --- |
| `tasks.json` | Las tareas: compilar, registrar, desregistrar, probar el programa y servir la página. |
| `c_cpp_properties.json` | Rutas y opciones para el autocompletado. |
| `extensions.json` | La extensión que VS Code recomienda instalar. |

## 5. Importar y compilar en Visual Studio

El programa de firma es una solución nativa de Visual Studio, así que no hay nada que importar ni que convertir: se abre y se compila. Visual Studio usa el mismo `DNIeAnfitrion.vcxproj` y el mismo MSBuild que `scripts\compilar.cmd`.

La solución contiene solo el programa. La página y la extensión son archivos de texto que no se compilan.

### Qué hace falta

- Visual Studio 2022, en cualquier edición. La Community, gratuita, es suficiente.
- La carga de trabajo **"Desarrollo para el escritorio con C++"**, que incluye el compilador (conjunto de herramientas v143) y el SDK de Windows. Se elige en el instalador de Visual Studio (*Visual Studio Installer* → Modificar).
- No hacen falta ATL ni MFC.
- Para la plataforma ARM64, el componente indicado en el apartado 3.

Con una versión posterior a la 2022, Visual Studio propone al abrir la solución "redirigir" el proyecto a su propio conjunto de herramientas. Puede aceptarlo, o instalar el conjunto v143 desde el instalador y dejar el proyecto como está.

### Abrir la solución

1. Abra Visual Studio y elija **Abrir un proyecto o una solución** (o **Archivo → Abrir → Proyecto o solución…**).
2. Seleccione `DNIeNativeMessaging.sln`, en la carpeta raíz del proyecto. También vale hacer doble clic sobre ese archivo en el Explorador de Windows.
3. En el **Explorador de soluciones** aparece el proyecto `DNIeAnfitrion`, con sus archivos de código, de cabecera y de recursos.

### Compilar

1. En la barra de herramientas, elija la configuración (**Debug** o **Release**) y la plataforma (**x64** o **ARM64**).
2. Elija **Compilar → Compilar solución** (`Ctrl+Mayús+B`).
3. El resultado aparece en la ventana "Salida", y los errores, en la "Lista de errores".

El programa queda en la misma carpeta que con los scripts:

| Configuración y plataforma | Programa generado |
| --- | --- |
| Release, x64 | `bin\x64\Release\DNIeAnfitrion.exe` |
| Release, ARM64 | `bin\ARM64\Release\DNIeAnfitrion.exe` |
| Debug, x64 | `bin\x64\Debug\DNIeAnfitrion.exe` |

Para generar varias plataformas de una vez, use **Compilar → Compilación por lotes…**, marque las filas de Release que quiera y pulse **Compilar**.

`scripts\registrar.cmd` registra el programa de **Release**. Compile en Release antes de registrar.

Antes de recompilar, termine o cancele la firma que tenga a medias. Si no, el `.exe` está en uso y la compilación falla con el error LNK1104.

### Depurar el programa

El programa lo arranca el navegador, no el depurador. Para detenerse en un punto de interrupción hay que asociar el depurador al proceso cuando ya está en marcha:

1. Compile en Release para x64 y registre el programa.
2. Ponga un punto de interrupción, por ejemplo en `SignFileDetached`, en `Crypto.cpp`.
3. Lance una firma desde la página. Mientras el diálogo "Abrir" está en pantalla, elija en Visual Studio **Depurar → Asociar al proceso…** y seleccione `DNIeAnfitrion.exe`.
4. Elija el archivo en el diálogo. La ejecución se detiene en el punto de interrupción.

Se depura el programa de Release, que es el registrado. Está optimizado, así que al avanzar paso a paso el depurador puede saltarse líneas. Para depurar la versión Debug, que no está optimizada, registre antes ese programa. Copie junto a él los dos manifiestos y apunte a esa copia la clave del registro de su navegador (la de Chrome, en el ejemplo):

```bat
copy anfitrion\manifiestos\*.json bin\x64\Debug\
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\es.fnmt.dniefirma" /ve /t REG_SZ /d "%CD%\bin\x64\Debug\es.fnmt.dniefirma.chromium.json" /f
```

Al terminar, ejecute de nuevo `scripts\registrar.cmd` para dejar registrado el de Release.

La operación "version" no muestra ningún diálogo y termina en un instante: no da tiempo a asociar el depurador. Para depurarla, ponga temporalmente un `MessageBoxW` al principio de `wWinMain`, que detiene el programa hasta que se pulsa "Aceptar".

**No escriba mensajes de depuración en la salida estándar** (`printf`, `std::cout`). Es el canal de respuesta al navegador, y cualquier byte de más rompe el formato de los mensajes. Use `OutputDebugStringW`, que se ve en la ventana "Salida" del depurador.

### Depurar la extensión

El JavaScript se depura con las herramientas de desarrollo del navegador:

| Qué | Dónde |
| --- | --- |
| La página y `dniefirma.js` | F12 en la página. |
| `contenido.js` | F12 en la página. En Chrome y Edge, en "Fuentes" → "Scripts de contenido". |
| `fondo.js` en Chrome y Edge | `chrome://extensions` o `edge://extensions` → en la ficha de la extensión, el enlace "service worker". |
| `fondo.js` en Firefox | `about:debugging#/runtime/this-firefox` → botón "Inspeccionar" de la extensión. |

Después de cambiar un archivo de la extensión, pulse "Recargar" en la ficha de la extensión y recargue después la página.

## 6. Usar la firma en su página

### El ejemplo mínimo

```html
<!DOCTYPE html>
<html lang="es">
<head><meta charset="utf-8"><title>Firmar</title></head>
<body>
    <button id="firmar">Firmar un archivo</button>
    <p id="estado"></p>

    <!-- La biblioteca: crea el objeto DNIeFirma. -->
    <script src="dniefirma.js"></script>
    <script>
        document.getElementById("firmar").addEventListener("click", async function () {
            const estado = document.getElementById("estado");
            try {
                // El programa pide el archivo y el PIN por su cuenta.
                const resultado = await DNIeFirma.firmar();
                estado.textContent = "Firmado por " + resultado.firmante +
                    ". Firma: " + resultado.archivoFirma;
            } catch (error) {
                // "cancelado" no es un fallo: el usuario cerró un diálogo.
                estado.textContent = error.codigo === "cancelado" ? "Cancelado." : error.message;
            }
        });
    </script>
</body>
</html>
```

Verificar es igual de corto:

```js
try {
    const resultado = await DNIeFirma.verificar();   // Pide el archivo y lee su .p7s
    alert("Firma válida de " + resultado.firmante);
} catch (error) {
    if (error.codigo !== "cancelado") { alert(error.message); }
}
```

Para que funcione en su sitio, el origen de la página tiene que estar en la lista de la extensión (ver el apartado 9).

El ejemplo completo, con interfaz y mensajes, está en `web/index.html`.

### Seis cosas que conviene saber

**1. Los errores son promesas rechazadas.** `firmar()`, `verificar()` y `version()` devuelven una promesa. Cuando algo falla, la promesa se rechaza con un `Error`: su `message` es una explicación en español pensada para mostrársela al usuario ("El PIN introducido no es correcto…", "No se detecta el DNIe…"), y su propiedad `codigo` dice qué ha pasado. Con `await`, el fallo llega al `catch`.

Que el usuario cancele un diálogo también rechaza la promesa, con el código `cancelado`. No lo presente como un error. La lista de códigos está en el apartado 8.

**2. La página no elige el archivo.** No hay forma de pasar una ruta a `firmar()`, y `<input type="file">` no sirve para esto. Es deliberado: así una página no puede hacer que se firme, ni que se lea, un archivo que el usuario no ha señalado. El archivo se elige en el diálogo "Abrir" de Windows, y a la página solo vuelve su nombre, sin la carpeta.

Para enviar después el documento al servidor, en cambio, `<input type="file">` sí sirve (ver el apartado 7).

**3. Las operaciones tardan lo que tarde el usuario.** La promesa no termina hasta que elige el archivo y escribe el PIN. La página sigue funcionando mientras tanto: no se queda bloqueada. Escriba un "Firmando…" y desactive los botones, como hace `web/index.html`:

```js
activarBotones(false);
mostrar("", "Firmando…", "Elija el archivo e introduzca el PIN de su DNIe cuando se le pida.");
try {
    const resultado = await DNIeFirma.firmar();
    // ...
} finally {
    activarBotones(true);
}
```

**4. Una operación a la vez.** Mientras hay un diálogo de firma o de verificación abierto, una segunda petición se rechaza con el código `ocupado`. `version()` no muestra nada y se puede llamar siempre.

**5. Compruebe la instalación al cargar la página.** Una llamada a `version()` basta: si falta la extensión, el error trae el código `sin_extension`; si falta el programa de firma, `sin_anfitrion`. Así puede explicar al usuario qué tiene que instalar antes de que pulse nada. `DNIeFirma.extensionDisponible()` da la primera respuesta al momento, sin promesa. Qué hacer con esa respuesta en un sitio real se explica más abajo, en "La primera visita de un usuario".

**6. La página puede saber qué DNIe hay en el lector.** `version()` entrega `"3.0"` o `"4.0"`, y una cadena vacía si no hay ningún DNIe insertado. No pide el PIN, así que puede consultarla al cargar la página y adaptar sus textos:

```js
const version = (await DNIeFirma.version()) || "4.0";     // Sin tarjeta: versión por defecto
document.title = "Firma con DNIe " + version;
```

`web/index.html` lo hace así, y repite la consulta cada vez que la ventana recupera el foco, para que el título y la línea de estado cambien cuando el usuario inserta la tarjeta.

### La primera visita de un usuario

Quien entra por primera vez en su página no tiene nada instalado. La página se carga con normalidad, pero no puede firmar. Y **no puede instalar nada por su cuenta**: el navegador no permite que una página instale una extensión ni un programa. Lo tiene que hacer el usuario, una sola vez.

La página de ejemplo se limita a avisar ("No se detecta la extensión de firma con DNIe…"). Eso basta para un desarrollador, pero no para un usuario, que no sabe qué es "la extensión" ni de dónde se obtiene. En un sitio real, la página tiene que **decirle qué le falta y llevarle a instalarlo**.

#### Qué necesita el usuario

| Qué | De dónde lo obtiene | Cómo sabe la página que falta |
| --- | --- | --- |
| La extensión | La tienda de su navegador, donde usted la ha publicado (apartado 9.3). | Código `sin_extension`. |
| El programa de firma | Un instalador que usted ofrece para descargar, y que copia el programa y lo registra (apartados 9.1 y 9.2). | Código `sin_anfitrion`. |
| El software del DNIe y un lector | El portal oficial del DNIe (apartado 9.4). | No hay un código propio: se nota al firmar, con el mensaje "No se detecta el DNIe…". |

Este proyecto no incluye el instalador del programa: `scripts\registrar.cmd` sirve de modelo para hacerlo. Cómo publicar la extensión y cómo ofrecer el instalador se explica en "Distribuirlo al público: qué tiene que publicar usted", en el apartado 9.

#### Qué debe hacer la página

1. **Comprobar al cargar**, con `version()`, sin esperar a que el usuario pulse "Firmar".
2. **Si falta la extensión**, mostrar el enlace a la tienda del navegador con el que ha entrado. La extensión solo empieza a actuar en la página al cargarla, así que hay que pedirle que **recargue la página** después de instalarla.
3. **Si falta el programa**, mostrar el enlace de descarga del instalador. Aquí no hace falta recargar: basta con repetir la comprobación.
4. **Repetir la comprobación cuando la ventana recupera el foco.** El usuario se va a otra pestaña o a otra ventana a instalar; al volver, la página se pone al día sola.
5. **Mantener desactivado el botón de firmar** hasta que todo esté en su sitio.

Conviene reunir estas instrucciones en una página de ayuda propia ("Requisitos para firmar"), con los tres enlaces y los pasos en orden, y enlazarla desde el aviso.

#### Un ejemplo

La página tiene un botón de firmar y un párrafo para los avisos:

```html
<button id="firmar" disabled>Firmar un archivo</button>
<p id="aviso"></p>
```

Y este es el guion. Las direcciones son de ejemplo: sustitúyalas por las de su extensión en cada tienda y por la de su instalador.

```js
// Dónde se instala la extensión, según el navegador.
const TIENDAS = {
    firefox: "https://addons.mozilla.org/firefox/addon/SU-EXTENSION/",
    edge:    "https://microsoftedge.microsoft.com/addons/detail/SU-IDENTIFICADOR",
    chrome:  "https://chromewebstore.google.com/detail/SU-IDENTIFICADOR"
};
// Dónde se descarga el instalador del programa de firma.
const INSTALADOR = "https://www.example.es/descargas/InstalarFirmaDNIe.exe";

// ¿Con qué navegador ha entrado el usuario? El orden importa: Edge
// también dice "Chrome" en su identificación.
function navegador() {
    const agente = navigator.userAgent;
    if (agente.includes("Firefox")) { return "firefox"; }
    if (agente.includes("Edg/"))    { return "edge"; }
    return "chrome";
}

// Escribe un aviso con un enlace debajo.
function avisar(texto, textoDelEnlace, direccion) {
    const aviso = document.getElementById("aviso");
    aviso.textContent = texto + " ";
    if (direccion) {
        const enlace = document.createElement("a");
        enlace.href = direccion;
        enlace.target = "_blank";              // En otra pestaña: esta sigue abierta.
        enlace.textContent = textoDelEnlace;
        aviso.appendChild(enlace);
    }
}

// Comprueba qué hay instalado y deja la página en consecuencia.
async function comprobarInstalacion() {
    const boton = document.getElementById("firmar");
    try {
        await DNIeFirma.version();             // No pide el PIN ni muestra nada.
        avisar("");                            // Todo en su sitio: sin aviso.
        boton.disabled = false;
    } catch (error) {
        boton.disabled = true;
        if (error.codigo === "sin_extension") {
            avisar("Para firmar necesita la extensión de firma con DNIe. " +
                   "Instálela y recargue esta página.",
                   "Instalar la extensión", TIENDAS[navegador()]);
        } else if (error.codigo === "sin_anfitrion") {
            avisar("Ya tiene la extensión. Falta el programa de firma: " +
                   "descárguelo, ejecútelo y vuelva a esta página.",
                   "Descargar el programa de firma", INSTALADOR);
        } else {
            avisar(error.message);             // Cualquier otro fallo.
        }
    }
}

// Al cargar la página, y cada vez que el usuario vuelve a ella.
comprobarInstalacion();
window.addEventListener("focus", comprobarInstalacion);
```

El orden de los avisos sale solo: mientras falta la extensión, la página no puede saber si el programa está; cuando la extensión ya está, el error pasa a ser `sin_anfitrion`.

El código `origen_no_permitido` no es un problema del usuario, sino de configuración: su sitio no está en la lista de la extensión (apartado 9). No debería llegar a verse en producción.

### Sobre el JavaScript

La página puede usar JavaScript actual (`const`, `async`/`await`, `fetch`): los tres navegadores lo entienden. `dniefirma.js` no depende de ninguna otra biblioteca y crea un único objeto global, `DNIeFirma`.

## 7. Llevar la firma al servidor

El ejemplo firma y verifica en el equipo del usuario. En una aplicación web, lo normal es que el documento y su firma acaben en el servidor. Este apartado explica cómo enviarlos y cómo comprobar allí la firma.

### El recorrido completo

1. La página llama a `DNIeFirma.firmar()`. El usuario elige el documento y escribe el PIN.
2. El programa guarda la firma junto al documento (`documento.pdf.p7s`) y se la entrega además a la página, en Base64.
3. La página envía la firma al servidor. Si el servidor no tiene el documento, el usuario lo envía con un formulario.
4. El servidor verifica la firma y comprueba quién ha firmado, por su NIF.
5. Si todo es correcto, el servidor guarda los dos archivos.

### Enviar la firma

`firmar()` entrega la firma en el campo `firma`, así que la página puede enviarla sin que el usuario tenga que buscar el `.p7s`:

```js
const resultado = await DNIeFirma.firmar();

await fetch("/firmas", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
        archivo: resultado.archivo,
        firma: resultado.firma          // El .p7s, en Base64
    })
});
```

`/firmas` es la dirección de su servidor que recibe la firma; el ejemplo no incluye esa parte.

### Enviar el documento

La firma es separada: no contiene el documento. El servidor necesita los dos. Hay dos situaciones:

- **El documento ya está en el servidor.** Es el caso más cómodo: el usuario lo descargó de su aplicación para firmarlo. El servidor solo tiene que recibir la firma y verificarla contra su copia.
- **El documento lo aporta el usuario.** La página no puede leerlo del disco, porque no conoce su ruta. Pídalo con un campo de archivo normal, que sí envía el contenido al servidor:

```html
<form action="/documentos" method="post" enctype="multipart/form-data">
    <p>Documento: <input type="file" name="documento"></p>
    <button type="submit">Enviar</button>
</form>
```

El inconveniente del segundo caso es que el usuario elige el documento dos veces: una para firmarlo y otra para enviarlo. Muéstrele el nombre del archivo que ha firmado (`resultado.archivo`), para que elija el mismo.

### Verificar la firma en el servidor

**No dé por buena la verificación del navegador.** Que `verificar()` termine bien en el equipo del usuario no demuestra nada al servidor. La página se ejecuta en un equipo que usted no controla, y su resultado se puede falsear. El servidor tiene que verificar la firma por su cuenta.

Debe comprobar lo mismo que `verificar()`:

1. Que la firma corresponde al documento recibido.
2. Que el certificado del firmante encadena con la raíz del DNIe.

Y una tercera cosa que el ejemplo no hace: que quien ha firmado es quien debía firmar. Para eso, compare el NIF del certificado con el del usuario de su aplicación (ver "Quién ha firmado", más abajo).

El servidor necesita para ello el certificado raíz del DNIe (ver "La raíz del DNIe"). La firma trae el certificado del firmante y los de sus autoridades intermedias, pero no la raíz.

**Con OpenSSL.** En un servidor Linux, esta orden hace las dos comprobaciones. Espera la firma en binario: si la ha recibido en Base64, decodifíquela antes.

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

static bool VerificarFirma(byte[] documento, string firmaBase64, X509Certificate2 raizDnie)
{
    byte[] firma = Convert.FromBase64String(firmaBase64);   // Como la envía la página

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
        // Sin consultar la revocación, igual que el ejemplo:
        cadena.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
        if (!cadena.Build(firmante.Certificate!)) { return false; }
    }
    return true;
}
```

La raíz se carga, por ejemplo, con `X509Certificate2.CreateFromPem(File.ReadAllText("raiz-dnie.pem"))`.

**Versiones.** La orden de OpenSSL es para la versión 3.0 o posterior, y el código de .NET, para .NET 8 o posterior.

**Con otros lenguajes.** En Java, la biblioteca Bouncy Castle ofrece lo mismo. En PHP 8, la función `openssl_cms_verify`. Esta guía no incluye ejemplos para ellos.

**Con VALIDe.** La Administración General del Estado ofrece VALIDe (https://valide.redsara.es/valide/), un servicio web para validar firmas y certificados. Su página no indica si admite firmas separadas como las de este ejemplo. Compruébelo antes de recomendárselo a sus usuarios.

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

Identifique al firmante por su NIF, no por su nombre: dos personas pueden llamarse igual. El ejemplo no devuelve el NIF (`firmante` solo da el nombre), pero está en el certificado del firmante, dentro de la firma.

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

## 8. Referencia de la biblioteca y de los mensajes

La biblioteca se incluye con `<script src="dniefirma.js"></script>` y crea el objeto global `DNIeFirma`.

### Funciones

| Función | Devuelve | Descripción |
| --- | --- | --- |
| `firmar()` | Promesa de `{ archivo, archivoFirma, firmante, firma }` | Muestra el diálogo "Abrir", pide el PIN y firma. Guarda la firma junto al archivo, como `<archivo>.p7s`. Si ya existía, la sobrescribe. |
| `verificar()` | Promesa de `{ archivo, archivoFirma, firmante }` | Muestra el diálogo "Abrir" y comprueba la firma `<archivo>.p7s`. Si no está junto al archivo, pregunta por ella con un segundo diálogo. Acepta firmas en binario (DER) y en Base64. Si la firma no es válida, la promesa se rechaza. |
| `version()` | Promesa de texto | Versión del DNIe que hay en el lector en ese momento: `"3.0"` o `"4.0"`. Vacía si no hay ningún DNIe insertado o si su versión no se reconoce. No muestra nada ni pide el PIN. |
| `extensionDisponible()` | `true` / `false` | No es una promesa: responde al momento. Dice si la extensión está instalada y activa en esta página. |

### Campos del resultado

| Campo | Contenido |
| --- | --- |
| `archivo` | Nombre del documento, sin carpeta: `informe.pdf`. |
| `archivoFirma` | Nombre del archivo de firma, sin carpeta: `informe.pdf.p7s`. |
| `firmante` | Titular del certificado de la firma, o firmante de la verificación. En el DNIe tiene la forma "APELLIDOS, NOMBRE (FIRMA)". Si hay varios firmantes, van separados por `"; "`. No incluye el NIF (ver el apartado 7). |
| `firma` | Solo en `firmar()`. El contenido del `.p7s` en Base64, todo seguido, sin saltos de línea. |

### Códigos de error

Cuando algo falla, la promesa se rechaza con un `Error`. Su propiedad `codigo` permite al programa distinguir los casos sin interpretar el texto:

| `codigo` | Significado | Quién lo genera | Qué hacer |
| --- | --- | --- | --- |
| `cancelado` | El usuario cerró un diálogo (archivo, certificado o PIN). | El programa | Nada. No es un fallo. |
| `error` | No se pudo firmar, o la firma no es válida. `message` da el motivo. | El programa | Mostrar `message`. |
| `ocupado` | Ya hay una firma o una verificación en marcha. | La extensión | Esperar a que termine. |
| `sin_extension` | La extensión no está instalada, está desactivada o no actúa en esta página. | La biblioteca y la extensión | Pedir al usuario que la instale y recargue. |
| `sin_anfitrion` | La extensión no consigue arrancar el programa de firma. | La extensión | Pedir al usuario que instale el programa. |
| `origen_no_permitido` | La página no está en la lista de orígenes de la extensión. | La extensión | Corregir la lista (ver el apartado 9). |

### Qué comprueba exactamente `verificar()`

1. Que el archivo de firma es un PKCS#7 firmado.
2. Que la firma de cada firmante corresponde al contenido del documento. Si el documento ha cambiado un solo bit, falla.
3. Que el certificado de cada firmante encadena con una autoridad raíz instalada en Windows y está dentro de su periodo de validez **en la fecha actual**.

No comprueba la revocación del certificado (ver el apartado 14, "Seguridad").

La tercera comprobación no se puede desactivar desde la página. `Main.cpp` llama a `VerifyFileDetached` con `checkTrust = true`; el apartado 12 indica cómo ofrecer la otra posibilidad.

### Mensajes de error

`message` está pensado para mostrárselo al usuario tal cual. Muchos mensajes tienen tres partes: qué se estaba haciendo, la explicación y el código de error de Windows. Por ejemplo: "La tarjeta no aceptó el PIN: El PIN introducido no es correcto. Atención: varios intentos fallidos seguidos bloquean el DNIe. (0x8010006B)".

Por eso la tabla da un fragmento de cada mensaje, y no su comienzo.

| `message` contiene… | `codigo` | Qué ha pasado |
| --- | --- | --- |
| "No se detecta la extensión de firma con DNIe…" | `sin_extension` | La extensión no está cargada o no actúa en esta página. |
| "La extensión de firma se ha actualizado. Recargue la página." | `sin_extension` | La extensión se recargó después de abrir la página. |
| "No se pudo conectar con el programa de firma…" | `sin_anfitrion` | El programa no está registrado, no existe en la ruta registrada o ha terminado antes de responder. El mensaje acaba con el detalle que da el navegador, en inglés (ver el apartado 15). |
| "Esta página (…) no está autorizada a firmar con el DNIe." | `origen_no_permitido` | El origen de la página no está en `ORIGENES_PERMITIDOS`. |
| "Ya hay una operación de firma en curso…" | `ocupado` | Hay un diálogo de firma o de verificación abierto. |
| "Operación cancelada…" | `cancelado` | El usuario cerró el diálogo "Abrir", el del PIN o el de selección de certificado. |
| "No se pudo abrir el archivo…" | `error` | El documento o el archivo de firma no se puede leer. |
| "No se detecta el DNIe…" | `error` | No hay ningún certificado de firma de DNIe en el almacén personal: nunca se ha insertado la tarjeta en ese equipo, o falta el lector o el Card Module. También aparece si se retira la tarjeta durante la firma. |
| "El PIN introducido no es correcto…" | `error` | La tarjeta rechazó el PIN. Varios fallos seguidos bloquean el DNIe. |
| "El PIN del DNIe está bloqueado…" | `error` | Se han agotado los intentos. El propio mensaje indica que se desbloquea en un Punto de Actualización del DNIe. |
| "No se pudo crear el archivo de firma…" | `error` | La carpeta del documento no admite escritura (ver el apartado 15). |
| "El archivo se ha firmado, pero no se pudo leer el archivo de firma." | `error` | El `.p7s` se ha guardado, pero no se ha podido entregar a la página: no se puede leer o pasa de 512 KB. |
| "…está vacío o es demasiado grande para ser una firma." | `error` | El archivo de firma está vacío o pasa de 16 MB. |
| "…no contiene una firma PKCS#7 válida." | `error` | El archivo indicado como firma no es una firma. |
| "…no contiene ningún firmante." o "…no incluye el certificado del firmante…" | `error` | La firma no la ha generado este ejemplo, que siempre incluye el firmante y su certificado. |
| "La firma NO es válida…" | `error` | El documento se modificó después de firmarse, o la firma es de otro documento. |
| "…no es de confianza…" | `error` | La firma es correcta, pero Windows no confía en quien firma: falta la autoridad raíz, o el certificado ha caducado. |

### Formato de los mensajes

Hay tres tramos, y en cada uno el mensaje se reduce a lo imprescindible. Solo hace falta conocerlos para ampliar el ejemplo o para depurarlo.

**Tramo 1: página ↔ guion de contenido (`window.postMessage`)**

Petición:

```json
{ "canal": "dnie-firma", "sentido": "peticion", "id": 7, "operacion": "firmar" }
```

Respuesta:

```json
{ "canal": "dnie-firma", "sentido": "respuesta", "id": 7,
  "respuesta": { "correcto": true, "archivo": "informe.pdf", "…": "…" } }
```

| Campo | Para qué sirve |
| --- | --- |
| `canal` | Distingue estos mensajes de otros que circulen por la página. Siempre `"dnie-firma"`. |
| `sentido` | `"peticion"` o `"respuesta"`. Evita que la biblioteca tome por respuesta su propia petición. |
| `id` | Número que pone la biblioteca. Vuelve igual en la respuesta, para emparejarlas. |
| `operacion` | `"version"`, `"firmar"` o `"verificar"`. |
| `respuesta` | La respuesta del programa de firma (tramo 3), sin cambios. |

**Tramo 2: guion de contenido ↔ guion de fondo (`chrome.runtime.sendMessage`)**

Petición:

```json
{ "operacion": "firmar" }
```

La respuesta es la del tramo 3. El origen de la página no viaja en el mensaje: lo aporta el navegador, que es de quien `fondo.js` se fía.

**Tramo 3: guion de fondo ↔ programa de firma (Native Messaging)**

Cada mensaje, en los dos sentidos, es un bloque binario:

```text
┌───────────────────┬──────────────────────────────────────┐
│ 4 bytes: longitud │ texto JSON en UTF-8 (de esa longitud)│
└───────────────────┴──────────────────────────────────────┘
```

La longitud es un entero de 32 bits sin signo, con el byte bajo primero. El navegador admite hasta 1 MB por mensaje del programa. El programa no acepta peticiones de más de 64 KB.

Petición:

```json
{ "operacion": "firmar" }
```

Respuestas correctas, de `version`, de `firmar` y de `verificar`:

```json
{ "correcto": true, "version": "4.0" }
```

```json
{ "correcto": true, "archivo": "informe.pdf", "archivoFirma": "informe.pdf.p7s",
  "firmante": "…", "firma": "MIIG…" }
```

```json
{ "correcto": true, "archivo": "informe.pdf", "archivoFirma": "informe.pdf.p7s",
  "firmante": "…" }
```

Respuesta de fallo:

```json
{ "correcto": false, "codigo": "cancelado", "error": "Operación cancelada: no se seleccionó ningún archivo." }
```

El programa solo emite los códigos `cancelado` y `error`. Los demás los generan la extensión y la biblioteca, con la misma forma.

## 9. Instalarlo en los equipos de los usuarios

`compilar.cmd` y `registrar.cmd` son para el equipo del desarrollador. Para que una página pueda firmar, cada equipo de usuario necesita cuatro cosas.

### 9.1. El programa

Copie a una carpeta del equipo `DNIeAnfitrion.exe` y los dos manifiestos (`es.fnmt.dniefirma.chromium.json` y `es.fnmt.dniefirma.firefox.json`), los tres juntos. El `.exe` no depende de nada más: no hay que instalar el *Visual C++ Redistributable*.

Qué `.exe` copiar:

| Equipo | Programa |
| --- | --- |
| Intel o AMD de 64 bits | `bin\x64\Release` |
| ARM de 64 bits | `bin\ARM64\Release` |

El programa es un proceso aparte, así que su arquitectura no tiene que coincidir con la del navegador: un navegador de 32 bits en un Windows de 64 usa sin problema el programa x64. Con lo que sí debe coincidir es con el **Card Module del DNIe** instalado, porque Windows lo carga dentro del programa. En un equipo ARM64, si el middleware instalado no tiene versión ARM64, pruebe con el programa x64 (`scripts\registrar.cmd x64`), que Windows 11 ejecuta por emulación.

Chrome, Edge y Firefox se distribuyen para Windows en x86, x64 y ARM64.

### 9.2. El registro

El navegador debe saber dónde está el programa. Cada uno lo busca en su propia clave del registro, cuyo valor predeterminado es la ruta completa del manifiesto:

| Navegador | Clave | Manifiesto |
| --- | --- | --- |
| Chrome | `Software\Google\Chrome\NativeMessagingHosts\es.fnmt.dniefirma` | `es.fnmt.dniefirma.chromium.json` |
| Edge | `Software\Microsoft\Edge\NativeMessagingHosts\es.fnmt.dniefirma` | `es.fnmt.dniefirma.chromium.json` |
| Firefox | `Software\Mozilla\NativeMessagingHosts\es.fnmt.dniefirma` | `es.fnmt.dniefirma.firefox.json` |

Hay dos alcances:

| Alcance | Dónde se escriben las claves | ¿Administrador? |
| --- | --- | --- |
| Solo el usuario actual | `HKEY_CURRENT_USER` | No |
| Todos los usuarios del equipo | `HKEY_LOCAL_MACHINE` | Sí |

`scripts\registrar.cmd` hace el registro por usuario y sirve de modelo para un instalador. Tras registrar no hace falta reiniciar el navegador.

En el manifiesto, la ruta del `.exe` es solo su nombre (`"path": "DNIeAnfitrion.exe"`). En Windows, los navegadores la entienden como relativa a la carpeta del manifiesto: por eso los tres archivos van juntos.

### 9.3. La extensión

"Cargar descomprimida" y los complementos temporales son para desarrollar. Para los usuarios, la extensión se distribuye de una de estas formas:

- **Publicándola en la tienda de cada navegador**: Chrome Web Store, Complementos de Microsoft Edge y addons.mozilla.org. Los pasos están en "Distribuirlo al público: qué tiene que publicar usted", más abajo.
- **En una organización, por directivas de empresa**, que la instalan sin intervención del usuario (ver "Instalarlo en muchos equipos").

Firefox exige además que la extensión esté firmada por Mozilla, también si no se publica en su tienda.

Al publicar la extensión en la tienda de Chrome o en la de Edge, la tienda le asigna su propio identificador. Hay que ponerlo en el manifiesto del anfitrión, o el navegador se negará a arrancar el programa (ver "Crear su propio proyecto a partir de este", en el apartado 12).

### 9.4. El software del DNIe y un lector de tarjetas

El software, con el Card Module, se descarga del portal oficial del DNIe (https://www.dnielectronico.es/PortalDNIe/), en "Área de descargas" → "Windows". Según la ayuda técnica de la Agencia Tributaria, Windows Update también puede instalarlo automáticamente.

Un equipo que solo verifica firmas no necesita lector ni software del DNIe. Pero sí necesita la raíz del DNIe entre las "Entidades de certificación raíz de confianza" de Windows: sin ella, la verificación responde "no es de confianza". El apartado 7 explica cómo obtenerla.

### Distribuirlo al público: qué tiene que publicar usted

Si su página es para el público general, sus usuarios instalan las piezas por sí mismos, y usted tiene que ponerlas a su alcance. Son dos piezas distintas, y cada una va por un canal diferente:

| Pieza | Dónde la publica | Quién la revisa |
| --- | --- | --- |
| La extensión | En la tienda de cada navegador. | La tienda, antes de publicarla. |
| El programa de firma | En su propio sitio web, como un instalador para descargar. | Nadie: no hay tienda para esto. |

Con las dos publicadas, su página enlaza a cada una con un enlace corriente (`<a href="…">`), como muestra "La primera visita de un usuario", en el apartado 6.

#### La extensión que publica es la suya

No hay una extensión común que sirva para todos los sitios. La lista de orígenes permitidos va escrita dentro de la extensión, así que **cada sitio web publica su propia copia**, con su dominio en la lista y con su propia identidad.

Antes de publicarla, prepárela como explican "Publicar la página en su servidor web", más abajo, y "Crear su propio proyecto a partir de este", en el apartado 12: sus orígenes, su nombre de anfitrión y su identidad.

#### Publicar la extensión en las tiendas

El proceso es parecido en las tres:

1. **Abra una cuenta de desarrollador** en la tienda. Cada tienda tiene sus condiciones.
2. **Empaquete la carpeta de la extensión en un `.zip`**: `extension\chromium` para Chrome y para Edge, `extension\firefox` para Firefox. El `manifest.json` debe quedar en la raíz del `.zip`.
3. **Súbalo y rellene la ficha**: nombre, descripción, icono y capturas. La tienda le pedirá que justifique el permiso `nativeMessaging`: explique que la extensión se comunica con un programa de firma instalado en el equipo.
4. **Espere a la revisión.** Puede tardar varios días. Cuente con ello antes de fijar una fecha.
5. **Anote la dirección de la ficha** que le da la tienda al publicarla. Es la que pondrá en el enlace de su página.

Lo que cambia de un navegador a otro:

| Navegador | Tienda | ¿Se puede instalar sin pasar por la tienda? |
| --- | --- | --- |
| Chrome | Chrome Web Store | No, salvo por directivas de empresa. |
| Edge | Complementos de Microsoft Edge | No, salvo por directivas de empresa. Edge puede instalar también desde la tienda de Chrome, si el usuario lo permite. |
| Firefox | addons.mozilla.org | Sí: puede alojar el archivo `.xpi` en su propio sitio. Pero Mozilla tiene que firmarlo igualmente, así que hay que enviárselo. |

**Después de publicar, actualice el manifiesto del anfitrión.** Las tiendas de Chrome y de Edge asignan a la extensión un identificador nuevo, distinto en cada una. Póngalos los dos en `allowed_origins`, o el navegador se negará a arrancar el programa. Hágalo antes de preparar el instalador, porque el manifiesto va dentro de él.

Cada vez que cambie la extensión (por ejemplo, para añadir un origen), tiene que subir una versión nueva y esperar otra revisión. Los navegadores la actualizan solos en los equipos de los usuarios.

#### Ofrecer el programa en su sitio

Este proyecto no incluye un instalador. Tiene que crearlo usted, con la herramienta que prefiera (un paquete MSI, por ejemplo), y alojarlo en su servidor. Debe hacer lo mismo que `scripts\registrar.cmd`:

1. Copiar `DNIeAnfitrion.exe` y los dos manifiestos a una carpeta, los tres juntos (apartado 9.1).
2. Escribir las tres claves del registro, una por navegador (apartado 9.2).

Tres recomendaciones:

- **Registre solo para el usuario actual** (`HKEY_CURRENT_USER`). Así el instalador no pide permisos de administrador.
- **Firme el instalador y el `.exe`** con un certificado de firma de código. Sin firma, el navegador y Windows muestran advertencias al descargarlo y al ejecutarlo, y muchos usuarios no seguirán adelante.
- **Elija la arquitectura dentro del instalador**, como hace `registrar.cmd`, o incluya solo la x64 si sus usuarios no usan equipos ARM. Así ofrece una única descarga.

Incluya también una forma de desinstalar, que borre los archivos y las tres claves. `scripts\desregistrar.cmd` sirve de modelo.

#### Lista de comprobación antes de abrir al público

- La extensión, con sus orígenes y su identidad, está aprobada en las tiendas de los navegadores que admite.
- El manifiesto del anfitrión contiene los identificadores definitivos de la extensión.
- El instalador está firmado y se descarga de su sitio por `https://`.
- Su página detecta qué falta y enlaza a la tienda y al instalador (apartado 6).
- Hay una página de ayuda con los requisitos, incluido el software del DNIe y el lector (apartado 9.4).

### Instalarlo en muchos equipos

Sirve cualquier herramienta de despliegue que copie archivos y escriba en el registro: un script de inicio de sesión por directiva de grupo, Intune o un paquete MSI, por ejemplo. Tiene que hacer lo mismo que `scripts\registrar.cmd`: copiar el `.exe` con sus dos manifiestos y escribir las tres claves.

La extensión se instala por directiva de cada navegador:

| Navegador | Directiva |
| --- | --- |
| Chrome y Edge | `ExtensionInstallForcelist`, con el identificador de la extensión y la dirección de actualización de su tienda. |
| Firefox | `ExtensionSettings`, con el identificador de la extensión y la dirección de su archivo `.xpi` firmado. |

Si la organización restringe los anfitriones nativos, tenga en cuenta dos directivas de Chrome y de Edge: `NativeMessagingBlocklist`, que puede bloquearlos todos salvo los de una lista de permitidos, y `NativeMessagingUserLevelHosts`, que, desactivada, hace que el navegador ignore los registrados en `HKEY_CURRENT_USER`.

Si los equipos no tienen el software del DNIe, el portal del DNIe indica cómo instalarlo sin preguntas al usuario (instalación desatendida):

```
Instalador_Tarjetas_DNIe_x64.exe /silent
Instalador_Tarjetas_DNIe_x86.exe /silent
```

### Publicar la página en su servidor web

El ejemplo viene preparado para `http://localhost:8080`. En una aplicación real, la página la sirve su servidor web, y la extensión tiene que conocer su origen. Está en dos lugares, y hay que cambiar los dos, en las carpetas `chromium` y `firefox`:

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

El patrón de `matches` no lleva puerto, porque Firefox no lo admite ahí. Por eso el ejemplo tiene `http://localhost/*` en `matches` y `http://localhost:8080` en `ORIGENES_PERMITIDOS`: el primero es amplio y el segundo, exacto.

Fuera de las pruebas en `localhost`, use solo orígenes `https://`.

Copie también `dniefirma.js` a su servidor, junto a su página. No hay que cambiar nada en él.

## 10. Cómo funciona por dentro

### Las piezas

```text
 ┌──────────────────────── Navegador ────────────────────────┐
 │                                                           │
 │  Página web              Extensión                        │
 │ ┌──────────────┐   ┌──────────────┐   ┌──────────────┐    │     ┌───────────────────┐
 │ │ index.html   │   │ contenido.js │   │  fondo.js    │    │     │ DNIeAnfitrion.exe │
 │ │ dniefirma.js │◀─▶│ (mensajero)  │◀─▶│  (portero)   │◀───┼────▶│ Main.cpp          │
 │ └──────────────┘   └──────────────┘   └──────────────┘    │     │ Crypto.cpp        │
 │   window.postMessage   runtime.sendMessage   connectNative│     │ PinDialog.cpp     │
 └───────────────────────────────────────────────────────────┘     └───────────────────┘
                                                  stdin / stdout            │
                                                                            │  CryptoAPI (certificados, PKCS#7)
                                                                            │  CNG (clave privada)
                                                                            ▼
                                             Windows: "Microsoft Smart Card Key Storage Provider"
                                                                            ▼
                                             Card Module (minidriver) del DNIe  ──►  lector  ──►  chip del DNIe
```

| Pieza | Archivo | Papel |
| --- | --- | --- |
| Biblioteca de la página | `web/dniefirma.js` | Ofrece `DNIeFirma.firmar()` y compañía. Convierte cada llamada en un mensaje y cada respuesta en una promesa. |
| Guion de contenido | `extension/*/contenido.js` | El navegador lo carga dentro de las páginas permitidas. Lleva los mensajes de la página a la extensión y de vuelta. No decide nada. |
| Guion de fondo | `extension/*/fondo.js` | Comprueba el origen de la página y la operación, arranca el programa de firma y devuelve su respuesta. |
| Anfitrión nativo | `anfitrion/Main.cpp` | Programa de Windows. Recibe la operación, muestra el diálogo "Abrir" y llama a `Crypto.cpp`. No contiene criptografía. |
| Núcleo criptográfico | `anfitrion/Crypto.cpp` | Firma y verificación. No sabe nada de navegadores ni de mensajes. |
| Diálogo del PIN | `anfitrion/PinDialog.cpp` | Cuadro de diálogo del PIN. |

La idea central es la misma que en el proyecto hermano: **el programa nunca habla con la tarjeta**. Pide las operaciones a Windows, y Windows las delega en el Card Module. Por eso el mismo código vale para cualquier tarjeta con minidriver.

Solo hay una consulta al lector, y es de lectura: el programa pregunta a Windows por el ATR de la tarjeta, unos bytes que la identifican y que se leen sin PIN. Con él sabe si es un DNIe 3.0 o 4.0 y qué longitud de PIN exigir (`ClassifyDnieAtr`, en `Crypto.cpp`). Si no lo consigue, el diálogo admite de 8 a 16 caracteres y la firma sigue adelante. La operación "version" hace esa misma consulta, recorriendo todos los lectores del equipo.

### Por qué hacen falta tantos saltos

Cada salto existe porque el navegador no permite atajarlo:

- Una página **no puede** hablar con un programa del equipo. Solo una extensión puede, y solo si declara el permiso `nativeMessaging`.
- Una página **no puede** llamar directamente a una extensión de cualquier manera. El camino más sencillo y común a los tres navegadores es el guion de contenido, que vive dentro de la página y ve los dos lados.
- El guion de contenido **no puede** usar `connectNative`. Solo el guion de fondo.

### Qué ocurre al pulsar "Firmar"

1. La página llama a `DNIeFirma.firmar()`. La biblioteca envía un mensaje con `window.postMessage` y devuelve una promesa.
2. `contenido.js` recoge el mensaje y reenvía a `fondo.js` solo el nombre de la operación.
3. `fondo.js` comprueba que la página es de un origen de su lista y que la operación existe. Si es así, arranca `DNIeAnfitrion.exe` y le envía `{"operacion":"firmar"}`.
4. El programa muestra el diálogo "Abrir" de Windows. El usuario elige el archivo.
5. `Crypto.cpp` busca el certificado de firma del DNIe, pide el PIN con su diálogo y firma. La operación la hace el chip del DNIe: la clave privada no sale de él.
6. El programa guarda la firma junto al documento (`.p7s`) y responde con el nombre del archivo, el firmante y la firma en Base64.
7. `fondo.js` recibe la respuesta y cierra la conexión. Con eso el programa termina.
8. La respuesta vuelve por el mismo camino y la promesa de la página se cumple.

**Un programa nuevo por petición.** El programa solo vive mientras dura la operación. Entre una firma y la siguiente no queda nada en marcha, ni el PIN en memoria.

### Cómo encuentra el navegador el programa

La extensión pide conectar con un nombre: `es.fnmt.dniefirma`. El navegador busca ese nombre en el registro de Windows (las claves están en el apartado 9). La clave contiene la ruta de un archivo JSON, el **manifiesto del anfitrión**, que dice dos cosas: dónde está el `.exe` y qué extensión puede usarlo.

```json
{
    "name": "es.fnmt.dniefirma",
    "description": "Firma de archivos con el DNI electrónico (ejemplo)",
    "path": "DNIeAnfitrion.exe",
    "type": "stdio",
    "allowed_origins": [
        "chrome-extension://amdchagegphbcmlidmjblabggpclhggj/"
    ]
}
```

Si la extensión que llama no es la del manifiesto, el navegador se niega a arrancar el programa.

Hay dos manifiestos porque cada familia de navegadores identifica la extensión a su manera: Chrome y Edge con `allowed_origins`, y Firefox con `allowed_extensions`.

### El programa, en pocas palabras

`Main.cpp` lo explica con detalle. Lo esencial:

- Es un programa **"de ventanas"** (`wWinMain`), no de consola. Así no aparece una ventana negra cada vez que el navegador lo arranca. La entrada y la salida estándar funcionan igual: las prepara el navegador.
- **Atiende peticiones hasta que el navegador cierra la conexión.** La extensión de este ejemplo abre una conexión por petición, así que lo habitual es dar una sola vuelta.
- **No usa ninguna biblioteca de JSON.** Solo necesita leer un campo de la petición (`LeerOperacion`) y escribir respuestas de pocos campos de texto (`TextoJson`).
- **Sus diálogos tienen una ventana "dueña" propia**, invisible y marcada como "siempre visible" (`CrearVentanaDuena`). El programa no tiene ventana, y quien está delante es el navegador: sin esa dueña, el diálogo "Abrir" o el del PIN podrían abrirse detrás de él. La dueña se coloca en el centro de la ventana del navegador, para que los diálogos salgan en ese mismo monitor.
- **Abierto con doble clic**, no tiene con quién hablar: lo explica con un mensaje y termina.

En Chrome y en Edge, el guion de fondo es un *service worker*, que el navegador detiene cuando lleva un rato sin actividad. La conexión abierta con el programa lo mantiene despierto mientras el usuario elige el archivo y escribe el PIN. En Firefox, el guion de fondo es una página de fondo, declarada con `scripts`.

### Dónde están los certificados y dónde la clave

Al insertar el DNIe, Windows copia sus **certificados** (la parte pública) al almacén personal del usuario, el mismo que se ve en `certmgr.msc` → Personal → Certificados. La **clave privada** no sale nunca del chip: junto a cada certificado, Windows anota en qué proveedor y tarjeta está su clave.

Un detalle con consecuencias: **los certificados se quedan en el almacén aunque se retire la tarjeta**. El programa busca el certificado en el almacén, así que:

- "No se detecta el DNIe" significa que ese usuario no ha insertado nunca un DNIe en ese equipo (o que falta el Card Module), no solo que ahora no haya tarjeta.
- Si el certificado está en el almacén pero la tarjeta no está puesta, es Windows quien pide insertarla al solicitar la clave.
- Si en el equipo se han usado varios DNIe (familiares, un DNI renovado), el programa firma con el que esté en el lector en ese momento. Solo pregunta con cuál firmar si no puede saberlo (ninguno insertado, o varios a la vez en distintos lectores). En esa lista aparecen también los caducados, con su fecha.

Por eso la línea de estado de la página y el mensaje "No se detecta el DNIe" no dicen lo mismo. La primera mira qué tarjeta hay ahora en el lector. El segundo, si hay certificados de DNIe en el almacén.

**Los certificados caducados cuentan.** El programa no descarta un certificado de FIRMA por estar caducado: un DNIe con los certificados caducados sigue siendo un DNIe, y el chip firma igualmente. La caducidad se comprueba al verificar. Si los descartara, el resultado sería un "No se detecta el DNIe" con la tarjeta puesta.

El DNIe contiene dos certificados del ciudadano:

| Certificado | Para qué | Uso de clave |
| --- | --- | --- |
| AUTENTICACIÓN | Identificarse en webs | Firma digital |
| FIRMA | Firmar documentos | No repudio |

El programa usa siempre el de FIRMA. Lo reconoce porque su emisor contiene "DNIE" (por ejemplo "AC DNIE 004" o "AC DNIE 006") y su uso de clave incluye "no repudio". Ninguno de los dos datos depende de la versión del DNIe.

### Qué ocurre dentro de la firma

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

Después, `Main.cpp` lee el `.p7s` recién creado y lo convierte a Base64 para devolvérselo a la página (`LeerArchivoEnBase64`).

### Qué ocurre dentro de la verificación

Los cuatro pasos están en `VerifyFileDetached` (`Crypto.cpp`):

1. Lee el `.p7s` (y lo convierte desde Base64 si hace falta).
2. Abre el mensaje PKCS#7 y le carga la firma.
3. Le pasa el documento a trozos para que calcule su resumen.
4. Por cada firmante: localiza su certificado dentro de la firma, comprueba la firma con su clave pública y comprueba que el certificado es de confianza.

**En la verificación no interviene el DNIe.** La hace entera Windows por software, con su biblioteca criptográfica (CryptoAPI, `crypt32.dll`). La clave pública que hace falta está en el certificado del firmante, que va dentro del propio `.p7s`. Por eso se puede verificar sin tarjeta, sin lector y sin PIN, y en un equipo distinto del que firmó.

| Operación | Quién hace el cálculo | ¿Hace falta el DNIe? |
| --- | --- | --- |
| Firmar (`firmar()`) | El chip del DNIe, con su clave privada. Windows se lo pide a través de CNG y del Card Module. | **Sí**: tarjeta, lector y PIN. |
| Verificar (`verificar()`) | Windows, por software (CryptoAPI), con la clave pública del certificado incluido en la firma. | **No**. |

Lo único que la verificación necesita del DNIe es su autoridad raíz, que se instala en Windows con el middleware. Sin ella, la firma se comprueba igual, pero el certificado del firmante no se da por fiable (ver el apartado 15).

## 11. Mapa del código

Orden de lectura recomendado, de lo que se ve desde fuera a lo de dentro:

| Orden | Archivo | Qué contiene |
| --- | --- | --- |
| 1 | `web/index.html` | La página de ejemplo. Cómo se usa la biblioteca desde JavaScript. |
| 2 | `web/dniefirma.js` | La biblioteca: `DNIeFirma.firmar()`, `verificar()` y `version()`. |
| 3 | `extension/chromium/contenido.js` | El guion de contenido: lleva mensajes entre la página y la extensión. |
| 4 | `extension/chromium/fondo.js` | El guion de fondo: lista de orígenes y conexión con el programa. |
| 5 | `anfitrion/Main.cpp` | El programa: formato de los mensajes, diálogo "Abrir" y las tres operaciones. Introducción a Native Messaging en la cabecera. |
| 6 | `anfitrion/Crypto.h` y `.cpp` | Firma y verificación, paso a paso. |
| 7 | `anfitrion/PinDialog.h` y `.cpp` | El diálogo del PIN. |

Archivos de apoyo:

| Archivo | Para qué |
| --- | --- |
| `extension/chromium/manifest.json` | Declaración de la extensión para Chrome y Edge. |
| `extension/firefox/manifest.json` | Declaración de la extensión para Firefox. |
| `extension/firefox/contenido.js`, `fondo.js` | Copias idénticas de los de `chromium`. |
| `anfitrion/manifiestos/*.json` | Los dos manifiestos del anfitrión: Chrome y Edge, y Firefox. |
| `anfitrion/DNIeAnfitrion.rc`, `resource.h`, `res/seguro.ico` | Recursos incrustados: diálogo del PIN, imagen y datos de versión del `.exe` (desarrollador y número de versión). |
| `anfitrion/Module.h` | Variable global con el "asa" del programa. |
| `anfitrion/DNIeAnfitrion.vcxproj`, `DNIeNativeMessaging.sln` | Proyecto de Visual Studio (x64 y ARM64). |
| `.vscode/*.json` | Configuración de Visual Studio Code: tareas y autocompletado. |
| `web/servir.cmd`, `servir.ps1` | Servidor web mínimo para probar en `http://localhost:8080/`. |
| `web/logoFNMT.png`, `logoFNMT.ico` | Imagen e icono de la página de ejemplo. |
| `scripts/compilar.cmd`, `registrar.cmd`, `desregistrar.cmd` | Compilar, registrar y desregistrar. |
| `scripts/probar-anfitrion.ps1` | Habla con el programa sin navegador. |

**Las dos carpetas de la extensión.** `contenido.js` y `fondo.js` son idénticos en `chromium` y en `firefox`. Entre navegadores solo cambia `manifest.json`: Chrome y Edge declaran el guion de fondo como `service_worker` y Firefox como `scripts`, y Firefox necesita un identificador explícito. Si modifica uno de los `.js`, copie el cambio a la otra carpeta.

**El código compartido con el proyecto hermano.** `Crypto.*`, `PinDialog.*` y `resource.h` proceden, sin cambios, de ActiveX para DNIe. Por eso algunos de sus comentarios hablan del "control", de `Signer.cpp` y de `DNIeActiveX.rc`: aquí equivalen al programa, a `Main.cpp` y a `DNIeAnfitrion.rc`.

## 12. Cómo ampliarlo

### Añadir una operación

Son siempre tres pasos. Ejemplo: una operación `"acerca"` que devuelve la versión del programa instalado.

**1. Atenderla en `Main.cpp`.** Escriba la función que monta la respuesta, junto a las demás operaciones:

```cpp
static std::string OperacionAcerca()
{
    return "{\"correcto\":true,\"programa\":" + TextoJson(L"1.0") + "}";
}
```

Y llámela desde `AtenderPeticion`:

```cpp
if (operacion == "acerca")
    return OperacionAcerca();
```

**2. Permitirla en `fondo.js`**, en las dos carpetas, añadiendo su nombre a la lista:

```js
const OPERACIONES = ["version", "firmar", "verificar", "acerca"];
```

**3. Ofrecerla en `dniefirma.js`**, junto a las demás funciones públicas:

```js
acerca: function () {
    return pedir("acerca").then(function (respuesta) {
        return respuesta.programa;
    });
}
```

Recompile, recargue la extensión y ya puede escribir `await DNIeFirma.acerca()` en su página. `contenido.js` no hay que tocarlo: lleva cualquier operación.

Reglas que conviene respetar:

- Responda siempre con un JSON que tenga `correcto`. Si es `false`, añada `codigo` y `error`, como hace `RespuestaDeError`. La biblioteca convierte eso en una promesa rechazada.
- Pase por `TextoJson` todo texto que vaya a la respuesta. Un nombre de archivo con comillas rompería el JSON.
- Si la operación abre diálogos, `fondo.js` ya impide que haya dos a la vez: lo hace con todas menos `"version"`. Una operación sin diálogos, como `"acerca"`, hay que añadirla a esa excepción (variable `abreDialogos`) para que se pueda atender siempre.
- Una vez publicado el ejemplo, **no cambie el nombre ni los campos de lo que ya existe**: rompería las páginas que lo usan. Añada operaciones y campos nuevos.

**Si la operación necesita datos de la página**, hay más trabajo, y es deliberado. Hoy la petición solo lleva el nombre de la operación, y eso es una de las barreras de seguridad del ejemplo. Tendrá que reenviar los datos en `contenido.js` y en `fondo.js` y leerlos en `Main.cpp`, donde conviene sustituir `LeerOperacion` por un analizador de JSON completo. Valide en `fondo.js` todo lo que venga de la página, y piense en cada dato nuevo como en algo que una página podría enviar con mala intención.

### Crear su propio proyecto a partir de este

Si copia el proyecto para hacer **otro** programa de firma, cambie obligatoriamente:

- El **nombre del anfitrión**, para que no choque con este ejemplo. `es.fnmt.dniefirma` aparece en `fondo.js` (constante `NOMBRE_DEL_ANFITRION`), en los dos manifiestos de `anfitrion/manifiestos` (campo `name` y nombre del archivo) y en `scripts/registrar.cmd` y `scripts/desregistrar.cmd` (variable `NOMBRE`). Solo admite letras minúsculas, números, puntos y guiones bajos.
- La **identidad de la extensión**, en los dos navegadores (ver la tabla siguiente).
- La **lista de orígenes permitidos** (ver el apartado 9).
- El GUID del proyecto en el `.vcxproj` y el `.sln`. Genere uno nuevo con `[guid]::NewGuid()` en PowerShell.

El manifiesto del anfitrión nombra la extensión que puede usar el programa. Si cambia la identidad de la extensión, hay que cambiarla también ahí:

| Navegador | Identidad del ejemplo | Dónde se define | Dónde se autoriza |
| --- | --- | --- | --- |
| Chrome y Edge | `amdchagegphbcmlidmjblabggpclhggj` | La calcula el navegador a partir del campo `key` de `extension/chromium/manifest.json` | `allowed_origins`, en `anfitrion/manifiestos/es.fnmt.dniefirma.chromium.json` |
| Firefox | `dniefirma@dnielectronico.es` | Campo `browser_specific_settings.gecko.id` de `extension/firefox/manifest.json` | `allowed_extensions`, en `anfitrion/manifiestos/es.fnmt.dniefirma.firefox.json` |

El campo `key` es una clave pública. Su única función aquí es que el identificador sea el mismo en todos los equipos; sin él, Chrome daría a una extensión cargada desde una carpeta un identificador distinto en cada sitio.

Si publica la extensión en la tienda de Chrome o en la de Edge, la tienda le asigna su propio identificador: quite el campo `key` y ponga el nuevo identificador en `allowed_origins`. Chrome y Edge pueden tener identificadores distintos; `allowed_origins` admite varios.

### Reutilizar solo la firma, sin navegador

`Crypto.h`, `Crypto.cpp`, `PinDialog.*`, `Module.h` y `resource.h` no dependen de Native Messaging. Puede incluirlos en un programa de consola, un servicio o una aplicación de escritorio y llamar directamente a `SignFileDetached` y `VerifyFileDetached`. Necesita además:

- El diálogo `IDD_PIN` y el icono `IDI_PIN` de `DNIeAnfitrion.rc`, con `res\seguro.ico`.
- Definir la variable `g_hInstance` que declara `Module.h`, y darle el módulo que contiene esos recursos (en un `.exe`, `GetModuleHandleW(nullptr)`).
- Enlazar con `crypt32.lib`, `cryptui.lib`, `ncrypt.lib` y `winscard.lib`.

Este proyecto es, de hecho, un ejemplo de esa reutilización: toma esos archivos del proyecto hermano y les pone delante `Main.cpp`.

### Ideas de ampliación

| Mejora | Por dónde empezar |
| --- | --- |
| Comprobar la revocación (OCSP/CRL) | `IsCertificateTrusted` en `Crypto.cpp`: añadir `CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT` a `CertGetCertificateChain`. Necesita conexión a Internet. |
| Validar con la fecha de la firma en vez de la actual | Leer el atributo de hora de firma del mensaje y pasarlo como `pTime` a `CertGetCertificateChain`. |
| Sello de tiempo (firma longeva) | Añadir un atributo no firmado con la respuesta de una autoridad de sellado (`CryptRetrieveTimeStamp`). |
| Firma con el documento dentro (*attached*) | Quitar `CMSG_DETACHED_FLAG` en `SignFileDetached` y adaptar la verificación. |
| Firmar texto o datos que envía la página | Nueva operación con datos (ver "Añadir una operación") que alimente el mensaje desde un búfer en lugar de `FeedFileToMessage`. Muestre al usuario lo que va a firmar antes de pedir el PIN. |
| Verificar sin exigir que el certificado sea de confianza | Nueva operación que llame a `VerifyFileDetached` con `checkTrust = false`. |
| Firmar con otro certificado del almacén | `SignFileDetached` ya admite una huella SHA-1 en su tercer parámetro; `Main.cpp` pasa siempre una cadena vacía. Sirve además para hacer pruebas sin DNIe (ver el apartado 13). |
| Devolver el NIF del firmante | Nuevo campo junto a `firmante`. `CertGetNameStringW` con `CERT_NAME_ATTR_TYPE` y el OID `2.5.4.5` (`szOID_DEVICE_SERIAL_NUMBER`), como en `GetCertificateName` (`Crypto.cpp`). |
| Devolver la hora de la firma | Nuevo campo con el atributo `signingTime` del firmante: `CryptMsgGetParam` con `CMSG_SIGNER_AUTH_ATTR_PARAM`, en `VerifyFileDetached`. |
| Código de error más preciso | Hoy `Main.cpp` distingue una cancelación buscando la palabra "cancelada" en el texto (`RespuestaDeFalloDeFirma`). Es frágil: si cambia el mensaje, deja de funcionar. Lo sólido es que `Crypto.cpp` devuelva además el código de Windows (`DescribeError`) y traducirlo a códigos como `pin_incorrecto` o `pin_bloqueado`. |
| Diálogo "Guardar como" para la firma | `GetSaveFileNameW`, junto a `SeleccionarArchivo` en `Main.cpp`. Resuelve el caso de una carpeta de solo lectura. |
| Otro aspecto para el diálogo del PIN | `IDD_PIN` en `DNIeAnfitrion.rc` (diseño) y `PinDialog.cpp` (comportamiento). |
| Otra longitud de PIN | `PIN_MIN_LENGTH_DNIE3`, `PIN_MIN_LENGTH_DNIE4` y `PIN_MAX_LENGTH` en `PinDialog.h`. Hoy valen 8, 12 y 16. |
| Reconocer otra versión del DNIe | `ClassifyDnieAtr` en `Crypto.cpp` (lee la versión en el ATR de la tarjeta), el tipo `DnieVersion` en `PinDialog.h` y `GetInsertedDnieVersion` en `Crypto.cpp` (da el texto de la operación "version"). |
| Rechazar o avisar al firmar con un certificado caducado | `IsUsableForSigning` en `Crypto.cpp` (`CertVerifyTimeValidity` dice si está en plazo). |
| Limpiar la página al retirar el DNIe | La página consulta `version()` al recuperar el foco. Con un `setInterval` puede consultarla cada pocos segundos y llamar a `limpiar()` cuando la versión pase a estar vacía. Cada consulta arranca el programa. |
| Admitir otros navegadores basados en Chromium | Añadir su clave del registro en `registrar.cmd` y `desregistrar.cmd`, y probar. |
| Versión para x86 | Añadir la plataforma Win32 al `.vcxproj` y a `compilar.cmd`. Hace falta si el Card Module instalado es de 32 bits. |
| Saber qué versión del programa está instalada | La operación `"acerca"` del ejemplo anterior. Útil cuando hay equipos con versiones distintas. |

## 13. Pruebas

El proyecto no trae pruebas automáticas. Trae una herramienta para probar el programa sin navegador, y este apartado añade la lista de lo que hay que comprobar a mano.

### Probar el programa sin navegador

`scripts\probar-anfitrion.ps1` hace lo mismo que el navegador: arranca `DNIeAnfitrion.exe`, le envía una petición por su entrada estándar y muestra la respuesta que llega por su salida estándar.

```bat
powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1
powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1 firmar
powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1 verificar
powershell -ExecutionPolicy Bypass -File scripts\probar-anfitrion.ps1 version ARM64
```

El primer argumento es la operación; sin argumentos se pide "version". El segundo es la plataforma del `.exe`; por defecto, x64.

| Operación | Qué necesita | Qué muestra |
| --- | --- | --- |
| `version` | Nada. No abre diálogos ni pide el PIN. | La versión del DNIe insertado, o vacía. |
| `firmar` | DNIe, lector y PIN. Abre el diálogo "Abrir" y el del PIN. | El nombre del archivo, el firmante y la firma en Base64. |
| `verificar` | Un archivo con su `.p7s`. Abre el diálogo "Abrir". | El nombre del archivo y el firmante, o el motivo del fallo. |

Sirve para dos cosas: ver el formato de los mensajes funcionando, y saber, si algo falla, si el problema está en el programa o en el navegador y la extensión. Si aquí responde bien y en la página no, el problema está en el registro, en la extensión o en la lista de orígenes (ver el apartado 15).

El script es además un ejemplo de cómo hablar con el programa desde otro lenguaje.

**Un detalle del script.** Si la consola está en UTF-8 (`chcp 65001`), Windows PowerShell escribe por su cuenta 3 bytes de "marca" (BOM) en la entrada del programa nada más arrancarlo. El programa los tomaría por el principio de la longitud del mensaje, la vería disparatada y terminaría sin responder. El script lo evita poniendo la entrada de la consola en UTF-8 "sin marca" antes de arrancar el programa. Si escribe su propia herramienta de prueba con PowerShell, haga lo mismo.

### Qué comprobar a mano

Repita esta lista **en cada navegador** que vaya a admitir, porque la extensión es distinta en Firefox:

- Sin la extensión cargada, la página dice que no la detecta y la línea de estado queda en rojo.
- Con la extensión y sin el programa registrado (`scripts\desregistrar.cmd`), la página dice que no puede conectar con el programa de firma.
- Con todo instalado y sin tarjeta, la línea de estado dice que no se detecta ningún DNIe.
- Con el DNIe en el lector, la línea de estado y el título dicen su versión.
- "Firmar un archivo…" abre el diálogo "Abrir" por delante del navegador. Tras escribir el PIN, aparece el nombre del firmante y el `.p7s` queda junto al documento.
- Cancelar el diálogo "Abrir" o el del PIN da "Operación cancelada", sin aspecto de error.
- Un PIN incorrecto da su mensaje. **Cuidado: varios intentos fallidos seguidos bloquean el DNIe.**
- "Verificar una firma…" con el archivo recién firmado da "La firma es válida".
- Tras modificar el documento, la verificación da "La firma NO es válida".
- Una segunda pulsación mientras hay un diálogo abierto no abre otro.
- El botón "Limpiar" borra el resultado.
- En el diálogo del PIN: el botón "Aceptar" se activa al llegar a la longitud mínima, y la casilla "Mostrar el PIN" funciona.

### Lo que no se puede probar sin tarjeta

La detección del certificado de FIRMA del DNIe, la de la versión de la tarjeta y la entrega del PIN al Card Module dependen de la tarjeta. Sin DNIe solo se pueden probar la operación "version" (que responde vacía), las cancelaciones y la verificación de una firma ya hecha.

Para automatizar la firma sin tarjeta haría falta firmar con un certificado temporal del almacén. `SignFileDetached` lo admite, pero `Main.cpp` no lo ofrece (ver "Ideas de ampliación", en el apartado 12). El proyecto hermano, ActiveX para DNIe, sí trae esas pruebas para el código compartido de `Crypto.cpp`.

**Si añade una operación, pruébela primero con `probar-anfitrion.ps1`**, que la acepta como primer argumento, y después desde la página.

## 14. Seguridad

Lea este apartado antes de instalar el ejemplo fuera de un entorno de pruebas.

- **Verifique siempre en el servidor.** La verificación en el navegador sirve para informar al usuario, no como prueba: la página se ejecuta en un equipo que usted no controla (ver el apartado 7).
- **Las firmas contienen datos personales.** Cada `.p7s` lleva el certificado del firmante, con su nombre y su NIF, y la página que pide la firma lo recibe. Por eso el `.gitignore` del proyecto excluye los `.p7s`. Si los guarda, trátelos como datos personales (ver el apartado 7).
- **Tres barreras, cada una en su sitio.** El navegador solo deja usar el programa a la extensión nombrada en el manifiesto del anfitrión. La extensión solo atiende a los orígenes de su lista. Y el programa no acepta rutas ni datos: solo el nombre de una operación. Así, lo peor que puede hacer quien consiga enviar un mensaje es abrir un diálogo que el usuario puede cancelar.
- **Mantenga corta la lista de orígenes.** Cualquier página de un origen permitido puede abrir los diálogos de firma. Use solo orígenes `https://` que usted controle. Un fallo de seguridad en una de esas páginas (por ejemplo, un XSS) da a un atacante esa misma capacidad.
- **La página no ve el disco.** El archivo lo elige el usuario en un diálogo de Windows, y a la página solo vuelve su nombre, sin carpeta. La página no puede leer archivos ni decidir dónde se escribe el `.p7s`.
- **El origen lo dice el navegador, no la página.** `fondo.js` no se fía de nada que venga en el mensaje: toma el origen del dato que aporta el navegador sobre el remitente, y construye una petición nueva para el programa en lugar de reenviar la de la página.
- **El usuario debe saber qué firma.** El programa firma el archivo que el usuario elige. Es responsabilidad de la página explicarle con claridad qué se le pide firmar y para qué.
- **Sin comprobación de revocación.** Un certificado robado y revocado seguiría verificándose como válido.
- **Validación con la fecha actual.** Una firma hecha con un certificado que entonces era válido pero ya ha caducado se dará por no fiable.
- **La hora de la firma no es un sello de tiempo.** Es la del reloj del equipo que firmó.
- **Se puede firmar con un certificado caducado.** El programa no lo impide. Quien recibe la firma lo detecta al verificar.
- **El PIN.** Toda firma lo exige. El programa lo mantiene en memoria el tiempo imprescindible y lo borra al terminar; además, el propio programa termina al acabar cada operación. No lo escribe en disco ni lo devuelve a la página ni a la extensión. Con la casilla "Mostrar el PIN" marcada queda a la vista en pantalla: está desmarcada por defecto y es el usuario quien decide.
- **El registro por usuario lo puede cambiar el usuario.** Las claves de `HKEY_CURRENT_USER` y la carpeta del programa no están protegidas frente a otros programas del mismo usuario. Para un uso real, instale el programa en una carpeta que solo pueda modificar un administrador y regístrelo en `HKEY_LOCAL_MACHINE`.
- **El programa y la extensión no están firmados.** Para distribuirlos conviene firmar el `.exe` con un certificado de firma de código y publicar la extensión en las tiendas de los navegadores.

## 15. Problemas frecuentes

### "No se detecta la extensión de firma con DNIe"

- La extensión no está cargada o está desactivada. Revise la página de extensiones del navegador. En Firefox, un complemento temporal desaparece al cerrar el navegador.
- `web\servir.cmd` abre la página en el navegador predeterminado, y la extensión está cargada en otro. Copie la dirección en el navegador que la tiene.
- La página no se ha abierto desde `http://localhost:8080/`. Abierta como archivo (`file://…`) o desde otro servidor, la extensión no actúa.
- La extensión se cargó después de abrir la página. Recargue la página.
- En Firefox, compruebe en `about:addons` → la extensión → "Permisos" que tiene acceso a `localhost`.
- Está en una ventana privada o de incógnito, donde la extensión no está permitida.

### "No se pudo conectar con el programa de firma"

El mensaje termina con el detalle que da el navegador, en inglés.

- *"Specified native messaging host not found"* o *"No such native application"*: falta el registro. Ejecute `scripts\registrar.cmd`. Si ha movido la carpeta del proyecto, ejecútelo de nuevo: el registro guarda la ruta completa.
- *"Access to the specified native messaging host is forbidden"*: el identificador de la extensión no coincide con el del manifiesto del anfitrión. En Chrome y Edge debe ser `amdchagegphbcmlidmjblabggpclhggj`; si es otro, el campo `key` de `manifest.json` se ha modificado o eliminado.
- *"Native host has exited"* o *"Error when communicating with the native messaging host"*: el programa arranca pero no responde bien. Pruébelo con `scripts\probar-anfitrion.ps1`.

Después de registrar no hace falta reiniciar el navegador.

### "Esta página no está autorizada a firmar con el DNIe"

El origen de la página no está en `ORIGENES_PERMITIDOS` de `fondo.js`. Ocurre, por ejemplo, al servir la página en otro puerto. Tras cambiar la lista, recargue la extensión en la página de extensiones y después la página.

### `probar-anfitrion.ps1` dice "El programa ha terminado sin responder"

El programa ha recibido algo que no es una petición y ha terminado. Con una herramienta de prueba propia, la causa habitual es la "marca" de UTF-8 que Windows PowerShell escribe en la entrada del programa cuando la consola está en UTF-8 (ver el apartado 13). El script del proyecto ya lo evita.

### El diálogo "Abrir" o el del PIN no aparece

Puede haber quedado detrás de otra ventana. Búsquelo con Alt+Tab. El programa lo coloca por encima del navegador, en el monitor donde este se encuentra.

Mientras ese diálogo siga abierto, la página responde "Ya hay una operación de firma en curso" a cualquier otra petición.

### La línea de estado dice que no hay DNIe, pero la tarjeta está puesta

La línea de estado se actualiza al abrir la página y al volver a ella desde otra ventana. Si ha insertado la tarjeta sin salir de la página, pulse fuera del navegador y vuelva, o recargue la página.

Si sigue igual, Windows no ve la tarjeta, o es un DNIe de una versión que el programa no reconoce. Lo primero se trata en el punto siguiente. En el segundo caso se puede firmar igualmente: el diálogo del PIN admite de 8 a 16 caracteres.

### "No se detecta el DNIe"

Abra `certmgr.msc` → Personal → Certificados después de haber insertado el DNIe. Debe aparecer un certificado a su nombre emitido por una "AC DNIE". Si no aparece:

- El lector no está conectado o Windows no lo ve (Administrador de dispositivos → Lectores de tarjetas inteligentes).
- No está instalado el Card Module del DNIe: Windows ve la tarjeta pero la muestra como "Tarjeta inteligente desconocida".
- El servicio "Propagación de certificados" de Windows está deshabilitado.

Si el certificado **sí aparece** y el error persiste, compruebe que es el de FIRMA (en sus detalles, "Uso de la clave" incluye "Sin repudio") y que Windows le asocia una clave privada: al abrirlo, debe indicar "Tiene una clave privada correspondiente a este certificado".

### En un Windows sobre ARM64 no se detecta el DNIe

El programa ARM64 solo puede cargar un Card Module ARM64. Si el middleware instalado no tiene esa versión, registre el programa x64 (`scripts\registrar.cmd x64`), que Windows 11 ejecuta por emulación (ver el apartado 9).

### Aparece el diálogo de PIN de Windows además del del programa, o la tarjeta rechaza el PIN

El punto a revisar es `GivePinToKey` en `Crypto.cpp`, que entrega el PIN al proveedor de Windows para que no muestre su propio diálogo.

### No se puede pulsar "Aceptar" en el diálogo del PIN

El botón se activa al llegar a la longitud mínima del PIN: 8 caracteres en el DNIe 3.0 y 12 en el 4.0. Si el programa no reconoce la versión de la tarjeta, exige 8. El propio diálogo indica el intervalo y cuántos caracteres lleva escritos. Si su tarjeta tiene un PIN más corto, baje `PIN_MIN_LENGTH_DNIE3` o `PIN_MIN_LENGTH_DNIE4` en `PinDialog.h` y recompile.

### La verificación falla con "no es de confianza"

La firma es correcta, pero Windows no confía en el certificado del firmante. Dos causas:

- En ese equipo falta la autoridad raíz del firmante. Para el DNIe se instala con su middleware. En un equipo sin el software del DNIe, hay que importarla (ver el apartado 7).
- Con el DNIe 4.0, la cadena es *AC DNIE 007* → *AC RAIZ DNIE 3*, distinta de la de las versiones anteriores (ver "La raíz del DNIe" en el apartado 7). El código `0x800B010A` indica que Windows no ha podido completar la cadena; `0x800B0109`, que la completa pero la raíz no está entre las de confianza del equipo.
- El certificado ha caducado (el mensaje lo indica). Ocurre siempre con las firmas hechas con un DNIe caducado.

La comprobación que vale es la del servidor (ver el apartado 7).

### "No se pudo crear el archivo de firma"

La carpeta del documento no admite escritura: por ejemplo, una carpeta de solo lectura o una unidad de red sin permiso. La firma se guarda siempre junto al documento, así que hay que copiar el documento a una carpeta donde se pueda escribir y firmarlo allí. En el apartado 12 se propone una ampliación para elegir dónde guardar la firma.

### El servidor rechaza una firma que la página da por buena

- **El documento ha cambiado por el camino.** Cualquier cambio invalida la firma, también un cambio en los saltos de línea. Pasa, por ejemplo, si el archivo atraviesa un FTP en modo texto o Git con conversión de finales de línea.
- **El usuario ha enviado otro documento**, y no el que firmó.
- **Falta `-binary` en la orden de OpenSSL** (ver el apartado 7).
- **El servidor no tiene la raíz de ese DNIe**, o tiene la de otra generación.
- **La firma llega en Base64.** Es como la entrega `firmar()`. La orden de OpenSSL del apartado 7 espera binario: decodifíquela antes.

### `compilar.cmd` dice que ARM64 se ha omitido

Faltan las herramientas de C++ de esa plataforma (ver el apartado 3). No es un error si su equipo es x64.

### La compilación falla con el error LNK1104

El `.exe` está en uso: hay una firma a medias, con un diálogo abierto. Termínela o cancélela y compile de nuevo.

### `servir.cmd` dice que no pudo abrir el puerto

Otro programa usa el puerto 8080, o ya hay otro `servir.cmd` en marcha. Ciérrelo, o cambie el puerto en `servir.ps1` y el origen en `ORIGENES_PERMITIDOS`.

### He cambiado `fondo.js` o `contenido.js` y no se nota

El navegador no vuelve a leer los archivos de la extensión por sí solo. Pulse "Recargar" en la ficha de la extensión y recargue después la página. Y recuerde copiar el cambio a la otra carpeta (`chromium` o `firefox`).

## 16. Glosario

Términos que aparecen en esta guía, explicados para quien empieza.

| Término | Qué es |
| --- | --- |
| Almacén personal | Lugar de Windows donde están los certificados del usuario (en `certmgr.msc`, "Personal"). Al insertar el DNIe, Windows copia en él sus certificados. Las claves privadas no salen de la tarjeta. |
| Anfitrión nativo | Programa del equipo que una extensión puede arrancar para intercambiar mensajes con él. Aquí, `DNIeAnfitrion.exe`. |
| ATR | Bytes con los que una tarjeta se identifica al insertarla en el lector. Se leen sin PIN. El programa deduce de ellos la versión del DNIe. |
| Autoridad de certificación (AC) | Entidad que emite certificados y responde de ellos. La AC raíz está en lo alto de la cadena. Las intermedias, como *AC DNIE 007*, emiten los certificados de los ciudadanos. |
| Base64 | Forma de escribir datos binarios como texto. Así viaja la firma del programa a la página. |
| Cadena de certificación | Secuencia que va del certificado del firmante a una AC raíz de confianza, pasando por las intermedias. |
| Caja de arena (*sandbox*) | Aislamiento con el que el navegador impide a una página acceder al equipo. |
| Card Module (minidriver) | Software del DNIe que permite a Windows usar la tarjeta. Se instala con el software del portal del DNIe. |
| Certificado | Documento electrónico que une una clave pública con una persona. El DNIe tiene dos: el de autenticación y el de firma. |
| CMS / PKCS#7 | Formato estándar de firma electrónica. CMS es la evolución de PKCS#7. Es el formato del archivo `.p7s`. |
| CNG | Las funciones criptográficas actuales de Windows. Con ellas se pide a la tarjeta que firme. |
| DER | Codificación binaria de certificados y firmas. Es la que usa el programa para el `.p7s`. |
| Extensión | Complemento que añade funciones al navegador. Se describe en un archivo `manifest.json`. |
| Firma separada (*detached*) | Firma que va en un archivo aparte y no contiene el documento. Para verificarla hacen falta los dos archivos. |
| Guion de contenido | Parte de una extensión que el navegador carga dentro de ciertas páginas. Aquí, `contenido.js`. |
| Guion de fondo | Parte de una extensión que funciona fuera de las páginas. En Chrome y Edge es un *service worker*. Aquí, `fondo.js`. |
| Manifest V3 | Versión actual del formato de las extensiones. |
| Manifiesto del anfitrión | Archivo JSON que dice al navegador dónde está el anfitrión nativo y qué extensión puede usarlo. |
| Native Messaging | Mecanismo de los navegadores para que una extensión hable con un programa del equipo. |
| NIF | Número de identificación fiscal. En el certificado del DNIe identifica al titular (ver el apartado 7). |
| Origen | Protocolo, servidor y puerto de una dirección web: `https://www.ejemplo.es`. |
| PIN | Clave secreta del DNIe. Sin ella, la tarjeta no firma. |
| Promesa | Objeto de JavaScript que representa un resultado que llegará más tarde. Se espera con `await` o con `.then()`. |
| Revocación | Anulación de un certificado antes de que caduque, por ejemplo por pérdida de la tarjeta. Se consulta por OCSP o en listas de revocación (CRL). |
| Sello de tiempo | Prueba de la hora de una firma, emitida por un tercero de confianza. Este ejemplo no lo añade. |
| *Service worker* | Guion que el navegador arranca cuando hace falta y detiene cuando lleva un rato sin actividad. |
| stdin / stdout | Entrada y salida estándar de un programa. Es por donde el navegador y el anfitrión se envían los mensajes. |
