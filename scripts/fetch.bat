@echo off
setlocal

REM ============================================================
REM  fetch.bat
REM  Fetch Chromium source into C:\cr\src and pin to STABLE_TAG.
REM
REM  Usage:
REM    scripts\fetch.bat                    (uses STABLE_TAG below)
REM    scripts\fetch.bat 130.0.6723.117     (override tag)
REM
REM  This is a one-time step (~1-2 hours, ~50 GB download).
REM ============================================================

REM ---- Edit this when bumping Chromium version ----
set "STABLE_TAG=148.0.7778.217"
REM -------------------------------------------------

if not "%~1"=="" set "STABLE_TAG=%~1"

set "CR_ROOT=C:\cr"
set "SRC=%CR_ROOT%\src"

echo.
echo === Fetching Chromium %STABLE_TAG% into %CR_ROOT% ===
echo.

if not exist "%CR_ROOT%" mkdir "%CR_ROOT%"

where gclient >nul 2>&1
if errorlevel 1 (
    echo [FAIL] gclient not on PATH. Run scripts\setup-env.bat first.
    exit /b 1
)

if exist "%SRC%\.git" (
    echo [info] %SRC% already exists. Skipping fetch, going straight to checkout.
    goto checkout
)

echo [1/3] fetch --nohooks chromium
pushd "%CR_ROOT%"
call fetch --nohooks chromium
if errorlevel 1 (
    popd
    echo [FAIL] fetch failed. Check network + disk space.
    exit /b 1
)
popd

:checkout
echo.
echo [2/3] Checking out tag refs/tags/%STABLE_TAG%
pushd "%SRC%"
call git fetch --tags
if errorlevel 1 (
    popd
    echo [FAIL] git fetch failed.
    exit /b 1
)

REM Create or reset chronium-base branch
git rev-parse --verify chronium-base >nul 2>&1
if errorlevel 1 (
    call git checkout -b chronium-base refs/tags/%STABLE_TAG%
) else (
    echo [info] chronium-base branch exists. Resetting to refs/tags/%STABLE_TAG%
    call git checkout chronium-base
    call git reset --hard refs/tags/%STABLE_TAG%
)
if errorlevel 1 (
    popd
    echo [FAIL] git checkout failed. Tag %STABLE_TAG% may not exist.
    echo        See https://chromiumdash.appspot.com/releases?platform=Windows
    exit /b 1
)

echo.
echo [3/3] gclient sync (this takes ~1 hour)
echo       Using -j4 to avoid Google rate limit (HTTP 429).
call gclient sync --no-history --with_branch_heads --with_tags -j4 -D
if errorlevel 1 (
    popd
    echo.
    echo [FAIL] gclient sync failed.
    echo        If the error mentions HTTP 429 / RESOURCE_EXHAUSTED,
    echo        wait 10-15 minutes for the rate limit to reset, then
    echo        re-run scripts\fetch.bat. It will skip the slow main
    echo        clone and resume the submodule sync.
    exit /b 1
)
call gclient runhooks
if errorlevel 1 (
    popd
    echo [FAIL] gclient runhooks failed.
    exit /b 1
)
popd

echo.
echo ============================================================
echo  Chromium %STABLE_TAG% ready at %SRC%
echo  Next: scripts\apply-patches.bat then scripts\build.bat
echo ============================================================
exit /b 0
