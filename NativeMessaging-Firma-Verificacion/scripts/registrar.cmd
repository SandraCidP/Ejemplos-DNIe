@echo off
rem ============================================================================
rem  registrar.cmd - Da a conocer el programa de firma a Chrome, Edge y
rem  Firefox, para el USUARIO ACTUAL.
rem
rem  No necesita permisos de administrador: escribe en HKEY_CURRENT_USER.
rem
rem  Uso:   scripts\registrar.cmd            (elige el .exe de este equipo)
rem         scripts\registrar.cmd x64        (fuerza una plataforma: x64
rem                                           o ARM64)
rem
rem  COMO ENCUENTRA UN NAVEGADOR EL PROGRAMA
rem  Cuando la extension pide conectar con "es.fnmt.dniefirma", el navegador
rem  busca ese nombre en una clave del registro de Windows. La clave contiene
rem  la ruta de un archivo JSON, el "manifiesto del anfitrion", que dice dos
rem  cosas: donde esta el .exe y que extension tiene permiso para usarlo.
rem  Cada navegador mira en su propia clave:
rem
rem    Chrome   HKCU\Software\Google\Chrome\NativeMessagingHosts\<nombre>
rem    Edge     HKCU\Software\Microsoft\Edge\NativeMessagingHosts\<nombre>
rem    Firefox  HKCU\Software\Mozilla\NativeMessagingHosts\<nombre>
rem
rem  Hay dos manifiestos porque cada familia identifica la extension a su
rem  manera: Chrome y Edge con "allowed_origins" y Firefox con
rem  "allowed_extensions". Estan en anfitrion\manifiestos.
rem
rem  QUE HACE ESTE SCRIPT
rem    1. Copia los dos manifiestos junto al .exe. (En ellos la ruta del .exe
rem       es solo su nombre, "DNIeAnfitrion.exe": en Windows los navegadores
rem       la entienden como relativa a la carpeta del manifiesto.)
rem    2. Escribe las tres claves del registro, apuntando a esas copias.
rem
rem  Se registran los tres navegadores aunque alguno no este instalado: una
rem  clave de mas no molesta, y asi sirve si se instala despues.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos o
rem  enes se muestren bien en los mensajes.
rem ============================================================================
setlocal
chcp 65001 >nul

set "NOMBRE=es.fnmt.dniefirma"

rem --- Plataforma: la indicada o la del procesador de este equipo ------------
rem PROCESSOR_ARCHITECTURE dice el procesador... del programa que pregunta.
rem Si este script se ejecuta desde una consola de 32 bits en un Windows de
rem 64, el procesador real esta en PROCESSOR_ARCHITEW6432.
set "PLATAFORMA=%~1"
set "PROCESADOR=%PROCESSOR_ARCHITEW6432%"
if not defined PROCESADOR set "PROCESADOR=%PROCESSOR_ARCHITECTURE%"
if not defined PLATAFORMA (
    if /i "%PROCESADOR%"=="AMD64" set "PLATAFORMA=x64"
    if /i "%PROCESADOR%"=="ARM64" set "PLATAFORMA=ARM64"
)
if not defined PLATAFORMA (
    echo ERROR: no hay version del programa para el procesador de este equipo ^(%PROCESADOR%^).
    exit /b 1
)

rem --- Carpeta raiz del proyecto, sin ".." en la ruta ------------------------
for %%i in ("%~dp0..") do set "RAIZ=%%~fi"
set "CARPETA=%RAIZ%\bin\%PLATAFORMA%\Release"
set "MANIFIESTOS=%RAIZ%\anfitrion\manifiestos"

if not exist "%CARPETA%\DNIeAnfitrion.exe" (
    echo ERROR: no existe "%CARPETA%\DNIeAnfitrion.exe".
    echo        Ejecute antes scripts\compilar.cmd, o indique otra plataforma:
    echo        scripts\registrar.cmd x64
    exit /b 1
)

rem --- 1. Manifiestos junto al .exe ------------------------------------------
copy /y "%MANIFIESTOS%\%NOMBRE%.chromium.json" "%CARPETA%\" >nul
if errorlevel 1 ( echo ERROR al copiar el manifiesto de Chrome y Edge. & exit /b 1 )
copy /y "%MANIFIESTOS%\%NOMBRE%.firefox.json" "%CARPETA%\" >nul
if errorlevel 1 ( echo ERROR al copiar el manifiesto de Firefox. & exit /b 1 )

rem --- 2. Claves del registro ------------------------------------------------
rem   /ve  escribe el valor "(Predeterminado)" de la clave.
rem   /f   sobrescribe sin preguntar si ya existia.
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NOMBRE%" /ve /t REG_SZ /d "%CARPETA%\%NOMBRE%.chromium.json" /f >nul
if errorlevel 1 ( echo ERROR al registrar para Chrome. & exit /b 1 )
echo Registrado para Chrome.

reg add "HKCU\Software\Microsoft\Edge\NativeMessagingHosts\%NOMBRE%" /ve /t REG_SZ /d "%CARPETA%\%NOMBRE%.chromium.json" /f >nul
if errorlevel 1 ( echo ERROR al registrar para Edge. & exit /b 1 )
echo Registrado para Edge.

reg add "HKCU\Software\Mozilla\NativeMessagingHosts\%NOMBRE%" /ve /t REG_SZ /d "%CARPETA%\%NOMBRE%.firefox.json" /f >nul
if errorlevel 1 ( echo ERROR al registrar para Firefox. & exit /b 1 )
echo Registrado para Firefox.

echo.
echo Programa de firma registrado ^(%PLATAFORMA%^).
echo Siguiente paso: cargar la extension en el navegador y abrir web\servir.cmd
endlocal
