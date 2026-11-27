// ============================================================================
//  Signer.cpp  -  Implementación de la clase COM "DNIeSigner"
// ============================================================================
//  Este archivo es solo la "fachada" COM: recibe las llamadas del script,
//  convierte los tipos (BSTR <-> std::wstring, VARIANT_BOOL <-> bool) y delega
//  el trabajo en Crypto.cpp. Explicación general de COM: ver Signer.h.
// ============================================================================
#include "Signer.h"
#include "Crypto.h"
#include "Module.h"

#include <commdlg.h>     // GetOpenFileNameW (diálogo "Abrir")

// ----------------------------------------------------------------------------
//  Conversión de tipos entre el mundo COM/script y C++
// ----------------------------------------------------------------------------
//  BSTR es la cadena de texto de COM (la que usan JavaScript y VBScript).
//  Dos particularidades: un BSTR nulo equivale a cadena vacía, y quien
//  recibe un BSTR de salida es responsable de liberarlo (lo hace el script).
//
//  VARIANT_BOOL es el booleano de COM: VARIANT_TRUE vale -1 (no 1) y
//  VARIANT_FALSE vale 0. Por eso nunca se asigna un 'bool' directamente.
// ----------------------------------------------------------------------------
static std::wstring FromBstr(BSTR text)
{
    return text != nullptr ? std::wstring(text, SysStringLen(text)) : std::wstring();
}

static HRESULT ToBstr(const std::wstring& text, BSTR* result)
{
    if (result == nullptr)
        return E_POINTER;
    *result = SysAllocStringLen(text.data(), (UINT)text.size());
    return *result != nullptr ? S_OK : E_OUTOFMEMORY;
}

static VARIANT_BOOL ToVariantBool(bool value)
{
    return value ? VARIANT_TRUE : VARIANT_FALSE;
}

// Ventana sobre la que mostrar nuestros diálogos (PIN, "Abrir"...): la
// ventana activa de este hilo o, si no hay, la que el usuario tiene delante.
static HWND GetOwnerWindow()
{
    HWND window = GetActiveWindow();
    return window != nullptr ? window : GetForegroundWindow();
}

// ============================================================================
//  Construcción y destrucción
// ============================================================================
DNIeSigner::DNIeSigner()
    : referenceCount_(1),      // Quien nos crea ya tiene una referencia.
      typeInfo_(nullptr),
      checkTrust_(true)
{
    // Un objeto vivo más: la DLL no debe descargarse mientras exista.
    InterlockedIncrement(&g_lockCount);
}

DNIeSigner::~DNIeSigner()
{
    if (typeInfo_ != nullptr)
        typeInfo_->Release();
    InterlockedDecrement(&g_lockCount);
}

// ============================================================================
//  IUnknown
// ============================================================================

// "¿Tienes la interfaz 'iid'?" Si la tenemos, devolvemos un puntero a ella
// (y contamos una referencia más); si no, E_NOINTERFACE.
STDMETHODIMP DNIeSigner::QueryInterface(REFIID iid, void** object)
{
    if (object == nullptr)
        return E_POINTER;

    // Como heredamos de dos interfaces, hay que indicar por cuál "rama" se
    // convierte el puntero; de ahí los static_cast explícitos.
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IDNIeSigner)
        *object = static_cast<IDNIeSigner*>(this);
    else if (iid == IID_IObjectSafety)
        *object = static_cast<IObjectSafety*>(this);
    else
    {
        *object = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) DNIeSigner::AddRef()
{
    return InterlockedIncrement(&referenceCount_);
}

STDMETHODIMP_(ULONG) DNIeSigner::Release()
{
    const LONG count = InterlockedDecrement(&referenceCount_);
    if (count == 0)
        delete this;      // Ya no nos usa nadie.
    return count;
}

// ============================================================================
//  IDispatch  -  llamadas por nombre desde JavaScript / VBScript
// ============================================================================
//  No implementamos a mano la traducción "nombre -> método". La biblioteca
//  de tipos incrustada en la DLL ya describe la interfaz, y el objeto
//  ITypeInfo que se obtiene de ella sabe hacer todo el trabajo:
//    - GetIDsOfNames: buscar el número (DISPID) de un nombre.
//    - Invoke: desempaquetar los argumentos y llamar al método C++ real.
//  Por eso añadir un método nuevo no obliga a tocar esta sección.
// ============================================================================
HRESULT DNIeSigner::EnsureTypeInfo()
{
    if (typeInfo_ != nullptr)
        return S_OK;

    // La biblioteca de tipos está dentro de nuestra propia DLL (ver el .rc),
    // así que la cargamos desde la ruta de la DLL.
    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInstance, modulePath, ARRAYSIZE(modulePath)) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    // REGKIND_NONE: solo cargarla, sin registrarla en el sistema. No hace
    // falta registrarla porque el objeto se usa siempre dentro del mismo
    // proceso y del mismo hilo. Si algún día hubiera que usarlo desde otro
    // hilo o proceso, sí habría que registrarla, para que COM supiera
    // transportar las llamadas de un lado a otro.
    ITypeLib* typeLibrary = nullptr;
    HRESULT hr = LoadTypeLibEx(modulePath, REGKIND_NONE, &typeLibrary);
    if (FAILED(hr))
        return hr;

    hr = typeLibrary->GetTypeInfoOfGuid(IID_IDNIeSigner, &typeInfo_);
    typeLibrary->Release();
    return hr;
}

STDMETHODIMP DNIeSigner::GetTypeInfoCount(UINT* count)
{
    if (count == nullptr)
        return E_POINTER;
    *count = 1;      // Sí, ofrecemos información de tipos.
    return S_OK;
}

STDMETHODIMP DNIeSigner::GetTypeInfo(UINT index, LCID, ITypeInfo** typeInfo)
{
    if (typeInfo == nullptr)
        return E_POINTER;
    *typeInfo = nullptr;
    if (index != 0)
        return DISP_E_BADINDEX;

    const HRESULT hr = EnsureTypeInfo();
    if (FAILED(hr))
        return hr;

    typeInfo_->AddRef();
    *typeInfo = typeInfo_;
    return S_OK;
}

// Los parámetros sin nombre (REFIID, LCID) se ignoran a propósito: el
// primero está reservado y siempre vale IID_NULL; el segundo es el idioma
// del llamante, que no nos afecta porque los nombres de nuestros métodos no
// se traducen. Lo mismo ocurre en Invoke.
STDMETHODIMP DNIeSigner::GetIDsOfNames(REFIID, LPOLESTR* names, UINT nameCount, LCID, DISPID* dispIds)
{
    const HRESULT hr = EnsureTypeInfo();
    if (FAILED(hr))
        return hr;
    return typeInfo_->GetIDsOfNames(names, nameCount, dispIds);
}

STDMETHODIMP DNIeSigner::Invoke(DISPID dispId, REFIID, LCID, WORD flags, DISPPARAMS* parameters,
                                VARIANT* result, EXCEPINFO* exceptionInfo, UINT* argumentError)
{
    const HRESULT hr = EnsureTypeInfo();
    if (FAILED(hr))
        return hr;
    return typeInfo_->Invoke(static_cast<IDNIeSigner*>(this), dispId, flags,
                             parameters, result, exceptionInfo, argumentError);
}

// ============================================================================
//  IDNIeSigner  -  nuestros métodos y propiedades
// ============================================================================
//  Criterio de errores: los fallos "de negocio" (PIN incorrecto, firma no
//  válida, archivo inexistente...) NO se devuelven como error COM, porque en
//  JavaScript se convertirían en una excepción. El método devuelve False y
//  el motivo queda en la propiedad LastError. Así el script queda sencillo:
//
//      if (!firmador.SignFile(ruta, "")) alert(firmador.LastError);
// ============================================================================

STDMETHODIMP DNIeSigner::SelectFile(BSTR title, BSTR* path)
{
    if (path == nullptr)
        return E_POINTER;

    const std::wstring dialogTitle = FromBstr(title);
    // Búfer amplio: las rutas de Windows pueden superar con creces los 260
    // caracteres clásicos (MAX_PATH).
    wchar_t fileName[4096] = L"";

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetOwnerWindow();
    // Filtro: pares "texto visible\0patrón\0", terminados con un \0 extra
    // (que aquí es el cero final que C++ añade a todo literal de texto).
    dialog.lpstrFilter = L"Todos los archivos (*.*)\0*.*\0Firmas PKCS#7 (*.p7s)\0*.p7s\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = ARRAYSIZE(fileName);
    dialog.lpstrTitle = dialogTitle.empty() ? nullptr : dialogTitle.c_str();
    //   OFN_EXPLORER        diálogo de estilo moderno (el del Explorador).
    //   OFN_FILEMUSTEXIST / OFN_PATHMUSTEXIST
    //                       no aceptar un nombre o una carpeta que no existan.
    //   OFN_HIDEREADONLY    ocultar la casilla "Abrir como solo lectura".
    //   OFN_NOCHANGEDIR     no cambiar el directorio de trabajo del navegador.
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    // GetOpenFileNameW devuelve FALSE si el usuario cancela, pero también si
    // hay un error (p. ej. una ruta más larga que el búfer), y entonces
    // fileName puede contener datos que no son una ruta. En ambos casos
    // devolvemos cadena vacía.
    if (!GetOpenFileNameW(&dialog))
        fileName[0] = L'\0';

    return ToBstr(fileName, path);
}

STDMETHODIMP DNIeSigner::SignFile(BSTR inputPath, BSTR signaturePath, VARIANT_BOOL* ok)
{
    if (ok == nullptr)
        return E_POINTER;

    const std::wstring input = FromBstr(inputPath);
    std::wstring signature = FromBstr(signaturePath);
    if (signature.empty())
        signature = input + L".p7s";      // Nombre por defecto de la firma.

    bool success = false;
    if (input.empty())
        lastError_ = L"No se ha indicado el archivo que se quiere firmar.";
    else
        success = SignFileDetached(input, signature, thumbprint_, GetOwnerWindow(), signerName_, lastError_);

    *ok = ToVariantBool(success);
    return S_OK;
}

STDMETHODIMP DNIeSigner::VerifyFile(BSTR inputPath, BSTR signaturePath, VARIANT_BOOL* valid)
{
    if (valid == nullptr)
        return E_POINTER;

    const std::wstring input = FromBstr(inputPath);
    std::wstring signature = FromBstr(signaturePath);
    if (signature.empty())
        signature = input + L".p7s";

    bool success = false;
    if (input.empty())
        lastError_ = L"No se ha indicado el archivo cuya firma se quiere verificar.";
    else
        success = VerifyFileDetached(input, signature, checkTrust_, signerName_, lastError_);

    *valid = ToVariantBool(success);
    return S_OK;
}

STDMETHODIMP DNIeSigner::get_LastError(BSTR* message)
{
    return ToBstr(lastError_, message);
}

STDMETHODIMP DNIeSigner::get_SignerName(BSTR* name)
{
    return ToBstr(signerName_, name);
}

STDMETHODIMP DNIeSigner::get_CertificateThumbprint(BSTR* thumbprint)
{
    return ToBstr(thumbprint_, thumbprint);
}

STDMETHODIMP DNIeSigner::put_CertificateThumbprint(BSTR thumbprint)
{
    thumbprint_ = FromBstr(thumbprint);
    return S_OK;
}

STDMETHODIMP DNIeSigner::get_CheckCertificateTrust(VARIANT_BOOL* check)
{
    if (check == nullptr)
        return E_POINTER;
    *check = ToVariantBool(checkTrust_);
    return S_OK;
}

STDMETHODIMP DNIeSigner::put_CheckCertificateTrust(VARIANT_BOOL check)
{
    checkTrust_ = (check != VARIANT_FALSE);
    return S_OK;
}

// No guardamos la versión: se pregunta al lector cada vez, porque el usuario
// puede insertar, retirar o cambiar la tarjeta en cualquier momento.
STDMETHODIMP DNIeSigner::get_DnieVersion(BSTR* version)
{
    return ToBstr(GetInsertedDnieVersion(), version);
}

// ============================================================================
//  IObjectSafety  -  "soy seguro para scripting"
// ============================================================================
//  Antes de dejar que una página use un ActiveX, Internet Explorer le
//  pregunta dos cosas:
//    - INTERFACESAFE_FOR_UNTRUSTED_CALLER: ¿es seguro que te llame un script
//      cualquiera?
//    - INTERFACESAFE_FOR_UNTRUSTED_DATA:   ¿es seguro inicializarte con datos
//      cualesquiera?
//  Respondemos que sí a ambas. (Lo mismo se declara en el registro de Windows
//  con las "categorías" que escribe DllMain.cpp; se hacen las dos cosas por
//  compatibilidad.)
//
//  ¡OJO, RESPONSABILIDAD! Decir "soy seguro" es una promesa del programador,
//  no algo que Windows compruebe. Este control lee archivos y escribe firmas
//  en las rutas que le pasa la página, así que solo debe instalarse en
//  equipos donde las páginas que lo usan sean de confianza. Para un uso real
//  conviene restringirlo, por ejemplo comprobando la URL de la página que lo
//  aloja ("site lock") antes de aceptar llamadas.
// ============================================================================
static const DWORD SAFETY_OPTIONS = INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA;

STDMETHODIMP DNIeSigner::GetInterfaceSafetyOptions(REFIID, DWORD* supported, DWORD* enabled)
{
    if (supported == nullptr || enabled == nullptr)
        return E_POINTER;
    *supported = SAFETY_OPTIONS;
    *enabled = SAFETY_OPTIONS;
    return S_OK;
}

STDMETHODIMP DNIeSigner::SetInterfaceSafetyOptions(REFIID, DWORD mask, DWORD)
{
    // El navegador nos pide activar ciertas opciones de seguridad. Aceptamos
    // si son de las que soportamos (ya están siempre activas).
    return (mask & ~SAFETY_OPTIONS) == 0 ? S_OK : E_FAIL;
}
