# Chronium account manager — single command for end users.
#
# Each "account" is a Windows folder under C:\ChroniumProfiles\<name>\
# holding the browser's user data (cookies / history / login state) plus
# a chronium_meta.json that pins which of the 170 device fingerprints
# this account was given. The pin is permanent — re-running `open` for
# the same account always loads the same fingerprint, so cookies and
# canvas/WebGL/audio hashes stay paired with that identity forever.
# New accounts get a random fingerprint that isn't already in use by
# another account on this machine; once all 170 are taken the picker
# starts allowing duplicates (with a warning).
#
# Quick start (end-user copy/paste):
#   .\chronium.ps1 new acc1                    # create + open new account
#   .\chronium.ps1 new acc1 -Url https://grok.com   # open at a specific URL
#   .\chronium.ps1 open acc1                   # reopen existing account
#   .\chronium.ps1 list                        # show all accounts + fingerprint
#   .\chronium.ps1 delete acc1                 # wipe an account
#   .\chronium.ps1 new acc2 -Proxy socks5://u:p@host:port

param(
    [Parameter(Position=0, Mandatory=$true)]
    [ValidateSet('new', 'open', 'list', 'delete', 'reassign')]
    [string]$Command,

    [Parameter(Position=1)]
    [string]$Name,

    [string]$Url = 'https://www.google.com',
    [string]$Proxy = $null,
    [string]$Profile = $null,    # force a specific profile JSON by name
    [switch]$Force                # for delete/reassign without confirm
)

# Resolve paths relative to this script.
$ScriptDir   = $PSScriptRoot
$ProfilesDir = Resolve-Path (Join-Path $ScriptDir '..\config\profiles')
$Chronium    = Join-Path (Resolve-Path (Join-Path $ScriptDir '..\..')) 'cr\src\out\Release\chrome.exe'
if (-not (Test-Path $Chronium)) {
    $Chronium = 'C:\cr\src\out\Release\chrome.exe'
}
$AccountsRoot = 'C:\ChroniumProfiles'
$RegistryFile = Join-Path $AccountsRoot 'accounts.json'
$PrepareScript = Join-Path $ScriptDir 'prepare-profile.ps1'

if (-not (Test-Path $AccountsRoot)) {
    New-Item -ItemType Directory -Path $AccountsRoot -Force | Out-Null
}

function Load-Registry {
    # Windows PowerShell 5.1 has no -AsHashtable, so we deserialize to
    # PSCustomObject and walk the property bag back into a Hashtable
    # manually. Hashtable is what every caller below wants (ContainsKey,
    # Remove, indexer set).
    $registry = @{}
    if (Test-Path $RegistryFile) {
        $obj = Get-Content $RegistryFile -Raw | ConvertFrom-Json
        if ($obj) {
            foreach ($prop in $obj.PSObject.Properties) {
                $registry[$prop.Name] = [string]$prop.Value
            }
        }
    }
    return $registry
}

function Save-Registry($registry) {
    # Hashtable serializes fine on 5.1 as long as we let ConvertTo-Json
    # walk it directly.
    $registry | ConvertTo-Json -Depth 5 | Out-File -FilePath $RegistryFile -Encoding utf8
}

function Get-AvailableProfiles {
    Get-ChildItem -Path $ProfilesDir -Filter *.json | ForEach-Object { $_.BaseName }
}

function Pick-RandomProfile($registry, $hostOSPrefix = 'win') {
    # Prefer Windows-host-matching profiles, prefer ones not already
    # in use by another account on this machine, prefer ones whose
    # GPU vendor matches the host's so iphey/pixelscan host probes
    # don't flag a cross-vendor mismatch. The PowerShell side can't
    # know the host GPU as cheaply as prepare-profile.py can, so we
    # just filter by the OS prefix here and let the launcher's host-
    # display detection sort the screen mismatch out at launch time.
    $all = Get-AvailableProfiles | Where-Object { $_ -like "$hostOSPrefix-*" }
    if (-not $all) {
        # Fallback to any profile.
        $all = Get-AvailableProfiles
    }
    $used = $registry.Values | Sort-Object -Unique
    $unused = $all | Where-Object { $_ -notin $used }
    if ($unused) {
        return ($unused | Get-Random)
    }
    Write-Warning "All $($all.Count) profiles already assigned. Reusing one (fingerprints will collide across accounts)."
    return ($all | Get-Random)
}

function Get-AccountDir($name) {
    return (Join-Path $AccountsRoot $name)
}

function Get-AccountMeta($name) {
    $metaPath = Join-Path (Get-AccountDir $name) 'chronium_meta.json'
    if (Test-Path $metaPath) {
        return Get-Content $metaPath -Raw | ConvertFrom-Json
    }
    return $null
}

function Set-AccountMeta($name, $profileName) {
    $accDir = Get-AccountDir $name
    if (-not (Test-Path $accDir)) {
        New-Item -ItemType Directory -Path $accDir -Force | Out-Null
    }
    $meta = @{
        profile      = $profileName
        created      = (Get-Date).ToString('o')
        last_opened  = (Get-Date).ToString('o')
    }
    $metaPath = Join-Path $accDir 'chronium_meta.json'
    $meta | ConvertTo-Json -Depth 5 | Out-File -FilePath $metaPath -Encoding utf8
}

function Touch-AccountMeta($name) {
    $accDir = Get-AccountDir $name
    $metaPath = Join-Path $accDir 'chronium_meta.json'
    if (Test-Path $metaPath) {
        $meta = Get-Content $metaPath -Raw | ConvertFrom-Json
        $meta.last_opened = (Get-Date).ToString('o')
        $meta | ConvertTo-Json -Depth 5 | Out-File -FilePath $metaPath -Encoding utf8
    }
}

function Launch-Account($name, $profileName, $url, $proxy) {
    $accDir = Get-AccountDir $name
    $sourceProfile = Join-Path $ProfilesDir "$profileName.json"
    if (-not (Test-Path $sourceProfile)) {
        Write-Error "Profile not found: $sourceProfile"
        return
    }

    # Auto-localize (timezone/language/geo per exit IP, host-screen/DPR
    # match) via prepare-profile.ps1. The script writes a temp JSON and
    # prints the path on the last stdout line.
    $prepParams = @{ Name = $profileName }
    if ($proxy) { $prepParams.Proxy = $proxy }
    $rawOut = & $PrepareScript @prepParams 2>&1
    $tempProfile = $null
    $jsonSuffix = [char]46 + 'json'
    foreach ($l in @($rawOut)) {
        $s = $l.ToString()
        if ($s.EndsWith($jsonSuffix)) { $tempProfile = $s }
        else { Write-Host $s }
    }
    if (-not $tempProfile) {
        Write-Warning "prepare-profile.ps1 didn't return a path; using the source profile uncustomized."
        $tempProfile = $sourceProfile
    }

    $args = @(
        "--user-data-dir=$accDir",
        "--fingerprint-profile=$tempProfile",
        '--no-first-run'
    )
    if ($proxy) { $args += "--proxy-server=$proxy" }
    $args += $url

    Write-Host ""
    Write-Host "[$name] -> $profileName -> $url" -ForegroundColor Cyan
    Start-Process -FilePath $Chronium -ArgumentList $args
    Touch-AccountMeta $name
}

# === Command dispatch ===

switch ($Command) {

    'new' {
        if (-not $Name) { Write-Error "Usage: .\chronium.ps1 new <accountName>"; return }
        $registry = Load-Registry
        if ($registry.ContainsKey($Name)) {
            Write-Host "Account '$Name' already exists (fingerprint: $($registry[$Name])). Use 'open' to reopen, 'reassign' to give it a new fingerprint, 'delete' to wipe." -ForegroundColor Yellow
            return
        }
        $picked = if ($Profile) { $Profile } else { Pick-RandomProfile $registry }
        if (-not $picked) { Write-Error "Couldn't pick a profile."; return }
        $registry[$Name] = $picked
        Save-Registry $registry
        Set-AccountMeta $Name $picked
        Write-Host "[NEW] $Name -> fingerprint '$picked'" -ForegroundColor Green
        Launch-Account $Name $picked $Url $Proxy
    }

    'open' {
        if (-not $Name) { Write-Error "Usage: .\chronium.ps1 open <accountName>"; return }
        $registry = Load-Registry
        if (-not $registry.ContainsKey($Name)) {
            Write-Host "Account '$Name' doesn't exist yet. Auto-creating with a random fingerprint." -ForegroundColor Yellow
            $picked = Pick-RandomProfile $registry
            $registry[$Name] = $picked
            Save-Registry $registry
            Set-AccountMeta $Name $picked
        }
        $picked = $registry[$Name]
        Launch-Account $Name $picked $Url $Proxy
    }

    'list' {
        $registry = Load-Registry
        if ($registry.Count -eq 0) {
            Write-Host "No accounts yet. Create one with .\chronium.ps1 new <name>"
            return
        }
        $rows = $registry.GetEnumerator() | ForEach-Object {
            $meta = Get-AccountMeta $_.Key
            [PSCustomObject]@{
                Account     = $_.Key
                Fingerprint = $_.Value
                LastOpened  = if ($meta) { ([datetime]$meta.last_opened).ToString('yyyy-MM-dd HH:mm') } else { '?' }
                DataDir     = Get-AccountDir $_.Key
            }
        }
        $rows | Sort-Object LastOpened -Descending | Format-Table -AutoSize
    }

    'delete' {
        if (-not $Name) { Write-Error "Usage: .\chronium.ps1 delete <accountName>"; return }
        $registry = Load-Registry
        if (-not $registry.ContainsKey($Name)) {
            Write-Warning "Account '$Name' not in registry."
        }
        $accDir = Get-AccountDir $Name
        if ((Test-Path $accDir) -and -not $Force) {
            $confirm = Read-Host "Wipe '$Name' (data dir + registry entry)? [y/N]"
            if ($confirm -notin @('y','Y','yes')) { Write-Host "Cancelled."; return }
        }
        if (Test-Path $accDir) { Remove-Item -Recurse -Force $accDir }
        $registry.Remove($Name)
        Save-Registry $registry
        Write-Host "[DELETE] $Name wiped." -ForegroundColor Red
    }

    'reassign' {
        if (-not $Name) { Write-Error "Usage: .\chronium.ps1 reassign <accountName>"; return }
        $registry = Load-Registry
        if (-not $registry.ContainsKey($Name)) {
            Write-Error "Account '$Name' doesn't exist. Create with 'new'."
            return
        }
        $old = $registry[$Name]
        $picked = if ($Profile) { $Profile } else { Pick-RandomProfile $registry }
        if (-not $Force) {
            $confirm = Read-Host "Replace fingerprint '$old' with '$picked' for '$Name'? (cookies/login may be invalidated by sites) [y/N]"
            if ($confirm -notin @('y','Y','yes')) { Write-Host "Cancelled."; return }
        }
        $registry[$Name] = $picked
        Save-Registry $registry
        Set-AccountMeta $Name $picked
        Write-Host "[REASSIGN] $Name : $old -> $picked" -ForegroundColor Magenta
    }
}
