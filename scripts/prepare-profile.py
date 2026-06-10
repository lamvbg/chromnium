"""
Pick a fingerprint profile and localize it on-the-fly to match the exit
IP's country. Prints the path of a temp JSON that the PowerShell
launcher can pass straight to --fingerprint-profile=<path>.

The fingerprint itself (canvas / WebGL / audio / clientrects noise +
all per-profile deterministic values) is owned by the profile file
under config/profiles/. The seed there is SHA256(profile_name), so the
fingerprint hashes for "mac-m1-air13" are identical on every launch,
forever. Two different profile names ALWAYS produce two distinct
fingerprints — the localization layer here never touches those bytes.

What IT does touch: timezone, ICU locale, navigator.languages,
Accept-Language, navigator.geolocation. Those have to be coherent with
the exit country or Pixelscan / Cloudflare / iphey flag the session.

Flow:
  1. Resolve the exit IP. With --proxy set, route the lookup through
     it (curl_cffi handles SOCKS5 + HTTP). Without a proxy, use the
     host's public IP directly.
  2. Map ISO country code -> chronium region tag (vn / us-east / uk /
     ...). Unknown countries fall back to en-US neutral.
  3. Deep-merge the region settings into the profile dict and dump to
     a temp file. PowerShell reads the temp path and launches chrome
     with it.

Usage:
  python prepare-profile.py --name mac-m1-air13
  python prepare-profile.py --name mac-m1-air13 --proxy socks5://u:p@h:port
  python prepare-profile.py --name mac-m1-air13 --region vn   # skip detect
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Optional

try:
    from curl_cffi import requests as curl_requests
except ImportError:
    curl_requests = None

PROFILES_DIR = (
    Path(__file__).resolve().parent.parent / "config" / "profiles"
)

# Country code -> chronium region tag. Anything not listed falls back to
# en-US neutral. The mapping is intentionally coarse: a single US-east
# settings bundle for all US states, single de for all of Germany etc.
# Real anti-detect tooling does per-city granularity but the marginal
# return for typical browsing flows is small.
COUNTRY_TO_REGION = {
    "VN": "vn",
    "US": "us-east",
    "CA": "us-east",  # close enough; could split if you ship a separate ca tag
    "GB": "uk",
    "UK": "uk",
    "DE": "de",
    "AT": "de",
    "CH": "de",
    "FR": "fr",
    "BE": "fr",
    "JP": "jp",
    "SG": "sg",
    "IN": "in",
    "BR": "br",
    "PL": "pl",
    "PT": "br",
    "ES": "fr",
    "IT": "fr",
    "KR": "jp",
    "TW": "sg",
    "HK": "sg",
    "TH": "vn",
    "ID": "sg",
    "MY": "sg",
    "PH": "sg",
}

# Per-region overrides. Each entry replaces the top-level keys with the
# same name on the source profile.
REGIONS: dict[str, dict[str, Any]] = {
    "vn": {
        "timezone": "Asia/Ho_Chi_Minh",
        "locale": "vi-VN",
        "accept_language": "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["vi-VN", "vi", "en-US", "en"],
        "geolocation": {"latitude": 10.8231, "longitude": 106.6297, "accuracy": 100},
    },
    "us-east": {
        "timezone": "America/New_York",
        "locale": "en-US",
        "accept_language": "en-US,en;q=0.9",
        "languages": ["en-US", "en"],
        "geolocation": {"latitude": 40.7128, "longitude": -74.006, "accuracy": 100},
    },
    "us-west": {
        "timezone": "America/Los_Angeles",
        "locale": "en-US",
        "accept_language": "en-US,en;q=0.9",
        "languages": ["en-US", "en"],
        "geolocation": {"latitude": 34.0522, "longitude": -118.2437, "accuracy": 100},
    },
    "uk": {
        "timezone": "Europe/London",
        "locale": "en-GB",
        "accept_language": "en-GB,en;q=0.9",
        "languages": ["en-GB", "en"],
        "geolocation": {"latitude": 51.5074, "longitude": -0.1278, "accuracy": 100},
    },
    "de": {
        "timezone": "Europe/Berlin",
        "locale": "de-DE",
        "accept_language": "de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["de-DE", "de", "en-US", "en"],
        "geolocation": {"latitude": 52.52, "longitude": 13.405, "accuracy": 100},
    },
    "fr": {
        "timezone": "Europe/Paris",
        "locale": "fr-FR",
        "accept_language": "fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["fr-FR", "fr", "en-US", "en"],
        "geolocation": {"latitude": 48.8566, "longitude": 2.3522, "accuracy": 100},
    },
    "jp": {
        "timezone": "Asia/Tokyo",
        "locale": "ja-JP",
        "accept_language": "ja-JP,ja;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["ja-JP", "ja", "en-US", "en"],
        "geolocation": {"latitude": 35.6895, "longitude": 139.6917, "accuracy": 100},
    },
    "sg": {
        "timezone": "Asia/Singapore",
        "locale": "en-SG",
        "accept_language": "en-SG,en;q=0.9,zh-CN;q=0.8,zh;q=0.7",
        "languages": ["en-SG", "en", "zh-CN", "zh"],
        "geolocation": {"latitude": 1.3521, "longitude": 103.8198, "accuracy": 100},
    },
    "in": {
        "timezone": "Asia/Kolkata",
        "locale": "en-IN",
        "accept_language": "en-IN,en;q=0.9,hi;q=0.8",
        "languages": ["en-IN", "en", "hi"],
        "geolocation": {"latitude": 28.6139, "longitude": 77.209, "accuracy": 100},
    },
    "br": {
        "timezone": "America/Sao_Paulo",
        "locale": "pt-BR",
        "accept_language": "pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["pt-BR", "pt", "en-US", "en"],
        "geolocation": {"latitude": -23.5505, "longitude": -46.6333, "accuracy": 100},
    },
    "pl": {
        "timezone": "Europe/Warsaw",
        "locale": "pl-PL",
        "accept_language": "pl-PL,pl;q=0.9,en-US;q=0.8,en;q=0.7",
        "languages": ["pl-PL", "pl", "en-US", "en"],
        "geolocation": {"latitude": 52.2297, "longitude": 21.0122, "accuracy": 100},
    },
    "neutral": {
        # Last-resort fallback. Used when IP geolocation is offline or
        # the country isn't mapped above.
        "timezone": "UTC",
        "locale": "en-US",
        "accept_language": "en-US,en;q=0.9",
        "languages": ["en-US", "en"],
        "geolocation": {"latitude": 0.0, "longitude": 0.0, "accuracy": 100000},
    },
}


def detect_country(proxy: Optional[str], timeout: float = 8.0) -> Optional[str]:
    """Returns a 2-letter ISO country code or None on failure."""
    if curl_requests is None:
        print(
            "[!] curl_cffi not installed; skipping IP geo detection. "
            "Run: pip install curl_cffi",
            file=sys.stderr,
        )
        return None
    kwargs: dict[str, Any] = {
        "timeout": timeout,
        "impersonate": "chrome131",
    }
    if proxy:
        kwargs["proxies"] = {"http": proxy, "https": proxy}
    try:
        # ip-api.com: free, no key, returns countryCode field.
        r = curl_requests.get(
            "http://ip-api.com/json/?fields=country,countryCode,query",
            **kwargs,
        )
        if r.status_code == 200:
            j = r.json()
            cc = j.get("countryCode")
            ip = j.get("query")
            country = j.get("country")
            if cc:
                print(
                    f"[geo] exit IP {ip} -> {country} ({cc})",
                    file=sys.stderr,
                )
                return cc
    except Exception as e:  # noqa: BLE001
        print(f"[geo] ip-api failed: {e}", file=sys.stderr)

    # Backup: ipinfo.io (also free, no key for low volume).
    try:
        r = curl_requests.get("https://ipinfo.io/json", **kwargs)
        if r.status_code == 200:
            j = r.json()
            cc = j.get("country")
            if cc:
                print(
                    f"[geo] ipinfo: {j.get('ip')} -> {cc}",
                    file=sys.stderr,
                )
                return cc
    except Exception as e:  # noqa: BLE001
        print(f"[geo] ipinfo failed: {e}", file=sys.stderr)

    return None


def country_to_region(country_code: Optional[str]) -> str:
    if not country_code:
        return "neutral"
    return COUNTRY_TO_REGION.get(country_code.upper(), "neutral")


def detect_host_display() -> Optional[dict[str, Any]]:
    """Returns {width, height, avail_width, avail_height, device_pixel_ratio,
    color_depth} for the host's PRIMARY monitor. None if detection fails.

    iphey + similar anti-detect checks reject profiles whose claimed
    devicePixelRatio doesn't match what the host's GDI actually renders
    at — they sample canvas pixels and compute the real DPR back. The
    same goes for screen.{width,height}: claiming 1536x864 when the
    host renders 1920x1080 fails consistency probes. Overriding the
    profile here matches ShardX's strategy of pinning hardware-detectable
    surfaces to the host's real values.
    """
    if sys.platform != "win32":
        return None
    try:
        # AppliedDPI in HKCU lives at 96 by default. 120 = 1.25x scaling,
        # 144 = 1.5x, 192 = 2x, etc. Reading registry is faster + more
        # reliable than spawning a .NET screen probe.
        import winreg
        dpi = 96
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop\WindowMetrics"
        ) as key:
            try:
                dpi, _ = winreg.QueryValueEx(key, "AppliedDPI")
            except FileNotFoundError:
                pass
        dpr = dpi / 96.0

        # Primary monitor physical resolution via wmic (cheap + already
        # bundled with Windows).
        wmic = subprocess.check_output(
            ["wmic", "path", "Win32_VideoController", "get",
             "CurrentHorizontalResolution,CurrentVerticalResolution",
             "/format:list"],
            stderr=subprocess.DEVNULL, text=True, timeout=8,
        )
        width = 1920
        height = 1080
        for line in wmic.splitlines():
            line = line.strip()
            if line.startswith("CurrentHorizontalResolution=") and len(line) > 28:
                try:
                    w = int(line.split("=", 1)[1])
                    if w > 0:
                        width = w
                except ValueError:
                    pass
            elif line.startswith("CurrentVerticalResolution=") and len(line) > 26:
                try:
                    h = int(line.split("=", 1)[1])
                    if h > 0:
                        height = h
                except ValueError:
                    pass

        # CSS pixels (what JS sees) = physical / DPR.
        css_w = int(width / dpr)
        css_h = int(height / dpr)

        return {
            "width": css_w,
            "height": css_h,
            "avail_width": css_w,
            # ~48 CSS pixel taskbar at bottom is the Windows default.
            "avail_height": css_h - 48,
            "device_pixel_ratio": dpr,
            "color_depth": 24,
            "pixel_depth": 24,
        }
    except Exception as e:  # noqa: BLE001
        print(f"[host] display detect failed: {e}", file=sys.stderr)
        return None


def localize(profile: dict[str, Any], region: str) -> dict[str, Any]:
    if region not in REGIONS:
        print(f"[!] Unknown region '{region}', using neutral", file=sys.stderr)
        region = "neutral"
    out = copy.deepcopy(profile)
    for key, value in REGIONS[region].items():
        out[key] = value
    out["_chronium_region"] = region  # informational; patches ignore it

    # Pin screen + DPR to host's real values. The profile's claimed
    # screen used to come from the original device the ShardX dataset
    # was harvested off of; when that doesn't match the host running
    # chronium, iphey/Pixelscan probe the actual GPU rendering DPI and
    # flag the mismatch as masking. Overriding here makes the profile's
    # canvas/WebGL surface ALIGNED with the host's rendering pipeline,
    # while every other vector (UA, brands, voices, fonts, ...) still
    # comes from the profile so two accounts on the same host still
    # produce distinct fingerprints.
    host_display = detect_host_display()
    if host_display:
        print(
            f"[host] display: {host_display['width']}x{host_display['height']}"
            f" @ DPR={host_display['device_pixel_ratio']}",
            file=sys.stderr,
        )
        out["screen"] = host_display
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True, help="Profile basename")
    parser.add_argument("--proxy", default=None, help="Optional proxy URL")
    parser.add_argument(
        "--region",
        default=None,
        help="Manual region override (skip IP detection)",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Output path; defaults to temp file",
    )
    parser.add_argument(
        "--list-regions",
        action="store_true",
        help="Print known regions and exit",
    )
    args = parser.parse_args()

    if args.list_regions:
        for r in REGIONS:
            data = REGIONS[r]
            print(f"  {r:<10s} tz={data['timezone']:<22s} lang={data['languages'][0]}")
        return 0

    src = PROFILES_DIR / f"{args.name}.json"
    if not src.exists():
        print(f"[!] Profile not found: {src}", file=sys.stderr)
        return 2
    with src.open("r", encoding="utf-8") as f:
        profile = json.load(f)

    if args.region:
        region = args.region
        print(f"[geo] manual region: {region}", file=sys.stderr)
    else:
        cc = detect_country(args.proxy)
        region = country_to_region(cc)
        print(f"[geo] resolved region: {region}", file=sys.stderr)

    out = localize(profile, region)

    # WebRTC policy: only force "proxy_only" when a proxy was actually
    # passed. Without one, leaving WebRTC unrestricted lets fingerprint
    # tests at pixelscan / iphey / browserleaks finish their probes —
    # otherwise the page hangs forever waiting for an ICE candidate.
    # The exit IP it surfaces is the same one HTTPS already leaks.
    if args.proxy:
        out["webrtc_policy"] = "proxy_only"
    else:
        # Drop any inherited value so the patch falls back to default
        # (no override → real WebRTC).
        out.pop("webrtc_policy", None)

    if args.out:
        out_path = Path(args.out)
    else:
        tmp = tempfile.NamedTemporaryFile(
            mode="w",
            prefix=f"chronium-{args.name}-{region}-",
            suffix=".json",
            delete=False,
            encoding="utf-8",
        )
        out_path = Path(tmp.name)
        tmp.close()
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    # Print ONLY the path on stdout so PowerShell can capture it.
    print(out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
