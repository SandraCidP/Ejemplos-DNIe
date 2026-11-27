// =================================================================================================
//  Dialogos.h  -  Ventanas del anfitrión (además del diálogo del PIN, PinDialog.cpp)
// =================================================================================================
//  Son ventanas de Windows, no de la página: la página web no puede verlas, rellenarlas ni
//  falsearlas. Todas dicen abajo qué página hace la petición (el origen que ha comprobado la
//  extensión).
//
//   - CrearVentanaDuena:  ventana invisible sobre la que se abren todas las demás.
//   - Confirmar:          pregunta Sí / No (permiso para leer los datos, confirmar una firma).
//   - ElegirArchivo:      el diálogo "Abrir" de Windows.
// =================================================================================================
#pragma once

#include <windows.h>
#include <string>

// Ventana "dueña" de los diálogos: invisible, siempre encima y centrada en la ventana que el
// usuario tiene delante (el navegador). Hay que destruirla con DestroyWindow al terminar.
HWND CrearVentanaDuena();

// Ventana de tareas de Windows (TaskDialog) con dos botones. Devuelve true si el usuario pulsa
// 'botonSi'. Esc, la X o 'botonNo' devuelven false. El botón por defecto es 'botonNo', para que una
// pulsación de Intro sin leer no dé el permiso.
//   instruccion  texto principal, en grande (la pregunta)
//   contenido    explicación debajo
//   origen       página que lo pide; se muestra al pie («Lo solicita: ...»)
bool Confirmar(HWND duena, const std::wstring& instruccion, const std::wstring& contenido,
               const std::wstring& origen, const wchar_t* botonSi, const wchar_t* botonNo);

// Diálogo "Abrir" de Windows. Devuelve la ruta elegida, o "" si el usuario cancela.
std::wstring ElegirArchivo(HWND duena, const wchar_t* titulo);
