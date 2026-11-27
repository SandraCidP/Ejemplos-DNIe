// ============================================================================
//  contenido.js  -  Guion de contenido de la extensión: el "mensajero"
// ============================================================================
//
//  DÓNDE ENCAJA
//  ------------
//    Página web ──▶ contenido.js ──▶ fondo.js ──▶ DNIeDatos.exe
//    (dniedatos.js)   (aquí)
//
//  Una página web no puede hablar directamente con una extensión. Lo que sí
//  hace el navegador es cargar este guion DENTRO de las páginas indicadas en
//  "matches" de manifest.json. Desde aquí se ven las dos orillas:
//
//    - la página, con la que se habla mediante window.postMessage, y
//    - la extensión, con la que se habla mediante chrome.runtime.sendMessage.
//
//  Este guion solo lleva mensajes de un lado a otro. No decide nada: quien
//  comprueba el origen, la operación y los parámetros es fondo.js.
//
//  FORMA DE LOS MENSAJES CON LA PÁGINA
//  -----------------------------------
//    Petición (página -> aquí):
//      { canal: "dnie-datos", sentido: "peticion", id: 7,
//        operacion: "leer", datos: { que: ["datos"] } }
//
//    Respuesta (aquí -> página):
//      { canal: "dnie-datos", sentido: "respuesta", id: 7,
//        respuesta: { correcto: true, ... } }
//
//    "canal"   distingue nuestros mensajes de otros que circulen por la
//              página (postMessage lo usan muchas bibliotecas).
//    "id"      número que pone la página; se devuelve igual para que sepa a
//              qué petición corresponde cada respuesta.
//
//  Este archivo es idéntico en las carpetas "chromium" y "firefox".
// ============================================================================

const CANAL = "dnie-datos";

// Marca en la página para que dniedatos.js sepa que la extensión está
// instalada y activa en ella. "run_at": "document_start" en manifest.json
// garantiza que está puesta antes de que se ejecute cualquier guion de la
// página.
document.documentElement.setAttribute("data-dnie-datos", "1");

// Entrega una respuesta a la página.
function responderALaPagina(id, respuesta) {
    // El segundo argumento limita quién puede recibir el mensaje: solo una
    // página de este mismo origen.
    window.postMessage(
        { canal: CANAL, sentido: "respuesta", id: id, respuesta: respuesta },
        window.location.origin);
}

window.addEventListener("message", function (evento) {
    // Solo interesan los mensajes que envía esta misma página (no los de
    // otras ventanas ni los de marcos incrustados)...
    if (evento.source !== window || evento.origin !== window.location.origin) {
        return;
    }
    // ...y, de esos, solo nuestras peticiones.
    const mensaje = evento.data;
    if (!mensaje || mensaje.canal !== CANAL || mensaje.sentido !== "peticion") {
        return;
    }

    // A fondo.js solo pasan la operación y sus datos; allí se comprueban.
    try {
        chrome.runtime.sendMessage({ operacion: mensaje.operacion, datos: mensaje.datos }).then(
            function (respuesta) {
                responderALaPagina(mensaje.id, respuesta);
            },
            function (error) {
                responderALaPagina(mensaje.id, {
                    correcto: false,
                    codigo: "sin_extension",
                    error: "La extensión del DNIe no responde: " + error.message
                });
            });
    } catch (e) {
        // Ocurre si la extensión se ha actualizado o recargado después de
        // abrir la página: este guion ha quedado "huérfano".
        responderALaPagina(mensaje.id, {
            correcto: false,
            codigo: "sin_extension",
            error: "La extensión del DNIe se ha actualizado. Recargue la página."
        });
    }
});
