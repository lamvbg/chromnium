"""Add webgl.params + webgl.shader_precision to every bundled profile.

Patch 0019 (in this same series) extends getParameter / getShaderPrecisionFormat
in chronium so the spoofed UNMASKED_RENDERER_WEBGL string is internally
consistent — same MAX_*, same range, same shader-precision triples a real
driver of the claimed vendor would report. Without this, CreepJS / iphey
catch "claims RTX 3060 but MAX_TEXTURE_SIZE == 8192" (a signature of older
Intel iGPUs) and flag the session as masked.

The 170 ShardX-derived profile JSONs we shipped in v0.1.x don't carry
these fields. This script walks the bundle, infers the vendor from
`webgl.unmasked_renderer`, and writes a vendor-appropriate `params` +
`shader_precision` dict into each file. Values come from real Chrome 148
on each vendor family; everything is fixed by ANGLE+D3D11 so vendors
overlap on most params (the few they differ on — MAX_VERTEX_UNIFORM_VECTORS,
ALIASED_*_RANGE — vary by family below).

Run once after each ShardX bundle refresh:
    python enrich-webgl-params.py
    python enrich-webgl-params.py --dry-run
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# GL enum values we care about (subset of <GL/gl.h> the WebGL spec exposes).
GL_ALIASED_LINE_WIDTH_RANGE     = 0x846E
GL_ALIASED_POINT_SIZE_RANGE     = 0x846D
GL_MAX_CUBE_MAP_TEXTURE_SIZE    = 0x851C
GL_MAX_RENDERBUFFER_SIZE        = 0x84E8
GL_MAX_TEXTURE_SIZE             = 0x0D33
GL_MAX_VIEWPORT_DIMS            = 0x0D3A
GL_MAX_VERTEX_ATTRIBS           = 0x8869
GL_MAX_VERTEX_UNIFORM_VECTORS   = 0x8DFB
GL_MAX_VARYING_VECTORS          = 0x8DFC
GL_MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD
GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS    = 0x8B4C
GL_MAX_TEXTURE_IMAGE_UNITS           = 0x8872
GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS  = 0x8B4D

# Shader / precision enums.
GL_VERTEX_SHADER   = 0x8B31
GL_FRAGMENT_SHADER = 0x8B30
GL_LOW_FLOAT       = 0x8DF0
GL_MEDIUM_FLOAT    = 0x8DF1
GL_HIGH_FLOAT      = 0x8DF2
GL_LOW_INT         = 0x8DF3
GL_MEDIUM_INT      = 0x8DF4
GL_HIGH_INT        = 0x8DF5


def hex_key(v: int) -> str:
    return f"0x{v:04X}"


# Common ANGLE+D3D11 values shared across desktop discrete GPUs (NVIDIA
# RTX/GTX, AMD Radeon RX). The Intel iGPU profile below overrides the
# fragment-uniform / cube-map / texture-size where the iGPU caps lower.
NVIDIA_AMD_PARAMS = {
    hex_key(GL_MAX_TEXTURE_SIZE):                  16384,
    hex_key(GL_MAX_CUBE_MAP_TEXTURE_SIZE):         16384,
    hex_key(GL_MAX_RENDERBUFFER_SIZE):             16384,
    hex_key(GL_MAX_VERTEX_ATTRIBS):                16,
    hex_key(GL_MAX_VERTEX_UNIFORM_VECTORS):        4095,
    hex_key(GL_MAX_VARYING_VECTORS):               30,
    hex_key(GL_MAX_FRAGMENT_UNIFORM_VECTORS):      1024,
    hex_key(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS):    32,
    hex_key(GL_MAX_TEXTURE_IMAGE_UNITS):           32,
    hex_key(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS):  32,
    hex_key(GL_MAX_VIEWPORT_DIMS):                 [32768, 32768],
    hex_key(GL_ALIASED_LINE_WIDTH_RANGE):          [1.0, 1.0],
    hex_key(GL_ALIASED_POINT_SIZE_RANGE):          [1.0, 1024.0],
}

INTEL_PARAMS = {
    hex_key(GL_MAX_TEXTURE_SIZE):                  16384,
    hex_key(GL_MAX_CUBE_MAP_TEXTURE_SIZE):         16384,
    hex_key(GL_MAX_RENDERBUFFER_SIZE):             16384,
    hex_key(GL_MAX_VERTEX_ATTRIBS):                16,
    # Intel iGPUs report this much lower than the NVIDIA/AMD 4095.
    hex_key(GL_MAX_VERTEX_UNIFORM_VECTORS):        1024,
    hex_key(GL_MAX_VARYING_VECTORS):               30,
    hex_key(GL_MAX_FRAGMENT_UNIFORM_VECTORS):      1024,
    hex_key(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS):    32,
    hex_key(GL_MAX_TEXTURE_IMAGE_UNITS):           32,
    hex_key(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS):  32,
    hex_key(GL_MAX_VIEWPORT_DIMS):                 [16384, 16384],
    hex_key(GL_ALIASED_LINE_WIDTH_RANGE):          [1.0, 1.0],
    hex_key(GL_ALIASED_POINT_SIZE_RANGE):          [1.0, 1024.0],
}

# Older Intel HD2500/HD3000 cap at 8192 for MAX_TEXTURE_SIZE.
INTEL_OLD_PARAMS = dict(INTEL_PARAMS, **{
    hex_key(GL_MAX_TEXTURE_SIZE):              8192,
    hex_key(GL_MAX_CUBE_MAP_TEXTURE_SIZE):     8192,
    hex_key(GL_MAX_RENDERBUFFER_SIZE):         8192,
    hex_key(GL_MAX_VIEWPORT_DIMS):             [8192, 8192],
})

# ANGLE on D3D11 reports the same shader-precision triples across vendors
# because the underlying HLSL compiler is the one answering. [range_min,
# range_max, precision].
ANGLE_D3D11_PRECISION = {
    hex_key(GL_VERTEX_SHADER): {
        hex_key(GL_LOW_FLOAT):    [127, 127, 23],
        hex_key(GL_MEDIUM_FLOAT): [127, 127, 23],
        hex_key(GL_HIGH_FLOAT):   [127, 127, 23],
        hex_key(GL_LOW_INT):      [31, 30, 0],
        hex_key(GL_MEDIUM_INT):   [31, 30, 0],
        hex_key(GL_HIGH_INT):     [31, 30, 0],
    },
    hex_key(GL_FRAGMENT_SHADER): {
        hex_key(GL_LOW_FLOAT):    [127, 127, 23],
        hex_key(GL_MEDIUM_FLOAT): [127, 127, 23],
        hex_key(GL_HIGH_FLOAT):   [127, 127, 23],
        hex_key(GL_LOW_INT):      [31, 30, 0],
        hex_key(GL_MEDIUM_INT):   [31, 30, 0],
        hex_key(GL_HIGH_INT):     [31, 30, 0],
    },
}


def pick_params(unmasked_renderer: str, name_hint: str) -> dict:
    s = (unmasked_renderer + " " + name_hint).lower()
    if "intel" in s or "uhd" in s or "iris" in s or "hd graphics" in s:
        # Distinguish the old HD2500/3000-era caps.
        if "hd2500" in s or "hd3000" in s or "hd 2500" in s or "hd 3000" in s:
            return INTEL_OLD_PARAMS
        return INTEL_PARAMS
    return NVIDIA_AMD_PARAMS


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path,
                    default=Path(__file__).resolve().parent.parent
                    / "config" / "profiles",
                    help="Directory holding the 170 *.json profiles.")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print what would change but don't write.")
    args = ap.parse_args()

    if not args.dir.is_dir():
        print(f"[!] not a directory: {args.dir}", file=sys.stderr)
        return 2

    touched = 0
    skipped = 0
    for path in sorted(args.dir.glob("*.json")):
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        webgl = data.get("webgl") or {}
        if "params" in webgl and "shader_precision" in webgl:
            skipped += 1
            continue
        unmasked = webgl.get("unmasked_renderer", "") or ""
        params = pick_params(unmasked, path.stem)
        webgl["params"] = params
        webgl["shader_precision"] = ANGLE_D3D11_PRECISION
        data["webgl"] = webgl
        touched += 1
        if args.dry_run:
            print(f"[would update] {path.name}")
            continue
        with path.open("w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        if touched <= 5:
            print(f"[updated] {path.name}")

    print(f"\nTotal: {touched} updated, {skipped} already had params")
    return 0


if __name__ == "__main__":
    sys.exit(main())
