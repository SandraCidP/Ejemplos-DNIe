@echo off
rem ================================================================================================
rem  abrir.cmd - Abre el ejemplo del ActiveX del DNIe (EjemploDNIe.hta) con el mshta.exe nativo
rem  del equipo (64 bits en Windows x64, ARM64 en equipos ARM), que carga el ActiveX registrado de
rem  esa misma arquitectura.
rem
rem  Para probar la DLL de 32 bits, ejecutar abrir.cmd 32
rem ================================================================================================
setlocal
set "MSHTA=%SystemRoot%\System32\mshta.exe"
rem Si este cmd es de 32 bits en un Windows de 64, System32 apunta a SysWOW64: Sysnative da el nativo
if exist "%SystemRoot%\Sysnative\mshta.exe" set "MSHTA=%SystemRoot%\Sysnative\mshta.exe"
if "%~1"=="32" if exist "%SystemRoot%\SysWOW64\mshta.exe" set "MSHTA=%SystemRoot%\SysWOW64\mshta.exe"
start "" "%MSHTA%" "%~dp0EjemploDNIe.hta"
endlocal
