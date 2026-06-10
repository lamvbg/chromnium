@echo off
setlocal

REM ============================================================
REM  package.bat
REM  Copy build output into release\chronium\, rename chrome.exe
REM  to chronium.exe, and patch Win32 resources (icon, ProductName).
REM
REM  Requires: tools\rcedit.exe (download from
REM    https://github.com/electron/rcedit/releases)
REM ============================================================

set "SRC_OUT=C:\cr\src\out\Release"
set "ROOT=%~dp0.."
set "DEST=%ROOT%\release\chronium"
set "RCEDIT=%ROOT%\tools\rcedit.exe"
set "ICON=%ROOT%\assets\chronium.ico"

if not exist "%SRC_OUT%\chrome.exe" (
    echo [FAIL] %SRC_OUT%\chrome.exe missing. Run scripts\build.bat first.
    exit /b 1
)

if exist "%DEST%" rmdir /s /q "%DEST%"
mkdir "%DEST%"

echo [1/4] Copying top-level binaries and data files
robocopy "%SRC_OUT%" "%DEST%" *.exe *.dll *.pak *.bin *.dat /NJH /NJS /NDL /NP /NFL >nul
if errorlevel 8 (
    echo [FAIL] robocopy of top-level files failed.
    exit /b 1
)

echo [2/4] Copying resource directories
for %%D in (Locales swiftshader MEIPreload resources) do (
    if exist "%SRC_OUT%\%%D" (
        xcopy /E /I /Y /Q "%SRC_OUT%\%%D" "%DEST%\%%D" >nul
    )
)

echo [3/4] Rename chrome.exe -^> chronium.exe
ren "%DEST%\chrome.exe" chronium.exe
if errorlevel 1 (
    echo [FAIL] Could not rename chrome.exe.
    exit /b 1
)

echo [4/4] rcedit branding
if not exist "%RCEDIT%" (
    echo [WARN] tools\rcedit.exe missing. Skipping resource patch.
    echo        Download from https://github.com/electron/rcedit/releases
    goto done
)

set "RCARGS="
if exist "%ICON%" set "RCARGS=%RCARGS% --set-icon ""%ICON%"""
set "RCARGS=%RCARGS% --set-version-string ""ProductName"" ""Chronium"""
set "RCARGS=%RCARGS% --set-version-string ""CompanyName"" ""Interlink"""
set "RCARGS=%RCARGS% --set-version-string ""FileDescription"" ""Chronium Browser"""
set "RCARGS=%RCARGS% --set-version-string ""OriginalFilename"" ""chronium.exe"""

call "%RCEDIT%" "%DEST%\chronium.exe" %RCARGS%
if errorlevel 1 (
    echo [WARN] rcedit failed. Binary is functional but un-branded.
)

:done
echo.
echo ============================================================
echo  Package ready: %DEST%\chronium.exe
echo.
echo  Quick smoke test:
echo    "%DEST%\chronium.exe" ^
echo        --user-data-dir=D:\profiles\test1 ^
echo        --fingerprint-profile=%ROOT%\config\example-profile.json ^
echo        --remote-debugging-port=9222
echo ============================================================
exit /b 0
