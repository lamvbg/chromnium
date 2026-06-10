# Localize a profile to the host: exit-IP country, real display
# resolution, and DPR. Writes a temp JSON ready for
# --fingerprint-profile=<path>. PowerShell-only — no Python needed.

param(
    [Parameter(Mandatory=$true)][string]$Name,
    [string]$Proxy = $null,
    [string]$Region = $null,
    [string]$Out = $null,
    [switch]$ListRegions
)

$ProfileDir = Resolve-Path (Join-Path $PSScriptRoot '..\config\profiles')

# Region overrides — exit-IP-coherent timezone + locale + languages +
# geo. The launcher detects the exit country and looks up the matching
# entry here. Without a matching entry it falls back to "neutral".
$Regions = @{
    'vn'      = @{ timezone='Asia/Ho_Chi_Minh'; locale='vi-VN'; accept_language='vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('vi-VN','vi','en-US','en'); geolocation=@{latitude=10.8231;longitude=106.6297;accuracy=100} }
    'us-east' = @{ timezone='America/New_York'; locale='en-US'; accept_language='en-US,en;q=0.9'; languages=@('en-US','en'); geolocation=@{latitude=40.7128;longitude=-74.006;accuracy=100} }
    'us-west' = @{ timezone='America/Los_Angeles'; locale='en-US'; accept_language='en-US,en;q=0.9'; languages=@('en-US','en'); geolocation=@{latitude=34.0522;longitude=-118.2437;accuracy=100} }
    'uk'      = @{ timezone='Europe/London'; locale='en-GB'; accept_language='en-GB,en;q=0.9'; languages=@('en-GB','en'); geolocation=@{latitude=51.5074;longitude=-0.1278;accuracy=100} }
    'de'      = @{ timezone='Europe/Berlin'; locale='de-DE'; accept_language='de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('de-DE','de','en-US','en'); geolocation=@{latitude=52.52;longitude=13.405;accuracy=100} }
    'fr'      = @{ timezone='Europe/Paris'; locale='fr-FR'; accept_language='fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('fr-FR','fr','en-US','en'); geolocation=@{latitude=48.8566;longitude=2.3522;accuracy=100} }
    'jp'      = @{ timezone='Asia/Tokyo'; locale='ja-JP'; accept_language='ja-JP,ja;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('ja-JP','ja','en-US','en'); geolocation=@{latitude=35.6895;longitude=139.6917;accuracy=100} }
    'sg'      = @{ timezone='Asia/Singapore'; locale='en-SG'; accept_language='en-SG,en;q=0.9,zh-CN;q=0.8,zh;q=0.7'; languages=@('en-SG','en','zh-CN','zh'); geolocation=@{latitude=1.3521;longitude=103.8198;accuracy=100} }
    'in'      = @{ timezone='Asia/Kolkata'; locale='en-IN'; accept_language='en-IN,en;q=0.9,hi;q=0.8'; languages=@('en-IN','en','hi'); geolocation=@{latitude=28.6139;longitude=77.209;accuracy=100} }
    'br'      = @{ timezone='America/Sao_Paulo'; locale='pt-BR'; accept_language='pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('pt-BR','pt','en-US','en'); geolocation=@{latitude=-23.5505;longitude=-46.6333;accuracy=100} }
    'pl'      = @{ timezone='Europe/Warsaw'; locale='pl-PL'; accept_language='pl-PL,pl;q=0.9,en-US;q=0.8,en;q=0.7'; languages=@('pl-PL','pl','en-US','en'); geolocation=@{latitude=52.2297;longitude=21.0122;accuracy=100} }
    'neutral' = @{ timezone='UTC'; locale='en-US'; accept_language='en-US,en;q=0.9'; languages=@('en-US','en'); geolocation=@{latitude=0;longitude=0;accuracy=100000} }
}

# ISO country code -> chronium region tag. Anything not listed falls
# back to "neutral".
$CountryToRegion = @{
    'VN'='vn'; 'US'='us-east'; 'CA'='us-east'; 'GB'='uk'; 'UK'='uk'
    'DE'='de'; 'AT'='de'; 'CH'='de'
    'FR'='fr'; 'BE'='fr'; 'IT'='fr'; 'ES'='fr'
    'JP'='jp'; 'KR'='jp'
    'SG'='sg'; 'TW'='sg'; 'HK'='sg'; 'ID'='sg'; 'MY'='sg'; 'PH'='sg'
    'IN'='in'
    'BR'='br'; 'PT'='br'
    'PL'='pl'
    'TH'='vn'
}

if ($ListRegions) {
    $Regions.GetEnumerator() | Sort-Object Name | ForEach-Object {
        $v = $_.Value
        "{0,-10} tz={1,-22} lang={2}" -f $_.Name, $v.timezone, ($v.languages[0])
    }
    return
}

function Detect-Country([string]$proxy) {
    # ip-api.com: free, no key, 45/min from a single IP. We use it as
    # the primary signal because the launcher only needs the country
    # code, not anything user-identifying.
    try {
        $url = 'http://ip-api.com/json/?fields=country,countryCode,query'
        if ($proxy) {
            # PowerShell's WebClient supports the WebProxy class. Only
            # HTTP/HTTPS upstream proxies work this way; SOCKS5 would
            # need a different transport (we recommend chaining via the
            # sidecar instead).
            $wc = New-Object System.Net.WebClient
            $wp = New-Object System.Net.WebProxy($proxy)
            $wc.Proxy = $wp
            $body = $wc.DownloadString($url)
        } else {
            $body = Invoke-RestMethod -Uri $url -TimeoutSec 8
            $body = $body | ConvertTo-Json   # normalise to text path below
        }
        $obj = $body | ConvertFrom-Json
        if ($obj.countryCode) {
            Write-Host "[geo] exit IP $($obj.query) -> $($obj.country) ($($obj.countryCode))" -ForegroundColor DarkGray
            return $obj.countryCode
        }
    } catch {
        Write-Host "[geo] ip-api failed: $_" -ForegroundColor DarkYellow
    }
    return $null
}

function Detect-HostDisplay {
    # Read Windows scaling from the registry (AppliedDPI in
    # HKCU\Control Panel\Desktop\WindowMetrics). 96 = 1.0x, 120 = 1.25x,
    # 144 = 1.5x, 192 = 2.0x. Then read the primary monitor's physical
    # resolution from wmic. The browser's window.screen reports CSS
    # pixels, which is physical / DPR.
    try {
        $dpi = 96
        try {
            $val = Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name 'AppliedDPI' -ErrorAction Stop
            if ($val.AppliedDPI) { $dpi = [int]$val.AppliedDPI }
        } catch { }
        $dpr = [Math]::Round($dpi / 96.0, 2)

        $width = 1920; $height = 1080
        try {
            $wmic = & wmic path Win32_VideoController get CurrentHorizontalResolution,CurrentVerticalResolution /format:list 2>$null
            foreach ($line in $wmic) {
                if ($line -match '^CurrentHorizontalResolution=(\d+)') { $w = [int]$matches[1]; if ($w -gt 0) { $width = $w } }
                if ($line -match '^CurrentVerticalResolution=(\d+)') { $h = [int]$matches[1]; if ($h -gt 0) { $height = $h } }
            }
        } catch { }

        $cssW = [int]($width / $dpr)
        $cssH = [int]($height / $dpr)

        Write-Host "[host] display: ${cssW}x${cssH} @ DPR=$dpr" -ForegroundColor DarkGray
        return @{
            width = $cssW
            height = $cssH
            avail_width = $cssW
            avail_height = ($cssH - 48)     # ~taskbar
            device_pixel_ratio = $dpr
            color_depth = 24
            pixel_depth = 24
        }
    } catch {
        Write-Host "[host] display detect failed: $_" -ForegroundColor DarkYellow
        return $null
    }
}

# --- Main ---

$src = Join-Path $ProfileDir "$Name.json"
if (-not (Test-Path $src)) {
    Write-Error "Profile not found: $src"
    exit 2
}

# JSON -> ordered hashtable is what we need to mutate.
$profile = Get-Content $src -Raw | ConvertFrom-Json

# Convert PSCustomObject to a hashtable so we can add/replace top-level
# keys. Two levels deep is enough for the keys we touch (screen,
# geolocation, languages).
function To-Hash($obj) {
    if ($null -eq $obj) { return $null }
    if ($obj -is [System.Collections.IEnumerable] -and -not ($obj -is [string])) {
        return @($obj | ForEach-Object { To-Hash $_ })
    }
    if ($obj -is [PSCustomObject]) {
        $h = [ordered]@{}
        foreach ($p in $obj.PSObject.Properties) {
            $h[$p.Name] = To-Hash $p.Value
        }
        return $h
    }
    return $obj
}
$profile = To-Hash $profile

# Pick region.
if ($Region) {
    $regionTag = $Region
    Write-Host "[geo] manual region: $regionTag" -ForegroundColor DarkGray
} else {
    $cc = Detect-Country $Proxy
    $regionTag = if ($cc -and $CountryToRegion.ContainsKey($cc)) { $CountryToRegion[$cc] } else { 'neutral' }
    Write-Host "[geo] resolved region: $regionTag" -ForegroundColor DarkGray
}
if (-not $Regions.ContainsKey($regionTag)) { $regionTag = 'neutral' }

foreach ($k in $Regions[$regionTag].Keys) {
    $profile[$k] = $Regions[$regionTag][$k]
}
$profile['_chronium_region'] = $regionTag

# Replace screen with host's real values so canvas/WebGL DPI probes
# don't fail consistency checks.
$display = Detect-HostDisplay
if ($display) {
    $profile['screen'] = $display
}

# WebRTC: only force proxy_only when a proxy is set; otherwise let real
# WebRTC negotiate so fingerprint probes finish.
if ($Proxy) {
    $profile['webrtc_policy'] = 'proxy_only'
} else {
    if ($profile.Contains('webrtc_policy')) { $profile.Remove('webrtc_policy') }
}

# Write temp file.
if (-not $Out) {
    $Out = Join-Path $env:TEMP ("chronium-" + $Name + "-" + $regionTag + "-" + [Guid]::NewGuid().ToString('N').Substring(0,8) + ".json")
}
$profile | ConvertTo-Json -Depth 10 -Compress:$false | Out-File -FilePath $Out -Encoding utf8
$Out
