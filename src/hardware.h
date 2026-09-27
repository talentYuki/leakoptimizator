#pragma once
// Hardware detection: CPU, GPU, RAM, OS (all via WinAPI / SetupAPI / registry).

#include <string>
#include <vector>

namespace hw {

struct GpuInfo {
    std::string name;
};

struct HardwareInfo {
    std::string cpuName;
    unsigned    cores = 0;
    unsigned    logicalProcessors = 0;
    std::vector<GpuInfo> gpus;
    unsigned long long    totalPhysMB = 0;   // MB
    std::string osName;                      // e.g. "Windows 10 Pro 22H2"
};

// Detects hardware synchronously. Fast (registry + SetupAPI + one PdhOp).
HardwareInfo detectHardware();

} // namespace hw