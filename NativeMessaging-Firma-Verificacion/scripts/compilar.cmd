@echo off
rem ============================================================================
rem  compilar.cmd - Compila DNIeAnfitrion.exe en Release.
rem
rem  Resultado:  bin\x64\Release\DNIeAnfitrion.exe     (Intel/AMD de 64 bits)
rem              bin\ARM64\Release\DNIeAnfitrion.exe   (ARM de 64 bits)
rem
rem  La plataforma x64 se compila siempre. ARM64 solo si Visual Studio tiene
rem  instaladas las herramientas de C++ de esa plataforma; si no, se omite
rem  con un aviso (no es un error: cada equipo solo necesita el .exe
rem  de su propio procesador).
rem
rem  Necesita Visual Studio 2022 o las "Build Tools" con las herramientas de
rem  C++ y el SDK de Windows. No hace falta abrir ninguna consola especial:
rem  el script localiza MSBuild por si mismo con vswhere.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos y
rem  los mensajes de MSBuild se muestren bien.
rem ============================================================================
setlocal
chcp 65001 >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: no se encuentra Visual Studio ni sus Build Tools ^(falta vswhere.exe^).
    exit /b 1
)

rem vswhere devuelve la ruta de MSBuild.exe de la instalacion mas reciente.
rem   -products *  incluye las "Build Tools" ademas de Visual Studio.
rem   -all         incluye instalaciones que el instalador marca como
rem                incompletas (p. ej. con una actualizacion pendiente), que
rem                normalmente compilan sin problema.
set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -latest -products * -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo ERROR: no se encuentra MSBuild.exe.
    exit /b 1
)

set "PROYECTO=%~dp0..\anfitrion\DNIeAnfitrion.vcxproj"

rem --- x64: obligatoria ------------------------------------------------------
echo.
echo === Compilando x64 ===
"%MSBUILD%" "%PROYECTO%" /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
if errorlevel 1 (
    echo ERROR: la compilacion de x64 ha fallado.
    exit /b 1
)

rem --- ARM64: solo si estan sus herramientas ------------------------------------
rem Para saber si estan, se busca la biblioteca de C++ de esa plataforma
rem (libcmt.lib) dentro de Visual Studio. El nombre de la carpeta va en
rem minusculas: lib\arm64.
call :compilar_opcional ARM64 arm64

echo.
echo Compilacion terminada. Siguiente paso: scripts\registrar.cmd
endlocal
exit /b 0

rem ============================================================================
rem  :compilar_opcional  PLATAFORMA  CARPETA_DE_BIBLIOTECAS
rem  Compila una plataforma si sus herramientas estan instaladas. Un fallo
rem  aqui se avisa, pero no detiene el script.
rem ============================================================================
:compilar_opcional
set "HERRAMIENTAS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -latest -products * -find VC\Tools\MSVC\**\lib\%2\libcmt.lib`) do set "HERRAMIENTAS=%%i"
echo.
if not defined HERRAMIENTAS (
    echo === %1: omitida ^(no estan instaladas las herramientas de C++ para %1^) ===
    exit /b 0
)
echo === Compilando %1 ===
"%MSBUILD%" "%PROYECTO%" /p:Configuration=Release /p:Platform=%1 /nologo /v:minimal
if errorlevel 1 (
    echo AVISO: la compilacion de %1 ha fallado. Compruebe que el SDK de Windows
    echo        instalado incluye las bibliotecas de %1 ^(ver la guia, apartado 3^).
)
exit /b 0
