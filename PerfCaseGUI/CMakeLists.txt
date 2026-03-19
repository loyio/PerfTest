# 自动收集 cases/ 下所有 .cpp 文件，新增用例无需手动修改此文件
file(GLOB CASE_SOURCES CONFIGURE_DEPENDS cases/*.cpp)

add_executable(PerfCaseGUI WIN32
    main.cpp
    ${CASE_SOURCES}
)

target_link_libraries(PerfCaseGUI
    perf_common
    imgui_lib
    d3d11
    dxgi
    winmm
    psapi
)
