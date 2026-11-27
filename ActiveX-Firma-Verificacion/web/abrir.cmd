@echo off
rem ============================================================================
rem  abrir.cmd - Abre la pagina de firma (DNIeFirma.hta) con el mshta.exe de
rem  32 bits.
rem
rem  Por que el de 32 bits: la DLL de 32 bits del control funciona en
rem  cualquier Windows (x86, x64 y ARM64). En cambio, en un equipo ARM64 el
rem  mshta.exe "normal" es ARM64 y no puede cargar ninguna de las dos DLL que
rem  generamos (x86 y x64). Usando siempre el de 32 bits, funciona en todos.
rem
rem  En un Windows de 64 bits los programas de 32 bits estan en SysWOW64.
rem ============================================================================
setlocal
set "MSHTA=%SystemRoot%\SysWOW64\mshta.exe"
rem En un Windows de 32 bits no existe SysWOW64: su unico mshta.exe (de 32
rem bits) esta en System32.
if not exist "%MSHTA%" set "MSHTA=%SystemRoot%\System32\mshta.exe"
start "" "%MSHTA%" "%~dp0DNIeFirma.hta"
endlocal
