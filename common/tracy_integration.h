/*
 * tracy_integration.h - Tracy Profiler 集成封装
 *
 * 提供统一的 Tracy 宏封装，使项目在未启用 Tracy 时也能正常编译。
 * Tracy 的开关由 CMake 选项 PERF_ENABLE_TRACY 控制：
 *   cmake -DPERF_ENABLE_TRACY=ON ..
 *
 * 启用 Tracy 后，可使用 Tracy Profiler GUI 连接正在运行的程序，
 * 实时查看帧时间、函数耗时、内存分配等性能数据。
 *
 * 常用宏:
 *   PERF_FRAME_MARK          - 标记帧边界（放在主循环末尾）
 *   PERF_ZONE_SCOPED         - 自动记录当前函数耗时
 *   PERF_ZONE_SCOPED_N(name) - 自定义名称的 Zone
 *   PERF_ZONE_COLOR(color)   - 带颜色标记的 Zone
 *   PERF_MESSAGE(text, len)  - 在时间线上标注消息
 *   PERF_MESSAGE_L(text)     - 在时间线上标注字面量消息
 */

#pragma once

#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>

    // 帧标记
    #define PERF_FRAME_MARK             FrameMark
    #define PERF_FRAME_MARK_N(name)     FrameMarkNamed(name)

    // Zone（函数/代码块耗时）
    #define PERF_ZONE_SCOPED            ZoneScoped
    #define PERF_ZONE_SCOPED_N(name)    ZoneScopedN(name)
    #define PERF_ZONE_COLOR(color)      ZoneScopedC(color)

    // 消息标注
    #define PERF_MESSAGE(text, len)     TracyMessage(text, len)
    #define PERF_MESSAGE_L(text)        TracyMessageL(text)

    // 内存追踪
    #define PERF_ALLOC(ptr, size)       TracyAlloc(ptr, size)
    #define PERF_FREE(ptr)              TracyFree(ptr)

    // Plot（数值图表）
    #define PERF_PLOT(name, val)        TracyPlot(name, val)
#else
    // Tracy 未启用时，所有宏展开为空操作
    #define PERF_FRAME_MARK             (void)0
    #define PERF_FRAME_MARK_N(name)     (void)0

    #define PERF_ZONE_SCOPED            (void)0
    #define PERF_ZONE_SCOPED_N(name)    (void)0
    #define PERF_ZONE_COLOR(color)      (void)0

    #define PERF_MESSAGE(text, len)     (void)0
    #define PERF_MESSAGE_L(text)        (void)0

    #define PERF_ALLOC(ptr, size)       (void)0
    #define PERF_FREE(ptr)              (void)0

    #define PERF_PLOT(name, val)        (void)0
#endif
