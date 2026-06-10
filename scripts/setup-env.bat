@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  setup-env.bat
REM  Verify the local environment matches Chronium build requirements.
REM  Exits non-zero (and prints what's missing) on the first hard failure
REM  so build.bat can bail out early.
REM ============================================================

set "ERRORS=0"

echo.
echo === Chronium build environment check ===
echo.

call :check_vs
call :check_sdk
call :check_dbgtools
call :check_python
call :check_depot_tools
call :check_env_vars
call :check_disk
call :check_cpu_ram

echo.
if !ERRORS! GTR 0 (
    echo ============================================================
    echo  !ERRORS! prerequisite^(s^) missing. Fix before running fetch.bat.
    echo ============================================================
    exit /b 1
)

echo ============================================================
echo  Environment looks OK. Next: scripts\fetch.bat
echo ============================================================
exit /b 0


REM ============================================================
REM  Subroutines
REM ============================================================

:check_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [FAIL] vswhere.exe not found at "%VSWHERE%"
    echo        Install Visual Studio Installer or VS 2022 Build Tools:
    echo          https://aka.ms/vs/17/release/vs_BuildTools.exe
    set /a ERRORS+=1
    goto :eof
)
set "VS_INSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    set "VS_INSTALL=%%i"
)
if not defined VS_INSTALL (
    echo [FAIL] VS 2022 with MSVC v143 x64/x86 build tools not installed.
    echo        Install workload "Desktop development with C++" with MSVC v143.
    set /a ERRORS+=1
    goto :eof
)
echo [ OK ] VS install: !VS_INSTALL!
goto :eof


:check_sdk
set "SDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10\Include"
set "SDK_FOUND="
if exist "%SDK_ROOT%" (
    for /d %%d in ("%SDK_ROOT%\10.0.*") do set "SDK_FOUND=%%~nxd"
)
if not defined SDK_FOUND (
    echo [FAIL] No Windows 10/11 SDK found under "%SDK_ROOT%".
    echo        Install Windows 11 SDK 10.0.22621.0 via the VS Installer
    echo        ^(workload "Desktop development with C++"^).
    set /a ERRORS+=1
    goto :eof
)
echo [ OK ] Windows SDK: !SDK_FOUND!
goto :eof


:check_dbgtools
set "DBGHELP="
if exist "%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64\dbghelp.dll" set "DBGHELP=found"
if not defined DBGHELP if exist "%ProgramFiles%\Windows Kits\10\Debuggers\x64\dbghelp.dll" set "DBGHELP=found"
if not defined DBGHELP (
    echo [FAIL] Debugging Tools for Windows not found ^(dbghelp.dll^).
    echo        Chromium's vs_toolchain.py copies dbghelp.dll into the build
    echo        output during gn gen — this is required, not optional.
    echo        Install via one of:
    echo        1. Open "Apps and Features" -^> Windows Software Development Kit
    echo           -^> Modify -^> check "Debugging Tools for Windows" -^> Change.
    echo        2. Or download the standalone Win 11 SDK installer:
    echo             https://developer.microsoft.com/windows/downloads/windows-sdk/
    echo           and tick only "Debugging Tools for Windows".
    set /a ERRORS+=1
    goto :eof
)
echo [ OK ] Debugging Tools present.
goto :eof


:check_python
where python >nul 2>&1
if errorlevel 1 (
    echo [FAIL] python not found on PATH.
    echo        Install python.org Python 3.11+ and add to PATH.
    set /a ERRORS+=1
    goto :eof
)
REM Detect Microsoft Store stub: WindowsApps in resolved path
set "PY_PATH="
for /f "delims=" %%p in ('where python') do (
    if not defined PY_PATH set "PY_PATH=%%p"
)
echo !PY_PATH! | findstr /I "WindowsApps" >nul
if not errorlevel 1 (
    echo [FAIL] MS Store Python detected at !PY_PATH!
    echo        Symlinks disabled — depot_tools gclient sync will fail.
    echo        1. Settings -^> Apps -^> App execution aliases -^> turn off
    echo           "App Installer" python.exe and python3.exe.
    echo        2. Install python.org Python 3.11+ ^(check "Add to PATH"^).
    set /a ERRORS+=1
    goto :eof
)
for /f "tokens=2" %%v in ('python --version 2^>^&1') do set "PY_VER=%%v"
echo [ OK ] python !PY_VER! at !PY_PATH!
goto :eof


:check_depot_tools
where gclient >nul 2>&1
if errorlevel 1 (
    echo [FAIL] gclient not on PATH. depot_tools missing.
    echo        1. Download https://storage.googleapis.com/chrome-infra/depot_tools.zip
    echo        2. Extract to C:\cr\depot_tools
    echo        3. Prepend C:\cr\depot_tools to system PATH ^(before any other Python^).
    set /a ERRORS+=1
    goto :eof
)
set "GCLIENT_PATH="
for /f "delims=" %%g in ('where gclient') do (
    if not defined GCLIENT_PATH set "GCLIENT_PATH=%%g"
)
echo [ OK ] gclient at !GCLIENT_PATH!
goto :eof


:check_env_vars
if not "%DEPOT_TOOLS_WIN_TOOLCHAIN%"=="0" (
    echo [WARN] DEPOT_TOOLS_WIN_TOOLCHAIN is not "0".
    echo        Set system env var DEPOT_TOOLS_WIN_TOOLCHAIN=0 to use local VS install.
)
if not defined GYP_MSVS_VERSION (
    echo [WARN] GYP_MSVS_VERSION not set. Recommended: GYP_MSVS_VERSION=2022
)
if not defined vs2022_install (
    echo [WARN] vs2022_install env var not set. Recommended:
    echo        vs2022_install=C:\Program Files ^(x86^)\Microsoft Visual Studio\2022\BuildTools
)
goto :eof


:check_disk
for /f "tokens=3" %%a in ('dir C:\ ^| findstr /C:"bytes free"') do set "FREE_BYTES=%%a"
if defined FREE_BYTES (
    echo [info] Free space on C:\ !FREE_BYTES! bytes ^(need ~150 GB = 161061273600^)
) else (
    echo [info] Could not determine free space on C:\
)
goto :eof


:check_cpu_ram
set "CORES="
for /f "skip=1 tokens=*" %%p in ('wmic cpu get NumberOfLogicalProcessors 2^>nul') do (
    if not defined CORES (
        for /f "tokens=1" %%n in ("%%p") do set "CORES=%%n"
    )
)
if defined CORES echo [info] Logical CPUs: !CORES! ^(need ^>=8^)

set "RAM_BYTES="
for /f "skip=1 tokens=*" %%m in ('wmic computersystem get TotalPhysicalMemory 2^>nul') do (
    if not defined RAM_BYTES (
        for /f "tokens=1" %%n in ("%%m") do set "RAM_BYTES=%%n"
    )
)
if defined RAM_BYTES echo [info] RAM bytes: !RAM_BYTES! ^(need ^>=32 GB = 34359738368^)
goto :eof
