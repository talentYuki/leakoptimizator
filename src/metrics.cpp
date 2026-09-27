#include "metrics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <thread>
#include <mutex>
#include <algorithm>

namespace met {

namespace {
struct PdhQuery {
    HQUERY query = nullptr;
    HCOUNTER counter = nullptr;
};
bool openCpuCounter(PdhQuery& q) {
    if (PdhOpenQueryW(nullptr, 0, &q.query) != ERROR_SUCCESS) return false;
    if (PdhAddEnglishCounterW(q.query, L"\\Thermal Zone Information\\Temperature", 0, &q.counter) != ERROR_SUCCESS) {
        PdhCloseQuery(q.query);
        q.query = nullptr;
        return false;
    }
    return true;
}
}

double readCpuTempC() {
    PdhQuery q;
    if (!openCpuCounter(q)) return NAN;
    PdhCollectQueryData(q.query);
    Sleep(250); // first sample is often meaningless; take a second read
    DWORD status = PdhCollectQueryData(q.query);
    double result = NAN;
    if (status == ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE val{};
        PdhGetFormattedCounterValue(q.counter, PDH_FMT_DOUBLE, nullptr, &val);
        if (val.CStatus == ERROR_SUCCESS) {
            // Value is in millikelvin * 10 (deci-K)? Convention: /10 then -273.15.
            double c = val.doubleValue / 10.0 - 273.15;
            if (c > 0 && c < 150) result = c;
        }
    }
    PdhCloseQuery(q.query);
    return result;
}

double readGpuTempC() {
    // No standard per-vendor PDH counter exists. Most reliable cross-vendor
    // source is LibreHardwareMonitor (MPL-2.0). Hook point left here.
    return NAN;
}

void Metrics::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread([this] { worker(); });
}

void Metrics::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void Metrics::frame(double dt) {
    if (dt <= 0) return;
    double fps = 1.0 / dt;
    std::lock_guard<std::mutex> lock(mutex_);
    if (windowN_ < 100) window_[windowN_++] = fps;
    else {
        // shift window
        for (int i = 1; i < 100; ++i) window_[i - 1] = window_[i];
        window_[99] = fps;
    }
    double sum = 0;
    for (int i = 0; i < windowN_; ++i) sum += window_[i];
    fps_.store(sum / std::max(1, windowN_));
}

// cpuTempC()/gpuTempC() are inline in the header.

void Metrics::worker() {
    PdhQuery q;
    bool have = openCpuCounter(q);
    while (running_.load()) {
        if (have) {
            PdhCollectQueryData(q.query);
            PDH_FMT_COUNTERVALUE val{};
            if (PdhGetFormattedCounterValue(q.counter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS
                && val.CStatus == ERROR_SUCCESS) {
                double c = val.doubleValue / 10.0 - 273.15;
                if (c > 0 && c < 150) cpu_.store(c);
            }
        } else {
            // retry occasionally (PDH may need a moment after boot)
            static int tries = 0;
            if ((++tries % 10) == 0) { have = openCpuCounter(q); }
        }
        gpu_.store(readGpuTempC());
        for (int i = 0; i < 20 && running_.load(); ++i) Sleep(50); // ~1s
    }
    if (q.query) PdhCloseQuery(q.query);
}

} // namespace met