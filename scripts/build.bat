@echo off
setlocal

REM ============================================================
REM  build.bat
REM  Run `gn gen` with config\args.gn then `autoninja -C out\Release chrome`.
REM
REM  First clean build: 4-8 hours on a 12-core box.
REM  Incremental rebuild: use scripts\rebuild-patch.bat instead.
REM ============================================================

set "CR_ROOT=C:\cr"
set "SRC=%CR_ROOT%\src"
set "OUT=out\Release"
set "ARGS_SRC=%~dp0..\config\args.gn"

if not exist "%SRC%\.git" (
    echo [FAIL] %SRC% not a git checkout. Run scripts\fetch.bat first.
    exit /b 1
)

if not exist "%ARGS_SRC%" (
    echo [FAIL] %ARGS_SRC% missing.
    exit /b 1
)

pushd "%SRC%"

if not exist "%OUT%" mkdir "%OUT%"

echo [1/2] Copy args.gn and gn gen
copy /Y "%ARGS_SRC%" "%OUT%\args.gn" >nul
call gn gen "%OUT%"
if errorlevel 1 (
    popd
    echo [FAIL] gn gen failed. Check args.gn syntax.
    exit /b 1
)

echo.
echo [2/2] autoninja -C %OUT% chrome -j 12
echo       ^(this is the long step — go get coffee^)
echo       Using -j 12 to cap parallel compile jobs at 12 (need ~24 GB RAM peak).
echo       If you have 64+ GB RAM, bump to -j 24 for ~2x speed.
call autoninja -C "%OUT%" -j 12 chrome
if errorlevel 1 (
    popd
    echo [FAIL] autoninja failed. See log above for compile error.
    exit /b 1
)

popd
echo.
echo ============================================================
echo  Build OK: %SRC%\%OUT%\chrome.exe
echo  Next: scripts\package.bat
echo ============================================================
exit /b 0
