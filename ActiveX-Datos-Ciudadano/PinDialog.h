// ============================================================================
//  PinDialog.h  -  Cuadro de diálogo para pedir el PIN del DNIe
//
//
//  AskForPin muestra la ventana del PIN (recurso IDD_PIN de ActiveXDNIe.rc). Quien llama indica para
//  qué se pide (el texto «Introduzca el PIN de su DNIe para ...») y la longitud admitida.
// ============================================================================
#pragma once

#include <windows.h>
#include <string>

// Longitud del PIN por defecto, si no se puede consultar al driver. Quien llama pasa la que anuncia
// el driver en C_GetTokenInfo, ajustada a la del DNIe (ver abajo).
const size_t PIN_MIN_LENGTH = 4;
const size_t PIN_MAX_LENGTH = 16;

// Longitud del PIN del DNIe. AsegurarPin (DLL_ADMIN.cpp) ajusta a estos
// valores la longitud que anuncia el driver.
const size_t PIN_MIN_DNIE4 = 12;   // DNIe 4.0: la tarjeta admite de 12 a 16 caracteres
const size_t PIN_MIN_DNIE3 = 8;    // DNIe 3.0: la tarjeta admite de 4 a 16, pero la certificación exige un
                                   // mínimo de 8 (se recomiendan 12)
const size_t PIN_MAX_DNIE  = 16;

// Muestra el diálogo y espera a que el usuario escriba el PIN.
//
//   ownerWindow  Ventana sobre la que aparece el diálogo (puede ser NULL).
//   pin          [salida] El PIN escrito por el usuario.
//
// Devuelve true si el usuario pulsó "Aceptar"; false si canceló.
//
// SEGURIDAD: quien llame a esta función debe borrar 'pin' de la memoria en
// cuanto termine de usarlo (ver WipeString en Crypto.cpp).
bool AskForPin(HWND ownerWindow, std::wstring& pin);

// Igual, indicando para qué se pide (completa «Introduzca el PIN de su DNIe para ...:») y la
// longitud admitida.
bool AskForPin(HWND ownerWindow, const std::wstring& purpose, size_t minLength, size_t maxLength,
               std::wstring& pin);
