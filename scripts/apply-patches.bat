@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  apply-patches.bat
REM  Apply patches\*.patch in numerical order onto C:\cr\src.
REM  Uses `git am` so each patch becomes a commit on chronium-base.
REM
REM  After this, C:\cr\src is the patched tree ready for build.bat.
REM ============================================================

set "CR_ROOT=C:\cr"
set "SRC=%CR_ROOT%\src"
set "PATCHES=%~dp0..\patches"

if not exist "%SRC%\.git" (
    echo [FAIL] %SRC% not a git checkout. Run scripts\fetch.bat first.
    exit /b 1
)

if not exist "%PATCHES%" (
    echo [FAIL] No patches dir at %PATCHES%
    exit /b 1
)

REM Count *.patch files (exclude README)
set "PATCH_COUNT=0"
for %%p in ("%PATCHES%\*.patch") do set /a PATCH_COUNT+=1
if !PATCH_COUNT! EQU 0 (
    echo [info] No *.patch files in %PATCHES%. Nothing to apply.
    exit /b 0
)

pushd "%SRC%"

REM Ensure we are on chronium-base; reset any prior patched state.
git rev-parse --verify chronium-base >nul 2>&1
if errorlevel 1 (
    popd
    echo [FAIL] Branch chronium-base missing. Run scripts\fetch.bat first.
    exit /b 1
)

call git checkout chronium-base
if errorlevel 1 (
    popd
    echo [FAIL] Cannot checkout chronium-base. Uncommitted changes?
    exit /b 1
)

REM Drop any previously applied patch commits by hard-resetting to the tag root.
REM (Cheap because gclient sync would also need to re-run if you switch tags.)
for /f "delims=" %%T in ('git describe --tags --abbrev^=0') do set "BASE_TAG=%%T"
if defined BASE_TAG (
    echo [info] Resetting chronium-base to %BASE_TAG% before re-applying patches.
    call git reset --hard "%BASE_TAG%"
)

echo.
echo === Applying !PATCH_COUNT! patch(es) from %PATCHES% ===
echo.

for %%p in ("%PATCHES%\*.patch") do (
    echo [apply] %%~nxp
    call git am --3way "%%p"
    if errorlevel 1 (
        echo.
        echo [FAIL] Patch %%~nxp failed to apply.
        echo        Conflicts left in working tree. Resolve with:
        echo          cd /d %SRC%
        echo          git status
        echo          ^<edit conflicted files^>
        echo          git add ^<files^>
        echo          git am --continue
        echo        Then re-export with: git format-patch chronium-base..HEAD -o %PATCHES%
        popd
        exit /b 1
    )
)

popd
echo.
echo ============================================================
echo  All !PATCH_COUNT! patches applied on top of chronium-base.
echo  Next: scripts\build.bat
echo ============================================================
exit /b 0
