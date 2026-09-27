#pragma once
// Background metric gathering: FPS (self-rendered) and CPU/GPU temperature
// via Performance Data Helper (PDH). Runs on a shared background thread.

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace met {

class Metrics {
public:
    Metrics() = default;
    ~Metrics() { stop(); }

    void start();
    void stop();

    double cpuTempC() const { return cpu_.load(); }   // NaN when unavailable
    double gpuTempC() const { return gpu_.load(); }   // NaN when unavailable
    double fps() const { return fps_.load(); }
    bool   running() const { return running_.load(); }

    // Feed one frame's delta-seconds from the render loop.
    void frame(double dt);

private:
    void worker();

    std::thread thread_;                 // guarded by running_
    std::atomic<bool> running_{false};
    std::atomic<double> cpu_{NAN};
    std::atomic<double> gpu_{NAN};

    // FPS (rolling average of the last second)
    std::atomic<double> fps_{0.0};
    double window_[100] = {};
    int windowN_ = 0;
    std::mutex mutex_;
};

double readCpuTempC();   // PDH thermal-zone read (blocking, ~ms)
double readGpuTempC();   // Not exposed over PDH on all GPUs -> NaN mostly.

} // namespace met