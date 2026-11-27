@echo off
rem ================================================================================================
rem  registrar.cmd - Registra el ActiveX del DNIe en Windows para el USUARIO ACTUAL.
rem
rem  No necesita permisos de administrador: escribe en HKEY_CURRENT_USER.
rem
rem  Registra cada DLL con el regsvr32 de su arquitectura, porque un programa solo puede cargar una
rem  DLL de su misma arquitectura:
rem    %SystemRoot%\SysWOW64\regsvr32.exe  ->  el de 32 bits (si, en "WOW64"): ActiveXDNIe_x86.dll
rem    %SystemRoot%\System32\regsvr32.exe  ->  el nativo: ActiveXDNIe_x64.dll en un Windows x64,
rem                                            ActiveXDNIe_arm64.dll en un Windows ARM64
rem
rem  Como funciona:
rem    regsvr32 /n /i:user  ->  llama a DllInstall(TRUE, "user") de la DLL (ver ActiveXDNIe.cpp)
rem    /s                   ->  silencioso: sin ventanas de confirmacion
rem
rem  OJO: un programa ejecutado "como administrador" NO ve el registro por usuario. Si va a usar el
rem  control desde programas elevados, registrelo para todo el equipo, como administrador:
rem    regsvr32 ruta\ActiveXDNIe_x64.dll
rem
rem  Registra las DLL de Release (compile antes con scripts\compilar.cmd). Tras recompilar no hace
rem  falta registrar de nuevo: la DLL sigue en la misma ruta.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos se vean bien.
rem ================================================================================================
setlocal
chcp 65001 >nul

set "RAIZ=%~dp0.."
set "DLL32=%RAIZ%\Win32\Release\ActiveXDNIe_x86.dll"
set "DLL64=%RAIZ%\x64\Release\ActiveXDNIe_x64.dll"
set "DLLARM=%RAIZ%\ARM64\Release\ActiveXDNIe_arm64.dll"

rem Arquitectura real de Windows (PROCESSOR_ARCHITEW6432 existe si esta consola es de 32 bits)
set "ARQ=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "ARQ=%PROCESSOR_ARCHITEW6432%"

rem regsvr32 nativo: si esta consola es de 32 bits, System32 apunta a SysWOW64 y Sysnative al nativo
set "REGNATIVO=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGNATIVO=%SystemRoot%\Sysnative\regsvr32.exe"

set "HECHO="

rem --- 32 bits ---
if exist "%DLL32%" (
    if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
        "%SystemRoot%\SysWOW64\regsvr32.exe" /s /n /i:user "%DLL32%"
    ) else (
        "%REGNATIVO%" /s /n /i:user "%DLL32%"
    )
    if errorlevel 1 ( echo ERROR al registrar la DLL de 32 bits. & exit /b 1 )
    echo Registrada la DLL de 32 bits.
    set "HECHO=1"
)

rem --- Nativa: x64 o ARM64 ---
if /i "%ARQ%"=="AMD64" if exist "%DLL64%" (
    "%REGNATIVO%" /s /n /i:user "%DLL64%"
    if errorlevel 1 ( echo ERROR al registrar la DLL de 64 bits. & exit /b 1 )
    echo Registrada la DLL de 64 bits.
    set "HECHO=1"
)
if /i "%ARQ%"=="ARM64" if exist "%DLLARM%" (
    "%REGNATIVO%" /s /n /i:user "%DLLARM%"
    if errorlevel 1 ( echo ERROR al registrar la DLL ARM64. & exit /b 1 )
    echo Registrada la DLL ARM64.
    set "HECHO=1"
)

if not defined HECHO (
    echo ERROR: no hay ninguna DLL compilada. Ejecute antes scripts\compilar.cmd
    exit /b 1
)

echo.
echo ActiveX registrado para el usuario actual. Ya se puede abrir la pagina con web\abrir.cmd
endlocal
