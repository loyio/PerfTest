cmake_minimum_required(VERSION 3.16)
project(PerfTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 平台特定配置
if(WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Zi /O2")
endif()

# ============================================================
# Tracy Profiler (可选，通过 PERF_ENABLE_TRACY 开关)
#
# 启用方法:
#   cmake -DPERF_ENABLE_TRACY=ON ..
#
# 启用后需运行 Tracy Profiler GUI 连接目标进程查看实时数据
# 下载地址: https://github.com/wolfpld/tracy/releases
# ============================================================
option(PERF_ENABLE_TRACY     "Enable Tracy Profiler integration" OFF)
option(PERF_ENABLE_TELEMETRY "Enable production telemetry system"  OFF)

include(FetchContent)

# ============================================================
# Tracy Profiler (通过 git submodule 管理: third_party/tracy)
#
# 首次克隆后需初始化 submodule:
#   git submodule update --init --recursive
# ============================================================
if(PERF_ENABLE_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiling" FORCE)
    add_subdirectory(third_party/tracy)
    message(STATUS "Tracy Profiler ENABLED (submodule: third_party/tracy)")
else()
    message(STATUS "Tracy Profiler DISABLED (use -DPERF_ENABLE_TRACY=ON to enable)")
endif()

# ============================================================
# ImGui (通过 FetchContent 获取)
# ============================================================
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC d3d11 d3dcompiler dxgi)

# ============================================================
# 子项目
# ============================================================
add_subdirectory(common)
add_subdirectory(memory_leak)
add_subdirectory(stutter)
add_subdirectory(framerate)
if(WIN32)
    add_subdirectory(win_perf)
    add_subdirectory(PerfCaseGUI)
endif()
