#pragma once
// Main application window / D3D11 / ImGui setup and UI tabs.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <deque>
#include <string>
#include <vector>

#include "hardware.h"
#include "metrics.h"
#include "overlay.h"
#include "theme.h"

// Process-wide "active" theme shared by the UI and the overlay.
inline theme::Theme gTheme;

namespace app {

// All application-level object handles, initialized once in WinMain.
struct App {
    hw::HardwareInfo* hardware = nullptr;
    ovl::Overlay*     overlay  = nullptr;
    met::Metrics*     metrics  = nullptr;

    bool  tweakGameDvr  = true;
    bool  tweakGameBar  = true;
    bool  tweakNagle    = true;
    bool  tweakWSearch  = true;
    bool  tweakSysMain  = false;   // off by default (heavy-handed)
    bool  tweakDiagTrack= true;

    char procName[64] = "";             // e.g. "rustclient.exe"
    DWORD procPid = 0;
    bool    overlayOn = false;
    float   overlayAlpha = 0.85f;

    std::string themePath;
    std::string backupPath;
    std::string exeDir;
};

// Draws the entire ImGui UI for this frame. Returns immediately.
void drawUi(App& a);

// Pushes a message line to the on-screen log.
void Log(const std::string& line);

} // namespace app