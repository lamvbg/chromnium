"""One-shot: decrypt a single .json.enc to stdout or to a file.

Use this when launching chrome.exe by hand for a debug session — the
chronium binary expects a plaintext JSON path on --fingerprint-profile=,
so you need the full bytes of one decrypted profile sitting on disk.

By default the file you get is the bundled fingerprint as-is. Pass
``--auto-localize`` to ALSO apply the same overrides the antidetect
backend applies before launch (timezone / locale / accept-language /
geolocation pulled from the exit IP via ip-api.com, screen dimensions
pulled from the Windows registry + wmic) — this is what closes the
iphey LOCATION / BROWSER badges when launching standalone instead of
through the app.

Usage:
    # Decrypt as-is to stdout
    python scripts/decrypt-profile.py win-rtx2000

    # Decrypt + auto-localize, write to %TEMP%, print path
    python scripts/decrypt-profile.py win-rtx2000 --to-temp --auto-localize

    # With a proxy (so the geo lookup goes through it instead of host IP)
    python scripts/decrypt-profile.py win-rtx2000 --to-temp --auto-localize \\
        --proxy socks5://user:pass@host:port
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parent.parent
KEY_FILE = ROOT / "config" / ".profile_key"
PROFILES_DIR = ROOT / "config" / "profiles"
MAGIC = b"CRP1"


# Same region pool the antidetect backend uses
# (handlers_profile._REGION_POOL). Kept inline so this script is
# self-contained — release-zip users don't have the Python backend.
# Tuple: (tz, lat, lng, locale, nav.language, accept-language).
_REGION_POOL = [
    ("Asia/Ho_Chi_Minh",    10.7769, 106.7009, "vi-VN", "vi-VN", "vi-VN,vi;q=0.9,en;q=0.8"),
    ("Asia/Bangkok",        13.7563, 100.5018, "th-TH", "th-TH", "th-TH,th;q=0.9,en;q=0.8"),
    ("Asia/Singapore",       1.3521, 103.8198, "en-SG", "en-SG", "en-SG,en;q=0.9"),
    ("Asia/Tokyo",          35.6762, 139.6503, "ja-JP", "ja-JP", "ja-JP,ja;q=0.9,en;q=0.8"),
    ("Asia/Seoul",          37.5665, 126.9780, "ko-KR", "ko-KR", "ko-KR,ko;q=0.9,en;q=0.8"),
    ("Asia/Hong_Kong",      22.3193, 114.1694, "zh-HK", "zh-HK", "zh-HK,zh;q=0.9,en;q=0.8"),
    ("Asia/Shanghai",       31.2304, 121.4737, "zh-CN", "zh-CN", "zh-CN,zh;q=0.9,en;q=0.8"),
    ("Asia/Manila",         14.5995, 120.9842, "en-PH", "en-PH", "en-PH,en;q=0.9,fil;q=0.8"),
    ("Asia/Jakarta",        -6.2088, 106.8456, "id-ID", "id-ID", "id-ID,id;q=0.9,en;q=0.8"),
    ("Asia/Kolkata",        19.0760,  72.8777, "en-IN", "en-IN", "en-IN,en;q=0.9,hi;q=0.8"),
    ("Asia/Dubai",          25.2048,  55.2708, "ar-AE", "ar-AE", "ar-AE,ar;q=0.9,en;q=0.8"),
    ("Australia/Sydney",   -33.8688, 151.2093, "en-AU", "en-AU", "en-AU,en;q=0.9"),
    ("Europe/London",       51.5074,  -0.1278, "en-GB", "en-GB", "en-GB,en;q=0.9"),
    ("Europe/Paris",        48.8566,   2.3522, "fr-FR", "fr-FR", "fr-FR,fr;q=0.9,en;q=0.8"),
    ("Europe/Berlin",       52.5200,  13.4050, "de-DE", "de-DE", "de-DE,de;q=0.9,en;q=0.8"),
    ("Europe/Madrid",       40.4168,  -3.7038, "es-ES", "es-ES", "es-ES,es;q=0.9,en;q=0.8"),
    ("Europe/Rome",         41.9028,  12.4964, "it-IT", "it-IT", "it-IT,it;q=0.9,en;q=0.8"),
    ("America/New_York",    40.7128, -74.0060, "en-US", "en-US", "en-US,en;q=0.9"),
    ("America/Los_Angeles", 34.0522,-118.2437, "en-US", "en-US", "en-US,en;q=0.9"),
    ("America/Sao_Paulo",  -23.5505, -46.6333, "pt-BR", "pt-BR", "pt-BR,pt;q=0.9,en;q=0.8"),
]

# ISO country code -> _REGION_POOL index.
_COUNTRY_TO_REGION = {
    "VN":0,"KH":1,"LA":0,
    "TH":1,
    "SG":2,"MY":2,"BN":2,
    "JP":3,
    "KR":4,"KP":4,
    "HK":5,"MO":5,"TW":5,
    "CN":6,
    "PH":7,
    "ID":8,"TL":8,
    "IN":9,"BD":9,"NP":9,"LK":9,"PK":9,
    "AE":10,"SA":10,"QA":10,"KW":10,"OM":10,"BH":10,
    "AU":11,"NZ":11,
    "GB":12,"IE":12,
    "FR":13,"BE":13,"LU":13,"MC":13,
    "DE":14,"AT":14,"CH":14,"CZ":14,"PL":14,"NL":14,"DK":14,
    "ES":15,"PT":15,
    "IT":16,"GR":16,"MT":16,
    "US":17,"CA":17,
    "MX":18,
    "BR":19,"AR":19,"CL":19,"PE":19,"CO":19,"UY":19,
}


def detect_country(proxy_url: str | None) -> str | None:
    """Resolve exit country code via ip-api.com. Optional HTTP/HTTPS
    proxy; SOCKS5 falls back to direct (urllib has no SOCKS handler and
    we don't want to pull in a third-party dep just for this)."""
    import urllib.error
    import urllib.parse
    import urllib.request

    url = "http://ip-api.com/json/?fields=countryCode,query"
    try:
        if proxy_url:
            scheme = urllib.parse.urlparse(proxy_url).scheme.lower()
            if scheme in ("http", "https"):
                opener = urllib.request.build_opener(
                    urllib.request.ProxyHandler(
                        {"http": proxy_url, "https": proxy_url}
                    )
                )
                with opener.open(url, timeout=6) as r:  # noqa: S310
                    body = json.loads(r.read())
            else:
                with urllib.request.urlopen(url, timeout=6) as r:  # noqa: S310
                    body = json.loads(r.read())
        else:
            with urllib.request.urlopen(url, timeout=6) as r:  # noqa: S310
                body = json.loads(r.read())
        cc = body.get("countryCode")
        if cc:
            print(f"[geo] exit IP {body.get('query')} -> {cc}",
                  file=sys.stderr)
        return cc
    except (urllib.error.URLError, OSError, ValueError) as e:
        print(f"[geo] ip-api lookup failed: {e}", file=sys.stderr)
        return None


def detect_host_display() -> dict | None:
    """On Windows, read primary display resolution + DPR so the spoofed
    screen{} can match what the GPU actually renders. iphey's
    canvas-DPI cross-check flags 2560x1440 from the source profile when
    the real monitor is e.g. 1920x1080 — that's what just bit you.
    Returns None on non-Windows (callers keep the bundled screen)."""
    if os.name != "nt":
        return None
    try:
        import winreg

        dpi = 96
        try:
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop\WindowMetrics"
            ) as k:
                dpi = int(winreg.QueryValueEx(k, "AppliedDPI")[0])
        except (OSError, ValueError):
            pass
        dpr = round(dpi / 96.0, 2)

        width, height = 1920, 1080
        try:
            out = subprocess.check_output(
                [
                    "wmic", "path", "Win32_VideoController",
                    "get", "CurrentHorizontalResolution,CurrentVerticalResolution",
                    "/format:list",
                ],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=8,
            )
            for line in out.splitlines():
                line = line.strip()
                if line.startswith("CurrentHorizontalResolution="):
                    v = int(line.split("=", 1)[1] or 0)
                    if v > 0:
                        width = v
                elif line.startswith("CurrentVerticalResolution="):
                    v = int(line.split("=", 1)[1] or 0)
                    if v > 0:
                        height = v
        except (subprocess.SubprocessError, OSError, ValueError):
            pass

        css_w = int(width / dpr)
        css_h = int(height / dpr)
        print(f"[host] display: {css_w}x{css_h} @ DPR={dpr}", file=sys.stderr)
        return {
            "width": css_w,
            "height": css_h,
            "avail_width": css_w,
            "avail_height": css_h - 48,
            "device_pixel_ratio": dpr,
            "color_depth": 24,
            "pixel_depth": 24,
        }
    except Exception as e:  # noqa: BLE001
        print(f"[host] display detect failed: {e}", file=sys.stderr)
        return None


def apply_localize(profile: dict, proxy_url: str | None) -> dict:
    """Mutate profile dict in place: timezone / locale / accept_language /
    languages / geolocation from the exit IP, screen from the real host
    display, webrtc_policy based on proxy presence. Returns the same dict
    for chaining."""
    import hashlib

    cc = detect_country(proxy_url)
    if cc and cc in _COUNTRY_TO_REGION:
        idx = _COUNTRY_TO_REGION[cc]
    else:
        # Deterministic fallback so the same profile name keeps landing
        # in the same pool entry across runs when ip-api is down.
        seed_src = profile.get("name") or "fallback"
        idx = hashlib.sha256(seed_src.encode()).digest()[0] % len(_REGION_POOL)

    tz, base_lat, base_lng, locale, nav_lang, accept = _REGION_POOL[idx]

    # Jitter geo by ~0.5° (~55 km) so multiple profiles in the same
    # region don't sit on identical coords.
    seed = hashlib.sha256(
        (profile.get("name") or "fallback").encode()
    ).digest()
    lat = round(base_lat + (seed[1] / 255.0 - 0.5), 4)
    lng = round(base_lng + (seed[2] / 255.0 - 0.5), 4)

    profile["timezone"] = tz
    profile["locale"] = locale
    profile["accept_language"] = accept
    profile["languages"] = (
        [nav_lang, nav_lang.split("-")[0], "en-US", "en"]
        if "-" in nav_lang
        else [nav_lang, "en-US", "en"]
    )
    profile["geolocation"] = {"latitude": lat, "longitude": lng, "accuracy": 100}
    print(f"[geo] applied region: tz={tz} locale={locale} "
          f"geo=({lat},{lng})", file=sys.stderr)

    display = detect_host_display()
    if display:
        profile["screen"] = display

    if proxy_url:
        profile["webrtc_policy"] = "proxy_only"
    else:
        profile.pop("webrtc_policy", None)

    return profile


def load_key() -> bytes:
    if not KEY_FILE.exists():
        sys.exit(f"[!] {KEY_FILE} missing. Run gen-license-secret.py first.")
    return KEY_FILE.read_bytes()


def decrypt(packed: bytes, key: bytes) -> bytes:
    if len(packed) < 4 + 12 + 16 or packed[:4] != MAGIC:
        sys.exit("[!] bad blob: missing magic or too short")
    nonce = packed[4:16]
    tag = packed[16:32]
    ct = packed[32:]
    return AESGCM(key).decrypt(nonce, ct + tag, MAGIC)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("name", help="Profile id, e.g. win-rtx2000")
    ap.add_argument("-o", "--out", type=Path, help="Output file (default: stdout).")
    ap.add_argument(
        "--to-temp", action="store_true",
        help="Decrypt into %TEMP%; print only the resulting path (suitable "
             "for `$plain = python ... | Select-Object -Last 1`).",
    )
    ap.add_argument(
        "--auto-localize", action="store_true",
        help="Override timezone / locale / accept-language / geolocation "
             "from the exit IP (ip-api.com) and screen from the real "
             "Windows display. Required for iphey to go green when "
             "launching standalone; the antidetect app does this "
             "automatically in Cach 1.",
    )
    ap.add_argument(
        "--proxy", default=None,
        help="Optional proxy URL used for both the ip-api lookup (HTTP/HTTPS "
             "only) and to set webrtc_policy=proxy_only. Pass the same URL "
             "you'll hand to chrome --proxy-server=.",
    )
    args = ap.parse_args()

    src = PROFILES_DIR / f"{args.name}.json.enc"
    if not src.exists():
        sys.exit(f"[!] {src} not found")

    plain_bytes = decrypt(src.read_bytes(), load_key())

    if args.auto_localize:
        try:
            profile = json.loads(plain_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            sys.exit(f"[!] decrypted blob isn't JSON: {e}")
        apply_localize(profile, args.proxy)
        plain_bytes = json.dumps(profile, ensure_ascii=False, indent=2).encode("utf-8")

    if args.to_temp:
        fd, path = tempfile.mkstemp(prefix=f"chronium-{args.name}-", suffix=".json")
        with os.fdopen(fd, "wb") as f:
            f.write(plain_bytes)
        print(path)
    elif args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(plain_bytes)
        print(f"[ok] wrote {args.out} ({len(plain_bytes)} bytes)", file=sys.stderr)
    else:
        sys.stdout.buffer.write(plain_bytes)

    return 0


if __name__ == "__main__":
    sys.exit(main())
