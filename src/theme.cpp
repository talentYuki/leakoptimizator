#include "theme.h"

#include <jsonxx/json.hpp>
#include <fstream>
#include <utility>

namespace theme {

Theme presetOf(Preset p) {
    Theme t;
    t.preset = p;
    switch (p) {
        case Preset::NeonBlue:  t.accent = {0.20f,0.65f,1.00f,1}; t.accent2 = {0.35f,0.30f,0.95f,1}; break;
        case Preset::ToxicGreen:t.accent = {0.20f,0.95f,0.40f,1}; t.accent2 = {0.10f,0.55f,0.20f,1}; break;
        case Preset::BloodRed:  t.accent = {0.95f,0.20f,0.10f,1}; t.accent2 = {0.55f,0.05f,0.15f,1}; break;
        case Preset::RustOrange:t.accent = {0.95f,0.45f,0.12f,1}; t.accent2 = {0.60f,0.20f,0.05f,1}; break;
        default:;
    }
    return t;
}

namespace {
ImVec4 mix(const ImVec4& a, const ImVec4& b, float k) {
    return { a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k };
}
}

void apply(const Theme& t) {
    ImGuiStyle& s = ImGui::GetStyle();
    auto& c = s.Colors;
    c[ImGuiCol_Text]                 = { t.text.x, t.text.y, t.text.z, t.text.w };
    c[ImGuiCol_TextDisabled]         = mix(t.text, t.bg, 0.65f);
    c[ImGuiCol_WindowBg]             = t.bg;
    c[ImGuiCol_ChildBg]              = { t.bg.x * 0.85f, t.bg.y * 1.05f, t.bg.z * 1.10f, 1.0f };
    c[ImGuiCol_PopupBg]              = { t.bg.x * 0.9f, t.bg.y * 0.9f, t.bg.z, 0.98f };
    c[ImGuiCol_Border]               = mix(t.text, t.bg, 0.78f);
    c[ImGuiCol_FrameBg]              = mix(t.bg, t.text, 0.08f);
    c[ImGuiCol_FrameBgHovered]       = mix(t.bg, t.accent, 0.18f);
    c[ImGuiCol_FrameBgActive]        = mix(t.bg, t.accent, 0.30f);
    c[ImGuiCol_TitleBg]              = mix(t.bg, t.accent2, 0.14f);
    c[ImGuiCol_TitleBgActive]        = mix(t.bg, t.accent2, 0.28f);
    c[ImGuiCol_Button]               = mix(t.bg, t.accent, 0.20f);
    c[ImGuiCol_ButtonHovered]        = mix(t.bg, t.accent, 0.42f);
    c[ImGuiCol_ButtonActive]         = t.accent2;
    c[ImGuiCol_Header]               = mix(t.bg, t.accent, 0.15f);
    c[ImGuiCol_HeaderHovered]        = mix(t.bg, t.accent, 0.35f);
    c[ImGuiCol_HeaderActive]         = mix(t.bg, t.accent, 0.50f);
    c[ImGuiCol_SliderGrab]           = t.accent;
    c[ImGuiCol_SliderGrabActive]     = t.accent2;
    c[ImGuiCol_CheckMark]            = t.accent;
    c[ImGuiCol_PlotLines]            = t.accent;
    c[ImGuiCol_PlotHistogram]        = t.accent;
    c[ImGuiCol_PlotHistogramHovered] = t.accent2;
    c[ImGuiCol_Tab]                  = mix(t.bg, t.accent, 0.12f);
    c[ImGuiCol_TabHovered]           = mix(t.bg, t.accent, 0.35f);
    c[ImGuiCol_TabActive]            = mix(t.bg, t.accent, 0.45f);
    c[ImGuiCol_TextSelectedBg]       = { t.accent.x, t.accent.y, t.accent.z, 0.32f };
    c[ImGuiCol_ScrollbarGrab]        = mix(t.bg, t.accent, 0.40f);
    // polish roundness/spacing
    s.WindowRounding = 8.f;
    s.FrameRounding = 6.f;
    s.TabRounding = 5.f;
    s.WindowBorderSize = 1.f;
    s.FramePadding = { 8, 5 };
}

void applyPreset(Preset p) {
    Theme t = presetOf(p);
    apply(t);
}

const char* displayName(Preset p) {
    switch (p) {
        case Preset::NeonBlue:   return "Neon Blue";
        case Preset::ToxicGreen: return "Toxic Green";
        case Preset::BloodRed:   return "Blood Red";
        case Preset::RustOrange: return "Rust Orange";
        default:                 return "Custom";
    }
}

// ---------------- JSON persistence ----------------
namespace {
jsonxx::Value vec4ToJson(const ImVec4& v) {
    jsonxx::Array a;
    a.emplace_back(static_cast<double>(v.x));
    a.emplace_back(static_cast<double>(v.y));
    a.emplace_back(static_cast<double>(v.z));
    a.emplace_back(static_cast<double>(v.w));
    return jsonxx::Value(std::move(a));
}
ImVec4 vec4FromJson(const jsonxx::Value& v) {
    ImVec4 out{0,0,0,1};
    if (v.type() == jsonxx::Type::Array) {
        const auto& a = v.asArray();
        if (a.size() > 0) out.x = (float)a[0].asNumber();
        if (a.size() > 1) out.y = (float)a[1].asNumber();
        if (a.size() > 2) out.z = (float)a[2].asNumber();
        if (a.size() > 3) out.w = (float)a[3].asNumber();
    }
    return out;
}
}

bool saveToFile(const std::string& path, const Theme& t) {
    jsonxx::Object o;
    o.emplace("preset", jsonxx::Value((int)t.preset));
    o.emplace("accent", vec4ToJson(t.accent));
    o.emplace("accent2", vec4ToJson(t.accent2));
    o.emplace("bg", vec4ToJson(t.bg));
    o.emplace("text", vec4ToJson(t.text));
    o.emplace("dark", jsonxx::Value(t.dark));
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << jsonxx::stringify(jsonxx::Value(std::move(o)), true);
    return true;
}

bool loadFromFile(const std::string& path, Theme& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    try {
        jsonxx::Value v = jsonxx::parse(text);
        const jsonxx::Object& o = v.asObject();
        out.preset = (Preset)o.at("preset").asInt();
        out.accent  = vec4FromJson(o.at("accent"));
        out.accent2 = vec4FromJson(o.at("accent2"));
        out.bg      = vec4FromJson(o.at("bg"));
        out.text    = vec4FromJson(o.at("text"));
        if (o.has("dark")) out.dark = o.at("dark").asBool();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace theme