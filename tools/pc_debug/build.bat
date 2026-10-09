@echo off
rem ============================================================
rem  build.bat - Build ai_debug_server.exe (one click)
rem
rem  Requirement: Visual Studio 2022 (Community is fine)
rem  Usage:       Double-click this file, or run "build.bat"
rem  Output:      ai_debug_server.exe in the same folder
rem ============================================================
setlocal

rem Switch to this script's own folder (so double-click works from anywhere)
cd /d "%~dp0"

echo === Finding Visual Studio build environment ...

rem ---- 1. Locate vcvarsall.bat ----
set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"

if "%VCVARS%"=="" (
    echo [ERROR] Visual Studio build environment not found!
    echo Please install Visual Studio 2022 with "Desktop development with C++"
    echo Download: https://visualstudio.microsoft.com/downloads/
    pause
    exit /b 1
)

rem ---- 2. Setup environment ----
echo Using: %VCVARS%
call "%VCVARS%" x64
if errorlevel 1 (
    echo [ERROR] vcvarsall.bat failed
    pause
    exit /b 1
)

rem ---- 3. Compile ----
echo.
echo === Compiling ...
cl /nologo /W3 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS main.c serial.c /Fe:ai_debug_server.exe /link kernel32.lib user32.lib
if errorlevel 1 (
    echo.
    echo [ERROR] Compile failed
    pause
    exit /b 1
)

rem ---- 4. Done ----
echo.
echo === Build OK! Output: ai_debug_server.exe
echo.
echo Usage:  ai_debug_server.exe COM3
echo          or double-click to list ports and pick one
echo.
pause
