#include "optimizer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>       // SYSTEM_INFORMATION_CLASS for NtSetSystemInformation
#include <winsvc.h>         // service control (excluded by WIN32_LEAN_AND_MEAN)
#include <tlhelp32.h>       // process snapshots
#include <iterator>         // std::size
#include <cwchar>           // _wcsicmp
#include <string>

namespace opt {

// ---------------------------------------------------------------------------
// Clear standby (standby list) memory using the (undocumented) ntdll call.
// ---------------------------------------------------------------------------
typedef LONG(WINAPI* NtSetInformationSystemType)(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength);

ActionResult clearStandbyMemory() {
    const DWORD SystemMemoryListInformation = 0x50; // 80
    struct SYSTEM_MEMORY_LIST_INFORMATION {
        ULONG_PTR MemoryListInformation; // MemoryPurgeLowPriority = 1
    };
    ActionResult r;

    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    auto proc = ntdll ? reinterpret_cast<NtSetInformationSystemType>(
        ::GetProcAddress(ntdll, "NtSetSystemInformation")) : nullptr;
    if (!proc) { r.message = "ntdll export not found"; return r; }

    // Enable required privilege so the call is allowed.
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        LUID luid{};
        const wchar_t* names[] = { L"SeIncreaseQuotaPrivilege", L"SeProfileSingleProcessPrivilege" };
        for (const wchar_t* n : names) {
            if (::LookupPrivilegeValueW(nullptr, n, &luid)) {
                tp.Privileges[0].Luid = luid;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                ::AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
            }
        }
        ::CloseHandle(token);
    }

    SYSTEM_MEMORY_LIST_INFORMATION info{};
    info.MemoryListInformation = 1; // MemoryPurgeLowPriority
    LONG status = proc((SYSTEM_INFORMATION_CLASS)SystemMemoryListInformation, &info, sizeof(info));
    if (status == 0) {
        r.ok = true;
        r.message = "Standby list purged";
    } else {
        r.message = "NtSetSystemInformation failed (status 0x" + std::to_string((unsigned)status) + "). Run as admin.";
    }
    return r;
}

// ---------------------------------------------------------------------------
// Process priority
// ---------------------------------------------------------------------------
std::optional<DWORD> findPidByName(const std::wstring& processName) {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
    std::optional<DWORD> pid;
    if (::Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) { pid = entry.th32ProcessID; break; }
        } while (::Process32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return pid;
}

ActionResult setProcessHighPriority(DWORD pid) {
    ActionResult r;
    HANDLE h = ::OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) { r.message = "OpenProcess failed: cannot attach to PID " + std::to_string(pid); return r; }
    BOOL ok = ::SetPriorityClass(h, HIGH_PRIORITY_CLASS);
    ::CloseHandle(h);
    r.ok = ok != FALSE;
    r.message = ok ? ("PID " + std::to_string(pid) + " set to High priority") : "SetPriorityClass failed (access denied?)";
    return r;
}

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------
namespace {
bool setRegDword(HKEY root, const std::wstring& path, const std::wstring& name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG st = ::RegSetValueExW(key, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    ::RegCloseKey(key);
    return st == ERROR_SUCCESS;
}
}

ActionResult setGameBarOff() {
    struct { const wchar_t* path; const wchar_t* name; DWORD value; } rules[] = {
        // Disable Game DVR background recording.
        { L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0 },
        { L"System\\GameConfigStore", L"GameDVR_Enabled", 0 },
        { L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode", 2 },
        // Disable Game Bar.
        { L"Software\\Microsoft\\GameBar", L"AllowAutoGameMode", 0 },
        { L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled", 0 },
        { L"Software\\Microsoft\\GameBar", L"UseNexusForGameBarEnabled", 0 },
    };
    ActionResult r;
    int ok = 0;
    for (const auto& rr : rules)
        if (setRegDword(HKEY_CURRENT_USER, rr.path, rr.name, rr.value)) ++ok;
    r.ok = ok > 0;
    r.message = "Game Bar / Game DVR regs applied (" + std::to_string(ok) + "/" +
                std::to_string(std::size(rules)) + ")";
    return r;
}

ActionResult setNagleOff() {
    const wchar_t* top = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
    ActionResult r;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, top, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        r.message = "Cannot open Tcpip Interfaces (admin required)";
        return r;
    }
    int touched = 0;
    for (DWORD idx = 0;; ++idx) {
        wchar_t subkey[256]{};
        DWORD sublen = 256;
        if (RegEnumKeyExW(root, idx, subkey, &sublen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        std::wstring path = std::wstring(top) + L"\\" + subkey;
        if (setRegDword(HKEY_LOCAL_MACHINE, path, L"TcpAckFrequency", 1) && setRegDword(HKEY_LOCAL_MACHINE, path, L"TCPNoDelay", 1))
            ++touched;
    }
    RegCloseKey(root);
    r.touched = touched;
    r.ok = touched > 0;
    r.message = "Nagle disabled on " + std::to_string(touched) + " interface(s)";
    return r;
}

ActionResult setService(const std::wstring& serviceName, bool disable) {
    ActionResult r;
    const std::string narrow(serviceName.begin(), serviceName.end());
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) { r.message = "SCM open failed (admin required)"; return r; }
    SC_HANDLE svc = ::OpenServiceW(scm, serviceName.c_str(), SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG | SERVICE_STOP);
    if (!svc) { r.message = "Service " + narrow + " not found"; CloseServiceHandle(scm); return r; }

    DWORD startType = disable ? SERVICE_DISABLED : SERVICE_DEMAND_START;
    SERVICE_STATUS status{};
    ::ControlService(svc, SERVICE_CONTROL_STOP, &status); // best-effort
    BOOL ok = ::ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, startType,
                                     SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);
    r.ok = ok != FALSE;
    r.message = ok ? (narrow + (disable ? " disabled" : " enabled")) : ("ChangeServiceConfig failed for " + narrow);
    return r;
}

bool relaunchAsAdmin(const std::wstring& args) {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ::ShellExecuteExW(&sei) != FALSE;
}

} // namespace opt