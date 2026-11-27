@echo off
rem ================================================================================================
rem  desregistrar.cmd - Deshace lo que hizo registrar.cmd: borra las tres claves del registro
rem  (Chrome, Edge y Firefox) del usuario actual. Desde ese momento los navegadores ya no
rem  encuentran el programa.
rem
rem  No borra el .exe ni los manifiestos copiados junto a el: estan en bin\, que se puede borrar
rem  entera.
rem
rem    /f      borra sin pedir confirmacion.
rem    2>nul   no muestra error si la clave no existia.
rem ================================================================================================
setlocal

set "NOMBRE=es.fnmt.dniedatos"

reg delete "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul
reg delete "HKCU\Software\Microsoft\Edge\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul
reg delete "HKCU\Software\Mozilla\NativeMessagingHosts\%NOMBRE%" /f >nul 2>nul

echo Registro del programa eliminado.
endlocal
