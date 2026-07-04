# Chronium Docker (Linux)

Run chronium as a headless anti-detect browser in a container. Xvfb
gives it a virtual display so `window.screen` / canvas / WebGL stay
consistent with the fingerprint's spoofed values; CDP is exposed on
port 9222 so a Playwright / raw CDP controller outside the container
drives navigation.

## Prereqs

1. **Chronium Linux binary**. Build from the [chronium-build](https://github.com/lamvbg/chromnium)
   repo with `docker/build-linux.sh` (WSL2 or native Ubuntu), or grab
   the tarball from a GitHub release once available.
2. **`.profile_key`** — the same 32-byte secret baked into
   chrome.dll's DRM gate. Hex-encoded (64 chars).
3. **Docker 24+** with `docker compose`.

## Build image

```bash
# Drop chronium-linux-v0.1.5.tar.gz here first:
cp /path/from/build/chronium-linux-v0.1.5.tar.gz release/

# Build image with license key baked (or pass at runtime, see below):
docker build -t chronium-linux:v0.1.5 .
```

## Run one profile

```bash
docker run -d \
  -p 9222:9222 \
  -e CHRONIUM_LICENSE_KEY=$(xxd -p -c 64 ~/.profile_key) \
  -e ACCOUNT_LABEL=acc1 \
  -e FINGERPRINT=linux-rtx3060 \
  -e START_URL=https://grok.com \
  -e PROXY_URL=socks5://user:pass@proxy:1080 \
  -v $(pwd)/data/acc1:/data/user-data-dir \
  --shm-size=2g \
  chronium-linux:v0.1.5
```

Environment reference (all optional except `CHRONIUM_LICENSE_KEY`):

| Var | Default | Notes |
|---|---|---|
| `CHRONIUM_LICENSE_KEY` | — | 64 hex chars; without it the DRM gate silently exits |
| `ACCOUNT_LABEL` | (unset) | Location bar chip text; drives the deterministic pastel color |
| `FINGERPRINT` | `linux-nvidia-0000174d` | Profile id; must exist in `/opt/chronium/config/profiles/` |
| `START_URL` | `about:blank` | First page opened |
| `USER_DATA_DIR` | `/data/user-data-dir` | Mount a volume here for cookie/login persistence |
| `SCREEN_WIDTH` / `SCREEN_HEIGHT` | `1920` / `1080` | Xvfb screen; keep close to what your fingerprint claims |
| `CDP_PORT` | `9222` | Match `-p HOST:9222` |
| `PROXY_URL` | (unset) | HTTP/HTTPS/SOCKS5; used for both `--proxy-server` AND the ip-api auto-localize probe |

## Control via CDP

```python
# Python — websockets + json
import json, websocket, requests

# Discover the browser WS endpoint
info = requests.get("http://server:9222/json/version").json()
ws_url = info["webSocketDebuggerUrl"]

# Speak CDP
ws = websocket.create_connection(ws_url)
ws.send(json.dumps({"id": 1, "method": "Target.getTargets"}))
print(json.loads(ws.recv()))
```

For higher-level use, point Playwright at it:

```python
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    browser = p.chromium.connect_over_cdp("ws://server:9222/devtools/browser/...")
    page = browser.contexts[0].pages[0]
    page.goto("https://iphey.com")
    page.screenshot(path="check.png")
```

## Multi-account

`docker-compose.yml` runs 3 accounts on ports 9222/9223/9224, each with
its own persistent volume + fingerprint + optional proxy. Copy-paste
the block per new account and bump the port.

## Anti-detect notes on Linux

Linux fingerprints in the pool (19 total: `linux-*.json.enc`) match
Ubuntu/Fedora dev-workstation devices. iphey / creepjs treat Linux
Chrome slightly harsher than Windows Chrome by default — expect:

- **Trustworthy** on iphey when Xvfb resolution + `PROXY_URL` region
  line up with the profile's timezone (via `--auto-localize`).
- **Trust ≥65%** on creepjs (Windows profiles land 75-80%).
- Fonts are the biggest cross-check: fontconfig on Ubuntu 22.04 with
  the packages this image installs matches a typical Ubuntu desktop.
  Don't strip fonts — the profile's `webgl.extensions` and the
  `fonts` list assume Noto/Roboto/Liberation are present.

## Troubleshooting

**Container exits immediately.** Check logs — the DRM gate is silent
by design but `[!] CHRONIUM_LICENSE_KEY not set` will show if the env
var is missing. Also verify `CHRONIUM_LICENSE_KEY` is exactly 64 hex
chars (no whitespace, no newlines).

**iphey shows LOCATION red.** `--auto-localize` needs egress to
`ip-api.com`. If a proxy is set, urllib inside `decrypt-profile.py`
routes through it for HTTP/HTTPS proxies. SOCKS5 falls back to direct
(the launcher-side lookup — the BROWSER still tunnels through the
socks5 proxy at launch).

**Container fingerprint doesn't match host CPU.** All Linux profiles
in the pool declare specific GPUs (NVIDIA GT1030 / GTX1050 / RTX3060,
AMD RX480, Intel UHD). Running with SwiftShader (no GPU device
passed in) still reports the profile's spoofed vendor/renderer via
patch 0004, but shader precision + MAX_TEXTURE_SIZE probes (patch
0019) come from the JSON, not the actual GPU, so this is safe.
