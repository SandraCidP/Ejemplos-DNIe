// ============================================================================
//  dniefirma.js  -  Biblioteca para la página: firmar con el DNIe
// ============================================================================
//
//  Es lo único que su página necesita incluir:
//
//      <script src="dniefirma.js"></script>
//
//  Crea un objeto global, DNIeFirma, con cuatro funciones:
//
//      DNIeFirma.extensionDisponible()   true / false, al momento.
//      DNIeFirma.version()               Promesa -> "4.0", "3.0" o "".
//      DNIeFirma.firmar()                Promesa -> datos de la firma.
//      DNIeFirma.verificar()             Promesa -> datos de la verificación.
//
//  EJEMPLO
//  -------
//      DNIeFirma.firmar().then(function (resultado) {
//          alert("Firmado por " + resultado.firmante);
//      }).catch(function (error) {
//          if (error.codigo !== "cancelado") { alert(error.message); }
//      });
//
//  O, con async / await:
//
//      try {
//          const resultado = await DNIeFirma.firmar();
//      } catch (error) { ... }
//
//  QUÉ DEVUELVEN
//  -------------
//    firmar()     { archivo: "informe.pdf", archivoFirma: "informe.pdf.p7s",
//                   firmante: "...", firma: "MIIG..." }
//                 'firma' es el contenido del .p7s en Base64, por si la
//                 página quiere enviarlo a su servidor.
//    verificar()  { archivo: "...", archivoFirma: "...", firmante: "..." }
//
//  La página nunca indica ni recibe rutas del disco: el archivo lo elige el
//  usuario en un diálogo de Windows, y solo vuelve su nombre.
//
//  SI ALGO FALLA
//  -------------
//  La promesa se rechaza con un Error. Su 'message' es la explicación en
//  español, lista para mostrar, y su propiedad 'codigo' dice qué ha pasado:
//
//    "cancelado"            El usuario canceló un diálogo. No es un fallo.
//    "error"                No se pudo firmar o la firma no es válida.
//    "ocupado"              Ya hay otra firma en marcha.
//    "sin_extension"        La extensión no está instalada o no está activa
//                           en esta página.
//    "sin_anfitrion"        Falta instalar el programa de firma en el equipo.
//    "origen_no_permitido"  Esta página no está en la lista de la extensión.
//
//  CÓMO FUNCIONA
//  -------------
//  La página no puede hablar con la extensión directamente. Envía un mensaje
//  "al aire" con window.postMessage; el guion que la extensión carga dentro
//  de la página (contenido.js) lo recoge, lo lleva a la extensión y trae la
//  respuesta por el mismo camino. Cada petición lleva un número ('id') para
//  emparejarla con su respuesta.
// ============================================================================
var DNIeFirma = (function () {
    "use strict";

    // Debe coincidir con el de contenido.js.
    var CANAL = "dnie-firma";

    // Peticiones enviadas que aún esperan respuesta. Para cada 'id' se guardan
    // las dos funciones de su promesa: { resolver, rechazar }.
    var pendientes = {};
    var ultimoId = 0;

    // Crea el Error con el que se rechazan las promesas.
    function crearError(codigo, explicacion) {
        var error = new Error(explicacion);
        error.codigo = codigo;
        return error;
    }

    // ¿Está la extensión instalada y activa en esta página? contenido.js
    // deja una marca en la etiqueta <html> nada más cargarse.
    function extensionDisponible() {
        return document.documentElement.getAttribute("data-dnie-firma") === "1";
    }

    // Envía una operación y devuelve la promesa de su resultado.
    function pedir(operacion) {
        return new Promise(function (resolver, rechazar) {
            if (!extensionDisponible()) {
                rechazar(crearError("sin_extension",
                    "No se detecta la extensión de firma con DNIe. Instálela (o actívela) " +
                    "y recargue la página."));
                return;
            }

            ultimoId += 1;
            pendientes[ultimoId] = { resolver: resolver, rechazar: rechazar };

            // El segundo argumento limita quién puede recibir el mensaje:
            // solo esta misma página (y el guion de la extensión que vive
            // en ella).
            window.postMessage(
                { canal: CANAL, sentido: "peticion", id: ultimoId, operacion: operacion },
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
            pendiente.resolver(respuesta);
        } else {
            pendiente.rechazar(crearError(respuesta.codigo || "error",
                respuesta.error || "Error desconocido."));
        }
    });

    // Lo que ve la página.
    return {
        extensionDisponible: extensionDisponible,

        // Versión del DNIe que hay en el lector: "3.0", "4.0" o "" (ninguno).
        // No muestra nada al usuario ni pide el PIN.
        version: function () {
            return pedir("version").then(function (respuesta) {
                return respuesta.version;
            });
        },

        // Abre el diálogo para elegir el archivo, pide el PIN y firma.
        firmar: function () {
            return pedir("firmar").then(function (respuesta) {
                return {
                    archivo: respuesta.archivo,
                    archivoFirma: respuesta.archivoFirma,
                    firmante: respuesta.firmante,
                    firma: respuesta.firma
                };
            });
        },

        // Abre el diálogo para elegir el archivo y comprueba su firma. Si la
        // firma no es válida, la promesa se rechaza con el motivo.
        verificar: function () {
            return pedir("verificar").then(function (respuesta) {
                return {
                    archivo: respuesta.archivo,
                    archivoFirma: respuesta.archivoFirma,
                    firmante: respuesta.firmante
                };
            });
        }
    };
})();
