/*
 * tracy_integration.h - 性能打点统一宏封装
 *
 * 根据编译选项自动路由到不同后端:
 *
 *   TRACY_ENABLE          → Tracy Profiler (实时可视化, 连接 Tracy GUI)
 *   PERF_TELEMETRY_ENABLE → Telemetry 系统 (文件采集, Chrome Trace 导出, 卡顿检测)
 *   都未定义              → 空操作, 零开销
 *
 * 两者都是内部工具, 按需二选一:
 *   cmake -DPERF_ENABLE_TRACY=ON ..       → Tracy 模式
 *   cmake -DPERF_ENABLE_TELEMETRY=ON ..   → Telemetry 模式
 *
 * 使用方式: 直接用 Tracy 原生 API 名称, 本头文件自动路由:
 *
 *   FrameMark                        - 帧边界标记 (主循环末尾)
 *   FrameMarkNamed(name)             - 命名帧标记
 *   ZoneScoped                       - 自动记录当前函数耗时
 *   ZoneScopedN(name)                - 自定义名称 Zone
 *   ZoneScopedC(color)               - 带颜色 Zone
 *   TracyMessage(text, len)          - 时间线消息
 *   TracyMessageL(text)              - 字面量消息
 *   TracyAlloc(ptr, size)            - 内存分配追踪
 *   TracyFree(ptr)                   - 内存释放追踪
 *   TracyPlot(name, val)             - 数值曲线
 *   TracySetThreadName(name)         - 设置线程名称
 *   TracyLockable(type, varname)     - 可追踪的锁
 *   TracyScopedContext(text)         - RAII 上下文标记 (Telemetry 扩展)
 */

#pragma once

// ============================================================
// Tracy 模式 (实时可视化)
// ============================================================
#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>

    // Tracy 原生宏已由 Tracy.hpp 提供, 此处仅补充 Telemetry 扩展的空实现
    #define TracyScopedContext(text)     (void)0

// ============================================================
// Telemetry 模式 (文件采集 + 卡顿检测)
// ============================================================
#elif defined(PERF_TELEMETRY_ENABLE)
    #include "telemetry.h"

    // 唯一变量名生成
    #define _TI_CONCAT2(x, y) x##y
    #define _TI_CONCAT(x, y) _TI_CONCAT2(x, y)
    #define _TI_UNIQUE(prefix) _TI_CONCAT(prefix, __COUNTER__)

    // 帧标记 (含卡顿检测)
    #define FrameMark                   telemetry::Manager::get().frameMark()
    #define FrameMarkNamed(name)        telemetry::Manager::get().frameMark(name)

    // Zone
    #define ZoneScoped                  telemetry::ZoneGuard _TI_UNIQUE(_tz_)(__FUNCTION__)
    #define ZoneScopedN(name)           telemetry::ZoneGuard _TI_UNIQUE(_tz_)(name)
    #define ZoneScopedC(color)          telemetry::ZoneGuard _TI_UNIQUE(_tz_)(__FUNCTION__, color)

    // 消息
    #define TracyMessage(text, len)     telemetry::RecordMessage(text)
    #define TracyMessageL(text)         telemetry::RecordMessage(text)

    // 内存追踪 (TODO: 后续扩展)
    #define TracyAlloc(ptr, size)       (void)0
    #define TracyFree(ptr)              (void)0

    // Plot
    #define TracyPlot(name, val)        telemetry::RecordPlot(name, static_cast<double>(val))

    // 线程名称
    #define TracySetThreadName(name)    telemetry::SetThreadName(name)

    // 可追踪锁
    #define TracyLockable(type, varname) telemetry::TrackedMutex<type> varname{#varname}

    // 上下文标记 (卡顿时自动捕获各线程上下文)
    #define TracyScopedContext(text)    telemetry::ScopedContext _TI_UNIQUE(_ctx_)(text)

// ============================================================
// 关闭模式 (零开销)
// ============================================================
#else
    #define FrameMark                   (void)0
    #define FrameMarkNamed(name)        (void)0

    #define ZoneScoped                  (void)0
    #define ZoneScopedN(name)           (void)0
    #define ZoneScopedC(color)          (void)0

    #define TracyMessage(text, len)     (void)0
    #define TracyMessageL(text)         (void)0

    #define TracyAlloc(ptr, size)       (void)0
    #define TracyFree(ptr)              (void)0

    #define TracyPlot(name, val)        (void)0

    #define TracySetThreadName(name)    (void)0

    #define TracyLockable(type, varname) type varname

    #define TracyScopedContext(text)    (void)0
#endif
