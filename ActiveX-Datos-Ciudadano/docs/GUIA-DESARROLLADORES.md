# Guía para desarrolladores: ActiveX del DNIe (DNIe 3.0 y 4.0)

Versión 1.0 · 26 de noviembre de 2026 · Equipo DIT Drivers, FNMT-RCM

Esta guía acompaña al proyecto **ActiveX del DNIe** (`FNMT.ActiveXDNIe`): un control que permite a una página web **leer los datos del ciudadano del DNI electrónico** (datos personales, MRZ, foto y firma manuscrita), leer sus certificados, y **firmar y verificar** con sus claves. Tiene **la misma interfaz que el ActiveX publicado** en el portal del DNIe (`ActiveXDNIe.dll` 1.0.0.2), pero funciona también con el DNIe 4.0, porque usa el driver PKCS#11 actual de la FNMT-RCM. Probado con DNIe 3.0 y 4.0. Está escrita para programadores que quieran:

1. **usar el control tal cual** en sus propias páginas, o
2. **tomar el código como base** y ampliarlo.

No hace falta saber COM ni PKCS#11 para seguirla. El código fuente está comentado paso a paso. Esta guía da la visión de conjunto y dice dónde mirar.

## Contenido

1. [Qué es y qué no es](#1-qué-es-y-qué-no-es)
2. [Restricciones de uso](#2-restricciones-de-uso)
3. [Puesta en marcha en diez minutos](#3-puesta-en-marcha-en-diez-minutos)
4. [Importar y compilar en Visual Studio Code](#4-importar-y-compilar-en-visual-studio-code)
5. [Importar y compilar en Visual Studio](#5-importar-y-compilar-en-visual-studio)
6. [Usar el control en su página](#6-usar-el-control-en-su-página)
7. [Llevar los datos y la firma al servidor](#7-llevar-los-datos-y-la-firma-al-servidor)
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

- Conecta con el DNIe 3.0 o 4.0 a través del driver PKCS#11 del DNIe instalado en Windows.
- Lee los datos del chip: nombre, apellidos, número de DNI, fechas, nacionalidad, sexo, lugar de nacimiento, padres y domicilio (grupo DG13), la MRZ (DG1), la foto (DG2) y la firma manuscrita (DG7).
- Lista y lee los certificados y las claves públicas.
- Firma un resumen SHA-256 con la clave de firma o con la de autenticación, y verifica firmas.
- Pide el **PIN** en una ventana propia cuando una operación lo necesita: la página nunca lo ve. La ventana exige la longitud de cada versión: de 12 a 16 caracteres en el DNIe 4.0 y de 8 a 16 en el 3.0. Tiene una casilla "Mostrar el PIN".
- Pide **permiso al usuario** antes de entregar sus datos personales a una página, indicándole qué página lo pide.
- Detecta el cambio de tarjeta en el lector, para que la página no mezcle datos de dos personas.

### Datos técnicos

| Aspecto | Valor |
| --- | --- |
| Nombre para crearlo (ProgID) | `FNMT.ActiveXDNIe` |
| Interfaz | `IDLL_ADMIN`, la misma del ActiveX publicado (mismos nombres, números de método y parámetros) |
| Acceso a la tarjeta | Driver PKCS#11 del DNIe (`DNIe_P11*.dll`), cargado solo desde la carpeta del sistema |
| Versiones del DNIe | 3.0 y 4.0 |
| Firma | SHA-256. RSA PKCS#1 v1.5 (DNIe 3.0) o ECDSA P-256 en bruto, `r‖s` (DNIe 4.0) |
| Tecnología del control | COM en C++ con ATL (enlazada de forma estática). DLL sin dependencias |
| Arquitecturas | x86 (`ActiveXDNIe_x86.dll`), x64 (`ActiveXDNIe_x64.dll`) y ARM64 (`ActiveXDNIe_arm64.dll`) |

**Limitación importante: el navegador.** Un ActiveX solo puede usarse desde el motor de Internet Explorer:

- Internet Explorer 11,
- Microsoft Edge en "modo Internet Explorer", o
- una aplicación HTA (una página que Windows ejecuta en ventana propia con `mshta.exe`), que es lo que usa el ejemplo.

En Chrome, Firefox o Edge normal **no funciona**: en ellos no existe `ActiveXObject`. Para esos navegadores existe la versión de este ejemplo con *Native Messaging*, con las mismas funciones. El apartado 2, "Restricciones de uso", detalla la compatibilidad navegador por navegador.

### Qué no hace

- No genera firmas CAdES, PAdES ni XAdES: la firma es "en bruto", tal como la devuelve la tarjeta. Para firma con formato está AutoFirma.
- No valida la cadena de confianza ni la revocación de los certificados: `Verificar` solo comprueba que la firma corresponde a los datos y a la clave.
- Algunas funciones del publicado no están disponibles y devuelven `0x54`, «función no disponible»: información biométrica, certificados del chip y `ObtenerCAN`. La lista completa está en el apartado 8, "Sin función".

El apartado 12, "Cómo ampliarlo", indica por dónde empezar algunas de estas mejoras.

### Diferencias con el ActiveX publicado

| | Publicado (1.0.0.2) | Este control |
| --- | --- | --- |
| ProgID | `DLL_ADMINISTRATIVA_DNIE.DLL_ADMIN` | `FNMT.ActiveXDNIe` |
| DNIe 4.0 | No funciona (`InicializarConexion` da 48) | Sí |
| Resumen de la firma | SHA-1 | SHA-256 |
| PIN | Lo da la página en `PinIn` | Lo pide el control en su ventana (`PinIn` sigue funcionando) |
| Datos personales | Sin preguntar | Pide permiso al usuario |
| Datos del DNIe 3.0 | Sin PIN (canal con el CAN) | Con PIN (el control lo pide) |

Los dos pueden estar instalados a la vez: tienen ProgID y CLSID distintos.

## 2. Restricciones de uso

El control tiene pocas restricciones, pero una es determinante: el navegador. Este apartado las reúne todas, para que pueda decidir si el control sirve para su caso antes de escribir una línea.

### Navegador mínimo

El navegador mínimo es **Internet Explorer 11**, o un programa que use su motor: Microsoft Edge en modo Internet Explorer o una aplicación HTA.

No sirve ninguna versión de Chrome, Firefox, Safari ni de otro navegador, ni antigua ni moderna. La restricción no es de versión, sino de tecnología: ActiveX solo existe en el motor de Internet Explorer.

La página de ejemplo necesita el modo de documento IE10 o superior, porque muestra la foto y la firma manuscrita con `canvas` y `Uint8Array`.

### Compatibilidad por fabricante

| Fabricante | Navegador | ¿Utilizable? | Observaciones |
| --- | --- | --- | --- |
| Microsoft | Aplicación HTA (`mshta.exe`) | **Sí** | Viene con Windows 10 y 11 y no requiere configurar nada. Es la vía que usa el ejemplo. |
| Microsoft | Internet Explorer 11 | Sí, con condiciones | Es la versión mínima. Requiere las condiciones que se indican más abajo. Retirado de las ediciones habituales de Windows 10 en junio de 2022 y ausente de Windows 11. Sigue disponible en Windows 10 LTSC y en Windows Server. |
| Microsoft | Internet Explorer 10 y anteriores | **No** | Sin soporte de Microsoft y sin versión para Windows 10 ni 11. |
| Microsoft | Edge en modo Internet Explorer | Sí, con condiciones | Hay que activar el modo IE para la página y cumplir las condiciones que se indican más abajo. |
| Microsoft | Edge en modo normal | **No** | Usa el motor Chromium, que no tiene ActiveX. Use la versión *Native Messaging*. |
| Google | Chrome, cualquier versión | **No** | No existe `ActiveXObject`. Use la versión *Native Messaging*. |
| Mozilla | Firefox, cualquier versión | **No** | No existe `ActiveXObject`. Use la versión *Native Messaging*. |
| Apple | Safari, cualquier versión | **No** | No existe `ActiveXObject`. Además, el control solo existe para Windows. |
| Otros | Opera, Brave, Vivaldi y demás navegadores basados en Chromium | **No** | Mismo motor que Chrome. |
| Cualquiera | Navegadores de Android, iOS, macOS y Linux | **No** | El control es una DLL de Windows. |

### Condiciones en Internet Explorer 11 y en Edge en modo IE

Que el navegador sea compatible no basta. Además:

- **El control debe estar ya registrado en el equipo.** La página no puede descargarlo ni instalarlo: no se distribuye como paquete `.cab` (ver el apartado 9).
- **La zona de seguridad de la página debe permitir ActiveX.** En concreto, las opciones "Ejecutar controles y complementos de ActiveX" y "Generar scripts de los controles ActiveX marcados como seguros para scripting". Es lo habitual en "Intranet local" y en "Sitios de confianza".
- **El filtrado ActiveX debe estar desactivado** para ese sitio.
- **En Edge, el modo IE hay que activarlo.** Lo hace el administrador con una lista de sitios de empresa (ver el apartado 9), o el propio usuario en `edge://settings/defaultBrowser` (opción "Permitir que los sitios se vuelvan a cargar en el modo Internet Explorer"). Si lo activa el usuario, caduca a los 30 días para cada página.
- **El JavaScript de la página debe ser el que entiende Internet Explorer** (ver el apartado 6).

### Otras restricciones

- **Sistema operativo.** Solo Windows 10 u 11. No hay versión para macOS, Linux, Android ni iOS.
- **Arquitectura.** Hay DLL para x86, x64 y ARM64. Un programa solo puede cargar la DLL de su misma arquitectura, y el control solo puede usar el driver del DNIe de esa misma arquitectura (ver el apartado 9).
- **Tarjeta.** Hacen falta un lector, el DNIe y el driver PKCS#11 del DNIe de la FNMT-RCM (2.0.0.13 o posterior).
- **Qué firma.** Un resumen SHA-256 (de un texto o de un archivo que calcula la página), con una firma en bruto. No genera firmas CAdES, XAdES ni PAdES.
- **Seguridad.** El control está marcado como "seguro para scripting": cualquier página que el navegador deje ejecutar puede usarlo. Por eso pide el PIN y el permiso del usuario en ventanas propias (ver el apartado 14).

Fuera de un navegador no hay restricción de programa. Puede usar el control cualquiera que hable COM: PowerShell (es lo que hacen las pruebas), *Windows Script Host* o VBA.

## 3. Puesta en marcha en diez minutos

### Requisitos

- Windows 10 u 11.
- Para compilar: Visual Studio 2022 o sus *Build Tools*, con las herramientas de C++, **ATL** y el SDK de Windows. Para ARM64, también las herramientas de C++ y ATL para ARM64.
- Para usarlo: el driver del DNIe instalado (2.0.0.13 o posterior), un lector de tarjetas y un DNIe.

Si no tiene Visual Studio, las *Build Tools* se instalan con esta orden. Descarga varios GB y pide permisos de administrador:

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

Para compilar también para ARM64, añada a la lista `--add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --add Microsoft.VisualStudio.Component.VC.ATL.ARM64`.

Sin ellas, `scripts\compilar.cmd` termina con "no se encuentra Visual Studio ni sus Build Tools (falta vswhere.exe)".

### Pasos

Desde la carpeta `activex`:

```bat
scripts\compilar.cmd      :: genera Win32\Release, x64\Release y ARM64\Release
scripts\registrar.cmd     :: registra el control para el usuario actual
web\abrir.cmd         :: abre la página de ejemplo
```

`compilar.cmd` admite una sola plataforma: `scripts\compilar.cmd x64`. Si no están instaladas las herramientas de ARM64, avisa y compila x86 y x64.

Ninguno de los tres necesita permisos de administrador: `registrar.cmd` registra el control solo para el usuario actual. Registra la DLL de 32 bits y la nativa del equipo (x64 o ARM64). Para deshacer el registro: `scripts\desregistrar.cmd`.

`abrir.cmd` abre la HTA con el `mshta` nativo, que carga la DLL de 64 bits (o la ARM64 en un equipo ARM). `web\abrir.cmd 32` la abre con el `mshta` de 32 bits, para probar la DLL x86.

Tras recompilar no hace falta registrar de nuevo, porque la DLL sigue en la misma ruta. Sí hay que cerrar antes la página, la HTA o el PowerShell que la tengan cargada. Si alguno sigue abierto, `compilar.cmd` renombra la DLL bloqueada y compila igualmente.

### La página de ejemplo

La página reúne todas las funciones del control: conexión, información del chip, datos personales, MRZ, foto y firma manuscrita, certificados, claves públicas, PIN, firma y verificación, y firma de un archivo (la página calcula su SHA-256 y el control firma ese resumen).

Para ver la foto y la firma manuscrita, que vienen en JPEG 2000, la página usa el decodificador `openjpeg.js` de su misma carpeta: OpenJPEG compilado a JavaScript, con licencia BSD (ver `AVISOS-TERCEROS.md`). Sin él, el resto de la página funciona y avisa de que falta.

Estos pasos se dan desde una consola. Para trabajar desde un entorno de desarrollo, vea el apartado 4 (Visual Studio Code) o el apartado 5 (Visual Studio).

## 4. Importar y compilar en Visual Studio Code

Visual Studio Code es un editor: no trae compilador y no entiende por sí mismo los proyectos `.vcxproj`. Por eso aquí no se "importa" un proyecto, sino que se abre su carpeta. Para compilar, VS Code llama a los mismos scripts del apartado 3. El proyecto ya trae esa configuración hecha, en la carpeta `.vscode`.

### Qué hace falta

- Windows 10 u 11. En otro sistema la carpeta se puede abrir y leer, pero no compilar.
- Visual Studio Code.
- Visual Studio 2022 o sus *Build Tools*, con ATL (ver el apartado 3). Son las que compilan.
- La extensión **C/C++** de Microsoft (`ms-vscode.cpptools`). No hace falta para compilar, pero sí para el autocompletado y para depurar. VS Code propone instalarla al abrir la carpeta.

### Abrir el proyecto

1. En VS Code, elija **Archivo → Abrir carpeta…** y seleccione la carpeta `activex`, la que contiene `ActiveXDNIe.sln`. No abra una subcarpeta ni un archivo suelto: las tareas no aparecerían.
2. Si VS Code pregunta si confía en los autores de la carpeta, responda que sí. En "modo restringido" las tareas no se ejecutan.
3. Acepte instalar la extensión recomendada cuando VS Code lo proponga.

Desde una consola es lo mismo con `code .` en la carpeta `activex`.

### Compilar

Pulse **Ctrl+Mayús+B**. Se ejecuta la tarea por defecto, que llama a `scripts\compilar.cmd` y genera las DLL de Release. El resultado se ve en el panel "Terminal". Los errores y avisos del compilador aparecen además en el panel "Problemas", con enlace a la línea del código.

Las demás tareas están en **Terminal → Ejecutar tarea…**:

| Tarea | Qué hace |
| --- | --- |
| Compilar (Release, x86, x64 y ARM64) | Ejecuta `scripts\compilar.cmd`. Es la de `Ctrl+Mayús+B`. |
| Compilar solo x64 | Ejecuta `scripts\compilar.cmd x64`. |
| Registrar el ActiveX | Ejecuta `scripts\registrar.cmd`. |
| Desregistrar el ActiveX | Ejecuta `scripts\desregistrar.cmd`. |
| Abrir la página de ejemplo | Ejecuta `web\abrir.cmd`. |
| Abrir la página de ejemplo (32 bits) | Ejecuta `web\abrir.cmd 32`. |

También puede escribir esas órdenes en el terminal integrado (**Terminal → Nuevo terminal**). Las tareas no hacen otra cosa.

### Autocompletado

`.vscode\c_cpp_properties.json` indica a la extensión C/C++ dónde están las cabeceras (las del proyecto, con las de PKCS#11 en `pkcs11\`) y con qué opciones se compila. Solo sirve al editor: no interviene en la compilación.

Un detalle: la cabecera `ActiveXDNIe_i.h` la genera el compilador de interfaces (MIDL) al compilar. Hasta la primera compilación no existe, y el editor subraya en rojo su `#include` en `DLL_ADMIN.h`. No es un error del código: compile una vez y desaparece.

### Depurar

Con el control compilado y registrado, ponga un punto de interrupción (por ejemplo, en `Firmar`, en `DLL_ADMIN.cpp`) y pulse **F5**. La configuración "Depurar el ActiveX en la HTA (64 bits)", de `.vscode\launch.json`, arranca la página de ejemplo con el `mshta.exe` nativo. La ejecución se detiene cuando la página llama al control. Hay otra configuración para la HTA de 32 bits.

Se depura la DLL de Release, que es la registrada. Está optimizada, así que al avanzar paso a paso el depurador puede saltarse líneas. Para depurar con más comodidad, use Visual Studio con la configuración Debug (ver el apartado 5).

### Los archivos de la carpeta .vscode

| Archivo | Para qué |
| --- | --- |
| `tasks.json` | Las tareas: compilar, registrar, desregistrar y abrir la página. |
| `c_cpp_properties.json` | Rutas y opciones para el autocompletado. |
| `launch.json` | Las configuraciones de depuración. |
| `extensions.json` | La extensión que VS Code recomienda instalar. |

## 5. Importar y compilar en Visual Studio

El proyecto es una solución nativa de Visual Studio, así que no hay nada que importar ni que convertir: se abre y se compila. Visual Studio usa el mismo `ActiveXDNIe.vcxproj` y el mismo MSBuild que `scripts\compilar.cmd`.

### Qué hace falta

- Visual Studio 2022, en cualquier edición. La Community, gratuita, es suficiente.
- La carga de trabajo **"Desarrollo para el escritorio con C++"**, que incluye el compilador (conjunto de herramientas v143) y el SDK de Windows, y el componente **"ATL de C++ para las herramientas de compilación v143 más recientes"**. Para ARM64, también sus herramientas y ATL para ARM64. Se eligen en el instalador de Visual Studio (*Visual Studio Installer* → Modificar).
- No hace falta MFC.

Con una versión posterior a la 2022, Visual Studio propone al abrir la solución "redirigir" el proyecto a su propio conjunto de herramientas. Puede aceptarlo, o instalar el conjunto v143 desde el instalador y dejar el proyecto como está.

### Abrir la solución

1. Abra Visual Studio y elija **Abrir un proyecto o una solución** (o **Archivo → Abrir → Proyecto o solución…**).
2. Seleccione `ActiveXDNIe.sln`, en la carpeta `activex`. También vale hacer doble clic sobre ese archivo en el Explorador de Windows.
3. En el **Explorador de soluciones** aparece el proyecto `ActiveXDNIe`, con sus archivos de código, de cabecera y de recursos.

### Compilar

1. En la barra de herramientas, elija la configuración (**Debug** o **Release**) y la plataforma (**x86**, **x64** o **ARM64**).
2. Elija **Compilar → Compilar solución** (`Ctrl+Mayús+B`).
3. El resultado aparece en la ventana "Salida", y los errores, en la "Lista de errores".

La DLL queda en la misma carpeta que con los scripts:

| Configuración y plataforma | DLL generada |
| --- | --- |
| Release, x86 | `Win32\Release\ActiveXDNIe_x86.dll` |
| Release, x64 | `x64\Release\ActiveXDNIe_x64.dll` |
| Release, ARM64 | `ARM64\Release\ActiveXDNIe_arm64.dll` |
| Debug, x86 / x64 / ARM64 | Lo mismo, en `Debug` en lugar de `Release` |

La plataforma que Visual Studio llama x86 es la que el proyecto y las carpetas llaman Win32.

Para generar las tres DLL de una vez, use **Compilar → Compilación por lotes…**, marque las filas de Release y pulse **Compilar**. Es lo mismo que hace `scripts\compilar.cmd`.

`scripts\registrar.cmd` registra las DLL de **Release**. Compílelas en Release antes de registrar.

Antes de recompilar, cierre la página, la HTA o el PowerShell que tenga cargado el control. Si no, la DLL está en uso y la compilación falla con el error LNK1104. `scripts\compilar.cmd` lo evita renombrando la DLL bloqueada.

Visual Studio compila la plataforma seleccionada en la barra. Si al probar parece que no ha cambiado nada, compruebe que es la de su navegador o `mshta` (normalmente x64).

### Depurar

Una DLL no se ejecuta sola: hay que decir a Visual Studio qué programa la carga.

1. Compile en Release para x64 y registre el control.
2. En el Explorador de soluciones, pulse con el botón derecho sobre el proyecto y elija **Propiedades → Depuración**. Arriba, elija "Todas las configuraciones" y la plataforma x64.
3. En **Comando**, escriba `C:\Windows\System32\mshta.exe`. En **Argumentos del comando**, escriba `"$(SolutionDir)web\EjemploDNIe.hta"`, con las comillas.
4. Ponga un punto de interrupción y pulse **F5**.

Estos datos se guardan en `ActiveXDNIe.vcxproj.user`, un archivo propio de cada equipo, que no forma parte del proyecto.

Para depurar la versión Debug, que no está optimizada, registre antes esa DLL:

```bat
%SystemRoot%\System32\regsvr32.exe /n /i:user x64\Debug\ActiveXDNIe_x64.dll
```

Al terminar, ejecute de nuevo `scripts\registrar.cmd` para dejar registrada la de Release.

## 6. Usar el control en su página

### El ejemplo mínimo

Leer los datos personales:

```html
<button onclick="leer()">Leer mis datos</button>

<script>
  function leer() {
    // 1. Crear el control. Falla si el navegador no admite ActiveX
    //    o si el control no está registrado en el equipo.
    var ax;
    try { ax = new ActiveXObject("FNMT.ActiveXDNIe"); }
    catch (e) { alert("No se puede usar el ActiveX del DNIe en este equipo."); return; }

    // 2. Conectar con la tarjeta.
    ax.InicializarConexion();
    if (ax.lastresultOut != 0) { alert("Error al conectar: " + ax.lastresultOut); return; }

    // 3. Leer. La primera lectura pide permiso al usuario (y el PIN con el DNIe 3.0).
    var nombre = ax.NombreOut;
    if (ax.lastresultOut == 0) {
      alert(nombre + " " + ax.Apellido1Out + " " + ax.Apellido2Out + "\nDNI: " + ax.NumeroDNIOut);
    } else {
      alert("No se han podido leer los datos: " + ax.lastresultOut);
    }

    // 4. Desconectar.
    ax.FinalizarConexion();
  }
</script>
```

Firmar es igual de corto:

```js
ax.InicializarConexion();
ax.EtiquetaKeyPrivIn = "KprivFirmaDigital";    // en el DNIe 4.0: "KprivFirmaDigitalEC"
ax.DatosIn = "Texto que se firma";
ax.Firmar();                                    // el control pide el PIN en su ventana
if (ax.lastresultOut == 0) {
  var firma = ax.FirmaOut;                      // binario: un carácter por byte
}
ax.FinalizarConexion();
```

El ejemplo completo, con todas las funciones, está en `web\EjemploDNIe.html`.

### Seis cosas que conviene saber

**1. Los errores no son excepciones.** Los métodos no lanzan excepciones: dejan el resultado en `lastresultOut`. `0` es correcto y cualquier otro valor es un código PKCS#11 (ver "Códigos de resultado" en el apartado 8). Compruébelo después de cada llamada.

**2. Sí hace falta `try/catch` al crear el control.** `new ActiveXObject(...)` lanza una excepción si el navegador no admite ActiveX o si el control no está registrado con la arquitectura del navegador. Capture ese caso y explíqueselo al usuario. `web\EjemploDNIe.html` muestra cómo.

**3. Primero se rellenan las entradas y después se llama al método.** Las propiedades `...In` son entradas y las `...Out` son salidas. Por ejemplo: `EtiquetaKeyPrivIn` + `DatosIn`, después `Firmar()`, después `FirmaOut`.

**4. Los datos binarios van "un carácter por byte".** Certificados, firmas, claves, la foto... llegan como un texto en el que cada carácter es un byte (0 a 255). Para obtener los bytes:

```js
function aBytes(s) {
  var b = new Uint8Array(s.length);
  for (var i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 0xFF;
  return b;
}
```

En Internet Explorer no use `Array.prototype.slice.apply` ni `String.fromCharCode.apply` con un `Uint8Array`: fallan. Recorra los bytes con un bucle, como arriba.

**5. Las llamadas son bloqueantes.** Mientras el control trabaja o muestra la ventana del PIN, la página no se repinta. Para mostrar un "Leyendo…", escríbalo y llame al control dentro de un `setTimeout`, para dar tiempo al navegador a pintarlo. El ejemplo lo hace en `Ejecutar()`.

```js
mostrar("Leyendo…");
setTimeout(function () {
  var nombre = ax.NombreOut;
  // ...
}, 50);
```

**6. Escriba JavaScript que entienda Internet Explorer.** Nada de `const`, `let`, funciones flecha, plantillas de texto ni `fetch`. Use `var` y `function`.

### El cambio de tarjeta

Antes de cada operación, el control comprueba si se ha cambiado la tarjeta del lector. Si ha cambiado, devuelve `0x32` (`CKR_DEVICE_REMOVED`) una vez y queda conectado con la nueva. Si no hay tarjeta, devuelve `0xE0` (`CKR_TOKEN_NOT_PRESENT`). En los dos casos, borre de la página los datos que mostraba, porque eran de la tarjeta anterior. El ejemplo lo hace en `OlvidarTarjeta()`.

## 7. Llevar los datos y la firma al servidor

El control trabaja en el equipo del usuario. En una aplicación web, lo normal es que los datos o la firma acaben en el servidor. Este apartado explica cómo enviarlos y qué puede comprobar el servidor.

### Lo primero: el servidor no puede fiarse de la página

La página se ejecuta en un equipo que usted no controla, y lo que envía se puede falsear. Eso tiene dos consecuencias:

- **Los datos personales que lee la página no prueban la identidad de nadie.** Sirven para rellenar un formulario y ahorrar trabajo al usuario, pero el servidor no puede comprobar que vienen de un DNIe auténtico. Si necesita saber quién es el usuario, use su certificado: la autenticación con certificado de cliente del servidor web, o una firma que el servidor verifique (más abajo).
- **La verificación del navegador no vale para el servidor.** Que `Verificar` devuelva `0` en el equipo del usuario no demuestra nada al servidor. El servidor tiene que verificar la firma por su cuenta.

### El recorrido completo de una firma

1. La página pide al servidor un texto que firmar. Si cambia en cada petición (por ejemplo, con un número aleatorio y la fecha), una firma antigua no sirve para hacerse pasar por el usuario.
2. La página lee el certificado de firma (`ObtenerCertificado`) y firma el texto (`Firmar`). El control pide el PIN.
3. La página envía al servidor el texto, la firma y el certificado.
4. El servidor comprueba que el texto es el que dio, verifica la firma con el certificado, comprueba que el certificado encadena con la raíz del DNIe y comprueba quién ha firmado, por su NIF.

### Enviar la firma

`FirmaOut` y `DatosOut` son binarios, un carácter por byte. Para enviarlos, conviértalos a Base64 con `btoa`, que admite ese formato:

```js
ax.EtiquetaCertificadoIn = "CertFirmaDigital";
ax.ObtenerCertificado();
var certificado = btoa(ax.DatosOut);            // certificado en DER, en Base64

ax.EtiquetaKeyPrivIn = "KprivFirmaDigital";     // en el DNIe 4.0: "KprivFirmaDigitalEC"
ax.DatosIn = textoDelServidor;
ax.Firmar();
var firma = btoa(ax.FirmaOut);                  // firma en bruto, en Base64

document.getElementById("certificado").value = certificado;   // campos ocultos de un formulario
document.getElementById("firma").value = firma;
```

### Verificar la firma en el servidor

La firma es "en bruto": RSA PKCS#1 v1.5 con SHA-256 en el DNIe 3.0, y ECDSA P-256 con SHA-256, en formato `r‖s` (64 bytes), en el DNIe 4.0. El servidor debe comprobar:

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
```

El control convierte `DatosIn` a bytes con la página de códigos ANSI de Windows (en un Windows en español, la 1252), como el ActiveX publicado. El servidor tiene que usar la misma conversión para obtener los mismos bytes. Lo más sencillo es firmar textos solo ASCII (letras sin acentos, números y signos básicos), con los que no hay diferencia; un texto aleatorio en hexadecimal o en Base64, por ejemplo.

El certificado de la autoridad intermedia (por ejemplo, *AC DNIE 007*) no lo entrega el control con el del firmante. Si la cadena no se completa, añádalo a `cadena.ChainPolicy.ExtraStore`. Está en el propio DNIe (`ObtenerCertificado` con la etiqueta `CertCAIntermediaDGP`) y en el portal del DNIe.

**Con OpenSSL.** Para una firma RSA:

```
openssl dgst -sha256 -verify clave.pem -signature firma.bin texto.txt
```

Donde `clave.pem` es la clave pública del certificado (`openssl x509 -inform DER -in certificado.cer -pubkey -noout > clave.pem`). Para ECDSA, OpenSSL espera la firma en DER y no en `r‖s`, así que hay que convertirla antes. En un servidor es más cómodo hacerlo con una biblioteca, como en el ejemplo de .NET.

**Con otros lenguajes.** En Java, `Signature.getInstance("SHA256withRSA")` o `"SHA256withECDSAinP1363Format"` verifican directamente los dos formatos. Esta guía no incluye ejemplos completos para ellos.

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

Los datos del chip, la foto, la firma manuscrita y el certificado (con el nombre y el NIF del titular) son datos personales sujetos al RGPD. Envíe al servidor solo lo que necesite, decida cuánto tiempo lo conserva y quién puede verlo, y consúltelo con el delegado de protección de datos de su organización. La foto y la firma manuscrita merecen especial cuidado.

## 8. Referencia del control

Se crea con `new ActiveXObject("FNMT.ActiveXDNIe")`. Todas las funciones dejan su resultado en `lastresultOut`.

### Conexión e información

| Miembro | Descripción |
| --- | --- |
| `InicializarConexion()` | Conecta con el DNIe del primer lector que tenga tarjeta. Olvida el permiso y los datos de una conexión anterior. |
| `FinalizarConexion()` | Cierra la conexión y borra de la memoria el PIN, los datos leídos y los resultados (`DatosOut`, `FirmaOut`...). |
| `lastresultOut` | Resultado del último método (ver "Códigos de resultado"). |
| `ObtenerNumeroSerie()` → `NumeroSerieOut`, `ulNumeroSerieLen` | Número de serie de la tarjeta (binario). |
| `ObtenerVersionesLibrerias()` → `ArrayVersionLibreriasLenOut`, `NombreLibreriaOut(i)`, `VersionLibreriaOut(i)` | Versión del control y del driver que usa. |
| `ObtenerVersionDNIe()`, `ObtenerTipoDocumento()` → `DatosOut` | Información de la tarjeta que da el driver. |

### PIN

| Miembro | Descripción |
| --- | --- |
| `PresentarPin()` | Sin `PinIn`, muestra la ventana del PIN del control. Con `PinIn`, presenta ese PIN (compatibilidad con el publicado). |
| `PinIn` | Opcional. Lo recomendable es no usarlo: las operaciones que necesitan el PIN lo piden solas. |

### Etiquetas, certificados y claves

| Miembro | Descripción |
| --- | --- |
| `ObtenerEtiquetasTodosCertificados()` | Etiquetas de los certificados (por ejemplo `CertAutenticacion`, `CertFirmaDigital` y `CertCAIntermediaDGP`). |
| `ObtenerEtiquetasClavesPublicas()` / `ObtenerEtiquetasClavesPrivadas()` | Etiquetas de las claves. Las privadas piden el PIN. |
| `ObtenerEtiquetasDatosPublicos()` / `ObtenerEtiquetasDatosPrivados()` | Etiquetas de los objetos de datos (`DG1`, `DG13`...). Los privados piden el PIN. |
| `ArrayEtiquetasLenOut`, `EtiquetaOut(i)` | Resultado de cualquiera de las anteriores. |
| `EtiquetaCertificadoIn` + `ObtenerCertificado()` → `DatosOut` | Certificado en DER (binario). No pide permiso, como el publicado. |
| `EtiquetaKeyPubIn` + `ObtenerClavePublicaRSA()` → `ModuloOut`, `ExponenteOut` | Clave pública RSA (DNIe 3.0). |
| `EtiquetaKeyPubIn` + `ObtenerClavePublicaEC()` → `DatosOut` | Punto público `04‖X‖Y` (DNIe 4.0). |
| `EtiquetaDatoIn` + `ObtenerDatoPublico()` / `ObtenerDatoPrivado()` → `DatosOut` | Contenido de un objeto de datos. Pide permiso al usuario. |

Etiquetas de las claves: `KprivFirmaDigital` / `KpuFirmaDigital` (firma) y `KprivAutenticacion` / `KpuAutenticacion` (autenticación). En el DNIe 4.0 llevan además la terminación `EC`.

### Firma, verificación y resumen

| Miembro | Descripción |
| --- | --- |
| `EtiquetaKeyPrivIn` + `DatosIn` (o `hashIn`) + `Firmar()` → `FirmaOut` | Firma el SHA-256 de `DatosIn`, o el resumen de 32 bytes que se dé en `hashIn` (por ejemplo, el de un archivo). Pide el PIN si hace falta. `FirmaOut` es binario. |
| `EtiquetaKeyPubIn` + `DatosIn` (o `hashIn`) + `FirmaIn` + `Verificar()` | `0` si la firma es válida; `0xC0` si no. `FirmaIn` admite binario o hexadecimal. |
| `DatosIn` + `Hash()` → `hashOut` | SHA-256 de `DatosIn`. |
| `Hash_Init()`, `Hash_Update()` (añade `DatosIn`), `Hash_Final()` → `hashOut` | SHA-256 por partes. |

`hashIn` se borra después de cada `Firmar` o `Verificar`, así que hay que volver a darlo antes de cada llamada.

### Datos del chip

| Miembro | Descripción |
| --- | --- |
| `NombreOut`, `Apellido1Out`, `Apellido2Out`, `NumeroDNIOut`, `FechaNacimientoOut`, `NacionalidadOut`, `SexoOut`, `FechaExpiracionOut`, `LocalidadNacimientoOut`, `ProvinciaNacimientoOut`, `NombrePadresOut`, `DomicilioOut`, `LocalidadDomicilioOut`, `ProvinciaDomicilioOut` | Texto. La tarjeta se lee la primera vez y el resultado se guarda hasta `FinalizarConexion`. |
| `ObtenerNumeroSerie_Soporte()` → `DatosOut` | Número de soporte del DNI. |
| `ObtenerMRZ()` → `DatosOut` | Las 3 líneas de la MRZ, seguidas (90 caracteres). |
| `ImagenFacialOut`, `ImagenFirmaOut` | Foto y firma manuscrita en JPEG 2000 (binario). El ejemplo las muestra con `openjpeg.js`. |

Todos piden permiso al usuario la primera vez en cada conexión. Con el DNIe 3.0 piden además el PIN.

### Sin función

`pkcs1Padding`, `pkcs1Unpadding`, `ObtenerInfoBiometrica`, `ObtenerClavePublicaRSAFirmada`, `ObtenerClavePublicaECFirmada`, `ObtenerCertCAComponente`, `ObtenerCertificadoCV_CA`, `ObtenerDisplayMessage` y `ObtenerCAN` devuelven `0x54`. `CANLecturaIn`, `BITIn` y `paddingTypeIn` se aceptan, pero no se usan.

### Códigos de resultado

`lastresultOut` contiene un código PKCS#11 (`CKR_...`). Los habituales:

| Código | Nombre | Qué ha pasado |
| --- | --- | --- |
| `0` | `CKR_OK` | Correcto. |
| `0x20` (32) | `CKR_DATA_INVALID` | Un dato de la tarjeta no tiene el formato esperado. |
| `0x21` (33) | `CKR_DATA_LEN_RANGE` | `hashIn` no tiene 32 bytes. |
| `0x30` (48) | `CKR_DEVICE_ERROR` | Error de comunicación con la tarjeta. Sacarla, volver a meterla y conectar de nuevo. |
| `0x32` (50) | `CKR_DEVICE_REMOVED` | **Se ha cambiado la tarjeta.** El control ya está conectado con la nueva; repita la operación. |
| `0x50` (80) | `CKR_FUNCTION_CANCELED` | El usuario ha cancelado la ventana del PIN. |
| `0x54` (84) | `CKR_FUNCTION_NOT_SUPPORTED` | Función no disponible. |
| `0x60` (96) | `CKR_KEY_HANDLE_INVALID` | No existe un objeto con esa etiqueta. |
| `0x63` (99) | `CKR_KEY_TYPE_INCONSISTENT` | La clave no es de ese tipo (RSA / curva elíptica). |
| `0x90` (144) | `CKR_OPERATION_ACTIVE` | Se ha llamado al control mientras tenía abierta la ventana del PIN o del permiso. Espere a que termine la operación anterior. |
| `0xA0` (160) | `CKR_PIN_INCORRECT` | PIN incorrecto. Varios fallos seguidos bloquean el DNIe. |
| `0xA4` (164) | `CKR_PIN_LOCKED` | PIN bloqueado: se desbloquea en un Punto de Actualización del DNIe. |
| `0xB3` (179) | `CKR_SESSION_HANDLE_INVALID` | No hay conexión: llame a `InicializarConexion`. |
| `0xC0` (192) | `CKR_SIGNATURE_INVALID` | La firma no es válida. |
| `0xE0` (224) | `CKR_TOKEN_NOT_PRESENT` | No hay tarjeta en el lector. |
| `0x101` (257) | `CKR_USER_NOT_LOGGED_IN` | Hace falta el PIN. |
| `0x1C2` (450) | `CKR_LIBRARY_LOAD_FAILED` | No se encuentra el driver del DNIe en la carpeta del sistema: no está instalado, o no es de la arquitectura del programa. |
| `0x200` (512) | `CKR_FUNCTION_REJECTED` | El usuario no ha permitido leer sus datos. Se vuelve a preguntar tras desconectar y conectar. |

## 9. Instalarlo en los equipos de los usuarios

Para que una página pueda usar el control, cada equipo necesita cuatro cosas.

### 9.1. Las DLL

Copie a una carpeta del equipo las DLL de las arquitecturas que vaya a usar: `Win32\Release\ActiveXDNIe_x86.dll`, `x64\Release\ActiveXDNIe_x64.dll` y, en equipos ARM, `ARM64\Release\ActiveXDNIe_arm64.dll`. No dependen de nada más: no hay que instalar el *Visual C++ Redistributable*. Las DLL de entrega deben ir firmadas (Authenticode).

### 9.2. El registro

Windows debe saber dónde están. Hay dos formas:

| Alcance | Orden | ¿Administrador? |
| --- | --- | --- |
| Solo el usuario actual | `regsvr32 /n /i:user ActiveXDNIe_x64.dll` | No |
| Todos los usuarios del equipo | `regsvr32 ActiveXDNIe_x64.dll` | Sí |

El registro por usuario tiene una trampa: **un programa ejecutado "como administrador" no lo ve**. Windows, por seguridad, hace que los programas elevados ignoren el registro COM del usuario. Si el control se va a usar desde programas elevados, regístrelo para todo el equipo.

Cada DLL se registra con el `regsvr32` de su arquitectura:

| DLL | `regsvr32` | Para |
| --- | --- | --- |
| `ActiveXDNIe_x86.dll` | `%SystemRoot%\SysWOW64\regsvr32.exe` | Programas de 32 bits (mshta de 32 bits, IE de 32 bits) |
| `ActiveXDNIe_x64.dll` | `%SystemRoot%\System32\regsvr32.exe` | Programas de 64 bits (mshta nativo, PowerShell) |
| `ActiveXDNIe_arm64.dll` | `%SystemRoot%\System32\regsvr32.exe` (en ARM) | Programas ARM64 |

Un programa solo puede cargar una DLL de su misma arquitectura: registre todas las que vayan a necesitarse. Edge en modo IE puede usar procesos de 32 o de 64 bits según su configuración, así que en ese caso conviene registrar las dos. `scripts\registrar.cmd` hace exactamente esto y sirve de modelo para un instalador.

### 9.3. Un navegador que lo admita

- **HTA.** Es lo que usa el ejemplo (`web\EjemploDNIe.hta`). No requiere configurar nada.
- **Internet Explorer 11 o Edge en modo IE.** La página debe estar en la lista del modo IE (en Edge) y en una zona de seguridad que permita ejecutar controles ActiveX marcados como seguros (lo habitual en "Intranet local" y "Sitios de confianza").

El apartado 2, "Restricciones de uso", detalla qué navegadores sirven y las condiciones de cada uno.

### 9.4. El driver del DNIe y un lector de tarjetas

El driver PKCS#11 del DNIe de la FNMT-RCM (2.0.0.13 o posterior) instala `DNIe_P11.dll`, `DNIe_P11_x64.dll` y, en ARM, `DNIe_P11_arm64.dll` en la carpeta del sistema. El control solo los busca allí, y cada DLL del control usa el driver de su misma arquitectura.

### Instalarlo en muchos equipos

Sirve cualquier herramienta de despliegue que copie archivos y ejecute órdenes: un script de inicio de sesión por directiva de grupo, Intune o un paquete MSI, por ejemplo. Tiene que hacer lo mismo que `scripts\registrar.cmd`: copiar las DLL y registrar cada una con su `regsvr32` (con `/s`, sin ventanas). Para todo el equipo, ejecútelo como administrador y sin `/n /i:user`.

### Publicar la página en un servidor web

El ejemplo abre una página local dentro de la HTA. En una aplicación real, la página la sirve un servidor web, y entonces cuentan las condiciones del apartado 2:

- **Internet Explorer 11.** Añada el sitio a "Sitios de confianza" o a "Intranet local": Opciones de Internet → Seguridad → elija la zona → Sitios. En una organización, el administrador puede asignar el sitio a una zona en todos los equipos por directiva de grupo.
- **Edge en modo IE.** El administrador publica una lista de sitios (un XML) en la que la página lleva `<open-in>IE11</open-in>`. La activa con las directivas "Configure Internet Explorer integration" y "Configure the Enterprise Mode Site List", explicadas en https://learn.microsoft.com/en-us/deployedge/edge-ie-mode-policies.
- **HTA.** Con `application="yes"`, la página del marco recibe los mismos permisos que la HTA (ver el comentario de `web/EjemploDNIe.hta`). Cargue en ella solo páginas de confianza.

## 10. Cómo funciona por dentro

### Las piezas

```text
 Página web (JavaScript)
        │  new ActiveXObject("FNMT.ActiveXDNIe")
        ▼
 ┌──────────────────────────── ActiveXDNIe_x64.dll ─────────────────────────────┐
 │  DLL_ADMIN.cpp    El objeto COM: recibe las llamadas de la página, convierte  │
 │                   los datos e interpreta los grupos de datos (DG1, DG13...).  │
 │  Tarjeta.cpp      Todo lo que habla con el driver PKCS#11 y con el lector.    │
 │  PinDialog.cpp    Ventana del PIN.                                            │
 │  Dialogos.cpp     Ventana de permiso y estilos visuales de las ventanas.      │
 │  ActiveXDNIe.cpp  Módulo ATL: registro y creación del objeto.                 │
 └───────────────────────────────────────────────────────────────────────────────┘
        │  C_Initialize, C_OpenSession, C_Login, C_Sign...       PC/SC (solo el lector)
        ▼
 Driver PKCS#11 del DNIe (System32\DNIe_P11_x64.dll)  ──►  lector  ──►  chip del DNIe
```

La idea central: **el control no habla directamente con el chip**. Se lo pide al driver PKCS#11, que ya sabe abrir el canal seguro de cada versión del DNIe. Así el control mejora con cada versión del driver. Con el lector solo habla por PC/SC para dos cosas: saber si se ha cambiado la tarjeta y reiniciarla.

### Dónde están los datos, los certificados y las claves

Todo está en el chip del DNIe, y el driver lo presenta como objetos PKCS#11, cada uno con su etiqueta:

- **Certificados** (`CertAutenticacion`, `CertFirmaDigital`, `CertCAIntermediaDGP`): se pueden leer sin PIN.
- **Claves privadas** (`KprivFirmaDigital`, `KprivAutenticacion`): **nunca salen de la tarjeta**. La tarjeta firma por dentro, y solo después de presentar el PIN.
- **Grupos de datos** (`DG1` con la MRZ, `DG2` con la foto, `DG7` con la firma manuscrita, `DG13` con los datos personales): objetos de datos que el control lee y descompone.

### Qué ocurre al llamar a `Firmar`

1. `Firmar` calcula el SHA-256 de `DatosIn`, o toma `hashIn`.
2. Comprueba que sigue siendo la misma tarjeta. Si ha cambiado, devuelve `0x32` sin firmar.
3. Si no se ha presentado el PIN, muestra la ventana del PIN y lo presenta (`C_Login`).
4. Busca la clave privada por su etiqueta y lee su tipo.
5. Con RSA, firma el DigestInfo de SHA-256 (`CKM_RSA_PKCS`). Con curva elíptica, firma el resumen directamente (`CKM_ECDSA`).
6. Deja la firma en `FirmaOut`.

### Qué ocurre al leer un dato personal

1. Si el grupo (por ejemplo el DG13) ya se leyó en esta conexión y la tarjeta es la misma, se usa el guardado.
2. Con el DNIe 3.0 sin PIN, se pide el PIN, porque la tarjeta no deja leer los grupos sin él.
3. Se pide permiso al usuario (una vez por conexión), mostrando qué página lo pide.
4. Se lee el objeto de datos del driver.
5. Se busca el campo dentro del grupo. Los grupos están en ASN.1 (bloques etiqueta-longitud-valor), y el DG13 son 16 textos en un orden fijo.

**Reinicios de la tarjeta.** Para que cada lectura de un grupo de datos empiece con el canal seguro recién abierto, el control reinicia la tarjeta en el lector entre una lectura y otra cuando no hay PIN presentado, y también si una lectura falla. Si había un PIN presentado, se pierde y se vuelve a pedir cuando haga falta. Está explicado en `LeerDato` (`DLL_ADMIN.cpp`).

### Cambio de tarjeta

Antes de cada operación, el control consulta al lector su contador de inserciones y extracciones. Si ha cambiado:

- olvida el PIN, el permiso y los datos guardados;
- conecta con la tarjeta nueva y devuelve `0x32` una vez, para que la página lo sepa y no mezcle datos de dos tarjetas.

Si no hay tarjeta, devuelve `0xE0`. Está explicado en `Comprobar` (`Tarjeta.cpp`).

### El objeto COM, en pocas palabras

Un control ActiveX es un objeto COM que el navegador crea por su nombre (`FNMT.ActiveXDNIe`). El registro de Windows relaciona ese nombre con un identificador (CLSID) y con la ruta de la DLL. ATL se encarga de lo repetitivo: el registro (`ActiveXDNIe.rgs`, `DLL_ADMIN.rgs`), la creación del objeto y la interfaz que usa JavaScript (`IDispatch`). El control implementa además `IObjectSafety`, para declararse seguro para scripting, e `IObjectWithSite`, para saber qué página lo usa.

## 11. Mapa del código

Orden de lectura recomendado:

| Orden | Archivo | Qué contiene |
| --- | --- | --- |
| 1 | `ActiveXDNIe.idl` | El "contrato": métodos, propiedades y números de método (DISPID). |
| 2 | `web/EjemploDNIe.html` | Cómo se usa cada función desde JavaScript. |
| 3 | `DLL_ADMIN.h` | El objeto COM y qué hace ATL por debajo. |
| 4 | `Tarjeta.h` / `Tarjeta.cpp` | PKCS#11 paso a paso: conectar, PIN, buscar objetos, firmar, verificar. |
| 5 | `DLL_ADMIN.cpp` | Los métodos, la conversión de datos y la lectura de los grupos de datos. |
| 6 | `PinDialog.cpp`, `Dialogos.cpp` | Ventanas del PIN y del permiso. |
| 7 | `ActiveXDNIe.cpp` | Módulo ATL y funciones que exporta la DLL, incluido el registro por usuario (`DllInstall`). |

Archivos de apoyo:

| Archivo | Para qué |
| --- | --- |
| `ActiveXDNIe.rc`, `resource.h`, `res/candado.ico` | Recursos: biblioteca de tipos, diálogo e icono del PIN, versión de la DLL. |
| `ActiveXDNIe.rgs`, `DLL_ADMIN.rgs` | Claves del registro (ProgID, CLSID). |
| `ActiveXDNIe.def` | Funciones que exporta la DLL. |
| `ActiveXDNIe.sln`, `ActiveXDNIe.vcxproj` | Solución y proyecto de Visual Studio. |
| `web/EjemploDNIe.hta`, `web/abrir.cmd` | La página como aplicación de escritorio. |
| `scripts/compilar.cmd` | Compilar sin abrir Visual Studio. |
| `scripts/registrar.cmd`, `scripts/desregistrar.cmd` | Registrar y desregistrar el control. |
| `.vscode/` | Tareas, autocompletado y depuración para Visual Studio Code. |
| `cryptoki.h`, `pkcs11/` | Cabeceras PKCS#11 estándar (v2.20) y las macros de Windows que necesitan. No hace falta el driver para compilar. |
| `tests/*.ps1` | Pruebas con tarjeta, sin mostrar datos personales. |
| `README.md` | Presentación del proyecto. |
| `web/openjpeg.js` | Decodificador de JPEG 2000 para la foto y la firma manuscrita (de terceros, licencia BSD). |
| `AVISOS-TERCEROS.md` | Avisos y licencias de los componentes de terceros (`openjpeg.js` y las cabeceras PKCS#11). |

## 12. Cómo ampliarlo

### Añadir un método o una propiedad

1. **Declararlo en `ActiveXDNIe.idl`**, al final de la interfaz `IDLL_ADMIN` y con un `id` que no esté usado.
2. **Declararlo en `DLL_ADMIN.h`** con `STDMETHOD(...)`.
3. **Implementarlo en `DLL_ADMIN.cpp`**, terminando con `return Resultado(rv);`.
4. **Usarlo en la página de ejemplo** y añadir su prueba (apartado 13).

Reglas:

- **No cambie los `id` ni los parámetros de lo que ya existe**: la interfaz es la del ActiveX publicado y las páginas que lo usan dejarían de funcionar.
- Use tipos de Automatización: `BSTR` (texto o binario de un carácter por byte), `int`, `ULONG`, `BYTE`.
- Devuelva siempre `S_OK` y comunique los fallos con `lastresultOut`. Un `HRESULT` de error se convierte en una excepción de JavaScript.

### Crear su propio control a partir de este

Si va a distribuir un control propio basado en este, cambie su identidad para que no choque con el original: el CLSID y el ProgID (`DLL_ADMIN.rgs`, `ActiveXDNIe.idl`), el LIBID y el IID de la interfaz (`ActiveXDNIe.idl`), y los nombres de las DLL (`ActiveXDNIe.vcxproj`). Genere identificadores nuevos con la herramienta "Crear GUID" de Visual Studio.

### Reutilizar solo el acceso a la tarjeta, sin ActiveX

`Tarjeta.h` / `Tarjeta.cpp` no dependen de COM: cargan el driver, conectan, presentan el PIN, buscan objetos, firman y verifican. Puede usarlos en cualquier programa de Windows en C++. La lectura de los grupos de datos está en `DLL_ADMIN.cpp` (`LeerDato` y las funciones que descomponen el DG13).

### Ideas de ampliación

| Mejora | Por dónde empezar |
| --- | --- |
| Firma de archivo en PKCS#7/CMS (`.p7s`), verificable con otras herramientas | Construir el CMS con CryptoAPI (`CryptMsgOpenToEncode`) a partir de la firma de la tarjeta y del certificado. |
| Validar la cadena de confianza al verificar | `CertGetCertificateChain` con el certificado, en `VerificarConCertificado` (`Tarjeta.cpp`). |
| Limitar qué páginas pueden usar el control | `OrigenPagina()` ya da la dirección: rechazar las que no estén en una lista. |
| Funciones del chip no disponibles (biometría, certificados CV) | Necesitan que el driver publique esos datos. |

## 13. Pruebas

Las pruebas necesitan un DNIe real y están en la carpeta `tests`. No muestran datos personales: solo resultados, longitudes y formatos.

| Script | Qué prueba |
| --- | --- |
| `probar_activex.ps1` | Conexión, certificados y claves; con `-Firma`, PIN, firma y verificación (comprobada también con .NET). Con `-ProgID DLL_ADMINISTRATIVA_DNIE.DLL_ADMIN` prueba el ActiveX publicado, para comparar. |
| `probar_datos.ps1` | Datos personales, MRZ, foto y firma manuscrita (`-Pin` para el DNIe 3.0). |

```powershell
# PowerShell normal de 64 bits, en la carpeta tests
.\probar_activex.ps1            # sin PIN
.\probar_activex.ps1 -Firma     # con PIN, firma y verificación
.\probar_datos.ps1              # datos personales e imágenes (añada -Pin con el DNIe 3.0)
```

Ejecútelas en una ventana de PowerShell nueva cada vez, para que cargue la DLL recién compilada. Con el DNIe 3.0, si una prueba da `48` al presentar el PIN, saque y vuelva a meter la tarjeta (ver el apartado 15).

**Si añade un método, añada su prueba** en el script que corresponda.

Lo que hay que probar a mano en la página: las ventanas del PIN (aceptar, cancelar, «Mostrar el PIN») y del permiso (permitir, no permitir), y el cambio de tarjeta con la página abierta.

## 14. Seguridad

Lea este apartado antes de instalar el control fuera de un entorno de pruebas.

- **El control está marcado como "seguro para scripting"**, como el publicado. Eso significa que cualquier página que el navegador deje ejecutar en modo IE puede usarlo. Por eso:
  - el **PIN** lo pide siempre el control en su propia ventana; la página no lo ve, salvo que lo dé ella misma en `PinIn`;
  - los **datos personales** se entregan solo si el usuario lo permite. La ventana indica el sitio (o la página local) que lo pide, y el permiso dura hasta que se desconecta;
  - los **certificados** se entregan sin preguntar, igual que en el ActiveX publicado. Tenga en cuenta que el certificado contiene el nombre y el número de DNI del titular.
- **El PIN** se borra de memoria en cuanto se presenta (`SecureZeroMemory`); no se guarda en disco ni se devuelve a la página. Los grupos de datos leídos y las entradas y salidas de la última operación (`DatosOut`, `FirmaOut`, `DatosIn`...) se borran igual al conectar, al desconectar y al cambiar de tarjeta. Con la casilla "Mostrar el PIN" marcada queda a la vista en pantalla: está desmarcada por defecto y es el usuario quien decide.
- **Una operación cada vez.** Mientras está abierta la ventana del PIN o la del permiso, el control rechaza cualquier otra llamada con `0x90`, para que la página no pueda cambiar de tarjeta, desconectar o leer datos por debajo de la ventana.
- **El driver se carga solo desde la carpeta del sistema**, nunca desde el PATH ni desde la carpeta de la página.
- **Verifique siempre en el servidor.** La verificación en el navegador sirve para informar al usuario, no como prueba (ver el apartado 7).
- **Los datos leídos por la página no prueban la identidad.** El servidor no puede comprobar que vienen de un DNIe auténtico (ver el apartado 7).
- **La verificación no valida la confianza ni la revocación** del certificado. Un certificado revocado seguiría verificándose como válido.
- **Las DLL deben firmarse** (Authenticode) antes de distribuirlas.

## 15. Problemas frecuentes

### «No se puede crear el ActiveX» / «El servidor de Automatización no puede crear el objeto»

- El control no está registrado, o solo se registró la DLL de otra arquitectura. Por ejemplo, una HTA abierta con el `mshta` de 32 bits necesita la DLL x86. Use `web\abrir.cmd`, que abre el `mshta` nativo.
- Está abriendo la página en un navegador sin ActiveX (ver el apartado 2).

### `InicializarConexion` devuelve `0x1C2`

No se encuentra el driver del DNIe de esa arquitectura en la carpeta del sistema. Instale el driver.

### `InicializarConexion` devuelve `5` (`0x5`) con el DNIe 4.0

Otro programa está usando el DNIe a la vez: por ejemplo, Firefox con el driver del DNIe cargado como dispositivo de seguridad, otra ventana con el ejemplo abierta o una consola de PowerShell de una prueba anterior. Con el DNIe 4.0, cada programa abre su propio canal seguro con la tarjeta y no pueden convivir. Cierre los demás programas que usen el DNIe, saque la tarjeta, vuelva a insertarla y repita la operación.

### En un Windows sobre ARM64 no se crea el control o no encuentra el driver

Use la DLL ARM64 con un programa ARM64, como el `mshta` nativo que abre `web\abrir.cmd`. Cada DLL del control necesita el driver del DNIe de su misma arquitectura en la carpeta del sistema.

### `48` (`0x30`) con el DNIe 3.0 al presentar el PIN

Saque la tarjeta, vuelva a meterla y conecte de nuevo.

### Los datos personales del DNIe 3.0 piden el PIN

Es normal: la tarjeta no deja leerlos sin PIN con el canal que usa el driver. El ActiveX publicado los leía sin PIN porque usaba el canal con el CAN.

### La página sigue mostrando datos de otro DNIe

El control solo se entera del cambio de tarjeta en la siguiente operación, que devuelve `0x32`. La página debe borrar entonces lo que mostraba (ver `OlvidarTarjeta()` en el ejemplo).

### No se ven la foto ni la firma manuscrita

Falta `openjpeg.js` en la carpeta de la página. Cópielo allí y vuelva a abrirla.

### El control funciona en una consola normal, pero no en un programa "como administrador"

`scripts\registrar.cmd` registra el control solo para el usuario actual, y los programas elevados no ven ese registro. Regístrelo para todo el equipo, como administrador (ver el apartado 9.2).

### Al recompilar: «LNK1104: no se puede abrir el archivo ActiveXDNIe_x64.dll»

La DLL está cargada por una página, una HTA o un PowerShell abiertos. Ciérrelos, o use `scripts\compilar.cmd`, que renombra la DLL bloqueada y compila igualmente.

### Al compilar sale una DLL antigua, o la de otra arquitectura

Visual Studio compila la plataforma seleccionada en la barra (puede ser ARM64). Use `scripts\compilar.cmd x64`, o elija `x64` en Visual Studio.

### `compilar.cmd` dice que ARM64 se ha omitido

No están instaladas las herramientas de C++ y ATL para ARM64. Instálelas (ver el apartado 3) o compile solo x86 y x64.

### La ventana de la HTA sale con el icono genérico

Con el modo de documento IE10 o superior, `mshta` ignora el icono de la HTA. La página necesita IE10 o superior para mostrar la foto y la firma (canvas y `Uint8Array`), así que se mantiene así.

## 16. Glosario

Términos que aparecen en esta guía, explicados para quien empieza.

| Término | Qué es |
| --- | --- |
| ActiveX | Tecnología de Microsoft para que una página use un componente instalado en el equipo. Solo existe en el motor de Internet Explorer. |
| ATL | Biblioteca de Microsoft para escribir objetos COM en C++ con menos código. |
| Autoridad de certificación (AC) | Entidad que emite certificados y responde de ellos. La AC raíz está en lo alto de la cadena. Las intermedias, como *AC DNIE 007*, emiten los certificados de los ciudadanos. |
| Base64 | Forma de escribir datos binarios como texto. |
| Cadena de certificación | Secuencia que va del certificado del firmante a una AC raíz de confianza, pasando por las intermedias. |
| Certificado | Documento electrónico que une una clave pública con una persona. El DNIe tiene dos: el de autenticación y el de firma. |
| CLSID / ProgID | Identificador único de un objeto COM, y el nombre legible con el que se crea (`FNMT.ActiveXDNIe`). |
| COM | Modelo de componentes de Windows. Un ActiveX es un objeto COM. |
| DER | Codificación binaria de certificados. Es la que devuelve `ObtenerCertificado`. |
| DG (grupo de datos) | Cada bloque de información del chip: DG1 (MRZ), DG2 (foto), DG7 (firma manuscrita), DG13 (datos personales). |
| ECDSA | Algoritmo de firma con claves de curva elíptica. Es el del DNIe 4.0. |
| HTA | Página web que Windows ejecuta como aplicación, con `mshta.exe` y el motor de Internet Explorer. |
| JPEG 2000 | Formato de imagen de la foto y la firma manuscrita del chip. Los navegadores no lo muestran solos: el ejemplo usa `openjpeg.js`. |
| MRZ | Zona de lectura mecánica: las líneas de caracteres del reverso del DNI. |
| NIF | Número de identificación fiscal. En el certificado del DNIe identifica al titular (ver el apartado 7). |
| PC/SC | Interfaz estándar de Windows para hablar con lectores de tarjetas. |
| PIN | Clave secreta del DNIe. Sin ella, la tarjeta no firma. |
| PKCS#11 | Interfaz estándar para usar tarjetas criptográficas. El driver del DNIe la ofrece, y el control la usa. |
| Revocación | Anulación de un certificado antes de que caduque, por ejemplo por pérdida de la tarjeta. Se consulta por OCSP o en listas de revocación (CRL). |
| RSA | Algoritmo de firma de clave pública. Es el del DNIe 3.0. |
| SHA-256 | Algoritmo de resumen. La firma se hace sobre el resumen de los datos, no sobre los datos. |
| Zona de seguridad | Clasificación de los sitios en Internet Explorer: Internet, Intranet local, Sitios de confianza… Cada zona permite o bloquea los ActiveX. |
