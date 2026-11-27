// ============================================================================
//  Crypto.h  -  Firma y verificación (la parte "criptográfica" del control)
// ============================================================================
//
//  Este módulo NO sabe nada de COM ni de ActiveX: son funciones normales de
//  C++ que firman archivos, verifican firmas y dicen qué DNIe hay insertado.
//  Esta separación es intencionada:
//
//    - Signer.cpp  = la "fachada" COM (lo que ve JavaScript).
//    - Crypto.cpp  = el trabajo real con el DNIe.
//
//  Así, quien quiera reutilizar la firma en un programa de consola, un
//  servicio, etc., puede copiar Crypto.* y PinDialog.* sin nada de COM.
//  PinDialog necesita además resource.h, el diálogo IDD_PIN y el icono
//  IDI_PIN de DNIeActiveX.rc (con res\seguro.ico), y la variable g_hInstance
//  de Module.h, que en un .exe hay que definir y rellenar con
//  GetModuleHandleW(nullptr). Hay que enlazar con crypt32.lib, cryptui.lib,
//  ncrypt.lib y winscard.lib.
// ============================================================================
#pragma once

#include <windows.h>
#include <string>

// Firma el archivo 'inputPath' y escribe en 'signaturePath' una firma
// PKCS#7/CMS "separada" (detached): el archivo de firma NO contiene el
// documento, solo la firma, la hora de firma y los certificados del firmante
// y de las autoridades intermedias. Si el archivo de firma ya existe, se
// sobrescribe.
//
// Si la clave está en una tarjeta (el DNIe), pide el PIN con un diálogo.
//
//   thumbprint   Huella SHA-1 (hex) del certificado a usar. Si está vacía,
//                se busca automáticamente el certificado de FIRMA del DNIe.
//   ownerWindow  Ventana "padre" para los diálogos (PIN, selección de
//                certificado). Puede ser NULL.
//   signerName   [salida] Nombre del titular del certificado usado.
//   error        [salida] Explicación del fallo, si la función devuelve false.
//
// Devuelve true si la firma se ha generado y guardado correctamente.
bool SignFileDetached(const std::wstring& inputPath,
                      const std::wstring& signaturePath,
                      const std::wstring& thumbprint,
                      HWND ownerWindow,
                      std::wstring& signerName,
                      std::wstring& error);

// Comprueba que 'signaturePath' contiene una firma válida de 'inputPath'.
//
//   checkTrust   true  = además de la firma, exige que el certificado del
//                        firmante sea de confianza para Windows (que encadene
//                        con una autoridad raíz instalada y no esté caducado).
//                false = solo comprueba la firma matemáticamente.
//   signerName   [salida] Nombre del firmante (o firmantes, separados por ";").
//   error        [salida] Explicación del fallo, si la función devuelve false.
//
// Devuelve true si la firma es válida.
bool VerifyFileDetached(const std::wstring& inputPath,
                        const std::wstring& signaturePath,
                        bool checkTrust,
                        std::wstring& signerName,
                        std::wstring& error);

// Versión del DNIe que hay ahora mismo en algún lector del equipo: "3.0" o
// "4.0". Devuelve una cadena vacía si no hay ningún DNIe insertado o si su
// versión no se reconoce.
//
// Solo pregunta a Windows qué tarjeta hay en cada lector: no usa el
// certificado ni la clave, y no pide el PIN. Por eso se puede llamar en
// cualquier momento, también antes de firmar.
std::wstring GetInsertedDnieVersion();
