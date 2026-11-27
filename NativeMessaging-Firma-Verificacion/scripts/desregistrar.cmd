@echo off
rem ============================================================================
rem  desregistrar.cmd - Deshace lo que hizo registrar.cmd: borra las tres
rem  claves del registro (Chrome, Edge y Firefox) del usuario actual. Desde
rem  ese momento los navegadores ya no encuentran el programa de firma.
rem
rem  No borra el .exe ni los manifiestos copiados junto a el: estan en bin\,
rem  que se puede eliminar entera.
rem
rem    /f      borra sin pedir confirmacion.
rem    2>nul   no muestra error si la clave no existia.
rem ============================================================================
setlocal

set "NOMBRE=es.fnmt.dniefirma"

reg delete "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul
reg delete "HKCU\Software\Microsoft\Edge\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul
reg delete "HKCU\Software\Mozilla\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul

echo Registro del programa de firma eliminado.
endlocal
