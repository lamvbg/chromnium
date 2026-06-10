# Package chronium for end-user distribution.
#
# Produces a self-contained folder + .zip containing:
#   - chronium.exe (renamed chrome.exe) + chrome.dll + paks + locales
#   - 170 device fingerprints
#   - PowerShell launcher + auto-localizer (no Python required)
#   - README.md with quick-start
#
# The output zip is ~300 MB. End users extract it anywhere and run
# scripts\chronium.ps1 directly — no installer, no admin, no Python.
#
# Run from chronium-build\scripts:
#   .\make-package.ps1                  # ./release/chronium-vX.X.X.zip
#   .\make-package.ps1 -Output D:\out   # custom output dir
#   .\make-package.ps1 -SkipZip         # leave the staging dir, skip zip

param(
    [string]$Output = $null,
    [string]$Version = 'v0.1.0',
    [switch]$SkipZip
)

$ScriptDir   = $PSScriptRoot
$BuildRoot   = Resolve-Path (Join-Path $ScriptDir '..')
$ReleaseSrc  = 'C:\cr\src\out\Release'
$ProfilesSrc = Join-Path $BuildRoot 'config\profiles'

if (-not $Output) { $Output = Join-Path $BuildRoot 'release' }
$Staging = Join-Path $Output "chronium-$Version"
$Zip     = "$Staging.zip"

# --- Validate sources ---
if (-not (Test-Path "$ReleaseSrc\chrome.exe")) {
    Write-Error "chronium build not found at $ReleaseSrc\chrome.exe. Run autoninja first."
    exit 2
}
if (-not (Test-Path $ProfilesSrc)) {
    Write-Error "Profiles not found at $ProfilesSrc. Run convert-shardx-profiles.py first."
    exit 2
}

# --- Clean staging ---
Write-Host "[1/6] Cleaning $Staging ..." -ForegroundColor Cyan
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Path $Staging -Force | Out-Null

# --- Copy chronium binary ---
Write-Host "[2/6] Copying chronium binary..." -ForegroundColor Cyan
$BinDir = Join-Path $Staging 'bin'
New-Item -ItemType Directory -Path $BinDir -Force | Out-Null

# Component build ships ~586 separate DLLs that chrome.exe loads at
# runtime, so the old "shortlist that real Chrome distributes" approach
# doesn't work — every component_*_*.dll needs to be present or the
# loader fails with WinError 14001 (side-by-side configuration is
# incorrect). Mirror the entire Release/ tree minus build-time
# artifacts (pdb / lib / obj / siso traces) instead.
#
# DO NOT rename chrome.exe → chronium.exe: Chrome's embedded manifest
# pins chrome.exe in several SxS assembly references; renaming triggers
# the SxS error on launch.
Write-Host "    mirroring Release/ -> bin/ (component build needs ~600 DLLs)"
robocopy $ReleaseSrc $BinDir /E `
    /XF "*.pdb" "*.lib" "*.o" "*.obj" "*.tlog" "*.ilk" `
        "siso_metrics*.json" "siso_trace*.json" "package.json" `
        "args.gn" "args.gn.d" "build.ninja" "build.ninja.d" `
        "toolchain.ninja" ".ninja_log" ".ninja_deps" `
    /XD "obj" "gen" `
    /NFL /NDL /NJH /NJS /NP /NS | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Error "robocopy failed with exit $LASTEXITCODE"
    exit $LASTEXITCODE
}
# robocopy returns 1-7 for successful copies, only >=8 is a real error.
$LASTEXITCODE = 0

$binSize = (Get-ChildItem $BinDir -Recurse | Measure-Object Length -Sum).Sum / 1MB
$fileCount = (Get-ChildItem $BinDir -Recurse -File).Count
Write-Host "    bin/ size: $([math]::Round($binSize,1)) MB ($fileCount files)"

# --- Copy fingerprint profiles ---
Write-Host "[3/6] Copying 170 fingerprint profiles..." -ForegroundColor Cyan
$ProfilesDst = Join-Path $Staging 'config\profiles'
New-Item -ItemType Directory -Path $ProfilesDst -Force | Out-Null
Copy-Item -Path "$ProfilesSrc\*.json" -Destination $ProfilesDst -Force
$profileCount = (Get-ChildItem $ProfilesDst -Filter *.json).Count
Write-Host "    $profileCount profiles copied"

# --- Copy scripts (PowerShell only — no Python required) ---
Write-Host "[4/6] Copying PowerShell scripts..." -ForegroundColor Cyan
$ScriptsDst = Join-Path $Staging 'scripts'
New-Item -ItemType Directory -Path $ScriptsDst -Force | Out-Null
foreach ($f in @('chronium.ps1', 'prepare-profile.ps1')) {
    $src = Join-Path $ScriptDir $f
    if (-not (Test-Path $src)) {
        Write-Warning "Missing $f at $src"
        continue
    }
    Copy-Item -Path $src -Destination $ScriptsDst -Force
}

# Patch chronium.ps1 paths so the script resolves chronium.exe relative
# to the package layout (bin/chronium.exe) instead of C:\cr\src\out.
$ChroniumPs = Join-Path $ScriptsDst 'chronium.ps1'
$content = Get-Content $ChroniumPs -Raw
$content = $content -replace "(?ms)\`$Chronium\s+=.*?if \(-not \(Test-Path \`$Chronium\)\) \{\s*\`$Chronium = 'C:\\cr\\src\\out\\Release\\chrome\.exe'\s*\}", @'
$Chronium = Join-Path (Resolve-Path (Join-Path $ScriptDir '..\bin')) 'chronium.exe'
'@
Set-Content -Path $ChroniumPs -Value $content -Encoding utf8

# --- Make launcher batch for double-click setup ---
Write-Host "[5/6] Writing launcher + README..." -ForegroundColor Cyan

$BatchLauncher = Join-Path $Staging 'chronium.bat'
@'
@echo off
REM Wrapper around scripts\chronium.ps1 so end users can drag-and-drop /
REM double-click without typing PowerShell syntax. Arguments are passed
REM straight through.
setlocal
set "SCRIPT=%~dp0scripts\chronium.ps1"
if "%1"=="" (
    powershell -ExecutionPolicy Bypass -File "%SCRIPT%" list
) else (
    powershell -ExecutionPolicy Bypass -File "%SCRIPT%" %*
)
'@ | Set-Content -Path $BatchLauncher -Encoding ascii

# Quick-start README.
$Readme = Join-Path $Staging 'README.md'
@"
# Chronium $Version — Anti-Detect Browser

Self-contained chronium binary + 170 device fingerprints + account
manager. No installer, no admin rights, no Python required.

## Quick start

Extract the zip anywhere (e.g. ``C:\Tools\chronium\``), then open
Command Prompt or PowerShell in the extracted folder.

``````
:: Create a new account (random unused fingerprint, opens grok.com)
chronium.bat new acc1 -Url https://grok.com

:: List all accounts and their assigned fingerprint
chronium.bat list

:: Reopen an existing account (same fingerprint, persistent cookies)
chronium.bat open acc1

:: Refresh fingerprint (gives the account a different device profile)
chronium.bat reassign acc1

:: Wipe an account (deletes user-data + registry entry)
chronium.bat delete acc1
``````

PowerShell users can call ``scripts\chronium.ps1`` directly with the
same arguments.

## What happens behind the scenes

Each account stores its browser data under
``C:\ChroniumProfiles\<accountName>\`` (cookies, history, login state).
The account-to-fingerprint mapping lives at
``C:\ChroniumProfiles\accounts.json`` and is read every launch — the
same account always loads the same fingerprint, so canvas / WebGL /
audio / Sec-CH-UA hashes stay paired with that identity forever.

The launcher auto-localizes each profile at startup:

* **Geo region** is derived from the exit IP (ip-api.com lookup).
  Timezone, locale, Accept-Language, and navigator.geolocation get
  swapped to match.
* **Screen + DPR** are pinned to the host's real values via the
  registry's AppliedDPI + wmic CurrentResolution. This is what kept
  iphey's pixel-DPI cross-check from failing the profile.

If you pass ``-Proxy socks5://user:pass@host:port`` the lookup uses
that proxy as well.

## File layout

``````
chronium-$Version/
  bin/                      # chronium.exe + chrome.dll + paks + locales
  config/
    profiles/               # 170 device fingerprints (.json)
  scripts/
    chronium.ps1            # account manager (new/open/list/delete/reassign)
    prepare-profile.ps1     # auto-localizer (geo + display)
  chronium.bat              # Windows wrapper for chronium.ps1
  README.md
``````

## Known limits

* iphey returns Trustworthy with the bundled win-* profiles on Windows
  hosts. Mac/Linux profiles run too but cross-platform detection trips
  the "Masking detected" check on stricter sites (Pixelscan, etc.).
* Pixelscan stays at "Collecting Data" — their JS crashes with a
  ``TypeError ... 'toString'`` against the bundled probe. ShardX,
  Multilogin and Kameleo all get the same stall. Not a chronium-side
  fix; it's their anti-detect signature list.
"@ | Set-Content -Path $Readme -Encoding utf8

# --- Zip ---
if (-not $SkipZip) {
    Write-Host "[6/6] Creating zip $Zip ..." -ForegroundColor Cyan
    if (Test-Path $Zip) { Remove-Item -Force $Zip }
    Compress-Archive -Path "$Staging\*" -DestinationPath $Zip -CompressionLevel Optimal
    $zipSize = (Get-Item $Zip).Length / 1MB
    Write-Host ""
    Write-Host "===" -ForegroundColor Green
    Write-Host "DONE" -ForegroundColor Green
    Write-Host "Staging: $Staging"
    Write-Host "Zip:     $Zip ($([math]::Round($zipSize,1)) MB)"
    Write-Host "===" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "===" -ForegroundColor Green
    Write-Host "Staging built (zip skipped): $Staging" -ForegroundColor Green
    Write-Host "===" -ForegroundColor Green
}
