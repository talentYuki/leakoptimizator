#pragma once
// Overlay: a small always-on-top, click-through, semi-transparent window that
// shows live FPS / CPU / GPU values. Rendered with GDI+ + UpdateLayeredWindow.

#include <windows.h>
#include <atomic>
#include <thread>

namespace ovl {

class Overlay {
public:
    Overlay() = default;
    ~Overlay() { destroy(); }

    // Creates the hidden overlay window. Returns false on failure.
    bool create();

    void show();
    void hide();
    void destroy();

    void setFps(double fps);
    void setCpu(double tempC);
    void setGpu(double tempC);
    void setAccent(float r, float g, float b, float a);

    bool visible() const { return visible_.load(); }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void redrawSurface();
    void renderLoop();

    HWND hwnd_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> visible_{false};

    std::atomic<float> fps_{-1.0f}, cpu_{-1.0f}, gpu_{-1.0f};
    std::atomic<float> ar_{0.35f}, ag_{0.55f}, ab_{1.0f}, aa_{1.0f};
};

} // namespace ovl