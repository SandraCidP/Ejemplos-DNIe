// ============================================================================
//  DllMain.cpp  -  Punto de entrada de la DLL, fábrica de objetos y registro
// ============================================================================
//
//  QUÉ PASA CUANDO LA PÁGINA HACE  new ActiveXObject("DNIeActiveX.Signer")
//  -----------------------------------------------------------------------
//    1. Windows busca "DNIeActiveX.Signer" (el ProgID) en el registro y
//       obtiene el CLSID, el identificador único de la clase.
//    2. Con el CLSID busca en el registro la ruta de la DLL y la carga.
//    3. Llama a DllGetClassObject pidiendo la "fábrica de clases".
//    4. Llama a IClassFactory::CreateInstance, que crea el objeto DNIeSigner.
//
//  Para que los pasos 1 y 2 funcionen, la DLL tiene que estar REGISTRADA:
//  eso es lo que hace regsvr32 al llamar a DllRegisterServer / DllInstall,
//  que escriben en el registro las claves que se detallan más abajo.
// ============================================================================
#include <windows.h>
#include <olectl.h>      // SELFREG_E_CLASS
#include <new>           // std::nothrow
#include <string>

#include "Module.h"
#include "Signer.h"

// MIDL genera en este archivo las constantes de los GUID (IID_IDNIeSigner,
// CLSID_DNIeSigner...). Se incluye UNA sola vez en todo el proyecto, aquí.
#include "DNIeActiveX_i.c"

HINSTANCE g_hInstance = nullptr;
LONG g_lockCount = 0;

// Nombres con los que se registra el control.
static const wchar_t* const PROG_ID = L"DNIeActiveX.Signer";              // El que usa la página web.
static const wchar_t* const VERSIONED_PROG_ID = L"DNIeActiveX.Signer.1";  // Lo mismo, con versión.
static const wchar_t* const DESCRIPTION = L"Firmador con DNIe";

// "Categorías de componente" estándar de Microsoft con las que un control
// declara en el registro que es seguro para scripting y para inicialización
// (ver también IObjectSafety en Signer.cpp).
static const wchar_t* const CATEGORY_SAFE_FOR_SCRIPTING = L"{7DD95801-9882-11CF-9FA9-00AA006C42C4}";
static const wchar_t* const CATEGORY_SAFE_FOR_INITIALIZING = L"{7DD95802-9882-11CF-9FA9-00AA006C42C4}";

// ============================================================================
//  DllMain  -  Windows la llama al cargar y descargar la DLL
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    // DLL_PROCESS_ATTACH: la DLL acaba de cargarse en un programa. Es el
    // momento de guardar nuestra "asa". (DllMain también recibe avisos al
    // crearse y destruirse hilos, y al descargarse; no nos interesan.)
    if (reason == DLL_PROCESS_ATTACH)
        g_hInstance = instance;
    return TRUE;
}

// ============================================================================
//  Fábrica de clases
// ============================================================================
//  COM no crea los objetos directamente: pide una "fábrica" (IClassFactory) y
//  le encarga crearlos. Nuestra fábrica es un único objeto global que vive
//  mientras la DLL esté cargada, así que no necesita contar referencias de
//  verdad (AddRef/Release devuelven valores fijos).
// ============================================================================
class ClassFactory : public IClassFactory
{
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IClassFactory)
        {
            *object = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // Crea un objeto DNIeSigner y devuelve la interfaz 'iid' que se pide.
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;

        // 'outer' se usa en la "agregación" de COM (un objeto dentro de
        // otro). No la soportamos; es lo habitual en controles sencillos.
        if (outer != nullptr)
            return CLASS_E_NOAGGREGATION;

        DNIeSigner* signer = new (std::nothrow) DNIeSigner();   // Nace con 1 referencia.
        if (signer == nullptr)
            return E_OUTOFMEMORY;

        // Si QueryInterface va bien, el llamante se queda con su referencia;
        // soltamos la nuestra. Si va mal, este Release destruye el objeto.
        const HRESULT hr = signer->QueryInterface(iid, object);
        signer->Release();
        return hr;
    }

    // El cliente puede pedir que la DLL siga cargada aunque no haya objetos.
    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            InterlockedIncrement(&g_lockCount);
        else
            InterlockedDecrement(&g_lockCount);
        return S_OK;
    }
};

static ClassFactory g_classFactory;

// ============================================================================
//  Funciones exportadas que usa COM (ver DNIeActiveX.def)
// ============================================================================

// "Dame la fábrica de la clase 'clsid'".
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object)
{
    if (clsid != CLSID_DNIeSigner)
        return CLASS_E_CLASSNOTAVAILABLE;   // Esta DLL solo contiene una clase.
    return g_classFactory.QueryInterface(iid, object);
}

// "¿Puedo descargarte ya?" Solo si no quedan objetos vivos ni bloqueos.
STDAPI DllCanUnloadNow()
{
    return g_lockCount == 0 ? S_OK : S_FALSE;
}

// ============================================================================
//  Registro en Windows
// ============================================================================
//  Claves que se escriben (bajo ...\Software\Classes):
//
//    DNIeActiveX.Signer                     ProgID -> descripción
//      \CLSID                               = {CLSID}
//      \CurVer                              = DNIeActiveX.Signer.1
//    DNIeActiveX.Signer.1                   ProgID con versión
//      \CLSID                               = {CLSID}
//    CLSID\{CLSID}                          La clase en sí
//      \InprocServer32                      = ruta de esta DLL
//          ThreadingModel                   = Apartment
//      \ProgID                              = DNIeActiveX.Signer.1
//      \VersionIndependentProgID            = DNIeActiveX.Signer
//      \Implemented Categories\{7DD95801…}  "seguro para scripting"
//      \Implemented Categories\{7DD95802…}  "seguro para inicialización"
//
//  DÓNDE se escriben, según el tipo de registro:
//    - Todo el equipo:      HKEY_LOCAL_MACHINE  (hay que ser administrador)
//    - Solo usuario actual: HKEY_CURRENT_USER   (no hace falta serlo)
//
//  OJO con el registro por usuario: los programas que se ejecutan "como
//  administrador" (elevados) ignoran, por seguridad, el registro COM de
//  HKEY_CURRENT_USER; para ellos solo cuenta el de HKEY_LOCAL_MACHINE. Si el
//  control "no está registrado" solo cuando se abre la página o las pruebas
//  desde una consola de administrador, la causa es esta.
//
//  32 y 64 BITS: un programa de 32 bits solo puede cargar DLL de 32 bits, y
//  uno de 64, de 64. Windows guarda el registro COM de cada "mundo" por
//  separado, así que hay que registrar las DOS versiones de la DLL (cada una
//  con su regsvr32). Las pestañas de Internet Explorer suelen ser de 32 bits
//  aunque Windows sea de 64.
//
//  "ThreadingModel = Apartment" indica que cada objeto se usa siempre desde
//  el hilo que lo creó (como hacen los navegadores y los scripts); así no
//  necesitamos proteger el objeto frente a accesos simultáneos. Las variables
//  globales (g_lockCount) sí las comparten objetos de hilos distintos; por
//  eso se modifican con InterlockedIncrement / InterlockedDecrement, que son
//  sumas y restas seguras entre hilos.
// ============================================================================

static std::wstring ClsidAsText()
{
    wchar_t text[64] = {};
    StringFromGUID2(CLSID_DNIeSigner, text, ARRAYSIZE(text));   // "{9742DA5B-...}"
    return text;
}

// Crea la clave (si no existe) y escribe en ella un valor de texto.
// 'valueName' nulo = valor "(Predeterminado)" de la clave.
static bool WriteRegistryText(HKEY root, const std::wstring& keyPath,
                              const wchar_t* valueName, const std::wstring& text)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;

    const DWORD byteCount = (DWORD)((text.size() + 1) * sizeof(wchar_t));   // Incluye el \0 final.
    const LSTATUS status = RegSetValueExW(key, valueName, 0, REG_SZ, (const BYTE*)text.c_str(), byteCount);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

static HRESULT RegisterControl(bool perUser)
{
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInstance, modulePath, ARRAYSIZE(modulePath)) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring clsid = ClsidAsText();
    const std::wstring clsidKey = classes + L"CLSID\\" + clsid;

    const bool ok =
        // ProgID independiente de versión.
        WriteRegistryText(root, classes + PROG_ID, nullptr, DESCRIPTION) &&
        WriteRegistryText(root, classes + PROG_ID + L"\\CLSID", nullptr, clsid) &&
        WriteRegistryText(root, classes + PROG_ID + L"\\CurVer", nullptr, VERSIONED_PROG_ID) &&
        // ProgID con versión.
        WriteRegistryText(root, classes + VERSIONED_PROG_ID, nullptr, DESCRIPTION) &&
        WriteRegistryText(root, classes + VERSIONED_PROG_ID + L"\\CLSID", nullptr, clsid) &&
        // La clase.
        WriteRegistryText(root, clsidKey, nullptr, DESCRIPTION) &&
        WriteRegistryText(root, clsidKey + L"\\InprocServer32", nullptr, modulePath) &&
        WriteRegistryText(root, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment") &&
        WriteRegistryText(root, clsidKey + L"\\ProgID", nullptr, VERSIONED_PROG_ID) &&
        WriteRegistryText(root, clsidKey + L"\\VersionIndependentProgID", nullptr, PROG_ID) &&
        // Categorías "seguro para scripting / inicialización".
        WriteRegistryText(root, clsidKey + L"\\Implemented Categories\\" + CATEGORY_SAFE_FOR_SCRIPTING, nullptr, L"") &&
        WriteRegistryText(root, clsidKey + L"\\Implemented Categories\\" + CATEGORY_SAFE_FOR_INITIALIZING, nullptr, L"");

    // SELFREG_E_CLASS es el error estándar de "no pude registrar la clase";
    // regsvr32 lo muestra al usuario. La causa típica es no ser administrador
    // al registrar para todo el equipo.
    return ok ? S_OK : SELFREG_E_CLASS;
}

static HRESULT UnregisterControl(bool perUser)
{
    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring classes = L"Software\\Classes\\";

    // RegDeleteTreeW borra la clave con todo su contenido. Si no existía
    // (ERROR_FILE_NOT_FOUND) lo damos por bueno: ya está "desregistrado".
    const std::wstring keys[] = {
        classes + L"CLSID\\" + ClsidAsText(),
        classes + PROG_ID,
        classes + VERSIONED_PROG_ID,
    };

    bool ok = true;
    for (const std::wstring& key : keys)
    {
        const LSTATUS status = RegDeleteTreeW(root, key.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
            ok = false;
    }
    return ok ? S_OK : SELFREG_E_CLASS;
}

// regsvr32 DNIeActiveX.dll        -> registro para todo el equipo.
STDAPI DllRegisterServer()
{
    return RegisterControl(false);
}

// regsvr32 /u DNIeActiveX.dll
STDAPI DllUnregisterServer()
{
    return UnregisterControl(false);
}

// regsvr32 /n /i:user DNIeActiveX.dll       -> registro solo para el usuario.
// regsvr32 /u /n /i:user DNIeActiveX.dll    -> quitar ese registro.
//
// "/i:texto" hace que regsvr32 llame a DllInstall pasándole ese texto, y "/n"
// que NO llame además a DllRegisterServer. Es la convención habitual para
// ofrecer un registro "por usuario" que no requiere ser administrador.
STDAPI DllInstall(BOOL install, PCWSTR commandLine)
{
    const bool perUser = (commandLine != nullptr && _wcsicmp(commandLine, L"user") == 0);
    return install ? RegisterControl(perUser) : UnregisterControl(perUser);
}
