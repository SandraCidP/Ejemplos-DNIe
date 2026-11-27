@echo off
rem ================================================================================================
rem  registrar.cmd - Da a conocer DNIeDatos.exe a Chrome, Edge y Firefox, para el USUARIO ACTUAL.
rem
rem  No necesita permisos de administrador: escribe en HKEY_CURRENT_USER.
rem
rem  Uso:   scripts\registrar.cmd            elige el .exe del procesador de este equipo
rem         scripts\registrar.cmd x64        fuerza una plataforma: Win32, x64 o ARM64
rem
rem  COMO ENCUENTRA UN NAVEGADOR EL PROGRAMA
rem  Cuando la extension pide conectar con "es.fnmt.dniedatos", el navegador busca ese nombre en
rem  una clave del registro. La clave contiene la ruta de un archivo JSON, el "manifiesto del
rem  anfitrion", que dice donde esta el .exe y que extension puede usarlo. Cada navegador mira en
rem  su propia clave:
rem
rem    Chrome   HKCU\Software\Google\Chrome\NativeMessagingHosts\es.fnmt.dniedatos
rem    Edge     HKCU\Software\Microsoft\Edge\NativeMessagingHosts\es.fnmt.dniedatos
rem    Firefox  HKCU\Software\Mozilla\NativeMessagingHosts\es.fnmt.dniedatos
rem
rem  Hay dos manifiestos (anfitrion\manifiestos) porque cada familia identifica la extension a su
rem  manera: Chrome y Edge con "allowed_origins" y Firefox con "allowed_extensions".
rem
rem  QUE HACE
rem    1. Copia los dos manifiestos junto al .exe. En ellos la ruta del .exe es solo su nombre,
rem       "DNIeDatos.exe": en Windows los navegadores la entienden como relativa a la carpeta
rem       del manifiesto.
rem    2. Escribe las tres claves, apuntando a esas copias. Se registran los tres navegadores
rem       aunque alguno no este instalado: una clave de mas no molesta.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos se vean bien.
rem ================================================================================================
setlocal
chcp 65001 >nul

set "NOMBRE=es.fnmt.dniedatos"

rem --- Plataforma: la indicada o la del procesador de este equipo ---------------------------------
rem PROCESSOR_ARCHITECTURE es la del programa que pregunta: desde una consola de 32 bits en un
rem Windows de 64, la real esta en PROCESSOR_ARCHITEW6432.
set "PLATAFORMA=%~1"
set "PROCESADOR=%PROCESSOR_ARCHITEW6432%"
if not defined PROCESADOR set "PROCESADOR=%PROCESSOR_ARCHITECTURE%"
if not defined PLATAFORMA (
    if /i "%PROCESADOR%"=="AMD64" set "PLATAFORMA=x64"
    if /i "%PROCESADOR%"=="ARM64" set "PLATAFORMA=ARM64"
    if /i "%PROCESADOR%"=="x86"   set "PLATAFORMA=Win32"
)
if not defined PLATAFORMA (
    echo ERROR: no hay version del programa para el procesador de este equipo ^(%PROCESADOR%^).
    exit /b 1
)

rem --- Carpeta raiz del proyecto, sin ".." en la ruta ---------------------------------------------
for %%i in ("%~dp0..") do set "RAIZ=%%~fi"
set "CARPETA=%RAIZ%\bin\%PLATAFORMA%\Release"
set "MANIFIESTOS=%RAIZ%\anfitrion\manifiestos"

if not exist "%CARPETA%\DNIeDatos.exe" (
    echo ERROR: no existe "%CARPETA%\DNIeDatos.exe".
    echo        Ejecute antes scripts\compilar.cmd, o indique otra plataforma:
    echo        scripts\registrar.cmd x64
    exit /b 1
)

rem --- 1. Manifiestos junto al .exe ---------------------------------------------------------------
copy /y "%MANIFIESTOS%\%NOMBRE%.chromium.json" "%CARPETA%\" >nul
if errorlevel 1 ( echo ERROR al copiar el manifiesto de Chrome y Edge. & exit /b 1 )
copy /y "%MANIFIESTOS%\%NOMBRE%.firefox.json" "%CARPETA%\" >nul
if errorlevel 1 ( echo ERROR al copiar el manifiesto de Firefox. & exit /b 1 )

rem --- 2. Claves del registro ---------------------------------------------------------------------
rem   /ve  escribe el valor "(Predeterminado)" de la clave;  /f  sobrescribe sin preguntar.
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
echo Programa registrado ^(%PLATAFORMA%^).
echo Siguiente paso: cargar la extension en el navegador y abrir web\servir.cmd
endlocal
