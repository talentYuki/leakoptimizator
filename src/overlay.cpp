#include "overlay.h"

#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace ovl {

namespace {
constexpr int kWidth = 360;
constexpr int kHeight = 128;
ULONG_PTR g_gdiToken = 0;
bool g_gdiStarted = false;

void startGdiPlus() {
    if (g_gdiStarted) return;
    GdiplusStartupInput in;
    GdiplusStartup(&g_gdiToken, &in, nullptr);
    g_gdiStarted = true;
}
void shutdownGdiPlus() {
    if (g_gdiStarted) { GdiplusShutdown(g_gdiToken); g_gdiStarted = false; }
}

// Premultiply straight alpha into 32-bit BGRA so UpdateLayeredWindow
// receives valid premultiplied data.
void premultiply(void* bits) {
    auto* p = static_cast<unsigned char*>(bits);
    for (int i = 0; i < kWidth * kHeight; ++i) {
        unsigned char a = p[i * 4 + 3];
        if (a == 0) { p[i*4] = p[i*4+1] = p[i*4+2] = 0; continue; }
        p[i * 4]     = (unsigned char)(p[i * 4]     * a / 255);
        p[i * 4 + 1] = (unsigned char)(p[i * 4 + 1] * a / 255);
        p[i * 4 + 2] = (unsigned char)(p[i * 4 + 2] * a / 255);
    }
}

} // namespace

bool Overlay::create() {
    startGdiPlus();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Overlay::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"UGOOverlayClass";
    RegisterClassExW(&wc);

    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    hwnd_ = CreateWindowExW(ex, L"UGOOverlayClass", L"UGO Overlay",
                            WS_POPUP, 0, 0, kWidth, kHeight,
                            nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    running_.store(true);
    thread_ = std::thread([this] { renderLoop(); });
    return true;
}

void Overlay::renderLoop() {
    while (running_.load()) {
        if (visible_.load()) redrawSurface();
        for (int i = 0; i < 10 && running_.load(); ++i) Sleep(25);
    }
}

void Overlay::redrawSurface() {
    HDC screen = ::GetDC(nullptr);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kWidth;
    bmi.bmiHeader.biHeight = -kHeight;         // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = ::CreateCompatibleDC(screen);
    ::SelectObject(mem, bmp);
    std::memset(bits, 0, size_t(kWidth) * kHeight * 4);

    if (bits) {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);

        float ar = ar_.load(), ag = ag_.load(), ab = ab_.load(), aa = aa_.load();
        Color accent((BYTE)(aa * 255), (BYTE)(ar * 255), (BYTE)(ag * 255), (BYTE)(ab * 255));
        SolidBrush accentBrush(accent);
        SolidBrush textBrush(Color(230, 235, 235, 238));
        SolidBrush dimBrush(Color(200, 160, 163, 168));

        // accent header bar
        g.FillRectangle(&accentBrush, 6, 6, kWidth - 12, 3);

        Font big(L"Segoe UI", 24, FontStyleBold, UnitPixel);
        Font small(L"Segoe UI", 15, FontStyleRegular, UnitPixel);
        StringFormat sf;

        std::wstring fps = L"FPS " + std::to_wstring((int)(fps_.load() + 0.5f));
        g.DrawString(fps.c_str(), -1, &big, PointF(14, 20), &sf, &textBrush);

        wchar_t line[128];
        float cpu = cpu_.load(), gpu = gpu_.load();
        _snwprintf_s(line, _TRUNCATE, L"CPU %5.1f C", (cpu < 0) ? 0.0 : (double)cpu);
        g.DrawString(line, -1, &small, PointF(14, 58), &sf, &textBrush);
        _snwprintf_s(line, _TRUNCATE, L"GPU %5.1f C", (gpu < 0) ? 0.0 : (double)gpu);
        g.DrawString(line, -1, &small, PointF(14, 82), &sf, &textBrush);

        premultiply(bits);
    }

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    POINT pt{ 0, 0 }, src{ 0, 0 };
    SIZE sz{ kWidth, kHeight };
    ::UpdateLayeredWindow(hwnd_, screen, &pt, &sz, mem, &src, 0, &bf, ULW_ALPHA);

    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
}

void Overlay::setFps(double v) { fps_.store((float)v); }
void Overlay::setCpu(double v) { cpu_.store((float)v); }
void Overlay::setGpu(double v) { gpu_.store((float)v); }
void Overlay::setAccent(float r, float g, float b, float a) { ar_ = r; ag_ = g; ab_ = b; aa_ = a; }

void Overlay::show() { if (hwnd_) { ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE); visible_.store(true); } }
void Overlay::hide() { if (hwnd_) { ::ShowWindow(hwnd_, SW_HIDE); visible_.store(false); } }

void Overlay::destroy() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    if (hwnd_) { ::DestroyWindow(hwnd_); hwnd_ = nullptr; }
    shutdownGdiPlus();
}

LRESULT CALLBACK Overlay::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_PAINT: { PAINTSTRUCT ps; ::BeginPaint(h, &ps); ::EndPaint(h, &ps); return 0; }
        default: break;
    }
    return ::DefWindowProcW(h, m, w, l);
}

} // namespace ovl