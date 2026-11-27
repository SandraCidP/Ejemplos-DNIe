@echo off
rem ============================================================================
rem  registrar.cmd - Registra el control en Windows para el USUARIO ACTUAL.
rem
rem  No necesita permisos de administrador: escribe en HKEY_CURRENT_USER.
rem  Registra las dos versiones (32 y 64 bits), porque cada programa solo
rem  puede cargar la DLL de su mismo numero de bits.
rem
rem  Como funciona:
rem    regsvr32 /n /i:user  ->  llama a DllInstall(TRUE, "user") de la DLL
rem                             (ver DllMain.cpp).
rem    /s                   ->  silencioso: sin ventanas de confirmacion.
rem
rem  Hay DOS regsvr32 en un Windows de 64 bits, cada uno pensado para las DLL
rem  de su mundo:
rem    %SystemRoot%\SysWOW64\regsvr32.exe  ->  el de 32 bits (si, en "WOW64")
rem    %SystemRoot%\System32\regsvr32.exe  ->  el de 64 bits
rem  (En un equipo ARM64 este mismo script tambien registra las dos DLL.)
rem
rem  OJO: un programa ejecutado "como administrador" NO ve el registro por
rem  usuario (ver DllMain.cpp). Si va a usar el control desde programas
rem  elevados, registrelo para todo el equipo.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos o
rem  enes se muestren bien en los mensajes.
rem
rem  Para registrar para TODO EL EQUIPO (como administrador), usar en su lugar:
rem    regsvr32 ruta\DNIeActiveX.dll
rem ============================================================================
setlocal
chcp 65001 >nul

set "DLL32=%~dp0..\bin\Win32\Release\DNIeActiveX.dll"
set "DLL64=%~dp0..\bin\x64\Release\DNIeActiveX.dll"

if not exist "%DLL32%" (
    echo ERROR: no existe "%DLL32%". Ejecute antes scripts\compilar.cmd
    exit /b 1
)

if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
    rem --- Windows de 64 bits: registrar las dos ---
    "%SystemRoot%\SysWOW64\regsvr32.exe" /s /n /i:user "%DLL32%"
    if errorlevel 1 ( echo ERROR al registrar la DLL de 32 bits. & exit /b 1 )
    echo Registrada la DLL de 32 bits.

    "%SystemRoot%\System32\regsvr32.exe" /s /n /i:user "%DLL64%"
    if errorlevel 1 ( echo ERROR al registrar la DLL de 64 bits. & exit /b 1 )
    echo Registrada la DLL de 64 bits.
) else (
    rem --- Windows de 32 bits: solo existe un regsvr32 ---
    "%SystemRoot%\System32\regsvr32.exe" /s /n /i:user "%DLL32%"
    if errorlevel 1 ( echo ERROR al registrar la DLL de 32 bits. & exit /b 1 )
    echo Registrada la DLL de 32 bits.
)

echo.
echo Control registrado. Ya se puede abrir la pagina con web\abrir.cmd
endlocal
