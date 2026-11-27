// =================================================================================================
//  DLL_ADMIN.cpp  -  Implementación de los métodos que llama la página (ver DLL_ADMIN.h)
// =================================================================================================
//
//  Cada método sigue el mismo patrón:
//    1. Lee las propiedades de entrada que la página ha rellenado antes (EtiquetaKeyPrivIn,
//       DatosIn...), guardadas en los miembros m_xxx.
//    2. Pide el trabajo a m_tarjeta (CTarjeta, Tarjeta.cpp), que habla con el driver PKCS#11.
//    3. Deja los resultados en los miembros de salida (m_datosOut, m_firma...), que la página leerá
//       después con las propiedades ...Out.
//    4. Termina con "return Resultado(rv)": el código queda en lastresultOut.
//
//  Secciones de este fichero, por orden:
//    Conversiones BSTR <-> datos · TLV de los grupos de datos · Conexión · Información del chip ·
//    PIN · Etiquetas · Certificados, datos y claves públicas · Firma y verificación · Resumen
//    SHA-256 · Propiedades sin función · Datos personales e imágenes · Lectura de grupos de datos ·
//    PIN y permiso del usuario (ventanas propias)
// =================================================================================================
#include "stdafx.h"
#include "DLL_ADMIN.h"
#include "Dialogos.h"
#include "PinDialog.h"
#include <exdisp.h>
#include <mshtml.h>
#include <shlguid.h>
#include <cwctype>
#include <cwchar>
#include <cstdio>

#pragma comment(lib, "version.lib")

// ---------------------------------------------------------------------------------------------
// Conversiones BSTR <-> datos
// ---------------------------------------------------------------------------------------------
// BSTR es el tipo de texto de COM: una cadena de caracteres de 16 bits (UTF-16) que guarda su
// longitud delante, así que puede contener caracteres nulos. JavaScript lo ve como un string normal.
//
// Para pasar datos BINARIOS (un certificado, una firma...) el ActiveX publicado usaba un truco que
// aquí se mantiene: cada byte va en un carácter, con su mismo valor (0..255). En JavaScript se
// recupera cada byte con s.charCodeAt(i) & 0xFF. Es distinto del TEXTO (nombres, etiquetas), que va
// convertido a caracteres de verdad (BstrATexto / TextoABstr, y DevolverCampo para UTF-8).

// Binario -> BSTR con un carácter por byte (convención del ActiveX publicado)
static HRESULT BytesABstr(const Bytes& b, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = SysAllocStringLen(NULL, (UINT)b.size());
	if (!*pVal && !b.empty())
		return E_OUTOFMEMORY;
	for (size_t i = 0; i < b.size(); i++)
		(*pVal)[i] = (WCHAR)b[i];
	return S_OK;
}

// BSTR con un carácter por byte -> binario (byte bajo de cada carácter)
static Bytes BstrABytes(BSTR s)
{
	UINT n = s ? SysStringLen(s) : 0;
	Bytes b(n);
	for (UINT i = 0; i < n; i++)
		b[i] = (unsigned char)(s[i] & 0xFF);
	return b;
}

// Texto -> cadena en la página de códigos ANSI del sistema (como el ActiveX publicado)
static std::string BstrATexto(BSTR s)
{
	UINT n = s ? SysStringLen(s) : 0;
	if (n == 0)
		return std::string();
	int len = WideCharToMultiByte(CP_ACP, 0, s, (int)n, NULL, 0, NULL, NULL);
	std::string r(len, '\0');
	WideCharToMultiByte(CP_ACP, 0, s, (int)n, &r[0], len, NULL, NULL);
	return r;
}

static HRESULT TextoABstr(const std::string& t, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	int n = MultiByteToWideChar(CP_ACP, 0, t.data(), (int)t.size(), NULL, 0);
	*pVal = SysAllocStringLen(NULL, n);
	if (!*pVal && n)
		return E_OUTOFMEMORY;
	MultiByteToWideChar(CP_ACP, 0, t.data(), (int)t.size(), *pVal, n);
	return S_OK;
}

static HRESULT Vacio(BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = SysAllocString(L"");
	return S_OK;
}

static std::string AHex(const Bytes& b)
{
	static const char* d = "0123456789ABCDEF";
	std::string r;
	for (unsigned char c : b) { r += d[c >> 4]; r += d[c & 15]; }
	return r;
}

// ---------------------------------------------------------------------------------------------
// TLV (BER) de los grupos de datos
// ---------------------------------------------------------------------------------------------
// Los grupos de datos del DNIe (DG1, DG2...) están codificados en ASN.1 BER, como los certificados.
// Todo son bloques TLV: Tipo (etiqueta), Longitud y Valor.
//
//   Etiqueta: 1 byte, o varios si los 5 bits bajos del primero son 11111 (p. ej. 5F 1F = MRZ).
//             El bit 0x20 del primer byte indica "construido": su valor contiene otros TLV dentro.
//   Longitud: si es menor de 128, un solo byte. Si no, 0x81, 0x82 o 0x83 seguido de 1, 2 o 3 bytes
//             con la longitud (p. ej. 82 1A 2B = 0x1A2B bytes).
//   Valor:    los bytes del dato.
//
// Ejemplo (DG1 del DNIe):  61 5D  5F 1F 5A  <90 caracteres de la MRZ>
//   61 = grupo DG1 (construido), 5D = 93 bytes de contenido;
//   5F 1F = etiqueta de la MRZ (dos bytes), 5A = 90 bytes de valor.

// Lee la cabecera TLV en p: etiqueta (1 o más bytes), posición y longitud del valor.
static bool LeerTLV(const Bytes& b, size_t p, size_t fin, unsigned& tag, size_t& valor, size_t& len, bool& construido)
{
	if (p >= fin)
		return false;
	construido = (b[p] & 0x20) != 0;
	tag = b[p++];
	if ((tag & 0x1F) == 0x1F)
	{
		do
		{
			if (p >= fin || tag > 0xFFFFFF)
				return false;
			tag = (tag << 8) | b[p];
		} while (b[p++] & 0x80);
	}
	if (p >= fin)
		return false;
	len = b[p++];
	if (len & 0x80)
	{
		size_t n = len & 0x7F;
		if (n == 0 || n > 3)
			return false;
		len = 0;
		for (size_t k = 0; k < n; k++)
		{
			if (p >= fin)
				return false;
			len = (len << 8) | b[p++];
		}
	}
	if (len > fin - p)
		return false;
	valor = p;
	return true;
}

// Busca una etiqueta en profundidad dentro de [ini, fin). Devuelve la posición y longitud de su valor.
// Recorre los TLV uno tras otro y, si alguno es construido, busca también dentro de él (recursión).
// Todas las longitudes se comprueban en LeerTLV, así que un grupo de datos mal formado no puede
// hacer que se lea fuera del búfer.
static bool BuscarTag(const Bytes& b, size_t ini, size_t fin, unsigned buscada, size_t& off, size_t& len)
{
	size_t p = ini;
	while (p < fin)
	{
		unsigned tag;
		size_t v, l;
		bool construido;
		if (!LeerTLV(b, p, fin, tag, v, l, construido))
			return false;
		if (tag == buscada)
		{
			off = v;
			len = l;
			return true;
		}
		if (construido && BuscarTag(b, v, v + l, buscada, off, len))
			return true;
		p = v + l;
	}
	return false;
}

// Posición de cada dato dentro del DG13 del DNIe (16 cadenas de texto sin etiquetar, en este orden).
// El DG13 es un grupo "opcional" del estándar que cada país usa a su manera; el DNIe guarda en él los
// datos personales como una secuencia (etiqueta 0x30) de 16 cadenas UTF8String/PrintableString.
// Como no llevan etiqueta propia, cada dato se identifica por su POSICIÓN en la secuencia.
enum
{
	DG13_APELLIDO1 = 0,
	DG13_APELLIDO2 = 1,
	DG13_NOMBRE = 2,
	DG13_NUMERO_DNI = 3,
	DG13_FECHA_NACIMIENTO = 4,
	DG13_NACIONALIDAD = 5,
	DG13_FECHA_EXPIRACION = 6,
	DG13_SOPORTE = 7,
	DG13_SEXO = 8,
	DG13_LOCALIDAD_NACIMIENTO = 9,
	DG13_PROVINCIA_NACIMIENTO = 10,
	DG13_PADRES = 11,
	DG13_DOMICILIO = 12,
	DG13_LOCALIDAD_DOMICILIO = 13,
	// 14: vacío en el DNIe probado; no se usa
	DG13_PROVINCIA_DOMICILIO = 15
};

// Admite la firma en hexadecimal (lo que devuelve FirmaOut) o en bruto (un carácter por byte)
// Se considera hexadecimal si todos los caracteres son 0-9 / A-F y hay un número par. Una firma en
// bruto de 64 o 256 bytes no cumple eso en la práctica.
static Bytes FirmaDesdeBstr(BSTR s)
{
	UINT n = s ? SysStringLen(s) : 0;
	bool hex = (n > 0) && (n % 2 == 0);
	for (UINT i = 0; hex && i < n; i++)
		hex = iswxdigit(s[i]) != 0;
	if (!hex)
		return BstrABytes(s);
	Bytes b(n / 2);
	for (UINT i = 0; i < n / 2; i++)
	{
		wchar_t par[3] = { s[2 * i], s[2 * i + 1], 0 };
		b[i] = (unsigned char)wcstoul(par, NULL, 16);
	}
	return b;
}

// Versión de fichero de un módulo ("a.b.c.d"): la que se ve en Propiedades > Detalles de la DLL.
// GetFileVersionInfo lee el recurso de versión y VerQueryValue("\\") da la parte numérica.
static std::wstring VersionFichero(const std::wstring& ruta)
{
	DWORD h = 0, n = GetFileVersionInfoSizeW(ruta.c_str(), &h);
	if (!n)
		return L"";
	std::vector<BYTE> buf(n);
	VS_FIXEDFILEINFO* fi = NULL;
	UINT len = 0;
	if (!GetFileVersionInfoW(ruta.c_str(), 0, n, buf.data()) || !VerQueryValueW(buf.data(), L"\\", (LPVOID*)&fi, &len) || !fi)
		return L"";
	wchar_t v[64];
	swprintf_s(v, L"%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
	           HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
	return v;
}

// SecureZeroMemory, y no memset: el compilador puede quitar un memset sobre memoria que ya no se
// va a usar (lo ve "inútil"), y el PIN se quedaría en memoria. SecureZeroMemory nunca se omite.
void CDLL_ADMIN::BorrarPin()
{
	if (!m_pin.empty())
		SecureZeroMemory(&m_pin[0], m_pin.size());
	m_pin.clear();
}

// Entradas y salidas de la última operación: pueden contener datos personales (DatosOut con la MRZ
// o un grupo de datos, el certificado, el texto firmado...). Se borran con ceros al conectar, al
// desconectar, al cambiar de tarjeta y al destruir el objeto, igual que el PIN.
static void Borrar(Bytes& b)
{
	if (!b.empty())
		SecureZeroMemory(b.data(), b.size());
	b.clear();
}

void CDLL_ADMIN::BorrarResultados()
{
	for (Bytes* b : { &m_datosIn, &m_datosOut, &m_firma, &m_hash, &m_hashIn, &m_acumuladoHash,
	                  &m_modulo, &m_exponente, &m_numSerie, &m_certComponente, &m_bit })
		Borrar(*b);
	m_etiquetas.clear();
}

// Mientras existe, la tarjeta está marcada como ocupada (ver CTarjeta::MarcarOcupada): se usa
// alrededor de las ventanas del PIN y del permiso.
namespace
{
	class VentanaAbierta
	{
	public:
		explicit VentanaAbierta(CTarjeta& t) : m_t(t) { m_t.MarcarOcupada(true); }
		~VentanaAbierta() { m_t.MarcarOcupada(false); }
	private:
		CTarjeta& m_t;
	};
}

// ---------------------------------------------------------------------------------------------
// Conexión
// ---------------------------------------------------------------------------------------------

// Lo primero que llama la página. Olvida lo leído y el permiso de una conexión anterior y conecta
// con la tarjeta (Tarjeta.cpp: cargar el driver, C_Initialize, buscar el lector y abrir sesión).
STDMETHODIMP CDLL_ADMIN::InicializarConexion()
{
	if (m_tarjeta.Ocupada())
		return Resultado(CKR_OPERATION_ACTIVE);   // hay una ventana del control abierta
	OlvidarGrupos();
	BorrarResultados();
	m_permisoDatos = 0;
	// Si ya estaba conectado, se comprueba que siga siendo la misma tarjeta
	CK_RV rv = m_tarjeta.Conectada() ? m_tarjeta.Comprobar() : m_tarjeta.Conectar();
	if (rv == CKR_DEVICE_REMOVED)
		rv = CKR_OK;                       // ya conectado con la tarjeta nueva
	m_generacion = m_tarjeta.Generacion();
	return Resultado(rv);
}

// Si se ha sacado o cambiado la tarjeta, se olvida lo que se sabía de la anterior: grupos de datos
// leídos, permiso del usuario y PIN guardado en PinIn.
CK_RV CDLL_ADMIN::RevisarTarjeta()
{
	CK_RV rv = m_tarjeta.Comprobar();
	if (m_tarjeta.Generacion() != m_generacion)
	{
		m_generacion = m_tarjeta.Generacion();
		OlvidarGrupos();
		m_permisoDatos = 0;
		BorrarPin();
		BorrarResultados();
	}
	return rv;
}

STDMETHODIMP CDLL_ADMIN::FinalizarConexion()
{
	if (m_tarjeta.Ocupada())
		return Resultado(CKR_OPERATION_ACTIVE);   // no se cierra la sesión debajo de la ventana del PIN
	BorrarPin();
	OlvidarGrupos();
	BorrarResultados();
	return Resultado(m_tarjeta.Desconectar());
}

// Las propiedades "get_" son lo que JavaScript lee (ax.lastresultOut). Siguen el patrón de COM:
// el valor se devuelve por un puntero (pVal) y el retorno es S_OK, o E_POINTER si el puntero es nulo.
STDMETHODIMP CDLL_ADMIN::get_lastresultOut(int* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = m_rv;
	return S_OK;
}

// ---------------------------------------------------------------------------------------------
// Información del chip
// ---------------------------------------------------------------------------------------------

// Número de serie del token (CK_TOKEN_INFO.serialNumber, en hexadecimal) -> bytes.
STDMETHODIMP CDLL_ADMIN::ObtenerNumeroSerie()
{
	m_numSerie.clear();
	CK_TOKEN_INFO info;
	CK_RV rv = m_tarjeta.InfoToken(info);
	if (rv != CKR_OK)
		return Resultado(rv);
	std::string s((char*)info.serialNumber, sizeof(info.serialNumber));
	s.erase(s.find_last_not_of(' ') + 1);
	for (size_t i = 0; i + 1 < s.size(); i += 2)
		m_numSerie.push_back((unsigned char)strtoul(s.substr(i, 2).c_str(), NULL, 16));
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::get_NumeroSerieOut(BSTR* pVal)   { return BytesABstr(m_numSerie, pVal); }
STDMETHODIMP CDLL_ADMIN::get_ulNumeroSerieLen(ULONG* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = (ULONG)m_numSerie.size();
	return S_OK;
}

// Versión a partir de CK_TOKEN_INFO (versiones de hardware y firmware que da el driver). El publicado
// devolvía la cadena de versión del chip ("DNIe 00.20 B10 H 0000 EXP 1-1").
STDMETHODIMP CDLL_ADMIN::ObtenerVersionDNIe()
{
	m_datosOut.clear();
	CK_TOKEN_INFO info;
	CK_RV rv = m_tarjeta.InfoToken(info);
	if (rv != CKR_OK)
		return Resultado(rv);
	char v[96];
	sprintf_s(v, "hw %u.%02u fw %u.%02u", info.hardwareVersion.major, info.hardwareVersion.minor,
	          info.firmwareVersion.major, info.firmwareVersion.minor);
	m_datosOut.assign(v, v + strlen(v));
	return Resultado(CKR_OK);
}

// 0x01 = DNI (lo que devolvía el publicado con un DNIe).
STDMETHODIMP CDLL_ADMIN::ObtenerTipoDocumento()
{
	if (!m_tarjeta.Conectada())
		return Resultado(CKR_SESSION_HANDLE_INVALID);
	m_datosOut.assign(1, 0x01);
	return Resultado(CKR_OK);
}

// Versiones del ActiveX y del driver PKCS#11 que usa
STDMETHODIMP CDLL_ADMIN::ObtenerVersionesLibrerias()
{
	m_nombresLib.clear();
	m_versionesLib.clear();

	wchar_t propia[MAX_PATH];
	GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), propia, MAX_PATH);
	m_nombresLib.push_back(PathFindFileNameW(propia));
	m_versionesLib.push_back(VersionFichero(propia));

	std::wstring driver = m_tarjeta.RutaDriver();
	if (!driver.empty())
	{
		m_nombresLib.push_back(PathFindFileNameW(driver.c_str()));
		m_versionesLib.push_back(VersionFichero(driver));
	}
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::get_ArrayVersionLibreriasLenOut(int* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = (int)m_versionesLib.size();
	return S_OK;
}

STDMETHODIMP CDLL_ADMIN::get_VersionLibreriaOut(int i, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	if (i < 0 || i >= (int)m_versionesLib.size())
		return Vacio(pVal);
	*pVal = SysAllocString(m_versionesLib[i].c_str());
	return S_OK;
}

STDMETHODIMP CDLL_ADMIN::get_NombreLibreriaOut(int i, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	if (i < 0 || i >= (int)m_nombresLib.size())
		return Vacio(pVal);
	*pVal = SysAllocString(m_nombresLib[i].c_str());
	return S_OK;
}

// ---------------------------------------------------------------------------------------------
// PIN
// ---------------------------------------------------------------------------------------------

// Compatibilidad con el publicado: la página PUEDE dar el PIN en PinIn y llamar a PresentarPin.
// Lo recomendable es no hacerlo y dejar que el ActiveX lo pida con su ventana (AsegurarPin), para
// que la página nunca vea el PIN.
STDMETHODIMP CDLL_ADMIN::put_PinIn(BSTR newVal)
{
	BorrarPin();
	m_pin = BstrATexto(newVal);
	return S_OK;
}

// Si la página no ha dado el PIN en PinIn, se pide con el diálogo del ActiveX.
// No hace falta llamar a PresentarPin: las operaciones que necesitan el PIN lo piden solas.
STDMETHODIMP CDLL_ADMIN::PresentarPin()
{
	if (m_pin.empty())
		return Resultado(AsegurarPin(L"identificarse"));
	CK_RV rv = m_tarjeta.PresentarPin(m_pin);
	BorrarPin();   // el PIN no se guarda más allá de esta llamada
	return Resultado(rv);
}

// ---------------------------------------------------------------------------------------------
// Etiquetas
// ---------------------------------------------------------------------------------------------

// Lista las etiquetas de los objetos de una clase. Después la página las recorre con
// ArrayEtiquetasLenOut y EtiquetaOut(i).
// "privados" separa los objetos de datos por su atributo CKA_PRIVATE (true = solo con PIN):
// ObtenerEtiquetasDatosPublicos pide 0 y ObtenerEtiquetasDatosPrivados pide 1; el resto, -1 (todos).
HRESULT CDLL_ADMIN::Etiquetas(CK_OBJECT_CLASS clase, int privados)
{
	m_etiquetas.clear();
	std::vector<std::string> todas;
	CK_RV rv = m_tarjeta.Etiquetas(clase, todas);
	if (rv != CKR_OK)
		return Resultado(rv);
	for (const std::string& e : todas)
	{
		if (privados >= 0)
		{
			Bytes v;
			bool esPrivado = m_tarjeta.ValorPorEtiqueta(clase, e, CKA_PRIVATE, v) == CKR_OK && !v.empty() && v[0];
			if (esPrivado != (privados == 1))
				continue;
		}
		m_etiquetas.push_back(e);
	}
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::ObtenerEtiquetasTodosCertificados() { return Etiquetas(CKO_CERTIFICATE, -1); }
STDMETHODIMP CDLL_ADMIN::ObtenerEtiquetasClavesPublicas()    { return Etiquetas(CKO_PUBLIC_KEY, -1); }
STDMETHODIMP CDLL_ADMIN::ObtenerEtiquetasClavesPrivadas()
{
	CK_RV rv = AsegurarPin(L"ver sus claves");
	if (rv != CKR_OK)
		return Resultado(rv);
	return Etiquetas(CKO_PRIVATE_KEY, -1);
}
STDMETHODIMP CDLL_ADMIN::ObtenerEtiquetasDatosPublicos()     { return Etiquetas(CKO_DATA, 0); }
STDMETHODIMP CDLL_ADMIN::ObtenerEtiquetasDatosPrivados()
{
	CK_RV rv = AsegurarPin(L"ver sus datos privados");
	if (rv != CKR_OK)
		return Resultado(rv);
	return Etiquetas(CKO_DATA, 1);
}

STDMETHODIMP CDLL_ADMIN::get_ArrayEtiquetasLenOut(int* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = (int)m_etiquetas.size();
	return S_OK;
}

STDMETHODIMP CDLL_ADMIN::get_EtiquetaOut(int i, BSTR* pVal)
{
	if (i < 0 || i >= (int)m_etiquetas.size())
		return Vacio(pVal);
	return TextoABstr(m_etiquetas[i], pVal);
}

// ---------------------------------------------------------------------------------------------
// Certificados, datos y claves públicas
// ---------------------------------------------------------------------------------------------

// Las propiedades "put_" son lo que JavaScript escribe (ax.EtiquetaCertificadoIn = "..."): solo se
// guardan; se usan cuando la página llama al método correspondiente.
STDMETHODIMP CDLL_ADMIN::put_EtiquetaCertificadoIn(BSTR newVal) { m_etiquetaCert = BstrATexto(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::put_EtiquetaDatoIn(BSTR newVal)        { m_etiquetaDato = BstrATexto(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::put_EtiquetaKeyPubIn(BSTR newVal)      { m_etiquetaKeyPub = BstrATexto(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::put_EtiquetaKeyPrivIn(BSTR newVal)     { m_etiquetaKeyPriv = BstrATexto(newVal); return S_OK; }

// Sin pedir permiso, igual que el ActiveX publicado, aunque el
// certificado contiene el nombre y el número de DNI del titular.
STDMETHODIMP CDLL_ADMIN::ObtenerCertificado()
{
	m_datosOut.clear();
	return Resultado(m_tarjeta.ValorPorEtiqueta(CKO_CERTIFICATE, m_etiquetaCert, CKA_VALUE, m_datosOut));
}

STDMETHODIMP CDLL_ADMIN::ObtenerDatoPublico()
{
	m_datosOut.clear();
	return Resultado(LeerDato(m_etiquetaDato, m_datosOut));
}

STDMETHODIMP CDLL_ADMIN::ObtenerDatoPrivado()
{
	m_datosOut.clear();
	return Resultado(LeerDato(m_etiquetaDato, m_datosOut));
}

// Clave pública RSA: primero del certificado asociado (siempre legible); si no hay certificado,
// de los atributos CKA_MODULUS / CKA_PUBLIC_EXPONENT de la clave.
STDMETHODIMP CDLL_ADMIN::ObtenerClavePublicaRSA()
{
	m_modulo.clear();
	m_exponente.clear();
	Bytes cert;
	if (m_tarjeta.CertificadoDeClave(m_etiquetaKeyPub, cert) == CKR_OK)
		return Resultado(ClavePublicaDeCertificado(cert, false, m_modulo, m_exponente));
	CK_RV rv = m_tarjeta.ValorPorEtiqueta(CKO_PUBLIC_KEY, m_etiquetaKeyPub, CKA_MODULUS, m_modulo);
	if (rv == CKR_OK)
		rv = m_tarjeta.ValorPorEtiqueta(CKO_PUBLIC_KEY, m_etiquetaKeyPub, CKA_PUBLIC_EXPONENT, m_exponente);
	return Resultado(rv);
}

// DNIe 4.0: punto público (04 || X || Y) de la clave EC con etiqueta EtiquetaKeyPubIn, en DatosOut.
// Se toma del certificado asociado, que se puede leer sin PIN; si no hay certificado, del atributo
// CKA_EC_POINT de la clave. (En el publicado esta función leía otra clave del chip.)
STDMETHODIMP CDLL_ADMIN::ObtenerClavePublicaEC()
{
	m_datosOut.clear();
	Bytes cert, nada;
	if (m_tarjeta.CertificadoDeClave(m_etiquetaKeyPub, cert) == CKR_OK)
		return Resultado(ClavePublicaDeCertificado(cert, true, m_datosOut, nada));

	Bytes punto;
	CK_RV rv = m_tarjeta.ValorPorEtiqueta(CKO_PUBLIC_KEY, m_etiquetaKeyPub, CKA_EC_POINT, punto);
	if (rv != CKR_OK)
		return Resultado(rv);
	// CKA_EC_POINT viene envuelto en un OCTET STRING DER (04 len ...)
	// Es decir: 04 (etiqueta OCTET STRING), la longitud (1, 2 o 3 bytes, ver TLV arriba) y dentro el
	// punto, que a su vez empieza por 04 (punto sin comprimir). Se quita solo el envoltorio.
	if (punto.size() > 2 && punto[0] == 0x04)
	{
		size_t cab = 2, len = punto[1];
		if (punto[1] == 0x81 && punto.size() > 3) { cab = 3; len = punto[2]; }
		else if (punto[1] == 0x82 && punto.size() > 4) { cab = 4; len = (punto[2] << 8) | punto[3]; }
		if (cab + len == punto.size())
			punto.erase(punto.begin(), punto.begin() + cab);
	}
	m_datosOut = punto;
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::put_ModuloIn(BSTR newVal)    { m_modulo = BstrABytes(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::put_ExponenteIn(BSTR newVal) { m_exponente = BstrABytes(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::get_ModuloOut(BSTR* pVal)    { return BytesABstr(m_modulo, pVal); }
STDMETHODIMP CDLL_ADMIN::get_ExponenteOut(BSTR* pVal) { return BytesABstr(m_exponente, pVal); }
STDMETHODIMP CDLL_ADMIN::get_DatosOut(BSTR* pVal)     { return BytesABstr(m_datosOut, pVal); }
STDMETHODIMP CDLL_ADMIN::put_DatosIn(BSTR newVal)
{
	std::string t = BstrATexto(newVal);
	m_datosIn.assign(t.begin(), t.end());
	return S_OK;
}

// ---------------------------------------------------------------------------------------------
// Firma y verificación
//   Se firma con SHA-256 (el publicado usaba SHA-1, que ya no se admite para firma):
//     - si se ha dado hashIn (32 bytes), se firma ese resumen tal cual;
//     - si no, se firma el SHA-256 de DatosIn.
//   RSA: PKCS#1 v1.5 (DigestInfo SHA-256);  EC: ECDSA, firma en bruto r || s.
//   Otra longitud de hashIn -> CKR_DATA_LEN_RANGE. hashIn se borra tras cada Firmar/Verificar.
//   FirmaOut se devuelve en bruto (un carácter por byte), como el ActiveX publicado.
//   FirmaIn admite bruto o hexadecimal.
// ---------------------------------------------------------------------------------------------

// Qué se firma o verifica: hashIn si la página lo ha dado (por ejemplo, el SHA-256 de un fichero
// calculado en la página), o si no el SHA-256 de DatosIn. Tiene que ser de 32 bytes. hashIn se
// borra siempre, para que no se use sin querer en la operación siguiente.
HRESULT CDLL_ADMIN::ResumenAFirmar(Bytes& resumen)
{
	if (m_hashIn.empty())
		resumen = Sha256(m_datosIn);
	else
		resumen = m_hashIn;
	m_hashIn.clear();
	return resumen.size() == 32 ? S_OK : S_FALSE;
}

// Orden: primero se comprueba el resumen (sin molestar al usuario si los datos no valen), luego se
// pide el PIN si hace falta y por último firma la tarjeta.
STDMETHODIMP CDLL_ADMIN::Firmar()
{
	m_firma.clear();
	Bytes resumen;
	if (ResumenAFirmar(resumen) != S_OK)
		return Resultado(CKR_DATA_LEN_RANGE);
	std::string clave = m_etiquetaKeyPriv;   // la elegida al llamar, aunque la página la cambie mientras
	CK_RV rv = AsegurarPin(L"firmar");      // está abierta la ventana del PIN
	if (rv != CKR_OK)
		return Resultado(rv);
	return Resultado(m_tarjeta.Firmar(clave, resumen, m_firma));
}

STDMETHODIMP CDLL_ADMIN::get_FirmaOut(BSTR* pVal)
{
	return BytesABstr(m_firma, pVal);
}

STDMETHODIMP CDLL_ADMIN::put_FirmaIn(BSTR newVal)
{
	m_firma = FirmaDesdeBstr(newVal);
	return S_OK;
}

STDMETHODIMP CDLL_ADMIN::Verificar()
{
	Bytes resumen;
	if (ResumenAFirmar(resumen) != S_OK)
		return Resultado(CKR_DATA_LEN_RANGE);
	return Resultado(m_tarjeta.Verificar(m_etiquetaKeyPub, resumen, m_firma));
}

STDMETHODIMP CDLL_ADMIN::put_paddingTypeIn(BYTE newVal) { m_padding = newVal; return S_OK; }

// ---------------------------------------------------------------------------------------------
// Resumen SHA-256 (hashOut en binario, un carácter por byte)
//   Hash():  hashOut = SHA-256(DatosIn)
//   Hash_Init() / Hash_Update() (añade DatosIn) / Hash_Final()
// ---------------------------------------------------------------------------------------------

STDMETHODIMP CDLL_ADMIN::Hash()
{
	m_hash = Sha256(m_datosIn);
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::Hash_Init()
{
	m_acumuladoHash.clear();
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::Hash_Update()
{
	m_acumuladoHash.insert(m_acumuladoHash.end(), m_datosIn.begin(), m_datosIn.end());
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::Hash_Final()
{
	m_hash = Sha256(m_acumuladoHash);
	m_acumuladoHash.clear();
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::get_hashOut(BSTR* pVal) { return BytesABstr(m_hash, pVal); }
STDMETHODIMP CDLL_ADMIN::put_hashIn(BSTR newVal) { m_hashIn = BstrABytes(newVal); return S_OK; }

// ---------------------------------------------------------------------------------------------
// Propiedades sin función en esta versión (se guardan / devuelven vacías)
// ---------------------------------------------------------------------------------------------

STDMETHODIMP CDLL_ADMIN::put_CANLecturaIn(BSTR newVal) { m_can = BstrATexto(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::put_BITIn(BSTR newVal)        { m_bit = BstrABytes(newVal); return S_OK; }
STDMETHODIMP CDLL_ADMIN::get_BITOut(BSTR* pVal)        { return BytesABstr(m_bit, pVal); }
STDMETHODIMP CDLL_ADMIN::get_CertComponenteOut(BSTR* pVal) { return BytesABstr(m_certComponente, pVal); }
STDMETHODIMP CDLL_ADMIN::get_TipoBiometriaOut(BYTE* pVal)
{
	if (!pVal)
		return E_POINTER;
	*pVal = 0;   // no disponible en esta versión
	return S_OK;
}

// ---------------------------------------------------------------------------------------------
// Funciones no disponibles en esta versión: devuelven
// lastresultOut = CKR_FUNCTION_NOT_SUPPORTED (0x54) o un valor vacío.
// ---------------------------------------------------------------------------------------------

STDMETHODIMP CDLL_ADMIN::pkcs1Padding()
{
	return NoDisponible();
}

STDMETHODIMP CDLL_ADMIN::pkcs1Unpadding()
{
	return NoDisponible();
}

STDMETHODIMP CDLL_ADMIN::ObtenerInfoBiometrica()
{
	return NoDisponible();
}

// Número de soporte (IDESP) del DNIe: campo del DG13
STDMETHODIMP CDLL_ADMIN::ObtenerNumeroSerie_Soporte()
{
	m_datosOut.clear();
	std::string v;
	CK_RV rv = CampoDG13(DG13_SOPORTE, v);
	if (rv == CKR_OK)
		m_datosOut.assign(v.begin(), v.end());
	return Resultado(rv);
}

STDMETHODIMP CDLL_ADMIN::ObtenerClavePublicaRSAFirmada()
{
	return NoDisponible();
}

STDMETHODIMP CDLL_ADMIN::ObtenerCertCAComponente()
{
	return NoDisponible();
}

STDMETHODIMP CDLL_ADMIN::ObtenerDisplayMessage()
{
	return NoDisponible();
}

// El CAN no se publica como objeto PKCS#11 (el driver lo usa internamente para abrir el canal PACE)
STDMETHODIMP CDLL_ADMIN::ObtenerCAN()
{
	return NoDisponible();
}

// MRZ (DG1, etiqueta 5F1F) en DatosOut, como texto
// La MRZ (zona de lectura mecánica) son las líneas de letras y "<" de la parte de atrás del DNI. En
// el DNIe son 3 líneas de 30 caracteres (90 en total) y están en el DG1, igual que en un pasaporte.
STDMETHODIMP CDLL_ADMIN::ObtenerMRZ()
{
	m_datosOut.clear();
	Bytes dg;
	CK_RV rv = Grupo("DG1", dg);
	if (rv != CKR_OK)
		return Resultado(rv);
	size_t off, len;
	if (!BuscarTag(dg, 0, dg.size(), 0x5F1F, off, len))
		return Resultado(CKR_DATA_INVALID);
	m_datosOut.assign(dg.begin() + off, dg.begin() + off + len);
	return Resultado(CKR_OK);
}

STDMETHODIMP CDLL_ADMIN::ObtenerClavePublicaECFirmada()
{
	return NoDisponible();
}

STDMETHODIMP CDLL_ADMIN::ObtenerCertificadoCV_CA()
{
	return NoDisponible();
}

// ---------------------------------------------------------------------------------------------
// Datos personales (DG13) e imágenes (DG2, DG7). Igual que en el publicado, cada propiedad lee la
// tarjeta la primera vez; el grupo leído se guarda hasta FinalizarConexion. lastresultOut indica
// si la lectura fue bien.
// ---------------------------------------------------------------------------------------------

STDMETHODIMP CDLL_ADMIN::get_NombreOut(BSTR* pVal)               { return DevolverCampo(DG13_NOMBRE, pVal); }
STDMETHODIMP CDLL_ADMIN::get_Apellido1Out(BSTR* pVal)            { return DevolverCampo(DG13_APELLIDO1, pVal); }
STDMETHODIMP CDLL_ADMIN::get_Apellido2Out(BSTR* pVal)            { return DevolverCampo(DG13_APELLIDO2, pVal); }
STDMETHODIMP CDLL_ADMIN::get_NumeroDNIOut(BSTR* pVal)            { return DevolverCampo(DG13_NUMERO_DNI, pVal); }
STDMETHODIMP CDLL_ADMIN::get_FechaNacimientoOut(BSTR* pVal)      { return DevolverCampo(DG13_FECHA_NACIMIENTO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_NacionalidadOut(BSTR* pVal)         { return DevolverCampo(DG13_NACIONALIDAD, pVal); }
STDMETHODIMP CDLL_ADMIN::get_FechaExpiracionOut(BSTR* pVal)      { return DevolverCampo(DG13_FECHA_EXPIRACION, pVal); }
STDMETHODIMP CDLL_ADMIN::get_SexoOut(BSTR* pVal)                 { return DevolverCampo(DG13_SEXO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_LocalidadNacimientoOut(BSTR* pVal)  { return DevolverCampo(DG13_LOCALIDAD_NACIMIENTO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_ProvinciaNacimientoOut(BSTR* pVal)  { return DevolverCampo(DG13_PROVINCIA_NACIMIENTO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_NombrePadresOut(BSTR* pVal)         { return DevolverCampo(DG13_PADRES, pVal); }
STDMETHODIMP CDLL_ADMIN::get_DomicilioOut(BSTR* pVal)            { return DevolverCampo(DG13_DOMICILIO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_LocalidadDomicilioOut(BSTR* pVal)   { return DevolverCampo(DG13_LOCALIDAD_DOMICILIO, pVal); }
STDMETHODIMP CDLL_ADMIN::get_ProvinciaDomicilioOut(BSTR* pVal)   { return DevolverCampo(DG13_PROVINCIA_DOMICILIO, pVal); }

// Foto: DG2 / 5F2E (cabecera ISO 19794-5 + imagen JPEG 2000). Se devuelve solo la imagen.
// El DG2 sigue el estándar de biometría: dentro del bloque 5F2E hay una cabecera con datos de la
// imagen (tamaño, tipo...) y después la imagen. InicioImagen la localiza por su "firma" de formato.
STDMETHODIMP CDLL_ADMIN::get_ImagenFacialOut(BSTR* pVal) { return DevolverImagen("DG2", 0x5F2E, pVal); }
// Firma manuscrita: DG7 / 5F43
STDMETHODIMP CDLL_ADMIN::get_ImagenFirmaOut(BSTR* pVal)  { return DevolverImagen("DG7", 0x5F43, pVal); }

// ---------------------------------------------------------------------------------------------
// Lectura de grupos de datos
// ---------------------------------------------------------------------------------------------

// Los grupos de datos son datos personales: al olvidarlos se sobrescriben con ceros antes de
// liberar la memoria, igual que el PIN.
void CDLL_ADMIN::OlvidarGrupos()
{
	m_datosLeidos = 0;
	for (auto& g : m_grupos)
		if (!g.second.empty())
			SecureZeroMemory(g.second.data(), g.second.size());
	m_grupos.clear();
}

// Lectura de un objeto de datos, de forma que cada lectura empiece con el canal seguro recién abierto:
//  - si ya se ha leído un objeto de datos en esta conexión y no hay PIN presentado, se reinicia la
//    tarjeta en el lector y se vuelve a conectar ANTES de leer el siguiente. Con PIN presentado no
//    se reinicia por adelantado, para no perderlo;
//  - si aun así la lectura falla, se reinicia y se repite una vez (el PIN se pierde).
// En el DNIe 3.0 los grupos de datos solo se pueden leer con el PIN presentado; ese caso no se
// reintenta.
//
// Pasos de LeerDato:
//   1. ¿Sigue la misma tarjeta? (RevisarTarjeta). Si ha cambiado, se avisa y no se lee nada.
//   2. DNIe 3.0 sin PIN: se pide el PIN (cuenta como permiso del usuario).
//   3. Permiso del usuario para leer sus datos (una vez por conexión).
//   4. Si ya se leyó otro dato sin PIN, reinicio previo de la tarjeta (canal seguro recién abierto).
//   5. Lectura del objeto de datos (CKO_DATA con esa etiqueta, atributo CKA_VALUE).
//   6. Si falla por algo que no sea "no existe", "no hay tarjeta", "tarjeta cambiada" o "falta el
//      PIN", reinicio y un segundo intento.
CK_RV CDLL_ADMIN::LeerDato(const std::string& etiqueta, Bytes& valor)
{
	valor.clear();
	CK_RV rt = RevisarTarjeta();
	if (rt != CKR_OK)
		return rt;
	if (!m_tarjeta.Conectada())
		return CKR_SESSION_HANDLE_INVALID;

	// DNIe 3.0 (claves RSA) sin PIN: la tarjeta no deja leer los grupos de datos. Se pide el
	// PIN con el diálogo del ActiveX, explicando para qué; ese PIN vale como permiso del usuario.
	if (!m_tarjeta.PinPresentado() && EsDNIeRSA())
	{
		CK_RV rp = AsegurarPin(L"leer sus datos");
		if (rp != CKR_OK)
			return rp;
		m_permisoDatos = 1;
	}
	// DNIe 4.0 (o DNIe 3.0 con el PIN ya presentado para otra cosa): se pide permiso al usuario
	// antes de entregar sus datos personales a la página.
	CK_RV rpd = PermisoDatos();
	if (rpd != CKR_OK)
		return rpd;

	CK_RV rv;
	if (m_datosLeidos > 0 && !m_tarjeta.PinPresentado())
	{
		rv = m_tarjeta.ReiniciarTarjeta();
		m_datosLeidos = 0;
		if (rv != CKR_OK)
			return rv;
	}
	rv = m_tarjeta.ValorPorEtiqueta(CKO_DATA, etiqueta, CKA_VALUE, valor);
	if (rv != CKR_OK && rv != CKR_KEY_HANDLE_INVALID && rv != CKR_TOKEN_NOT_PRESENT && rv != CKR_DEVICE_REMOVED &&
	    rv != CKR_USER_NOT_LOGGED_IN)
	{
		valor.clear();
		rv = m_tarjeta.ReiniciarTarjeta();
		if (rv != CKR_OK)
			return rv;
		rv = m_tarjeta.ValorPorEtiqueta(CKO_DATA, etiqueta, CKA_VALUE, valor);
	}
	if (rv == CKR_OK)
		m_datosLeidos++;
	return rv;
}

// true si el certificado de autenticación es RSA (DNIe 3.0); el DNIe 4.0 es de curva elíptica
// Es la forma más sencilla de distinguirlos con lo que publica el driver: el certificado se puede
// leer sin PIN en los dos, y ClavePublicaDeCertificado(..., ec = false) solo funciona si es RSA.
bool CDLL_ADMIN::EsDNIeRSA()
{
	Bytes cert, a, b;
	if (m_tarjeta.ValorPorEtiqueta(CKO_CERTIFICATE, "CertAutenticacion", CKA_VALUE, cert) != CKR_OK)
		return false;
	return ClavePublicaDeCertificado(cert, false, a, b) == CKR_OK;
}

// Caché de grupos: cada dato personal (nombre, apellidos, DNI...) es una propiedad distinta, pero
// todos están en el DG13. Sin caché, leer el nombre y los dos apellidos leería el DG13 tres veces
// (con su permiso, reinicios, etc.). Se guarda hasta que se desconecta o cambia la tarjeta.
CK_RV CDLL_ADMIN::Grupo(const char* dg, Bytes& valor)
{
	valor.clear();
	CK_RV rt = RevisarTarjeta();
	if (rt != CKR_OK)
		return rt;
	auto it = m_grupos.find(dg);
	if (it != m_grupos.end())
	{
		valor = it->second;
		return CKR_OK;
	}
	CK_RV rv = LeerDato(dg, valor);
	if (rv == CKR_OK)
		m_grupos[dg] = valor;
	return rv;
}

// DG13 (propio del DNIe): 6D { 30 { 16 cadenas de texto sin etiquetar } }
// Se busca la secuencia (30) y se recorren sus elementos contando hasta llegar al pedido (ver la
// lista DG13_xxx al principio del fichero).
CK_RV CDLL_ADMIN::CampoDG13(int indice, std::string& valor)
{
	valor.clear();
	Bytes dg;
	CK_RV rv = Grupo("DG13", dg);
	if (rv != CKR_OK)
		return rv;
	size_t off, len;
	if (!BuscarTag(dg, 0, dg.size(), 0x30, off, len))
		return CKR_DATA_INVALID;
	size_t p = off, fin = off + len;
	for (int i = 0; p < fin; i++)
	{
		unsigned tag;
		size_t v, l;
		bool construido;
		if (!LeerTLV(dg, p, fin, tag, v, l, construido))
			return CKR_DATA_INVALID;
		if (i == indice)
		{
			valor.assign((const char*)dg.data() + v, l);
			return CKR_OK;
		}
		p = v + l;
	}
	return CKR_DATA_INVALID;
}

HRESULT CDLL_ADMIN::DevolverCampo(int indice, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	std::string v;
	m_rv = (int)CampoDG13(indice, v);
	if (m_rv != CKR_OK)
		return Vacio(pVal);
	// Los campos son UTF8String o PrintableString (ASCII)
	int n = MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), NULL, 0);
	*pVal = SysAllocStringLen(NULL, n);
	if (!*pVal && n)
		return E_OUTOFMEMORY;
	MultiByteToWideChar(CP_UTF8, 0, v.data(), (int)v.size(), *pVal, n);
	return S_OK;
}

// Busca el inicio de la imagen dentro del contenedor: JPEG 2000 (fichero JP2 o flujo J2K) o JPEG.
// Si no se reconoce, se devuelve el contenido completo.
// Cada formato de imagen empieza por unos bytes fijos ("firma" o "número mágico"):
//   JP2 (fichero JPEG 2000): 00 00 00 0C 6A 50 20 20 0D 0A 87 0A   ("jP  " + control)
//   J2K (flujo JPEG 2000):   FF 4F FF 51
//   JPEG:                    FF D8 FF
// La página usa estos mismos bytes para saber cómo descodificar la imagen (openjpeg.js).
static size_t InicioImagen(const Bytes& b, size_t off, size_t len)
{
	static const unsigned char jp2[] = { 0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A };
	static const unsigned char j2k[] = { 0xFF, 0x4F, 0xFF, 0x51 };
	static const unsigned char jpg[] = { 0xFF, 0xD8, 0xFF };
	struct { const unsigned char* f; size_t n; } firmas[] = { { jp2, sizeof(jp2) }, { j2k, sizeof(j2k) }, { jpg, sizeof(jpg) } };
	for (auto& f : firmas)
		for (size_t i = off; i + f.n <= off + len; i++)
			if (memcmp(&b[i], f.f, f.n) == 0)
				return i;
	return off;
}

HRESULT CDLL_ADMIN::DevolverImagen(const char* dg, unsigned tag, BSTR* pVal)
{
	if (!pVal)
		return E_POINTER;
	Bytes g;
	m_rv = (int)Grupo(dg, g);
	if (m_rv != CKR_OK)
		return Vacio(pVal);
	size_t off, len;
	if (!BuscarTag(g, 0, g.size(), tag, off, len))
	{
		m_rv = CKR_DATA_INVALID;
		return Vacio(pVal);
	}
	size_t ini = InicioImagen(g, off, len);
	return BytesABstr(Bytes(g.begin() + ini, g.begin() + off + len), pVal);
}

// ---------------------------------------------------------------------------------------------
// PIN y permiso del usuario
// ---------------------------------------------------------------------------------------------

// Pide el PIN con el diálogo del ActiveX (PinDialog.cpp) si
// todavía no se ha presentado. Cancelar devuelve CKR_FUNCTION_CANCELED; un PIN erróneo, el error de
// la tarjeta (CKR_PIN_INCORRECT...). No se repite automáticamente para no gastar intentos del PIN.
//
// Por qué lo pide el ActiveX y no la página: el control está marcado como seguro para scripting, así
// que cualquier página podría usarlo. Con la ventana propia, el PIN lo escribe el usuario en una
// ventana de Windows que la página no puede ver, rellenar ni falsear.
//
// Pasos:
//   1. ¿Sigue la misma tarjeta? ¿Ya está presentado el PIN? Entonces no hay nada que hacer.
//   2. Longitud admitida: la del driver (C_GetTokenInfo), ajustada a la del DNIe (3.0: 8-16; 4.0: 12-16).
//   3. Ventana del PIN, con los estilos visuales de Windows activados mientras está abierta.
//   4. El PIN se convierte al formato que espera el driver y se presenta (C_Login).
//   5. Todas las copias del PIN se borran de memoria con SecureZeroMemory.
CK_RV CDLL_ADMIN::AsegurarPin(const wchar_t* para)
{
	CK_RV rt = RevisarTarjeta();
	if (rt != CKR_OK)
		return rt;
	if (!m_tarjeta.Conectada())
		return CKR_SESSION_HANDLE_INVALID;
	if (m_tarjeta.PinPresentado())
		return CKR_OK;
	// Longitud del PIN: la que anuncia el driver (C_GetTokenInfo), ajustada a la del DNIe:
	//   DNIe 4.0: de 12 a 16 caracteres.
	//   DNIe 3.0: de 8 a 16. La tarjeta admite desde 4, pero la certificación exige un mínimo de 8
	//             (se recomiendan 12). Un mínimo mayor de 8 dejaría fuera PIN válidos.
	size_t minimo = PIN_MIN_LENGTH, maximo = PIN_MAX_LENGTH;
	CK_TOKEN_INFO ti;
	if (m_tarjeta.InfoToken(ti) == CKR_OK && ti.ulMinPinLen > 0 && ti.ulMaxPinLen >= ti.ulMinPinLen)
	{
		minimo = ti.ulMinPinLen;
		maximo = ti.ulMaxPinLen;
	}
	const size_t minimoDNIe = EsDNIeRSA() ? PIN_MIN_DNIE3 : PIN_MIN_DNIE4;
	if (minimo < minimoDNIe)
		minimo = minimoDNIe;
	if (maximo > PIN_MAX_DNIE)
		maximo = PIN_MAX_DNIE;
	if (maximo < minimo)
		maximo = minimo;
	std::wstring pinW;
	bool aceptado;
	{
		VentanaAbierta abierta(m_tarjeta);
		EstilosVisuales estilos;
		aceptado = AskForPin(VentanaPagina(), para, minimo, maximo, pinW);
	}
	if (!aceptado)
	{
		if (!pinW.empty())
			SecureZeroMemory(&pinW[0], pinW.size() * sizeof(wchar_t));
		return CKR_FUNCTION_CANCELED;
	}
	// El driver recibe el PIN como texto en la página de códigos del sistema, igual que PinIn
	std::string pin;
	pin.reserve(maximo * 2 + 1);
	int n = WideCharToMultiByte(CP_ACP, 0, pinW.c_str(), (int)pinW.size(), NULL, 0, NULL, NULL);
	pin.resize(n > 0 ? n : 0);
	if (n > 0)
		WideCharToMultiByte(CP_ACP, 0, pinW.c_str(), (int)pinW.size(), &pin[0], n, NULL, NULL);
	if (!pinW.empty())
		SecureZeroMemory(&pinW[0], pinW.size() * sizeof(wchar_t));
	CK_RV rv = m_tarjeta.PresentarPin(pin);
	if (!pin.empty())
		SecureZeroMemory(&pin[0], pin.size());
	return rv;
}

// Permiso para entregar los datos personales a la página. Se pregunta una vez por conexión
// (InicializarConexion vuelve a preguntar). Si el usuario lo deniega: CKR_FUNCTION_REJECTED.
//
// Por qué: el driver deja leer los datos del DNIe 4.0 sin PIN. Sin esta pregunta, cualquier página
// abierta en modo Internet Explorer podría leer nombre, domicilio, foto... sin que el titular se
// enterase. m_permisoDatos: 0 = no se ha preguntado, 1 = permitido, -1 = denegado.
CK_RV CDLL_ADMIN::PermisoDatos()
{
	if (m_permisoDatos == 0)
	{
		VentanaAbierta abierta(m_tarjeta);
		EstilosVisuales estilos;
		m_permisoDatos = PedirPermisoDatos(VentanaPagina(), OrigenPagina()) ? 1 : -1;
	}
	return m_permisoDatos == 1 ? CKR_OK : CKR_FUNCTION_REJECTED;
}

// Dirección de la página que usa el control: Internet Explorer / Edge en modo IE le pasa su
// "sitio" (IObjectWithSite). Si no hay sitio (por ejemplo, desde PowerShell), vacío.
//
// El "sitio" es el objeto del navegador que contiene al control. Se le pregunta de dos maneras:
//   1. Como servicio del navegador (IServiceProvider -> IWebBrowser2): la dirección de la ventana.
//   2. Si no, por el documento HTML que contiene al control (IOleClientSite -> IHTMLDocument2).
// En una HTA con marco (iframe), la segunda da la dirección de la página del marco.
std::wstring CDLL_ADMIN::OrigenPagina()
{
	CComBSTR url;
	if (m_spUnkSite)
	{
		CComQIPtr<IServiceProvider> sp(m_spUnkSite);
		CComPtr<IWebBrowser2> nav;
		if (sp && SUCCEEDED(sp->QueryService(SID_SWebBrowserApp, IID_IWebBrowser2, (void**)&nav)) && nav)
			nav->get_LocationURL(&url);
		if (!url || !url.Length())
		{
			CComQIPtr<IOleClientSite> cs(m_spUnkSite);
			CComPtr<IOleContainer> cont;
			if (cs && SUCCEEDED(cs->GetContainer(&cont)) && cont)
			{
				CComQIPtr<IHTMLDocument2> doc(cont);
				if (doc)
				{
					url.Empty();
					doc->get_URL(&url);
				}
			}
		}
	}
	std::wstring r = (url && url.Length()) ? std::wstring(url, url.Length()) : std::wstring();
	return OrigenLegible(r);
}

// Lo que importa al usuario de la dirección, en corto (la ruta completa no cabe y oculta el sitio):
//   https://sede.ejemplo.es/a/b?c   -> "https://sede.ejemplo.es"
//   file:///C:/.../EjemploDNIe.html -> "la página EjemploDNIe.html de este equipo"
//   otras                           -> los primeros 80 caracteres
std::wstring CDLL_ADMIN::OrigenLegible(const std::wstring& url)
{
	if (url.empty())
		return url;
	if (_wcsnicmp(url.c_str(), L"file:", 5) == 0)
	{
		wchar_t ruta[MAX_PATH * 2] = {};
		DWORD n = ARRAYSIZE(ruta);
		std::wstring nombre;
		if (SUCCEEDED(PathCreateFromUrlW(url.c_str(), ruta, &n, 0)))
			nombre = PathFindFileNameW(ruta);
		return nombre.empty() ? std::wstring(L"una página de este equipo")
		                      : L"la página " + nombre + L" de este equipo";
	}
	if (_wcsnicmp(url.c_str(), L"http:", 5) == 0 || _wcsnicmp(url.c_str(), L"https:", 6) == 0)
	{
		wchar_t sitio[512] = {};
		DWORD n = ARRAYSIZE(sitio);
		if (SUCCEEDED(UrlGetPartW(url.c_str(), sitio, &n, URL_PART_HOSTNAME, URL_PARTFLAG_KEEPSCHEME)))
		{
			std::wstring r = sitio;
			wchar_t puerto[16] = {};
			n = ARRAYSIZE(puerto);
			if (SUCCEEDED(UrlGetPartW(url.c_str(), puerto, &n, URL_PART_PORT, 0)) && puerto[0])
				r += std::wstring(L":") + puerto;
			return r;
		}
	}
	return url.size() > 80 ? url.substr(0, 80) + L"…" : url;
}

// Ventana del navegador, para que las ventanas del PIN y del permiso salgan delante de ella y la
// bloqueen mientras están abiertas (ventana "propietaria"). Si no hay sitio, la ventana activa.
HWND CDLL_ADMIN::VentanaPagina()
{
	if (m_spUnkSite)
	{
		CComQIPtr<IOleWindow> ow(m_spUnkSite);
		HWND h = NULL;
		if (ow && SUCCEEDED(ow->GetWindow(&h)) && h)
			return GetAncestor(h, GA_ROOT);
	}
	return GetForegroundWindow();
}
