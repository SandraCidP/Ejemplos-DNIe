// ============================================================================
//  prueba.js  -  Pruebas automáticas del control (se ejecutan con cscript)
// ============================================================================
//  No ejecutar directamente: lo lanza tests\ejecutar-pruebas.ps1, que antes
//  crea un certificado de prueba y le pasa aquí su huella.
//
//  Uso:  cscript //nologo prueba.js <carpetaTemporal> <huellaCertificado> <hayDnie>
//        (hayDnie: "si" o "no"; ver la prueba 8)
//
//  Además de probar, este archivo sirve de EJEMPLO de uso del control desde
//  Windows Script Host: se usa igual que desde una página web.
// ============================================================================
var carpeta = WScript.Arguments(0);
var huella = WScript.Arguments(1);
var hayDnie = (WScript.Arguments(2) == "si");

var fso = new ActiveXObject("Scripting.FileSystemObject");
var firmador = new ActiveXObject("DNIeActiveX.Signer");

var fallos = 0;

// Anota el resultado de una comprobación.
function comprobar(descripcion, condicion, detalle) {
    if (condicion) {
        WScript.Echo("  OK     " + descripcion);
    } else {
        fallos++;
        WScript.Echo("  FALLO  " + descripcion + (detalle ? "  -> " + detalle : ""));
    }
}

function escribirArchivo(ruta, texto) {
    var archivo = fso.CreateTextFile(ruta, true);
    archivo.Write(texto);
    archivo.Close();
}

// --- Preparación -------------------------------------------------------------
var documento = carpeta + "\\documento.txt";
var firma = documento + ".p7s";
escribirArchivo(documento, "Este es el documento que se va a firmar.\r\n");

// Firmamos con el certificado de prueba (no con un DNIe), y como es
// autofirmado no puede ser "de confianza": desactivamos esa comprobación.
firmador.CertificateThumbprint = huella;
firmador.CheckCertificateTrust = false;

// --- 1. Firmar ---------------------------------------------------------------
var ok = firmador.SignFile(documento, "");
comprobar("SignFile firma el documento", ok, firmador.LastError);
comprobar("SignFile crea el archivo .p7s", fso.FileExists(firma));
comprobar("SignerName contiene el titular", firmador.SignerName == "Prueba firma ActiveX", firmador.SignerName);

// --- 2. Verificar el documento intacto ----------------------------------------
ok = firmador.VerifyFile(documento, "");
comprobar("VerifyFile acepta el documento original", ok, firmador.LastError);
comprobar("LastError queda vacio tras un exito", firmador.LastError == "", firmador.LastError);

// --- 3. Con comprobación de confianza, el certificado de prueba se rechaza -----
firmador.CheckCertificateTrust = true;
ok = firmador.VerifyFile(documento, firma);
comprobar("VerifyFile rechaza un certificado que no es de confianza", !ok);
comprobar("...y explica que el problema es la confianza", firmador.LastError.indexOf("no es de confianza") >= 0, firmador.LastError);
firmador.CheckCertificateTrust = false;

// --- 4. Documento modificado después de firmar --------------------------------
var modificado = carpeta + "\\modificado.txt";
escribirArchivo(modificado, "Este es el documento que se va a firmar!\r\n");
ok = firmador.VerifyFile(modificado, firma);
comprobar("VerifyFile rechaza un documento modificado", !ok);
comprobar("...y explica que la firma no es valida", firmador.LastError.indexOf("NO es v") >= 0, firmador.LastError);

// --- 5. Archivo grande (varios trozos de 64 KB) y archivo vacío ---------------
var grande = carpeta + "\\grande.txt";
var bloque = new Array(1025).join("0123456789ABCDEF");   // 16 KB
var archivo = fso.CreateTextFile(grande, true);
for (var i = 0; i < 20; i++) archivo.Write(bloque);       // 320 KB
archivo.Close();
ok = firmador.SignFile(grande, "") && firmador.VerifyFile(grande, "");
comprobar("Firma y verifica un archivo de varios trozos", ok, firmador.LastError);

var vacio = carpeta + "\\vacio.txt";
escribirArchivo(vacio, "");
ok = firmador.SignFile(vacio, "") && firmador.VerifyFile(vacio, "");
comprobar("Firma y verifica un archivo vacio", ok, firmador.LastError);

// --- 6. Firma en Base64 (como la guardan otras herramientas) ------------------
var firmaBase64 = carpeta + "\\firma-base64.p7s";
var shell = new ActiveXObject("WScript.Shell");
shell.Run('certutil -f -encode "' + firma + '" "' + firmaBase64 + '"', 0, true);
ok = firmador.VerifyFile(documento, firmaBase64);
comprobar("VerifyFile acepta una firma guardada en Base64", ok, firmador.LastError);

// --- 7. Errores de uso --------------------------------------------------------
ok = firmador.SignFile(carpeta + "\\no-existe.txt", "");
comprobar("SignFile falla si el archivo no existe", !ok && firmador.LastError != "", firmador.LastError);

ok = firmador.VerifyFile(documento, documento);
comprobar("VerifyFile falla si la 'firma' no es un PKCS#7", !ok && firmador.LastError != "", firmador.LastError);

firmador.CertificateThumbprint = "1234";
ok = firmador.SignFile(documento, "");
comprobar("SignFile falla con una huella mal escrita", !ok && firmador.LastError.indexOf("huella") >= 0, firmador.LastError);

// --- 8. Sin huella y sin ningún DNIe conocido ---------------------------------
// El control no debe ofrecer otros certificados del equipo: debe avisar de que
// no hay DNIe.
//
// Esta prueba solo tiene sentido si en el almacén personal del usuario NO hay
// ningún certificado de firma de DNIe. Si este usuario ha insertado alguna vez
// un DNIe en este equipo, sus certificados siguen en el almacén y el control
// intentaría firmar con él (pediría la tarjeta y el PIN). En ese caso
// ejecutar-pruebas.ps1 nos lo indica y la prueba se omite.
if (hayDnie) {
    WScript.Echo("  OMITIDA  SignFile sin DNIe (hay certificados de DNIe en el almacen)");
} else {
    firmador.CertificateThumbprint = "";
    ok = firmador.SignFile(documento, "");
    comprobar("SignFile sin DNIe avisa de que no se detecta", !ok && firmador.LastError.indexOf("No se detecta el DNIe") >= 0, firmador.LastError);
}

// --- 9. Versión del DNIe insertado ----------------------------------------------
// El resultado depende de si hay un DNIe en el lector al ejecutar las pruebas,
// así que solo comprobamos que la propiedad responde con uno de sus tres
// valores posibles: "" (sin DNIe), "3.0" o "4.0".
var version = firmador.DnieVersion;
comprobar("DnieVersion devuelve vacio, 3.0 o 4.0", version == "" || version == "3.0" || version == "4.0", version);

WScript.Echo(fallos == 0 ? "RESULTADO: todas las pruebas correctas" : "RESULTADO: " + fallos + " prueba(s) con fallo");
WScript.Quit(fallos == 0 ? 0 : 1);
