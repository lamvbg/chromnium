# Chronium

Custom Chromium fork with C++ patches for per-profile fingerprint
spoofing. Windows x64 only. Solo build pipeline.

```
chronium-build/
├── scripts/       build pipeline (setup-env, fetch, apply-patches, build, package, rebase)
├── patches/       *.patch series applied on top of chronium-base branch
├── config/        args.gn + example-profile.json
├── assets/        chronium.ico, version info
├── tools/         rcedit.exe (download manually)
└── release/       package output (gitignored)
```

The Chromium source itself lives **outside** this repo at `C:\cr\src`
(~50 GB). Only the patch series, scripts, and config files are tracked
here.

## Quick start (first build, ~1 day)

```
# 1. Verify environment (Visual Studio 2022, Win SDK, Python, depot_tools).
scripts\setup-env.bat

# 2. Fetch Chromium source + pin to STABLE_TAG (defined at top of fetch.bat).
#    ~1-2 hours, ~50 GB download.
scripts\fetch.bat

# 3. Apply patches\*.patch series onto chronium-base branch.
scripts\apply-patches.bat

# 4. Build chrome.exe with our args.gn. First clean build = 4-8 hours.
scripts\build.bat

# 5. Repack output under release\chronium\, rename to chronium.exe,
#    rebrand resources.
scripts\package.bat

# 6. Smoke test.
release\chronium\chronium.exe ^
    --user-data-dir=D:\profiles\test1 ^
    --fingerprint-profile=config\example-profile.json ^
    --remote-debugging-port=9222
```

## Incremental workflow (after first build)

When tweaking a single C++ file under `C:\cr\src`:

```
scripts\rebuild-patch.bat   # 1-10 min
scripts\package.bat
```

When committing your edits as a patch:

```
cd /d C:\cr\src
git add <files>
git commit -m "..."
git format-patch chronium-base..HEAD -o c:\Users\Admin\Desktop\Interlink\chronium-build\patches
```

## Updating to a new Chromium stable

```
# 1. Find new tag at https://chromiumdash.appspot.com/releases?platform=Windows
# 2. Run rebase:
scripts\rebase.bat 131.0.6778.85
# 3. Resolve any conflicts (expect ~30% on Blink, ~10% on chrome/).
# 4. Update STABLE_TAG in scripts\fetch.bat to the new version.
# 5. Rebuild.
scripts\build.bat
```

Budget 1 day per stable bump; 2-3 days when Blink refactors something
big (typically every ~3 months).

## Anti-detect patches

The full patch surface is documented in `patches\README.md` and in the
plan file at
`C:\Users\Admin\.claude\plans\b-n-bi-t-v-wayfern-graceful-kazoo.md`.

Per-profile fingerprint is loaded from a JSON file passed at launch via
`--fingerprint-profile=<path>`. See `config\example-profile.json` for
the schema.

## Verification targets

After build, test against (in order of difficulty):

1. `https://bot.sannysoft.com/` — all checks green
2. `https://browserleaks.com/` — section-by-section
3. `https://amiunique.org/fp` — reasonable diversity score
4. `https://abrahamjuliot.github.io/creepjs/` — target "trusted" 75%+
5. `https://pixelscan.net/` — cross-vector consistency
6. `https://www.browserscan.net/` — anti-detect score

## Out of scope (build separately later)

- Profile manager backend (REST API + SQLite)
- Desktop UI (profile list, create/edit, launch button)
- Automation API wrapper (Selenium/Playwright/Puppeteer)
- Installer (Inno Setup)
- Auto-update
