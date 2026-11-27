// =================================================================================================
//  stdafx.h  -  Cabeceras comunes de Windows que incluyen todos los .cpp del anfitrión
// =================================================================================================
//  A diferencia del ActiveX, el anfitrión es un programa (.exe) normal: no usa COM ni ATL.
// =================================================================================================
#pragma once

#ifndef STRICT
#define STRICT
#endif
#ifndef NOMINMAX
#define NOMINMAX            // que windows.h no defina min y max (chocan con std::min / std::max)
#endif
#define WIN32_LEAN_AND_MEAN

// Estilos visuales de Windows (Common Controls 6): botones, casillas y ventanas de tareas con el
// aspecto actual. El enlazador incrusta esta dependencia en el manifiesto del .exe. Sin esta línea,
// los diálogos se dibujarían con el aspecto de Windows 95 y no existiría TaskDialogIndirect.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <sdkddkver.h>
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
