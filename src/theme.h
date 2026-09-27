#pragma once
// Color themes: presets + custom accent, applied to ImGui style and the overlay.
// Saved to / loaded from a JSON file.

#include <imgui.h>
#include <string>

namespace theme {

enum class Preset { NeonBlue, ToxicGreen, BloodRed, RustOrange, Custom };

struct Theme {
    Preset preset = Preset::Custom;
    ImVec4 accent{ 0.35f, 0.55f, 1.00f, 1.00f };   // primary
    ImVec4 accent2{ 0.10f, 0.75f, 0.65f, 1.00f };  // secondary / gradient end
    ImVec4 bg{ 0.055f, 0.065f, 0.095f, 1.00f };     // window background
    ImVec4 text{ 0.92f, 0.93f, 0.96f, 1.00f };
    bool  dark = true;
};

Theme presetOf(Preset p);
void  apply(const Theme& t);            // writes ImGui style colors
void  applyPreset(Preset p);            // shortcut: apply(current preset)

bool  saveToFile(const std::string& path, const Theme& t);
bool  loadFromFile(const std::string& path, Theme& out);

const char* displayName(Preset p);

} // namespace theme