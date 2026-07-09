// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/fingerprint/fingerprint_profile.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"

namespace chronium {

namespace {

// Parses a 32-byte seed from a 64-character hex string. Accepts lowercase
// and uppercase. Returns false on malformed input.
bool ParseSeedHex(std::string_view hex, std::array<uint8_t, 32>& out) {
  if (hex.size() != 64) {
    return false;
  }
  auto hex_to_nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (size_t i = 0; i < 32; ++i) {
    int hi = hex_to_nibble(hex[i * 2]);
    int lo = hex_to_nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace

// static
FingerprintProfile* FingerprintProfile::GetInstance() {
  static base::NoDestructor<FingerprintProfile> instance;
  return instance.get();
}

// static
void FingerprintProfile::Bind(
    content::RenderFrameHost* /*render_frame_host*/,
    mojo::PendingReceiver<chrome::mojom::FingerprintConfigProvider> receiver) {
  GetInstance()->receivers_.Add(GetInstance(), std::move(receiver));
}

FingerprintProfile::FingerprintProfile() = default;
FingerprintProfile::~FingerprintProfile() = default;

void FingerprintProfile::GetConfig(GetConfigCallback callback) {
  auto config = chrome::mojom::FingerprintConfig::New();
  config->seed = std::vector<uint8_t>(seed_.begin(), seed_.end());
  std::move(callback).Run(std::move(config));
}

std::string FingerprintProfile::WebGLUnmaskedVendor() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* webgl = dict_.FindDict("webgl");
  if (!webgl) {
    return {};
  }
  const std::string* s = webgl->FindString("unmasked_vendor");
  return s ? *s : std::string();
}

std::string FingerprintProfile::WebGLUnmaskedRenderer() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* webgl = dict_.FindDict("webgl");
  if (!webgl) {
    return {};
  }
  const std::string* s = webgl->FindString("unmasked_renderer");
  return s ? *s : std::string();
}

std::string FingerprintProfile::UserAgent() const {
  if (!loaded_) {
    return {};
  }
  const std::string* s = dict_.FindString("user_agent");
  return s ? *s : std::string();
}

std::string FingerprintProfile::Platform() const {
  if (!loaded_) {
    return {};
  }
  const std::string* s = dict_.FindString("platform");
  return s ? *s : std::string();
}

std::string FingerprintProfile::ScreenCsv() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* screen = dict_.FindDict("screen");
  if (!screen) {
    return {};
  }
  // All fields required for the override to take effect.
  std::optional<int> w = screen->FindInt("width");
  std::optional<int> h = screen->FindInt("height");
  if (!w || !h || *w <= 0 || *h <= 0) {
    return {};
  }
  int aw = screen->FindInt("avail_width").value_or(*w);
  int ah = screen->FindInt("avail_height").value_or(*h);
  int cd = screen->FindInt("color_depth").value_or(24);
  double dpr = screen->FindDouble("device_pixel_ratio").value_or(1.0);
  return base::StringPrintf("%d,%d,%d,%d,%d,%g", *w, *h, aw, ah, cd, dpr);
}

std::string FingerprintProfile::HardwareCsv() const {
  if (!loaded_) {
    return {};
  }
  std::optional<int> conc = dict_.FindInt("hardware_concurrency");
  if (!conc || *conc <= 0) {
    return {};
  }
  double dm = dict_.FindDouble("device_memory").value_or(4.0);
  int mtp = dict_.FindInt("max_touch_points").value_or(0);
  return base::StringPrintf("%d,%g,%d", *conc, dm, mtp);
}

std::string FingerprintProfile::BatteryCsv() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* bat = dict_.FindDict("battery");
  if (!bat) {
    return {};
  }
  std::optional<double> level = bat->FindDouble("level");
  if (!level) {
    return {};
  }
  std::optional<bool> charging = bat->FindBool("charging");
  // charging_time / discharging_time may be null in JSON for plugged in /
  // unplugged states; represent that as -1.
  double ct = bat->FindDouble("charging_time").value_or(-1.0);
  double dt = bat->FindDouble("discharging_time").value_or(-1.0);
  return base::StringPrintf("%g,%d,%g,%g", *level,
                            charging.value_or(true) ? 1 : 0, ct, dt);
}

std::string FingerprintProfile::WebRtcPolicyValue() const {
  if (!loaded_) {
    return {};
  }
  const std::string* mode = dict_.FindString("webrtc_policy");
  if (!mode) {
    return {};
  }
  if (*mode == "proxy_only") {
    return "1";
  }
  if (*mode == "public_only" || *mode == "disabled") {
    return "2";
  }
  return {};
}

std::string FingerprintProfile::Timezone() const {
  if (!loaded_) {
    return {};
  }
  const std::string* s = dict_.FindString("timezone");
  return s ? *s : std::string();
}

namespace {

// Walks a JSON list of {brand, version} dicts and appends to |out|.
void AppendBrandList(const base::ListValue* list,
                     blink::UserAgentBrandList& out) {
  if (!list) {
    return;
  }
  for (const auto& item : *list) {
    const base::DictValue* d = item.GetIfDict();
    if (!d) continue;
    const std::string* brand = d->FindString("brand");
    const std::string* version = d->FindString("version");
    if (brand && version) {
      out.emplace_back(*brand, *version);
    }
  }
}

}  // namespace

std::optional<blink::UserAgentMetadata>
FingerprintProfile::UserAgentMetadata() const {
  if (!loaded_) {
    return std::nullopt;
  }
  const base::DictValue* uad = dict_.FindDict("user_agent_data");
  if (!uad) {
    return std::nullopt;
  }
  blink::UserAgentMetadata m;
  AppendBrandList(uad->FindList("brands"), m.brand_version_list);
  AppendBrandList(uad->FindList("full_version_list"),
                  m.brand_full_version_list);
  if (const std::string* s = uad->FindString("platform")) m.platform = *s;
  if (const std::string* s = uad->FindString("platform_version"))
    m.platform_version = *s;
  if (const std::string* s = uad->FindString("architecture"))
    m.architecture = *s;
  if (const std::string* s = uad->FindString("bitness")) m.bitness = *s;
  if (const std::string* s = uad->FindString("model")) m.model = *s;
  if (std::optional<bool> b = uad->FindBool("mobile")) m.mobile = *b;
  if (std::optional<bool> b = uad->FindBool("wow64")) m.wow64 = *b;
  // full_version: derived from the first entry of brand_full_version_list
  // (Chromium's own brand) — most fingerprinters never read it directly.
  if (!m.brand_full_version_list.empty()) {
    m.full_version = m.brand_full_version_list.front().version;
  }
  return m;
}

std::string FingerprintProfile::FontsCsv() const {
  if (!loaded_) {
    return {};
  }
  const base::ListValue* list = dict_.FindList("fonts");
  if (!list || list->empty()) {
    return {};
  }
  std::string out;
  for (const auto& item : *list) {
    if (const std::string* s = item.GetIfString()) {
      if (!out.empty()) out += ",";
      out += *s;
    }
  }
  return out;
}

std::string FingerprintProfile::AcceptLanguageCsv() const {
  if (!loaded_) {
    return {};
  }
  // Prefer the JS-side "languages" array if present (matches what JS sees).
  const base::ListValue* langs = dict_.FindList("languages");
  if (langs && !langs->empty()) {
    std::string out;
    for (const auto& item : *langs) {
      if (const std::string* s = item.GetIfString()) {
        if (!out.empty()) {
          out += ",";
        }
        out += *s;
      }
    }
    if (!out.empty()) {
      return out;
    }
  }
  // Fall back to the HTTP-style accept_language string.
  const std::string* al = dict_.FindString("accept_language");
  return al ? *al : std::string();
}

std::string FingerprintProfile::MemoryHeapValue() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* mem = dict_.FindDict("memory");
  if (!mem) {
    return {};
  }
  // JSON stores integers as int (32-bit). For 4 GB heap_size_limit we need
  // 64-bit. Accept either number (which can lose precision past 2^53) or a
  // string-encoded integer for exactness.
  if (const std::string* s = mem->FindString("heap_size_limit")) {
    return *s;
  }
  std::optional<double> d = mem->FindDouble("heap_size_limit");
  if (!d.has_value() || *d <= 0.0) {
    return {};
  }
  return base::StringPrintf("%llu",
                            static_cast<unsigned long long>(*d));
}

std::string FingerprintProfile::ConnectionCsv() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* conn = dict_.FindDict("connection");
  if (!conn) {
    return {};
  }
  const std::string* etype = conn->FindString("effective_type");
  if (!etype || etype->empty()) {
    return {};
  }
  double downlink = conn->FindDouble("downlink_mbps").value_or(10.0);
  int rtt = conn->FindInt("rtt_msec").value_or(50);
  bool save_data = conn->FindBool("save_data").value_or(false);
  return base::StringPrintf("%s,%g,%d,%d", etype->c_str(), downlink, rtt,
                            save_data ? 1 : 0);
}

std::string FingerprintProfile::StorageQuotaValue() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* st = dict_.FindDict("storage_estimate");
  if (!st) {
    return {};
  }
  if (const std::string* s = st->FindString("quota_bytes")) {
    return *s;
  }
  std::optional<double> gb = st->FindDouble("quota_gb");
  if (gb.has_value() && *gb > 0.0) {
    uint64_t bytes = static_cast<uint64_t>(*gb * 1024.0 * 1024.0 * 1024.0);
    return base::StringPrintf("%llu",
                              static_cast<unsigned long long>(bytes));
  }
  return {};
}

std::string FingerprintProfile::WebGLLimitsCsv() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* webgl = dict_.FindDict("webgl");
  if (!webgl) {
    return {};
  }
  int max_tex = webgl->FindInt("max_texture_size").value_or(0);
  int max_vtx = webgl->FindInt("max_vertex_attribs").value_or(0);
  if (max_tex <= 0 && max_vtx <= 0) {
    return {};
  }
  return base::StringPrintf("%d,%d", max_tex, max_vtx);
}

std::string FingerprintProfile::SpeechVoicesBlob() const {
  if (!loaded_) {
    return {};
  }
  // Either profile.speech.voices (ShardX convention) or profile.speech_voices.
  const base::ListValue* voices = nullptr;
  if (const base::DictValue* speech = dict_.FindDict("speech")) {
    voices = speech->FindList("voices");
  }
  if (!voices) {
    voices = dict_.FindList("speech_voices");
  }
  if (!voices || voices->empty()) {
    return {};
  }
  std::string out;
  for (const auto& v : *voices) {
    const base::DictValue* d = v.GetIfDict();
    if (!d) {
      continue;
    }
    const std::string* name = d->FindString("name");
    const std::string* lang = d->FindString("lang");
    if (!name || !lang || name->empty() || lang->empty()) {
      continue;
    }
    bool is_default = d->FindBool("is_default").value_or(false);
    bool local = d->FindBool("local_service").value_or(false);
    const std::string* uri = d->FindString("voice_uri");
    if (!out.empty()) {
      out += '\n';
    }
    out += *name;
    out += '\t';
    out += *lang;
    out += '\t';
    out += is_default ? '1' : '0';
    out += '\t';
    out += local ? '1' : '0';
    out += '\t';
    out += (uri && !uri->empty()) ? *uri : *name;
  }
  return out;
}

std::string FingerprintProfile::WebAuthnUvpaValue() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* wa = dict_.FindDict("webauthn");
  if (!wa) {
    return {};
  }
  std::optional<bool> u = wa->FindBool("uvpa");
  if (!u.has_value()) {
    return {};
  }
  return *u ? "1" : "0";
}

std::string FingerprintProfile::WebGLExtensionsBlob() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* webgl = dict_.FindDict("webgl");
  if (!webgl) {
    return {};
  }
  const base::ListValue* exts = webgl->FindList("extensions");
  if (!exts || exts->empty()) {
    return {};
  }
  std::string out;
  for (const auto& item : *exts) {
    const std::string* s = item.GetIfString();
    if (!s || s->empty()) {
      continue;
    }
    if (!out.empty()) {
      out += '\n';
    }
    out += *s;
  }
  return out;
}

std::string FingerprintProfile::WebGLParamsBlob() const {
  // Build a TSV blob the renderer's FingerprintState::SetWebGLParams
  // parser consumes. Read both `webgl.params` (a dict keyed by GLenum
  // hex; values are int, [int,int], or [float,float]) and
  // `webgl.shader_precision` (a dict keyed by shader_type-hex, each
  // value is itself a dict keyed by precision_type-hex with a triple
  // [range_min, range_max, precision]). Both keys are optional —
  // omitting one just leaves that table unpopulated on the renderer
  // side.
  if (!loaded_) {
    return {};
  }
  const base::DictValue* webgl = dict_.FindDict("webgl");
  if (!webgl) {
    return {};
  }
  std::string out;
  auto append_line = [&](std::string line) {
    if (!out.empty()) {
      out += '\n';
    }
    out += std::move(line);
  };

  const base::DictValue* params = webgl->FindDict("params");
  if (params) {
    for (const auto [pname_str, value] : *params) {
      // pname can be either int or string-of-int. Use the string form
      // verbatim — the renderer parser accepts decimal or 0xHEX.
      if (value.is_int()) {
        append_line("i\t" + pname_str + "\t" +
                    base::NumberToString(value.GetInt()));
      } else if (const base::ListValue* arr = value.GetIfList()) {
        if (arr->size() != 2) continue;
        const base::Value& a = (*arr)[0];
        const base::Value& b = (*arr)[1];
        if (a.is_int() && b.is_int()) {
          append_line("ir\t" + pname_str + "\t" +
                      base::NumberToString(a.GetInt()) + "\t" +
                      base::NumberToString(b.GetInt()));
        } else if (a.is_double() || a.is_int()) {
          // Float-range — accept ints too (e.g. [1, 1] for line width).
          double da = a.is_double() ? a.GetDouble() : a.GetInt();
          double db = b.is_double() ? b.GetDouble() : b.GetInt();
          append_line("fr\t" + pname_str + "\t" +
                      base::NumberToString(da) + "\t" +
                      base::NumberToString(db));
        }
      }
    }
  }

  const base::DictValue* prec = webgl->FindDict("shader_precision");
  if (prec) {
    for (const auto [shader_str, shader_v] : *prec) {
      const base::DictValue* inner = shader_v.GetIfDict();
      if (!inner) continue;
      for (const auto [prec_str, val] : *inner) {
        const base::ListValue* arr = val.GetIfList();
        if (!arr || arr->size() != 3) continue;
        if (!(*arr)[0].is_int() || !(*arr)[1].is_int() ||
            !(*arr)[2].is_int()) {
          continue;
        }
        append_line("sp\t" + shader_str + "\t" + prec_str + "\t" +
                    base::NumberToString((*arr)[0].GetInt()) + "\t" +
                    base::NumberToString((*arr)[1].GetInt()) + "\t" +
                    base::NumberToString((*arr)[2].GetInt()));
      }
    }
  }
  return out;
}

std::string FingerprintProfile::AudioNoiseValue() const {
  if (!loaded_) {
    return {};
  }
  const base::DictValue* noise = dict_.FindDict("noise");
  if (!noise) {
    return {};
  }
  // The profile JSON exposes audio_amplitude as the scale; treat 0 or
  // negative as "noise disabled". Anything > 0 keeps the legacy noised
  // path active.
  std::optional<double> amp = noise->FindDouble("audio_amplitude");
  if (!amp.has_value()) {
    return {};
  }
  return *amp > 0.0 ? "1" : "0";
}

bool FingerprintProfile::LoadFromFile(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    LOG(WARNING) << "[chronium] Cannot read fingerprint profile at "
                 << path.value();
    return false;
  }
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!parsed) {
    LOG(WARNING) << "[chronium] Fingerprint profile is not a JSON object: "
                 << path.value();
    return false;
  }
  dict_ = std::move(*parsed);

  if (const std::string* seed_hex = dict_.FindString("seed")) {
    if (!ParseSeedHex(*seed_hex, seed_)) {
      LOG(WARNING) << "[chronium] Fingerprint profile seed must be 64 hex "
                      "chars; ignoring seed. File: "
                   << path.value();
    }
  }

  loaded_ = true;
  LOG(INFO) << "[chronium] Loaded fingerprint profile from " << path.value();
  return true;
}

}  // namespace chronium
