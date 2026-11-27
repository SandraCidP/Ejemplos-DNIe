@echo off
rem ============================================================================
rem  compilar.cmd - Compila DNIeActiveX.dll en Release para 32 y 64 bits.
rem
rem  Resultado:  bin\Win32\Release\DNIeActiveX.dll   (32 bits)
rem              bin\x64\Release\DNIeActiveX.dll     (64 bits)
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

set "PROYECTO=%~dp0..\src\DNIeActiveX\DNIeActiveX.vcxproj"

for %%p in (Win32 x64) do (
    echo.
    echo === Compilando %%p ===
    "%MSBUILD%" "%PROYECTO%" /p:Configuration=Release /p:Platform=%%p /nologo /v:minimal
    if errorlevel 1 (
        echo ERROR: la compilacion de %%p ha fallado.
        exit /b 1
    )
)

echo.
echo Compilacion correcta. Siguiente paso: scripts\registrar.cmd
endlocal
