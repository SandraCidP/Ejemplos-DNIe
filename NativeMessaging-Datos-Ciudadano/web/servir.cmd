@echo off
rem ============================================================================
rem  servir.cmd - Sirve la pagina de ejemplo en http://localhost:8081/
rem
rem  La extension solo atiende a los origenes de su lista (fondo.js), y el de
rem  la pagina de ejemplo es http://localhost:8081. Abierta con doble clic
rem  (file://) la pagina no puede usar la extension.
rem
rem  Es un servidor minimo hecho en PowerShell (servir.ps1): no hay que
rem  instalar nada. Se detiene con Ctrl+C o cerrando la ventana.
rem ============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0servir.ps1" %*
