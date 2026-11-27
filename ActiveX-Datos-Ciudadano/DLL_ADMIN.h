// =================================================================================================
//  DLL_ADMIN.h  -  El objeto COM que ve la página: new ActiveXObject("FNMT.ActiveXDNIe")
// =================================================================================================
//
//  Reproduce la interfaz del ActiveX publicado (ActiveXDNIe.dll 1.0.0.2): mismos nombres, mismos
//  números de método (DISPID) y mismos parámetros, para que las páginas que ya lo usan funcionen
//  cambiando solo el nombre con el que se crea.
//
//  CONVENCIONES HEREDADAS DEL PUBLICADO (ver el ejemplo de dnielectronico.es)
//  --------------------------------------------------------------------------
//  - Cada método deja su resultado en lastresultOut: 0 = correcto; otro valor = código de error
//    (códigos CKR_ de PKCS#11). Los métodos devuelven siempre S_OK para que el JavaScript no salte
//    por excepción y pueda consultar lastresultOut.
//  - Los datos binarios (certificados, módulo, número de serie, datos públicos...) se devuelven en un
//    BSTR con un carácter por byte (0..255). El ejemplo los pasa a hexadecimal con a2hex().
//  - Las etiquetas, el PIN y DatosIn se reciben como texto.
//  - Las entradas son propiedades "...In" que se rellenan ANTES de llamar al método, y las salidas
//    son propiedades "...Out" que se leen DESPUÉS. Por ejemplo, para firmar:
//        ax.EtiquetaKeyPrivIn = "KprivFirmaDigital";  ax.DatosIn = "texto";  ax.Firmar();
//        if (ax.lastresultOut == 0) firma = ax.FirmaOut;
//
//  QUÉ HACE ATL POR NOSOTROS
//  -------------------------
//  Un objeto COM necesita bastante código repetitivo: contar referencias (AddRef/Release), responder
//  a "¿tienes esta interfaz?" (QueryInterface), traducir las llamadas por nombre de JavaScript
//  (IDispatch), crearse cuando lo pide Windows (fábrica de clases) y escribir sus claves en el
//  registro. ATL (Active Template Library, de Visual Studio) lo da hecho mediante las clases de las
//  que deriva CDLL_ADMIN y unas macros:
//
//    CComObjectRootEx<CComSingleThreadModel>  Cuenta de referencias. "SingleThread" porque Internet
//                                             Explorer y mshta llaman siempre desde el mismo hilo.
//    CComCoClass<..., &CLSID_DLL_ADMIN>       La fábrica: Windows crea el objeto por su CLSID.
//    IDispatchImpl<IDLL_ADMIN, ...>           Llamadas por nombre desde JavaScript ("Firmar" ->
//                                             DISPID -> método C++), usando la biblioteca de tipos
//                                             generada a partir de ActiveXDNIe.idl.
//    IProvideClassInfo2Impl                   Permite al navegador consultar la biblioteca de tipos.
//    IObjectSafetyImpl                        Declara el control "seguro para scripting": sin esto,
//                                             Internet Explorer no deja que la página lo use. Es una
//                                             promesa del programador, no algo que Windows compruebe:
//                                             por eso los datos personales y el PIN se piden con
//                                             ventanas propias (ver AsegurarPin y PermisoDatos).
//    IObjectWithSiteImpl                      El navegador le pasa al control su "sitio", del que se
//                                             obtiene la dirección de la página (OrigenPagina).
//
//    BEGIN_COM_MAP ... END_COM_MAP            Las interfaces que responde QueryInterface.
//    DECLARE_REGISTRY_RESOURCEID              Las claves del registro, en DLL_ADMIN.rgs.
//    OBJECT_ENTRY_AUTO (al final)             Da de alta la clase en el módulo ATL (ActiveXDNIe.cpp).
//
//  CÓMO AÑADIR UN MÉTODO NUEVO
//  ---------------------------
//    1. Declararlo en ActiveXDNIe.idl, AL FINAL de la interfaz y con un id nuevo. Nunca cambiar los
//       id ni los parámetros de lo que ya existe: romperíamos las páginas que lo usan.
//    2. Declararlo aquí (STDMETHOD) e implementarlo en DLL_ADMIN.cpp terminando con
//       "return Resultado(rv);" para dejar el código en lastresultOut.
//    3. Recompilar: la biblioteca de tipos se regenera sola.
// =================================================================================================

#pragma once

#include "resource.h"
#include "ActiveXDNIe_i.h"
#include "Tarjeta.h"
#include <string>
#include <vector>
#include <map>

class ATL_NO_VTABLE CDLL_ADMIN :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CDLL_ADMIN, &CLSID_DLL_ADMIN>,
	public IDispatchImpl<IDLL_ADMIN, &IID_IDLL_ADMIN, &LIBID_ActiveXDNIeLib, 1, 0>,
	public IProvideClassInfo2Impl<&CLSID_DLL_ADMIN, NULL, &LIBID_ActiveXDNIeLib>,
	public IObjectSafetyImpl<CDLL_ADMIN, INTERFACESAFE_FOR_UNTRUSTED_CALLER | INTERFACESAFE_FOR_UNTRUSTED_DATA>,
	public IObjectWithSiteImpl<CDLL_ADMIN>   // para saber qué página usa el control (diálogos de PIN y permiso)
{
public:
	CDLL_ADMIN() : m_rv(0), m_padding(0), m_datosLeidos(0), m_permisoDatos(0), m_generacion(0) {}

	DECLARE_REGISTRY_RESOURCEID(IDR_DLL_ADMIN)

	BEGIN_COM_MAP(CDLL_ADMIN)
		COM_INTERFACE_ENTRY(IDLL_ADMIN)
		COM_INTERFACE_ENTRY(IDispatch)
		COM_INTERFACE_ENTRY(IProvideClassInfo)
		COM_INTERFACE_ENTRY(IProvideClassInfo2)
		COM_INTERFACE_ENTRY(IObjectSafety)
		COM_INTERFACE_ENTRY(IObjectWithSite)
	END_COM_MAP()

	// FinalConstruct se ejecuta al crear el objeto; FinalRelease, cuando la página lo suelta (al
	// cerrarla o recargarla): se cierra la conexión con la tarjeta y se borra el PIN de memoria.
	DECLARE_PROTECT_FINAL_CONSTRUCT()
	HRESULT FinalConstruct() { return S_OK; }
	void FinalRelease() { m_tarjeta.Desconectar(); BorrarPin(); BorrarResultados(); }

	// IDLL_ADMIN (mismo orden y DISPID que el ActiveX publicado)
	STDMETHOD(InicializarConexion)();
	STDMETHOD(FinalizarConexion)();
	STDMETHOD(ObtenerNumeroSerie)();
	STDMETHOD(get_NumeroSerieOut)(BSTR* pVal);
	STDMETHOD(get_ulNumeroSerieLen)(ULONG* pVal);
	STDMETHOD(get_lastresultOut)(int* pVal);
	STDMETHOD(put_EtiquetaKeyPrivIn)(BSTR newVal);
	STDMETHOD(get_DatosOut)(BSTR* pVal);
	STDMETHOD(put_DatosIn)(BSTR newVal);
	STDMETHOD(put_EtiquetaDatoIn)(BSTR newVal);
	STDMETHOD(ObtenerDatoPrivado)();
	STDMETHOD(ObtenerDatoPublico)();
	STDMETHOD(get_FirmaOut)(BSTR* pVal);
	STDMETHOD(Firmar)();
	STDMETHOD(put_EtiquetaCertificadoIn)(BSTR newVal);
	STDMETHOD(ObtenerCertificado)();
	STDMETHOD(put_paddingTypeIn)(BYTE newVal);
	STDMETHOD(pkcs1Padding)();
	STDMETHOD(pkcs1Unpadding)();
	STDMETHOD(Verificar)();
	STDMETHOD(put_ModuloIn)(BSTR newVal);
	STDMETHOD(get_ModuloOut)(BSTR* pVal);
	STDMETHOD(put_ExponenteIn)(BSTR newVal);
	STDMETHOD(get_ExponenteOut)(BSTR* pVal);
	STDMETHOD(ObtenerClavePublicaRSA)();
	STDMETHOD(get_ArrayEtiquetasLenOut)(int* pVal);
	STDMETHOD(get_EtiquetaOut)(int i, BSTR* pVal);
	STDMETHOD(ObtenerEtiquetasDatosPublicos)();
	STDMETHOD(ObtenerEtiquetasDatosPrivados)();
	STDMETHOD(ObtenerEtiquetasClavesPrivadas)();
	STDMETHOD(ObtenerEtiquetasClavesPublicas)();
	STDMETHOD(ObtenerEtiquetasTodosCertificados)();
	STDMETHOD(Hash_Init)();
	STDMETHOD(get_hashOut)(BSTR* pVal);
	STDMETHOD(Hash)();
	STDMETHOD(Hash_Final)();
	STDMETHOD(Hash_Update)();
	STDMETHOD(ObtenerInfoBiometrica)();
	STDMETHOD(put_FirmaIn)(BSTR newVal);
	STDMETHOD(put_BITIn)(BSTR newVal);
	STDMETHOD(ObtenerNumeroSerie_Soporte)();
	STDMETHOD(PresentarPin)();
	STDMETHOD(put_PinIn)(BSTR newVal);
	STDMETHOD(put_EtiquetaKeyPubIn)(BSTR newVal);
	STDMETHOD(ObtenerClavePublicaRSAFirmada)();
	STDMETHOD(get_CertComponenteOut)(BSTR* pVal);
	STDMETHOD(get_BITOut)(BSTR* pVal);
	STDMETHOD(get_TipoBiometriaOut)(BYTE* pVal);
	STDMETHOD(ObtenerCertCAComponente)();
	STDMETHOD(put_hashIn)(BSTR newVal);
	STDMETHOD(ObtenerVersionDNIe)();
	STDMETHOD(ObtenerVersionesLibrerias)();
	STDMETHOD(get_ArrayVersionLibreriasLenOut)(int* pVal);
	STDMETHOD(get_VersionLibreriaOut)(int i, BSTR* pVal);
	STDMETHOD(get_NombreLibreriaOut)(int i, BSTR* pVal);
	STDMETHOD(ObtenerDisplayMessage)();
	STDMETHOD(ObtenerCAN)();
	STDMETHOD(ObtenerMRZ)();
	STDMETHOD(ObtenerClavePublicaEC)();
	STDMETHOD(ObtenerClavePublicaECFirmada)();
	STDMETHOD(ObtenerTipoDocumento)();
	STDMETHOD(ObtenerCertificadoCV_CA)();
	STDMETHOD(put_CANLecturaIn)(BSTR newVal);
	STDMETHOD(get_NombreOut)(BSTR* pVal);
	STDMETHOD(get_Apellido1Out)(BSTR* pVal);
	STDMETHOD(get_Apellido2Out)(BSTR* pVal);
	STDMETHOD(get_NumeroDNIOut)(BSTR* pVal);
	STDMETHOD(get_FechaNacimientoOut)(BSTR* pVal);
	STDMETHOD(get_NacionalidadOut)(BSTR* pVal);
	STDMETHOD(get_FechaExpiracionOut)(BSTR* pVal);
	STDMETHOD(get_SexoOut)(BSTR* pVal);
	STDMETHOD(get_LocalidadNacimientoOut)(BSTR* pVal);
	STDMETHOD(get_ProvinciaNacimientoOut)(BSTR* pVal);
	STDMETHOD(get_NombrePadresOut)(BSTR* pVal);
	STDMETHOD(get_DomicilioOut)(BSTR* pVal);
	STDMETHOD(get_LocalidadDomicilioOut)(BSTR* pVal);
	STDMETHOD(get_ProvinciaDomicilioOut)(BSTR* pVal);
	STDMETHOD(get_ImagenFacialOut)(BSTR* pVal);
	STDMETHOD(get_ImagenFirmaOut)(BSTR* pVal);

private:
	// Todos los métodos terminan con "return Resultado(rv)": guarda el código para lastresultOut y
	// devuelve S_OK, para que JavaScript no reciba una excepción (convención del publicado).
	HRESULT Resultado(CK_RV rv) { m_rv = (int)rv; return S_OK; }
	HRESULT NoDisponible() { return Resultado(CKR_FUNCTION_NOT_SUPPORTED); }
	HRESULT Etiquetas(CK_OBJECT_CLASS clase, int privados);   // privados: -1 todos, 0 públicos, 1 privados
	void BorrarPin();
	void BorrarResultados();     // entradas y salidas de la última operación, sobrescritas con ceros
	HRESULT ResumenAFirmar(Bytes& resumen);                 // hashIn (32 bytes) o SHA-256(DatosIn)

	// Grupos de datos del estándar de documentos de viaje (DG1, DG2, DG7, DG11, DG13)
	CK_RV LeerDato(const std::string& etiqueta, Bytes& valor); // objeto de datos, reintentando una vez
	CK_RV Grupo(const char* dg, Bytes& valor);               // grupo de datos, guardado en caché
	CK_RV CampoDG13(int indice, std::string& valor);         // campo de texto (UTF-8) del DG13
	HRESULT DevolverCampo(int indice, BSTR* pVal);           // getter de datos personales
	HRESULT DevolverImagen(const char* dg, unsigned tag, BSTR* pVal);
	void OlvidarGrupos();
	CK_RV RevisarTarjeta();      // olvida lo de la tarjeta anterior si se ha sacado o cambiado
	bool EsDNIeRSA();                                        // DNIe 3.0 (RSA) o DNIe 4.0 (EC)

	// PIN y permiso pedidos al usuario con ventanas propias del ActiveX (Dialogos.cpp)
	CK_RV AsegurarPin(const wchar_t* para);                  // pide el PIN si aún no se ha presentado
	CK_RV PermisoDatos();                                    // pide permiso para leer los datos personales
	std::wstring OrigenPagina();                              // dirección de la página que usa el control
	static std::wstring OrigenLegible(const std::wstring& url); // sitio o fichero, en corto
	HWND VentanaPagina();                                    // ventana sobre la que mostrar los diálogos

	CTarjeta m_tarjeta;
	int      m_rv;

	// Propiedades de entrada
	std::string m_etiquetaKeyPriv, m_etiquetaKeyPub, m_etiquetaDato, m_etiquetaCert, m_pin, m_can;
	Bytes       m_datosIn, m_modulo, m_exponente, m_hash, m_hashIn, m_bit;
	BYTE        m_padding;

	// Propiedades de salida
	Bytes       m_datosOut, m_firma, m_numSerie, m_certComponente, m_acumuladoHash;
	std::vector<std::string> m_etiquetas;
	std::vector<std::wstring> m_nombresLib, m_versionesLib;

	// Grupos de datos ya leídos en esta conexión (se borran al conectar y al desconectar)
	std::map<std::string, Bytes> m_grupos;
	int m_datosLeidos;   // objetos de datos leídos desde la última conexión o reinicio de la tarjeta
	unsigned m_generacion;  // cambios de tarjeta ya atendidos (CTarjeta::Generacion)
	int m_permisoDatos;  // permiso del usuario para leer sus datos personales en esta conexión:
	                     // 0 = no preguntado, 1 = permitido, -1 = denegado
};

OBJECT_ENTRY_AUTO(__uuidof(DLL_ADMIN), CDLL_ADMIN)
