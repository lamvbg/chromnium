@echo off
setlocal

REM ============================================================
REM  rebase.bat
REM  Rebase the chronium-base patch series onto a newer Chromium
REM  stable tag. Expect conflicts ~30%% on Blink files, ~10%% on
REM  chrome/. Resolve manually, then re-export patches\.
REM
REM  Usage:
REM    scripts\rebase.bat 131.0.6778.85
REM ============================================================

if "%~1"=="" (
    echo Usage: scripts\rebase.bat NEW_STABLE_TAG
    echo Example: scripts\rebase.bat 131.0.6778.85
    exit /b 1
)

set "NEW_TAG=%~1"
set "CR_ROOT=C:\cr"
set "SRC=%CR_ROOT%\src"
set "PATCHES=%~dp0..\patches"

if not exist "%SRC%\.git" (
    echo [FAIL] %SRC% not a git checkout.
    exit /b 1
)

pushd "%SRC%"

echo [1/4] Fetch new tags
call git fetch --tags
if errorlevel 1 (
    popd
    echo [FAIL] git fetch failed.
    exit /b 1
)

call git rev-parse --verify "refs/tags/%NEW_TAG%" >nul 2>&1
if errorlevel 1 (
    popd
    echo [FAIL] Tag %NEW_TAG% not found.
    exit /b 1
)

echo [2/4] Rebase chronium-base onto %NEW_TAG%
call git checkout chronium-base
call git rebase "refs/tags/%NEW_TAG%"
if errorlevel 1 (
    echo.
    echo [PAUSE] Rebase has conflicts. Resolve with:
    echo          git status
    echo          ^<edit files^>
    echo          git add ^<files^>
    echo          git rebase --continue
    echo        When done, run:
    echo          scripts\rebase.bat %NEW_TAG% (skip — already done up to conflict)
    echo        and re-run gclient sync + build.
    popd
    exit /b 1
)

echo [3/4] gclient sync to the new tag deps
call gclient sync --with_branch_heads --with_tags -j8 -D
if errorlevel 1 (
    popd
    echo [FAIL] gclient sync failed.
    exit /b 1
)
call gclient runhooks

echo [4/4] Re-export patches\
REM Wipe old patches but keep README.md
for %%p in ("%PATCHES%\*.patch") do del /q "%%p"

REM Export the commits between the new tag and HEAD as numbered patches.
call git format-patch "refs/tags/%NEW_TAG%..HEAD" -o "%PATCHES%"
if errorlevel 1 (
    popd
    echo [FAIL] git format-patch failed.
    exit /b 1
)

popd
echo.
echo ============================================================
echo  Rebased onto %NEW_TAG%. Patches re-exported to
echo    %PATCHES%
echo  Next: scripts\build.bat
echo ============================================================
exit /b 0
