// ============================================================================
//  Crypto.cpp  -  Firma y verificación de archivos con el DNIe
// ============================================================================
//
//  CÓMO ENCAJAN LAS PIEZAS (léase antes que el código)
//  ---------------------------------------------------
//
//    Este archivo                    Windows                      DNIe
//   ┌──────────────┐   ┌───────────────────────────────┐   ┌──────────────┐
//   │ CryptMsg...  │──▶│ CNG (NCrypt)                  │──▶│ Card Module  │
//   │ Cert...      │   │ "Smart Card Key Storage Prov."│   │ (minidriver) │──▶ chip
//   └──────────────┘   └───────────────────────────────┘   └──────────────┘
//
//  0. El "Card Module" (también llamado minidriver o, coloquialmente,
//     middleware del DNIe) es una DLL que se instala aparte y que enseña a
//     Windows a hablar con el chip del DNIe. Sin él, Windows ve la tarjeta
//     pero no sabe usarla.
//
//  1. Al insertar el DNIe, Windows copia sus CERTIFICADOS (la parte pública)
//     al almacén personal del usuario, llamado "MY", y ahí se quedan aunque
//     después se retire la tarjeta. La CLAVE PRIVADA nunca sale del chip:
//     junto a cada certificado, Windows guarda una "nota" que dice en qué
//     proveedor y tarjeta está su clave.
//
//  2. Para firmar pedimos a Windows la clave privada de un certificado
//     (CryptAcquireCertificatePrivateKey). Windows lee esa nota y nos da un
//     "asa" (handle) de CNG. Cuando firmamos con ella, CNG pasa la petición
//     al Card Module del DNIe, y es el chip quien hace la operación.
//
//  3. Nosotros nunca hablamos directamente con la tarjeta. Por eso este mismo
//     código sirve para cualquier tarjeta que tenga minidriver, y también
//     para certificados software: indicando su huella se puede firmar con
//     uno de ellos, y así es como las pruebas automáticas funcionan sin DNIe.
//     (La única consulta al lector es de lectura y no pasa por la tarjeta:
//     ver "Versión del DNIe", más abajo.)
//
//  FORMATO DE LA FIRMA
//  -------------------
//  PKCS#7 / CMS "separada" (detached), con hash SHA-256. El archivo .p7s
//  contiene la firma, la hora de firma y los certificados del firmante y de
//  las autoridades intermedias que lo emitieron (no la raíz), pero NO el
//  documento. Para verificar hacen falta los dos: documento + .p7s.
//
//  APIS DE WINDOWS UTILIZADAS
//  --------------------------
//    Cert...      Almacenes de certificados y cadenas de confianza.
//    CryptMsg...  Construir / leer mensajes PKCS#7 ("API de mensajes de bajo
//                 nivel"). La usamos en vez de CryptSignMessage porque
//                 permite procesar el archivo a trozos: se pueden firmar
//                 archivos enormes sin cargarlos enteros en memoria.
//    NCrypt...    CNG: acceso a la clave privada (aquí, para darle el PIN).
// ============================================================================
#include "Crypto.h"
#include "PinDialog.h"

#include <wincrypt.h>
#include <ncrypt.h>
#include <cryptuiapi.h>
#include <winscard.h>     // Tarjetas inteligentes: códigos de error SCARD_... y lectura del ATR.

#include <cstring>        // memcmp
#include <cwchar>         // wcslen
#include <functional>
#include <vector>

// Tipo de codificación que piden casi todas las funciones Cert/CryptMsg:
// certificados X.509 y mensajes PKCS#7, ambos codificados en ASN.1 (DER).
static const DWORD ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

// Tamaño máximo razonable de un archivo de firma. Una firma con su cadena de
// certificados ocupa unos pocos KB; este límite evita cargar en memoria por
// error un archivo enorme que no es una firma.
static const DWORD MAX_SIGNATURE_FILE_SIZE = 16 * 1024 * 1024;

// ============================================================================
//  Utilidades generales
// ============================================================================

// Ejecuta una acción al salir del bloque donde se declara, pase lo que pase
// (return normal, return por error...). Lo usamos para liberar los recursos
// de Windows sin tener que repetir la limpieza en cada "return false".
//
//     HANDLE f = CreateFileW(...);
//     ScopeExit closeFile([&] { CloseHandle(f); });
//     ...                      // cualquier return posterior cierra el archivo
class ScopeExit
{
public:
    explicit ScopeExit(std::function<void()> action) : action_(std::move(action)) {}
    ~ScopeExit() { action_(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    std::function<void()> action_;
};

// Borra de forma segura el contenido de una cadena (para el PIN). Un simple
// clear() no sobrescribe la memoria; SecureZeroMemory sí, y el compilador no
// puede eliminar la llamada al optimizar.
static void WipeString(std::wstring& text)
{
    if (!text.empty())
        SecureZeroMemory(&text[0], text.size() * sizeof(wchar_t));
    text.clear();
}

static std::wstring ToUpper(std::wstring text)
{
    if (!text.empty())
        CharUpperBuffW(&text[0], (DWORD)text.size());
    return text;
}

// Convierte un código de error de Windows en un texto para el usuario:
//   "<contexto>: <explicación> (0x8009000F)"
// Para los errores más habituales con tarjetas damos una explicación propia,
// más clara que la del sistema.
static std::wstring DescribeError(const std::wstring& context, DWORD code)
{
    std::wstring detail;

    switch (code)
    {
    case SCARD_W_WRONG_CHV:
        detail = L"El PIN introducido no es correcto. Atención: varios intentos fallidos seguidos bloquean el DNIe.";
        break;
    case SCARD_W_CHV_BLOCKED:
        detail = L"El PIN del DNIe está bloqueado. Debe desbloquearlo en un Punto de Actualización del DNIe.";
        break;
    case SCARD_W_CANCELLED_BY_USER:
    case ERROR_CANCELLED:
        detail = L"Operación cancelada por el usuario.";
        break;
    case SCARD_E_NO_SMARTCARD:
    case SCARD_W_REMOVED_CARD:
    case SCARD_E_NO_READERS_AVAILABLE:
        detail = L"No se detecta el DNIe. Compruebe que está insertado en el lector.";
        break;
    default:
    {
        // Texto estándar de Windows para ese código de error.
        //   FROM_SYSTEM      busca el texto en la tabla de mensajes de Windows.
        //   ALLOCATE_BUFFER  Windows reserva la memoria del texto y nos deja
        //                    su dirección en 'buffer' (de ahí el extraño
        //                    molde "(LPWSTR)&buffer"); nos toca liberarla
        //                    después con LocalFree.
        //   IGNORE_INSERTS   no intentar rellenar huecos como "%1" del texto.
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, (LPWSTR)&buffer, 0, nullptr);
        if (length != 0 && buffer != nullptr)
        {
            detail.assign(buffer, length);
            while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n' || detail.back() == L' '))
                detail.pop_back();
        }
        if (buffer != nullptr)
            LocalFree(buffer);
        if (detail.empty())
            detail = L"Error desconocido.";
        break;
    }
    }

    wchar_t codeText[32];
    swprintf_s(codeText, L" (0x%08X)", code);
    return context + L": " + detail + codeText;
}

// Nombre "legible" del titular de un certificado (normalmente su CN; en el
// DNIe, "APELLIDOS, NOMBRE (FIRMA)").
static std::wstring GetCertificateName(PCCERT_CONTEXT certificate)
{
    wchar_t name[512] = {};
    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, ARRAYSIZE(name));
    return name;
}

// Lee un archivo pequeño entero en memoria (se usa para el archivo de firma).
static bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& data, std::wstring& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = DescribeError(L"No se pudo abrir el archivo de firma \"" + path + L"\"", GetLastError());
        return false;
    }
    ScopeExit closeFile([&] { CloseHandle(file); });

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart == 0 || size.QuadPart > MAX_SIGNATURE_FILE_SIZE)
    {
        error = L"El archivo \"" + path + L"\" está vacío o es demasiado grande para ser una firma.";
        return false;
    }

    data.resize((size_t)size.QuadPart);
    DWORD bytesRead = 0;
    if (!ReadFile(file, data.data(), (DWORD)data.size(), &bytesRead, nullptr) || bytesRead != data.size())
    {
        error = DescribeError(L"No se pudo leer el archivo de firma", GetLastError());
        return false;
    }
    return true;
}

// Lee el documento a trozos y se lo va entregando al mensaje PKCS#7, que
// calcula su hash sobre la marcha. Sirve igual para firmar que para
// verificar: en ambos casos lo que se necesita es el hash del documento.
//
// El último trozo se marca como "final". Al firmar, es justo en ese momento
// cuando CryptMsgUpdate pide la firma a la tarjeta; por eso los errores de
// tarjeta (retirada, PIN rechazado, bloqueada...) pueden aparecer aquí.
static bool FeedFileToMessage(HCRYPTMSG message, const std::wstring& path,
                              const std::wstring& failureContext, std::wstring& error)
{
    // FILE_SHARE_READ: otros programas pueden leer el archivo mientras tanto.
    // FILE_FLAG_SEQUENTIAL_SCAN: avisamos a Windows de que lo leeremos de
    // principio a fin, para que optimice su caché.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = DescribeError(L"No se pudo abrir el archivo \"" + path + L"\"", GetLastError());
        return false;
    }
    ScopeExit closeFile([&] { CloseHandle(file); });

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size))
    {
        error = DescribeError(L"No se pudo obtener el tamaño del archivo", GetLastError());
        return false;
    }

    std::vector<BYTE> chunk(64 * 1024);
    ULONGLONG remaining = (ULONGLONG)size.QuadPart;

    // do/while para que un archivo vacío también genere la llamada "final".
    do
    {
        const DWORD wanted = (DWORD)(remaining < chunk.size() ? remaining : chunk.size());
        DWORD bytesRead = 0;
        if (!ReadFile(file, chunk.data(), wanted, &bytesRead, nullptr) || bytesRead != wanted)
        {
            error = DescribeError(L"No se pudo leer el archivo \"" + path + L"\"", GetLastError());
            return false;
        }
        remaining -= bytesRead;

        const BOOL isFinalChunk = (remaining == 0);
        if (!CryptMsgUpdate(message, chunk.data(), bytesRead, isFinalChunk))
        {
            error = DescribeError(failureContext, GetLastError());
            return false;
        }
    } while (remaining > 0);

    return true;
}

// ============================================================================
//  Selección del certificado de firma
// ============================================================================

// ¿Tiene este certificado una clave privada asociada? Los certificados de
// otras personas o de autoridades también pueden estar en un almacén, pero
// sin clave privada no se puede firmar con ellos.
//
// Lo que miramos es la "nota" de la que habla la cabecera: la propiedad
// CERT_KEY_PROV_INFO_PROP_ID, que Windows guarda en el almacén junto al
// certificado (no forma parte de él). Solo comprobamos que la nota existe,
// no que la clave esté accesible ahora mismo: la tarjeta puede no estar
// puesta. (Llamar con búfer nulo es la forma de preguntar "¿existe?".)
static bool HasPrivateKey(PCCERT_CONTEXT certificate)
{
    DWORD size = 0;
    return CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) != FALSE;
}

// Bits de "uso de la clave" del certificado (para qué está autorizado).
// Devuelve false si el certificado no declara ningún uso.
static bool GetKeyUsage(PCCERT_CONTEXT certificate, BYTE& usage)
{
    usage = 0;
    return CertGetIntendedKeyUsage(ENCODING, certificate->pCertInfo, &usage, 1) && usage != 0;
}

// ¿Es el certificado de FIRMA de un DNIe?
//
// El DNIe contiene DOS certificados del ciudadano:
//   - AUTENTICACIÓN: para identificarse en webs.  Uso: "firma digital".
//   - FIRMA:         para firmar documentos.      Uso: "no repudio".
// Para firmar documentos con validez legal hay que usar el de FIRMA. Lo
// reconocemos por dos datos: lo emite una autoridad del DNIe (su emisor
// contiene "DNIE", p. ej. "CN=AC DNIE 004, OU=DNIE, O=DIRECCION GENERAL DE
// LA POLICIA, C=ES") y su uso de clave incluye "no repudio".
static bool IsDnieSigningCertificate(PCCERT_CONTEXT certificate)
{
    wchar_t issuer[512] = {};
    CertNameToStrW(ENCODING, &certificate->pCertInfo->Issuer, CERT_X500_NAME_STR, issuer, ARRAYSIZE(issuer));
    if (ToUpper(issuer).find(L"DNIE") == std::wstring::npos)
        return false;

    BYTE usage = 0;
    return GetKeyUsage(certificate, usage) && (usage & CERT_NON_REPUDIATION_KEY_USAGE) != 0;
}

// ¿Sirve este certificado para firmar? Debe tener clave privada y, si
// declara usos, incluir alguno de firma.
//
// A propósito NO se mira si está caducado: un DNIe con los certificados
// caducados sigue siendo un DNIe insertado, y el chip firma igualmente.
// Descartarlo aquí haría que el control dijera "No se detecta el DNIe" con la
// tarjeta puesta. La caducidad se comprueba donde corresponde, al verificar
// la firma (IsCertificateTrusted), y el diálogo de selección muestra la fecha.
static bool IsUsableForSigning(PCCERT_CONTEXT certificate)
{
    if (!HasPrivateKey(certificate))
        return false;

    BYTE usage = 0;
    if (GetKeyUsage(certificate, usage) &&
        (usage & (CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_NON_REPUDIATION_KEY_USAGE)) == 0)
        return false;

    return true;
}

// ¿Está accesible AHORA MISMO la clave privada de este certificado? Para un
// certificado de tarjeta equivale a preguntar "¿está su tarjeta en el lector?".
//
// Hace falta porque Windows no borra del almacén los certificados de una
// tarjeta al retirarla: si se saca un DNIe y se mete otro, en el almacén
// quedan los certificados de los dos y hay que saber cuál es el que está
// puesto.
//
// Pedimos la clave como al firmar, pero con dos indicadores más:
//   CRYPT_ACQUIRE_SILENT_FLAG       sin diálogos: si la tarjeta no está, la
//                                   llamada falla en vez de pedir que se
//                                   inserte.
//   CRYPT_ACQUIRE_COMPARE_KEY_FLAG  comprobar que la clave encontrada es de
//                                   verdad la de ESTE certificado (compara las
//                                   claves públicas). Sin esto, otra tarjeta
//                                   que guarde su clave con el mismo nombre
//                                   podría pasar por buena.
// Nada de esto necesita el PIN: solo se lee la parte pública.
static bool IsPrivateKeyAvailableNow(PCCERT_CONTEXT certificate)
{
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD keySpec = 0;
    BOOL mustFreeKey = FALSE;
    if (!CryptAcquireCertificatePrivateKey(
            certificate,
            CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG,
            nullptr, &key, &keySpec, &mustFreeKey))
        return false;

    if (mustFreeKey)
    {
        if (keySpec == CERT_NCRYPT_KEY_SPEC)
            NCryptFreeObject(key);
        else
            CryptReleaseContext(key, 0);
    }
    return true;
}

// Convierte una huella en hexadecimal ("A1 B2 C3..." o "a1b2c3...") en sus
// 20 bytes. Ignora espacios y otros separadores.
static bool ParseThumbprint(const std::wstring& text, std::vector<BYTE>& bytes)
{
    std::vector<BYTE> digits;
    for (wchar_t c : text)
    {
        if (c >= L'0' && c <= L'9')      digits.push_back((BYTE)(c - L'0'));
        else if (c >= L'a' && c <= L'f') digits.push_back((BYTE)(c - L'a' + 10));
        else if (c >= L'A' && c <= L'F') digits.push_back((BYTE)(c - L'A' + 10));
    }
    if (digits.size() != 40)   // SHA-1 = 20 bytes = 40 dígitos hexadecimales.
        return false;

    bytes.clear();
    for (size_t i = 0; i < digits.size(); i += 2)
        bytes.push_back((BYTE)((digits[i] << 4) | digits[i + 1]));
    return true;
}

// Decide con qué certificado se firma. Devuelve un certificado que el
// llamante debe liberar con CertFreeCertificateContext, o nullptr (y 'error').
//
// Orden de decisión:
//   1. Si la aplicación indicó una huella, se usa ese certificado y ninguno
//      más. Solo se exige que tenga clave privada: no se comprueba su
//      vigencia ni su uso de clave; eso queda a cargo de la aplicación.
//   2. Si en el almacén hay exactamente un certificado de FIRMA de DNIe
//      (vigente o caducado, de cualquier versión del DNIe), se usa sin
//      preguntar.
//      OJO: que esté en el almacén no garantiza que la tarjeta esté puesta
//      ahora (Windows no borra los certificados al retirarla). Si no lo
//      está, Windows pedirá que se inserte al solicitar la clave (paso 3 de
//      SignFileDetached), o la operación fallará ahí o al firmar.
//   3. Si no hay ninguno (este usuario nunca ha insertado un DNIe en este
//      equipo, o falta el Card Module), es un error: este control firma con
//      DNIe, y no ofrece por su cuenta otros certificados del equipo.
//   4. Si hay varios (varios DNIe usados en este equipo: familiares, un DNI
//      renovado...), se usa el que esté en el lector en este momento. Solo si
//      eso no lo aclara (ninguno puesto, o varios a la vez en distintos
//      lectores) se muestra el diálogo estándar de Windows para elegir.
static PCCERT_CONTEXT ChooseSigningCertificate(HCERTSTORE store, const std::wstring& thumbprint,
                                               HWND ownerWindow, std::wstring& error)
{
    // --- 1. Certificado indicado por su huella -----------------------------
    if (!thumbprint.empty())
    {
        std::vector<BYTE> hash;
        if (!ParseThumbprint(thumbprint, hash))
        {
            error = L"La huella del certificado no es válida: deben ser 40 dígitos hexadecimales (SHA-1).";
            return nullptr;
        }
        CRYPT_HASH_BLOB hashBlob = { (DWORD)hash.size(), hash.data() };
        PCCERT_CONTEXT certificate = CertFindCertificateInStore(store, ENCODING, 0, CERT_FIND_SHA1_HASH, &hashBlob, nullptr);
        if (certificate == nullptr)
        {
            error = L"No se encontró ningún certificado con la huella indicada en el almacén personal.";
            return nullptr;
        }
        if (!HasPrivateKey(certificate))
        {
            CertFreeCertificateContext(certificate);
            error = L"El certificado indicado no tiene clave privada asociada; no se puede firmar con él.";
            return nullptr;
        }
        return certificate;
    }

    // --- 2, 3 y 4. Recorremos el almacén -----------------------------------
    // Vamos copiando los certificados de FIRMA de DNIe a un almacén temporal
    // en memoria, que es lo que necesita el diálogo de selección de Windows
    // si al final hubiera más de uno.
    HCERTSTORE candidates = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (candidates == nullptr)
    {
        error = DescribeError(L"No se pudo crear el almacén temporal de certificados", GetLastError());
        return nullptr;
    }
    ScopeExit closeCandidates([&] { CertCloseStore(candidates, 0); });

    PCCERT_CONTEXT dnieCertificate = nullptr;   // Último certificado de FIRMA de DNIe visto.
    int dnieCount = 0;

    // CertEnumCertificatesInStore libera el certificado anterior en cada
    // vuelta; si queremos conservar uno hay que duplicarlo.
    PCCERT_CONTEXT current = nullptr;
    while ((current = CertEnumCertificatesInStore(store, current)) != nullptr)
    {
        if (!IsUsableForSigning(current) || !IsDnieSigningCertificate(current))
            continue;

        CertAddCertificateContextToStore(candidates, current, CERT_STORE_ADD_ALWAYS, nullptr);

        if (dnieCertificate != nullptr)
            CertFreeCertificateContext(dnieCertificate);
        dnieCertificate = CertDuplicateCertificateContext(current);
        ++dnieCount;
    }

    if (dnieCount == 1)
        return dnieCertificate;   // Caso normal: un único DNIe conocido en este equipo.

    if (dnieCount == 0)
    {
        error = L"No se detecta el DNIe. Compruebe que el lector está conectado, que el DNIe está "
                L"insertado y que su middleware (Card Module) está instalado.";
        return nullptr;
    }

    // Hay varios DNIe conocidos. Lo habitual es que solo uno esté en el lector
    // ahora mismo (los demás son de tarjetas que se usaron antes y ya se
    // retiraron): lo buscamos y, si es así, lo usamos sin preguntar.
    CertFreeCertificateContext(dnieCertificate);

    HCERTSTORE inserted = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    ScopeExit closeInserted([&] { if (inserted != nullptr) CertCloseStore(inserted, 0); });

    PCCERT_CONTEXT insertedCertificate = nullptr;   // Último certificado visto cuya tarjeta está puesta.
    int insertedCount = 0;

    if (inserted != nullptr)
    {
        current = nullptr;
        while ((current = CertEnumCertificatesInStore(candidates, current)) != nullptr)
        {
            if (!IsPrivateKeyAvailableNow(current))
                continue;

            CertAddCertificateContextToStore(inserted, current, CERT_STORE_ADD_ALWAYS, nullptr);

            if (insertedCertificate != nullptr)
                CertFreeCertificateContext(insertedCertificate);
            insertedCertificate = CertDuplicateCertificateContext(current);
            ++insertedCount;
        }
    }

    if (insertedCount == 1)
        return insertedCertificate;   // El DNIe que hay en el lector.
    if (insertedCertificate != nullptr)
        CertFreeCertificateContext(insertedCertificate);

    // Sigue sin estar claro: o no hay ningún DNIe puesto (se ofrecen todos
    // los conocidos, y Windows pedirá después que se inserte el elegido), o
    // hay varios a la vez en distintos lectores (se ofrecen solo esos). Que
    // elija el usuario. Los tres últimos argumentos (0, 0, nullptr) son "sin
    // opciones especiales": mostrar todas las columnas habituales y no usar
    // parámetros reservados.
    PCCERT_CONTEXT chosen = CryptUIDlgSelectCertificateFromStore(
        insertedCount > 1 ? inserted : candidates, ownerWindow,
        L"Seleccione el certificado de firma",
        L"Se ha encontrado más de un DNIe. Elija el certificado con el que desea firmar:",
        0, 0, nullptr);
    if (chosen == nullptr)
        error = L"Operación cancelada: no se seleccionó ningún certificado.";
    return chosen;
}

// ============================================================================
//  PIN de la tarjeta
// ============================================================================

// ¿Está la clave privada de este certificado en una tarjeta inteligente?
// Lo sabemos por el nombre del proveedor criptográfico anotado en el
// certificado: los de tarjeta que trae Windows (los que usan un Card Module)
// se llaman "Microsoft Smart Card Key Storage Provider" y "Microsoft Base
// Smart Card Crypto Provider". Un certificado software usa otros proveedores
// y no tiene PIN que pedir.
static bool IsSmartCardKey(PCCERT_CONTEXT certificate)
{
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size))
        return false;

    std::vector<BYTE> buffer(size);
    if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, buffer.data(), &size))
        return false;

    const CRYPT_KEY_PROV_INFO* info = (const CRYPT_KEY_PROV_INFO*)buffer.data();
    return info->pwszProvName != nullptr &&
           ToUpper(info->pwszProvName).find(L"SMART CARD") != std::wstring::npos;
}

// Entrega el PIN a la clave privada, para que el proveedor de Windows no
// muestre su propio diálogo.
//
// ¿Cuándo comprueba la tarjeta el PIN? Depende del proveedor: puede hacerlo
// ya en esta llamada o esperar a la firma. Por eso un PIN incorrecto puede
// dar error aquí ("La tarjeta no aceptó el PIN") o más tarde, al firmar
// ("No se pudo firmar"). En los dos casos DescribeError lo explica igual.
//
// Según cómo nos haya dado Windows la clave hay dos caminos:
//   - Clave CNG (lo normal y lo que pedimos): propiedad NCRYPT_PIN_PROPERTY,
//     el PIN como texto Unicode terminado en cero.
//   - Clave CryptoAPI antigua (solo si CNG no estuviera disponible):
//     parámetro PP_SIGNATURE_PIN / PP_KEYEXCHANGE_PIN, el PIN como texto ANSI.
static bool GivePinToKey(HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key, DWORD keySpec,
                         const std::wstring& pin, std::wstring& error)
{
    if (keySpec == CERT_NCRYPT_KEY_SPEC)
    {
        const SECURITY_STATUS status = NCryptSetProperty(
            (NCRYPT_KEY_HANDLE)key, NCRYPT_PIN_PROPERTY,
            (PBYTE)pin.c_str(), (DWORD)((pin.size() + 1) * sizeof(wchar_t)), 0);
        if (status != ERROR_SUCCESS)
        {
            error = DescribeError(L"La tarjeta no aceptó el PIN", (DWORD)status);
            return false;
        }
        return true;
    }

    // Hasta 4 bytes por carácter: en algunas páginas de códigos ANSI un
    // carácter ocupa más de un byte. El +1 es para el cero final.
    char ansiPin[PIN_MAX_LENGTH * 4 + 1] = {};
    WideCharToMultiByte(CP_ACP, 0, pin.c_str(), -1, ansiPin, sizeof(ansiPin) - 1, nullptr, nullptr);
    const DWORD parameter = (keySpec == AT_SIGNATURE) ? PP_SIGNATURE_PIN : PP_KEYEXCHANGE_PIN;
    const BOOL ok = CryptSetProvParam((HCRYPTPROV)key, parameter, (const BYTE*)ansiPin, 0);
    const DWORD lastError = GetLastError();
    SecureZeroMemory(ansiPin, sizeof(ansiPin));
    if (!ok)
    {
        error = DescribeError(L"La tarjeta no aceptó el PIN", lastError);
        return false;
    }
    return true;
}

// ============================================================================
//  Versión del DNIe
// ============================================================================
//
//  El PIN no mide lo mismo en todas las versiones (ver PinDialog.h), así que
//  antes de pedirlo averiguamos qué DNIe hay en el lector. Lo sabemos por su
//  ATR ("Answer To Reset"): la primera respuesta que da un chip al recibir
//  corriente, unos pocos bytes que lo identifican y que cualquiera puede
//  leer sin PIN.
//
//  Es el único momento en que este archivo consulta el lector directamente
//  (con las funciones SCard... de Windows), y solo para leer: toda la
//  criptografía sigue pasando por el Card Module.
//
//  Si algo falla por el camino, la versión queda como DNIE_UNKNOWN y el
//  diálogo del PIN admite el intervalo completo: la firma no se impide.
//
//  La versión se averigua en dos situaciones:
//    - Al firmar (DetectDnieVersion): se mira el lector donde está la tarjeta
//      de la clave con la que se va a firmar.
//    - Cuando la página lo pregunta (GetInsertedDnieVersion, propiedad
//      DnieVersion del control): todavía no hay clave, así que se recorren
//      todos los lectores del equipo.

// Nombre del lector donde está la tarjeta de esta clave privada. Devuelve
// una cadena vacía si no se puede saber.
static std::wstring GetSmartCardReaderName(HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key, DWORD keySpec)
{
    if (keySpec == CERT_NCRYPT_KEY_SPEC)
    {
        // Clave CNG: propiedad "SmartCardReader", texto Unicode.
        wchar_t reader[256] = {};
        DWORD size = 0;
        if (NCryptGetProperty((NCRYPT_KEY_HANDLE)key, NCRYPT_READER_PROPERTY, (PBYTE)reader,
                              sizeof(reader) - sizeof(wchar_t), &size, 0) != ERROR_SUCCESS)
            return L"";
        return reader;
    }

    // Clave CryptoAPI antigua: parámetro PP_SMARTCARD_READER, texto ANSI.
    char ansiReader[256] = {};
    DWORD size = sizeof(ansiReader) - 1;
    if (!CryptGetProvParam((HCRYPTPROV)key, PP_SMARTCARD_READER, (BYTE*)ansiReader, &size, 0))
        return L"";

    wchar_t reader[256] = {};
    MultiByteToWideChar(CP_ACP, 0, ansiReader, -1, reader, ARRAYSIZE(reader) - 1);
    return reader;
}

// Lee el ATR de la tarjeta que hay en el lector indicado. Devuelve false si
// no hay tarjeta o no se puede consultar el lector.
static bool ReadCardAtr(const std::wstring& readerName, std::vector<BYTE>& atr)
{
    atr.clear();

    // "Contexto": la conexión con el servicio de tarjetas inteligentes de
    // Windows, necesaria para cualquier función SCard...
    SCARDCONTEXT context = 0;
    if (SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &context) != SCARD_S_SUCCESS)
        return false;
    ScopeExit releaseContext([&] { SCardReleaseContext(context); });

    // Preguntamos por el estado del lector. SCARD_STATE_UNAWARE = "no sé
    // nada de él, dímelo ya"; con espera 0 la llamada vuelve al instante.
    // Windows devuelve en la misma estructura el estado y el ATR. No hace
    // falta conectarse a la tarjeta, así que no estorbamos al Card Module.
    SCARD_READERSTATEW state = {};
    state.szReader = readerName.c_str();
    state.dwCurrentState = SCARD_STATE_UNAWARE;
    if (SCardGetStatusChangeW(context, 0, &state, 1) != SCARD_S_SUCCESS)
        return false;
    if ((state.dwEventState & SCARD_STATE_PRESENT) == 0 || state.cbAtr == 0)
        return false;

    atr.assign(state.rgbAtr, state.rgbAtr + state.cbAtr);
    return true;
}

// Deduce la versión del DNIe a partir de su ATR.
//
// El ATR de un DNIe tiene 20 bytes. Estos son los de las dos versiones
// ("xx" es un byte que varía de una tarjeta a otra):
//
//   posición   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
//   DNIe 3.0  3B 7F 96 00 00 00 6A 44 4E 49 65 xx 01 01 55 04 21 03 90 00
//   DNIe 4.0  3B 7F 96 00 00 00 6A 44 4E 49 65 xx 02 00 B8 05 53 03 90 00
//                               ^^^^^^^^^^^^^^             ^^
//                               6A + "DNIe"                versión
//
//   - Posiciones 6 a 10: la marca que identifica la tarjeta como DNIe, el
//     byte 6A seguido de las letras "DNIe" (44 4E 49 65 en ASCII).
//   - Posición 15: el número de versión del sistema operativo del chip,
//     04 en el DNIe 3.0 y 05 en el 4.0. Es el dato que miramos.
//
// No comparamos el ATR entero porque otros bytes cambian entre tarjetas de
// una misma versión (fabricante, modelo de chip, revisión...).
static DnieVersion ClassifyDnieAtr(const std::vector<BYTE>& atr)
{
    static const BYTE DNIE_MARK[] = { 0x6A, 'D', 'N', 'I', 'e' };

    if (atr.size() != 20 || memcmp(&atr[6], DNIE_MARK, sizeof(DNIE_MARK)) != 0)
        return DNIE_UNKNOWN;   // No es un DNIe.

    switch (atr[15])
    {
    case 0x04: return DNIE_3;
    case 0x05: return DNIE_4;
    default:   return DNIE_UNKNOWN;   // Otra versión, anterior o futura.
    }
}

// Versión del DNIe que contiene esta clave privada, o DNIE_UNKNOWN.
static DnieVersion DetectDnieVersion(HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key, DWORD keySpec)
{
    const std::wstring reader = GetSmartCardReaderName(key, keySpec);
    if (reader.empty())
        return DNIE_UNKNOWN;

    std::vector<BYTE> atr;
    if (!ReadCardAtr(reader, atr))
        return DNIE_UNKNOWN;

    return ClassifyDnieAtr(atr);
}

// Nombres de los lectores de tarjetas conectados al equipo. Devuelve una
// lista vacía si no hay ninguno o si no se pueden consultar.
static std::vector<std::wstring> ListSmartCardReaders()
{
    std::vector<std::wstring> readers;

    SCARDCONTEXT context = 0;
    if (SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &context) != SCARD_S_SUCCESS)
        return readers;
    ScopeExit releaseContext([&] { SCardReleaseContext(context); });

    // Patrón habitual en Windows: primera llamada para saber el tamaño,
    // segunda para obtener los datos.
    DWORD length = 0;
    if (SCardListReadersW(context, nullptr, nullptr, &length) != SCARD_S_SUCCESS || length == 0)
        return readers;
    std::vector<wchar_t> names(length);
    if (SCardListReadersW(context, nullptr, names.data(), &length) != SCARD_S_SUCCESS)
        return readers;

    // Windows devuelve todos los nombres seguidos en un mismo búfer, cada
    // uno terminado en cero, y un cero más al final: "Lector A\0Lector B\0\0".
    // Los recorremos saltando de un nombre al siguiente.
    for (const wchar_t* name = names.data(); *name != L'\0'; name += wcslen(name) + 1)
        readers.push_back(name);
    return readers;
}

std::wstring GetInsertedDnieVersion()
{
    // Miramos la tarjeta de cada lector y nos quedamos con el primer DNIe
    // cuya versión reconozcamos.
    for (const std::wstring& reader : ListSmartCardReaders())
    {
        std::vector<BYTE> atr;
        if (!ReadCardAtr(reader, atr))
            continue;   // Lector vacío.

        switch (ClassifyDnieAtr(atr))
        {
        case DNIE_3: return L"3.0";
        case DNIE_4: return L"4.0";
        default:     break;   // Otra tarjeta, o un DNIe de versión desconocida.
        }
    }
    return L"";
}

// ============================================================================
//  FIRMAR
// ============================================================================
bool SignFileDetached(const std::wstring& inputPath, const std::wstring& signaturePath,
                      const std::wstring& thumbprint, HWND ownerWindow,
                      std::wstring& signerName, std::wstring& error)
{
    signerName.clear();
    error.clear();

    // --- Paso 1: abrir el almacén personal "MY" del usuario ----------------
    // Ahí están los certificados propios; Windows añade los del DNIe al
    // insertar la tarjeta.
    //   CERT_SYSTEM_STORE_CURRENT_USER  el almacén del usuario, no el del equipo.
    //   CERT_STORE_READONLY_FLAG        solo vamos a leer.
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    if (store == nullptr)
    {
        error = DescribeError(L"No se pudo abrir el almacén de certificados", GetLastError());
        return false;
    }
    ScopeExit closeStore([&] { CertCloseStore(store, 0); });

    // --- Paso 2: elegir el certificado de firma ----------------------------
    PCCERT_CONTEXT certificate = ChooseSigningCertificate(store, thumbprint, ownerWindow, error);
    if (certificate == nullptr)
        return false;
    ScopeExit freeCertificate([&] { CertFreeCertificateContext(certificate); });

    signerName = GetCertificateName(certificate);

    // --- Paso 3: obtener la clave privada del certificado ------------------
    // No obtenemos la clave en sí (no puede salir del chip), sino un "asa"
    // para pedir operaciones con ella.
    //   CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG  = la queremos a través de CNG.
    //   CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG      = le decimos sobre qué ventana
    //       mostrar sus diálogos, si necesitara alguno (p. ej. "inserte la
    //       tarjeta"), para que no queden ocultos detrás del navegador.
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD keySpec = 0;          // Tipo de clave que nos han dado (CNG o CryptoAPI).
    BOOL mustFreeKey = FALSE;   // Windows nos dice si nos toca liberarla.
    DWORD acquireFlags = CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG;
    if (ownerWindow != nullptr)
        acquireFlags |= CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG;

    if (!CryptAcquireCertificatePrivateKey(certificate, acquireFlags,
                                           ownerWindow != nullptr ? &ownerWindow : nullptr,
                                           &key, &keySpec, &mustFreeKey))
    {
        error = DescribeError(L"No se pudo acceder a la clave privada del certificado", GetLastError());
        return false;
    }
    ScopeExit freeKey([&] {
        if (!mustFreeKey)
            return;
        if (keySpec == CERT_NCRYPT_KEY_SPEC)
            NCryptFreeObject(key);
        else
            CryptReleaseContext(key, 0);
    });

    // --- Paso 4: pedir el PIN (solo si la clave está en una tarjeta) -------
    if (IsSmartCardKey(certificate))
    {
        std::wstring pin;
        ScopeExit wipePin([&] { WipeString(pin); });   // Se borra al salir, pase lo que pase.

        if (!AskForPin(ownerWindow, DetectDnieVersion(key, keySpec), pin))
        {
            error = L"Operación cancelada: no se introdujo el PIN.";
            return false;
        }
        if (!GivePinToKey(key, keySpec, pin, error))
            return false;
    }

    // --- Paso 5: preparar los datos del firmante ---------------------------
    CMSG_SIGNER_ENCODE_INFO signer = {};
    signer.cbSize = sizeof(signer);
    signer.pCertInfo = certificate->pCertInfo;      // Quién firma.
    // Con qué clave. El campo hNCryptKey comparte sitio (es una "union") con
    // hCryptProv, el de las claves CryptoAPI antiguas; dwKeySpec le dice a
    // Windows cuál de las dos clases de clave le estamos pasando.
    signer.hNCryptKey = key;
    signer.dwKeySpec = keySpec;
    signer.HashAlgorithm.pszObjId = (LPSTR)szOID_NIST_sha256;   // Hash SHA-256.
    // El algoritmo de firma (RSA, ECDSA...) no se indica: Windows lo deduce
    // del tipo de clave del certificado.

    // Atributo firmado "hora de la firma". Es la hora del reloj de este PC
    // (no un sello de tiempo de una autoridad), pero queda protegida por la
    // firma: no se puede cambiar después sin invalidarla.
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    BYTE encodedTime[64];
    DWORD encodedTimeSize = sizeof(encodedTime);
    CRYPT_ATTR_BLOB timeBlob = {};
    CRYPT_ATTRIBUTE timeAttribute = {};
    if (CryptEncodeObject(ENCODING, szOID_RSA_signingTime, &now, encodedTime, &encodedTimeSize))
    {
        timeBlob.cbData = encodedTimeSize;
        timeBlob.pbData = encodedTime;
        timeAttribute.pszObjId = (LPSTR)szOID_RSA_signingTime;
        timeAttribute.cValue = 1;
        timeAttribute.rgValue = &timeBlob;
        signer.cAuthAttr = 1;
        signer.rgAuthAttr = &timeAttribute;
    }

    // --- Paso 6: certificados que se incluyen dentro de la firma -----------
    // Incluimos el del firmante y los de las autoridades intermedias que lo
    // emitieron (p. ej. "AC DNIE 004"), para que quien verifique pueda
    // reconstruir la cadena de confianza. La raíz no se incluye: quien
    // verifica debe tenerla ya instalada (si no, no sería de confianza).
    std::vector<CERT_BLOB> includedCertificates;
    includedCertificates.push_back({ certificate->cbCertEncoded, certificate->pbCertEncoded });

    CERT_CHAIN_PARA chainParameters = {};
    chainParameters.cbSize = sizeof(chainParameters);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    // CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL: no salir a Internet a buscar
    // certificados; usar solo los que ya hay en el equipo (más rápido).
    // Los argumentos nulos significan: motor de cadenas por defecto del
    // usuario, validar en la fecha actual y sin almacén adicional.
    if (!CertGetCertificateChain(nullptr, certificate, nullptr, nullptr, &chainParameters,
                                 CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr, &chain))
        chain = nullptr;
    ScopeExit freeChain([&] { if (chain != nullptr) CertFreeCertificateChain(chain); });

    if (chain != nullptr && chain->cChain > 0)
    {
        const CERT_SIMPLE_CHAIN* simpleChain = chain->rgpChain[0];
        // El elemento 0 es el firmante (ya añadido); empezamos en el 1.
        for (DWORD i = 1; i < simpleChain->cElement; ++i)
        {
            const CERT_CHAIN_ELEMENT* element = simpleChain->rgpElement[i];
            if (element->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED)
                continue;   // Es la raíz: no se incluye.
            includedCertificates.push_back({ element->pCertContext->cbCertEncoded,
                                             element->pCertContext->pbCertEncoded });
        }
    }

    // --- Paso 7: crear el mensaje PKCS#7 y pasarle el documento ------------
    CMSG_SIGNED_ENCODE_INFO signedInfo = {};
    signedInfo.cbSize = sizeof(signedInfo);
    signedInfo.cSigners = 1;
    signedInfo.rgSigners = &signer;
    signedInfo.cCertEncoded = (DWORD)includedCertificates.size();
    signedInfo.rgCertEncoded = includedCertificates.data();

    // CMSG_DETACHED_FLAG = firma separada: el documento no va dentro.
    HCRYPTMSG message = CryptMsgOpenToEncode(ENCODING, CMSG_DETACHED_FLAG, CMSG_SIGNED,
                                             &signedInfo, nullptr, nullptr);
    if (message == nullptr)
    {
        error = DescribeError(L"No se pudo iniciar la firma", GetLastError());
        return false;
    }
    ScopeExit closeMessage([&] { CryptMsgClose(message); });

    // Aquí se calcula el hash del documento y, al llegar al final, la tarjeta
    // realiza la firma.
    if (!FeedFileToMessage(message, inputPath, L"No se pudo firmar", error))
        return false;

    // --- Paso 8: recoger la firma terminada y guardarla --------------------
    // Patrón habitual en Windows: primera llamada para saber el tamaño,
    // segunda para obtener los datos.
    // El nombre engaña: en un mensaje abierto para CODIFICAR,
    // CMSG_CONTENT_PARAM no devuelve "el contenido" (el documento), sino el
    // PKCS#7 completo ya terminado: justo lo que hay que guardar en el .p7s.
    DWORD signatureSize = 0;
    if (!CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, nullptr, &signatureSize))
    {
        error = DescribeError(L"No se pudo obtener la firma", GetLastError());
        return false;
    }
    std::vector<BYTE> signature(signatureSize);
    if (!CryptMsgGetParam(message, CMSG_CONTENT_PARAM, 0, signature.data(), &signatureSize))
    {
        error = DescribeError(L"No se pudo obtener la firma", GetLastError());
        return false;
    }

    // CREATE_ALWAYS: si ya existía una firma con ese nombre, se sobrescribe
    // sin preguntar.
    HANDLE output = CreateFileW(signaturePath.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        error = DescribeError(L"No se pudo crear el archivo de firma \"" + signaturePath + L"\"", GetLastError());
        return false;
    }
    DWORD bytesWritten = 0;
    const BOOL written = WriteFile(output, signature.data(), signatureSize, &bytesWritten, nullptr);
    const DWORD writeError = GetLastError();
    CloseHandle(output);
    if (!written || bytesWritten != signatureSize)
    {
        DeleteFileW(signaturePath.c_str());   // No dejar una firma a medias.
        error = DescribeError(L"No se pudo escribir el archivo de firma", writeError);
        return false;
    }

    return true;
}

// ============================================================================
//  VERIFICAR
// ============================================================================

// Comprueba que el certificado del firmante es de confianza para Windows:
// que encadena con una autoridad raíz instalada, que no está caducado, etc.
// 'extraStore' son los certificados que venían dentro de la firma (las
// autoridades intermedias), que ayudan a construir la cadena.
//
// Son dos pasos:
//   - CertGetCertificateChain CONSTRUYE la cadena (firmante -> autoridad
//     intermedia -> raíz). Casi siempre "tiene éxito" aunque la cadena no
//     sea de confianza: los problemas los deja anotados dentro.
//   - CertVerifyCertificateChainPolicy la EVALÚA y resume el resultado en
//     un único código de error. CERT_CHAIN_POLICY_BASE es la política
//     general: raíz de confianza, fechas y firmas de la cadena.
//
// Limitaciones de este ejemplo (ampliables por quien lo necesite):
//   - Se valida con la fecha actual, no con la fecha de la firma.
//   - No se consulta si el certificado ha sido revocado (OCSP/CRL), porque
//     obliga a consultar por Internet a la autoridad en cada verificación.
//     Para activarlo, añadir a CertGetCertificateChain el indicador
//     CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT.
//   - Aun sin revocación, Windows puede salir a Internet aquí para
//     descargar un certificado intermedio que le falte (al firmar se lo
//     impedimos con CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL; aquí no).
static bool IsCertificateTrusted(PCCERT_CONTEXT certificate, HCERTSTORE extraStore, std::wstring& reason)
{
    CERT_CHAIN_PARA chainParameters = {};
    chainParameters.cbSize = sizeof(chainParameters);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!CertGetCertificateChain(nullptr, certificate, nullptr, extraStore, &chainParameters, 0, nullptr, &chain))
    {
        reason = DescribeError(L"no se pudo construir su cadena de certificación", GetLastError());
        return false;
    }
    ScopeExit freeChain([&] { CertFreeCertificateChain(chain); });

    CERT_CHAIN_POLICY_PARA policyParameters = {};
    policyParameters.cbSize = sizeof(policyParameters);
    CERT_CHAIN_POLICY_STATUS policyStatus = {};
    policyStatus.cbSize = sizeof(policyStatus);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policyParameters, &policyStatus))
    {
        reason = DescribeError(L"no se pudo evaluar su cadena de certificación", GetLastError());
        return false;
    }
    if (policyStatus.dwError != ERROR_SUCCESS)
    {
        reason = DescribeError(L"Windows no confía en él", policyStatus.dwError);
        return false;
    }
    return true;
}

bool VerifyFileDetached(const std::wstring& inputPath, const std::wstring& signaturePath,
                        bool checkTrust, std::wstring& signerName, std::wstring& error)
{
    signerName.clear();
    error.clear();

    // --- Paso 1: leer el archivo de firma ----------------------------------
    std::vector<BYTE> signature;
    if (!ReadWholeFile(signaturePath, signature, error))
        return false;

    // Una firma binaria (DER) empieza siempre por el byte 0x30. Si no es así,
    // probablemente esté guardada como texto Base64 (con o sin cabeceras
    // "-----BEGIN PKCS7-----"): intentamos convertirla a binario.
    if (signature[0] != 0x30)
    {
        DWORD binarySize = 0;
        if (CryptStringToBinaryA((LPCSTR)signature.data(), (DWORD)signature.size(),
                                 CRYPT_STRING_BASE64_ANY, nullptr, &binarySize, nullptr, nullptr))
        {
            std::vector<BYTE> binary(binarySize);
            if (CryptStringToBinaryA((LPCSTR)signature.data(), (DWORD)signature.size(),
                                     CRYPT_STRING_BASE64_ANY, binary.data(), &binarySize, nullptr, nullptr))
            {
                binary.resize(binarySize);
                signature.swap(binary);
            }
        }
    }

    // --- Paso 2: abrir el mensaje PKCS#7 y cargarle la firma ---------------
    // El tercer argumento (0) significa "averigua tú qué tipo de mensaje
    // es"; por eso justo después comprobamos que sea uno firmado (CMSG_SIGNED).
    HCRYPTMSG message = CryptMsgOpenToDecode(ENCODING, CMSG_DETACHED_FLAG, 0, 0, nullptr, nullptr);
    if (message == nullptr)
    {
        error = DescribeError(L"No se pudo iniciar la verificación", GetLastError());
        return false;
    }
    ScopeExit closeMessage([&] { CryptMsgClose(message); });

    DWORD messageType = 0;
    DWORD size = sizeof(messageType);
    if (!CryptMsgUpdate(message, signature.data(), (DWORD)signature.size(), TRUE) ||
        !CryptMsgGetParam(message, CMSG_TYPE_PARAM, 0, &messageType, &size) ||
        messageType != CMSG_SIGNED)
    {
        error = L"El archivo \"" + signaturePath + L"\" no contiene una firma PKCS#7 válida.";
        return false;
    }

    // --- Paso 3: pasarle el documento para que calcule su hash -------------
    // (En una firma separada, primero se carga la firma y después el
    // documento; por eso hay dos tandas de CryptMsgUpdate.)
    if (!FeedFileToMessage(message, inputPath, L"No se pudo procesar el archivo", error))
        return false;

    // --- Paso 4: comprobar la firma de cada firmante -----------------------
    DWORD signerCount = 0;
    size = sizeof(signerCount);
    if (!CryptMsgGetParam(message, CMSG_SIGNER_COUNT_PARAM, 0, &signerCount, &size) || signerCount == 0)
    {
        error = L"El archivo de firma no contiene ningún firmante.";
        return false;
    }

    // Los certificados que venían dentro de la firma, vistos como un almacén.
    HCERTSTORE messageStore = CertOpenStore(CERT_STORE_PROV_MSG, ENCODING, 0, 0, message);
    if (messageStore == nullptr)
    {
        error = DescribeError(L"No se pudieron leer los certificados de la firma", GetLastError());
        return false;
    }
    ScopeExit closeMessageStore([&] { CertCloseStore(messageStore, 0); });

    for (DWORD index = 0; index < signerCount; ++index)
    {
        // La firma identifica al firmante por emisor + número de serie.
        // Con esos datos localizamos su certificado completo.
        DWORD signerInfoSize = 0;
        if (!CryptMsgGetParam(message, CMSG_SIGNER_CERT_INFO_PARAM, index, nullptr, &signerInfoSize))
        {
            error = DescribeError(L"No se pudo leer la identidad del firmante", GetLastError());
            return false;
        }
        std::vector<BYTE> signerInfo(signerInfoSize);
        if (!CryptMsgGetParam(message, CMSG_SIGNER_CERT_INFO_PARAM, index, signerInfo.data(), &signerInfoSize))
        {
            error = DescribeError(L"No se pudo leer la identidad del firmante", GetLastError());
            return false;
        }

        PCCERT_CONTEXT signerCertificate =
            CertGetSubjectCertificateFromStore(messageStore, ENCODING, (PCERT_INFO)signerInfo.data());
        if (signerCertificate == nullptr)
        {
            error = L"La firma no incluye el certificado del firmante; no se puede verificar.";
            return false;
        }
        ScopeExit freeSignerCertificate([&] { CertFreeCertificateContext(signerCertificate); });

        const std::wstring name = GetCertificateName(signerCertificate);
        if (!signerName.empty())
            signerName += L"; ";
        signerName += name;

        // LA COMPROBACIÓN CLAVE: con la clave pública del certificado se
        // comprueba que la firma corresponde al hash del documento. Si se ha
        // cambiado un solo bit del documento (o de la firma), falla.
        if (!CryptMsgControl(message, 0, CMSG_CTRL_VERIFY_SIGNATURE, signerCertificate->pCertInfo))
        {
            const DWORD code = GetLastError();
            if (code == (DWORD)NTE_BAD_SIGNATURE || code == (DWORD)CRYPT_E_HASH_VALUE)
                error = L"La firma NO es válida: el archivo ha sido modificado después de firmarse, "
                        L"o la firma corresponde a otro archivo.";
            else
                error = DescribeError(L"La firma NO es válida", code);
            return false;
        }

        // Comprobación adicional: ¿quién dice ser el firmante lo es de verdad?
        // (¿su certificado lo avala una autoridad de confianza?)
        if (checkTrust)
        {
            std::wstring reason;
            if (!IsCertificateTrusted(signerCertificate, messageStore, reason))
            {
                error = L"La firma corresponde al archivo, pero el certificado del firmante (" + name +
                        L") no es de confianza: " + reason;
                return false;
            }
        }
    }

    return true;
}
