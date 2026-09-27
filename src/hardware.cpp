#include "hardware.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

#include <array>
#include <bit>
#include <cstdio>
#include <vector>

namespace hw {
namespace {

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring readRegString(HKEY root, const wchar_t* path, const wchar_t* value) {
    HKEY key = nullptr;
    std::wstring outValue;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0) {
            outValue.resize((size / sizeof(wchar_t)) + 1, L'\0');
            DWORD needed = size;
            if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(outValue.data()), &needed) == ERROR_SUCCESS) {
                outValue.resize(wcsnlen_s(outValue.data(), outValue.size()));
            }
        }
        RegCloseKey(key);
    }
    return outValue;
}

void detectCpu(HardwareInfo& info) {
    info.cpuName = wideToUtf8(readRegString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString"));

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);
    info.cores = si.dwNumberOfProcessors;          // logical
    info.logicalProcessors = si.dwNumberOfProcessors;
    // Accurate number of physical cores via GetLogicalProcessorInformation.
    DWORD bytes = 0;
    ::GetLogicalProcessorInformation(nullptr, &bytes);
    if (bytes > 0 && bytes < 1024 * 1024) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 2);
        if (::GetLogicalProcessorInformation(buf.data(), &bytes)) {
            unsigned n = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            unsigned phys = 0, logical = 0;
            for (unsigned i = 0; i < n; ++i) {
                if (buf[i].Relationship == RelationProcessorCore)
                    { ++phys; logical += (unsigned)std::countr_zero((unsigned)buf[i].ProcessorMask) + 1; }
            }
            if (phys) { info.cores = phys; info.logicalProcessors = logical; }
        }
    }
}

void detectGpus(HardwareInfo& info) {
    HDEVINFO devs = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devs == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(devs, i, &dev); ++i) {
        wchar_t name[MAX_PATH]{};
        DWORD type = 0, size = 0;
        if (::SetupDiGetDeviceRegistryPropertyW(devs, &dev, SPDRP_DEVICEDESC, &type,
            reinterpret_cast<BYTE*>(name), sizeof(name), &size) && name[0]) {
            info.gpus.push_back({ wideToUtf8(name) });
        }
    }
    ::SetupDiDestroyDeviceInfoList(devs);
}

void detectMemory(HardwareInfo& info) {
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (::GlobalMemoryStatusEx(&st)) info.totalPhysMB = st.ullTotalPhys / (1024 * 1024);
}

void detectOs(HardwareInfo& info) {
    // Display version: registry ProductName + DisplayVersion (Win11 adds its own).
    std::string product = wideToUtf8(readRegString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName"));
    std::string version = wideToUtf8(readRegString(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion"));
    if (!version.empty()) product += " " + version;
    info.osName = product;
}

} // namespace

HardwareInfo detectHardware() {
    HardwareInfo info;
    detectCpu(info);
    detectGpus(info);
    detectMemory(info);
    detectOs(info);
    return info;
}

} // namespace hw