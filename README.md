# LeakOptimizator

A Windows desktop utility that detects your hardware, applies performance
tweaks, and shows a live in-game overlay — built with **C++20**, **ImGui**,
**Direct3D 11** and the **Win32 API**.

> Note: tweaks and service changes require **administrator** privileges, and
> every registry / service change can be backed up and restored from JSON.

## Features

- **Hardware detection** — CPU (registry), GPU (SetupAPI), RAM
  (GlobalMemoryStatusEx), OS version.
- **System tweaks**
  - Clear the Windows *standby list* (free memory) via `NtSetSystemInformation`.
  - Set a running process to **High priority** (`SetPriorityClass`).
  - Disable **Game Bar / Game DVR** (registry `GameDVR` / `GameBar`).
  - Disable **Nagle's algorithm** (`TCPNoDelay`, `TcpAckFrequency`) on every
    TCP/IP interface.
  - Disable background services: `WSearch`, `SysMain`, `DiagTrack`.
- **Overlay** — always-on-top, click-through layered window
  (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST`) showing FPS and CPU
  temperature. Rendered with GDI+ + `UpdateLayeredWindow`.
- **Metrics** — rolling FPS window; CPU temperature via **PDH** (thermal zone).
  GPU temperature is left as an integration point (LibreHardwareMonitor).
- **Themes** — color presets (`Neon Blue`, `Toxic Green`, `Blood Red`,
  `Rust Orange`) or a custom accent, applied to the UI and the overlay.
  Persisted to `theme.json`.
- **Backup / Restore** — snapshots every key the optimizer touches into
  `backup.json` and can restore it.

## Build (Windows)

Requirements: **CMake ≥ 3.16**, **Visual Studio 2022** (MSVC) or MinGW with a
Windows SDK (Win10 SDK) — the Direct3D 11 backend needs the Windows SDK.

```sh
cmake -S . -B build -A x64
cmake --build build --config Release
```

The `build/Release/LeakOptimizator.exe` binary is produced. Run it **as administrator**
to use the tweaks.

## Project layout

```
src/
  main.cpp      # entry point, window, D3D11 + ImGui bootstrap
  ui.cpp        # ImGui panels (hardware / tweaks / theme / backup / overlay)
  hardware.*    # CPU/GPU/RAM/OS detection
  optimizer.*   # WinAPI tweaks (memory, priority, registry, services)
  theme.*       # color presets + JSON persistence
  backup.*      # registry/service snapshot + restore (JSON)
  metrics.*     # background FPS/CPU-temp gathering (PDH)
  overlay.*     # layered click-through overlay (GDI+ / UpdateLayeredWindow)
third_party/    # vendored: ImGui, ImPlot, jsonxx
CMakeLists.txt
```

## Libraries

- **ImGui** / **ImPlot** — UI + graphs (vendored, MIT)
- **jsonxx** — JSON (vendored, MIT) for theme / backup files
- Windows SDK — D3D11, GDI+, PDH, SetupAPI, ADVAPI32

## Safety disclaimer

This tool edits real Windows settings. It ships its own backup/restore, but you
should only run it if you understand the effect of each tweak (disabling
services like SysMain affects how Windows caches). Test on a disposable VM or
machine first.

## License

MIT
