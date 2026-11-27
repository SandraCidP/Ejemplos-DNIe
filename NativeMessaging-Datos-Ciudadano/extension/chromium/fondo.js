// ============================================================================
//  fondo.js  -  Guion de fondo de la extensión: el "portero"
// ============================================================================
//
//  DÓNDE ENCAJA
//  ------------
//    Página web ──▶ contenido.js ──▶ fondo.js ──▶ DNIeDatos.exe
//                                    (aquí)
//
//  De todas las piezas de la extensión, esta es la única a la que el
//  navegador deja arrancar el programa del DNIe (permiso "nativeMessaging"
//  de manifest.json). Por eso es también donde se decide QUIÉN puede pedir
//  algo y QUÉ puede pedir. Para cada petición:
//
//    1. Comprueba que viene de una página de un origen permitido.
//    2. Comprueba la operación y sus parámetros, y construye un mensaje
//       NUEVO solo con ellos (nunca reenvía lo que manda la página).
//    3. Añade el origen de la página, que el programa enseña al usuario en
//       las ventanas de permiso y del PIN.
//    4. Arranca el programa, le pasa el mensaje y devuelve su respuesta.
//
//  Las peticiones se atienden de una en una, en orden de llegada (la
//  tarjeta solo puede hacer una cosa a la vez).
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
//  https://www.ejemplo.es/tramites/alta.html  es  https://www.ejemplo.es
//
//  Solo las páginas de estos orígenes pueden usar el DNIe. Se escriben sin
//  barra final. El de abajo es el de la página de ejemplo (web\servir.cmd).
//
//  Al añadir un origen hay que añadir también su patrón en "matches" de
//  manifest.json (por ejemplo "https://www.ejemplo.es/*"), que es lo que
//  hace que contenido.js se cargue en esas páginas.
// ----------------------------------------------------------------------------
const ORIGENES_PERMITIDOS = [
    "http://localhost:8081"
];

// Nombre con el que el programa está registrado en Windows
// (scripts\registrar.cmd). El navegador lo busca por este nombre.
const NOMBRE_DEL_ANFITRION = "es.fnmt.dniedatos";

// Límites de los parámetros (los mismos que aplica el programa).
const TEXTO_MAXIMO = 64 * 1024;          // bytes UTF-8 del texto a firmar
const FIRMA_MAXIMA = 2048;               // caracteres Base64 de una firma
const QUE_SE_PUEDE_LEER = ["datos", "mrz", "imagenes", "certificados"];
const USOS = ["firma", "autenticacion"];

// ----------------------------------------------------------------------------
//  Utilidades
// ----------------------------------------------------------------------------

// Respuesta de fallo, con la misma forma que las del programa.
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

function esTexto(valor) {
    return typeof valor === "string";
}

// ¿Es un texto Base64 de como mucho 'maximo' caracteres?
function esBase64(valor, maximo) {
    return esTexto(valor) && valor.length > 0 && valor.length <= maximo &&
        /^[A-Za-z0-9+/]+={0,2}$/.test(valor);
}

// "texto" o "hash" (uno de los dos), comunes a firmar y verificar.
// Devuelve un texto con el problema, o "" si están bien.
function copiarTextoOHash(datos, mensaje) {
    const hayTexto = datos.texto !== undefined;
    const hayHash = datos.hash !== undefined;
    if (hayTexto === hayHash) {
        return "Indique el texto (\"texto\") o su resumen SHA-256 (\"hash\"), uno de los dos.";
    }
    if (hayTexto) {
        if (!esTexto(datos.texto) || new TextEncoder().encode(datos.texto).length > TEXTO_MAXIMO) {
            return "El texto tiene que ser una cadena de 64 KB como mucho.";
        }
        mensaje.texto = datos.texto;
    } else {
        // 32 bytes en Base64 son 44 caracteres.
        if (!esBase64(datos.hash, 44)) {
            return "El resumen (\"hash\") tiene que ser un SHA-256 en Base64.";
        }
        mensaje.hash = datos.hash;
    }
    return "";
}

function copiarUso(datos, mensaje) {
    if (datos.uso === undefined) {
        return "";
    }
    if (!USOS.includes(datos.uso)) {
        return "\"uso\" tiene que ser \"firma\" o \"autenticacion\".";
    }
    mensaje.uso = datos.uso;
    return "";
}

// ----------------------------------------------------------------------------
//  Comprobar la petición y construir el mensaje para el programa
// ----------------------------------------------------------------------------
//  Devuelve { mensaje } si todo está bien, o { error } con la explicación.
//  Solo pasan los campos conocidos de cada operación, con su tipo correcto.
// ----------------------------------------------------------------------------
function construirMensaje(operacion, datos) {
    datos = (datos && typeof datos === "object") ? datos : {};
    const mensaje = { operacion: operacion };
    let problema = "";

    switch (operacion) {
        case "version":
        case "informacion":
        case "claves":
            break;

        case "leer": {
            const que = datos.que;
            if (!Array.isArray(que) || que.length === 0 || que.length > QUE_SE_PUEDE_LEER.length ||
                !que.every(function (q) { return QUE_SE_PUEDE_LEER.includes(q); })) {
                problema = "\"que\" tiene que ser una lista con datos, mrz, imagenes y/o certificados.";
            } else {
                mensaje.que = Array.from(new Set(que));   // sin repetidos
            }
            break;
        }

        case "firmar":
            problema = copiarTextoOHash(datos, mensaje) || copiarUso(datos, mensaje);
            break;

        case "firmarArchivo":
            problema = copiarUso(datos, mensaje);
            break;

        case "verificar":
            problema = copiarTextoOHash(datos, mensaje) || copiarUso(datos, mensaje);
            if (!problema) {
                if (!esBase64(datos.firma, FIRMA_MAXIMA)) {
                    problema = "La firma (\"firma\") tiene que ir en Base64.";
                } else {
                    mensaje.firma = datos.firma;
                }
            }
            break;

        case "hash":
            if (!esTexto(datos.texto) || new TextEncoder().encode(datos.texto).length > TEXTO_MAXIMO) {
                problema = "El texto tiene que ser una cadena de 64 KB como mucho.";
            } else {
                mensaje.texto = datos.texto;
            }
            break;

        default:
            problema = "Operación desconocida.";
    }
    return problema ? { error: problema } : { mensaje: mensaje };
}

// ----------------------------------------------------------------------------
//  Hablar con el programa
// ----------------------------------------------------------------------------
//  connectNative arranca DNIeDatos.exe y abre un "puerto" con él. Por el
//  puerto enviamos la petición y esperamos UNA respuesta; después lo
//  cerramos, y con eso el programa termina. Un programa nuevo por petición:
//  no queda nada en marcha (ni datos, ni PIN) entre una petición y otra.
//
//  Devuelve una promesa que siempre se cumple con un objeto de respuesta
//  (correcto: true o false); nunca se rechaza.
// ----------------------------------------------------------------------------
function llamarAlAnfitrion(mensaje) {
    return new Promise(function (resolver) {
        let puerto;
        try {
            puerto = chrome.runtime.connectNative(NOMBRE_DEL_ANFITRION);
        } catch (e) {
            resolver(fallo("sin_anfitrion", "No se pudo arrancar el programa del DNIe: " + e.message));
            return;
        }

        let respondido = false;

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
                "No se pudo conectar con el programa del DNIe. Compruebe que está instalado " +
                "(scripts\\registrar.cmd)." + (motivo && motivo.message ? " Detalle: " + motivo.message : "")));
        });

        puerto.postMessage(mensaje);
    });
}

// ----------------------------------------------------------------------------
//  Cola: una petición detrás de otra
// ----------------------------------------------------------------------------
//  'cola' es la promesa de la última petición encolada. Cada petición nueva
//  se encadena detrás, así que el programa nunca se arranca dos veces a la
//  vez y dos peticiones no se pisan en la tarjeta.
//
//  Excepción: si ya hay en cola una operación que abre ventanas (permiso,
//  PIN, "Abrir"), otra de ese tipo se rechaza con "ocupado" en vez de
//  esperar. Así dos pulsaciones seguidas no abren dos ventanas del PIN una
//  detrás de otra.
// ----------------------------------------------------------------------------
const ABREN_VENTANAS = ["leer", "firmar", "firmarArchivo"];
let cola = Promise.resolve();
let ventanasEnCola = 0;

function encolar(mensaje) {
    const abreVentanas = ABREN_VENTANAS.includes(mensaje.operacion);
    if (abreVentanas && ventanasEnCola > 0) {
        return Promise.resolve(fallo("ocupado",
            "Ya hay una operación con el DNIe esperando al usuario. Termínela o cancélela primero."));
    }
    if (abreVentanas) {
        ventanasEnCola++;
    }
    const resultado = cola.then(function () {
        return llamarAlAnfitrion(mensaje);
    });
    cola = resultado.then(function () {
        if (abreVentanas) {
            ventanasEnCola--;
        }
    });
    return resultado;
}

// ----------------------------------------------------------------------------
//  Atender las peticiones que reenvía contenido.js
// ----------------------------------------------------------------------------
//  'responder' es la función con la que se contesta a contenido.js. Como la
//  respuesta tarda (hay que esperar a la tarjeta y al usuario), el oyente
//  devuelve true: así se le dice al navegador "contestaré más tarde, no
//  cierres el canal".
// ----------------------------------------------------------------------------
chrome.runtime.onMessage.addListener(function (peticion, remitente, responder) {
    // 1. ¿Viene de un origen permitido?
    const origen = origenDe(remitente);
    if (!ORIGENES_PERMITIDOS.includes(origen)) {
        responder(fallo("origen_no_permitido",
            "Esta página (" + origen + ") no está autorizada a usar el DNIe."));
        return false;
    }

    // 2. ¿Operación y parámetros correctos?
    const comprobada = construirMensaje(peticion && peticion.operacion, peticion && peticion.datos);
    if (comprobada.error) {
        responder(fallo("error", comprobada.error));
        return false;
    }

    // 3. El origen lo pone la extensión, nunca la página.
    comprobada.mensaje.origen = origen;

    // 4. Al programa, por orden de llegada.
    encolar(comprobada.mensaje).then(responder);
    return true;
});
