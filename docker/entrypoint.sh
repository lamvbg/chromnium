#!/bin/bash
# Chronium container entrypoint.
#
# 1. Boot Xvfb on :99 sized to match the profile's screen{} block.
# 2. Decrypt fingerprint JSON + optional --auto-localize via ip-api.
# 3. Generate license token bound to THIS shell's PID (which becomes
#    chrome's parent). The DRM gate in chrome.dll validates this before
#    startup — nothing else has to match, the token IS the auth.
# 4. Launch chrome as the entrypoint's foreground child so container
#    exit signal propagates cleanly.
#
# Required env:
#   CHRONIUM_LICENSE_KEY   — 64 hex chars, the 32-byte secret; read by
#                            decrypt-profile.py + gen-debug-token.py
#
# Optional env:
#   ACCOUNT_LABEL          — location bar chip text (default: unset,
#                            no chip)
#   FINGERPRINT            — profile id, e.g. "linux-rtx3060" (default:
#                            linux-nvidia-0000174d)
#   PROXY_URL              — HTTP/HTTPS/SOCKS5 for --proxy-server AND
#                            for the auto-localize geo lookup
#   START_URL              — page chrome opens first (default:
#                            about:blank)
#   USER_DATA_DIR          — mount point for browser data. Cookies /
#                            login state persist here across container
#                            restarts. Default: /data/user-data-dir.
#   SCREEN_WIDTH, SCREEN_HEIGHT — override Xvfb screen size
#                            (defaults: 1920x1080)
#   CDP_PORT               — override the --remote-debugging-port
#                            (default: 9222 to match EXPOSE)

set -euo pipefail

: "${FINGERPRINT:=linux-nvidia-0000174d}"
: "${START_URL:=about:blank}"
: "${USER_DATA_DIR:=/data/user-data-dir}"
: "${SCREEN_WIDTH:=1920}"
: "${SCREEN_HEIGHT:=1080}"
: "${CDP_PORT:=9222}"

if [ -z "${CHRONIUM_LICENSE_KEY:-}" ]; then
    echo "[!] CHRONIUM_LICENSE_KEY not set — chrome.exe will silent-exit at DRM gate."
    echo "    Pass -e CHRONIUM_LICENSE_KEY=<64-hex> on docker run."
    exit 2
fi

# ---- Persist the license key as .profile_key so the helpers can find
#      it via their normal ROOT/config/.profile_key resolution. This
#      duplicates the value across two places (env + file) intentionally
#      — the Python helpers read the file, the C++ gate reads its baked
#      constant, and env is the transport between them.
KEY_DIR=/opt/chronium/config
mkdir -p "${KEY_DIR}"
python3 -c "
import binascii, os, sys
hex_key = os.environ['CHRONIUM_LICENSE_KEY'].strip()
if len(hex_key) != 64:
    sys.exit('CHRONIUM_LICENSE_KEY must be 64 hex chars')
open('${KEY_DIR}/.profile_key','wb').write(binascii.unhexlify(hex_key))
"

mkdir -p "${USER_DATA_DIR}"

# ---- Boot Xvfb ----
# :99 is a conventional headless display. Xvfb runs in the background
# and dies with the container thanks to tini. Screen 0 is bit depth 24
# so canvas / WebGL colour probes see the same bit depth a real desktop
# reports.
export DISPLAY=:99
Xvfb :99 -screen 0 "${SCREEN_WIDTH}x${SCREEN_HEIGHT}x24" -nolisten tcp -nolisten unix &
XVFB_PID=$!

# Wait for Xvfb to accept connections — chrome fails startup if we race
# it. `xdpyinfo -display :99` returns non-zero until the socket is
# ready; poll with a hard 5s cap.
for _ in $(seq 1 25); do
    if xdpyinfo -display :99 >/dev/null 2>&1; then break; fi
    sleep 0.2
done
echo "[xvfb] display :99 ready (${SCREEN_WIDTH}x${SCREEN_HEIGHT})"

# ---- Decrypt fingerprint (+ auto-localize when possible) ----
DECRYPT_ARGS=(--to-temp --auto-localize)
if [ -n "${PROXY_URL:-}" ]; then
    DECRYPT_ARGS+=(--proxy "${PROXY_URL}")
fi
PLAIN_JSON=$(python3 /opt/chronium/scripts/decrypt-profile.py \
    "${FINGERPRINT}" "${DECRYPT_ARGS[@]}")
echo "[decrypt] plaintext at ${PLAIN_JSON}"

# ---- Generate per-launch license token bound to OUR pid ----
# This shell's PID = chrome's ppid after exec. gen-debug-token.py's
# --ppid $$ produces a token the DRM gate will accept.
TOKEN_ARGS=$(python3 /opt/chronium/scripts/gen-debug-token.py --ppid $$)
echo "[license] token generated"

# ---- Build chrome command line ----
CHROME_ARGS=(
    "--user-data-dir=${USER_DATA_DIR}"
    "--fingerprint-profile=${PLAIN_JSON}"
    "--remote-debugging-port=${CDP_PORT}"
    "--remote-debugging-address=0.0.0.0"
    "--remote-allow-origins=*"
    "--no-first-run"
    "--no-default-browser-check"
    "--use-angle=vulkan"
    # Container: no user session -> no seccomp / setuid sandbox helper.
    # This is the standard way to run chromium in a container; the
    # container is itself the sandbox from an OS PoV.
    "--no-sandbox"
    "--disable-dev-shm-usage"
)
if [ -n "${ACCOUNT_LABEL:-}" ]; then
    CHROME_ARGS+=("--account-label=${ACCOUNT_LABEL}")
fi
if [ -n "${PROXY_URL:-}" ]; then
    CHROME_ARGS+=("--proxy-server=${PROXY_URL}")
fi
# Split the token blob into individual args.
read -r -a TOKEN_SPLIT <<< "${TOKEN_ARGS}"
CHROME_ARGS+=("${TOKEN_SPLIT[@]}")
CHROME_ARGS+=("${START_URL}")

echo "[chrome] launching with ${#CHROME_ARGS[@]} args (label=${ACCOUNT_LABEL:-none}, fp=${FINGERPRINT})"

# exec so chrome becomes PID 1 of tini's child slot — signal handling
# from `docker stop` works cleanly.
exec /opt/chronium/bin/chrome "${CHROME_ARGS[@]}"
