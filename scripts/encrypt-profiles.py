"""AES-256-GCM encrypt the bundled fingerprint JSONs.

Layer 2 of the chronium DRM (see also patches/0020-license-gate.patch and
scripts/gen-license-secret.py): even if a pirate cracks the HMAC license
gate, the 170 bundled fingerprint JSONs are stored as opaque ciphertext
on disk, so they can't be read or reused with a vanilla Chrome.

Format per encrypted file (binary, written next to the source as
``<name>.json.enc``):

    +--------+---------+--------+--------------------+
    | MAGIC  | nonce   | tag    | ciphertext         |
    | 4 B    | 12 B    | 16 B   | N B                |
    +--------+---------+--------+--------------------+

MAGIC = b"CRP1" so the loader rejects truncated / wrong-version files
fast. nonce is per-file random; tag is the AES-GCM authentication tag
(prevents tampering); ciphertext is the original JSON bytes. AAD is the
4-byte MAGIC so any version bump rotates the AAD.

Key source: ``config/.profile_key`` (32 raw bytes, written by
gen-license-secret.py). The same secret is baked into chrome.dll and
into the Tauri shell, so the backend can decrypt on launch and the
chronium binary stays oblivious.

Usage:
    python scripts/encrypt-profiles.py
    python scripts/encrypt-profiles.py --src <dir> --dst <dir>
    python scripts/encrypt-profiles.py --decrypt-test win-rtx2000
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = ROOT / "config" / "profiles"
DEFAULT_DST = ROOT / "config" / "profiles"
KEY_FILE    = ROOT / "config" / ".profile_key"
MAGIC       = b"CRP1"


def load_key() -> bytes:
    if not KEY_FILE.exists():
        sys.exit(
            f"[!] {KEY_FILE} missing. Run scripts/gen-license-secret.py first."
        )
    key = KEY_FILE.read_bytes()
    if len(key) != 32:
        sys.exit(f"[!] {KEY_FILE} has {len(key)} bytes; expected 32.")
    return key


def encrypt_one(plaintext: bytes, key: bytes) -> bytes:
    aead = AESGCM(key)
    nonce = os.urandom(12)
    # encrypt() returns ciphertext || tag (last 16 bytes are the tag).
    blob = aead.encrypt(nonce, plaintext, MAGIC)
    ct, tag = blob[:-16], blob[-16:]
    return MAGIC + nonce + tag + ct


def decrypt_one(packed: bytes, key: bytes) -> bytes:
    if len(packed) < 4 + 12 + 16:
        raise ValueError("blob too small")
    if packed[:4] != MAGIC:
        raise ValueError(f"bad magic; expected {MAGIC!r}, got {packed[:4]!r}")
    nonce = packed[4:16]
    tag = packed[16:32]
    ct = packed[32:]
    aead = AESGCM(key)
    return aead.decrypt(nonce, ct + tag, MAGIC)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--dst", type=Path, default=DEFAULT_DST)
    ap.add_argument(
        "--decrypt-test", metavar="NAME",
        help="Decrypt config/profiles/<NAME>.json.enc and print first 200 chars.",
    )
    ap.add_argument(
        "--keep-plaintext", action="store_true",
        help="Don't delete the .json after writing .json.enc. Default deletes.",
    )
    args = ap.parse_args()

    key = load_key()

    if args.decrypt_test:
        path = args.src / f"{args.decrypt_test}.json.enc"
        if not path.exists():
            sys.exit(f"[!] {path} missing.")
        plain = decrypt_one(path.read_bytes(), key)
        print(plain.decode("utf-8")[:200])
        print("...")
        print(f"[ok] decrypted {path.name} ({len(plain)} bytes)")
        return 0

    if not args.src.is_dir():
        sys.exit(f"[!] {args.src} not a directory")
    args.dst.mkdir(parents=True, exist_ok=True)

    encrypted = 0
    skipped = 0
    for src in sorted(args.src.glob("*.json")):
        dst = args.dst / f"{src.stem}.json.enc"
        try:
            packed = encrypt_one(src.read_bytes(), key)
        except (OSError, ValueError) as e:
            print(f"  [skip] {src.name}: {e}")
            skipped += 1
            continue
        dst.write_bytes(packed)
        if not args.keep_plaintext:
            src.unlink()
        encrypted += 1
        if encrypted <= 3:
            print(f"  [enc] {src.name} -> {dst.name} ({len(packed)} bytes)")

    print()
    print(f"[done] encrypted {encrypted} profiles, skipped {skipped}.")
    if encrypted:
        print(f"       output: {args.dst}")
    if not args.keep_plaintext and encrypted:
        print("       plaintext .json files deleted. Re-run convert-shardx-")
        print("       profiles.py to regenerate them if you need to edit one.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
