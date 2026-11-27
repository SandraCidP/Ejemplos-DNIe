// =================================================================================================
//  ActiveXDNIe.cpp  -  Módulo ATL y funciones que exporta la DLL (servidor COM "en proceso")
// =================================================================================================
//
//  Una DLL COM exporta cuatro funciones que llama Windows, nunca la página (ver ActiveXDNIe.def):
//
//    DllRegisterServer     regsvr32 ActiveXDNIe_x64.dll      escribe en el registro el ProgID
//                                                            (FNMT.ActiveXDNIe), el CLSID y la ruta
//                                                            de la DLL (ficheros .rgs)
//    DllUnregisterServer   regsvr32 /u ActiveXDNIe_x64.dll   lo borra
//    DllGetClassObject     new ActiveXObject(...)            Windows pide la "fábrica" de la clase
//                                                            para crear un objeto CDLL_ADMIN
//    DllCanUnloadNow       de vez en cuando                  ¿queda algún objeto vivo? Si no, Windows
//                                                            puede descargar la DLL
//
//  Las cuatro se las da hechas ATL a través de _AtlModule (CAtlDllModuleT). Las clases que crea el
//  módulo se dan de alta con OBJECT_ENTRY_AUTO (al final de DLL_ADMIN.h).
//
//  Exporta además DllInstall, para registrar el control solo para el usuario actual, sin ser
//  administrador:
//
//    DllInstall            regsvr32 /n /i:user ActiveXDNIe_x64.dll      registra en HKEY_CURRENT_USER
//                          regsvr32 /u /n /i:user ActiveXDNIe_x64.dll   lo borra
// =================================================================================================
#include "stdafx.h"
#include "resource.h"
#include "ActiveXDNIe_i.h"
#include "DLL_ADMIN.h"

class CActiveXDNIeModule : public ATL::CAtlDllModuleT<CActiveXDNIeModule>
{
public:
	DECLARE_LIBID(LIBID_ActiveXDNIeLib)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_ACTIVEXDNIE, "{199E5536-EB92-405D-B739-4536052C17EE}")
};

CActiveXDNIeModule _AtlModule;

// DllMain la llama Windows al cargar y descargar la DLL. DisableThreadLibraryCalls evita que se la
// llame además cada vez que el proceso crea o termina un hilo, algo que no necesitamos.
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hInstance);
	return _AtlModule.DllMain(dwReason, lpReserved);
}

STDAPI DllCanUnloadNow(void)
{
	return _AtlModule.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer(void)
{
	return _AtlModule.DllRegisterServer();
}

STDAPI DllUnregisterServer(void)
{
	return _AtlModule.DllUnregisterServer();
}

// DllInstall la llama regsvr32 cuando se le pasa /i:<texto>; con /n no llama además a
// DllRegisterServer. Con el texto "user", AtlSetPerUserRegistration hace que ATL escriba las mismas
// claves (las de los ficheros .rgs y la biblioteca de tipos) en HKEY_CURRENT_USER\Software\Classes
// en lugar de en HKEY_LOCAL_MACHINE, que es lo que exige ser administrador. Sin "user", registra
// para todo el equipo, igual que DllRegisterServer.
//
// Ojo: un programa ejecutado "como administrador" no ve el registro por usuario; para usar el
// control desde programas elevados hay que registrarlo para todo el equipo (regsvr32 sin /n /i).
STDAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
	if (pszCmdLine != NULL && _wcsicmp(pszCmdLine, L"user") == 0)
		ATL::AtlSetPerUserRegistration(true);

	HRESULT hr;
	if (bInstall)
	{
		hr = DllRegisterServer();
		if (FAILED(hr))
			DllUnregisterServer();      // no dejar un registro a medias
	}
	else
	{
		hr = DllUnregisterServer();
	}
	return hr;
}
