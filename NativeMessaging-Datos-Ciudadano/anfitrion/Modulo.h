// =================================================================================================
//  Modulo.h  -  Datos del programa que comparten varios .cpp
// =================================================================================================
#pragma once

#include <windows.h>

// Instancia del .exe (la recibe wWinMain). Sirve para cargar los recursos de DNIeDatos.rc: el
// diálogo del PIN y su icono.
extern HINSTANCE g_hInstance;
