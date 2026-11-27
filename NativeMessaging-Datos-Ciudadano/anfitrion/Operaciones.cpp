// =================================================================================================
//  Operaciones.cpp  -  Las operaciones del anfitrión
// =================================================================================================
//
//  PETICIÓN
//  --------
//  La construye siempre la extensión (fondo.js), que ya ha comprobado el origen de la página y el
//  formato de cada parámetro:
//
//      {"operacion":"leer","origen":"https://sede.ejemplo.es","que":["datos","imagenes"]}
//
//  "origen" no da permisos (eso lo decide la extensión): solo se muestra al usuario en las ventanas
//  de permiso y del PIN, para que sepa qué página se lo pide.
//
//  OPERACIONES                                   VENTANAS QUE ABRE
//  -----------                                   -----------------
//    version        Qué DNIe hay en el lector    ninguna
//    informacion    Chip y programas             ninguna
//    leer           Datos personales, MRZ,       permiso (y el PIN con el DNIe 3.0)
//                   imágenes y/o certificados
//    claves         Claves públicas              ninguna
//    firmar         Firma un texto o un resumen  confirmación y PIN
//    firmarArchivo  Firma un archivo del equipo  "Abrir" y PIN
//    verificar      Comprueba una firma          ninguna
//    hash           SHA-256 de un texto          ninguna (no usa la tarjeta)
//
//  Los parámetros y lo que devuelve cada una están en su función, más abajo.
//
//  RESPUESTA
//  ---------
//    {"correcto":true, ...datos de la operación...}
//    {"correcto":false,"codigo":"sin_tarjeta","error":"No hay ningún DNIe en el lector.",
//     "pkcs11":"0x000000E0"}
//
//  "codigo" permite a la página distinguir los casos sin interpretar el texto (lista en
//  CodigoDeError). "pkcs11" es el código del driver, cuando el fallo viene de él.
//
//  UNA PETICIÓN, UN PROCESO
//  ------------------------
//  La extensión arranca el anfitrión para cada petición y lo cierra al recibir la respuesta. Por
//  eso aquí no se guarda nada entre peticiones: ni datos leídos, ni permisos, ni el PIN. Cada
//  operación conecta con la tarjeta, hace su trabajo y desconecta (al destruirse CTarjeta).
// =================================================================================================
#include "stdafx.h"
#include "Operaciones.h"
#include "Tarjeta.h"
#include "GruposDatos.h"
#include "Dialogos.h"
#include "PinDialog.h"
#include "Modulo.h"

#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

// Tamaño máximo del texto que se puede firmar o resumir (en bytes UTF-8). Para algo más grande, la
// página calcula el SHA-256 y pide firmar el resumen ("hash"), o se usa firmarArchivo.
static const size_t TEXTO_MAXIMO = 64 * 1024;

// =================================================================================================
//  1. Respuestas de error
// =================================================================================================

std::string RespuestaDeError(const char* codigo, const std::wstring& explicacion)
{
	ObjetoJson r;
	r.Bool("correcto", false).Texto("codigo", std::string(codigo)).Texto("error", explicacion);
	return r.Json();
}

static std::string Hex32(CK_RV rv)
{
	char t[16];
	sprintf_s(t, "0x%08lX", (unsigned long)rv);
	return t;
}

// Código y explicación para un error PKCS#11 del driver. Los códigos que ve la página:
//   cancelado        el usuario canceló o no dio permiso (no es un fallo)
//   sin_tarjeta      no hay DNIe en el lector
//   sin_driver       no está instalado el driver del DNIe
//   pin_incorrecto   PIN incorrecto (el DNIe descuenta un intento)
//   pin_bloqueado    PIN bloqueado
//   error            cualquier otro
// (Los códigos ocupado, sin_extension, sin_anfitrion y origen_no_permitido los pone la extensión
// o la biblioteca de la página, no el anfitrión.)
static std::string ErrorPkcs11(CK_RV rv)
{
	const char* codigo = "error";
	std::wstring texto;
	switch (rv)
	{
	case CKR_FUNCTION_CANCELED:
		codigo = "cancelado"; texto = L"Operación cancelada por el usuario."; break;
	case CKR_FUNCTION_REJECTED:
		codigo = "cancelado"; texto = L"El usuario no ha dado permiso."; break;
	case CKR_TOKEN_NOT_PRESENT:
	case CKR_SESSION_HANDLE_INVALID:
		codigo = "sin_tarjeta"; texto = L"No hay ningún DNIe en el lector."; break;
	case CKR_TOKEN_NOT_RECOGNIZED:
		codigo = "sin_tarjeta"; texto = L"La tarjeta del lector no es un DNIe o no se reconoce."; break;
	case CKR_LIBRARY_LOAD_FAILED:
		codigo = "sin_driver"; texto = L"No está instalado el driver del DNIe en este equipo."; break;
	case CKR_PIN_INCORRECT:
		codigo = "pin_incorrecto"; texto = L"El PIN no es correcto."; break;
	case CKR_PIN_LOCKED:
		codigo = "pin_bloqueado"; texto = L"El PIN del DNIe está bloqueado. Puede desbloquearlo en un Punto de Actualización del DNIe."; break;
	case CKR_DEVICE_REMOVED:
	case CKR_DEVICE_ERROR:
		texto = L"Se ha retirado el DNIe o ha fallado la comunicación con él. Vuelva a intentarlo."; break;
	case CKR_KEY_HANDLE_INVALID:
		texto = L"El DNIe no tiene ese dato o esa clave."; break;
	case CKR_DATA_INVALID:
		texto = L"Los datos del DNIe no tienen el formato esperado."; break;
	case CKR_DATA_LEN_RANGE:
		texto = L"El resumen que se quiere firmar no es de 32 bytes (SHA-256)."; break;
	default:
		texto = L"Error del DNIe."; break;
	}
	ObjetoJson r;
	r.Bool("correcto", false).Texto("codigo", std::string(codigo)).Texto("error", texto).Texto("pkcs11", Hex32(rv));
	return r.Json();
}

// =================================================================================================
//  2. Contexto de una petición
// =================================================================================================
//  Lo que comparten los pasos de una operación: la conexión con la tarjeta, la página que la pide
//  y la ventana dueña de los diálogos (se crea solo si hace falta alguno).
namespace
{
	struct Contexto
	{
		CTarjeta     tarjeta;
		std::wstring origen;
		HWND         duena = NULL;
		int          datosLeidos = 0;   // objetos de datos leídos sin PIN (ver LeerDato)

		HWND Duena()
		{
			if (!duena)
				duena = CrearVentanaDuena();
			return duena;
		}
		~Contexto()
		{
			if (duena)
				DestroyWindow(duena);
		}
	};
}

// Número de serie del chip (CK_TOKEN_INFO.serialNumber, sin el relleno de espacios).
static std::string Serie(Contexto& c)
{
	CK_TOKEN_INFO info;
	if (c.tarjeta.InfoToken(info) != CKR_OK)
		return std::string();
	std::string s((char*)info.serialNumber, sizeof(info.serialNumber));
	s.erase(s.find_last_not_of(' ') + 1);
	return TextoSeguro(s);
}

// true si el certificado de autenticación es RSA (DNIe 3.0); el DNIe 4.0 es de curva elíptica.
// Es la forma más sencilla de distinguirlos con lo que publica el driver: el certificado se puede
// leer sin PIN en los dos, y ClavePublicaDeCertificado(..., ec = false) solo funciona si es RSA.
static bool EsDNIeRSA(Contexto& c)
{
	Bytes cert, a, b;
	if (c.tarjeta.ValorPorEtiqueta(CKO_CERTIFICATE, "CertAutenticacion", CKA_VALUE, cert) != CKR_OK)
		return false;
	return ClavePublicaDeCertificado(cert, false, a, b) == CKR_OK;
}

// Versión de fichero de un módulo ("a.b.c.d"): la que se ve en Propiedades > Detalles.
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

// =================================================================================================
//  3. PIN y permiso del usuario
// =================================================================================================

// Pide el PIN con la ventana del anfitrión (PinDialog.cpp) y lo presenta a la tarjeta. Cancelar
// devuelve CKR_FUNCTION_CANCELED; un PIN erróneo, el error de la tarjeta (CKR_PIN_INCORRECT...). No
// se repite automáticamente, para no gastar intentos del PIN.
//
// Por qué lo pide el anfitrión y no la página: con la ventana propia, el PIN lo escribe el usuario
// en una ventana de Windows que la página no puede ver, rellenar ni falsear. La página nunca
// recibe el PIN.
//
// Pasos:
//   1. Longitud admitida: la del driver (C_GetTokenInfo), ajustada a la del DNIe (3.0: 8-16; 4.0: 12-16).
//   2. Ventana del PIN, con la página que lo solicita.
//   3. El PIN se convierte al formato que espera el driver y se presenta (C_Login).
//   4. Todas las copias del PIN se borran de memoria con SecureZeroMemory (y no memset: el
//      compilador puede quitar un memset sobre memoria que ya no se va a usar).
static CK_RV AsegurarPin(Contexto& c, const std::wstring& para)
{
	if (!c.tarjeta.Conectada())
		return CKR_SESSION_HANDLE_INVALID;
	if (c.tarjeta.PinPresentado())
		return CKR_OK;

	size_t minimo = PIN_MIN_LENGTH, maximo = PIN_MAX_LENGTH;
	CK_TOKEN_INFO ti;
	if (c.tarjeta.InfoToken(ti) == CKR_OK && ti.ulMinPinLen > 0 && ti.ulMaxPinLen >= ti.ulMinPinLen)
	{
		minimo = ti.ulMinPinLen;
		maximo = ti.ulMaxPinLen;
	}
	const size_t minimoDNIe = EsDNIeRSA(c) ? PIN_MIN_DNIE3 : PIN_MIN_DNIE4;
	if (minimo < minimoDNIe)
		minimo = minimoDNIe;
	if (maximo > PIN_MAX_DNIE)
		maximo = PIN_MAX_DNIE;
	if (maximo < minimo)
		maximo = minimo;

	std::wstring pinW;
	if (!AskForPin(c.Duena(), para, c.origen, minimo, maximo, pinW))
	{
		if (!pinW.empty())
			SecureZeroMemory(&pinW[0], pinW.size() * sizeof(wchar_t));
		return CKR_FUNCTION_CANCELED;
	}
	// El driver recibe el PIN como texto en la página de códigos del sistema.
	std::string pin;
	pin.reserve(maximo * 2 + 1);
	int n = WideCharToMultiByte(CP_ACP, 0, pinW.c_str(), (int)pinW.size(), NULL, 0, NULL, NULL);
	pin.resize(n > 0 ? n : 0);
	if (n > 0)
		WideCharToMultiByte(CP_ACP, 0, pinW.c_str(), (int)pinW.size(), &pin[0], n, NULL, NULL);
	if (!pinW.empty())
		SecureZeroMemory(&pinW[0], pinW.size() * sizeof(wchar_t));
	CK_RV rv = c.tarjeta.PresentarPin(pin);
	if (!pin.empty())
		SecureZeroMemory(&pin[0], pin.size());
	return rv;
}

// Permiso para entregar a la página los datos que pide. Si el usuario no lo da:
// CKR_FUNCTION_REJECTED.
//
// Por qué: el driver deja leer los datos del DNIe 4.0 sin PIN. Sin esta pregunta, cualquier página
// autorizada en la extensión podría leer nombre, domicilio, foto... sin que el titular se
// enterase. La ventana dice exactamente qué se va a leer y qué página lo pide.
static CK_RV PermisoLectura(Contexto& c, bool datos, bool mrz, bool imagenes, bool certificados)
{
	std::wstring lista;
	if (datos)
		lista += L"• Datos personales: nombre y apellidos, número de DNI, sexo, nacionalidad, fecha y lugar "
		         L"de nacimiento, domicilio, nombre de los padres y fechas del documento.\n";
	if (mrz)
		lista += L"• Las líneas de lectura mecánica (MRZ) de la parte de atrás del documento.\n";
	if (imagenes)
		lista += L"• Su fotografía y la imagen de su firma manuscrita.\n";
	if (certificados)
		lista += L"• Sus certificados, que incluyen su nombre y su número de DNI.\n";
	std::wstring contenido = L"Se van a leer de su DNIe y entregar a la página:\n" + lista;
	contenido.pop_back();
	return Confirmar(c.Duena(), L"¿Permite leer sus datos del DNIe?", contenido, c.origen,
	                 L"Permitir", L"No permitir") ? CKR_OK : CKR_FUNCTION_REJECTED;
}

// =================================================================================================
//  4. Lectura de los objetos de datos (grupos DG1, DG2, DG7, DG13)
// =================================================================================================
// Cada lectura empieza con el canal seguro recién abierto:
//  - si ya se ha leído un objeto de datos en esta petición y no hay PIN presentado, se reinicia la
//    tarjeta en el lector y se vuelve a conectar ANTES de leer el siguiente. Con PIN presentado no
//    se reinicia por adelantado, para no perderlo;
//  - si aun así la lectura falla, se reinicia y se repite una vez (el PIN se pierde).
// En el DNIe 3.0 los grupos de datos solo se pueden leer con el PIN presentado; ese caso no se
// reintenta.
static CK_RV LeerDato(Contexto& c, const char* etiqueta, Bytes& valor)
{
	valor.clear();
	CK_RV rv;
	if (c.datosLeidos > 0 && !c.tarjeta.PinPresentado())
	{
		rv = c.tarjeta.ReiniciarTarjeta();
		c.datosLeidos = 0;
		if (rv != CKR_OK)
			return rv;
	}
	rv = c.tarjeta.ValorPorEtiqueta(CKO_DATA, etiqueta, CKA_VALUE, valor);
	if (rv != CKR_OK && rv != CKR_KEY_HANDLE_INVALID && rv != CKR_TOKEN_NOT_PRESENT && rv != CKR_DEVICE_REMOVED &&
	    rv != CKR_USER_NOT_LOGGED_IN)
	{
		valor.clear();
		rv = c.tarjeta.ReiniciarTarjeta();
		if (rv != CKR_OK)
			return rv;
		rv = c.tarjeta.ValorPorEtiqueta(CKO_DATA, etiqueta, CKA_VALUE, valor);
	}
	if (rv == CKR_OK)
		c.datosLeidos++;
	return rv;
}

// Los grupos de datos son datos personales: se borran de memoria al terminar con ellos.
static void Borrar(Bytes& b)
{
	if (!b.empty())
		SecureZeroMemory(b.data(), b.size());
	b.clear();
}

// =================================================================================================
//  5. Parámetros
// =================================================================================================

// "uso" -> parte central de las etiquetas del DNIe: CertFirmaDigital / KprivFirmaDigital(EC),
// CertAutenticacion / KprivAutenticacion(EC). Sin "uso": el certificado de firma.
static bool Nucleo(const ValorJson& p, std::string& nucleo)
{
	std::string uso = p.Texto("uso");
	if (uso.empty() || uso == "firma")
		nucleo = "FirmaDigital";
	else if (uso == "autenticacion")
		nucleo = "Autenticacion";
	else
		return false;
	return true;
}

// Lo que se firma o verifica: el SHA-256 de "texto" o el resumen "hash" (Base64, 32 bytes).
static bool Resumen(const ValorJson& p, Bytes& resumen, std::string& error)
{
	if (p.TieneTexto("texto"))
	{
		const std::string t = p.Texto("texto");
		if (t.size() > TEXTO_MAXIMO)
		{
			error = "El texto supera los 64 KB. Firme su resumen SHA-256 (\"hash\") o use firmarArchivo.";
			return false;
		}
		resumen = Sha256(Bytes(t.begin(), t.end()));
		return true;
	}
	if (p.TieneTexto("hash"))
	{
		if (!DeBase64(p.Texto("hash"), resumen) || resumen.size() != 32)
		{
			error = "El resumen (\"hash\") tiene que ser un SHA-256 (32 bytes) en Base64.";
			return false;
		}
		return true;
	}
	error = "Falta el texto (\"texto\") o el resumen (\"hash\").";
	return false;
}

static std::string ErrorParametros(const std::string& explicacion)
{
	return RespuestaDeError("error", DeUtf8(explicacion));
}

// Primeros caracteres de un texto, para enseñarlo en una ventana.
static std::wstring Extracto(const std::wstring& t, size_t maximo)
{
	if (t.size() <= maximo)
		return t;
	return t.substr(0, maximo) + L"…";
}

// El texto que se va a firmar, tal como se enseña al usuario antes de pedirle el PIN:
//  - Los caracteres de control de dirección del texto (U+202A a U+202E y U+2066 a U+2069) se
//    sustituyen por U+FFFD. Con ellos, una página podría hacer que el texto se viera en otro
//    orden del que realmente se firma.
//  - Si es largo, se enseña el principio y se avisa de que hay más: el usuario firma el texto
//    entero, no solo lo que ve.
static std::wstring TextoParaMostrar(const std::wstring& t)
{
	const size_t MAXIMO = 600;
	std::wstring r = Extracto(t, MAXIMO);
	for (wchar_t& ch : r)
		if ((ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2066 && ch <= 0x2069))
			ch = 0xFFFD;
	if (t.size() > MAXIMO)
		r += L"\n\n(Se muestran los primeros " + std::to_wstring(MAXIMO) + L" caracteres de " +
		     std::to_wstring(t.size()) + L". Se firmará el texto completo.)";
	return r;
}

static std::wstring Hex(const Bytes& b)
{
	static const wchar_t* d = L"0123456789ABCDEF";
	std::wstring r;
	for (unsigned char x : b) { r += d[x >> 4]; r += d[x & 15]; }
	return r;
}

// =================================================================================================
//  6. Las operaciones
// =================================================================================================

// ---- version --------------------------------------------------------------------------------------
// Sin parámetros. Devuelve {"version":"4.0"} ("3.0", "4.0" o "" si no hay DNIe en el lector) y
// "serie" (número de serie del chip; vacío sin DNIe). No muestra nada ni pide el PIN.
//
// El número de serie permite a la página saber si el usuario ha cambiado de DNIe entre dos
// operaciones (por ejemplo, para borrar de la pantalla los datos del anterior).
static std::string OperacionVersion(Contexto& c)
{
	CK_RV rv = c.tarjeta.Conectar();
	if (rv == CKR_TOKEN_NOT_PRESENT)
		return ObjetoJson().Bool("correcto", true).Texto("version", std::string()).Texto("serie", std::string()).Json();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);
	return ObjetoJson().Bool("correcto", true)
	                   .Texto("version", std::string(EsDNIeRSA(c) ? "3.0" : "4.0"))
	                   .Texto("serie", Serie(c)).Json();
}

// ---- informacion ----------------------------------------------------------------------------------
// Sin parámetros. Devuelve:
//   version, serie       como "version"
//   tipo                 "DNI"
//   hardware, firmware   versiones del chip que da el driver ("1.00")
//   programas            [{"nombre":"DNIeDatos.exe","version":"1.0.0.0"},
//                         {"nombre":"DNIe_P11_x64.dll","version":"..."}]
// Sin DNIe en el lector devuelve igualmente "programas" (y lo demás vacío).
static std::string OperacionInformacion(Contexto& c)
{
	ObjetoJson r;
	r.Bool("correcto", true);
	CK_RV rv = c.tarjeta.Conectar();
	CK_TOKEN_INFO info;
	if (rv == CKR_OK && c.tarjeta.InfoToken(info) == CKR_OK)
	{
		char hw[16], fw[16];
		sprintf_s(hw, "%u.%02u", info.hardwareVersion.major, info.hardwareVersion.minor);
		sprintf_s(fw, "%u.%02u", info.firmwareVersion.major, info.firmwareVersion.minor);
		r.Texto("version", std::string(EsDNIeRSA(c) ? "3.0" : "4.0"))
		 .Texto("serie", Serie(c)).Texto("tipo", std::string("DNI"))
		 .Texto("hardware", std::string(hw)).Texto("firmware", std::string(fw));
	}
	else if (rv == CKR_OK || rv == CKR_TOKEN_NOT_PRESENT || rv == CKR_LIBRARY_LOAD_FAILED)
		r.Texto("version", std::string()).Texto("serie", std::string());
	else
		return ErrorPkcs11(rv);

	std::vector<std::string> programas;
	wchar_t propio[MAX_PATH];
	GetModuleFileNameW(NULL, propio, MAX_PATH);
	programas.push_back(ObjetoJson().Texto("nombre", std::wstring(PathFindFileNameW(propio)))
	                                .Texto("version", VersionFichero(propio)).Json());
	std::wstring driver = c.tarjeta.RutaDriver();
	if (!driver.empty())
		programas.push_back(ObjetoJson().Texto("nombre", std::wstring(PathFindFileNameW(driver.c_str())))
		                                .Texto("version", VersionFichero(driver)).Json());
	r.Crudo("programas", ListaJson(programas));
	return r.Json();
}

// ---- leer -----------------------------------------------------------------------------------------
// Parámetro "que": lista con una o más de "datos", "mrz", "imagenes", "certificados".
// Pide permiso UNA vez para todo lo pedido (la ventana dice qué se va a leer) y, con el DNIe 3.0,
// el PIN si hace falta leer grupos de datos. Devuelve solo lo pedido:
//   serie         número de serie del chip
//   datos         {"nombre","apellido1","apellido2","numero","sexo","nacionalidad",
//                  "fechaNacimiento","localidadNacimiento","provinciaNacimiento","padres",
//                  "domicilio","localidadDomicilio","provinciaDomicilio","fechaCaducidad","soporte"}
//                 Las fechas, tal como las guarda el DNIe ("DD MM AAAA").
//   mrz           las tres líneas seguidas (90 caracteres)
//   imagenes      {"foto":Base64,"formatoFoto":"jp2","firma":Base64,"formatoFirma":"jp2"}
//                 (formato: "jp2", "j2k" o "jpg"; la página de ejemplo dibuja JPEG 2000 con openjpeg.js)
//   certificados  [{"etiqueta":"CertAutenticacion","certificado":Base64 del DER}, ...]
static std::string OperacionLeer(Contexto& c, const ValorJson& p)
{
	bool datos = false, mrz = false, imagenes = false, certificados = false;
	const ValorJson* que = p.Campo("que");
	if (!que || que->tipo != ValorJson::Lista || que->elementos.empty())
		return ErrorParametros("Falta la lista de lo que se quiere leer (\"que\").");
	for (const ValorJson& e : que->elementos)
	{
		if (e.tipo != ValorJson::Cadena)
			return ErrorParametros("\"que\" tiene que ser una lista de textos.");
		if (e.texto == "datos") datos = true;
		else if (e.texto == "mrz") mrz = true;
		else if (e.texto == "imagenes") imagenes = true;
		else if (e.texto == "certificados") certificados = true;
		else return ErrorParametros("No se puede leer \"" + e.texto + "\". Admitidos: datos, mrz, imagenes, certificados.");
	}

	CK_RV rv = c.tarjeta.Conectar();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);

	// 1. Permiso del usuario, para todo lo pedido a la vez.
	rv = PermisoLectura(c, datos, mrz, imagenes, certificados);
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);

	// 2. DNIe 3.0: los grupos de datos solo se leen con el PIN presentado.
	const bool grupos = datos || mrz || imagenes;
	if (grupos && EsDNIeRSA(c))
	{
		rv = AsegurarPin(c, L"leer sus datos");
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
	}

	ObjetoJson r;
	r.Bool("correcto", true).Texto("serie", Serie(c));

	// 3. Certificados (no son grupos de datos: van primero, antes de cualquier reinicio).
	if (certificados)
	{
		std::vector<std::string> etiquetas, lista;
		rv = c.tarjeta.Etiquetas(CKO_CERTIFICATE, etiquetas);
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
		for (const std::string& e : etiquetas)
		{
			Bytes der;
			if (c.tarjeta.ValorPorEtiqueta(CKO_CERTIFICATE, e, CKA_VALUE, der) == CKR_OK)
				lista.push_back(ObjetoJson().Texto("etiqueta", TextoSeguro(e)).Base64("certificado", der).Json());
		}
		r.Crudo("certificados", ListaJson(lista));
	}

	// 4. Grupos de datos.
	if (datos)
	{
		Bytes dg13;
		std::vector<std::string> f;
		rv = LeerDato(c, "DG13", dg13);
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
		bool ok = CamposDG13(dg13, f);
		Borrar(dg13);
		if (!ok)
			return ErrorPkcs11(CKR_DATA_INVALID);
		ObjetoJson d;
		d.Texto("nombre", TextoSeguro(f[DG13_NOMBRE]))
		 .Texto("apellido1", TextoSeguro(f[DG13_APELLIDO1]))
		 .Texto("apellido2", TextoSeguro(f[DG13_APELLIDO2]))
		 .Texto("numero", TextoSeguro(f[DG13_NUMERO_DNI]))
		 .Texto("sexo", TextoSeguro(f[DG13_SEXO]))
		 .Texto("nacionalidad", TextoSeguro(f[DG13_NACIONALIDAD]))
		 .Texto("fechaNacimiento", TextoSeguro(f[DG13_FECHA_NACIMIENTO]))
		 .Texto("localidadNacimiento", TextoSeguro(f[DG13_LOCALIDAD_NACIMIENTO]))
		 .Texto("provinciaNacimiento", TextoSeguro(f[DG13_PROVINCIA_NACIMIENTO]))
		 .Texto("padres", TextoSeguro(f[DG13_PADRES]))
		 .Texto("domicilio", TextoSeguro(f[DG13_DOMICILIO]))
		 .Texto("localidadDomicilio", TextoSeguro(f[DG13_LOCALIDAD_DOMICILIO]))
		 .Texto("provinciaDomicilio", TextoSeguro(f[DG13_PROVINCIA_DOMICILIO]))
		 .Texto("fechaCaducidad", TextoSeguro(f[DG13_FECHA_EXPIRACION]))
		 .Texto("soporte", TextoSeguro(f[DG13_SOPORTE]));
		for (std::string& s : f)
			if (!s.empty())
				SecureZeroMemory(&s[0], s.size());
		r.Crudo("datos", d.Json());
	}
	if (mrz)
	{
		Bytes dg1;
		std::string m;
		rv = LeerDato(c, "DG1", dg1);
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
		bool ok = MrzDG1(dg1, m);
		Borrar(dg1);
		if (!ok)
			return ErrorPkcs11(CKR_DATA_INVALID);
		r.Texto("mrz", TextoSeguro(m));
	}
	if (imagenes)
	{
		Bytes dg2, dg7, foto, firma;
		std::string fFoto, fFirma;
		rv = LeerDato(c, "DG2", dg2);
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
		bool ok = ImagenDeGrupo(dg2, 0x5F2E, foto, fFoto);
		Borrar(dg2);
		if (!ok)
			return ErrorPkcs11(CKR_DATA_INVALID);
		rv = LeerDato(c, "DG7", dg7);
		if (rv != CKR_OK)
			return ErrorPkcs11(rv);
		ok = ImagenDeGrupo(dg7, 0x5F43, firma, fFirma);
		Borrar(dg7);
		if (!ok)
			return ErrorPkcs11(CKR_DATA_INVALID);
		r.Crudo("imagenes", ObjetoJson().Base64("foto", foto).Texto("formatoFoto", fFoto)
		                                .Base64("firma", firma).Texto("formatoFirma", fFirma).Json());
		Borrar(foto);
		Borrar(firma);
	}
	return r.Json();
}

// ---- claves ---------------------------------------------------------------------------------------
// Sin parámetros. Claves públicas de los dos certificados del titular, sacadas de los certificados
// (se leen sin PIN):
//   claves  [{"uso":"autenticacion","tipo":"EC","punto":Base64 de 04||X||Y},
//            {"uso":"firma","tipo":"RSA","modulo":Base64,"exponente":Base64}]
static std::string OperacionClaves(Contexto& c)
{
	CK_RV rv = c.tarjeta.Conectar();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);
	std::vector<std::string> lista;
	const struct { const char* uso; const char* cert; } usos[] = {
		{ "autenticacion", "CertAutenticacion" }, { "firma", "CertFirmaDigital" } };
	for (auto& u : usos)
	{
		Bytes cert, a, b;
		if (c.tarjeta.ValorPorEtiqueta(CKO_CERTIFICATE, u.cert, CKA_VALUE, cert) != CKR_OK)
			continue;
		ObjetoJson k;
		k.Texto("uso", std::string(u.uso));
		if (ClavePublicaDeCertificado(cert, true, a, b) == CKR_OK)
			k.Texto("tipo", std::string("EC")).Base64("punto", a);
		else if (ClavePublicaDeCertificado(cert, false, a, b) == CKR_OK)
			k.Texto("tipo", std::string("RSA")).Base64("modulo", a).Base64("exponente", b);
		else
			continue;
		lista.push_back(k.Json());
	}
	if (lista.empty())
		return ErrorPkcs11(CKR_KEY_HANDLE_INVALID);
	return ObjetoJson().Bool("correcto", true).Texto("serie", Serie(c)).Crudo("claves", ListaJson(lista)).Json();
}

// Firma un resumen SHA-256 con la clave privada del uso pedido. Común a firmar y firmarArchivo.
// Pide el PIN ('para' completa «Introduzca el PIN de su DNIe para ...»). Añade a 'r' la firma, el
// algoritmo y el certificado.
static CK_RV FirmarResumen(Contexto& c, const std::string& nucleo, const Bytes& resumen,
                           const std::wstring& para, ObjetoJson& r)
{
	CK_RV rv = AsegurarPin(c, para);
	if (rv != CKR_OK)
		return rv;

	// Etiqueta de la clave privada: con el PIN presentado ya se pueden listar. Es
	// "KprivFirmaDigital" en el DNIe 3.0 y "KprivFirmaDigitalEC" en el 4.0 (igual con Autenticacion).
	std::vector<std::string> etiquetas;
	rv = c.tarjeta.Etiquetas(CKO_PRIVATE_KEY, etiquetas);
	if (rv != CKR_OK)
		return rv;
	std::string clave;
	for (const std::string& e : etiquetas)
		if (e.find(nucleo) != std::string::npos)
		{
			clave = e;
			break;
		}
	if (clave.empty())
		return CKR_KEY_HANDLE_INVALID;

	Bytes firma, cert, a, b;
	rv = c.tarjeta.Firmar(clave, resumen, firma);
	if (rv != CKR_OK)
		return rv;
	c.tarjeta.CertificadoDeClave(clave, cert);
	const bool ec = !cert.empty() && ClavePublicaDeCertificado(cert, true, a, b) == CKR_OK;
	r.Base64("firma", firma)
	 .Texto("algoritmo", std::string(ec ? "ECDSA-SHA256" : "RSA-PKCS1-SHA256"))
	 .Base64("hash", resumen)
	 .Base64("certificado", cert);
	return CKR_OK;
}

// ---- firmar ---------------------------------------------------------------------------------------
// Parámetros:
//   texto  el texto que se firma (hasta 64 KB en UTF-8); se firma su SHA-256    -- o bien --
//   hash   el resumen SHA-256 ya calculado (Base64, 32 bytes), p. ej. de un documento del servidor
//   uso    "firma" (por defecto: certificado de firma) o "autenticacion"
// Antes del PIN, una ventana enseña el texto (o avisa de que solo se ve su resumen) y pide
// confirmación. Devuelve:
//   firma        Base64. ECDSA: r||s en bruto (64 bytes, DNIe 4.0). RSA: PKCS#1 v1.5 (256 bytes, DNIe 3.0)
//   algoritmo    "ECDSA-SHA256" o "RSA-PKCS1-SHA256"
//   hash         el SHA-256 firmado (Base64)
//   certificado  el certificado del firmante (Base64 del DER), para verificar la firma en el servidor
static std::string OperacionFirmar(Contexto& c, const ValorJson& p)
{
	std::string nucleo, error;
	Bytes resumen;
	if (!Nucleo(p, nucleo))
		return ErrorParametros("\"uso\" tiene que ser \"firma\" o \"autenticacion\".");
	if (!Resumen(p, resumen, error))
		return ErrorParametros(error);

	CK_RV rv = c.tarjeta.Conectar();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);

	const std::wstring cert = nucleo == "FirmaDigital" ? L"su certificado de firma" : L"su certificado de autenticación";
	bool sigue;
	if (p.TieneTexto("texto"))
		sigue = Confirmar(c.Duena(), L"¿Firmar este texto con su DNIe?",
		                  L"Se firmará con " + cert + L":\n\n" + TextoParaMostrar(DeUtf8(p.Texto("texto"))),
		                  c.origen, L"Continuar", L"Cancelar");
	else
		sigue = Confirmar(c.Duena(), L"¿Firmar un documento con su DNIe?",
		                  L"La página no muestra el documento: solo envía su resumen (SHA-256)\n" + Hex(resumen) +
		                  L"\n\nSe firmará con " + cert + L". Continúe solo si confía en esta página y sabe qué "
		                  L"documento está firmando.",
		                  c.origen, L"Continuar", L"Cancelar");
	if (!sigue)
		return ErrorPkcs11(CKR_FUNCTION_CANCELED);

	ObjetoJson r;
	r.Bool("correcto", true);
	rv = FirmarResumen(c, nucleo, resumen, L"firmar", r);
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);
	return r.Json();
}

// SHA-256 de un archivo, leyéndolo por partes (sirve para archivos de cualquier tamaño).
static bool Sha256Archivo(const std::wstring& ruta, Bytes& resumen, unsigned long long& tamano)
{
	resumen.clear();
	tamano = 0;
	HANDLE f = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE h = NULL;
	bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
	          BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0;
	std::vector<unsigned char> buf(1 << 20);
	while (ok)
	{
		DWORD n = 0;
		if (!ReadFile(f, buf.data(), (DWORD)buf.size(), &n, NULL))
			ok = false;
		else if (n == 0)
			break;
		else
		{
			tamano += n;
			ok = BCryptHashData(h, buf.data(), n, 0) == 0;
		}
	}
	if (ok)
	{
		resumen.resize(32);
		ok = BCryptFinishHash(h, resumen.data(), 32, 0) == 0;
	}
	if (h) BCryptDestroyHash(h);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	CloseHandle(f);
	if (!ok)
		resumen.clear();
	return ok;
}

// ---- firmarArchivo --------------------------------------------------------------------------------
// Parámetro "uso" (como en firmar). El usuario elige el archivo en el diálogo "Abrir" de Windows y
// el anfitrión firma su SHA-256 (pide el PIN, que dice el nombre del archivo). La página no indica
// ninguna ruta y solo recibe el NOMBRE del archivo. Devuelve lo mismo que firmar, más:
//   archivo  nombre del archivo (sin la carpeta)
//   tamano   tamaño en bytes
static std::string OperacionFirmarArchivo(Contexto& c, const ValorJson& p)
{
	std::string nucleo;
	if (!Nucleo(p, nucleo))
		return ErrorParametros("\"uso\" tiene que ser \"firma\" o \"autenticacion\".");

	CK_RV rv = c.tarjeta.Conectar();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);

	std::wstring ruta = ElegirArchivo(c.Duena(), L"Seleccione el archivo que quiere firmar con su DNIe");
	if (ruta.empty())
		return RespuestaDeError("cancelado", L"Operación cancelada: no se seleccionó ningún archivo.");
	std::wstring nombre = PathFindFileNameW(ruta.c_str());

	Bytes resumen;
	unsigned long long tamano = 0;
	if (!Sha256Archivo(ruta, resumen, tamano))
		return RespuestaDeError("error", L"No se pudo leer el archivo «" + nombre + L"».");

	ObjetoJson r;
	r.Bool("correcto", true).Texto("archivo", nombre).Entero("tamano", (long long)tamano);
	rv = FirmarResumen(c, nucleo, resumen, L"firmar «" + Extracto(nombre, 40) + L"»", r);
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);
	return r.Json();
}

// ---- verificar ------------------------------------------------------------------------------------
// Parámetros: "texto" o "hash" (como en firmar), "firma" (Base64, como la devuelve firmar) y "uso".
// Comprueba la firma con la clave pública del certificado del DNIe (no necesita PIN). Devuelve
//   valida  true o false
// Una firma que no es válida no es un error: la respuesta es correcta, con "valida":false.
static std::string OperacionVerificar(Contexto& c, const ValorJson& p)
{
	std::string nucleo, error;
	Bytes resumen, firma;
	if (!Nucleo(p, nucleo))
		return ErrorParametros("\"uso\" tiene que ser \"firma\" o \"autenticacion\".");
	if (!Resumen(p, resumen, error))
		return ErrorParametros(error);
	if (!DeBase64(p.Texto("firma"), firma))
		return ErrorParametros("Falta la firma (\"firma\", en Base64).");

	CK_RV rv = c.tarjeta.Conectar();
	if (rv != CKR_OK)
		return ErrorPkcs11(rv);
	// "Kpu" + núcleo: CertificadoDeClave (Tarjeta.cpp) lo convierte en "Cert" + núcleo.
	rv = c.tarjeta.Verificar("Kpu" + nucleo, resumen, firma);
	if (rv == CKR_OK || rv == CKR_SIGNATURE_INVALID || rv == CKR_SIGNATURE_LEN_RANGE)
		return ObjetoJson().Bool("correcto", true).Bool("valida", rv == CKR_OK).Json();
	return ErrorPkcs11(rv);
}

// ---- hash -----------------------------------------------------------------------------------------
// Parámetro "texto" (hasta 64 KB). Devuelve "hash": su SHA-256 en Base64. No usa la tarjeta.
// (Es el mismo resumen que firma "firmar" con ese texto.)
static std::string OperacionHash(const ValorJson& p)
{
	if (!p.TieneTexto("texto"))
		return ErrorParametros("Falta el texto (\"texto\").");
	Bytes resumen;
	std::string error;
	if (!Resumen(p, resumen, error))
		return ErrorParametros(error);
	return ObjetoJson().Bool("correcto", true).Base64("hash", resumen).Json();
}

// =================================================================================================
//  7. Reparto
// =================================================================================================
std::string AtenderPeticion(const ValorJson& p)
{
	const std::string operacion = p.Texto("operacion");
	if (operacion == "hash")
		return OperacionHash(p);

	Contexto c;
	c.origen = DeUtf8(p.Texto("origen"));
	if (operacion == "version")       return OperacionVersion(c);
	if (operacion == "informacion")   return OperacionInformacion(c);
	if (operacion == "leer")          return OperacionLeer(c, p);
	if (operacion == "claves")        return OperacionClaves(c);
	if (operacion == "firmar")        return OperacionFirmar(c, p);
	if (operacion == "firmarArchivo") return OperacionFirmarArchivo(c, p);
	if (operacion == "verificar")     return OperacionVerificar(c, p);
	return RespuestaDeError("error", L"Operación desconocida.");
}
