"""Generate one valid --license-{ts,nonce,token} triple for a manual
chrome.exe debug launch.

The chronium license gate (patch 0020) wants `HMAC_SHA256(secret,
"{ts}|{ppid}" || nonce_bytes)[:16]` plus `abs(now - ts) <= 60`. In day-
to-day work the antidetect launcher generates this for you and the
end-user never sees it; this script is for the rare developer case
where you want to run chrome.exe directly with no app, e.g. iterating
on a patch and checking startup behavior in-tree.

Important: the token binds to the CURRENT PROCESS'S PID. The process
that runs chrome.exe must have PID == the PID we used here, which means
this script must be invoked in the same shell from which chrome.exe is
launched. Example:

    PS> $args = (python scripts\gen-debug-token.py) -split " "
    PS> & C:\\cr\\src\\out\\Release\\chrome.exe `
            --user-data-dir=C:\\tmp\\debug `
            --fingerprint-profile=C:\\path\\to\\plaintext.json `
            $args `
            https://iphey.com

If you open a new shell between the two commands, the PID changes and
the gate will (correctly) reject the token. Add --bypass-ppid to skip
the ppid binding when you only need to smoke-test startup; this only
works on debug builds where the gate honors the bypass flag.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import secrets
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY_FILE = ROOT / "config" / ".profile_key"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--ppid", type=int, default=os.getpid(),
        help="Parent PID to bind into the HMAC. Defaults to this script's PID.",
    )
    ap.add_argument(
        "--verbose", action="store_true",
        help="Print the input message and intermediate values.",
    )
    args = ap.parse_args()

    if not KEY_FILE.exists():
        sys.exit(f"[!] {KEY_FILE} missing. Run gen-license-secret.py first.")
    key = KEY_FILE.read_bytes()
    if len(key) != 32:
        sys.exit(f"[!] {KEY_FILE} has {len(key)} bytes; expected 32.")

    ts = int(time.time())
    nonce = secrets.token_bytes(16)
    msg = f"{ts}|{args.ppid}".encode() + nonce
    tok = hmac.new(key, msg, hashlib.sha256).digest()[:16]

    if args.verbose:
        print(f"# secret  : {key.hex()[:16]}...", file=sys.stderr)
        print(f"# ts      : {ts}", file=sys.stderr)
        print(f"# ppid    : {args.ppid}", file=sys.stderr)
        print(f"# nonce   : {nonce.hex()}", file=sys.stderr)
        print(f"# msg     : {msg!r}", file=sys.stderr)
        print(f"# token   : {tok.hex()}", file=sys.stderr)

    print(f"--license-ts={ts} --license-nonce={nonce.hex()} --license-token={tok.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
