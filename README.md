# Wedpo — a cleaner, brighter web

**Wedpo** is an independent, privacy-first desktop browser: a **native Qt 6
shell** (frameless, tabs-in-titlebar, zero web-UI chrome) over a
**Chromium-grade rendering engine** (QtWebEngine), with a **Rust privacy
core** (`wedpo_core`) compiled to a native library and wired in over a small
C ABI.

> Simple. Fast. Yours.

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│  Qt 6 native shell (C++)                                   │
│  tab strip · omnibox · bookmarks · settings · dashboard    │
└──────────────┬─────────────────────────────┬───────────────┘
               │ QtWebEngine (sandboxed       │ C ABI
               │ multi-process Chromium)      ▼
               │                     ┌─────────────────────┐
               └────────────────────►│  wedpo_core (Rust)  │
                                     │ adblock engine      │
                                     │ cosmetic filtering  │
                                     │ tracker stripping   │
                                     │ download risk score │
                                     └─────────────────────┘
```

- **`core/`** — Rust crate: Brave-grade filter engine (EasyList/EasyPrivacy
  syntax via the `adblock` crate), two-phase cosmetic filtering, tracking
  parameter stripping, download risk scoring. All engine access runs on a
  dedicated worker thread (the 0.13 engine is single-threaded by design);
  the C ABI stays synchronous.
- **`app/`** — Qt 6 Widgets application. Every control is wired to real
  functionality: SQLite bookmarks/history, session restore with crash
  recovery, permission prompts with memory, live privacy counters,
  filter-list updater, find-in-page, print, save page, per-site zoom,
  DevTools, private windows.
- **Rendering** is QtWebEngine (Chromium sandbox + multi-process) — the same
  class of decision every real independent browser makes (Brave, Edge,
  Vivaldi). The product differentiation is the shell, the privacy stack, and
  the resource discipline.

## Features

**Browsing** — tabs (pin/mute/duplicate/close-others/reopen-closed, drag
reorder), omnibox with security indicator + live blocked-count shield,
bookmark star and bookmarks bar, history/bookmarks/downloads managers,
find-in-page with match counter, per-site zoom, full screen, print,
save page (MHTML), DevTools, private windows, session restore with crash
recovery, keyboard shortcuts.

**Privacy & security** — ad + tracker blocking (EasyList, EasyPrivacy,
Peter Lowe's, AdGuard, Fanboy annoyances + custom lists), two-phase
cosmetic element hiding, URL tracking-parameter stripping (utm_*, fbclid,
gclid, …), HTTPS-only upgrades (local/ private ranges excluded), Global
Privacy Control + DNT headers, cross-site referer trimming, cookie modes
(allow / block third-party / block all), fingerprint noise (canvas, audio,
WebGL vendor; strict mode caps hardware reporting), WebRTC IP-leak policy,
popup blocking, autoplay blocking, per-site permission prompts with
remembered decisions, download risk scoring with human-readable reasons.

## Building

CI builds Windows and Linux artifacts on every push to `main`
(`.github/workflows/`). To build locally:

```bash
# 1. Rust privacy core
cargo build --release --manifest-path core/Cargo.toml

# 2. Qt app (Qt 6.8+, CMake 3.21+, Ninja)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRUST_CORE_DIR="$PWD/core/target"
cmake --build build --parallel

# 3. Deployable bundle
cmake --build build --target deploy       # dist/ (windeployqt / package script)
```

Tests: `cargo test --manifest-path core/Cargo.toml` (engine unit tests) and
`python core/tests/abi_smoke.py` against the built cdylib (C ABI contract).

## License

GPL-3.0. Wedpo is original code; no proprietary assets or code are copied
from any browser vendor.
