#include "backup.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsvc.h>
#include <jsonxx/json.hpp>

#include <fstream>
#include <string>
#include <vector>

namespace bk {
namespace {

// ---------------------------------------------------------------------------
// Registry value read/write as raw bytes (keeps type fidelity).
// ---------------------------------------------------------------------------
struct RegItem {
    std::string  root;    // "HKCU" | "HKLM"
    std::wstring path;
    std::wstring name;
    DWORD type = REG_BINARY;
    std::vector<unsigned char> data;
};

HKEY rootKey(const std::string& which) {
    return which == "HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

bool readRegItem(RegItem& it) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(rootKey(it.root), it.path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    bool ok = RegQueryValueExW(key, it.name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    if (ok && size > 0) {
        it.data.assign(size, 0);
        ok = RegQueryValueExW(key, it.name.c_str(), nullptr, &type, it.data.data(), &size) == ERROR_SUCCESS
             || ok; // second query already filled; keep ok true if existed
        it.data.resize(size);
    }
    it.type = type;
    RegCloseKey(key);
    return true;
}

bool writeRegItem(const RegItem& it) {
    HKEY key = nullptr;
    DWORD disp = 0;
    if (RegCreateKeyExW(rootKey(it.root), it.path.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, &disp) != ERROR_SUCCESS)
        return false;
    LONG st = RegSetValueExW(key, it.name.c_str(), 0, it.type,
                             it.data.empty() ? nullptr : it.data.data(),
                             (DWORD)it.data.size());
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// jsonxx helpers
// ---------------------------------------------------------------------------
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

jsonxx::Value bytesToJson(const std::vector<unsigned char>& d) {
    jsonxx::Array a;
    for (unsigned char b : d) a.emplace_back(static_cast<int>(b));
    return jsonxx::Value(std::move(a));
}

std::vector<unsigned char> bytesFromJson(const jsonxx::Value& v) {
    std::vector<unsigned char> out;
    if (v.type() == jsonxx::Type::Array)
        for (const auto& e : v.asArray()) out.push_back(static_cast<unsigned char>(e.asInt()));
    return out;
}

// ---------------------------------------------------------------------------
// The list of everything the optimizer touches.
// ---------------------------------------------------------------------------
struct Target {
    std::string  root;      // HKEY
    std::wstring path;
    std::wstring name;      // registry value name, or a service name when isService
    bool         isService = false;
};

std::vector<Target> collectTargets() {
    std::vector<Target> out;
    const std::vector<std::pair<std::string, std::wstring>> roots = {
        { "HKCU", L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR" },
        { "HKCU", L"System\\GameConfigStore" },
        { "HKCU", L"Software\\Microsoft\\GameBar" },
    };
    const wchar_t* names[] = { L"AppCaptureEnabled", L"GameDVR_Enabled", L"GameDVR_FSEBehaviorMode",
                               L"AllowAutoGameMode", L"AutoGameModeEnabled", L"UseNexusForGameBarEnabled" };
    for (const auto& [root, p] : roots)
        for (const wchar_t* n : names)
            out.push_back({ root, p, n, false });

    // Nagle keys across every TCP/IP interface adapter.
    const std::wstring ifaceTop = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ifaceTop.c_str(), 0, KEY_READ, &root) == ERROR_SUCCESS) {
        for (DWORD i = 0;; ++i) {
            wchar_t sub[256]{}; DWORD n = 256;
            if (RegEnumKeyExW(root, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring p = ifaceTop + L"\\" + sub;
            out.push_back({ "HKLM", p, L"TcpAckFrequency", false });
            out.push_back({ "HKLM", p, L"TCPNoDelay", false });
        }
        RegCloseKey(root);
    }

    for (const wchar_t* s : { L"WSearch", L"SysMain", L"DiagTrack" })
        out.push_back({ "svc", L"", s, true });
    return out;
}

} // namespace

std::string snapshotToFile(const std::string& path) {
    jsonxx::Array items;
    for (const Target& t : collectTargets()) {
        jsonxx::Object o;
        if (t.isService) {
            o.emplace("kind", jsonxx::Value("service"));
            o.emplace("name", jsonxx::Value(utf8(t.name)));
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (scm) {
                SC_HANDLE h = OpenServiceW(scm, t.name.c_str(), SERVICE_QUERY_CONFIG);
                if (h) {
                    DWORD bytesNeeded = 0;
                    QueryServiceConfigW(h, nullptr, 0, &bytesNeeded);
                    std::vector<BYTE> buf(bytesNeeded + 16);
                    if (QueryServiceConfigW(h, (LPQUERY_SERVICE_CONFIGW)buf.data(), (DWORD)buf.size(), &bytesNeeded)) {
                        auto cfg = (LPQUERY_SERVICE_CONFIGW)buf.data();
                        o.emplace("startType", jsonxx::Value(static_cast<int>(cfg->dwStartType)));
                        items.emplace_back(jsonxx::Value(std::move(o)));
                    }
                    CloseServiceHandle(h);
                }
                CloseServiceHandle(scm);
            }
        } else {
            RegItem it{ t.root, t.path, t.name };
            if (readRegItem(it)) {
                o.emplace("kind", jsonxx::Value("reg"));
                o.emplace("root", jsonxx::Value(it.root));
                o.emplace("path", jsonxx::Value(utf8(it.path)));
                o.emplace("name", jsonxx::Value(utf8(it.name)));
                o.emplace("type", jsonxx::Value(static_cast<int>(it.type)));
                o.emplace("data", bytesToJson(it.data));
                items.emplace_back(jsonxx::Value(std::move(o)));
            }
        }
    }

    jsonxx::Object rootVal;
    rootVal.emplace("items", jsonxx::Value(std::move(items)));
    std::ofstream f(path, std::ios::binary);
    if (!f) return "cannot open " + path;
    f << jsonxx::stringify(jsonxx::Value(std::move(rootVal)), true);
    return "backup written: " + path;
}

int restoreFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return -1;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    jsonxx::Value rootVal;
    try { rootVal = jsonxx::parse(text); } catch (...) { return -1; }
    if (rootVal.type() != jsonxx::Type::Object || !rootVal.has("items")) return -1;

    int restored = 0;
    for (const auto& item : rootVal.at("items").asArray()) {
        if (item.type() != jsonxx::Type::Object) continue;
        const jsonxx::Object& o = item.asObject();
        std::string kind = (o.find("kind") != o.end()) ? o.at("kind").asString() : "";
        if (kind == "reg") {
            RegItem it;
            it.root = o.at("root").asString();
            const std::string p = o.at("path").asString();
            const std::string n = o.at("name").asString();
            it.path.assign(p.begin(), p.end());
            it.name.assign(n.begin(), n.end());
            it.type = static_cast<DWORD>(o.at("type").asInt());
            it.data = bytesFromJson(o.at("data"));
            // Restore only if we still hold the value (restore != remove).
            if (!it.data.empty() && writeRegItem(it)) ++restored;
        } else if (kind == "service") {
            const std::string n = o.at("name").asString();
            std::wstring wname(n.begin(), n.end());
            DWORD startType = static_cast<DWORD>(o.at("startType").asInt());
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (scm) {
                SC_HANDLE h = OpenServiceW(scm, wname.c_str(), SERVICE_CHANGE_CONFIG);
                if (h) {
                    if (ChangeServiceConfigW(h, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
                        ++restored;
                    CloseServiceHandle(h);
                }
                CloseServiceHandle(scm);
            }
        }
    }
    return restored;
}

} // namespace bk