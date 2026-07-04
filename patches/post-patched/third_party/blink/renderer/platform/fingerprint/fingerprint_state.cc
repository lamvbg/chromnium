// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "third_party/blink/renderer/platform/fingerprint/fingerprint_state.h"

#include <atomic>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"

namespace blink {

namespace {

// Process-wide state. The seed is set once early in the process by
// chrome/renderer's Mojo response handler and read by every fingerprint
// surface patch thereafter. We use std::atomic on a flag + raw bytes
// behind it; the bytes are written exactly once before |g_active| flips
// to true, so concurrent readers see a fully-initialized seed.
std::array<uint8_t, 32> g_seed{};
std::atomic<bool> g_active{false};

// WebGL string overrides. base::NoDestructor avoids the
// -Wexit-time-destructors error Chromium enforces on plain static
// std::string. First-setter wins, controlled by g_webgl_set.
std::string& WebGLVendorStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::string& WebGLRendererStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_webgl_set{false};

std::string& PlatformStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_platform_set{false};

std::string& AcceptLanguageStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_accept_language_set{false};

std::string& TimezoneStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_timezone_set{false};

// Lowercased font names separated by '\n' for stable substring matching.
std::string& FontsAllowlistStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_fonts_allowlist_set{false};

FingerprintState::ScreenFp& ScreenStorage() {
  // POD with trivial destructor — base::NoDestructor would static-assert.
  static FingerprintState::ScreenFp s;
  return s;
}
std::atomic<bool> g_screen_set{false};

FingerprintState::HardwareFp& HardwareStorage() {
  static FingerprintState::HardwareFp s;
  return s;
}
std::atomic<bool> g_hw_set{false};

FingerprintState::BatteryFp& BatteryStorage() {
  static FingerprintState::BatteryFp s;
  return s;
}
std::atomic<bool> g_battery_set{false};

std::atomic<int> g_webrtc_policy{0};

FingerprintState::MemoryFp& MemoryStorage() {
  static FingerprintState::MemoryFp s;
  return s;
}
std::atomic<bool> g_memory_set{false};

FingerprintState::ConnectionFp& ConnectionStorage() {
  static base::NoDestructor<FingerprintState::ConnectionFp> s;
  return *s;
}
std::atomic<bool> g_connection_set{false};

FingerprintState::StorageFp& StorageStorage() {
  static FingerprintState::StorageFp s;
  return s;
}
std::atomic<bool> g_storage_set{false};

FingerprintState::WebGLLimitsFp& WebGLLimitsStorage() {
  static FingerprintState::WebGLLimitsFp s;
  return s;
}
std::atomic<bool> g_webgl_limits_set{false};

// Stored as raw TSV blob; parsed lazily on each SpeechVoices() call.
// SpeechSynthesis::getVoices() is rarely hot.
std::string& SpeechVoicesBlobStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_speech_voices_set{false};

std::atomic<bool> g_webauthn_uvpa_set{false};
std::atomic<bool> g_webauthn_uvpa_value{false};

std::string& WebGLExtensionsStorage() {
  static base::NoDestructor<std::string> s;
  return *s;
}
std::atomic<bool> g_webgl_extensions_set{false};

// Full WebGL parameter surface. Three parallel maps keyed by GLenum
// (pname) cover the three value shapes WebGLRenderingContextBase's
// getParameter switch returns: single int, int-range pair, float-range
// pair. Shader precision is a separate map keyed by the (shader_type,
// precision_type) composite, since the spec exposes it via the
// distinct getShaderPrecisionFormat() entry point. All four are
// populated once during SetWebGLParams() at process start and read
// from many times per frame, so we lock only on the rare write path
// and let the (cheap) std::unordered_map lookups race lock-free under
// the assumption the write happens before any getParameter call.
using WebGLIntMap = std::unordered_map<uint32_t, int64_t>;
using WebGLIntRangeMap = std::unordered_map<uint32_t,
                                            std::pair<int32_t, int32_t>>;
using WebGLFloatRangeMap = std::unordered_map<uint32_t,
                                              std::pair<float, float>>;
using WebGLPrecisionMap = std::unordered_map<uint64_t,
                                             std::tuple<int32_t, int32_t,
                                                        int32_t>>;

WebGLIntMap& WebGLIntStorage() {
  static base::NoDestructor<WebGLIntMap> s;
  return *s;
}
WebGLIntRangeMap& WebGLIntRangeStorage() {
  static base::NoDestructor<WebGLIntRangeMap> s;
  return *s;
}
WebGLFloatRangeMap& WebGLFloatRangeStorage() {
  static base::NoDestructor<WebGLFloatRangeMap> s;
  return *s;
}
WebGLPrecisionMap& WebGLPrecisionStorage() {
  static base::NoDestructor<WebGLPrecisionMap> s;
  return *s;
}
std::atomic<bool> g_webgl_params_set{false};

// Pack (shader_type, precision_type) into one 64-bit key. Both fit in
// 32 bits each.
uint64_t PrecisionKey(uint32_t shader_type, uint32_t precision_type) {
  return (static_cast<uint64_t>(shader_type) << 32) |
         static_cast<uint64_t>(precision_type);
}

std::atomic<bool> g_audio_noise_enabled{true};
std::atomic<bool> g_audio_noise_set{false};

// xxHash-style scalar mix. Cryptographically weak but plenty good for
// distinguishing fingerprints. Deterministic for the same (seed, channel,
// x, y) tuple.
uint32_t Mix32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  uint32_t h = a;
  h ^= b * 0x9E3779B1u;
  h ^= c * 0x85EBCA77u;
  h ^= d * 0xC2B2AE3Du;
  h = (h ^ (h >> 16)) * 0x7FEB352Du;
  h = (h ^ (h >> 15)) * 0x846CA68Bu;
  return h ^ (h >> 16);
}

// Folds a string into a 32-bit value. Tiny FNV-1a is enough — we only
// use a handful of channel names ("canvas", "webgl-readpixels", ...).
uint32_t FoldChannel(std::string_view s) {
  uint32_t h = 0x811C9DC5u;
  for (char c : s) {
    h = (h ^ static_cast<uint8_t>(c)) * 0x01000193u;
  }
  return h;
}

}  // namespace

// static
void FingerprintState::Set(const uint8_t* seed_32) {
  if (g_active.load(std::memory_order_acquire)) {
    return;  // First setter wins.
  }
  // Check whether the seed is all-zeros, which means "no profile" on the
  // browser side. Stay inactive in that case so patches keep default
  // behavior.
  // SAFETY: caller contract documented at FingerprintState::Set — |seed_32|
  // must point at a 32-byte buffer. Production build's
  // -Wunsafe-buffer-usage requires the wrap.
  auto seed_span = UNSAFE_BUFFERS(base::span<const uint8_t>(seed_32, 32u));
  bool any_nonzero = false;
  for (uint8_t b : seed_span) {
    if (b != 0) {
      any_nonzero = true;
      break;
    }
  }
  if (!any_nonzero) {
    return;
  }
  base::span(g_seed).copy_from(seed_span);
  g_active.store(true, std::memory_order_release);
}

// static
void FingerprintState::SetWebGLStrings(std::string_view unmasked_vendor,
                                       std::string_view unmasked_renderer) {
  if (g_webgl_set.load(std::memory_order_acquire)) {
    return;
  }
  WebGLVendorStorage().assign(unmasked_vendor);
  WebGLRendererStorage().assign(unmasked_renderer);
  g_webgl_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::IsActive() {
  return g_active.load(std::memory_order_acquire);
}

// static
std::string_view FingerprintState::WebGLUnmaskedVendor() {
  if (!g_webgl_set.load(std::memory_order_acquire)) {
    return {};
  }
  return WebGLVendorStorage();
}

// static
std::string_view FingerprintState::WebGLUnmaskedRenderer() {
  if (!g_webgl_set.load(std::memory_order_acquire)) {
    return {};
  }
  return WebGLRendererStorage();
}

// static
void FingerprintState::SetPlatform(std::string_view platform) {
  if (g_platform_set.load(std::memory_order_acquire) || platform.empty()) {
    return;
  }
  PlatformStorage().assign(platform);
  g_platform_set.store(true, std::memory_order_release);
}

// static
std::string_view FingerprintState::Platform() {
  if (!g_platform_set.load(std::memory_order_acquire)) {
    return {};
  }
  return PlatformStorage();
}

// static
void FingerprintState::SetAcceptLanguage(std::string_view value) {
  if (g_accept_language_set.load(std::memory_order_acquire) || value.empty()) {
    return;
  }
  AcceptLanguageStorage().assign(value);
  g_accept_language_set.store(true, std::memory_order_release);
}

// static
std::string_view FingerprintState::AcceptLanguage() {
  if (!g_accept_language_set.load(std::memory_order_acquire)) {
    return {};
  }
  return AcceptLanguageStorage();
}

// static
void FingerprintState::SetTimezone(std::string_view value) {
  if (g_timezone_set.load(std::memory_order_acquire) || value.empty()) {
    return;
  }
  TimezoneStorage().assign(value);
  g_timezone_set.store(true, std::memory_order_release);
}

// static
std::string_view FingerprintState::Timezone() {
  if (!g_timezone_set.load(std::memory_order_acquire)) {
    return {};
  }
  return TimezoneStorage();
}

// static
void FingerprintState::SetFontsAllowlist(std::string_view csv) {
  if (g_fonts_allowlist_set.load(std::memory_order_acquire) || csv.empty()) {
    return;
  }
  // Store as "\n<lower>\n<lower>\n..." so we can match
  // "\n<lower>\n" without splitting at lookup time.
  std::string& out = FontsAllowlistStorage();
  out = "\n";
  std::string current;
  for (char c : csv) {
    if (c == ',') {
      if (!current.empty()) {
        out += current;
        out += '\n';
        current.clear();
      }
    } else if (c == ' ' || c == '\t') {
      current += c;
    } else {
      // ASCII tolower; non-ASCII chars left as-is (acceptable: font names
      // we ship in profiles are ASCII).
      current += (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }
  }
  if (!current.empty()) {
    out += current;
    out += '\n';
  }
  g_fonts_allowlist_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::IsFontAllowed(std::string_view family) {
  if (!g_fonts_allowlist_set.load(std::memory_order_acquire)) {
    return true;
  }
  if (family.empty()) {
    return true;
  }
  std::string needle = "\n";
  for (char c : family) {
    if (c == ' ' || c == '\t') {
      needle += c;
    } else {
      needle += (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }
  }
  needle += '\n';
  return FontsAllowlistStorage().find(needle) != std::string::npos;
}

// static
void FingerprintState::SetScreen(const ScreenFp& screen) {
  if (g_screen_set.load(std::memory_order_acquire) || screen.width <= 0 ||
      screen.height <= 0) {
    return;
  }
  ScreenStorage() = screen;
  g_screen_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::ScreenFp* FingerprintState::Screen() {
  if (!g_screen_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &ScreenStorage();
}

// static
void FingerprintState::SetHardware(const HardwareFp& hw) {
  if (g_hw_set.load(std::memory_order_acquire) || hw.concurrency == 0) {
    return;
  }
  HardwareStorage() = hw;
  g_hw_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::HardwareFp* FingerprintState::Hardware() {
  if (!g_hw_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &HardwareStorage();
}

// static
void FingerprintState::SetBattery(const BatteryFp& battery) {
  if (g_battery_set.load(std::memory_order_acquire) || battery.level < 0.0) {
    return;
  }
  BatteryStorage() = battery;
  g_battery_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::BatteryFp* FingerprintState::Battery() {
  if (!g_battery_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &BatteryStorage();
}

// static
void FingerprintState::SetWebRtcPolicy(WebRtcPolicy policy) {
  int expected = 0;
  g_webrtc_policy.compare_exchange_strong(expected, static_cast<int>(policy),
                                          std::memory_order_acq_rel);
}

// static
FingerprintState::WebRtcPolicy FingerprintState::GetWebRtcPolicy() {
  return static_cast<WebRtcPolicy>(g_webrtc_policy.load(std::memory_order_acquire));
}

// static
void FingerprintState::SetMemory(const MemoryFp& memory) {
  if (g_memory_set.load(std::memory_order_acquire) ||
      memory.heap_size_limit == 0) {
    return;
  }
  MemoryStorage() = memory;
  g_memory_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::MemoryFp* FingerprintState::Memory() {
  if (!g_memory_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &MemoryStorage();
}

// static
void FingerprintState::SetConnection(const ConnectionFp& conn) {
  if (g_connection_set.load(std::memory_order_acquire) ||
      conn.effective_type.empty()) {
    return;
  }
  ConnectionStorage() = conn;
  g_connection_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::ConnectionFp* FingerprintState::Connection() {
  if (!g_connection_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &ConnectionStorage();
}

// static
void FingerprintState::SetStorage(const StorageFp& storage) {
  if (g_storage_set.load(std::memory_order_acquire) ||
      storage.quota_bytes == 0) {
    return;
  }
  StorageStorage() = storage;
  g_storage_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::StorageFp* FingerprintState::Storage() {
  if (!g_storage_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &StorageStorage();
}

// static
void FingerprintState::SetWebGLLimits(const WebGLLimitsFp& limits) {
  if (g_webgl_limits_set.load(std::memory_order_acquire) ||
      (limits.max_texture_size <= 0 && limits.max_vertex_attribs <= 0)) {
    return;
  }
  WebGLLimitsStorage() = limits;
  g_webgl_limits_set.store(true, std::memory_order_release);
}

// static
const FingerprintState::WebGLLimitsFp* FingerprintState::WebGLLimits() {
  if (!g_webgl_limits_set.load(std::memory_order_acquire)) {
    return nullptr;
  }
  return &WebGLLimitsStorage();
}

// static
void FingerprintState::SetWebAuthnUvpa(bool uvpa) {
  if (g_webauthn_uvpa_set.load(std::memory_order_acquire)) {
    return;
  }
  g_webauthn_uvpa_value.store(uvpa, std::memory_order_release);
  g_webauthn_uvpa_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::HasWebAuthnUvpa() {
  return g_webauthn_uvpa_set.load(std::memory_order_acquire);
}

// static
bool FingerprintState::WebAuthnUvpa() {
  return g_webauthn_uvpa_value.load(std::memory_order_acquire);
}

// static
void FingerprintState::SetWebGLExtensions(std::string_view newline_list) {
  if (g_webgl_extensions_set.load(std::memory_order_acquire) ||
      newline_list.empty()) {
    return;
  }
  WebGLExtensionsStorage().assign(newline_list);
  g_webgl_extensions_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::HasWebGLExtensions() {
  return g_webgl_extensions_set.load(std::memory_order_acquire);
}

// static
std::vector<std::string> FingerprintState::WebGLExtensions() {
  std::vector<std::string> out;
  if (!g_webgl_extensions_set.load(std::memory_order_acquire)) {
    return out;
  }
  const std::string& blob = WebGLExtensionsStorage();
  size_t pos = 0;
  while (pos < blob.size()) {
    size_t end = blob.find('\n', pos);
    if (end == std::string::npos) {
      end = blob.size();
    }
    if (end > pos) {
      out.emplace_back(blob.substr(pos, end - pos));
    }
    pos = end + 1;
  }
  return out;
}

// static
void FingerprintState::SetWebGLParams(std::string_view tsv_blob) {
  if (g_webgl_params_set.load(std::memory_order_acquire)) {
    return;  // first-set-wins; same first-frame discipline as the seed.
  }
  if (tsv_blob.empty()) {
    return;
  }
  auto& int_map = WebGLIntStorage();
  auto& int_range_map = WebGLIntRangeStorage();
  auto& float_range_map = WebGLFloatRangeStorage();
  auto& prec_map = WebGLPrecisionStorage();

  auto parse_u32 = [](std::string_view s, uint32_t* out) -> bool {
    if (s.empty()) return false;
    // Accept 0x-prefixed hex or plain decimal — the packer in
    // handlers_chronium picks whichever the source profile uses.
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
      s.remove_prefix(2);
      base = 16;
    }
    uint32_t v = 0;
    for (char c : s) {
      uint32_t digit = 0;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
      else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
      else return false;
      v = v * base + digit;
    }
    *out = v;
    return true;
  };
  auto parse_i64 = [](std::string_view s, int64_t* out) -> bool {
    if (s.empty()) return false;
    int64_t sign = 1;
    if (s[0] == '-') { sign = -1; s.remove_prefix(1); }
    int64_t v = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0');
    }
    *out = sign * v;
    return true;
  };
  auto parse_i32 = [&](std::string_view s, int32_t* out) -> bool {
    int64_t v = 0;
    if (!parse_i64(s, &v)) return false;
    *out = static_cast<int32_t>(v);
    return true;
  };
  auto parse_f32 = [](std::string_view s, float* out) -> bool {
    if (s.empty()) return false;
    double v = 0.0;
    if (!base::StringToDouble(std::string(s), &v)) return false;
    *out = static_cast<float>(v);
    return true;
  };

  size_t pos = 0;
  while (pos < tsv_blob.size()) {
    size_t end = tsv_blob.find('\n', pos);
    if (end == std::string::npos) end = tsv_blob.size();
    std::string_view line = tsv_blob.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty()) continue;

    // Split into fields on '\t'.
    std::vector<std::string_view> fields;
    size_t fp = 0;
    while (fp <= line.size()) {
      size_t fe = line.find('\t', fp);
      if (fe == std::string_view::npos) fe = line.size();
      fields.push_back(line.substr(fp, fe - fp));
      fp = fe + 1;
    }
    if (fields.size() < 3) continue;

    const std::string_view tag = fields[0];
    if (tag == "i" && fields.size() == 3) {
      uint32_t pname;
      int64_t value;
      if (parse_u32(fields[1], &pname) && parse_i64(fields[2], &value)) {
        int_map[pname] = value;
      }
    } else if (tag == "ir" && fields.size() == 4) {
      uint32_t pname;
      int32_t a, b;
      if (parse_u32(fields[1], &pname) && parse_i32(fields[2], &a) &&
          parse_i32(fields[3], &b)) {
        int_range_map[pname] = std::make_pair(a, b);
      }
    } else if (tag == "fr" && fields.size() == 4) {
      uint32_t pname;
      float a, b;
      if (parse_u32(fields[1], &pname) && parse_f32(fields[2], &a) &&
          parse_f32(fields[3], &b)) {
        float_range_map[pname] = std::make_pair(a, b);
      }
    } else if (tag == "sp" && fields.size() == 6) {
      uint32_t shader, prec;
      int32_t rmin, rmax, p;
      if (parse_u32(fields[1], &shader) && parse_u32(fields[2], &prec) &&
          parse_i32(fields[3], &rmin) && parse_i32(fields[4], &rmax) &&
          parse_i32(fields[5], &p)) {
        prec_map[PrecisionKey(shader, prec)] = std::make_tuple(rmin, rmax, p);
      }
    }
  }
  g_webgl_params_set.store(true, std::memory_order_release);
}

// static
int64_t FingerprintState::WebGLIntParam(uint32_t pname) {
  if (!g_webgl_params_set.load(std::memory_order_acquire)) {
    return -1;
  }
  const auto& m = WebGLIntStorage();
  auto it = m.find(pname);
  return it == m.end() ? -1 : it->second;
}

// static
std::pair<int32_t, int32_t> FingerprintState::WebGLIntRangeParam(
    uint32_t pname) {
  if (!g_webgl_params_set.load(std::memory_order_acquire)) {
    return std::make_pair(INT32_MIN, INT32_MIN);
  }
  const auto& m = WebGLIntRangeStorage();
  auto it = m.find(pname);
  return it == m.end() ? std::make_pair(INT32_MIN, INT32_MIN) : it->second;
}

// static
std::pair<float, float> FingerprintState::WebGLFloatRangeParam(
    uint32_t pname) {
  if (!g_webgl_params_set.load(std::memory_order_acquire)) {
    return std::make_pair(-1.0f, -1.0f);
  }
  const auto& m = WebGLFloatRangeStorage();
  auto it = m.find(pname);
  return it == m.end() ? std::make_pair(-1.0f, -1.0f) : it->second;
}

// static
std::tuple<int32_t, int32_t, int32_t> FingerprintState::WebGLShaderPrecision(
    uint32_t shader_type, uint32_t precision_type) {
  if (!g_webgl_params_set.load(std::memory_order_acquire)) {
    return std::make_tuple(0, 0, -1);
  }
  const auto& m = WebGLPrecisionStorage();
  auto it = m.find(PrecisionKey(shader_type, precision_type));
  return it == m.end() ? std::make_tuple(0, 0, -1) : it->second;
}

// static
void FingerprintState::SetAudioNoise(bool enabled) {
  g_audio_noise_enabled.store(enabled, std::memory_order_release);
  g_audio_noise_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::AudioNoiseEnabled() {
  return g_audio_noise_enabled.load(std::memory_order_acquire);
}

// static
void FingerprintState::SetSpeechVoices(std::string_view tsv_blob) {
  if (g_speech_voices_set.load(std::memory_order_acquire) || tsv_blob.empty()) {
    return;
  }
  SpeechVoicesBlobStorage().assign(tsv_blob);
  g_speech_voices_set.store(true, std::memory_order_release);
}

// static
bool FingerprintState::HasSpeechVoices() {
  return g_speech_voices_set.load(std::memory_order_acquire);
}

// static
FingerprintState::VoiceList FingerprintState::SpeechVoices() {
  VoiceList out;
  if (!g_speech_voices_set.load(std::memory_order_acquire)) {
    return out;
  }
  const std::string& blob = SpeechVoicesBlobStorage();
  size_t pos = 0;
  while (pos < blob.size()) {
    size_t line_end = blob.find('\n', pos);
    if (line_end == std::string::npos) {
      line_end = blob.size();
    }
    std::string_view line = std::string_view(blob).substr(pos, line_end - pos);
    pos = line_end + 1;
    if (line.empty()) {
      continue;
    }
    // Parse "name\tlang\tdefault\tlocal\turi" — 5 fields tab-separated.
    VoiceFp v;
    size_t f0 = 0;
    int field = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
      if (i == line.size() || line[i] == '\t') {
        std::string_view part = line.substr(f0, i - f0);
        switch (field) {
          case 0: v.name = std::string(part); break;
          case 1: v.lang = std::string(part); break;
          case 2: v.is_default = (part == "1"); break;
          case 3: v.local_service = (part == "1"); break;
          case 4: v.uri = std::string(part); break;
        }
        ++field;
        f0 = i + 1;
      }
    }
    if (!v.name.empty() && !v.lang.empty()) {
      if (v.uri.empty()) {
        v.uri = v.name;  // Match Chrome convention: voiceURI defaults to name.
      }
      out.push_back(std::move(v));
    }
  }
  return out;
}

// static
uint32_t FingerprintState::HashAt(std::string_view channel,
                                  uint32_t x,
                                  uint32_t y) {
  // Pack 16 bytes of seed into 4 uint32s for mixing. Using only half of
  // the 32-byte seed is fine — that's still 128 bits of entropy.
  uint32_t s0 = 0;
  uint32_t s1 = 0;
  auto seed_span = base::span(g_seed);
  base::byte_span_from_ref(s0).copy_from(seed_span.subspan(0u, 4u));
  base::byte_span_from_ref(s1).copy_from(seed_span.subspan(8u, 4u));
  return Mix32(s0 ^ s1, FoldChannel(channel), x, y);
}

}  // namespace blink
