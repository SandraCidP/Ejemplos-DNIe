// ============================================================================
//  Main.cpp  -  Anfitrión de "Native Messaging": el programa que firma
// ============================================================================
//
//  QUÉ ES ESTE PROGRAMA
//  --------------------
//  Una página web no puede usar el DNIe: el navegador no la deja salir de su
//  "caja de arena". Lo que sí permiten Chrome, Edge y Firefox es que una
//  EXTENSIÓN arranque un programa instalado en el equipo y hable con él. Ese
//  mecanismo se llama Native Messaging, y el programa, "anfitrión nativo".
//  Este archivo es ese programa.
//
//    Página web  ──▶  Extensión  ──▶  DNIeAnfitrion.exe  ──▶  Windows / DNIe
//    (dniefirma.js)   (contenido.js   (este archivo +         (Crypto.cpp)
//                      y fondo.js)     Crypto.cpp)
//
//  CÓMO HABLA CON EL NAVEGADOR
//  ---------------------------
//  El navegador arranca el .exe y se comunica con él por la entrada y la
//  salida estándar (stdin y stdout), las mismas que usa un programa de
//  consola. Cada mensaje, en los dos sentidos, tiene este formato:
//
//      ┌──────────────────┬─────────────────────────────────────┐
//      │ 4 bytes: longitud│ texto JSON en UTF-8 (esa longitud)  │
//      └──────────────────┴─────────────────────────────────────┘
//
//  La longitud es un entero de 32 bits sin signo, en el orden de bytes del
//  equipo (en todos los Windows, "little-endian": el byte bajo primero).
//
//  QUÉ MENSAJES ENTIENDE
//  ---------------------
//  Petición (la envía la extensión):   {"operacion":"firmar"}
//
//    "version"    ¿Qué DNIe hay en el lector? No muestra nada ni pide el PIN.
//    "firmar"     Pide el archivo con el diálogo "Abrir", pide el PIN y firma.
//    "verificar"  Pide el archivo con el diálogo "Abrir" y comprueba su firma.
//
//  Respuesta, si todo va bien:
//    version      {"correcto":true,"version":"4.0"}      ("3.0", "4.0" o "")
//    firmar       {"correcto":true,"archivo":"informe.pdf",
//                  "archivoFirma":"informe.pdf.p7s","firmante":"...",
//                  "firma":"MIIG..."}                    (la firma, en Base64)
//    verificar    {"correcto":true,"archivo":"informe.pdf",
//                  "archivoFirma":"informe.pdf.p7s","firmante":"..."}
//
//  Respuesta, si algo falla:
//    {"correcto":false,"codigo":"cancelado","error":"Operación cancelada..."}
//    {"correcto":false,"codigo":"error","error":"...explicación..."}
//
//  DOS DECISIONES DE SEGURIDAD
//  ---------------------------
//  1. El archivo lo elige SIEMPRE el usuario, aquí, con el diálogo de
//     Windows. La página no puede indicar una ruta, y en las respuestas solo
//     viajan NOMBRES de archivo, nunca la carpeta: la página no llega a saber
//     nada del disco del usuario.
//  2. La petición no lleva más datos que el nombre de la operación. Así, lo
//     peor que puede hacer quien consiga enviar un mensaje es abrir un
//     diálogo que el usuario puede cancelar.
//
//  Quién puede arrancar este programa lo decide el navegador con el
//  "manifiesto" que escribe scripts\registrar.cmd: solo nuestra extensión.
// ============================================================================
#include "Crypto.h"
#include "Module.h"

#include <commdlg.h>     // GetOpenFileNameW (diálogo "Abrir")
#include <wincrypt.h>    // CryptBinaryToStringW (Base64)

#include <cwchar>        // swprintf_s
#include <string>
#include <vector>

// Pide a Windows los controles con el aspecto actual (botones y casillas con
// "estilos visuales"). Sin esta línea, los diálogos de un .exe se dibujan
// con el aspecto de Windows 95.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Definición de la variable declarada en Module.h.
HINSTANCE g_hInstance = nullptr;

// Tamaño máximo de una petición. Las nuestras ocupan unos 25 bytes; el
// límite evita reservar memoria de más si llega algo que no es una petición.
static const DWORD TAMANO_MAXIMO_PETICION = 64 * 1024;

// Tamaño máximo de la firma que devolvemos a la página. El navegador no
// admite mensajes del anfitrión de más de 1 MB, y una firma con sus
// certificados ocupa unos pocos KB.
static const DWORD TAMANO_MAXIMO_FIRMA = 512 * 1024;

// ============================================================================
//  1. Leer y escribir mensajes (el formato del navegador)
// ============================================================================

// Lee exactamente 'cantidad' bytes. ReadFile puede entregar menos de los
// pedidos (los que haya en ese momento), así que hay que insistir.
static bool LeerBytes(HANDLE entrada, void* destino, DWORD cantidad)
{
    BYTE* posicion = (BYTE*)destino;
    while (cantidad > 0)
    {
        DWORD leidos = 0;
        if (!ReadFile(entrada, posicion, cantidad, &leidos, nullptr) || leidos == 0)
            return false;      // Error, o el navegador ha cerrado la conexión.
        posicion += leidos;
        cantidad -= leidos;
    }
    return true;
}

static bool EscribirBytes(HANDLE salida, const void* origen, DWORD cantidad)
{
    const BYTE* posicion = (const BYTE*)origen;
    while (cantidad > 0)
    {
        DWORD escritos = 0;
        if (!WriteFile(salida, posicion, cantidad, &escritos, nullptr) || escritos == 0)
            return false;
        posicion += escritos;
        cantidad -= escritos;
    }
    return true;
}

// Lee un mensaje completo: 4 bytes de longitud y después el JSON.
// Devuelve false cuando no hay más mensajes (el navegador cerró la conexión).
static bool LeerMensaje(HANDLE entrada, std::string& json)
{
    DWORD longitud = 0;
    if (!LeerBytes(entrada, &longitud, sizeof(longitud)))
        return false;
    if (longitud == 0 || longitud > TAMANO_MAXIMO_PETICION)
        return false;

    json.resize(longitud);
    return LeerBytes(entrada, &json[0], longitud);
}

static bool EscribirMensaje(HANDLE salida, const std::string& json)
{
    const DWORD longitud = (DWORD)json.size();
    return EscribirBytes(salida, &longitud, sizeof(longitud)) &&
           EscribirBytes(salida, json.data(), longitud);
}

// ============================================================================
//  2. JSON mínimo
// ============================================================================
//  No usamos ninguna biblioteca de JSON: el ejemplo solo necesita leer UN
//  campo de la petición y escribir respuestas de pocos campos de texto.
// ============================================================================

// Windows trabaja con texto UTF-16 (std::wstring); el navegador, con UTF-8.
static std::string AUtf8(const std::wstring& texto)
{
    if (texto.empty())
        return std::string();

    // Primera llamada: cuántos bytes hacen falta. Segunda: convertir.
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(), &utf8[0], bytes, nullptr, nullptr);
    return utf8;
}

// Convierte un texto en un valor de JSON: entre comillas y con los
// caracteres especiales "escapados". Sin esto, un nombre de archivo con
// comillas rompería la respuesta.
static std::string TextoJson(const std::wstring& texto)
{
    std::wstring resultado = L"\"";
    for (const wchar_t caracter : texto)
    {
        if (caracter == L'"' || caracter == L'\\')
        {
            resultado += L'\\';
            resultado += caracter;
        }
        else if (caracter < 0x20)      // Caracteres de control (salto de línea...).
        {
            wchar_t codigo[8];
            swprintf_s(codigo, L"\\u%04x", (unsigned)caracter);
            resultado += codigo;
        }
        else
            resultado += caracter;
    }
    resultado += L'"';
    return AUtf8(resultado);
}

// Devuelve el valor del campo "operacion" de la petición ("" si no está).
//
// No es un analizador de JSON completo, ni hace falta: la petición la
// construye siempre nuestra extensión (fondo.js), con esta forma exacta y
// con un nombre de operación sin caracteres especiales. Cualquier otra cosa
// da una operación desconocida, que se rechaza.
static std::string LeerOperacion(const std::string& json)
{
    const std::string clave = "\"operacion\"";

    size_t posicion = json.find(clave);
    if (posicion == std::string::npos)
        return std::string();

    posicion = json.find(':', posicion + clave.size());
    if (posicion == std::string::npos)
        return std::string();

    const size_t inicio = json.find('"', posicion);
    if (inicio == std::string::npos)
        return std::string();

    const size_t fin = json.find('"', inicio + 1);
    if (fin == std::string::npos)
        return std::string();

    return json.substr(inicio + 1, fin - inicio - 1);
}

// Respuesta de fallo. 'codigo' permite a la página distinguir los casos sin
// tener que interpretar el texto: "cancelado" o "error".
static std::string RespuestaDeError(const wchar_t* codigo, const std::wstring& explicacion)
{
    return "{\"correcto\":false,\"codigo\":" + TextoJson(codigo) +
           ",\"error\":" + TextoJson(explicacion) + "}";
}

// Fallo comunicado por Crypto.cpp. Sus funciones explican el motivo con un
// texto; cuando es el usuario quien ha cancelado (el PIN, o la elección de
// certificado), ese texto empieza por "Operación cancelada".
static std::string RespuestaDeFalloDeFirma(const std::wstring& explicacion)
{
    const bool cancelado = explicacion.find(L"cancelada") != std::wstring::npos;
    return RespuestaDeError(cancelado ? L"cancelado" : L"error", explicacion);
}

static std::string RespuestaDeCancelacion()
{
    return RespuestaDeError(L"cancelado", L"Operación cancelada: no se seleccionó ningún archivo.");
}

// ============================================================================
//  3. Utilidades de archivos
// ============================================================================

// "C:\Carpeta\informe.pdf" -> "informe.pdf". Es lo único que se cuenta a la
// página sobre dónde está el archivo.
static std::wstring NombreDeArchivo(const std::wstring& ruta)
{
    const size_t barra = ruta.find_last_of(L"\\/");
    return barra == std::wstring::npos ? ruta : ruta.substr(barra + 1);
}

static bool ExisteArchivo(const std::wstring& ruta)
{
    const DWORD atributos = GetFileAttributesW(ruta.c_str());
    return atributos != INVALID_FILE_ATTRIBUTES && (atributos & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Lee el archivo de firma recién creado y lo devuelve en Base64, que es la
// forma habitual de meter datos binarios en un JSON.
static bool LeerArchivoEnBase64(const std::wstring& ruta, std::wstring& base64)
{
    HANDLE archivo = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (archivo == INVALID_HANDLE_VALUE)
        return false;

    const DWORD tamano = GetFileSize(archivo, nullptr);
    std::vector<BYTE> datos;
    bool leido = false;
    if (tamano != INVALID_FILE_SIZE && tamano > 0 && tamano <= TAMANO_MAXIMO_FIRMA)
    {
        datos.resize(tamano);
        leido = LeerBytes(archivo, datos.data(), tamano);
    }
    CloseHandle(archivo);
    if (!leido)
        return false;

    // CRYPT_STRING_NOCRLF: todo seguido, sin saltos de línea.
    // Primera llamada: cuántos caracteres hacen falta (incluido el cero
    // final). Segunda: convertir.
    const DWORD formato = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD caracteres = 0;
    if (!CryptBinaryToStringW(datos.data(), tamano, formato, nullptr, &caracteres))
        return false;

    base64.resize(caracteres);
    if (!CryptBinaryToStringW(datos.data(), tamano, formato, &base64[0], &caracteres))
        return false;
    base64.resize(caracteres);      // Ahora 'caracteres' no cuenta el cero final.
    return true;
}

// ============================================================================
//  4. Ventanas
// ============================================================================

// Crea la ventana "dueña" de nuestros diálogos ("Abrir", PIN...).
//
// El problema: este programa no tiene ventana, y quien está delante es el
// navegador. Un diálogo sin dueña puede abrirse DETRÁS del navegador, y el
// usuario no lo vería.
//
// La solución: una ventana propia, sin tamaño (no se ve), marcada como
// "siempre visible" (WS_EX_TOPMOST). Windows mantiene los diálogos por
// encima de su dueña, así que quedan también por encima del navegador.
// La colocamos en el centro de la ventana que el usuario tiene delante (el
// navegador) para que los diálogos salgan en ese mismo monitor.
//
// ¿Y por qué no usar como dueña la ventana del navegador? Porque un diálogo
// modal desactiva a su dueña mientras está abierto. Si el navegador cerrase
// este programa a mitad de un diálogo, su ventana podría quedarse
// desactivada, sin responder al ratón ni al teclado.
//
//   WS_EX_TOOLWINDOW  no aparece en la barra de tareas.
//   "STATIC"          clase de ventana que ya trae Windows; así no hay que
//                     registrar una propia.
static HWND CrearVentanaDuena()
{
    int x = 0;
    int y = 0;
    RECT marco = {};
    const HWND delante = GetForegroundWindow();
    if (delante != nullptr && GetWindowRect(delante, &marco))
    {
        x = (marco.left + marco.right) / 2;
        y = (marco.top + marco.bottom) / 2;
    }

    const HWND ventana = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"Firma con DNIe",
                                         WS_POPUP | WS_VISIBLE, x, y, 0, 0,
                                         nullptr, nullptr, g_hInstance, nullptr);
    if (ventana != nullptr)
        SetForegroundWindow(ventana);      // Que el teclado vaya a nuestros diálogos.
    return ventana;
}

// Windows coloca el diálogo "Abrir" con su esquina superior izquierda en la
// ventana dueña. Como la dueña es un punto en el centro del navegador, el
// diálogo saldría desplazado hacia abajo a la derecha, y con la escala de
// Windows alta incluso fuera de la pantalla. Para centrarlo usamos un
// "gancho" (hook) CBT de este hilo: Windows nos avisa justo antes de activar
// el diálogo, ya con su tamaño, y lo movemos para que su centro coincida con
// la dueña, sin salirse del monitor. Así se conserva el diálogo moderno (un
// gancho del propio GetOpenFileNameW lo cambiaría por el antiguo).
static HHOOK g_ganchoCentrar = nullptr;
static HWND g_duenaCentrar = nullptr;

static LRESULT CALLBACK CentrarAlActivar(int codigo, WPARAM wParam, LPARAM lParam)
{
    if (codigo == HCBT_ACTIVATE)
    {
        const HWND dialogo = (HWND)wParam;
        RECT d = {};
        RECT o = {};
        if (GetWindow(dialogo, GW_OWNER) == g_duenaCentrar && GetWindowRect(dialogo, &d) && GetWindowRect(g_duenaCentrar, &o))
        {
            const int ancho = d.right - d.left;
            const int alto = d.bottom - d.top;
            int x = o.left - ancho / 2;
            int y = o.top - alto / 2;
            MONITORINFO monitor = { sizeof(monitor) };
            if (GetMonitorInfoW(MonitorFromWindow(g_duenaCentrar, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                if (x + ancho > monitor.rcWork.right) x = monitor.rcWork.right - ancho;
                if (y + alto > monitor.rcWork.bottom) y = monitor.rcWork.bottom - alto;
                if (x < monitor.rcWork.left) x = monitor.rcWork.left;
                if (y < monitor.rcWork.top) y = monitor.rcWork.top;
            }
            SetWindowPos(dialogo, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            UnhookWindowsHookEx(g_ganchoCentrar);   // Basta con la primera vez.
            g_ganchoCentrar = nullptr;
            return 0;
        }
    }
    return CallNextHookEx(nullptr, codigo, wParam, lParam);
}

// Muestra el diálogo "Abrir" de Windows y devuelve la ruta elegida, o una
// cadena vacía si el usuario cancela.
static std::wstring SeleccionarArchivo(HWND duena, const wchar_t* titulo, const wchar_t* filtro)
{
    // Búfer amplio: las rutas de Windows pueden superar con creces los 260
    // caracteres clásicos (MAX_PATH).
    std::vector<wchar_t> ruta(4096, L'\0');

    OPENFILENAMEW dialogo = {};
    dialogo.lStructSize = sizeof(dialogo);
    dialogo.hwndOwner = duena;
    dialogo.lpstrFilter = filtro;
    dialogo.lpstrFile = ruta.data();
    dialogo.nMaxFile = (DWORD)ruta.size();
    dialogo.lpstrTitle = titulo;
    //   OFN_EXPLORER        diálogo de estilo moderno (el del Explorador).
    //   OFN_FILEMUSTEXIST / OFN_PATHMUSTEXIST
    //                       no aceptar un nombre o una carpeta que no existan.
    //   OFN_HIDEREADONLY    ocultar la casilla "Abrir como solo lectura".
    //   OFN_NOCHANGEDIR     no cambiar el directorio de trabajo del programa.
    dialogo.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    // GetOpenFileNameW devuelve FALSE si el usuario cancela y también si hay
    // un error; en los dos casos devolvemos cadena vacía.
    g_duenaCentrar = duena;
    g_ganchoCentrar = duena != nullptr ? SetWindowsHookExW(WH_CBT, CentrarAlActivar, nullptr, GetCurrentThreadId()) : nullptr;
    const BOOL elegido = GetOpenFileNameW(&dialogo);
    if (g_ganchoCentrar != nullptr)
    {
        UnhookWindowsHookEx(g_ganchoCentrar);
        g_ganchoCentrar = nullptr;
    }
    if (!elegido)
        return std::wstring();
    return std::wstring(ruta.data());
}

// Filtros del diálogo "Abrir": pares "texto visible\0patrón\0", terminados
// con un \0 extra (el cero final que C++ añade a todo literal de texto).
static const wchar_t FILTRO_TODOS[] = L"Todos los archivos (*.*)\0*.*\0";
static const wchar_t FILTRO_FIRMAS[] = L"Firmas PKCS#7 (*.p7s)\0*.p7s\0Todos los archivos (*.*)\0*.*\0";

// ============================================================================
//  5. Las tres operaciones
// ============================================================================
//  Cada función devuelve el JSON de la respuesta. El trabajo de verdad está
//  en Crypto.cpp; aquí solo se piden los archivos y se monta la respuesta.
// ============================================================================

static std::string OperacionVersion()
{
    return "{\"correcto\":true,\"version\":" + TextoJson(GetInsertedDnieVersion()) + "}";
}

static std::string OperacionFirmar(HWND duena)
{
    const std::wstring archivo = SeleccionarArchivo(duena, L"Seleccione el archivo que quiere firmar", FILTRO_TODOS);
    if (archivo.empty())
        return RespuestaDeCancelacion();

    // La firma se guarda junto al documento: informe.pdf -> informe.pdf.p7s
    const std::wstring archivoFirma = archivo + L".p7s";

    // Huella vacía = buscar el certificado de FIRMA del DNIe (ver Crypto.h).
    std::wstring firmante;
    std::wstring explicacion;
    if (!SignFileDetached(archivo, archivoFirma, L"", duena, firmante, explicacion))
        return RespuestaDeFalloDeFirma(explicacion);

    std::wstring firma;
    if (!LeerArchivoEnBase64(archivoFirma, firma))
        return RespuestaDeError(L"error", L"El archivo se ha firmado, pero no se pudo leer el archivo de firma.");

    return "{\"correcto\":true,\"archivo\":" + TextoJson(NombreDeArchivo(archivo)) +
           ",\"archivoFirma\":" + TextoJson(NombreDeArchivo(archivoFirma)) +
           ",\"firmante\":" + TextoJson(firmante) +
           ",\"firma\":" + TextoJson(firma) + "}";
}

static std::string OperacionVerificar(HWND duena)
{
    const std::wstring archivo = SeleccionarArchivo(duena, L"Seleccione el archivo cuya firma quiere comprobar", FILTRO_TODOS);
    if (archivo.empty())
        return RespuestaDeCancelacion();

    // Lo normal es que la firma esté junto al documento, con el nombre que
    // le da "firmar". Solo si no está ahí se pregunta por ella.
    std::wstring archivoFirma = archivo + L".p7s";
    if (!ExisteArchivo(archivoFirma))
    {
        archivoFirma = SeleccionarArchivo(duena, L"Seleccione el archivo de firma (.p7s)", FILTRO_FIRMAS);
        if (archivoFirma.empty())
            return RespuestaDeCancelacion();
    }

    // true = exigir además que el certificado del firmante sea de confianza
    // para Windows (ver Crypto.h).
    std::wstring firmante;
    std::wstring explicacion;
    if (!VerifyFileDetached(archivo, archivoFirma, true, firmante, explicacion))
        return RespuestaDeError(L"error", explicacion);

    return "{\"correcto\":true,\"archivo\":" + TextoJson(NombreDeArchivo(archivo)) +
           ",\"archivoFirma\":" + TextoJson(NombreDeArchivo(archivoFirma)) +
           ",\"firmante\":" + TextoJson(firmante) + "}";
}

static std::string AtenderPeticion(const std::string& peticion)
{
    const std::string operacion = LeerOperacion(peticion);

    if (operacion == "version")
        return OperacionVersion();

    if (operacion == "firmar" || operacion == "verificar")
    {
        const HWND duena = CrearVentanaDuena();
        const std::string respuesta = operacion == "firmar" ? OperacionFirmar(duena) : OperacionVerificar(duena);
        if (duena != nullptr)
            DestroyWindow(duena);
        return respuesta;
    }

    return RespuestaDeError(L"error", L"Operación desconocida.");
}

// ============================================================================
//  6. Punto de entrada
// ============================================================================
//  wWinMain en vez de main: el programa es "de ventanas", no de consola. Así
//  no aparece una ventana negra de consola cada vez que el navegador lo
//  arranca. La entrada y la salida estándar funcionan igual: las prepara el
//  navegador y se obtienen con GetStdHandle.
// ============================================================================
int WINAPI wWinMain(HINSTANCE instancia, HINSTANCE, PWSTR, int)
{
    g_hInstance = instancia;

    const HANDLE entrada = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE salida = GetStdHandle(STD_OUTPUT_HANDLE);

    // Si alguien abre el .exe con doble clic no hay entrada ni salida
    // estándar: no hay con quién hablar. Lo explicamos y terminamos.
    if (entrada == nullptr || entrada == INVALID_HANDLE_VALUE ||
        salida == nullptr || salida == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(nullptr,
                    L"Este programa no se usa directamente: lo arranca el navegador "
                    L"cuando una página web pide firmar con el DNIe.\n\n"
                    L"Para instalarlo, ejecute scripts\\registrar.cmd.",
                    L"Firma con DNIe", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Atendemos peticiones hasta que el navegador cierre la conexión. (La
    // extensión de este ejemplo abre una conexión por petición, así que lo
    // habitual es dar una sola vuelta.)
    std::string peticion;
    while (LeerMensaje(entrada, peticion))
    {
        if (!EscribirMensaje(salida, AtenderPeticion(peticion)))
            break;
    }
    return 0;
}
