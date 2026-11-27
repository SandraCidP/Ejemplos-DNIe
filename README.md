# Ejemplos de código para usar el DNIe desde una página web

Ejemplos de la FNMT-RCM para usar el **DNI electrónico (DNIe 3.0 y 4.0)** desde una página web en Windows: **firma y verificación** y **lectura de los datos del ciudadano**, cada uno en dos tecnologías.

| Ejemplo | Qué hace | Navegadores | Acceso a la tarjeta |
| --- | --- | --- | --- |
| [ActiveX-Firma-Verificacion](ActiveX-Firma-Verificacion/) | Firma y verificación de archivos (PKCS#7) | Motor de Internet Explorer (IE 11, Edge en modo IE, HTA) | CNG y minidriver del DNIe |
| [ActiveX-Datos-Ciudadano](ActiveX-Datos-Ciudadano/) | Datos del ciudadano (datos personales, MRZ, foto, firma manuscrita), certificados, claves, firma y verificación. Misma interfaz que el ActiveX publicado en el portal del DNIe | Motor de Internet Explorer (IE 11, Edge en modo IE, HTA) | Driver PKCS#11 del DNIe |
| [NativeMessaging-Firma-Verificacion](NativeMessaging-Firma-Verificacion/) | Firma y verificación de archivos (PKCS#7) | Chrome, Edge y Firefox | CNG y minidriver del DNIe |
| [NativeMessaging-Datos-Ciudadano](NativeMessaging-Datos-Ciudadano/) | Datos del ciudadano, certificados, claves, firma y verificación | Chrome, Edge y Firefox | Driver PKCS#11 del DNIe |

## ¿ActiveX o Native Messaging?

ActiveX solo funciona con el motor de Internet Explorer, una tecnología retirada. Se mantiene para las páginas que ya usan el ActiveX publicado. Para páginas nuevas, use los ejemplos de **Native Messaging**, que funcionan en Chrome, Edge y Firefox. El documento [Por qué Native Messaging](NativeMessaging-Firma-Verificacion/docs/POR-QUE-NATIVE-MESSAGING.md) lo explica con detalle.

## Cómo está organizado

Cada carpeta es un proyecto completo e independiente: se abre por separado en Visual Studio o en Visual Studio Code y tiene su propio README, sus scripts para compilar y registrar, una página de ejemplo y una **Guía para desarrolladores** en `docs/`. Las cuatro guías siguen el mismo índice de 16 apartados:

1. Qué es y qué no es
2. Restricciones de uso
3. Puesta en marcha en diez minutos
4. Importar y compilar en Visual Studio Code
5. Importar y compilar en Visual Studio
6. Uso en su página
7. Llevar la firma (y los datos) al servidor
8. Referencia
9. Instalarlo en los equipos de los usuarios
10. Cómo funciona por dentro
11. Mapa del código
12. Cómo ampliarlo
13. Pruebas
14. Seguridad
15. Problemas frecuentes
16. Glosario

Los ejemplos pueden instalarse a la vez en el mismo equipo: cada uno usa sus propios identificadores y, los de Native Messaging, su propio puerto de pruebas (8080 y 8081).

## Requisitos comunes

- Windows 10 u 11.
- Visual Studio 2022 o sus Build Tools, con las herramientas de C++ (y ATL para los ActiveX).
- Un lector de tarjetas, un DNIe y el software del DNIe instalado (el driver PKCS#11 o el minidriver, según el ejemplo).

## Licencia

Los cuatro ejemplos se distribuyen con la licencia MIT, con copyright de la FNMT-RCM: ver el `LICENSE` de cada uno.

## Componentes de terceros

Los ejemplos de lectura de datos incluyen `openjpeg.js` (OpenJPEG compilado a JavaScript, licencia BSD) y las cabeceras PKCS#11 de RSA. Sus avisos y licencias están en el `AVISOS-TERCEROS.md` de cada uno.

FNMT-RCM
