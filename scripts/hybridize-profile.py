"""
Take a Mac/Linux profile and graft the host's real hardware fingerprint
onto it. Result: UA, brands, fonts, voices, locale stay "Mac" while
WebGL, hardware_concurrency, and other GPU/CPU surfaces become "Windows
host's real values".

Why this exists: cross-platform spoof (claim macOS on a Windows host) is
visible to anti-detect tools because canvas + WebGL + audio rendering
all run on the host's actual hardware, and the resulting pixel/sample
hashes don't match what real Mac M1 produces. Wayfern / Multilogin
Enterprise solve this by renting actual Mac VMs; we can't, so instead
we ship a hybrid: a profile that ADMITS its hardware is the host while
keeping the soft fields (UA, language, timezone, voices) Mac-ish. This
covers two real-world use cases:

1. macOS users who run a Hackintosh / eGPU on AMD → genuinely
   "macOS with AMD GPU + N cores". Not impossible, just unusual.
2. Anti-detect sessions where the only thing that matters is the UA
   string + soft signals (Cloudflare bot check, sec-ch-ua), not the
   pixel-level canvas hash.

The result will not pass Pixelscan's strict "Masking detected" check
in every case (they have specific macOS<>GPU pairing rules) but will
pass most fingerprint sites that hash WebGL/canvas directly.

Usage:
  python hybridize-profile.py --name mac-m1-air13
  python hybridize-profile.py --name mac-m1-air13 --keep-mac-canvas
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Optional

PROFILES_DIR = (
    Path(__file__).resolve().parent.parent / "config" / "profiles"
)


def detect_host_gpu() -> dict[str, Any]:
    """Returns {vendor, renderer, unmasked_vendor, unmasked_renderer, max_texture, max_vertex_attribs}.
    Best-effort — falls back to generic values when wmic isn't available."""
    out = {
        "vendor": "Google Inc. (Unknown)",
        "renderer": "ANGLE (Unknown Device)",
        "unmasked_vendor": "Google Inc.",
        "unmasked_renderer": "ANGLE (Unknown)",
        "max_texture_size": 16384,
        "max_vertex_attribs": 16,
    }
    try:
        # On Windows, wmic returns the registered display adapter name.
        # Example: "AMD Radeon (TM) Graphics" or "NVIDIA GeForce RTX 3060".
        if sys.platform == "win32":
            wmic = subprocess.check_output(
                ["wmic", "path", "win32_VideoController", "get",
                 "Name,DriverVersion,AdapterRAM", "/format:list"],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=10,
            )
            # Parse Name= lines
            names: list[str] = []
            for line in wmic.splitlines():
                line = line.strip()
                if line.startswith("Name=") and len(line) > 5:
                    names.append(line[5:])
            # Filter out generic "Microsoft Basic Display" if a real GPU present
            real_gpus = [n for n in names if "Basic Display" not in n and "Hyper-V" not in n]
            primary = real_gpus[0] if real_gpus else (names[0] if names else "Unknown")
            if "AMD" in primary or "Radeon" in primary:
                out["vendor"] = "Google Inc. (AMD)"
                out["unmasked_vendor"] = "Google Inc. (AMD)"
                # Match ShardX's ANGLE pattern. The (0xNNNN) PCI device
                # ID is fluff that pixelscan/iphey don't validate.
                out["renderer"] = f"ANGLE (AMD, {primary} (0x00000000) Direct3D11 vs_5_0 ps_5_0, D3D11)"
                out["unmasked_renderer"] = out["renderer"]
            elif "NVIDIA" in primary or "GeForce" in primary or "Quadro" in primary:
                out["vendor"] = "Google Inc. (NVIDIA)"
                out["unmasked_vendor"] = "Google Inc. (NVIDIA)"
                out["renderer"] = f"ANGLE (NVIDIA, {primary} (0x00000000) Direct3D11 vs_5_0 ps_5_0, D3D11)"
                out["unmasked_renderer"] = out["renderer"]
            elif "Intel" in primary:
                out["vendor"] = "Google Inc. (Intel)"
                out["unmasked_vendor"] = "Google Inc. (Intel)"
                out["renderer"] = f"ANGLE (Intel, {primary} Direct3D11 vs_5_0 ps_5_0, D3D11)"
                out["unmasked_renderer"] = out["renderer"]
    except Exception as e:  # noqa: BLE001
        print(f"[hybrid] wmic GPU detect failed: {e}", file=sys.stderr)
    return out


def detect_host_cores() -> int:
    # On Windows, os.cpu_count() includes hyperthreads which is what
    # navigator.hardwareConcurrency reports too.
    return os.cpu_count() or 8


def hybridize(profile: dict[str, Any], keep_mac_canvas: bool) -> dict[str, Any]:
    out = copy.deepcopy(profile)
    gpu = detect_host_gpu()
    cores = detect_host_cores()

    # Replace WebGL with host's real GPU. Without this, the rendered
    # pixels match the host but the renderer string lies — that's the
    # canonical cross-platform tell.
    if not out.get("webgl"):
        out["webgl"] = {}
    out["webgl"]["vendor"] = gpu["vendor"]
    out["webgl"]["renderer"] = gpu["renderer"]
    out["webgl"]["unmasked_vendor"] = gpu["unmasked_vendor"]
    out["webgl"]["unmasked_renderer"] = gpu["unmasked_renderer"]
    out["webgl"]["max_texture_size"] = gpu["max_texture_size"]
    out["webgl"]["max_vertex_attribs"] = gpu["max_vertex_attribs"]
    # Drop the profile's claimed extension list — the host's real
    # context exposes a different set, and JS that tries getExtension()
    # on a Mac Metal extension over a D3D11 context gets null and
    # crashes its own probe. Empty list = renderer reports the real
    # underlying extension list (chronium patch 0019 falls through
    # when HasWebGLExtensions() is false).
    out["webgl"]["extensions"] = []

    # navigator.hardwareConcurrency — host's real CPU count. Sites
    # probe this via timing attacks (run N parallel Atomics-based
    # workers, time the throughput) and a profile claiming 8 cores on
    # a 56-thread host fails immediately. Truthful here.
    out["hardware_concurrency"] = cores

    if not keep_mac_canvas:
        # Drop canvas noise so the host's natural canvas hash gets
        # surfaced unchanged. Real Mac canvas pixels don't match
        # Windows GDI/DirectWrite output anyway — noising on top of a
        # Windows-rendered canvas doesn't make it look like Mac.
        if not out.get("noise"):
            out["noise"] = {}
        out["noise"]["canvas_threshold"] = 0
        out["noise"]["clientrects_subpixel"] = 0
        out["noise"]["measure_text_subpixel"] = 0
        # Audio noise already off-by-default after our converter update.

    out["_chronium_hybrid"] = {
        "gpu_vendor": gpu["vendor"],
        "gpu_renderer": gpu["renderer"],
        "host_cores": cores,
    }
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--out", default=None)
    parser.add_argument(
        "--keep-mac-canvas",
        action="store_true",
        help="leave canvas noise on (less truthful, slightly less"
             " detectable in some scenarios)",
    )
    args = parser.parse_args()

    src = PROFILES_DIR / f"{args.name}.json"
    if not src.exists():
        print(f"[!] Profile not found: {src}", file=sys.stderr)
        return 2
    with src.open("r", encoding="utf-8") as f:
        profile = json.load(f)

    out = hybridize(profile, keep_mac_canvas=args.keep_mac_canvas)

    print(
        f"[hybrid] grafted host GPU: "
        f"{out['_chronium_hybrid']['gpu_renderer'][:80]}",
        file=sys.stderr,
    )
    print(
        f"[hybrid] hardware_concurrency: {out['_chronium_hybrid']['host_cores']}",
        file=sys.stderr,
    )

    if args.out:
        out_path = Path(args.out)
    else:
        tmp = tempfile.NamedTemporaryFile(
            mode="w", prefix=f"chronium-hybrid-{args.name}-", suffix=".json",
            delete=False, encoding="utf-8",
        )
        out_path = Path(tmp.name)
        tmp.close()
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print(out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
