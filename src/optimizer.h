#pragma once
// System "optimizer" tweaks. Everything goes through WinAPI / ntdll / ADVAPI32.
// Registry and service changes require administrator privileges.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opt {

struct ActionResult {
    bool ok = false;
    std::string message;
    // For Nagle: how many network adapters were patched.
    int touched = 0;
};

// --- Standby memory (NtSetSystemInformation / SystemMemoryListInformation) ---
ActionResult clearStandbyMemory();

// --- Process priority ---
ActionResult setProcessHighPriority(DWORD pid);
// Returns the first matching process id for a (game) name, or nullopt.
std::optional<DWORD> findPidByName(const std::wstring& processName);

// --- Registry tweak: a single DWORD with rollback capture ---
struct RegDwordBackup { std::wstring path, value; DWORD oldValue; bool hadValue; };

// Enables/disables Xbox Game Bar + Game DVR. Returns list of applied values.
ActionResult setGameBarOff();

// Sets TCPNoDelay + TcpAckFrequency on all TCP/IP interfaces (Nagle off).
ActionResult setNagleOff();

// Disables/restores a Windows service's start type. True disables, false enables.
ActionResult setService(const std::wstring& serviceName, bool disable);

// A single admin-elevation re-launch helper (used when a tweak needs admin).
bool relaunchAsAdmin(const std::wstring& args);

} // namespace opt