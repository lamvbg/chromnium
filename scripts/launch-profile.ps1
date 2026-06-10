# Pick a fingerprint profile, auto-localize it to the exit IP's country,
# and launch Chronium with the result.
#
# Usage:
#   .\launch-profile.ps1 -Name mac-m1-air13
#   .\launch-profile.ps1 -Name win-rtx3060 -Url https://browserleaks.com/javascript
#   .\launch-profile.ps1 -Random           # surprise me
#   .\launch-profile.ps1 -Name mac-m1-air13 -Proxy socks5://u:p@host:port
#   .\launch-profile.ps1 -Name mac-m1-air13 -Region vn   # skip auto-detect
#
# The script invokes prepare-profile.py to:
#   1. Look up the exit IP's country (through -Proxy if set, otherwise
#      from the host's public IP). ip-api.com / ipinfo.io provide the
#      free geolocation lookup.
#   2. Pick the matching region bundle (timezone, locale, languages,
#      Accept-Language, geolocation).
#   3. Merge it into a copy of the source profile under %TEMP%\.
#
# The browser then sees a profile whose locale/timezone match the exit
# IP — Pixelscan's IP-vs-timezone cross-check stops firing.
#
# The underlying fingerprint (canvas / WebGL / audio / clientrects
# noise + UA + GPU + speech voices + etc.) stays pinned to the source
# profile name, so two different -Name values always produce two
# distinct fingerprints, while two launches of the SAME name produce
# the same fingerprint regardless of exit country.

param(
    [string]$Name = $null,
    [switch]$Random,
    [switch]$List,
    [string]$Url = 'https://tls.peet.ws/api/all',
    [string]$Proxy = $null,
    [string]$Region = $null,
    [switch]$NoLocalize
)

$ProfileDir   = Join-Path $PSScriptRoot '..\config\profiles' | Resolve-Path
$ScriptDir    = $PSScriptRoot
$BrowserExe   = 'C:\cr\src\out\Release\chrome.exe'
$UserDataRoot = 'C:\ChroniumProfiles'

if ($List) {
    Get-ChildItem -Path $ProfileDir -Filter *.json |
        ForEach-Object {
            $p = $_
            $j = Get-Content $p.FullName -Raw | ConvertFrom-Json
            [PSCustomObject]@{
                Name = $p.BaseName
                Notes = $j.notes
                UA   = ($j.user_agent -replace '^Mozilla/5\.0 \(', '' -replace '\)\ AppleWebKit.*$', '')
            }
        } | Format-Table -AutoSize
    return
}

if ($Random) {
    $picked = Get-ChildItem -Path $ProfileDir -Filter *.json | Get-Random
    $Name = $picked.BaseName
    Write-Host "[random] picked $Name"
}

if (-not $Name) {
    Write-Error "Provide -Name <profile> or -Random or -List"
    return
}

$SourceProfilePath = Join-Path $ProfileDir "$Name.json"
if (-not (Test-Path $SourceProfilePath)) {
    Write-Error "Profile not found: $SourceProfilePath"
    return
}

$UserDataDir = Join-Path $UserDataRoot $Name
if (-not (Test-Path $UserDataDir)) {
    New-Item -ItemType Directory -Path $UserDataDir -Force | Out-Null
}

# Resolve the fingerprint profile path. With -NoLocalize, skip the
# detect + localize step and pass the source JSON straight through.
if ($NoLocalize) {
    $ProfilePathToUse = $SourceProfilePath
    Write-Host "[geo] skipping localization (-NoLocalize)"
} else {
    $prepArgs = @('--name', $Name)
    if ($Proxy)  { $prepArgs += @('--proxy', $Proxy) }
    if ($Region) { $prepArgs += @('--region', $Region) }
    $prepScript = Join-Path $ScriptDir 'prepare-profile.py'

    $prepOutput = python $prepScript @prepArgs 2>&1
    $exit = $LASTEXITCODE
    # python's stderr lines start with "[geo]" etc. The temp profile
    # path is the LAST line on stdout. We grab whichever last line
    # actually looks like a path.
    $lines = @($prepOutput | ForEach-Object { $_.ToString() })
    $pathLine = $null
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match '\.json$') { $pathLine = $lines[$i]; break }
    }

    foreach ($l in $lines) {
        if ($l -ne $pathLine) { Write-Host $l }
    }

    if ($exit -ne 0 -or -not $pathLine -or -not (Test-Path $pathLine)) {
        Write-Warning "prepare-profile failed (exit=$exit); falling back to source profile uncustomized."
        $ProfilePathToUse = $SourceProfilePath
    } else {
        $ProfilePathToUse = $pathLine
        Write-Host "[geo] localized profile: $ProfilePathToUse"
    }
}

$Args = @(
    "--user-data-dir=$UserDataDir",
    "--fingerprint-profile=$ProfilePathToUse",
    '--no-first-run'
)
if ($Proxy) { $Args += "--proxy-server=$Proxy" }
$Args += $Url

Write-Host "Launching $Name -> $Url"
Start-Process -FilePath $BrowserExe -ArgumentList $Args
