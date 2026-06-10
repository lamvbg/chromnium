# Chronium patches

Numbered patch series applied on top of upstream Chromium 148.0.7778.217
`chronium-base` branch by `scripts\apply-patches.bat`. Order matters —
`0001` and `0002` set up the flag + Mojo + state plumbing every later
patch reads from.

## Workflow

1. After `scripts\fetch.bat` succeeds, `C:\cr\src` is on tag
   148.0.7778.217 with branch `chronium-base`.
2. `scripts\apply-patches.bat` applies the `.patch` files here onto
   `chronium-base` via `git am --3way`. Each patch becomes a commit.
3. `scripts\build.bat` produces `C:\cr\src\out\Release\chrome.exe`.
4. `scripts\package.bat` repackages into `release\chronium\chronium.exe`
   with rebranded resources.

When editing patches:
1. Make source edits under `C:\cr\src` directly on `chronium-base`.
2. Commit each logical change (or amend the relevant existing one).
3. Export the series back here:
   ```
   cd /d C:\cr\src
   git format-patch <tag>..chronium-base -o c:\Users\Admin\Desktop\Interlink\chronium-build\patches
   ```

## Series (15 patches, 25 fingerprint vectors covered)

| # | Vectors | File |
|---|---|---|
| 0001 | `--fingerprint-profile` switch + browser-process `FingerprintProfile` singleton (JSON loader, seed) | `0001-chronium-add-fingerprint-profile-switch-...patch` |
| 0002 | Mojo channel browser → renderer (legacy/fallback; the seed mostly travels via switch since 0004) | `0002-chronium-add-Mojo-plumbing-browser-renderer-...patch` |
| 0003 | Canvas `toDataURL` + `toBlob` seeded pixel noise (LSB flip on ~1/256 pixels) | `0003-chronium-seeded-canvas-pixel-noise-...patch` |
| 0004 | Synchronous seed via `--fingerprint-seed-hex` to renderers (kills first-load race) + `getImageData` noise + WebGL `UNMASKED_VENDOR_WEBGL` / `UNMASKED_RENDERER_WEBGL` spoof | `0004-chronium-sync-seed-propagation-...patch` |
| 0005 | Audio: `AudioBuffer.getChannelData` ~1e-5 multiplier + `AnalyserNode.getFloat/ByteFrequency/TimeDomainData` perturbation | `0005-chronium-seeded-sub-perceptible-noise-on-AudioBuffer.patch` |
| 0006 | `navigator.userAgent` (via existing `--user-agent`) + `navigator.platform` + matching HTTP `User-Agent` header | `0006-chronium-per-profile-navigator.userAgent-...patch` |
| 0007 | `screen.{width,height,availWidth,availHeight,colorDepth,pixelDepth}` + `window.devicePixelRatio` | `0007-chronium-per-profile-screen.-width-height-...patch` |
| 0008 | `navigator.hardwareConcurrency` + `deviceMemory` + `maxTouchPoints` + Battery 4 fields + WebRTC IP policy (force `enable_nonproxied_udp=false` etc.) | `0008-chronium-hardwareConcurrency-deviceMemory-...patch` |
| 0009 | `navigator.language(s)` (overrides DevTools probe path) + `Element.getBoundingClientRect` double-precision sub-pixel noise | `0009-chronium-per-profile-navigator.languages-Element.get...patch` |
| 0010 | `navigator.webdriver` = false + IANA timezone via `TimeZoneController::SetTimeZoneOverride` leaked-handle | `0010-chronium-hide-navigator.webdriver-per-profile-IANA-t.patch` |
| 0011 | `MediaDeviceInfo.deviceId` + `groupId` = 64-char seeded hex (per-profile-stable, cross-profile-unlinkable) | `0011-chronium-per-profile-seeded-HMAC-on-MediaDeviceInfo-.patch` |
| 0012 | WebGPU `GPUAdapterInfo.vendor` + `description` (mirrors WebGL UNMASKED_*) + suppress "Google API keys missing" infobar + suppress "launches when Windows starts" infobar | `0012-chronium-WebGPU-adapter-spoof-suppress-two-Chronium-.patch` |
| 0013 | UA Client Hints: full `blink::UserAgentMetadata` from JSON, drives every `Sec-CH-UA-*` request header AND `navigator.userAgentData.getHighEntropyValues()` | `0013-chronium-per-profile-UA-Client-Hints-via-...patch` |
| 0014 | `permissions.query` DENIED→ASK for notifications/geolocation/audio/video/midi (matches `Notification.permission === 'default'`) + per-profile font enumeration allowlist (locally-installed fonts not in list become unavailable to `measureText`) | `0014-chronium-permissions.query-DENIED-ASK-per-profile-fo.patch` |
| 0015 | `performance.memory.jsHeapSizeLimit` + `navigator.connection.{effectiveType,downlink,rtt,saveData}` + `navigator.storage.estimate().quota` + `speechSynthesis.getVoices()` (replaces OS voices with profile-pinned list) + WebGL `MAX_TEXTURE_SIZE`/`MAX_VERTEX_ATTRIBS` (parsing only, consumer left for follow-up) | `0015-chronium-add-memory-connection-storage-speech-...patch` |

## Plan §0 hard requirements — coverage

- **§0.1 Pass Google detect (Layer 1-3 only)** — covered by 0003-0014.
  Layer 0 (TLS/JA3, HTTP/2 frame ordering, TCP fingerprint) is *out of
  scope for the browser fork*; see the plan file for the TLS proxy
  sidecar approach.
- **§0.2.1 Fingerprint determinism** — seed sourced from JSON, all noise
  generators key off it via SipHash-style `FingerprintState::HashAt`.
- **§0.2.2 UA / UA-CH HTTP↔JS consistency** — single source of truth in
  the profile JSON; patch 0006 drives the legacy `User-Agent` + JS
  `navigator.userAgent`, patch 0013 drives every `Sec-CH-UA-*` HTTP
  header + `navigator.userAgentData`. Inconsistency between the two now
  requires editing the JSON.
- **§0.2.3 Device IDs stable** — patch 0011 HMACs `deviceId`/`groupId`
  with the seed, so they're stable per profile and unlinkable across
  profiles.

## Configuration

The single source of truth is the per-profile JSON passed via
`--fingerprint-profile=<path>`. See
[`config/example-profile.json`](../config/example-profile.json) for the
full schema. Browser-side `FingerprintProfile` reads it once at startup
in `PreCreateThreadsImpl`; everything else (renderer FingerprintState,
HTTP headers, WebRTC policy) is derived from there.

## Why no `0015`-`0020`

The original plan listed 20 patches because it expected one vector per
patch. In practice 0004, 0008, 0009, 0010, 0012, 0014 each bundled
multiple vectors that share infrastructure (switches, FingerprintProfile
accessors, state plumbing), and the rebrand step lives in
`scripts\package.bat` (rcedit on the binary) so didn't need its own
source patch.

## Adding a new vector

1. If new fingerprint config field: add the parser/accessor to
   `chrome/browser/fingerprint/fingerprint_profile.{h,cc}` and a setter
   /getter to `third_party/blink/renderer/platform/fingerprint/fingerprint_state.{h,cc}`.
2. If new command-line value: add the switch in
   `chrome/common/chrome_switches.{h,cc}`, append it from
   `ChromeContentBrowserClient::AppendExtraCommandLineSwitches`, and
   parse it in `ChromeContentRendererClient::RenderThreadStarted`.
3. Patch the Blink getter (or browser-side service) to read from
   `FingerprintState` with fall-through to platform default when
   `FingerprintState::IsActive()` is false.
4. `scripts\rebuild-patch.bat` (1-3 minutes), verify in a console.
5. `git commit -m "..."` on `chronium-base`, then
   `git format-patch <tag>..chronium-base -o patches\`.

## Reference

- [brave/brave-core](https://github.com/brave/brave-core)
  `chromium_src/` — production-quality patches for the same surfaces.
  Their `farbling` mechanism is a per-eTLD+1 PRNG; ours is per-profile.
- Original plan file:
  `C:\Users\Admin\.claude\plans\b-n-bi-t-v-wayfern-graceful-kazoo.md` —
  sections §0 (hard requirements), §4 (plumbing), §5 (vector table), §9
  (verification matrix).
