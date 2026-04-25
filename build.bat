@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  High Security Screenshot Service - Build
echo ============================================
echo.

REM Check if running in WSL
wsl uname -r >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [ERROR] This script must run in Windows CMD, not WSL!
    echo Please open a Windows Command Prompt and run this script.
    pause
    exit /b 1
)

REM Find Visual Studio
set "VS_PATH="
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
) do (
    if exist %%p (
        set "VS_PATH=%%~p"
        goto :found_vs
    )
)

echo [ERROR] Visual Studio not found!
echo Please install Visual Studio 2019/2022 with "Desktop development with C++" workload.
echo Download: https://visualstudio.microsoft.com/downloads/
pause
exit /b 1

:found_vs
echo [OK] Found Visual Studio: %VS_PATH%
echo.

REM Find CMake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] CMake not found in PATH, checking common locations...
    for %%c in (
        "C:\Program Files\CMake\bin\cmake.exe"
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    ) do (
        if exist %%c (
            set "CMAKE_PATH=%%~c"
            goto :found_cmake
        )
    )
    echo [ERROR] CMake not found! Please install CMake 3.15+
    echo Download: https://cmake.org/download/
    pause
    exit /b 1
) else (
    set "CMAKE_PATH=cmake"
)

:found_cmake
echo [OK] Found CMake: %CMAKE_PATH%
echo.

REM Setup build environment
echo Setting up build environment...
call "%VS_PATH%"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to setup Visual Studio environment
    pause
    exit /b 1
)

REM Create build directory
cd /d "%~dp0"
if exist build rmdir /s /q build
mkdir build
cd build

echo.
echo ============================================
echo  Building user-mode program...
echo ============================================
echo.

%CMAKE_PATH% .. -G "Visual Studio 17 2022" -A x64 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] VS2022 generator failed, trying VS2019...
    %CMAKE_PATH% .. -G "Visual Studio 16 2019" -A x64
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] CMake configuration failed!
        pause
        exit /b 1
    )
)

%CMAKE_PATH% --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
if exist Release\screenshot_svc.exe (
    echo [OK] Build successful!
    echo     Output: %~dp0build\Release\screenshot_svc.exe
    copy Release\screenshot_svc.exe ..\screenshot_svc.exe >nul
    echo     Copied to: %~dp0screenshot_svc.exe
) else (
    echo [WARN] Executable not found in Release folder, checking other locations...
    for /r %%e in (screenshot_svc.exe) do (
        if exist "%%e" (
            echo [OK] Found: %%e
            copy "%%e" ..\screenshot_svc.exe >nul
            echo     Copied to: %~dp0screenshot_svc.exe
        )
    )
)

echo.
echo ============================================
echo  Build Complete!
echo ============================================
echo.
echo  Next steps:
echo  1. Double-click screenshot_svc.exe to start the service
echo  2. Press Ctrl+Shift+Alt+P to take a screenshot
echo  3. Screenshots saved to: %%LOCALAPPDATA%%\screenshot-service\screenshots\
echo.
echo  For kernel driver (maximum stealth), run install_driver.bat as Administrator
echo.
pause
