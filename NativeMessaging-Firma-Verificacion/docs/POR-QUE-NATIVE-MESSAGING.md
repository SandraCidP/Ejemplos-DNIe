# Por qué Native Messaging en lugar de ActiveX

Este documento explica, para lectores con experiencia en tarjeta inteligente,
por qué el ejemplo basado en ActiveX no basta para documentar el uso del
DNIe 4.0 desde una página web y cómo lo resuelve este proyecto.

El detalle completo (compilación, registro, formato de los mensajes y
adaptación a un sitio propio) está en la
[guía para desarrolladores](GUIA-DESARROLLADORES.md).

## El problema

El ejemplo ActiveX solo se puede ejecutar en Internet Explorer, retirado
en 2022, o en el modo IE de Edge, pensado para aplicaciones heredadas.

Chrome, Edge y Firefox no cargan código nativo dentro de la página: ni
ActiveX ni los antiguos plugins NPAPI. Tampoco hay una API web que sirva de
sustituto:

- **WebCrypto** no llega a las claves de una tarjeta.
- **El uso que el propio navegador hace del DNIe** se limita a la
  autenticación TLS de cliente; no se expone a JavaScript.

Para firmar un documento desde una página hace falta, por tanto, un
componente fuera del navegador.

## La solución

Este proyecto usa Native Messaging, el mecanismo que los tres navegadores
ofrecen para que una página se comunique con un programa instalado en el
equipo. La cadena es:

```text
página (dniefirma.js) ⇄ script de contenido ⇄ fondo de la extensión ⇄ DNIeAnfitrion.exe ⇄ CNG + minidriver ⇄ DNIe
        postMessage         runtime.sendMessage        connectNative (stdin/stdout)
```

| Pieza | Tecnología | Función |
| --- | --- | --- |
| Anfitrión nativo | C++, Win32 | Accede al DNIe y genera la firma. |
| Extensión | Manifest V3 | Filtra los orígenes y enlaza la página con el anfitrión. |
| Biblioteca de página | JavaScript, promesas | Ofrece la API a la página. |

### Anfitrión nativo

Es un `.exe` que el navegador lanza por petición y con el que habla por
stdin/stdout: 4 bytes de longitud en little-endian más JSON en UTF-8.

Se registra por usuario en HKCU, con un manifiesto que enumera las
extensiones autorizadas a invocarlo.

### Extensión

Hay una variante para Chrome y Edge (service worker) y otra para Firefox
(script de fondo). Comprueba el origen del remitente contra una lista de
orígenes permitidos y abre un puerto por petición.

### Biblioteca de página

Expone `DNIeFirma.firmar()`, `verificar()`, `version()` y
`extensionDisponible()`. Los fallos rechazan la promesa con un `Error` cuyo
`codigo` distingue los casos: `cancelado`, `error`, `ocupado`,
`sin_extension`, `sin_anfitrion` y `origen_no_permitido`.

## La criptografía no cambia

El núcleo es el del ejemplo ActiveX, reutilizado sin modificar: firma PKCS#7
separada mediante CNG y el minidriver del DNIe. Solo se sustituye el puente
entre la página y ese código.

El minidriver se carga en el proceso del anfitrión, así que la arquitectura
del `.exe` debe coincidir con la del middleware instalado. El proyecto se
compila para x64 y ARM64.

## Superficie expuesta a la página

Es deliberadamente mínima:

- **Peticiones:** solo llevan el nombre de la operación. La página no envía
  rutas, datos ni parámetros.
- **Archivo y PIN:** el archivo se elige en un diálogo nativo del anfitrión y
  el PIN se pide en una ventana propia. Ninguno pasa por el navegador.
- **Respuestas:** llevan el nombre del archivo sin carpeta, el firmante y la
  firma en Base64. El protocolo limita a 1 MB los mensajes del anfitrión al
  navegador.

## Aplicación a otros usos del DNIe

El puente es independiente de la capa criptográfica. El mismo esquema de
extensión más anfitrión sirve para exponer a una página otras operaciones
con la tarjeta; por ejemplo, la lectura de los datos del ciudadano en formato
de pasaporte electrónico ICAO mediante el módulo PKCS#11.

Bastaría con situar ese módulo detrás del anfitrión y añadir una operación al
protocolo. La extensión y la biblioteca de página podrían compartirse.
