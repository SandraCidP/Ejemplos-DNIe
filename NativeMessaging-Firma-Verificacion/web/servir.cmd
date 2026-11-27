@echo off
rem ============================================================================
rem  servir.cmd - Sirve la pagina de ejemplo en http://localhost:8080/ y la
rem  abre en el navegador predeterminado.
rem
rem  POR QUE HACE FALTA UN SERVIDOR
rem  La extension solo actua en paginas de los origenes de su lista, y un
rem  archivo abierto con doble clic (file://...) no tiene un origen util.
rem  Este script arranca un servidor web minimo, escrito en PowerShell
rem  (servir.ps1), que no necesita instalar nada.
rem
rem  Para detenerlo: Ctrl+C en esta ventana, o cerrarla.
rem
rem    -ExecutionPolicy Bypass   permite ejecutar el script aunque el equipo
rem                              tenga restringidos los scripts de PowerShell.
rem ============================================================================
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0servir.ps1"
