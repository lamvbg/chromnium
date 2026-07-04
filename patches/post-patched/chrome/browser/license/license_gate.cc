// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/license/license_gate.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/license/license_secret.h"
#include "chrome/common/chrome_switches.h"
#include "third_party/boringssl/src/include/openssl/hmac.h"

namespace chronium {

namespace {

// 60-second drift allowance — launchers and chronium have to agree on
// wall-clock time within this window. Picked to cover NTP slew + slow
// disk-cold-start launches; tightening it further makes legitimate
// launches racy on under-spec'd Windows boxes.
constexpr int64_t kMaxClockDriftSeconds = 60;

// Truncated HMAC tag length we compare against. The HMAC itself is 32
// bytes; we ship the first 16 to keep the cmdline shorter. 128-bit
// truncated HMAC is the standard practice (RFC 2104 §5) and still gives
// 128 bits of forgery resistance.
constexpr size_t kTokenLen = 16;

// Decode an even-length lowercase-hex string into raw bytes. Returns
// false on any malformed character; we treat malformed inputs as gate
// failures rather than crashing.
bool HexDecode(std::string_view hex, std::vector<uint8_t>* out) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  out->resize(hex.size() / 2);
  for (size_t i = 0; i < out->size(); ++i) {
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int hi = nibble(hex[i * 2]);
    int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    (*out)[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

// Constant-time compare to keep the gate insensitive to side-channel
// timing analysis. Same shape as boringssl's CRYPTO_memcmp — we keep an
// in-line copy so callers don't have to plumb the header.
bool ConstantTimeEqual(base::span<const uint8_t> a,
                       base::span<const uint8_t> b) {
  if (a.size() != b.size()) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
}

// Compute HMAC-SHA256(kLicenseSecret, msg) truncated to kTokenLen. Used
// twice — once here to verify the launcher's token, and once in debug
// builds via the gen-debug-token tool for symmetry.
void ComputeExpectedToken(base::span<const uint8_t> msg,
                          base::span<uint8_t, kTokenLen> out) {
  uint8_t full[32] = {0};
  unsigned full_len = 0;
  HMAC(EVP_sha256(), kLicenseSecret, sizeof(kLicenseSecret),
       msg.data(), msg.size(), full, &full_len);
  auto full_span = base::span(full);
  out.copy_from(full_span.first<kTokenLen>());
}

[[noreturn]] void FailSilently() {
  // Match the launcher's _exit semantics: do not run any atexit handlers,
  // do not print a banner, do not return a non-zero code. From the
  // pirate's perspective the window flashes and disappears like a normal
  // crash — there is no hint that we explicitly rejected them.
  std::_Exit(0);
}

}  // namespace

void EnforceLicenseGate() {
  const auto* cmd = base::CommandLine::ForCurrentProcess();
  if (!cmd) {
    FailSilently();
  }

  const std::string ts_str = cmd->GetSwitchValueASCII(switches::kLicenseTs);
  const std::string nonce_hex = cmd->GetSwitchValueASCII(switches::kLicenseNonce);
  const std::string token_hex = cmd->GetSwitchValueASCII(switches::kLicenseToken);
  if (ts_str.empty() || nonce_hex.empty() || token_hex.empty()) {
    FailSilently();
  }

  // Parse + clamp the timestamp drift before doing any crypto work so
  // a quickly-replayed token gets rejected cheaply.
  int64_t ts = 0;
  if (!base::StringToInt64(ts_str, &ts)) {
    FailSilently();
  }
  const int64_t now =
      static_cast<int64_t>(std::time(nullptr));
  int64_t drift = now - ts;
  if (drift < 0) {
    drift = -drift;
  }
  if (drift > kMaxClockDriftSeconds) {
    FailSilently();
  }

  // Recompute the token. Message format is exactly what the launcher
  // uses: ASCII "ts|ppid" || nonce_bytes. ppid is the launcher's PID =
  // our parent process's PID; binding it means a replayed token from
  // outside the launcher's process tree fails immediately.
  std::vector<uint8_t> nonce, token;
  if (!HexDecode(nonce_hex, &nonce) || nonce.size() != 16) {
    FailSilently();
  }
  if (!HexDecode(token_hex, &token) || token.size() != kTokenLen) {
    FailSilently();
  }

  const base::ProcessId ppid = base::GetParentProcessId(
      base::GetCurrentProcessHandle());
  const std::string prefix =
      ts_str + "|" + base::NumberToString(static_cast<int64_t>(ppid));
  std::vector<uint8_t> msg(prefix.begin(), prefix.end());
  msg.insert(msg.end(), nonce.begin(), nonce.end());

  uint8_t expected[kTokenLen] = {0};
  ComputeExpectedToken(base::span(msg),
                       base::span<uint8_t, kTokenLen>(expected));

  if (!ConstantTimeEqual(base::span(token),
                         base::span<const uint8_t, kTokenLen>(expected))) {
    FailSilently();
  }
  // Pass: caller continues normal startup.
}

}  // namespace chronium
