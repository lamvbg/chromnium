"""One-shot: decrypt a single .json.enc to stdout or to a file.

Use this when launching chrome.exe by hand for a debug session — the
chronium binary expects a plaintext JSON path on --fingerprint-profile=,
so you need the full bytes of one decrypted profile sitting on disk.

Usage:
    # Decrypt to stdout
    python scripts/decrypt-profile.py win-rtx2000

    # Decrypt to a specific file
    python scripts/decrypt-profile.py win-rtx2000 -o C:\\tmp\\win-rtx2000.json

    # Decrypt to a temp file and print the path (one line, easy to capture
    # from PowerShell with `$plain = python ... | Select-Object -Last 1`)
    python scripts/decrypt-profile.py win-rtx2000 --to-temp
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parent.parent
KEY_FILE = ROOT / "config" / ".profile_key"
PROFILES_DIR = ROOT / "config" / "profiles"
MAGIC = b"CRP1"


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
    args = ap.parse_args()

    src = PROFILES_DIR / f"{args.name}.json.enc"
    if not src.exists():
        sys.exit(f"[!] {src} not found")

    plain = decrypt(src.read_bytes(), load_key())

    if args.to_temp:
        fd, path = tempfile.mkstemp(prefix=f"chronium-{args.name}-", suffix=".json")
        with os.fdopen(fd, "wb") as f:
            f.write(plain)
        print(path)
    elif args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(plain)
        print(f"[ok] wrote {args.out} ({len(plain)} bytes)", file=sys.stderr)
    else:
        sys.stdout.buffer.write(plain)

    return 0


if __name__ == "__main__":
    sys.exit(main())
