// Copyright 2026 The Chronium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

// Per-launch HMAC-SHA256 license check. The antidetect launcher computes
//   token = HMAC_SHA256(kLicenseSecret, "{ts}|{ppid}" || nonce)[:16]
// once per chronium spawn and passes (ts, nonce, token) as command-line
// switches. ValidateLicenseToken() reads them back here, recomputes the
// HMAC with the BAKED secret (license_secret.h, gitignored), enforces a
// 60-second drift on `ts`, and confirms our parent process matches the
// ppid that was bound into the HMAC. Any failure exits the process
// silently (exit code 0) — no error message, no log, so a pirate running
// chrome.exe standalone just sees the window flash and close.

#ifndef CHROME_BROWSER_LICENSE_LICENSE_GATE_H_
#define CHROME_BROWSER_LICENSE_LICENSE_GATE_H_

namespace chronium {

// Reads `--license-ts=` / `--license-nonce=` / `--license-token=` from the
// process's command line and verifies them. Calls `_exit(0)` on any failure
// — does not return. On success, returns normally.
//
// Call this exactly once, very early in the browser process (after
// CommandLine is initialized, before any UI or profile work).
void EnforceLicenseGate();

}  // namespace chronium

#endif  // CHROME_BROWSER_LICENSE_LICENSE_GATE_H_
