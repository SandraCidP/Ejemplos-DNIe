// =================================================================================================
//  cryptoki.h  -  Cabeceras PKCS#11 para Windows
// =================================================================================================
//  Las cabeceras estándar de PKCS#11 (carpeta pkcs11\: pkcs11.h, pkcs11t.h y pkcs11f.h, "derived
//  from the RSA Security Inc. PKCS #11 Cryptographic Token Interface (Cryptoki)") necesitan que
//  antes se definan unas macros que dependen de la plataforma. En Windows:
//    - las estructuras se empaquetan a 1 byte (#pragma pack), como las compila el driver;
//    - las funciones usan la convención de llamada __cdecl;
//    - CK_PTR es un puntero normal (*).
//  Este fichero define esas macros e incluye pkcs11.h. Es lo que hace cualquier programa PKCS#11
//  para Windows; el control solo usa las funciones a través de la tabla C_GetFunctionList.
// =================================================================================================
#pragma once

#pragma pack(push, cryptoki, 1)

// Vacío: el programa no enlaza con el driver (no usa su .lib); lo carga con LoadLibrary y llama a
// sus funciones a través de la tabla de C_GetFunctionList.
#define CK_IMPORT_SPEC
#define CK_EXPORT_SPEC CK_IMPORT_SPEC
#define CK_CALL_SPEC   __cdecl
#define CK_PTR         *

#define CK_DEFINE_FUNCTION(returnType, name) \
    returnType CK_EXPORT_SPEC CK_CALL_SPEC name
#define CK_DECLARE_FUNCTION(returnType, name) \
    returnType CK_EXPORT_SPEC CK_CALL_SPEC name
#define CK_DECLARE_FUNCTION_POINTER(returnType, name) \
    returnType CK_IMPORT_SPEC (CK_CALL_SPEC CK_PTR name)
#define CK_CALLBACK_FUNCTION(returnType, name) \
    returnType (CK_CALL_SPEC CK_PTR name)

#ifndef NULL_PTR
#define NULL_PTR 0
#endif

#include "pkcs11/pkcs11.h"

#pragma pack(pop, cryptoki)
