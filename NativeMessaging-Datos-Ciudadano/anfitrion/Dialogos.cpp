// =================================================================================================
//  Dialogos.cpp  -  Ventana dueña, confirmaciones y diálogo "Abrir"
// =================================================================================================
//  (El PIN se pide en PinDialog.cpp.)
// =================================================================================================
#include "stdafx.h"
#include "Dialogos.h"
#include "Modulo.h"

#include <commctrl.h>    // TaskDialogIndirect
#include <commdlg.h>     // GetOpenFileNameW
#include <vector>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

static const wchar_t* TITULO_VENTANAS = L"DNI electrónico";

// -------------------------------------------------------------------------------------------------
//  Ventana dueña
// -------------------------------------------------------------------------------------------------
// El problema: este programa no tiene ventana, y quien está delante es el navegador. Un diálogo
// sin dueña puede abrirse DETRÁS del navegador, y el usuario no lo vería.
//
// La solución: una ventana propia, sin tamaño (no se ve), marcada como "siempre visible"
// (WS_EX_TOPMOST). Windows mantiene los diálogos por encima de su dueña, así que quedan también por
// encima del navegador. Se coloca en el centro de la ventana que el usuario tiene delante para que
// los diálogos salgan en ese mismo monitor.
//
// ¿Por qué no usar como dueña la ventana del navegador? Porque un diálogo modal desactiva a su
// dueña mientras está abierto. Si el navegador cerrase este programa a mitad de un diálogo, su
// ventana podría quedarse desactivada.
//
//   WS_EX_TOOLWINDOW  no aparece en la barra de tareas.
//   "STATIC"          clase de ventana que ya trae Windows: no hay que registrar una propia.
HWND CrearVentanaDuena()
{
	int x = 0, y = 0;
	RECT marco = {};
	HWND delante = GetForegroundWindow();
	if (delante && GetWindowRect(delante, &marco))
	{
		x = (marco.left + marco.right) / 2;
		y = (marco.top + marco.bottom) / 2;
	}
	HWND ventana = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", TITULO_VENTANAS,
	                               WS_POPUP | WS_VISIBLE, x, y, 0, 0, NULL, NULL, g_hInstance, NULL);
	if (ventana)
		SetForegroundWindow(ventana);      // que el teclado vaya a nuestros diálogos
	return ventana;
}

// -------------------------------------------------------------------------------------------------
//  Confirmaciones
// -------------------------------------------------------------------------------------------------
// TaskDialog es la ventana de tareas de Windows: texto principal, explicación, botones con texto
// propio («Permitir», «Firmar»...) y un pie con icono. Existe a partir de la versión 6 de los
// controles comunes, que el .exe pide en su manifiesto (stdafx.h).
bool Confirmar(HWND duena, const std::wstring& instruccion, const std::wstring& contenido,
               const std::wstring& origen, const wchar_t* botonSi, const wchar_t* botonNo)
{
	const int SI = 100, NO = 101;
	TASKDIALOG_BUTTON botones[] = { { SI, botonSi }, { NO, botonNo } };
	std::wstring pie = origen.empty() ? std::wstring(L"Lo solicita una aplicación de este equipo.")
	                                  : L"Lo solicita: " + origen;

	TASKDIALOGCONFIG tc = { sizeof(tc) };
	tc.hwndParent = duena;
	tc.hInstance = g_hInstance;
	tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
	tc.pszWindowTitle = TITULO_VENTANAS;
	tc.pszMainIcon = TD_WARNING_ICON;
	tc.pszMainInstruction = instruccion.c_str();
	tc.pszContent = contenido.c_str();
	tc.pszFooter = pie.c_str();
	tc.pszFooterIcon = TD_INFORMATION_ICON;
	tc.cButtons = ARRAYSIZE(botones);
	tc.pButtons = botones;
	tc.nDefaultButton = NO;

	int pulsado = 0;
	if (SUCCEEDED(TaskDialogIndirect(&tc, &pulsado, NULL, NULL)))
		return pulsado == SI;                // Esc o la X cuentan como «No»

	// Si TaskDialog fallara (no debería en Windows 10/11), un MessageBox con el mismo texto.
	std::wstring texto = instruccion + L"\r\n\r\n" + contenido + L"\r\n\r\n" + pie;
	return MessageBoxW(duena, texto.c_str(), TITULO_VENTANAS,
	                   MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST) == IDYES;
}

// -------------------------------------------------------------------------------------------------
//  Diálogo "Abrir"
// -------------------------------------------------------------------------------------------------
// Windows coloca el diálogo "Abrir" con su esquina superior izquierda en la ventana dueña. Como la
// dueña es un punto en el centro del navegador, el diálogo saldría desplazado hacia abajo a la
// derecha, y con la escala de Windows alta incluso fuera de la pantalla. Para centrarlo se usa un
// "gancho" (hook) CBT de este hilo: Windows nos avisa justo antes de activar el diálogo, ya con su
// tamaño, y lo movemos para que su centro coincida con la dueña, sin salirse del monitor.
// Así se conserva el diálogo moderno (un gancho del propio GetOpenFileNameW lo cambiaría por el
// antiguo).
static HHOOK g_ganchoCentrar = NULL;
static HWND g_duenaCentrar = NULL;

static LRESULT CALLBACK CentrarAlActivar(int codigo, WPARAM wParam, LPARAM lParam)
{
	if (codigo == HCBT_ACTIVATE)
	{
		HWND dialogo = (HWND)wParam;
		RECT d, o;
		if (GetWindow(dialogo, GW_OWNER) == g_duenaCentrar && GetWindowRect(dialogo, &d) && GetWindowRect(g_duenaCentrar, &o))
		{
			const int ancho = d.right - d.left, alto = d.bottom - d.top;
			int x = o.left - ancho / 2, y = o.top - alto / 2;
			MONITORINFO mi = { sizeof(mi) };
			if (GetMonitorInfoW(MonitorFromWindow(g_duenaCentrar, MONITOR_DEFAULTTONEAREST), &mi))
			{
				if (x + ancho > mi.rcWork.right) x = mi.rcWork.right - ancho;
				if (y + alto > mi.rcWork.bottom) y = mi.rcWork.bottom - alto;
				if (x < mi.rcWork.left) x = mi.rcWork.left;
				if (y < mi.rcWork.top) y = mi.rcWork.top;
			}
			SetWindowPos(dialogo, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			UnhookWindowsHookEx(g_ganchoCentrar);   // basta con la primera vez
			g_ganchoCentrar = NULL;
			return 0;
		}
	}
	return CallNextHookEx(NULL, codigo, wParam, lParam);
}

// El archivo lo elige SIEMPRE el usuario: la página no puede indicar una ruta, y a la página solo
// se le devuelve el NOMBRE del archivo, nunca la carpeta.
std::wstring ElegirArchivo(HWND duena, const wchar_t* titulo)
{
	// Búfer amplio: las rutas de Windows pueden superar los 260 caracteres clásicos (MAX_PATH).
	std::vector<wchar_t> ruta(4096, L'\0');

	OPENFILENAMEW of = { sizeof(of) };
	of.hwndOwner = duena;
	of.lpstrFilter = L"Todos los archivos (*.*)\0*.*\0";
	of.lpstrFile = ruta.data();
	of.nMaxFile = (DWORD)ruta.size();
	of.lpstrTitle = titulo;
	//   OFN_EXPLORER          diálogo de estilo moderno (el del Explorador).
	//   OFN_FILEMUSTEXIST /
	//   OFN_PATHMUSTEXIST     no aceptar un nombre o una carpeta que no existan.
	//   OFN_HIDEREADONLY      ocultar la casilla "Abrir como solo lectura".
	//   OFN_NOCHANGEDIR       no cambiar el directorio de trabajo del programa.
	of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

	g_duenaCentrar = duena;
	g_ganchoCentrar = duena ? SetWindowsHookExW(WH_CBT, CentrarAlActivar, NULL, GetCurrentThreadId()) : NULL;
	const BOOL elegido = GetOpenFileNameW(&of);
	if (g_ganchoCentrar)
	{
		UnhookWindowsHookEx(g_ganchoCentrar);
		g_ganchoCentrar = NULL;
	}
	if (!elegido)
		return std::wstring();               // cancelado (o error): nada
	return std::wstring(ruta.data());
}
