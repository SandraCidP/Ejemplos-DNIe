// =================================================================================================
//  Dialogos.h  -  Ventanas propias del ActiveX (además del diálogo del PIN, PinDialog.cpp)
// =================================================================================================
//  Son ventanas de Windows, no de la página: la página web no puede verlas, rellenarlas ni falsearlas.
//   - PedirPermisoDatos: pide permiso al usuario antes de entregar a una página sus datos
//     personales (DNIe 4.0, que los da sin PIN).
//   - EstilosVisuales:   hace que esas ventanas usen el aspecto actual de Windows.
// =================================================================================================

#pragma once

#include <windows.h>
#include <string>

// El PIN se pide con AskForPin (PinDialog.h).

// Mientras existe, las ventanas que se crean usan los estilos visuales de Windows (Common Controls 6,
// según el manifiesto de esta DLL), sin cambiar nada de la página ni del proceso que la muestra.
class EstilosVisuales
{
public:
	EstilosVisuales();
	~EstilosVisuales();
private:
	ULONG_PTR m_cookie;
	bool      m_activo;
};

// Pregunta si se permite a 'origen' leer los datos personales del DNIe. true = permitido.
bool PedirPermisoDatos(HWND propietario, const std::wstring& origen);
