// =================================================================================================
//  stdafx.h  -  Cabeceras comunes (Windows y ATL) que incluyen todos los .cpp del proyecto
// =================================================================================================
//  _ATL_APARTMENT_THREADED       el objeto vive en un solo hilo (el del navegador o mshta).
//  _ATL_NO_AUTOMATIC_NAMESPACE   obliga a escribir ATL:: (o el "using" del final) explícitamente.
//  _ATL_CSTRING_EXPLICIT_CONSTRUCTORS  evita conversiones de texto implícitas y silenciosas.
// =================================================================================================
#pragma once

#ifndef STRICT
#define STRICT
#endif
#define _ATL_APARTMENT_THREADED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

// Estilos visuales de Windows (Common Controls 6) en las ventanas propias del control (PIN y permiso).
// Sin esto, una DLL cargada en Internet Explorer o mshta dibuja los botones y casillas con el estilo
// clásico. El enlazador incrusta esta dependencia en el manifiesto de la DLL (recurso 2) y
// EstilosVisuales (Dialogos.h) lo activa mientras se muestran las ventanas.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <sdkddkver.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

using namespace ATL;
