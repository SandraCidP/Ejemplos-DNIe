@echo off
rem ============================================================================
rem  desregistrar.cmd - Quita el registro del control hecho por registrar.cmd
rem  (el del usuario actual). Hace lo contrario: regsvr32 /u /n /i:user llama
rem  a DllInstall(FALSE, "user"), que borra las claves de HKEY_CURRENT_USER.
rem
rem  ("chcp 65001" pone la consola en UTF-8; ver registrar.cmd.)
rem ============================================================================
setlocal
chcp 65001 >nul

set "DLL32=%~dp0..\bin\Win32\Release\DNIeActiveX.dll"
set "DLL64=%~dp0..\bin\x64\Release\DNIeActiveX.dll"

if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
    if exist "%DLL32%" "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u /n /i:user "%DLL32%"
    if exist "%DLL64%" "%SystemRoot%\System32\regsvr32.exe" /s /u /n /i:user "%DLL64%"
) else (
    if exist "%DLL32%" "%SystemRoot%\System32\regsvr32.exe" /s /u /n /i:user "%DLL32%"
)

echo Registro del control eliminado.
endlocal
