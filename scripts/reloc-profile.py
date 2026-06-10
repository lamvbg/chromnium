"""
Swap a profile's timezone/locale/languages to match a given exit
country, leaving every other field (UA, brands, WebGL, etc.) untouched.

ShardX-derived profiles ship with the locale that matched the device's
original owner — `mac-m1-air13.json` is Polish, `linux-rtx3060.json` is
Bulgarian, and so on. That's the right starting point if you also exit
through a Polish/Bulgarian proxy; if you don't, Pixelscan's IP-geo vs
timezone cross-check fires and flags the session as masking.

This script writes a "<name>_<region>.json" sibling next to the source
so the original stays available for the matching proxy region.

Usage:
    python reloc-profile.py mac-m1-air13 vn
    python reloc-profile.py win-rtx3060 us-east
    python reloc-profile.py --list-regions
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any

PROFILES_DIR = (
    Path(__file__).resolve().parent.parent / "config" / "profiles"
)

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
}


def reloc(profile: dict[str, Any], region: str) -> dict[str, Any]:
    if region not in REGIONS:
        raise KeyError(region)
    out = copy.deepcopy(profile)
    region_data = REGIONS[region]
    for key, value in region_data.items():
        out[key] = value
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("name", nargs="?")
    parser.add_argument("region", nargs="?")
    parser.add_argument("--list-regions", action="store_true")
    parser.add_argument("--in-place", action="store_true",
                        help="overwrite the source instead of writing a sibling")
    args = parser.parse_args()

    if args.list_regions:
        for r, data in REGIONS.items():
            print(f"  {r:<8s} tz={data['timezone']:<22s} lang={data['languages'][0]}")
        return 0

    if not args.name or not args.region:
        parser.error("name and region are required (use --list-regions to see options)")

    src = PROFILES_DIR / f"{args.name}.json"
    if not src.exists():
        print(f"[!] Profile not found: {src}", file=sys.stderr)
        return 2

    if args.region not in REGIONS:
        print(f"[!] Unknown region '{args.region}'. Available:", file=sys.stderr)
        for r in REGIONS:
            print(f"      {r}", file=sys.stderr)
        return 2

    with src.open("r", encoding="utf-8") as f:
        profile = json.load(f)

    out = reloc(profile, args.region)

    if args.in_place:
        dst = src
    else:
        dst = PROFILES_DIR / f"{args.name}_{args.region}.json"

    with dst.open("w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print(f"[OK] {dst.name}: timezone={out['timezone']}, languages={out['languages']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
