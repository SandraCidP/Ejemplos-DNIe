// ============================================================================
//  Signer.h  -  La clase COM "DNIeSigner": lo que ve la página web
// ============================================================================
//
//  QUÉ ES UN OBJETO COM, EN TRES IDEAS
//  -----------------------------------
//  1. Un objeto COM se usa solo a través de INTERFACES: listas de funciones
//     con un identificador único (IID). Quien lo usa no conoce la clase C++,
//     solo le pregunta "¿tienes la interfaz X?" con QueryInterface.
//
//  2. Su vida se controla contando referencias: cada usuario llama a AddRef
//     al empezar a usarlo y a Release al terminar. Cuando la cuenta llega a
//     cero, el objeto se destruye a sí mismo. Nadie hace "delete" desde fuera.
//
//  3. Todas las interfaces derivan de IUnknown, que tiene justo esas tres
//     funciones: QueryInterface, AddRef y Release.
//
//  INTERFACES QUE IMPLEMENTA ESTA CLASE
//  ------------------------------------
//    IDNIeSigner    La nuestra (definida en DNIeActiveX.idl): SignFile,
//                   VerifyFile, etc. Deriva de IDispatch.
//    IDispatch      "Llamadas por nombre". Es la única forma que tienen
//                   JavaScript y VBScript de usar un objeto: preguntan
//                   "¿qué número tiene el método 'SignFile'?" (GetIDsOfNames)
//                   y luego "ejecuta el método número N" (Invoke).
//    IObjectSafety  Con ella el control declara al navegador que es "seguro
//                   para scripting". Si un control no lo declara (por esta
//                   interfaz o por las categorías del registro que escribe
//                   DllMain.cpp), Internet Explorer no deja que el script de
//                   una página lo use, o muestra un aviso, según la
//                   configuración de seguridad de la zona. Una HTA o un
//                   script de Windows Script Host no hacen esta comprobación:
//                   allí el código ya es de plena confianza.
//
//  Normalmente todo esto lo genera ATL con macros. Aquí está escrito a mano,
//  sin ATL, precisamente para que se vea qué hay debajo.
//
//  SOBRE LAS MACROS DE LAS DECLARACIONES
//  -------------------------------------
//    STDMETHODIMP        equivale a "HRESULT __stdcall": el tipo de retorno
//                        y la convención de llamada que exige COM.
//    STDMETHODIMP_(T)    lo mismo, pero devolviendo el tipo T.
//    HRESULT             código de resultado de COM: S_OK es éxito; los
//                        E_... son errores.
// ============================================================================
#pragma once

#include <windows.h>
#include <objsafe.h>          // IObjectSafety
#include <string>

#include "DNIeActiveX_h.h"    // Generado por MIDL a partir de DNIeActiveX.idl

class DNIeSigner : public IDNIeSigner, public IObjectSafety
{
public:
    DNIeSigner();

    // ---- IUnknown ----------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ---- IDispatch ---------------------------------------------------------
    STDMETHODIMP GetTypeInfoCount(UINT* count) override;
    STDMETHODIMP GetTypeInfo(UINT index, LCID locale, ITypeInfo** typeInfo) override;
    STDMETHODIMP GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT nameCount,
                               LCID locale, DISPID* dispIds) override;
    STDMETHODIMP Invoke(DISPID dispId, REFIID iid, LCID locale, WORD flags,
                        DISPPARAMS* parameters, VARIANT* result,
                        EXCEPINFO* exceptionInfo, UINT* argumentError) override;

    // ---- IDNIeSigner (nuestros métodos; ver DNIeActiveX.idl) ---------------
    STDMETHODIMP SelectFile(BSTR title, BSTR* path) override;
    STDMETHODIMP SignFile(BSTR inputPath, BSTR signaturePath, VARIANT_BOOL* ok) override;
    STDMETHODIMP VerifyFile(BSTR inputPath, BSTR signaturePath, VARIANT_BOOL* valid) override;
    STDMETHODIMP get_LastError(BSTR* message) override;
    STDMETHODIMP get_SignerName(BSTR* name) override;
    STDMETHODIMP get_CertificateThumbprint(BSTR* thumbprint) override;
    STDMETHODIMP put_CertificateThumbprint(BSTR thumbprint) override;
    STDMETHODIMP get_CheckCertificateTrust(VARIANT_BOOL* check) override;
    STDMETHODIMP put_CheckCertificateTrust(VARIANT_BOOL check) override;
    STDMETHODIMP get_DnieVersion(BSTR* version) override;

    // ---- IObjectSafety -----------------------------------------------------
    STDMETHODIMP GetInterfaceSafetyOptions(REFIID iid, DWORD* supported, DWORD* enabled) override;
    STDMETHODIMP SetInterfaceSafetyOptions(REFIID iid, DWORD mask, DWORD enabled) override;

private:
    // Destructor privado: el objeto solo puede destruirse desde Release().
    ~DNIeSigner();

    // Carga (la primera vez) la descripción de IDNIeSigner desde la
    // biblioteca de tipos incrustada en la DLL.
    HRESULT EnsureTypeInfo();

    LONG referenceCount_;              // Cuántos usuarios tiene el objeto ahora.
    ITypeInfo* typeInfo_;              // Descripción de la interfaz (para IDispatch).

    std::wstring lastError_;           // Propiedad LastError.
    std::wstring signerName_;          // Propiedad SignerName.
    std::wstring thumbprint_;          // Propiedad CertificateThumbprint.
    bool checkTrust_;                  // Propiedad CheckCertificateTrust.
};
