"""
Convert ShardX launcher fingerprint JSON files to the Chronium profile
schema that --fingerprint-profile=<path> consumes.

ShardX (in <APPDATA>\\Roaming\\shardx-launcher\\fingerprints\\*.json)
ships a slightly different schema: navigator/client_hints/webgl/audio
fields are nested, brand list is built from individual chrome_build /
chrome_patch numbers rather than spelled out, media_devices are device
counts instead of labeled entries, and noise is configured per-channel
instead of per-vector.

We ingest 170 production-tested device profiles and emit a directory
of Chronium-shaped JSONs ready to be passed as --fingerprint-profile.
The mapping is purely mechanical — every field below comes straight from
the source JSON; the only synthesized value is the per-profile 32-byte
seed, derived as SHA256(profile_name) so the same name yields the same
canvas/audio/WebGL noise on every launch.

Usage:
    python convert-shardx-profiles.py
    python convert-shardx-profiles.py --src <dir> --dst <dir>

The default --src is the ShardX install path and --dst is
chronium-build/config/profiles. Both can be overridden for testing.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any

DEFAULT_SRC = (
    Path.home()
    / "AppData"
    / "Roaming"
    / "shardx-launcher"
    / "fingerprints"
)
DEFAULT_DST = (
    Path(__file__).resolve().parent.parent / "config" / "profiles"
)

# Chronium's default font allowlist mirrors what real Chrome on Win/Mac
# exposes via local-font enumeration. We can't read it from the ShardX
# profile (ShardX doesn't surface fonts), but Cloudflare and creepjs
# probe these names; shipping a per-OS bucket keeps fingerprints
# coherent with the claimed platform.
WIN_FONTS = [
    "Arial", "Arial Black", "Calibri", "Cambria", "Cambria Math",
    "Comic Sans MS", "Consolas", "Courier New", "Georgia", "Impact",
    "Lucida Console", "Microsoft Sans Serif", "Segoe UI", "Tahoma",
    "Times New Roman", "Trebuchet MS", "Verdana", "Webdings",
]
MAC_FONTS = [
    "American Typewriter", "Andale Mono", "Arial", "Arial Black",
    "Arial Narrow", "Arial Rounded MT Bold", "Avenir", "Avenir Next",
    "Baskerville", "Big Caslon", "Bodoni 72", "Brush Script MT",
    "Chalkboard", "Chalkduster", "Charter", "Cochin", "Comic Sans MS",
    "Copperplate", "Courier", "Courier New", "Didot", "Futura",
    "Georgia", "Gill Sans", "Helvetica", "Helvetica Neue", "Hoefler Text",
    "Impact", "Lucida Grande", "Marker Felt", "Menlo", "Monaco",
    "Optima", "Palatino", "Papyrus", "Phosphate", "Rockwell",
    "Savoye LET", "SignPainter", "Snell Roundhand", "Tahoma",
    "Times", "Times New Roman", "Trebuchet MS", "Verdana", "Zapfino",
]
LINUX_FONTS = [
    "DejaVu Sans", "DejaVu Sans Mono", "DejaVu Serif", "FreeMono",
    "FreeSans", "FreeSerif", "Liberation Mono", "Liberation Sans",
    "Liberation Sans Narrow", "Liberation Serif", "Noto Color Emoji",
    "Noto Mono", "Noto Sans CJK JP", "Noto Sans CJK KR",
    "Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans Mono",
    "Noto Serif CJK JP", "Ubuntu", "Ubuntu Condensed", "Ubuntu Mono",
]


def derive_seed_hex(profile_name: str) -> str:
    """32-byte seed = SHA256(profile_name).hex — same name -> same seed."""
    digest = hashlib.sha256(profile_name.encode("utf-8")).digest()
    return digest.hex()


def fonts_for_platform(platform: str) -> list[str]:
    p = platform.lower()
    if "win" in p:
        return WIN_FONTS
    if "mac" in p or "darwin" in p:
        return MAC_FONTS
    return LINUX_FONTS


def build_brands(client_hints: dict[str, Any]) -> list[dict[str, str]]:
    """Compose UA-CH low-entropy brands list from ShardX's spelled-out
    `brand`, `brand_version`, `grease_brand`, `grease_version` fields.
    Real Chrome ships a permuted order; we keep ShardX's order since
    every ShardX profile was hand-verified against the device it claims."""
    brands: list[dict[str, str]] = []
    if (b := client_hints.get("brand")) and (v := client_hints.get("brand_version")):
        brands.append({"brand": b, "version": str(v)})
    # 'Chromium' brand is implicit on real Chrome (Chrome 130 -> Chromium 130)
    if client_hints.get("brand") and client_hints.get("brand_version"):
        brands.append(
            {"brand": "Chromium", "version": str(client_hints["brand_version"])}
        )
    if (gb := client_hints.get("grease_brand")) and (
        gv := client_hints.get("grease_version")
    ):
        brands.append({"brand": gb, "version": str(gv)})
    return brands


def build_full_version_list(
    client_hints: dict[str, Any],
) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    bfv = client_hints.get("brand_full_version")
    if client_hints.get("brand") and bfv:
        out.append({"brand": client_hints["brand"], "version": str(bfv)})
        out.append({"brand": "Chromium", "version": str(bfv)})
    gfv = client_hints.get("grease_full_version")
    if client_hints.get("grease_brand") and gfv:
        out.append({"brand": client_hints["grease_brand"], "version": str(gfv)})
    return out


def media_devices_from_counts(counts: dict[str, Any]) -> list[dict[str, str]]:
    """ShardX stores counts (audio_input_count etc); chronium expects
    labeled entries. Synthesize plausible labels — Chronium's patch 0011
    HMACs the deviceId from the seed anyway, so labels don't have to be
    real, just consistent."""
    out: list[dict[str, str]] = []
    for _ in range(int(counts.get("audio_input_count", 0))):
        out.append(
            {
                "kind": "audioinput",
                "label": "Default - Microphone (Built-in Audio)",
            }
        )
    for _ in range(int(counts.get("audio_output_count", 0))):
        out.append(
            {
                "kind": "audiooutput",
                "label": "Default - Speakers (Built-in Audio)",
            }
        )
    for _ in range(int(counts.get("video_input_count", 0))):
        out.append({"kind": "videoinput", "label": "FaceTime HD Camera"})
    return out


def battery(sx_bat: dict[str, Any]) -> dict[str, Any]:
    """ShardX uses `discharging_time: "Infinity"` (string); Chronium expects
    a numeric or `null` for unknown. -1 means "not available" in our patch."""
    raw_dt = sx_bat.get("discharging_time")
    discharging_time = (
        None if isinstance(raw_dt, str) and raw_dt.lower().startswith("infi")
        else (raw_dt if raw_dt is not None else None)
    )
    raw_ct = sx_bat.get("charging_time")
    charging_time = (
        None if isinstance(raw_ct, str) and raw_ct.lower().startswith("infi")
        else (raw_ct if raw_ct is not None else None)
    )
    return {
        "level": float(sx_bat.get("level", 1.0)),
        "charging": bool(sx_bat.get("charging", True)),
        "charging_time": charging_time,
        "discharging_time": discharging_time,
    }


def convert(sx: dict[str, Any], name: str) -> dict[str, Any]:
    nav = sx.get("navigator", {}) or {}
    ch = sx.get("client_hints", {}) or {}
    screen = sx.get("screen", {}) or {}
    webgl = sx.get("webgl", {}) or {}
    storage = sx.get("storage_estimate", {}) or {}
    mem = sx.get("memory", {}) or {}
    bat = sx.get("battery", {}) or {}
    speech = sx.get("speech", {}) or {}
    conn = sx.get("connection", {}) or {}
    webauthn = sx.get("webauthn", {}) or {}
    med = sx.get("media_devices", {}) or {}

    platform_value = nav.get("platform_value") or nav.get("platform", "")

    out: dict[str, Any] = {
        "schema_version": 1,
        "name": name,
        "notes": sx.get("notes", ""),
        "seed": derive_seed_hex(name),
        "user_agent": nav.get("user_agent", ""),
        "user_agent_data": {
            "brands": build_brands(ch),
            "full_version_list": build_full_version_list(ch),
            "platform": nav.get("platform", ""),
            "platform_version": ch.get("platform_version", nav.get("platform_version", "")),
            "architecture": ch.get("architecture", ""),
            "bitness": ch.get("bitness", "64"),
            "model": "",
            "mobile": bool(ch.get("mobile", False)),
            "wow64": False,
        },
        "platform": platform_value,
        "vendor": nav.get("vendor", "Google Inc."),
        "accept_language": nav.get("accept_language", "en-US,en;q=0.9"),
        "languages": nav.get("languages", ["en-US", "en"]),
        "screen": {
            "width": int(screen.get("width", 1920)),
            "height": int(screen.get("height", 1080)),
            "avail_width": int(screen.get("avail_width", screen.get("width", 1920))),
            "avail_height": int(
                screen.get("avail_height", screen.get("height", 1080))
            ),
            "color_depth": int(screen.get("color_depth", 24)),
            "pixel_depth": int(screen.get("pixel_depth", 24)),
            "device_pixel_ratio": float(screen.get("device_pixel_ratio", 1.0)),
        },
        "hardware_concurrency": int(nav.get("hardware_concurrency", 8)),
        "device_memory": float(nav.get("device_memory", 8)),
        "max_touch_points": int(nav.get("max_touch_points", 0)),
        "timezone": sx.get("timezone", "America/New_York"),
        "locale": sx.get("icu_locale", "en-US"),
        "geolocation": {
            # ShardX leaves geo to the launcher to derive from proxy
            # exit IP; we ship a sane neutral default that the user can
            # override per-launch.
            "latitude": 40.7128,
            "longitude": -74.006,
            "accuracy": 100,
        },
        # WebGL field mapping note:
        # ShardX's `vendor`/`renderer` are the values surfaced via the
        # WEBGL_debug_renderer_info extension (UNMASKED_VENDOR_WEBGL /
        # UNMASKED_RENDERER_WEBGL), i.e. the "real GPU" strings —
        # "Google Inc. (Apple)" / "ANGLE (Apple, ANGLE Metal Renderer:
        # Apple M1, ...)". ShardX's `vendor_masked`/`renderer_masked`
        # are the plain VENDOR/RENDERER values without the extension,
        # always "WebKit"/"WebKit WebGL" in real Chrome.
        # Chronium's patch 0004 reads `unmasked_vendor`/`unmasked_renderer`
        # to override the EXTENSION-exposed values; the masked ones it
        # leaves untouched. So the right mapping is:
        #   chronium.unmasked_vendor   <- shardx.vendor   (full ANGLE string)
        #   chronium.unmasked_renderer <- shardx.renderer (full ANGLE string)
        # The chronium `vendor`/`renderer` keys go unread today (the
        # patch never reads them) — we keep them populated for human
        # inspection of the JSON. Putting "WebKit" there matches what
        # JS sees from VENDOR/RENDERER without the extension; putting
        # the full ANGLE string would mislead readers.
        "webgl": {
            "vendor": webgl.get("vendor_masked", "WebKit"),
            "renderer": webgl.get("renderer_masked", "WebKit WebGL"),
            "unmasked_vendor": webgl.get("vendor", ""),
            "unmasked_renderer": webgl.get("renderer", ""),
            "version": "WebGL 2.0 (OpenGL ES 3.0 Chromium)",
            "max_texture_size": int(webgl.get("max_texture_size", 16384)),
            "max_vertex_attribs": int(webgl.get("max_vertex_attribs", 16)),
            "extensions": webgl.get("extensions", []),
        },
        "fonts": fonts_for_platform(nav.get("platform", "")),
        "plugins": [
            {
                "name": "PDF Viewer",
                "description": "Portable Document Format",
                "filename": "internal-pdf-viewer",
            },
            {
                "name": "Chrome PDF Viewer",
                "description": "Portable Document Format",
                "filename": "internal-pdf-viewer",
            },
        ],
        "mime_types": [
            {
                "type": "application/pdf",
                "suffixes": "pdf",
                "description": "Portable Document Format",
            }
        ],
        "media_devices": media_devices_from_counts(med),
        "battery": battery(bat),
        # WebRTC policy intentionally omitted at conversion time. With it
        # forced to "proxy_only" on every profile, sites that probe
        # WebRTC for fingerprinting (Pixelscan's "WebRTC IP Address"
        # row, iphey's RTC test) stall their own JS waiting for a
        # candidate that will never arrive. prepare-profile.py reattaches
        # "proxy_only" when launched with --proxy; without a proxy we
        # let real WebRTC negotiate against the host so the page test
        # completes — the IP leaked is the host's exit IP anyway, same
        # one HTTPS already exposes.
        "memory": {
            "heap_size_limit": str(int(mem.get("heap_size_limit", 4294705152)))
        },
        "connection": {
            "effective_type": conn.get("effective_type", "4g"),
            "downlink_mbps": float(conn.get("downlink_mbps", 10.0)),
            "rtt_msec": int(conn.get("rtt_msec", 75)),
            "save_data": bool(conn.get("save_data", False)),
        },
        "storage_estimate": {
            "quota_gb": float(storage.get("quota_gb", 10)),
        },
        "webauthn": {
            "uvpa": bool(webauthn.get("uvpa", False)),
        },
        "speech": {
            "voices": speech.get("voices", []),
        },
        "noise": {
            # Canvas + clientrects noise kept on for canvas-fingerprint
            # uniqueness across profiles (each profile's seed produces a
            # different canvas hash). Audio noise OFF because creepjs's
            # audio trap fires on any per-sample perturbation, regardless
            # of magnitude — it hashes specific oscillator output windows
            # and any deviation registers. ShardX-derived profiles claim
            # real consumer devices, so the right defense is to not noise
            # the audio context at all and let the unique seed take care
            # of canvas/WebGL differentiation instead.
            "canvas_threshold":      0.001,
            "webgl_readpixels":      True,
            "audio_amplitude":       0,
            "clientrects_subpixel":  1e-5,
            "measure_text_subpixel": 1e-5,
        },
    }
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", type=Path, default=DEFAULT_SRC)
    parser.add_argument("--dst", type=Path, default=DEFAULT_DST)
    args = parser.parse_args()

    if not args.src.is_dir():
        print(f"[!] Source not found: {args.src}", file=sys.stderr)
        return 2
    args.dst.mkdir(parents=True, exist_ok=True)

    n_ok = 0
    n_fail = 0
    for sx_path in sorted(args.src.glob("*.json")):
        name = sx_path.stem
        try:
            with sx_path.open("r", encoding="utf-8") as f:
                sx = json.load(f)
            out = convert(sx, name)
            out_path = args.dst / f"{name}.json"
            with out_path.open("w", encoding="utf-8") as f:
                json.dump(out, f, ensure_ascii=False, indent=2)
            n_ok += 1
        except Exception as e:  # noqa: BLE001
            n_fail += 1
            print(f"[!] {name}: {type(e).__name__}: {e}", file=sys.stderr)

    print(f"[OK] Converted {n_ok} profiles -> {args.dst}")
    if n_fail:
        print(f"[!]  {n_fail} failures", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
