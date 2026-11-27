// ============================================================================
//  Module.h  -  Datos globales del programa, compartidos por todos los .cpp
// ============================================================================
#pragma once

#include <windows.h>

// "Asa" (handle) de nuestro .exe una vez cargado en memoria. Windows nos la
// entrega en wWinMain (ver Main.cpp). PinDialog.cpp la necesita para cargar
// los recursos incrustados en el .exe: el cuadro de diálogo del PIN y la
// imagen del candado.
extern HINSTANCE g_hInstance;
