// ============================================================================
//  dniedatos.js  -  Biblioteca para la página: datos del DNIe y firma
// ============================================================================
//
//  Es lo único que su página necesita incluir:
//
//      <script src="dniedatos.js"></script>
//
//  Crea un objeto global, DNIeDatos. Todas sus funciones, salvo
//  extensionDisponible, devuelven una PROMESA:
//
//    DNIeDatos.extensionDisponible()   true / false, al momento.
//
//    SIN VENTANAS (no molestan al usuario)
//    DNIeDatos.version()               -> { version: "4.0" | "3.0" | "", serie }
//    DNIeDatos.informacion()           -> { version, serie, tipo, hardware,
//                                           firmware, programas: [...] }
//    DNIeDatos.claves()                -> { serie, claves: [...] }
//    DNIeDatos.verificar(opciones)     -> { valida: true | false }
//    DNIeDatos.hash(texto)             -> { hash }
//
//    CON PERMISO DEL USUARIO (y el PIN con el DNIe 3.0)
//    DNIeDatos.leer(["datos", "mrz", "imagenes", "certificados"])
//                                      -> { serie, datos, mrz, imagenes, certificados }
//    DNIeDatos.datos()                 -> leer(["datos"])
//    DNIeDatos.mrz()                   -> leer(["mrz"])
//    DNIeDatos.imagenes()              -> leer(["imagenes"])
//    DNIeDatos.certificados()          -> leer(["certificados"])
//
//    CON PIN
//    DNIeDatos.firmar(opciones)        -> { firma, algoritmo, hash, certificado }
//    DNIeDatos.firmarArchivo(opciones) -> { archivo, tamano, firma, algoritmo,
//                                           hash, certificado }
//
//  'opciones' de firmar y verificar:
//      { texto: "..." }  o  { hash: "<SHA-256 en Base64>" }
//      uso: "firma" (por defecto) o "autenticacion"
//      firma: "<Base64>"   (solo verificar: la firma que se comprueba)
//
//  Los datos binarios (firmas, certificados, imágenes, claves) van en
//  Base64. DNIeDatos.base64ABytes() los convierte en Uint8Array.
//
//  El detalle de cada respuesta está en docs\GUIA-DESARROLLADORES.md
//  (apartado 8).
//
//  Leer varias cosas a la vez con leer([...]) pide permiso UNA vez; pedirlas
//  por separado lo pide en cada llamada.
//
//  EJEMPLO
//  -------
//      DNIeDatos.datos().then(function (r) {
//          alert("Hola, " + r.datos.nombre);
//      }).catch(function (error) {
//          if (error.codigo !== "cancelado") { alert(error.message); }
//      });
//
//  SI ALGO FALLA
//  -------------
//  La promesa se rechaza con un Error. Su 'message' es la explicación en
//  español, lista para mostrar; 'codigo' dice qué ha pasado:
//
//    "cancelado"            El usuario canceló o no dio permiso. No es un fallo.
//    "sin_tarjeta"          No hay DNIe en el lector.
//    "pin_incorrecto"       PIN incorrecto (el DNIe descuenta un intento).
//    "pin_bloqueado"        PIN bloqueado.
//    "sin_driver"           Falta el driver del DNIe en el equipo.
//    "error"                Cualquier otro fallo (y parámetros incorrectos).
//    "ocupado"              Ya hay una operación esperando al usuario.
//    "sin_extension"        La extensión no está instalada o no está activa
//                           en esta página.
//    "sin_anfitrion"        Falta instalar el programa en el equipo.
//    "origen_no_permitido"  Esta página no está en la lista de la extensión.
//
//  Si el fallo viene del driver, 'pkcs11' trae su código ("0x000000A0").
//
//  CÓMO FUNCIONA
//  -------------
//  La página no puede hablar con la extensión directamente. Envía un mensaje
//  "al aire" con window.postMessage; el guion que la extensión carga dentro
//  de la página (contenido.js) lo recoge, lo lleva a la extensión y trae la
//  respuesta por el mismo camino. Cada petición lleva un número ('id') para
//  emparejarla con su respuesta.
// ============================================================================
var DNIeDatos = (function () {
    "use strict";

    // Debe coincidir con el de contenido.js.
    var CANAL = "dnie-datos";

    // Peticiones enviadas que aún esperan respuesta. Para cada 'id' se guardan
    // las dos funciones de su promesa: { resolver, rechazar }.
    var pendientes = {};
    var ultimoId = 0;

    function crearError(codigo, explicacion, pkcs11) {
        var error = new Error(explicacion);
        error.codigo = codigo;
        if (pkcs11) {
            error.pkcs11 = pkcs11;
        }
        return error;
    }

    // ¿Está la extensión instalada y activa en esta página? contenido.js deja
    // una marca en la etiqueta <html> nada más cargarse.
    function extensionDisponible() {
        return document.documentElement.getAttribute("data-dnie-datos") === "1";
    }

    // Envía una operación y devuelve la promesa de su respuesta (sin el campo
    // "correcto").
    function pedir(operacion, datos) {
        return new Promise(function (resolver, rechazar) {
            if (!extensionDisponible()) {
                rechazar(crearError("sin_extension",
                    "No se detecta la extensión del DNIe. Instálela (o actívela) y recargue la página."));
                return;
            }
            ultimoId += 1;
            pendientes[ultimoId] = { resolver: resolver, rechazar: rechazar };
            // El segundo argumento limita quién puede recibir el mensaje: solo
            // esta misma página (y el guion de la extensión que vive en ella).
            window.postMessage(
                { canal: CANAL, sentido: "peticion", id: ultimoId, operacion: operacion, datos: datos || {} },
                window.location.origin);
        });
    }

    // Recibe las respuestas que trae contenido.js.
    window.addEventListener("message", function (evento) {
        if (evento.source !== window || evento.origin !== window.location.origin) {
            return;
        }
        var mensaje = evento.data;
        if (!mensaje || mensaje.canal !== CANAL || mensaje.sentido !== "respuesta") {
            return;
        }
        var pendiente = pendientes[mensaje.id];
        if (!pendiente) {
            return;      // No es de ninguna petición nuestra.
        }
        delete pendientes[mensaje.id];

        var respuesta = mensaje.respuesta || {};
        if (respuesta.correcto) {
            delete respuesta.correcto;
            pendiente.resolver(respuesta);
        } else {
            pendiente.rechazar(crearError(respuesta.codigo || "error",
                respuesta.error || "Error desconocido.", respuesta.pkcs11));
        }
    });

    // Base64 -> Uint8Array (para firmas, certificados, imágenes...).
    function base64ABytes(texto) {
        var binario = atob(texto || "");
        var bytes = new Uint8Array(binario.length);
        for (var i = 0; i < binario.length; i++) {
            bytes[i] = binario.charCodeAt(i);
        }
        return bytes;
    }

    // Uint8Array (o Base64) -> hexadecimal en mayúsculas, para mostrar.
    function hex(bytes) {
        if (typeof bytes === "string") {
            bytes = base64ABytes(bytes);
        }
        var r = "";
        for (var i = 0; i < bytes.length; i++) {
            r += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16).toUpperCase();
        }
        return r;
    }

    function leer(que) {
        return pedir("leer", { que: que });
    }

    // Lo que ve la página.
    return {
        extensionDisponible: extensionDisponible,

        version:       function ()         { return pedir("version"); },
        informacion:   function ()         { return pedir("informacion"); },
        claves:        function ()         { return pedir("claves"); },
        hash:          function (texto)    { return pedir("hash", { texto: texto }); },
        verificar:     function (opciones) { return pedir("verificar", opciones); },

        leer:          leer,
        datos:         function () { return leer(["datos"]); },
        mrz:           function () { return leer(["mrz"]); },
        imagenes:      function () { return leer(["imagenes"]); },
        certificados:  function () { return leer(["certificados"]); },

        firmar:        function (opciones) { return pedir("firmar", opciones); },
        firmarArchivo: function (opciones) { return pedir("firmarArchivo", opciones); },

        base64ABytes:  base64ABytes,
        hex:           hex
    };
})();
