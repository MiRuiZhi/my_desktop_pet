@echo off
setlocal enabledelayedexpansion

REM Check for admin privileges
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] This script must be run as Administrator!
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

echo ============================================
echo  HideProc Kernel Driver - Build & Install
echo ============================================
echo.

REM Check test signing
echo Checking test signing status...
bcdedit | findstr /i "testsigning" | findstr /i "Yes" >nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] Test signing is NOT enabled!
    echo.
    echo To enable test signing:
    echo   1. Open Command Prompt as Administrator
    echo   2. Run: bcdedit /set testsigning on
    echo   3. Reboot your computer
    echo.
    echo Without test signing, the driver will NOT load.
    echo.
    set /p CONTINUE="Continue anyway? (y/n): "
    if /i not "!CONTINUE!"=="y" exit /b 1
) else (
    echo [OK] Test signing is enabled
)
echo.

REM Find Visual Studio
set "VS_PATH="
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%p (
        set "VS_PATH=%%~p"
        goto :found_vs
    )
)

echo [ERROR] Visual Studio not found!
echo Please install VS 2019/2022 with "Desktop development with C++" and "Windows Driver Kit"
pause
exit /b 1

:found_vs
echo [OK] Found Visual Studio
call "%VS_PATH%"

REM Find MSBuild
set "MSBUILD_PATH="
for %%m in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
) do (
    if exist %%m (
        set "MSBUILD_PATH=%%~m"
        goto :found_msbuild
    )
)

where msbuild >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "MSBUILD_PATH=msbuild"
    goto :found_msbuild
)

echo [ERROR] MSBuild not found!
pause
exit /b 1

:found_msbuild
echo [OK] Found MSBuild
echo.

REM Build driver
cd /d "%~dp0driver"

echo ============================================
echo  Building kernel driver...
echo ============================================
echo.

"%MSBUILD_PATH%" hideproc.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Driver build failed!
    pause
    exit /b 1
)

REM Find the built driver
set "DRIVER_PATH="
if exist x64\Release\hideproc.sys set "DRIVER_PATH=%CD%\x64\Release\hideproc.sys"
if exist x64\release\hideproc.sys set "DRIVER_PATH=%CD%\x64\release\hideproc.sys"

if "!DRIVER_PATH!"=="" (
    for /r %%s in (hideproc.sys) do (
        if exist "%%s" set "DRIVER_PATH=%%s"
    )
)

if "!DRIVER_PATH!"=="" (
    echo [ERROR] Driver file not found after build!
    pause
    exit /b 1
)

echo [OK] Driver built: !DRIVER_PATH!
echo.

REM Install driver
echo ============================================
echo  Installing driver...
echo ============================================
echo.

REM Stop existing driver
sc stop HideProc >nul 2>&1
timeout /t 2 /nobreak >nul

REM Delete existing service
sc delete HideProc >nul 2>&1
timeout /t 1 /nobreak >nul

REM Create and start service
sc create HideProc binPath= "!DRIVER_PATH!" type= kernel
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to create service!
    pause
    exit /b 1
)

sc start HideProc
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to start driver!
    echo You may need to reboot after enabling test signing.
    sc delete HideProc >nul 2>&1
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Driver installed successfully!
echo ============================================
echo.
echo  The HideProc driver is now running.
echo  When screenshot_svc.exe starts, its process
echo  will be hidden from Task Manager and Process Explorer.
echo.
echo  To uninstall:
echo    sc stop HideProc
echo    sc delete HideProc
echo.
pause
