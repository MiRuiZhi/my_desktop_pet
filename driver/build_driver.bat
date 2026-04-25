@echo off
echo ============================================
echo HideProc Driver Build Script
echo ============================================
echo.
echo Prerequisites:
echo   1. Visual Studio 2019/2022 with Windows Driver Kit (WDK)
echo   2. Test signing enabled: bcdedit /set testsigning on
echo.
echo Building driver...
echo.

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

msbuild hideproc.vcxproj /p:Configuration=Release /p:Platform=x64

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo Driver location: x64\Release\hideproc.sys
    echo.
    echo Installation:
    echo   1. sc create HideProc binPath= ^<full_path_to^>\hideproc.sys type= kernel
    echo   2. sc start HideProc
) else (
    echo.
    echo Build failed. Check errors above.
)

pause
