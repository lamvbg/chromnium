# Chronium Linux headless container.
#
# Design goals:
#   - Xvfb virtual display so window.screen matches the fingerprint's
#     screen{} block. Chromium's native --headless leaks a distinct
#     UA + DOM surface — every real anti-detect deploy uses Xvfb.
#   - Real fontconfig with the allowlist a real Ubuntu desktop ships
#     (Roboto / Noto / Ubuntu). Patch 0009 (fonts) enforces the
#     profile.fonts list, but fontconfig underneath must still HAVE
#     the fonts on disk to hand back.
#   - Chronium binary + 170 encrypted profiles, mounted from image.
#   - Python + cryptography for AES-GCM decrypt + HMAC token gen.
#   - CDP remote-debugging exposed on 9222 for orchestration from
#     a controller (Playwright/CDP client) outside the container.
#
# Build:
#   docker build -t chronium-linux:v0.1.5 .
#
# Run one profile:
#   docker run -d \
#     -p 9222:9222 \
#     -e CHRONIUM_LICENSE_KEY=<hex64> \
#     -e ACCOUNT_LABEL=acc1 \
#     -e FINGERPRINT=linux-rtx3060 \
#     -e PROXY_URL=socks5://user:pass@host:port \
#     -v $(pwd)/data/acc1:/data/user-data-dir \
#     chronium-linux:v0.1.5
#
# The account label chip renders in the location bar as usual; the
# controller can drive the browser via CDP at ws://host:9222/devtools/browser/.

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# ---- Runtime deps ----
# Chromium runtime pull:
#   - nss / atk / cups / gtk3 / mesa / vulkan-loader — GUI stack
#   - fontconfig + noto/roboto/liberation — the fonts fontconfig hands
#     to the fingerprint's allowlist
#   - xvfb + xauth — virtual display
#   - dbus-x11 — DBus session (Chromium expects one)
#   - python3 + cryptography — decrypt helpers
#   - tini — proper PID 1 for signal handling
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      dbus-x11 \
      fontconfig \
      fonts-liberation \
      fonts-noto \
      fonts-noto-cjk \
      fonts-noto-color-emoji \
      fonts-roboto \
      libasound2 \
      libatk1.0-0 \
      libatk-bridge2.0-0 \
      libcairo2 \
      libcups2 \
      libdbus-1-3 \
      libdrm2 \
      libgbm1 \
      libgtk-3-0 \
      libnspr4 \
      libnss3 \
      libpango-1.0-0 \
      libx11-xcb1 \
      libxcomposite1 \
      libxdamage1 \
      libxfixes3 \
      libxkbcommon0 \
      libxrandr2 \
      libxshmfence1 \
      libxtst6 \
      procps \
      python3 \
      python3-cryptography \
      tini \
      wget \
      xauth \
      xvfb \
    && rm -rf /var/lib/apt/lists/*

# ---- Chronium binary ----
# The build script writes chronium-linux-vX.Y.Z.tar.gz into ./release/.
# The tarball layout mirrors chronium-build/scripts/make-package.ps1:
#   bin/         — chrome + shared libs + paks + Locales
#   config/      — encrypted profile JSONs + scripts
ARG CHRONIUM_VERSION=v0.1.5
COPY release/chronium-linux-${CHRONIUM_VERSION}.tar.gz /tmp/chronium.tar.gz
RUN mkdir -p /opt/chronium \
    && tar xzf /tmp/chronium.tar.gz -C /opt/chronium --strip-components=1 \
    && rm /tmp/chronium.tar.gz \
    && chmod +x /opt/chronium/bin/chrome

# ---- Entrypoint scripts ----
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# CDP port exposed for external Playwright / raw CDP orchestration.
EXPOSE 9222

# tini as PID 1 so Ctrl+C / docker stop cleanly kills Xvfb + chrome + python.
ENTRYPOINT ["/usr/bin/tini", "--", "/entrypoint.sh"]
