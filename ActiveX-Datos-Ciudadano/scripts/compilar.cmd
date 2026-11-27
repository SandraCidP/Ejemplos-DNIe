@echo off
rem ================================================================================================
rem  compilar.cmd - Compila el ActiveX del DNIe en Release, sin abrir Visual Studio ni una consola
rem  especial ("Developer PowerShell").
rem
rem  Uso:   scripts\compilar.cmd              compila x86, x64 y ARM64
rem         scripts\compilar.cmd x64          solo una plataforma (x86, x64 o ARM64)
rem
rem  Resultado:   Win32\Release\ActiveXDNIe_x86.dll
rem               x64\Release\ActiveXDNIe_x64.dll
rem               ARM64\Release\ActiveXDNIe_arm64.dll
rem
rem  COMO FUNCIONA
rem    1. Localiza MSBuild con vswhere.exe, que instala Visual Studio (o sus Build Tools) siempre en
rem       la misma carpeta. -products * incluye las Build Tools; -all, instalaciones que el
rem       instalador marca como incompletas (p. ej. con una actualizacion pendiente).
rem    2. Si la DLL de destino esta en uso (la tiene cargada una pagina, una HTA o un PowerShell
rem       abiertos), Windows no deja sobrescribirla y el enlazador falla con LNK1104. Pero si deja
rem       RENOMBRARLA: se renombra a ActiveXDNIe_xxx.old_<fecha-hora>.dll y se compila la nueva. El
rem       programa que la tenia cargada sigue usando la antigua hasta que se cierre. Los .old se
rem       pueden borrar cuando ya no esten en uso (no se suben al repositorio: *.dll esta ignorado).
rem    3. Compila la solucion con MSBuild para cada plataforma.
rem
rem  Tras compilar no hace falta registrar de nuevo: la DLL sigue en la misma ruta.
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

set "RAIZ=%~dp0.."
set "SOLUCION=%RAIZ%\ActiveXDNIe.sln"

rem Marca de fecha y hora para el nombre de las DLL renombradas (aaaammdd_hhmmss)
for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"`) do set "MARCA=%%t"

rem Plataformas: la que se indique, o las tres. ARM64 solo si estan instaladas sus herramientas de
rem C++ con ATL (componente Microsoft.VisualStudio.Component.VC.ATL.ARM64); si no, se avisa y se
rem compilan x86 y x64.
set "PLATAFORMAS=x86 x64 ARM64"
if "%~1"=="" (
    set "ATLARM="
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.ATL.ARM64 -property installationPath`) do set "ATLARM=%%i"
    if not defined ATLARM (
        set "PLATAFORMAS=x86 x64"
        echo AVISO: no estan instaladas las herramientas de C++ con ATL para ARM64; se compilan x86 y x64.
    )
) else (
    set "PLATAFORMAS=%~1"
)

for %%p in (%PLATAFORMAS%) do (
    rem Carpeta y nombre de la DLL de cada plataforma (ver ActiveXDNIe.vcxproj)
    set "CARPETA="
    if /i "%%p"=="x86"   ( set "CARPETA=Win32" & set "DLL=ActiveXDNIe_x86" )
    if /i "%%p"=="x64"   ( set "CARPETA=x64"   & set "DLL=ActiveXDNIe_x64" )
    if /i "%%p"=="ARM64" ( set "CARPETA=ARM64" & set "DLL=ActiveXDNIe_arm64" )
    if not defined CARPETA (
        echo ERROR: plataforma desconocida "%%p". Use x86, x64 o ARM64.
        exit /b 1
    )

    set "DESTINO=%RAIZ%\!CARPETA!\Release\!DLL!.dll"
    if exist "!DESTINO!" (
        rem Si se puede abrir en exclusiva, no esta en uso; si no, se renombra
        powershell -NoProfile -Command "try { [IO.File]::Open('!DESTINO!', 'Open', 'ReadWrite', 'None').Close(); exit 0 } catch { exit 1 }"
        if errorlevel 1 (
            ren "!DESTINO!" "!DLL!.old_!MARCA!.dll" && echo La DLL de %%p estaba en uso: renombrada a !DLL!.old_!MARCA!.dll
        )
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
echo Compilacion correcta.
endlocal
