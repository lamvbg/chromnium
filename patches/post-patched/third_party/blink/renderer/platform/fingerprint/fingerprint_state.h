// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

// Renderer-process-wide holder for the fingerprint seed delivered by the
// browser via Mojo (see patch 0002). All fingerprint surface patches
// (canvas, WebGL, audio, ...) read from this singleton via IsActive() +
// NoiseAtPixel() / PrngFor().
//
// Same renderer process = same Chronium browser instance = same
// fingerprint profile, so a process-wide single value is correct. Set
// is idempotent: if called twice with the same seed, second call is
// a no-op; with a different seed, second call is ignored (the first
// frame wins).

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_FINGERPRINT_FINGERPRINT_STATE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_FINGERPRINT_FINGERPRINT_STATE_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

class PLATFORM_EXPORT FingerprintState {
 public:
  // Sets the 32-byte seed for this renderer process. Safe to call
  // multiple times; only the first successful call has effect. The
  // |seed| must point to 32 bytes.
  static void Set(const uint8_t* seed_32);

  // Sets the WebGL UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL
  // strings returned by webgl getParameter. Empty strings mean "no
  // override" — keep the real driver value. First setter wins.
  static void SetWebGLStrings(std::string_view unmasked_vendor,
                              std::string_view unmasked_renderer);

  // True when a non-zero seed has been Set(). Fingerprint patches MUST
  // check this and fall back to default behavior when false.
  static bool IsActive();

  // Sets the navigator.platform override. Empty value = no override.
  // First setter wins.
  static void SetPlatform(std::string_view platform);

  // Sets the Accept-Language header / navigator.languages override.
  // Format: comma-separated tags ("en-US,en"). Empty = no override.
  static void SetAcceptLanguage(std::string_view value);
  static std::string_view AcceptLanguage();

  // IANA timezone id, e.g. "America/New_York". Empty = no override.
  static void SetTimezone(std::string_view value);
  static std::string_view Timezone();

  // Comma-separated allowlist of font family names. When set, fonts NOT in
  // the list are treated as unavailable by FontCache::GetFontPlatformData,
  // which is what local-font fingerprinters key on.
  static void SetFontsAllowlist(std::string_view csv);
  // True when the allowlist is unset (no restriction) OR when |family| is
  // in the allowlist. Match is case-insensitive on ASCII.
  static bool IsFontAllowed(std::string_view family);

  // Empty when no override is in effect.
  static std::string_view WebGLUnmaskedVendor();
  static std::string_view WebGLUnmaskedRenderer();
  static std::string_view Platform();

  // Per-profile screen properties: width, height, availWidth, availHeight,
  // colorDepth, devicePixelRatio. Width<=0 means "not set" -- callers
  // must fall back to the platform value.
  struct ScreenFp {
    int width = 0;
    int height = 0;
    int avail_width = 0;
    int avail_height = 0;
    int color_depth = 0;
    double device_pixel_ratio = 0.0;
  };
  static void SetScreen(const ScreenFp& screen);
  // Returns nullptr when no override is in effect.
  static const ScreenFp* Screen();

  // navigator.hardwareConcurrency / navigator.deviceMemory /
  // navigator.maxTouchPoints. concurrency==0 means "not set".
  struct HardwareFp {
    unsigned concurrency = 0;
    double device_memory = 0.0;
    unsigned max_touch_points = 0;
  };
  static void SetHardware(const HardwareFp& hw);
  static const HardwareFp* Hardware();

  // BatteryManager fields. level<0 means "not set".
  struct BatteryFp {
    double level = -1.0;
    bool charging = true;
    double charging_time = 0.0;
    double discharging_time = 0.0;
  };
  static void SetBattery(const BatteryFp& battery);
  static const BatteryFp* Battery();

  // WebRTC IP handling policy override. 0 = no override, 1 = relay-only
  // (kDisableNonProxiedUdp), 2 = public-interface-only.
  enum class WebRtcPolicy : int {
    kNone = 0,
    kProxyOnly = 1,
    kPublicOnly = 2,
  };
  static void SetWebRtcPolicy(WebRtcPolicy policy);
  static WebRtcPolicy GetWebRtcPolicy();

  // performance.memory.jsHeapSizeLimit override. 0 = not set.
  // The browser-side V8 limit is a runtime-derived number that leaks the
  // device class (e.g. 4294705152 = ~4GB on high-end vs 2147483648 = 2GB
  // on low-end). Real Chrome on real desktop reports 4294705152; we let
  // the profile pin this to a stable value.
  struct MemoryFp {
    uint64_t heap_size_limit = 0;  // bytes
  };
  static void SetMemory(const MemoryFp& memory);
  static const MemoryFp* Memory();

  // navigator.connection: effective_type ("4g","3g","2g","slow-2g"),
  // downlink (Mbps), rtt (ms), save_data flag. effective_type empty = not set.
  struct ConnectionFp {
    std::string effective_type;
    double downlink_mbps = 0.0;
    uint32_t rtt_msec = 0;
    bool save_data = false;
  };
  static void SetConnection(const ConnectionFp& conn);
  static const ConnectionFp* Connection();

  // navigator.storage.estimate() quota override. 0 = not set.
  struct StorageFp {
    uint64_t quota_bytes = 0;
  };
  static void SetStorage(const StorageFp& storage);
  static const StorageFp* Storage();

  // WebGL parameter overrides: GL_MAX_TEXTURE_SIZE / GL_MAX_VERTEX_ATTRIBS.
  // Both zero = no override.
  struct WebGLLimitsFp {
    int max_texture_size = 0;
    int max_vertex_attribs = 0;
  };
  static void SetWebGLLimits(const WebGLLimitsFp& limits);
  static const WebGLLimitsFp* WebGLLimits();

  // Full WebGL parameter surface — covers everything getParameter() can
  // probe that fingerprints behavior of the underlying driver. Pattern:
  // the browser side passes a TSV blob over a single switch
  // (--fingerprint-webgl-params=...) once at process start; we parse
  // into in-memory maps and the getParameter override consults them
  // before any GL roundtrip. Three value shapes show up in the spec:
  //   * single int  (MAX_TEXTURE_SIZE, MAX_VERTEX_ATTRIBS, …)
  //   * int range   (MAX_VIEWPORT_DIMS — 2 ints)
  //   * float range (ALIASED_LINE_WIDTH_RANGE, ALIASED_POINT_SIZE_RANGE)
  //
  // Blob format (one entry per line, tab-separated):
  //   i\t<pname-hex>\t<int-value>
  //   ir\t<pname-hex>\t<int1>\t<int2>
  //   fr\t<pname-hex>\t<float1>\t<float2>
  //   sp\t<shader-type-hex>\t<precision-type-hex>\t<range-min>\t<range-max>\t<precision>
  //
  // Lookups return sentinel "not set" values so the getParameter
  // override can fall through to the real driver call unchanged:
  //   WebGLIntParam       -> -1
  //   WebGLIntRangeParam  -> (INT32_MIN, INT32_MIN)
  //   WebGLFloatRangeParam-> (-1.0f, -1.0f)
  //   WebGLShaderPrecision-> (_, _, -1)
  static void SetWebGLParams(std::string_view tsv_blob);
  static int64_t WebGLIntParam(uint32_t pname);
  static std::pair<int32_t, int32_t> WebGLIntRangeParam(uint32_t pname);
  static std::pair<float, float> WebGLFloatRangeParam(uint32_t pname);
  static std::tuple<int32_t, int32_t, int32_t> WebGLShaderPrecision(
      uint32_t shader_type, uint32_t precision_type);

  // WebAuthn isUserVerifyingPlatformAuthenticatorAvailable() result.
  // Real devices return true on machines with Windows Hello / Touch ID /
  // platform-bound credentials, false otherwise. Bots that just spoof
  // the UA without backing it with the right platform-auth bit get caught
  // by sites that interrogate this — pin from the profile.
  // The optional tri-state: nullopt = no override, true/false = pinned.
  static void SetWebAuthnUvpa(bool uvpa);
  static bool HasWebAuthnUvpa();
  static bool WebAuthnUvpa();

  // WebGL getSupportedExtensions() override. Stored as the extensions
  // list joined by '\n' (no embedded newlines in real names), parsed
  // lazily into a vector. Empty = no override (real ANGLE-reported list).
  static void SetWebGLExtensions(std::string_view newline_list);
  static std::vector<std::string> WebGLExtensions();
  static bool HasWebGLExtensions();

  // speechSynthesis.getVoices() override. Voices are stored as a single
  // serialized blob to avoid heap fragmentation across multiple
  // SpeechSynthesis instances. Format: lines separated by '\n', each line
  // is "name\tlang\tdefault(0|1)\tlocal(0|1)\turi". Empty = not set.
  // Voices are returned as VoiceList (a vector of structs) on demand.
  struct VoiceFp {
    std::string name;
    std::string lang;
    bool is_default = false;
    bool local_service = false;
    std::string uri;
  };
  using VoiceList = std::vector<VoiceFp>;
  static void SetSpeechVoices(std::string_view tsv_blob);
  // Returns the parsed voice list (empty when no override).
  static VoiceList SpeechVoices();
  static bool HasSpeechVoices();

  // Per-vector noise enable. CreepJS / Pixelscan ship specific traps
  // that detect when the audio sample stream is being perturbed (their
  // "audio trap" hashes samples 80-90 and checks for the expected
  // oscillator output) — a profile pinned to a real-but-vanilla device
  // can opt out so the noise stops giving the spoof away. Default is
  // enabled (legacy behavior) until SetAudioNoise(false) is called.
  static void SetAudioNoise(bool enabled);
  static bool AudioNoiseEnabled();

  // Deterministic 32-bit hash of (process seed, channel, x, y).
  // |channel| should be a short stable string identifying the fingerprint
  // surface, e.g. "canvas", "webgl-readpixels", "audio".
  // Same (seed, channel, x, y) -> same output, every call, forever.
  static uint32_t HashAt(std::string_view channel, uint32_t x, uint32_t y);

 private:
  FingerprintState() = delete;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_FINGERPRINT_FINGERPRINT_STATE_H_
