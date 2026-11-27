// =================================================================================================
//  Tarjeta.cpp  -  Acceso al DNIe a través del driver PKCS#11 (ver la introducción en Tarjeta.h)
// =================================================================================================
//
//  Además del driver PKCS#11 se usan tres partes de Windows:
//    - BCrypt (CNG):   SHA-256 y verificación de firmas con la clave pública del certificado.
//    - CryptoAPI:      leer certificados (DER) para sacar su clave pública.
//    - PC/SC (WinSCard): hablar con el LECTOR, no con la tarjeta: saber si se ha sacado o cambiado
//                      la tarjeta y reiniciarla. Las operaciones con el chip las hace siempre el driver.
// =================================================================================================
#include "stdafx.h"
#include "Tarjeta.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#include <wincrypt.h>
#include <winscard.h>
#pragma comment(lib, "winscard.lib")
#include <algorithm>

// Nombre del driver del DNIe según la arquitectura del proceso. Se carga SOLO desde la carpeta
// del sistema (System32, o SysWOW64 para procesos de 32 bits), nunca por la ruta de búsqueda,
// para evitar que se cargue una DLL suplantada.
#if defined(_M_ARM64)
static const wchar_t* DRIVER_DNIE = L"DNIe_P11_arm64.dll";
#elif defined(_M_X64)
static const wchar_t* DRIVER_DNIE = L"DNIe_P11_x64.dll";
#else
static const wchar_t* DRIVER_DNIE = L"DNIe_P11.dll";
#endif

// Etiqueta sin el relleno final (nulos o espacios) que añade el driver.
// En PKCS#11 los textos de las estructuras (descripción del slot, etiqueta del token...) tienen
// longitud fija y se rellenan con espacios; algunos atributos CKA_LABEL vienen además con nulos.
static std::string Limpia(std::string e)
{
	size_t n = e.find_last_not_of(std::string(" \0", 2));
	e.erase(n == std::string::npos ? 0 : n + 1);
	return e;
}

CTarjeta::CTarjeta()
	: m_hDriver(NULL), m_p11(NULL), m_inicializado(false), m_slot(0), m_hSesion(CK_INVALID_HANDLE),
	  m_pinPresentado(false), m_abierta(false), m_eventos(0), m_hayEventos(false), m_generacion(0),
	  m_ocupada(false)
{
}

CTarjeta::~CTarjeta()
{
	Desconectar();
}

// ---------------------------------------------------------------------------------------------
// Carga del driver
// ---------------------------------------------------------------------------------------------
//  1. Construye la ruta completa en la carpeta del sistema (GetSystemDirectory). Para un proceso de
//     32 bits en un Windows de 64 bits, Windows redirige System32 a SysWOW64 sin que haya que hacer
//     nada: por eso el mismo código carga DNIe_P11.dll (32 bits) o DNIe_P11_x64.dll (64 bits).
//  2. LoadLibraryEx con la ruta completa: nunca se busca la DLL por el PATH ni en la carpeta del
//     programa, para que nadie pueda colocar un "driver" falso.
//  3. Pide al driver su tabla de funciones (C_GetFunctionList). A partir de aquí todo se llama a
//     través de m_p11 (m_p11->C_Initialize, m_p11->C_Login...).
CK_RV CTarjeta::CargarDriver()
{
	if (m_p11)
		return CKR_OK;

	wchar_t sistema[MAX_PATH];
	UINT n = GetSystemDirectoryW(sistema, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return CKR_GENERAL_ERROR;
	m_rutaDriver = std::wstring(sistema) + L"\\" + DRIVER_DNIE;

	m_hDriver = LoadLibraryExW(m_rutaDriver.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!m_hDriver)
		return CKR_LIBRARY_LOAD_FAILED;

	CK_C_GetFunctionList pGetFunctionList = (CK_C_GetFunctionList)GetProcAddress(m_hDriver, "C_GetFunctionList");
	if (!pGetFunctionList)
	{
		DescargarDriver();
		return CKR_LIBRARY_LOAD_FAILED;
	}
	CK_RV rv = pGetFunctionList(&m_p11);
	if (rv != CKR_OK || !m_p11)
	{
		DescargarDriver();
		return rv != CKR_OK ? rv : CKR_GENERAL_ERROR;
	}
	return CKR_OK;
}

void CTarjeta::DescargarDriver()
{
	m_p11 = NULL;
	if (m_hDriver)
	{
		FreeLibrary(m_hDriver);
		m_hDriver = NULL;
	}
}

// ---------------------------------------------------------------------------------------------
// Conexión: los pasos de cualquier programa PKCS#11
// ---------------------------------------------------------------------------------------------
//  1. Cargar el driver (CargarDriver).
//  2. C_Initialize: el driver se prepara. Si otro componente del mismo proceso ya lo había
//     inicializado, devuelve CKR_CRYPTOKI_ALREADY_INITIALIZED, que para nosotros es correcto.
//  3. C_GetSlotList(CK_TRUE): lista de lectores CON tarjeta. Se llama dos veces, como es habitual
//     en PKCS#11: la primera, con NULL, solo para saber cuántos hay; la segunda, para obtenerlos.
//     Se usa el primero. Sin tarjeta: CKR_TOKEN_NOT_PRESENT ("No hay tarjeta en el lector").
//  4. C_GetSlotInfo: nombre del lector, que se guarda para reiniciar la tarjeta o detectar si la
//     cambian (eso se hace por PC/SC, que identifica los lectores por su nombre).
//  5. C_OpenSession: abre la sesión. CKF_SERIAL_SESSION es obligatorio; CKF_RW_SESSION pide lectura
//     y escritura. El canal seguro con la tarjeta (en el DNIe 4.0, PACE con el CAN) lo abre el
//     driver por su cuenta cuando lo necesita; el anfitrión no tiene que hacer nada.
//  6. Anota el contador de eventos del lector, para saber después si se ha cambiado la tarjeta.
CK_RV CTarjeta::Conectar()
{
	if (Conectada())
		return CKR_OK;

	CK_RV rv = CargarDriver();
	if (rv != CKR_OK)
		return rv;

	if (!m_inicializado)
	{
		rv = m_p11->C_Initialize(NULL_PTR);
		if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED)
			return rv;
		m_inicializado = true;
	}

	// Primer lector con tarjeta
	CK_ULONG nSlots = 0;
	rv = m_p11->C_GetSlotList(CK_TRUE, NULL_PTR, &nSlots);
	if (rv != CKR_OK)
		return rv;
	if (nSlots == 0)
		return CKR_TOKEN_NOT_PRESENT;
	std::vector<CK_SLOT_ID> slots(nSlots);
	rv = m_p11->C_GetSlotList(CK_TRUE, slots.data(), &nSlots);
	if (rv != CKR_OK)
		return rv;
	m_slot = slots[0];

	CK_SLOT_INFO si;
	if (m_p11->C_GetSlotInfo(m_slot, &si) == CKR_OK)
		m_lector = Limpia(std::string((char*)si.slotDescription, sizeof(si.slotDescription)));

	rv = m_p11->C_OpenSession(m_slot, CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL_PTR, NULL_PTR, &m_hSesion);
	if (rv == CKR_OK)
	{
		m_abierta = true;
		bool presente = false;
		m_lectorPCSC.clear();
		m_hayEventos = EventosLector(m_eventos, presente);
	}
	return rv;
}

// Desconectar cierra todo y ya no se reconecta sola.
// Cerrar hace lo mismo pero sin dar por terminada la conexión: la usan ReiniciarTarjeta y
// Comprobar, que vuelven a conectar justo después.
CK_RV CTarjeta::Desconectar()
{
	m_abierta = false;
	Cerrar();
	return CKR_OK;
}

// Orden inverso a Conectar: C_Logout (olvida el PIN), C_CloseSession, C_Finalize y descargar la DLL.
void CTarjeta::Cerrar()
{
	if (m_p11 && m_hSesion != CK_INVALID_HANDLE)
	{
		m_p11->C_Logout(m_hSesion);
		m_p11->C_CloseSession(m_hSesion);
	}
	m_hSesion = CK_INVALID_HANDLE;
	m_pinPresentado = false;
	if (m_p11 && m_inicializado)
		m_p11->C_Finalize(NULL_PTR);
	m_inicializado = false;
	m_hayEventos = false;
	DescargarDriver();
}

// Reinicio de la tarjeta por PC/SC: equivale a sacarla y volver a meterla, y deja el chip sin canal
// seguro para que el driver lo abra de nuevo al conectar.
//
// Cómo funciona: PC/SC es la interfaz de Windows para los lectores. Se abre un "contexto"
// (SCardEstablishContext), se busca el lector por su nombre y se conecta con él en modo compartido
// (SCardConnect). Al desconectar con SCARD_RESET_CARD, el lector reinicia la tarjeta (le corta y
// le devuelve la corriente), igual que si se sacara y se volviera a meter.
static void ReiniciarLector(const std::string& lector)
{
	if (lector.empty())
		return;
	SCARDCONTEXT ctx = 0;
	if (SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx) != SCARD_S_SUCCESS)
		return;
	DWORD n = 0;
	if (SCardListReadersA(ctx, NULL, NULL, &n) == SCARD_S_SUCCESS && n > 0)
	{
		std::vector<char> lista(n);
		if (SCardListReadersA(ctx, NULL, lista.data(), &n) == SCARD_S_SUCCESS)
		{
			// El driver puede recortar el nombre del lector a 64 caracteres: se compara por prefijo
			for (const char* r = lista.data(); *r; r += strlen(r) + 1)
			{
				std::string nombre(r);
				if (nombre.compare(0, lector.size(), lector) != 0 && lector.compare(0, nombre.size(), nombre) != 0)
					continue;
				SCARDHANDLE h = 0;
				DWORD proto = 0;
				if (SCardConnectA(ctx, r, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &h, &proto) == SCARD_S_SUCCESS)
					SCardDisconnect(h, SCARD_RESET_CARD);
				break;
			}
		}
	}
	SCardReleaseContext(ctx);
}

// Para qué: que el driver abra de nuevo el canal seguro con la tarjeta (ver LeerDato en
// GruposDatos.cpp). Se cierra la conexión, se reinicia la tarjeta, se espera un poco a que el lector la
// vuelva a activar y se conecta de nuevo. El PIN se pierde.
CK_RV CTarjeta::ReiniciarTarjeta()
{
	std::string lector = m_lector;
	Cerrar();
	ReiniciarLector(lector);
	Sleep(300);
	return Conectar();
}

// Contador de eventos del lector de la tarjeta: en Windows, la palabra alta de dwEventState cuenta
// las inserciones y extracciones de tarjeta en ese lector (no cambia al reiniciarla).
//
// SCardGetStatusChange con espera 0 no espera a ningún cambio: solo devuelve el estado actual del
// lector (SCARD_STATE_UNAWARE = "no sé en qué estado estaba"). Es una consulta al lector, rápida,
// que no envía nada al chip ni interfiere con lo que esté haciendo el driver.
bool CTarjeta::EventosLector(DWORD& eventos, bool& presente)
{
	eventos = 0;
	presente = false;
	SCARDCONTEXT ctx = 0;
	if (SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx) != SCARD_S_SUCCESS)
		return false;
	bool ok = false;
	if (m_lectorPCSC.empty() && !m_lector.empty())
	{
		// El driver puede recortar el nombre del lector a 64 caracteres: se busca por prefijo
		DWORD n = 0;
		if (SCardListReadersA(ctx, NULL, NULL, &n) == SCARD_S_SUCCESS && n > 0)
		{
			std::vector<char> lista(n);
			if (SCardListReadersA(ctx, NULL, lista.data(), &n) == SCARD_S_SUCCESS)
				for (const char* r = lista.data(); *r; r += strlen(r) + 1)
				{
					std::string nombre(r);
					if (nombre.compare(0, m_lector.size(), m_lector) == 0 || m_lector.compare(0, nombre.size(), nombre) == 0)
					{
						m_lectorPCSC = nombre;
						break;
					}
				}
		}
	}
	if (!m_lectorPCSC.empty())
	{
		SCARD_READERSTATEA st = {};
		st.szReader = m_lectorPCSC.c_str();
		st.dwCurrentState = SCARD_STATE_UNAWARE;
		if (SCardGetStatusChangeA(ctx, 0, &st, 1) == SCARD_S_SUCCESS)
		{
			eventos = HIWORD(st.dwEventState);
			presente = (st.dwEventState & SCARD_STATE_PRESENT) != 0;
			ok = true;
		}
	}
	SCardReleaseContext(ctx);
	return ok;
}

// ---------------------------------------------------------------------------------------------
// ¿Sigue siendo la misma tarjeta?
// ---------------------------------------------------------------------------------------------
// La llaman todas las operaciones antes de usar la sesión. Casos:
//   - La tarjeta está marcada como ocupada (MarcarOcupada): CKR_OPERATION_ACTIVE, sin hacer nada.
//   - El contador de eventos del lector no ha cambiado y hay tarjeta: la misma, se sigue.
//   - Ha cambiado (se ha sacado, o sacado y metido otra): se cierra todo (PIN incluido) y se
//     aumenta m_generacion, para que quien guarde datos de la tarjeta los olvide.
//       - Sin tarjeta ahora:  CKR_TOKEN_NOT_PRESENT.
//       - Con tarjeta:        se conecta con ella y se devuelve CKR_DEVICE_REMOVED UNA vez, sin
//                             hacer la operación, para que quien llama avise y no mezcle datos de dos
//                             tarjetas. La siguiente operación ya funciona con la tarjeta nueva.
//   - No había conexión (se sacó antes la tarjeta): se intenta conectar con la que haya.
// Si no se puede consultar el lector (m_hayEventos falso), se sigue como si nada.
CK_RV CTarjeta::Comprobar()
{
	if (m_ocupada)
		return CKR_OPERATION_ACTIVE;             // tarjeta marcada como ocupada
	if (!m_abierta)
		return Conectada() ? CKR_OK : CKR_SESSION_HANDLE_INVALID;
	if (Conectada())
	{
		DWORD eventos = 0;
		bool presente = false;
		if (!m_hayEventos || !EventosLector(eventos, presente))
			return CKR_OK;                       // no se puede saber: se sigue como hasta ahora
		if (presente && eventos == m_eventos)
			return CKR_OK;                       // la misma tarjeta
		Cerrar();                                // se ha sacado o cambiado: se olvida todo, PIN incluido
		m_generacion++;
		m_abierta = true;
		if (!presente)
			return CKR_TOKEN_NOT_PRESENT;
		// Otra tarjeta (o la misma sacada y vuelta a meter): se conecta con ella, pero esta operación
		// no se hace, para que quien llama sepa que la tarjeta ha cambiado y no mezcle datos de dos.
		CK_RV rc = Conectar();
		if (rc != CKR_OK)
		{
			Cerrar();
			return rc;
		}
		return CKR_DEVICE_REMOVED;
	}
	// Sin conexión (se sacó la tarjeta antes): se intenta con la que haya ahora
	CK_RV rv = Conectar();
	if (rv != CKR_OK)
		Cerrar();
	m_abierta = true;
	return rv;
}

// C_Login con el usuario normal (CKU_USER; el otro tipo, CKU_SO, es el administrador de la tarjeta).
// El PIN se pasa como bytes con su longitud, no como cadena terminada en nulo. Si ya estaba
// presentado, el driver responde CKR_USER_ALREADY_LOGGED_IN, que aquí se da por bueno.
CK_RV CTarjeta::PresentarPin(const std::string& pin)
{
	CK_RV rc = Comprobar();
	if (rc != CKR_OK)
		return rc;
	if (!Conectada())
		return CKR_SESSION_HANDLE_INVALID;
	CK_RV rv = m_p11->C_Login(m_hSesion, CKU_USER, (CK_UTF8CHAR_PTR)pin.data(), (CK_ULONG)pin.size());
	if (rv == CKR_USER_ALREADY_LOGGED_IN)
		rv = CKR_OK;
	if (rv == CKR_OK)
		m_pinPresentado = true;
	return rv;
}

// C_GetTokenInfo: etiqueta, fabricante, modelo, número de serie y longitudes de PIN que admite
// (ulMinPinLen / ulMaxPinLen, las que usa el diálogo del PIN).
CK_RV CTarjeta::InfoToken(CK_TOKEN_INFO& info)
{
	CK_RV rc = Comprobar();
	if (rc != CKR_OK)
		return rc;
	if (!m_p11)
		return CKR_CRYPTOKI_NOT_INITIALIZED;
	return m_p11->C_GetTokenInfo(m_slot, &info);
}

// ---------------------------------------------------------------------------------------------
// Buscar objetos
// ---------------------------------------------------------------------------------------------
// La búsqueda en PKCS#11 tiene tres pasos:
//   1. C_FindObjectsInit con una "plantilla": la lista de atributos que deben cumplir. Aquí solo la
//      clase (CKO_CERTIFICATE, CKO_PRIVATE_KEY, CKO_PUBLIC_KEY o CKO_DATA).
//   2. C_FindObjects, repetido hasta que devuelve menos de los que se piden: da los "handles"
//      (números que identifican cada objeto mientras dura la sesión).
//   3. C_FindObjectsFinal, siempre, aunque haya habido error: cierra la búsqueda. Mientras una
//      búsqueda está abierta, la sesión no admite otra.
// Después se lee la etiqueta (CKA_LABEL) de cada objeto encontrado.
CK_RV CTarjeta::Etiquetas(CK_OBJECT_CLASS clase, std::vector<std::string>& etiquetas)
{
	etiquetas.clear();
	CK_RV rc = Comprobar();
	if (rc != CKR_OK)
		return rc;
	if (!Conectada())
		return CKR_SESSION_HANDLE_INVALID;

	CK_ATTRIBUTE plantilla[] = { { CKA_CLASS, &clase, sizeof(clase) } };
	CK_RV rv = m_p11->C_FindObjectsInit(m_hSesion, plantilla, 1);
	if (rv != CKR_OK)
		return rv;

	CK_OBJECT_HANDLE h[16];
	CK_ULONG n = 0;
	std::vector<CK_OBJECT_HANDLE> todos;
	do
	{
		rv = m_p11->C_FindObjects(m_hSesion, h, 16, &n);
		if (rv != CKR_OK)
			break;
		todos.insert(todos.end(), h, h + n);
	} while (n == 16);
	m_p11->C_FindObjectsFinal(m_hSesion);
	if (rv != CKR_OK)
		return rv;

	for (CK_OBJECT_HANDLE o : todos)
	{
		Bytes etiqueta;
		if (LeerAtributo(o, CKA_LABEL, etiqueta) == CKR_OK)
			etiquetas.push_back(Limpia(std::string(etiqueta.begin(), etiqueta.end())));
	}
	return CKR_OK;
}

// Busca por clase y etiqueta. Las etiquetas se comparan sin el relleno final, porque el driver puede
// guardarlas con nulos o espacios al final.
CK_RV CTarjeta::BuscarObjeto(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_OBJECT_HANDLE& h)
{
	h = CK_INVALID_HANDLE;
	CK_RV rc = Comprobar();
	if (rc != CKR_OK)
		return rc;
	if (!Conectada())
		return CKR_SESSION_HANDLE_INVALID;

	CK_ATTRIBUTE plantilla[] = { { CKA_CLASS, &clase, sizeof(clase) } };
	CK_RV rv = m_p11->C_FindObjectsInit(m_hSesion, plantilla, 1);
	if (rv != CKR_OK)
		return rv;
	std::vector<CK_OBJECT_HANDLE> todos;
	CK_OBJECT_HANDLE lote[16];
	CK_ULONG n = 0;
	do
	{
		rv = m_p11->C_FindObjects(m_hSesion, lote, 16, &n);
		if (rv != CKR_OK)
			break;
		todos.insert(todos.end(), lote, lote + n);
	} while (n == 16);
	m_p11->C_FindObjectsFinal(m_hSesion);
	if (rv != CKR_OK)
		return rv;

	std::string buscada = Limpia(etiqueta);
	for (CK_OBJECT_HANDLE o : todos)
	{
		Bytes e;
		if (LeerAtributo(o, CKA_LABEL, e) == CKR_OK && Limpia(std::string(e.begin(), e.end())) == buscada)
		{
			h = o;
			return CKR_OK;
		}
	}
	return CKR_KEY_HANDLE_INVALID;   // no hay ningún objeto de esa clase con esa etiqueta
}

// Lectura de un atributo en dos llamadas, la forma habitual en PKCS#11:
//   1. C_GetAttributeValue con pValue = NULL: el driver solo dice cuánto ocupa (ulValueLen).
//      Si el objeto no tiene ese atributo, la longitud vuelve como -1.
//   2. Se reserva ese tamaño y se vuelve a llamar para que el driver lo rellene.
CK_RV CTarjeta::LeerAtributo(CK_OBJECT_HANDLE h, CK_ATTRIBUTE_TYPE atributo, Bytes& valor)
{
	CK_ATTRIBUTE a = { atributo, NULL_PTR, 0 };
	CK_RV rv = m_p11->C_GetAttributeValue(m_hSesion, h, &a, 1);
	if (rv != CKR_OK)
		return rv;
	if (a.ulValueLen == (CK_ULONG)-1)
		return CKR_ATTRIBUTE_TYPE_INVALID;
	valor.resize(a.ulValueLen);
	a.pValue = valor.data();
	return m_p11->C_GetAttributeValue(m_hSesion, h, &a, 1);
}

CK_RV CTarjeta::ValorPorEtiqueta(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_ATTRIBUTE_TYPE atributo, Bytes& valor)
{
	CK_OBJECT_HANDLE h;
	CK_RV rv = BuscarObjeto(clase, etiqueta, h);
	if (rv != CKR_OK)
		return rv;
	return LeerAtributo(h, atributo, valor);
}

CK_RV CTarjeta::TipoClave(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_KEY_TYPE& tipo)
{
	Bytes v;
	CK_RV rv = ValorPorEtiqueta(clase, etiqueta, CKA_KEY_TYPE, v);
	if (rv != CKR_OK)
		return rv;
	if (v.size() != sizeof(CK_KEY_TYPE))
		return CKR_ATTRIBUTE_VALUE_INVALID;
	memcpy(&tipo, v.data(), sizeof(tipo));
	return CKR_OK;
}

// Firma el resumen SHA-256 ya calculado:
//   RSA -> CKM_RSA_PKCS sobre DigestInfo(SHA-256)  (PKCS#1 v1.5)
//   EC  -> CKM_ECDSA sobre el resumen; la firma sale en bruto (r || s)
//
// Pasos:
//   1. Busca la clave privada por su etiqueta (KprivFirmaDigital, KprivAutenticacion...).
//   2. Lee su tipo (CKA_KEY_TYPE) para elegir el "mecanismo": RSA en el DNIe 3.0, curva elíptica
//      en el DNIe 4.0.
//   3. Prepara lo que se firma. CKM_RSA_PKCS no calcula el resumen ni dice qué algoritmo se usó, así
//      que se le da el DigestInfo: una cabecera DER fija que identifica SHA-256 seguida del resumen
//      (DigestInfoSha256). CKM_ECDSA firma directamente los 32 bytes del resumen.
//   4. C_SignInit + C_Sign. C_Sign se llama dos veces: con NULL para saber el tamaño de la firma
//      (256 bytes con RSA-2048, 64 con ECDSA P-256) y otra para obtenerla. La tarjeta solo firma con
//      el PIN presentado; si no, el driver devuelve CKR_USER_NOT_LOGGED_IN.
CK_RV CTarjeta::Firmar(const std::string& etiquetaPrivada, const Bytes& resumen, Bytes& firma)
{
	CK_OBJECT_HANDLE h;
	CK_RV rv = BuscarObjeto(CKO_PRIVATE_KEY, etiquetaPrivada, h);
	if (rv != CKR_OK)
		return rv;

	Bytes tipoB;
	rv = LeerAtributo(h, CKA_KEY_TYPE, tipoB);
	if (rv != CKR_OK)
		return rv;
	CK_KEY_TYPE tipo;
	memcpy(&tipo, tipoB.data(), sizeof(tipo));

	CK_MECHANISM mec = { (CK_MECHANISM_TYPE)(tipo == CKK_EC ? CKM_ECDSA : CKM_RSA_PKCS), NULL_PTR, 0 };
	Bytes entrada = (tipo == CKK_EC) ? resumen : DigestInfoSha256(resumen);

	rv = m_p11->C_SignInit(m_hSesion, &mec, h);
	if (rv != CKR_OK)
		return rv;
	CK_ULONG len = 0;
	rv = m_p11->C_Sign(m_hSesion, entrada.data(), (CK_ULONG)entrada.size(), NULL_PTR, &len);
	if (rv != CKR_OK)
		return rv;
	firma.resize(len);
	rv = m_p11->C_Sign(m_hSesion, entrada.data(), (CK_ULONG)entrada.size(), firma.data(), &len);
	if (rv == CKR_OK)
		firma.resize(len);
	return rv;
}

// Verificación en el equipo con la clave pública del certificado asociado (no depende de que la
// tarjeta exponga la clave pública). Si no hay certificado, se pide la verificación a la tarjeta.
//
// Verificar no necesita la clave privada ni el PIN: basta la clave pública, que está en el
// certificado. Por eso se hace con Windows (VerificarConCertificado) y no con la tarjeta.
CK_RV CTarjeta::Verificar(const std::string& etiquetaPublica, const Bytes& resumen, const Bytes& firma)
{
	Bytes cert;
	if (CertificadoDeClave(etiquetaPublica, cert) == CKR_OK)
		return VerificarConCertificado(cert, resumen, firma);

	CK_OBJECT_HANDLE h;
	CK_RV rv = BuscarObjeto(CKO_PUBLIC_KEY, etiquetaPublica, h);
	if (rv != CKR_OK)
		return rv;

	Bytes tipoB;
	rv = LeerAtributo(h, CKA_KEY_TYPE, tipoB);
	if (rv != CKR_OK)
		return rv;
	CK_KEY_TYPE tipo;
	memcpy(&tipo, tipoB.data(), sizeof(tipo));

	CK_MECHANISM mec = { (CK_MECHANISM_TYPE)(tipo == CKK_EC ? CKM_ECDSA : CKM_RSA_PKCS), NULL_PTR, 0 };
	Bytes entrada = (tipo == CKK_EC) ? resumen : DigestInfoSha256(resumen);

	rv = m_p11->C_VerifyInit(m_hSesion, &mec, h);
	if (rv != CKR_OK)
		return rv;
	return m_p11->C_Verify(m_hSesion, entrada.data(), (CK_ULONG)entrada.size(),
	                       (CK_BYTE_PTR)firma.data(), (CK_ULONG)firma.size());
}

// Certificado (DER) asociado a una clave (pública o privada) con esa etiqueta:
//   1) por el nombre: "KpuAutenticacionEC" / "KprivAutenticacionEC" -> "CertAutenticacion".
//      Va primero porque en el DNIe 4.0, sin PIN, el driver no deja consultar los objetos de clave
//      pública, pero sí leer los certificados.
//   2) por el mismo CKA_ID que la clave.
CK_RV CTarjeta::CertificadoDeClave(const std::string& etiquetaClave, Bytes& cert)
{
	cert.clear();

	std::string nucleo = Limpia(etiquetaClave);
	for (const char* pre : { "Kpriv", "Kpub", "Kpu" })
		if (nucleo.compare(0, strlen(pre), pre) == 0) { nucleo.erase(0, strlen(pre)); break; }
	for (const char* suf : { "EC", "RSA" })
	{
		size_t ls = strlen(suf);
		if (nucleo.size() > ls && nucleo.compare(nucleo.size() - ls, ls, suf) == 0) { nucleo.erase(nucleo.size() - ls); break; }
	}
	if (!nucleo.empty() && nucleo != Limpia(etiquetaClave))
	{
		if (ValorPorEtiqueta(CKO_CERTIFICATE, "Cert" + nucleo, CKA_VALUE, cert) == CKR_OK && !cert.empty())
			return CKR_OK;
	}
	cert.clear();

	CK_OBJECT_HANDLE h;
	CK_RV rv = BuscarObjeto(CKO_PUBLIC_KEY, etiquetaClave, h);
	if (rv != CKR_OK)
		rv = BuscarObjeto(CKO_PRIVATE_KEY, etiquetaClave, h);
	if (rv != CKR_OK)
		return rv;
	Bytes id;
	rv = LeerAtributo(h, CKA_ID, id);
	if (rv != CKR_OK || id.empty())
		return rv != CKR_OK ? rv : CKR_ATTRIBUTE_VALUE_INVALID;

	CK_OBJECT_CLASS clase = CKO_CERTIFICATE;
	CK_ATTRIBUTE plantilla[] = {
		{ CKA_CLASS, &clase, sizeof(clase) },
		{ CKA_ID, id.data(), (CK_ULONG)id.size() }
	};
	rv = m_p11->C_FindObjectsInit(m_hSesion, plantilla, 2);
	if (rv != CKR_OK)
		return rv;
	CK_OBJECT_HANDLE hc = CK_INVALID_HANDLE;
	CK_ULONG n = 0;
	rv = m_p11->C_FindObjects(m_hSesion, &hc, 1, &n);
	m_p11->C_FindObjectsFinal(m_hSesion);
	if (rv != CKR_OK)
		return rv;
	if (n != 1)
		return CKR_KEY_HANDLE_INVALID;
	return LeerAtributo(hc, CKA_VALUE, cert);
}

// Clave pública del certificado: EC -> punto sin comprimir (04||X||Y); RSA -> módulo y exponente
// (big-endian). Devuelve CKR_KEY_TYPE_INCONSISTENT si el tipo no coincide con el pedido.
//
// Un certificado X.509 lleva la clave pública en el campo SubjectPublicKeyInfo: un identificador
// de algoritmo (OID) y los bytes de la clave. CertCreateCertificateContext descodifica el
// certificado DER. En EC, los bytes ya son el punto 04||X||Y. En RSA son una estructura DER con
// módulo y exponente; CryptDecodeObjectEx la convierte al formato "blob" de CryptoAPI, que guarda
// el módulo al revés (el byte menos significativo primero), y por eso se le da la vuelta.
CK_RV ClavePublicaDeCertificado(const Bytes& cert, bool ec, Bytes& a, Bytes& b)
{
	a.clear(); b.clear();
	PCCERT_CONTEXT pc = CertCreateCertificateContext(X509_ASN_ENCODING, cert.data(), (DWORD)cert.size());
	if (!pc)
		return CKR_DATA_INVALID;
	CK_RV rv = CKR_OK;
	CERT_PUBLIC_KEY_INFO& spki = pc->pCertInfo->SubjectPublicKeyInfo;
	bool esEc = strcmp(spki.Algorithm.pszObjId, szOID_ECC_PUBLIC_KEY) == 0;
	if (esEc != ec)
		rv = CKR_KEY_TYPE_INCONSISTENT;
	else if (ec)
		a.assign(spki.PublicKey.pbData, spki.PublicKey.pbData + spki.PublicKey.cbData);
	else
	{
		BYTE* blob = NULL;
		DWORD len = 0;
		if (CryptDecodeObjectEx(X509_ASN_ENCODING, RSA_CSP_PUBLICKEYBLOB, spki.PublicKey.pbData, spki.PublicKey.cbData,
		                        CRYPT_DECODE_ALLOC_FLAG, NULL, &blob, &len))
		{
			RSAPUBKEY* rsa = (RSAPUBKEY*)(blob + sizeof(BLOBHEADER));
			BYTE* mod = (BYTE*)(rsa + 1);
			DWORD nb = rsa->bitlen / 8;
			a.assign(mod, mod + nb);
			std::reverse(a.begin(), a.end());              // el blob CSP es little-endian
			DWORD e = rsa->pubexp;
			while (e) { b.insert(b.begin(), (unsigned char)(e & 0xFF)); e >>= 8; }
			LocalFree(blob);
		}
		else
			rv = CKR_DATA_INVALID;
	}
	CertFreeCertificateContext(pc);
	return rv;
}

// Verificación con BCrypt (CNG):
//   1. CryptImportPublicKeyInfoEx2 convierte la clave pública del certificado en una clave CNG.
//   2. BCryptVerifySignature comprueba la firma sobre el resumen. En RSA hay que decirle el relleno
//      (PKCS#1 v1.5) y el algoritmo de resumen (SHA-256), que es lo que identifica el DigestInfo; en
//      ECDSA, la firma tiene que venir en bruto (r || s), que es como la devuelve la tarjeta.
//   3. Devuelve 0 si la firma es válida -> CKR_OK; si no, CKR_SIGNATURE_INVALID.
CK_RV CTarjeta::VerificarConCertificado(const Bytes& cert, const Bytes& resumen, const Bytes& firma)
{
	PCCERT_CONTEXT pc = CertCreateCertificateContext(X509_ASN_ENCODING, cert.data(), (DWORD)cert.size());
	if (!pc)
		return CKR_DATA_INVALID;
	BCRYPT_KEY_HANDLE hKey = NULL;
	CK_RV rv = CKR_FUNCTION_FAILED;
	CERT_PUBLIC_KEY_INFO& spki = pc->pCertInfo->SubjectPublicKeyInfo;
	if (CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, &spki, 0, NULL, &hKey))
	{
		bool ec = strcmp(spki.Algorithm.pszObjId, szOID_ECC_PUBLIC_KEY) == 0;
		BCRYPT_PKCS1_PADDING_INFO pad = { BCRYPT_SHA256_ALGORITHM };
		NTSTATUS st = BCryptVerifySignature(hKey, ec ? NULL : &pad, (PUCHAR)resumen.data(), (ULONG)resumen.size(),
		                                    (PUCHAR)firma.data(), (ULONG)firma.size(), ec ? 0 : BCRYPT_PAD_PKCS1);
		rv = (st == 0) ? CKR_OK : CKR_SIGNATURE_INVALID;
		BCryptDestroyKey(hKey);
	}
	CertFreeCertificateContext(pc);
	return rv;
}

// ---------------------------------------------------------------------------------------------

// SHA-256 con BCrypt: abrir el algoritmo, crear el objeto de resumen, pasarle los datos, obtener
// los 32 bytes del resultado y liberar todo.
Bytes Sha256(const Bytes& datos)
{
	Bytes resumen(32);
	BCRYPT_ALG_HANDLE hAlg = NULL;
	BCRYPT_HASH_HANDLE hHash = NULL;
	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0)
	{
		if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0)
		{
			BCryptHashData(hHash, (PUCHAR)datos.data(), (ULONG)datos.size(), 0);
			BCryptFinishHash(hHash, resumen.data(), (ULONG)resumen.size(), 0);
			BCryptDestroyHash(hHash);
		}
		BCryptCloseAlgorithmProvider(hAlg, 0);
	}
	return resumen;
}

// DigestInfo = SEQUENCE { algoritmo SHA-256 (OID 2.16.840.1.101.3.4.2.1), NULL }, OCTET STRING
// (32 bytes). La cabecera es siempre la misma (19 bytes) y detrás va el resumen.
Bytes DigestInfoSha256(const Bytes& resumen)
{
	static const unsigned char prefijo[] = {
		0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
		0x05, 0x00, 0x04, 0x20 };
	Bytes di(prefijo, prefijo + sizeof(prefijo));
	di.insert(di.end(), resumen.begin(), resumen.end());
	return di;
}
