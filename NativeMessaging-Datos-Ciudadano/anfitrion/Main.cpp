// =================================================================================================
//  Main.cpp  -  Anfitrión de "Native Messaging" para el DNIe: punto de entrada y mensajes
// =================================================================================================
//
//  QUÉ ES ESTE PROGRAMA
//  --------------------
//  Una página web no puede usar el DNIe: el navegador no la deja salir de su "caja de arena". Lo
//  que sí permiten Chrome, Edge y Firefox es que una EXTENSIÓN arranque un programa instalado en el
//  equipo y hable con él. Ese mecanismo se llama Native Messaging, y el programa, "anfitrión
//  nativo". Este programa es ese anfitrión: lee los datos del DNIe y firma con él, a través del
//  driver PKCS#11 del DNIe instalado en Windows.
//
//    Página web  ──▶  Extensión  ──▶  DNIeDatos.exe  ──▶  Driver PKCS#11 del DNIe  ──▶  DNIe
//    (dniedatos.js)   (contenido.js   (este programa)     (DNIe_P11_x64.dll...)
//                      y fondo.js)
//
//  PIEZAS DEL PROGRAMA
//  -------------------
//    Main.cpp         este archivo: leer la petición del navegador y escribir la respuesta.
//    Operaciones.cpp  las operaciones (version, leer, firmar...).
//    Tarjeta.cpp      hablar con el driver PKCS#11 (la misma clase que el ActiveX del DNIe).
//    GruposDatos.cpp  interpretar los grupos de datos del DNIe (datos personales, MRZ, imágenes).
//    PinDialog.cpp    la ventana del PIN.
//    Dialogos.cpp     ventana dueña, confirmaciones y diálogo "Abrir".
//    Json.cpp         JSON mínimo.
//
//  CÓMO HABLA CON EL NAVEGADOR
//  ---------------------------
//  El navegador arranca el .exe y se comunica con él por la entrada y la salida estándar (stdin y
//  stdout), las mismas que usa un programa de consola. Cada mensaje, en los dos sentidos, tiene
//  este formato:
//
//      ┌───────────────────┬─────────────────────────────────────┐
//      │ 4 bytes: longitud │ texto JSON en UTF-8 (esa longitud)  │
//      └───────────────────┴─────────────────────────────────────┘
//
//  La longitud es un entero de 32 bits sin signo, en el orden de bytes del equipo (en todos los
//  Windows, "little-endian": el byte bajo primero). El navegador no admite respuestas de más de
//  1 MB; las nuestras ocupan como mucho unas decenas de KB (la foto y la firma manuscrita).
//
//  Quién puede arrancar este programa lo decide el navegador con el "manifiesto" que escribe
//  scripts\registrar.cmd: solo nuestra extensión.
// =================================================================================================
#include "stdafx.h"
#include "Modulo.h"
#include "Json.h"
#include "Operaciones.h"

#include <objbase.h>     // CoInitializeEx
#include <string>
#pragma comment(lib, "ole32.lib")

// Definición de la variable declarada en Modulo.h.
HINSTANCE g_hInstance = NULL;

// Tamaño máximo de una petición. La más grande es la de firmar un texto de 64 KB, que en JSON
// puede ocupar varias veces más (caracteres escapados). El límite evita reservar memoria de más
// si llega algo que no es una petición.
static const DWORD TAMANO_MAXIMO_PETICION = 1024 * 1024;

// =================================================================================================
//  Leer y escribir mensajes (el formato del navegador)
// =================================================================================================

// Lee exactamente 'cantidad' bytes. ReadFile puede entregar menos de los pedidos (los que haya en
// ese momento), así que hay que insistir.
static bool LeerBytes(HANDLE entrada, void* destino, DWORD cantidad)
{
	BYTE* p = (BYTE*)destino;
	while (cantidad > 0)
	{
		DWORD leidos = 0;
		if (!ReadFile(entrada, p, cantidad, &leidos, NULL) || leidos == 0)
			return false;      // error, o el navegador ha cerrado la conexión
		p += leidos;
		cantidad -= leidos;
	}
	return true;
}

static bool EscribirBytes(HANDLE salida, const void* origen, DWORD cantidad)
{
	const BYTE* p = (const BYTE*)origen;
	while (cantidad > 0)
	{
		DWORD escritos = 0;
		if (!WriteFile(salida, p, cantidad, &escritos, NULL) || escritos == 0)
			return false;
		p += escritos;
		cantidad -= escritos;
	}
	return true;
}

// Lee un mensaje completo: 4 bytes de longitud y después el JSON. Devuelve false cuando no hay más
// mensajes (el navegador cerró la conexión) o el mensaje no es válido.
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

// =================================================================================================
//  Punto de entrada
// =================================================================================================
//  wWinMain en vez de main: el programa es "de ventanas", no de consola. Así no aparece una ventana
//  negra cada vez que el navegador lo arranca. La entrada y la salida estándar funcionan igual: las
//  prepara el navegador y se obtienen con GetStdHandle.
int WINAPI wWinMain(HINSTANCE instancia, HINSTANCE, PWSTR, int)
{
	g_hInstance = instancia;

	const HANDLE entrada = GetStdHandle(STD_INPUT_HANDLE);
	const HANDLE salida = GetStdHandle(STD_OUTPUT_HANDLE);

	// Si alguien abre el .exe con doble clic no hay entrada ni salida estándar: no hay con quién
	// hablar. Se explica y se termina.
	if (entrada == NULL || entrada == INVALID_HANDLE_VALUE || salida == NULL || salida == INVALID_HANDLE_VALUE ||
	    GetFileType(entrada) == FILE_TYPE_CHAR || GetFileType(entrada) == FILE_TYPE_UNKNOWN)
	{
		MessageBoxW(NULL,
		            L"Este programa no se usa directamente: lo arranca el navegador cuando una página web "
		            L"pide leer los datos del DNIe o firmar con él.\n\n"
		            L"Para instalarlo, ejecute scripts\\registrar.cmd.",
		            L"DNI electrónico", MB_OK | MB_ICONINFORMATION);
		return 1;
	}

	// El diálogo "Abrir" de Windows usa COM: el hilo tiene que estar inicializado como "apartamento"
	// (STA), que es lo que esperan las ventanas.
	const HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	// Se atienden peticiones hasta que el navegador cierre la conexión. (La extensión abre una
	// conexión por petición, así que lo habitual es dar una sola vuelta.)
	std::string peticion;
	while (LeerMensaje(entrada, peticion))
	{
		ValorJson valor;
		std::string respuesta = AnalizarJson(peticion, valor) && valor.tipo == ValorJson::Objeto
		                            ? AtenderPeticion(valor)
		                            : RespuestaDeError("error", L"La petición no es un JSON válido.");
		// La petición puede llevar un texto para firmar: se borra al terminar con ella.
		if (!peticion.empty())
			SecureZeroMemory(&peticion[0], peticion.size());
		if (!EscribirMensaje(salida, respuesta))
			break;
	}

	if (SUCCEEDED(com))
		CoUninitialize();
	return 0;
}
