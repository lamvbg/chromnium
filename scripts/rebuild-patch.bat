@echo off
setlocal

REM ============================================================
REM  rebuild-patch.bat
REM  Incremental rebuild after editing C++ files in C:\cr\src.
REM  ninja figures out dependencies automatically. 1-10 min.
REM ============================================================

set "SRC=C:\cr\src"
set "OUT=out\Release"

if not exist "%SRC%\%OUT%\build.ninja" (
    echo [FAIL] No build.ninja in %SRC%\%OUT%. Run scripts\build.bat first.
    exit /b 1
)

pushd "%SRC%"
call autoninja -C "%OUT%" chrome
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
