// ============================================================================
//  fondo.js  -  Guion de fondo de la extensión: el "portero"
// ============================================================================
//
//  DÓNDE ENCAJA
//  ------------
//    Página web ──▶ contenido.js ──▶ fondo.js ──▶ DNIeAnfitrion.exe
//                                    (aquí)
//
//  De todas las piezas de la extensión, esta es la única a la que el
//  navegador deja arrancar el programa de firma (permiso "nativeMessaging"
//  de manifest.json). Por eso es también donde se decide QUIÉN puede pedir
//  una firma y QUÉ puede pedir. Hace tres cosas, en este orden:
//
//    1. Comprueba que la petición viene de una página de un origen permitido.
//    2. Comprueba que la operación es una de las tres que existen.
//    3. Arranca el programa, le pasa la operación y devuelve su respuesta.
//
//  Este archivo es idéntico en las carpetas "chromium" y "firefox". Lo único
//  que cambia entre navegadores es manifest.json. Se usa el objeto "chrome",
//  que también existe en Firefox.
// ============================================================================

// ----------------------------------------------------------------------------
//  LISTA DE ORÍGENES PERMITIDOS  (lo primero que hay que adaptar)
// ----------------------------------------------------------------------------
//  El "origen" de una página es el principio de su dirección: protocolo,
//  nombre del servidor y puerto. Por ejemplo, el origen de
//  https://www.ejemplo.es/tramites/firmar.html  es  https://www.ejemplo.es
//
//  Solo las páginas de estos orígenes pueden pedir una firma. Se escriben
//  sin barra final. El de abajo es el de la página de ejemplo (web\servir.cmd).
//
//  Al añadir un origen hay que añadir también su patrón en "matches" de
//  manifest.json (por ejemplo "https://www.ejemplo.es/*"), que es lo que
//  hace que contenido.js se cargue en esas páginas.
// ----------------------------------------------------------------------------
const ORIGENES_PERMITIDOS = [
    "http://localhost:8080"
];

// Nombre con el que el programa de firma está registrado en Windows
// (scripts\registrar.cmd). El navegador lo busca por este nombre.
const NOMBRE_DEL_ANFITRION = "es.fnmt.dniefirma";

// Las únicas operaciones que se aceptan. Ver su descripción en Main.cpp.
const OPERACIONES = ["version", "firmar", "verificar"];

// true mientras hay una firma o una verificación en marcha. Evita que se
// abran dos diálogos a la vez (por ejemplo, con dos pulsaciones seguidas).
let hayDialogoAbierto = false;

// ----------------------------------------------------------------------------
//  Utilidades
// ----------------------------------------------------------------------------

// Respuesta de fallo, con la misma forma que las del programa de firma.
function fallo(codigo, explicacion) {
    return { correcto: false, codigo: codigo, error: explicacion };
}

// Origen de la página que envía el mensaje. El dato lo pone el navegador,
// no la página: por eso nos podemos fiar de él.
function origenDe(remitente) {
    if (remitente.origin) {
        return remitente.origin;
    }
    try {
        return new URL(remitente.url).origin;
    } catch (e) {
        return "";
    }
}

// ----------------------------------------------------------------------------
//  Hablar con el programa de firma
// ----------------------------------------------------------------------------
//  connectNative arranca DNIeAnfitrion.exe y abre un "puerto" con él. Por el
//  puerto enviamos la petición y esperamos UNA respuesta; después lo
//  cerramos, y con eso el programa termina. Un programa nuevo por petición:
//  no queda nada en marcha entre una firma y la siguiente.
//
//  Devuelve una promesa que siempre se cumple con un objeto de respuesta
//  (correcto: true o false); nunca se rechaza.
// ----------------------------------------------------------------------------
function llamarAlAnfitrion(operacion) {
    return new Promise(function (resolver) {
        let puerto;
        try {
            puerto = chrome.runtime.connectNative(NOMBRE_DEL_ANFITRION);
        } catch (e) {
            resolver(fallo("sin_anfitrion", "No se pudo arrancar el programa de firma: " + e.message));
            return;
        }

        let respondido = false;

        // Llega la respuesta del programa.
        puerto.onMessage.addListener(function (respuesta) {
            respondido = true;
            puerto.disconnect();
            resolver(respuesta);
        });

        // El puerto se cierra sin haber recibido respuesta: el programa no
        // está registrado, no existe en la ruta registrada o ha terminado
        // antes de tiempo. El navegador explica el motivo (en inglés) en
        // chrome.runtime.lastError (Chrome y Edge) o en puerto.error (Firefox).
        puerto.onDisconnect.addListener(function (puertoCerrado) {
            if (respondido) {
                return;
            }
            const motivo = chrome.runtime.lastError || (puertoCerrado && puertoCerrado.error);
            resolver(fallo("sin_anfitrion",
                "No se pudo conectar con el programa de firma. Compruebe que está instalado " +
                "(scripts\\registrar.cmd)." + (motivo && motivo.message ? " Detalle: " + motivo.message : "")));
        });

        // La petición lleva solo el nombre de la operación. Fíjese en que NO
        // reenviamos el mensaje de la página: construimos uno nuevo, para
        // que al programa no le llegue nada que no hayamos puesto nosotros.
        puerto.postMessage({ operacion: operacion });
    });
}

// ----------------------------------------------------------------------------
//  Atender las peticiones que reenvía contenido.js
// ----------------------------------------------------------------------------
//  'responder' es la función con la que se contesta a contenido.js. Como la
//  respuesta tarda (hay que esperar al usuario), el oyente devuelve true:
//  así se le dice al navegador "contestaré más tarde, no cierres el canal".
// ----------------------------------------------------------------------------
chrome.runtime.onMessage.addListener(function (mensaje, remitente, responder) {
    // 1. ¿Viene de un origen permitido?
    const origen = origenDe(remitente);
    if (!ORIGENES_PERMITIDOS.includes(origen)) {
        responder(fallo("origen_no_permitido",
            "Esta página (" + origen + ") no está autorizada a firmar con el DNIe."));
        return false;
    }

    // 2. ¿Es una operación conocida?
    const operacion = mensaje && mensaje.operacion;
    if (!OPERACIONES.includes(operacion)) {
        responder(fallo("error", "Operación desconocida."));
        return false;
    }

    // "version" no muestra nada al usuario y se puede atender siempre.
    // "firmar" y "verificar" abren diálogos: solo uno a la vez.
    const abreDialogos = operacion !== "version";
    if (abreDialogos && hayDialogoAbierto) {
        responder(fallo("ocupado", "Ya hay una operación de firma en curso. Termínela o cancélela primero."));
        return false;
    }
    if (abreDialogos) {
        hayDialogoAbierto = true;
    }

    // 3. Pasar la operación al programa y devolver su respuesta.
    llamarAlAnfitrion(operacion).then(function (respuesta) {
        if (abreDialogos) {
            hayDialogoAbierto = false;
        }
        responder(respuesta);
    });
    return true;
});
