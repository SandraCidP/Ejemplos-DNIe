// =================================================================================================
//  Dialogos.cpp  -  Aviso de permiso para los datos personales y estilos visuales de las ventanas
// =================================================================================================
//  (El PIN se pide en PinDialog.cpp.)
// =================================================================================================

#include "stdafx.h"
#include "Dialogos.h"
#include <commctrl.h>

namespace
{
	std::wstring LineaOrigen(const std::wstring& origen)
	{
		return origen.empty() ? std::wstring(L"Lo solicita una aplicación de este equipo.")
		                      : L"Lo solicita: " + origen;
	}
}

// Contexto de activación con el manifiesto de esta DLL (recurso 2). Se crea una vez.
//
// QUÉ ES UN CONTEXTO DE ACTIVACIÓN
// Windows tiene dos versiones de los controles comunes (botones, casillas...): la 5, con el aspecto
// clásico, y la 6, con el de la versión de Windows instalada. Un programa elige la 6 declarándolo en
// su "manifiesto" (un XML incrustado como recurso). Nuestra DLL no es el programa: el programa es
// mshta.exe o el navegador, que no lo declaran. Por eso la DLL lleva su propio manifiesto (el
// enlazador lo incrusta como recurso 2, ver stdafx.h) y lo "activa" solo mientras muestra sus
// ventanas: CreateActCtx lo carga y ActivateActCtx / DeactivateActCtx lo encienden y apagan para el
// hilo actual. Así las ventanas del control se ven modernas sin cambiar nada del navegador.
static HANDLE ContextoDll()
{
	static HANDLE contexto = INVALID_HANDLE_VALUE;
	static bool creado = false;
	if (!creado)
	{
		creado = true;
		wchar_t ruta[MAX_PATH] = {};
		HMODULE modulo = _AtlBaseModule.GetModuleInstance();
		if (GetModuleFileNameW(modulo, ruta, MAX_PATH))
		{
			ACTCTXW ac = { sizeof(ac) };
			ac.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
			ac.lpSource = ruta;
			ac.hModule = modulo;
			ac.lpResourceName = MAKEINTRESOURCEW(2);   // ISOLATIONAWARE_MANIFEST_RESOURCE_ID
			contexto = CreateActCtxW(&ac);
		}
	}
	return contexto;
}

// Patrón RAII: el constructor activa y el destructor desactiva, así se desactiva siempre al salir
// del bloque, aunque se salga por un return. Uso:  { EstilosVisuales estilos; ...ventana...; }
EstilosVisuales::EstilosVisuales() : m_cookie(0), m_activo(false)
{
	HANDLE contexto = ContextoDll();
	if (contexto != INVALID_HANDLE_VALUE)
		m_activo = ActivateActCtx(contexto, &m_cookie) != FALSE;
}

EstilosVisuales::~EstilosVisuales()
{
	if (m_activo)
		DeactivateActCtx(0, m_cookie);
}

// Con los estilos visuales activos (EstilosVisuales) se usa la ventana de tareas de Windows
// (TaskDialog): texto principal, detalle, botones «Permitir» / «No permitir» y la página que lo pide
// abajo. Si no está disponible, un MessageBox con el mismo texto.
//
// TaskDialogIndirect solo existe en la versión 6 de los controles. Si se enlazara directamente, la
// DLL no cargaría en un equipo donde no estuviera; por eso se busca en tiempo de ejecución
// (LoadLibrary + GetProcAddress) y, si no aparece, se usa MessageBox.
typedef HRESULT (WINAPI* TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

bool PedirPermisoDatos(HWND propietario, const std::wstring& origen)
{
	const wchar_t* titulo = L"DNI electrónico - Datos personales";
	const wchar_t* detalle =
		L"Se van a leer de su DNIe: nombre y apellidos, número de DNI, fecha y lugar de nacimiento, "
		L"domicilio, nombre de los padres, fotografía e imagen de la firma manuscrita.";
	std::wstring pie = LineaOrigen(origen);

	HMODULE comctl = LoadLibraryW(L"comctl32.dll");   // con el contexto activo, la versión 6
	TaskDialogIndirectFn taskDialog = comctl ? (TaskDialogIndirectFn)GetProcAddress(comctl, "TaskDialogIndirect") : NULL;
	if (taskDialog)
	{
		const int PERMITIR = 100, NO_PERMITIR = 101;
		TASKDIALOG_BUTTON botones[] = { { PERMITIR, L"Permitir" }, { NO_PERMITIR, L"No permitir" } };
		TASKDIALOGCONFIG tc = { sizeof(tc) };
		tc.hwndParent = propietario;
		tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (propietario ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
		tc.pszWindowTitle = titulo;
		tc.pszMainIcon = TD_WARNING_ICON;
		tc.pszMainInstruction = L"¿Permite leer sus datos personales?";
		tc.pszContent = detalle;
		tc.pszFooter = pie.c_str();
		tc.pszFooterIcon = TD_INFORMATION_ICON;
		tc.cButtons = ARRAYSIZE(botones);
		tc.pButtons = botones;
		tc.nDefaultButton = NO_PERMITIR;
		int pulsado = 0;
		HRESULT hr = taskDialog(&tc, &pulsado, NULL, NULL);
		FreeLibrary(comctl);
		if (SUCCEEDED(hr))
			return pulsado == PERMITIR;   // Esc o la X cuentan como «No permitir»
	}
	else if (comctl)
		FreeLibrary(comctl);

	std::wstring texto = std::wstring(detalle) + L"\r\n\r\n" + pie + L"\r\n\r\n¿Permite leer estos datos?";
	int r = MessageBoxW(propietario, texto.c_str(), titulo,
	                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST);
	return r == IDYES;
}
