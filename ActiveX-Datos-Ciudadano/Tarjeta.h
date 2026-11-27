// =================================================================================================
//  Tarjeta.h  -  Acceso al DNIe a través del driver PKCS#11 de la FNMT-RCM (DNIe_P11*.dll)
// =================================================================================================
//
//  QUÉ HACE ESTA CLASE
//  -------------------
//  CTarjeta es la única parte del ActiveX que "habla" con el DNIe, y no lo hace directamente: se lo
//  pide al driver PKCS#11 del DNIe que ya está instalado en Windows. Así no se duplica el código del
//  driver (canal seguro del DNIe 4.0 con PACE/EAC2, ECDSA, lectura del CardSecurity...) y el ActiveX
//  mejora sola cada vez que se instala una versión nueva del driver.
//
//  Claves, certificados, PIN y firma, y también los datos personales, la foto, la firma manuscrita y
//  la MRZ, que el driver publica como objetos de datos (los grupos DG1, DG2, DG7, DG11 y DG13 del
//  estándar de documentos de viaje), se leen con esta misma clase; el contenido de los grupos lo
//  interpreta DLL_ADMIN.cpp.
//
//  PKCS#11 EN CINCO IDEAS
//  ----------------------
//  PKCS#11 (también llamado "Cryptoki") es la interfaz estándar de los drivers de tarjetas y otros
//  dispositivos criptográficos. Todas sus funciones empiezan por C_ y devuelven un CK_RV: CKR_OK (0)
//  si ha ido bien, o un código de error (CKR_PIN_INCORRECT, CKR_TOKEN_NOT_PRESENT...). Estos mismos
//  códigos son los que la página recibe en lastresultOut.
//
//   1. Driver:  una DLL que exporta C_GetFunctionList, que devuelve una tabla con punteros a todas
//               las demás funciones (C_Initialize, C_Login, C_Sign...). Se llaman como m_p11->C_xxx.
//   2. Slot:    un lector de tarjetas. "Token" es la tarjeta que hay en él.
//   3. Sesión:  una conexión abierta con el token (C_OpenSession). Todo se hace a través de ella.
//   4. Login:   presentar el PIN (C_Login). Algunas operaciones, como firmar, lo necesitan.
//   5. Objetos: lo que hay en la tarjeta (certificados, claves, datos). Cada uno tiene atributos:
//               CKA_CLASS (qué es), CKA_LABEL (nombre, p. ej. "CertFirmaDigital"), CKA_ID (lo que
//               enlaza un certificado con su clave), CKA_VALUE (el contenido)... Se buscan con
//               C_FindObjectsInit / C_FindObjects / C_FindObjectsFinal y se leen con
//               C_GetAttributeValue.
//
//  Las claves privadas nunca salen del chip: para firmar se le pide a la tarjeta que firme
//  (C_SignInit + C_Sign) y devuelve solo la firma.
//
//  ORDEN DE LECTURA RECOMENDADO
//  ----------------------------
//  CargarDriver -> Conectar -> PresentarPin -> BuscarObjeto / LeerAtributo -> Firmar -> Verificar,
//  y después Comprobar (detección de cambio de tarjeta) y ReiniciarTarjeta.
// =================================================================================================

#pragma once

#include <windows.h>
#include <string>
#include <vector>

#ifndef WIN32
#define WIN32   // cryptoki.h usa WIN32 para el empaquetado y las convenciones de llamada de Windows
#endif
#include "cryptoki.h"

#ifndef CKR_LIBRARY_LOAD_FAILED
#define CKR_LIBRARY_LOAD_FAILED 0x000001C2   // PKCS#11 v2.30; las cabeceras de pkcs11\ son v2.20
#endif

// Datos binarios (certificados, firmas, resúmenes, grupos de datos...)
typedef std::vector<unsigned char> Bytes;

class CTarjeta
{
public:
	CTarjeta();
	~CTarjeta();

	// Conexión
	CK_RV Conectar();                 // carga el driver, C_Initialize, busca la tarjeta y abre sesión
	CK_RV Desconectar();              // cierra sesión, C_Finalize y descarga el driver
	// Desconecta, reinicia la tarjeta en el lector (como sacarla y meterla) y vuelve a conectar.
	// Se usa para que el driver abra de nuevo el canal seguro antes de leer otro grupo de datos.
	// Tras reiniciar, la sesión ya no tiene el PIN presentado.
	CK_RV ReiniciarTarjeta();
	bool  Conectada() const { return m_hSesion != CK_INVALID_HANDLE; }
	// Comprueba en el lector (PC/SC) si se ha sacado o cambiado la tarjeta desde que se conectó. Si es
	// así, cierra la conexión (y con ella el PIN presentado) y conecta con la tarjeta que haya ahora,
	// devolviendo CKR_DEVICE_REMOVED una vez (la operación no se hace: hay que repetirla); sin
	// tarjeta devuelve CKR_TOKEN_NOT_PRESENT y conecta en cuanto se inserte una. Cada cambio aumenta
	// Generacion(), para que quien guarde datos de la tarjeta sepa que debe olvidarlos. Lo llaman
	// todas las operaciones.
	CK_RV Comprobar();
	unsigned Generacion() const { return m_generacion; }
	// Mientras está abierta una ventana del control (PIN o permiso), Windows sigue procesando
	// mensajes y la página podría volver a llamar al control. Durante ese tiempo la tarjeta se marca
	// como ocupada y cualquier operación con ella devuelve CKR_OPERATION_ACTIVE (0x90) sin hacer nada.
	void  MarcarOcupada(bool ocupada) { m_ocupada = ocupada; }
	bool  Ocupada() const { return m_ocupada; }
	bool  PinPresentado() const { return m_pinPresentado; }
	CK_RV PresentarPin(const std::string& pin);

	// Información del token
	CK_RV InfoToken(CK_TOKEN_INFO& info);

	// Objetos
	CK_RV Etiquetas(CK_OBJECT_CLASS clase, std::vector<std::string>& etiquetas);
	CK_RV ValorPorEtiqueta(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_ATTRIBUTE_TYPE atributo, Bytes& valor);
	CK_RV TipoClave(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_KEY_TYPE& tipo);

	// Criptografía (el resumen ya viene calculado: SHA-256 de los datos)
	CK_RV Firmar(const std::string& etiquetaPrivada, const Bytes& resumen, Bytes& firma);
	CK_RV Verificar(const std::string& etiquetaPublica, const Bytes& resumen, const Bytes& firma);

	// Certificado asociado a una clave (mismo CKA_ID)
	CK_RV CertificadoDeClave(const std::string& etiquetaClave, Bytes& cert);

	// Driver cargado (ruta y versión del fichero)
	std::wstring RutaDriver() const { return m_rutaDriver; }

private:
	CK_RV CargarDriver();
	void  DescargarDriver();
	CK_RV BuscarObjeto(CK_OBJECT_CLASS clase, const std::string& etiqueta, CK_OBJECT_HANDLE& h);
	CK_RV LeerAtributo(CK_OBJECT_HANDLE h, CK_ATTRIBUTE_TYPE atributo, Bytes& valor);
	void  Cerrar();                               // Desconectar sin dar por terminada la conexión
	bool  EventosLector(DWORD& eventos, bool& presente);   // contador de inserciones/extracciones
	CK_RV VerificarConCertificado(const Bytes& cert, const Bytes& resumen, const Bytes& firma);

	// Estado de la conexión. El ciclo es: driver cargado (m_hDriver, m_p11) -> C_Initialize hecho
	// (m_inicializado) -> sesión abierta en un slot (m_slot, m_hSesion) -> PIN presentado.
	HMODULE              m_hDriver;
	CK_FUNCTION_LIST_PTR m_p11;
	bool                 m_inicializado;
	CK_SLOT_ID           m_slot;
	CK_SESSION_HANDLE    m_hSesion;
	std::wstring         m_rutaDriver;
	std::string          m_lector;     // descripción del slot (nombre del lector PC/SC)
	bool                 m_pinPresentado;
	bool                 m_abierta;    // InicializarConexion sin FinalizarConexion: reconectar si cambia la tarjeta
	std::string          m_lectorPCSC; // nombre completo del lector en PC/SC
	DWORD                m_eventos;    // contador de eventos del lector al conectar
	bool                 m_hayEventos; // m_eventos es válido
	unsigned             m_generacion; // cambios de tarjeta detectados
	bool                 m_ocupada;    // hay una ventana del control abierta (ver MarcarOcupada)
};

// Utilidades
Bytes Sha256(const Bytes& datos);
Bytes DigestInfoSha256(const Bytes& resumen);     // prefijo DER de SHA-256 para firmas RSA PKCS#1 v1.5
// Clave pública de un certificado DER: EC -> a = punto 04||X||Y; RSA -> a = módulo, b = exponente
CK_RV ClavePublicaDeCertificado(const Bytes& cert, bool ec, Bytes& a, Bytes& b);
