// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

// Browser-process singleton holding per-profile fingerprint configuration
// loaded once at startup from the JSON file referenced by the
// --fingerprint-profile=<path> command-line switch.
//
// Renderers receive a typed subset of this config via Mojo (patch 0002).
// Browser-side code reads from this singleton directly via GetInstance().

#ifndef CHROME_BROWSER_FINGERPRINT_FINGERPRINT_PROFILE_H_
#define CHROME_BROWSER_FINGERPRINT_FINGERPRINT_PROFILE_H_

#include <array>
#include <cstdint>
#include <string>

#include <optional>

#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/common/fingerprint.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace content {
class RenderFrameHost;
}

namespace chronium {

class FingerprintProfile : public chrome::mojom::FingerprintConfigProvider {
 public:
  static FingerprintProfile* GetInstance();

  // Binder registered with PopulateChromeFrameBinders for the per-frame
  // FingerprintConfigProvider interface. The renderer reaches the browser
  // through this. Routes the receiver to the singleton.
  static void Bind(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<chrome::mojom::FingerprintConfigProvider> receiver);

  FingerprintProfile(const FingerprintProfile&) = delete;
  FingerprintProfile& operator=(const FingerprintProfile&) = delete;

  // Loads the JSON file at |path|. Called once during browser startup from
  // ChromeBrowserMainParts::PreCreateThreadsImpl(). Returns true on success.
  // On failure the singleton stays in "no profile" mode and IsLoaded()
  // returns false; renderer patches MUST fall back to default behavior.
  bool LoadFromFile(const base::FilePath& path);

  // chrome::mojom::FingerprintConfigProvider:
  void GetConfig(GetConfigCallback callback) override;

  // True once a JSON file has been successfully loaded.
  bool IsLoaded() const { return loaded_; }

  // 32-byte seed parsed from the JSON "seed" field. All deterministic noise
  // generators in renderer patches key off this seed. Returns all-zeros
  // when IsLoaded() is false.
  const std::array<uint8_t, 32>& Seed() const { return seed_; }

  // Raw JSON dict for accessors added by later patches. Null when not loaded.
  const base::DictValue* Dict() const {
    return loaded_ ? &dict_ : nullptr;
  }

  // WebGL UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL spoofs. Empty
  // strings mean "no override".
  std::string WebGLUnmaskedVendor() const;
  std::string WebGLUnmaskedRenderer() const;

  // navigator.userAgent / HTTP User-Agent. Empty = no override.
  std::string UserAgent() const;

  // navigator.platform. Empty = no override.
  std::string Platform() const;

  // Screen properties as a comma-separated "W,H,AW,AH,CD,DPR" string ready
  // for AppendSwitchASCII. Empty = no override.
  std::string ScreenCsv() const;

  // "concurrency,deviceMemory,maxTouchPoints". Empty = no override.
  std::string HardwareCsv() const;

  // "level,charging(0|1),chargingTime,dischargingTime". Empty = no override.
  std::string BatteryCsv() const;

  // "1" (proxy_only) | "2" (public_only) | "" (no override).
  std::string WebRtcPolicyValue() const;

  // Accept-Language header / navigator.languages comma-list. Empty = none.
  std::string AcceptLanguageCsv() const;

  // IANA timezone id. Empty = none.
  std::string Timezone() const;

  // Builds a full blink::UserAgentMetadata from the JSON 'user_agent_data'
  // dict (brands, platform, architecture, bitness, mobile, wow64,
  // full_version_list, ...). Returns nullopt when the field is missing,
  // which signals callers to fall back to Chromium's default metadata.
  std::optional<blink::UserAgentMetadata> UserAgentMetadata() const;

  // Comma-separated allowlist of font family names from JSON 'fonts' array.
  // Empty = no restriction.
  std::string FontsCsv() const;

  // performance.memory.jsHeapSizeLimit override, as a decimal string of
  // bytes ready for AppendSwitchASCII. Empty = no override.
  std::string MemoryHeapValue() const;

  // navigator.connection: "effective_type,downlink_mbps,rtt_msec,save_data".
  // Empty = no override.
  std::string ConnectionCsv() const;

  // navigator.storage.estimate() quota as decimal bytes. Empty = no override.
  std::string StorageQuotaValue() const;

  // WebGL "max_texture,max_vertex". Empty = no override.
  std::string WebGLLimitsCsv() const;

  // speechSynthesis.getVoices() as a TSV blob (see kFingerprintSpeechVoices
  // doc in chrome_switches.h). Empty = no override.
  std::string SpeechVoicesBlob() const;

  // WebAuthn isUserVerifyingPlatformAuthenticatorAvailable() — returns
  // "0" / "1" or empty when not pinned.
  std::string WebAuthnUvpaValue() const;

  // WebGL getSupportedExtensions() list joined by '\n'. Empty = no
  // override.
  std::string WebGLExtensionsBlob() const;

  // WebGL getParameter() + getShaderPrecisionFormat() override. Built
  // from `webgl.params` (single ints / int-ranges / float-ranges) and
  // `webgl.shader_precision` (per shader/precision triples) in the
  // profile JSON. Lines separated by '\n', tab-separated fields. See
  // chrome_switches.h::kFingerprintWebGLParams for the format. Empty
  // when neither key is set in the profile.
  std::string WebGLParamsBlob() const;

  // AudioContext sample-noise enable. "1" = enabled (legacy), "0" =
  // disabled. Empty when the profile doesn't explicitly set it (then
  // the renderer keeps the historical default of enabled).
  std::string AudioNoiseValue() const;

 private:
  friend class base::NoDestructor<FingerprintProfile>;
  FingerprintProfile();
  ~FingerprintProfile() override;

  bool loaded_ = false;
  base::DictValue dict_;
  std::array<uint8_t, 32> seed_{};

  // Holds every active renderer-side Remote so callbacks can resolve
  // even after a navigation rebinds the interface.
  mojo::ReceiverSet<chrome::mojom::FingerprintConfigProvider> receivers_;
};

}  // namespace chronium

#endif  // CHROME_BROWSER_FINGERPRINT_FINGERPRINT_PROFILE_H_
