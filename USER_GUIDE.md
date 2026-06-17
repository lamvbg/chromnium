# Chronium — Hướng dẫn sử dụng

Chronium là Chromium fork tự build cho anti-detect browsing. Có 2 cách dùng:

- **Cách 1 (khuyên dùng)**: Cài app **Antidetect** — UI quản lý profile, tự động hoá launch, geo override, encryption. Không cần gõ command.
- **Cách 2 (advanced/dev)**: Launch `chrome.exe` trực tiếp bằng PowerShell với token tự gen. Dành cho debug patch C++ hoặc người muốn integrate tự.

---

## Cách 1 — Cài app Antidetect (recommended)

### Bước 1: Tải installer

Vào https://github.com/lamvbg/antidetect/releases → tải `Antidetect_v0.x.x_setup.exe` mới nhất.

### Bước 2: Cài

Double-click installer → Next, Next, Install. Không cần admin (per-user install).

App cài tại: `%LOCALAPPDATA%\Programs\Antidetect\`

### Bước 3: Mở app + tải Chronium engine

Mở app lần đầu → vào **⚙ Cài đặt** → **Nâng cao** → **Chronium vunknown** → click **Check updates** → button **"Update to v0.1.x"** xuất hiện → click.

App tự download zip 1.1 GB từ GitHub release → extract vào `%LOCALAPPDATA%\Antidetect\binaries\chronium\`. Tiến trình hiển thị bar (resolving → downloading 0–100% → extracting → done).

### Bước 4: Tạo profile + launch

1. Click **+ Tạo profile mới** → chọn **Chronium**
2. Đặt tên, chọn Windows (mac/linux sẽ có sau)
3. Tuỳ chọn **Proxy** → SOCKS5 / HTTP / HTTPS (auto detect geo qua proxy)
4. Click **Tạo**
5. Trong danh sách → click **Start** trên profile vừa tạo
6. Browser chronium mở. Cookie + login state lưu riêng từng profile.

### Bước 5: Kiểm tra anti-detect

Trong browser vừa mở, vào:
- https://iphey.com → 5 badges xanh + **Trustworthy**
- https://browserleaks.com/canvas → canvas hash khác nhau giữa 2 profile, nhưng cùng profile thì reload luôn ra cùng hash
- https://abrahamjuliot.github.io/creepjs → target trust ≥75%

### Quản lý profile

| Action | UI |
|---|---|
| Mở lại profile | Click profile → Start |
| Refresh fingerprint | Profile detail → tab Vân tay → **Làm mới vân tay** |
| Nhân bản | Profile detail → **Nhân bản** (góc trên) |
| Xoá | Profile detail → **Xoá profile** |
| Đổi proxy | Profile detail → tab Mạng |
| Copy cookies giữa profile | Profile list → menu ⋯ → Cookie copy |

### Cập nhật engine sau

Khi có version mới (v0.1.4, v0.2.0…) → Settings → Check updates → 1-click upgrade. App tự download + atomic swap binary.

---

## Cách 2 — Launch chrome.exe trực tiếp (advanced)

Dành cho:
- Dev đang debug patch C++ chronium
- Test fingerprint JSON tay
- Integrate chronium vào script automation tự viết

### Yêu cầu

Bạn cần có:
1. **Chronium binary**: tải từ https://github.com/lamvbg/chromnium/releases/download/v0.1.3/chronium-v0.1.3.zip → giải nén
2. **License secret** (`.profile_key`): chỉ có nếu bạn là dev đã chạy `gen-license-secret.py`. **Không có file này → không launch được.**
3. **Python 3** + script `gen-debug-token.py` từ repo `chromnium`
4. **Fingerprint JSON plaintext**: hoặc có sẵn `example-profile.json` trong chronium-build, hoặc decrypt một file `.json.enc` từ release zip

### ⚠️ Đặt biến PowerShell

**KHÔNG dùng `$args`** — đó là biến reserved của PowerShell, assign vào sẽ silent fail. Dùng `$tokenArgs` hoặc tên khác.

### Bước 1: Decrypt một fingerprint JSON sang file tạm

```powershell
# Cài cryptography (1 lần)
pip install cryptography

# Decrypt win-rtx2000 → temp file, in path ra để capture
$CHRONIUM = 'C:\Users\Admin\Desktop\Interlink\chronium-build'
$plainFile = python $CHRONIUM\scripts\decrypt-profile.py win-rtx2000 --to-temp
Write-Host "Plaintext: $plainFile"
```

Output mẫu: `Plaintext: C:\Users\Admin\AppData\Local\Temp\chronium-win-rtx2000-xyz.json` (9 KB JSON đầy đủ).

### Bước 2: Generate license token

Token bind vào PID của terminal hiện tại — phải gen + launch chrome trong **CÙNG** PowerShell window.

```powershell
$tokenArgs = python $CHRONIUM\scripts\gen-debug-token.py --ppid $PID
Write-Host "Token: $tokenArgs"
# Output: --license-ts=... --license-nonce=... --license-token=...
```

### Bước 3: Launch chrome.exe

```powershell
$udd = "$env:TEMP\chronium-test-$(Get-Random)"
New-Item -ItemType Directory -Force -Path $udd | Out-Null

$chrome = 'C:\work\antidetect\vendor\chronium\bin\chrome.exe'   # hoặc path zip giải nén của bạn

$launchArgs = @(
    "--user-data-dir=$udd",
    '--no-first-run',
    '--no-default-browser-check',
    '--use-angle=d3d11',
    "--fingerprint-profile=$plainFile"
) + ($tokenArgs -split ' ') + @('https://iphey.com')

& $chrome @launchArgs
```

→ Browser mở vào iphey.com. Nếu chrome.exe tắt ngay → check 3 thứ:

1. **`$tokenArgs` rỗng?** → vẫn dùng `$args` (đổi tên!)
2. **`$plainFile` không tồn tại?** → check `Get-Item $plainFile`
3. **Different terminal?** → mở chrome ở terminal khác sẽ fail vì PPID khác

### Bước 4: Verify

Trong browser vừa mở:
1. URL bar gõ `chrome://version` → tìm dòng **"Command Line"** → phải thấy `--license-token=...` và `--fingerprint-profile=...`
2. Vào iphey.com → check Trustworthy

### Lưu ý quan trọng

- **Token expire sau 60 giây** kể từ lúc gen. Nếu pause lâu giữa gen + launch → token timeout → chrome silent exit. Re-gen.
- **Token bind PID**: nếu mở chrome ở terminal khác hoặc qua `Start-Process` mà không tracking — PPID không khớp, chrome silent exit.
- **Multiple launches**: mỗi lần launch chrome cần 1 token tươi (recommended) hoặc 1 token có thể dùng lại trong 60s.
- **Không có `.profile_key`?** Bạn không phải dev gốc → không tự launch được. Cài app Antidetect (Cách 1) thay.

### Wrapper script tiện cho dev

Lưu thành `launch-chrome.ps1` (cần `$CHRONIUM` + `$BIN` chỉnh path tới máy bạn):

```powershell
# launch-chrome.ps1 - one-shot chronium launch with fresh token + decrypted profile
param(
    [string]$Fingerprint = 'win-rtx2000',
    [string]$Url = 'https://iphey.com',
    [string]$Udd = "$env:TEMP\chronium-dev-$(Get-Random)"
)
$CHRONIUM = 'C:\Users\Admin\Desktop\Interlink\chronium-build'
$BIN      = 'C:\work\antidetect\vendor\chronium\bin'

# Decrypt fingerprint to temp plaintext
$plain = python "$CHRONIUM\scripts\decrypt-profile.py" $Fingerprint --to-temp
if (-not $plain) { Write-Error "decrypt failed"; exit 1 }

# Gen license token bound to current shell
$tokenArgs = python "$CHRONIUM\scripts\gen-debug-token.py" --ppid $PID
if (-not $tokenArgs) { Write-Error "token gen failed"; exit 1 }

# Launch
New-Item -ItemType Directory -Force -Path $Udd | Out-Null
$args = @(
    "--user-data-dir=$Udd",
    '--no-first-run',
    '--no-default-browser-check',
    '--use-angle=d3d11',
    "--fingerprint-profile=$plain"
) + ($tokenArgs -split ' ') + @($Url)

& "$BIN\chrome.exe" @args
```

Dùng:
```powershell
.\launch-chrome.ps1                                   # default: win-rtx2000 -> iphey.com
.\launch-chrome.ps1 -Fingerprint win-amd-000015e7     # khác GPU
.\launch-chrome.ps1 -Url https://creepjs.com          # khác target
```

---

## Troubleshooting

### Chronium silent exit khi launch

Nguyên nhân thường gặp:
| Symptom | Fix |
|---|---|
| Chrome window flash rồi tắt ngay | Thiếu `--license-token` hoặc HMAC sai |
| Cũng tắt khi có token | Token ts >60s (timeout) → re-gen |
| Vẫn tắt | PPID mismatch — gen + launch không cùng terminal |
| Standalone test bin/ (không qua app) | Đúng behavior! Cần token mới launch được |

### `WinError 14001` (side-by-side configuration)

Bin/ thiếu DLL (libEGL, libGLESv2, d3dcompiler_47, vcruntime140…). Tải lại zip release v0.1.3+, giải nén ĐẦY ĐỦ (đừng chỉ copy `chrome.exe`).

### iphey FAIL ở Location

- Profile cũ (pre-v0.1.2): timezone vẫn theo pl-PL từ ShardX bundle. Click **Làm mới vân tay** trong app.
- Proxy không reachable → backend không detect được country → fallback hash random pick. Thay proxy khác.

### iphey FAIL ở Hardware

- Profile có GPU renderer khác vendor host (vd profile NVIDIA, máy Intel iGPU). v0.1.2+ có GPU-aware picker tự fix khi tạo profile mới, **nhưng profile cũ giữ pin GPU cũ**.
- Fix: **Làm mới vân tay** trong app, hoặc xoá profile + tạo lại.

### "GitHub API HTTP 404" khi Check updates

Mạng không ra được api.github.com (firewall / VPN). Tải zip release tay → giải nén vào `%LOCALAPPDATA%\Antidetect\binaries\chronium\`.

### Pixelscan stuck "Collecting Data"

Lỗi từ phía pixelscan (JS của họ crash với chronium). Không fix được bên mình. ShardX, Multilogin cũng vậy.

---

## Cấu trúc folder sau khi cài

```
%LOCALAPPDATA%\Antidetect\           ← app data
├── antidetect.sqlite                ← profile registry
├── binaries\
│   └── chronium\
│       ├── VERSION                  ← v0.1.3
│       ├── bin\                     ← chrome.exe + 586 DLLs (~3.7 GB)
│       └── config\
│           └── profiles\            ← 170 .json.enc (encrypted)
└── profiles\                        ← per-account user-data-dir
    ├── acc1\
    └── acc2\

%LOCALAPPDATA%\Programs\Antidetect\  ← installed app
├── antidetect.exe                   ← Tauri shell (chứa SECRET baked)
└── antidetect-sidecar.exe           ← Python backend (PyInstaller)
```

### Backup cookies + login state

Sao chép thư mục `%LOCALAPPDATA%\Antidetect\profiles\<acc-name>\` sang máy khác → cookies, history, login giữ nguyên.

### Uninstall sạch

1. Control Panel → Programs → Uninstall **Antidetect**
2. Xoá `%LOCALAPPDATA%\Antidetect\` (toàn bộ data accounts)

---

## So sánh 2 cách

| | Cách 1 (App) | Cách 2 (Manual) |
|---|---|---|
| Cần install | Antidetect installer | Python, chronium zip |
| Cần license `.profile_key` | Không (baked sẵn) | **Có** |
| Auto geo từ IP | ✅ ip-api.com qua proxy | ❌ tự override timezone trong JSON |
| Encrypted profile | ✅ tự decrypt | ❌ tự lo |
| GPU-aware fingerprint pick | ✅ | ❌ tự chọn |
| Cookie persistence | ✅ per-account | ✅ per user-data-dir |
| Manage 50 accounts | ✅ UI | Phải tự script |
| **Use case** | End-user, mass farming | Dev debug, automation custom |

---

## Liên hệ / Báo bug

- App bugs: https://github.com/lamvbg/antidetect/issues
- Chronium engine bugs: https://github.com/lamvbg/chromnium/issues
- Patches: chronium-build/patches/0001-0020
