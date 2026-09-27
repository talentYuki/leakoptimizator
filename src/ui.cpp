#include "main.h"
#include "optimizer.h"
#include "backup.h"
#include "theme.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>

namespace app {

namespace {
std::deque<std::string> g_log;
std::vector<float> g_cpuHist;   // ImPlot history
std::vector<float> g_fpsHist;
std::string g_status;

void logLine(const std::string& s) {
    g_log.push_front(s);
    if (g_log.size() > 250) g_log.pop_back();
}

std::string w2sUnused(const std::wstring&) { return {}; }
std::wstring s2w(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

// ---- tabs ----
void tabHardware(App& a) {
    if (ImGui::CollapsingHeader("Detected hardware", ImGuiTreeNodeFlags_DefaultOpen)) {
        const hw::HardwareInfo& h = *a.hardware;
        ImGui::Text("CPU   : %s", h.cpuName.c_str());
        ImGui::Text("Cores : %u physical / %u logical", h.cores, h.logicalProcessors);
        ImGui::Text("RAM   : %.2f GB", double(h.totalPhysMB) / 1024.0);
        for (const auto& g : h.gpus) ImGui::Text("GPU   : %s", g.name.c_str());
        ImGui::Text("OS    : %s", h.osName.c_str());
    }
    if (ImGui::CollapsingHeader("Live metrics", ImGuiTreeNodeFlags_DefaultOpen)) {
        double cpu = a.metrics->cpuTempC(), gpu = a.metrics->gpuTempC();
        ImGui::Text("App FPS   : %.0f", a.metrics->fps());
        if (!std::isnan(cpu)) ImGui::Text("CPU temp  : %.1f C", cpu);
        else                  ImGui::Text("CPU temp  : n/a");
        if (!std::isnan(gpu)) ImGui::Text("GPU temp  : %.1f C", gpu);
        else                  ImGui::Text("GPU temp  : n/a");
    }
}

void tabTweaks(App& a) {
    ImGui::Checkbox("Disable Game Bar / Game DVR (registry)", &a.tweakGameDvr);
    ImGui::Checkbox("Disable Nagle's algorithm on all TCP/IP adapters", &a.tweakNagle);
    ImGui::Separator();
    ImGui::Text("Background services (requires admin):");
    ImGui::Checkbox("Disable Windows Search (WSearch)", &a.tweakWSearch);
    ImGui::Checkbox("Disable SysMain (SuperFetch)", &a.tweakSysMain);
    ImGui::Checkbox("Disable DiagTrack (telemetry)", &a.tweakDiagTrack);
    ImGui::Separator();

    if (ImGui::Button("Apply selected tweaks")) {
        if (a.tweakGameDvr) logLine(opt::setGameBarOff().message);
        if (a.tweakNagle)   logLine(opt::setNagleOff().message);
        if (a.tweakWSearch)  logLine(opt::setService(L"WSearch", true).message);
        if (a.tweakSysMain)  logLine(opt::setService(L"SysMain", true).message);
        if (a.tweakDiagTrack)logLine(opt::setService(L"DiagTrack", true).message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-enable services")) {
        logLine(opt::setService(L"WSearch", false).message);
        logLine(opt::setService(L"DiagTrack", false).message);
    }
    ImGui::Separator();

    if (ImGui::Button("Clear Standby Memory"))
        logLine(opt::clearStandbyMemory().message);

    ImGui::Separator();
    ImGui::Text("Boost process priority:");
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("##proc", a.procName, sizeof(a.procName));
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
        auto pid = opt::findPidByName(s2w(a.procName));
        a.procPid = pid ? *pid : 0;
        logLine(pid ? ("found PID " + std::to_string(*pid)) : "process not found");
    }
    ImGui::SameLine();
    if (ImGui::Button("Set High Priority") && a.procPid)
        logLine(opt::setProcessHighPriority(a.procPid).message);
    if (a.procPid) ImGui::Text("target PID: %lu", a.procPid);

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1), "Some tweaks require running as administrator.");
}

void tabTheme(App& a) {
    static const theme::Preset presets[] = {
        theme::Preset::NeonBlue, theme::Preset::ToxicGreen,
        theme::Preset::BloodRed, theme::Preset::RustOrange };
    for (auto p : presets) {
        ImVec4 c = theme::presetOf(p).accent;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x, c.y, c.z, 0.6f));
        if (ImGui::Button(theme::displayName(p))) {
            gTheme = theme::presetOf(p);
            theme::apply(gTheme);
            logLine("theme -> " + std::string(theme::displayName(p)));
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    bool changed = false;
    changed |= ImGui::ColorEdit4("Accent", (float*)&gTheme.accent);
    changed |= ImGui::ColorEdit4("Accent 2", (float*)&gTheme.accent2);
    changed |= ImGui::ColorEdit4("Background", (float*)&gTheme.bg);
    changed |= ImGui::ColorEdit4("Accent alpha (overlay)", &gTheme.accent.w);
    if (changed) theme::apply(gTheme);

    if (ImGui::Button("Save theme")) {
        bool ok = theme::saveToFile(a.themePath, gTheme);
        logLine(ok ? ("theme saved -> " + a.themePath) : "failed to save theme");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load theme")) {
        bool ok = theme::loadFromFile(a.themePath, gTheme);
        if (ok) theme::apply(gTheme);
        logLine(ok ? "theme loaded" : "no saved theme");
    }
}

void tabBackup(App& a) {
    ImGui::TextWrapped("Capture the current values of every key the optimizer touches "
                       "(GameDVR, GameBar, Nagle interfaces, services) into a JSON file, "
                       "so you can restore them exactly later.");
    if (ImGui::Button("Save backup")) {
        g_status = bk::snapshotToFile(a.backupPath);
        logLine(g_status);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore")) {
        int n = bk::restoreFromFile(a.backupPath);
        logLine(n >= 0 ? ("restored " + std::to_string(n) + " item(s)") : "restore failed / no backup");
    }
    if (!g_status.empty()) ImGui::TextWrapped("last: %s", g_status.c_str());
}

void tabOverlay(App& a) {
    if (ImGui::Checkbox("Enable overlay", &a.overlayOn)) {
        if (a.overlayOn) a.overlay->show(); else a.overlay->hide();
    }
    ImGui::SliderFloat("Opacity", &a.overlayAlpha, 0.2f, 1.0f, "%.2f");
    ImGui::TextWrapped("Always-on-top, click-through window above your game; "
                       "shows FPS and CPU temperature. GPU temp uses PDH where available.");
}
} // namespace

void Log(const std::string& line) { logLine(line); }

void drawUi(App& a) {
    // history buffers
    g_cpuHist.push_back((float)(std::isnan(a.metrics->cpuTempC()) ? 0 : a.metrics->cpuTempC()));
    g_fpsHist.push_back((float)a.metrics->fps());
    if (g_cpuHist.size() > 120) g_cpuHist.erase(g_cpuHist.begin());
    if (g_fpsHist.size() > 120) g_fpsHist.erase(g_fpsHist.begin());

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Universal Game Optimizer", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Hardware"))    { tabHardware(a); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Tweaks"))      { tabTweaks(a);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Theme"))       { tabTheme(a);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Backup"))      { tabBackup(a);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Overlay"))     { tabOverlay(a);  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    // temperature graph
    if (ImPlot::BeginPlot("CPU temperature", ImVec2(-1, 160))) {
        ImPlot::PlotLine("CPU C", g_cpuHist.data(), (int)g_cpuHist.size());
        ImPlot::EndPlot();
    }
    ImGui::End();

    // log window
    ImGui::SetNextWindowPos(ImVec2(730, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::BeginChild("##scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto it = g_log.rbegin(); it != g_log.rend(); ++it)
        ImGui::TextUnformatted(it->c_str());
    ImGui::EndChild();
    ImGui::End();
}

} // namespace app