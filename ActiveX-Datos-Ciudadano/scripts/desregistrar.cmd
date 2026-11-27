@echo off
rem ================================================================================================
rem  desregistrar.cmd - Deshace el registro que hace scripts\registrar.cmd (el del usuario actual).
rem
rem  No necesita permisos de administrador. Cierre antes la pagina, la HTA o el PowerShell que tengan
rem  cargado el ActiveX.
rem
rem  Si el control se registro para todo el equipo (regsvr32 sin /n /i:user), se quita como
rem  administrador con:   regsvr32 /u ruta\ActiveXDNIe_x64.dll
rem ================================================================================================
setlocal
chcp 65001 >nul

set "RAIZ=%~dp0.."
set "DLL32=%RAIZ%\Win32\Release\ActiveXDNIe_x86.dll"
set "DLL64=%RAIZ%\x64\Release\ActiveXDNIe_x64.dll"
set "DLLARM=%RAIZ%\ARM64\Release\ActiveXDNIe_arm64.dll"

set "ARQ=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "ARQ=%PROCESSOR_ARCHITEW6432%"

set "REGNATIVO=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGNATIVO=%SystemRoot%\Sysnative\regsvr32.exe"

if exist "%DLL32%" (
    if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
        "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u /n /i:user "%DLL32%"
    ) else (
        "%REGNATIVO%" /s /u /n /i:user "%DLL32%"
    )
    echo Desregistrada la DLL de 32 bits.
)
if /i "%ARQ%"=="AMD64" if exist "%DLL64%" (
    "%REGNATIVO%" /s /u /n /i:user "%DLL64%"
    echo Desregistrada la DLL de 64 bits.
)
if /i "%ARQ%"=="ARM64" if exist "%DLLARM%" (
    "%REGNATIVO%" /s /u /n /i:user "%DLLARM%"
    echo Desregistrada la DLL ARM64.
)

echo.
echo Registro del usuario actual deshecho.
endlocal
