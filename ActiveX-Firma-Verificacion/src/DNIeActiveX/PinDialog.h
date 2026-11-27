// ============================================================================
//  PinDialog.h  -  Cuadro de diálogo para pedir el PIN del DNIe
// ============================================================================
#pragma once

#include <windows.h>
#include <string>

// Versión del DNIe para la que se pide el PIN. De ella dependen la longitud
// mínima del PIN y el texto del diálogo.
enum DnieVersion
{
    DNIE_UNKNOWN,   // No se ha podido saber la versión.
    DNIE_3,         // DNIe 3.0
    DNIE_4          // DNIe 4.0
};

// Longitud del PIN, en caracteres de cualquier tipo (letras, números,
// símbolos...):
//   - DNIe 3.0: de 8 a 16.
//   - DNIe 4.0: de 12 a 16.
// Si no se conoce la versión, el diálogo admite el intervalo completo (de 8
// a 16) y es la tarjeta quien decide si el PIN es correcto.
// Si cambia la norma, basta con tocar estas constantes. (Con un mínimo
// demasiado alto, el botón "Aceptar" no se activaría para un PIN válido más
// corto.)
const size_t PIN_MIN_LENGTH_DNIE3 = 8;
const size_t PIN_MIN_LENGTH_DNIE4 = 12;
const size_t PIN_MAX_LENGTH = 16;

// Muestra el diálogo y espera a que el usuario escriba el PIN.
//
//   ownerWindow  Ventana sobre la que aparece el diálogo (puede ser NULL).
//   version      Versión del DNIe: el diálogo la nombra en su texto y exige
//                la longitud mínima de PIN que le corresponde.
//   pin          [salida] El PIN escrito por el usuario.
//
// Devuelve true si el usuario pulsó "Aceptar"; false si canceló.
//
// SEGURIDAD: quien llame a esta función debe borrar 'pin' de la memoria en
// cuanto termine de usarlo (ver WipeString en Crypto.cpp).
bool AskForPin(HWND ownerWindow, DnieVersion version, std::wstring& pin);
