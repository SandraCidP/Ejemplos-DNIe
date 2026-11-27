// ============================================================================
//  Module.h  -  Datos globales de la DLL, compartidos por todos los .cpp
// ============================================================================
#pragma once

#include <windows.h>

// "Asa" (handle) de nuestra DLL una vez cargada en memoria. Windows nos la
// entrega en DllMain. La necesitamos para dos cosas:
//   - averiguar la ruta de la DLL en disco (para registrarla y para cargar
//     la biblioteca de tipos incrustada), y
//   - cargar recursos incrustados, como el cuadro de diálogo del PIN.
extern HINSTANCE g_hInstance;

// Contador de "motivos para seguir cargada": objetos COM vivos + bloqueos
// pedidos por el cliente. Mientras sea mayor que cero, DllCanUnloadNow
// responde "no me descargues todavía". Ver DllMain.cpp.
extern LONG g_lockCount;
