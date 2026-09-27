cmake_minimum_required(VERSION 3.16)
project(UniversalGameOptimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/utf-8 /W3)
else()
  add_compile_options(-Wall -Wextra -O2)
endif()

# ---- ImGui ----
add_library(imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/backends/imgui_impl_dx11.cpp
  third_party/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE)

# ---- ImPlot ----
add_library(implot STATIC
  third_party/implot/implot.cpp
  third_party/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC third_party/implot)
target_link_libraries(implot PUBLIC imgui)

# ---- jsonxx (vendor) ----
add_library(jsonxx INTERFACE)
target_include_directories(jsonxx INTERFACE third_party/jsonxx)

# ---- App ----
add_executable(UGO WIN32
  src/main.cpp
  src/ui.cpp
  src/hardware.cpp
  src/optimizer.cpp
  src/theme.cpp
  src/backup.cpp
  src/metrics.cpp
  src/overlay.cpp
)
target_include_directories(UGO PRIVATE src third_party/jsonxx)
target_link_libraries(UGO PRIVATE imgui implot jsonxx)

target_compile_definitions(UGO PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)

# ---- Windows SDK libraries ----
set(win_libs
  d3d11 dxgi d3dcompiler   # ImGui D3D11 backend
  gdiplus                  # overlay text (anti-aliased)
  dwmapi                   # DWM frame conveniences
  pdh                      # performance counters (CPU temp)
  setupapi advapi32 shell32 user32 gdi32 comctl32
)
target_link_libraries(UGO PRIVATE ${win_libs})