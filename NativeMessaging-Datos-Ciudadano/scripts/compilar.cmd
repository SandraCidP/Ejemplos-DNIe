@echo off
rem ================================================================================================
rem  compilar.cmd - Compila DNIeDatos.exe en Release, sin abrir Visual Studio ni una consola
rem  especial ("Developer PowerShell").
rem
rem  Uso:   scripts\compilar.cmd              compila x86, x64 y ARM64
rem         scripts\compilar.cmd x64          solo una plataforma (x86, x64 o ARM64)
rem
rem  Resultado:   bin\Win32\Release\DNIeDatos.exe     Windows de 32 bits
rem               bin\x64\Release\DNIeDatos.exe       Intel/AMD de 64 bits
rem               bin\ARM64\Release\DNIeDatos.exe     ARM de 64 bits
rem
rem  COMO FUNCIONA
rem    1. Localiza MSBuild con vswhere.exe, que instala Visual Studio (o sus Build Tools) siempre en
rem       la misma carpeta. -products * incluye las Build Tools; -all, instalaciones que el
rem       instalador marca como incompletas (p. ej. con una actualizacion pendiente).
rem    2. Compila la solucion con MSBuild para cada plataforma. ARM64 solo si estan instaladas sus
rem       herramientas de C++ (componente Microsoft.VisualStudio.Component.VC.Tools.ARM64); si no,
rem       se avisa y se compilan x86 y x64.
rem
rem  El navegador arranca el .exe solo mientras atiende una peticion, asi que normalmente no esta
rem  en uso al compilar. Tras compilar no hace falta registrar de nuevo: sigue en la misma ruta.
rem
rem  "chcp 65001" pone la consola en UTF-8, para que las rutas con acentos se vean bien.
rem ================================================================================================
setlocal EnableDelayedExpansion
chcp 65001 >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: no se encuentra Visual Studio ni sus Build Tools ^(falta vswhere.exe^).
    exit /b 1
)

set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -latest -products * -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo ERROR: no se encuentra MSBuild.exe.
    exit /b 1
)

set "SOLUCION=%~dp0..\DNIeDatos.sln"

set "PLATAFORMAS=x86 x64 ARM64"
if "%~1"=="" (
    set "ARM="
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath`) do set "ARM=%%i"
    if not defined ARM (
        set "PLATAFORMAS=x86 x64"
        echo AVISO: no estan instaladas las herramientas de C++ para ARM64; se compilan x86 y x64.
    )
) else (
    set "PLATAFORMAS=%~1"
)

for %%p in (%PLATAFORMAS%) do (
    set "VALIDA="
    if /i "%%p"=="x86"   set "VALIDA=1"
    if /i "%%p"=="x64"   set "VALIDA=1"
    if /i "%%p"=="ARM64" set "VALIDA=1"
    if not defined VALIDA (
        echo ERROR: plataforma desconocida "%%p". Use x86, x64 o ARM64.
        exit /b 1
    )
    echo.
    echo === Compilando %%p ===
    "%MSBUILD%" "%SOLUCION%" /p:Configuration=Release /p:Platform=%%p /nologo /v:minimal
    if errorlevel 1 (
        echo ERROR: la compilacion de %%p ha fallado.
        exit /b 1
    )
)

echo.
echo Compilacion correcta. Siguiente paso: scripts\registrar.cmd
endlocal
