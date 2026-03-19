/*
 * perf_case.h - 性能用例注册框架
 *
 * 使用方法:
 *
 * 1. 创建一个 .cpp 文件，定义一个继承 PerfCase 的类
 * 2. 实现 getCategory(), getName(), getDescription(), onDrawUI()
 * 3. 文件末尾用 ADD_PERF_CASE(ClassName) 宏注册
 * 4. 在 CMakeLists.txt 中把 .cpp 加入编译即可
 *
 * 示例:
 *
 *   class MyCpuBurnCase : public PerfCase {
 *   public:
 *       const char* getCategory()    const override { return "CPU"; }
 *       const char* getName()        const override { return "Sin/Cos Burn"; }
 *       const char* getDescription() const override { return "密集sin/cos计算负载"; }
 *
 *       void onUpdate(float dt) override {
 *           volatile double r = 0;
 *           for (int i = 0; i < iterations; i++)
 *               r += std::sin((double)i);
 *       }
 *
 *       void onDrawUI() override {
 *           ImGui::SliderInt("Iterations", &iterations, 1000, 10000000);
 *       }
 *
 *   private:
 *       int iterations = 1000000;
 *   };
 *   ADD_PERF_CASE(MyCpuBurnCase)
 */

#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <map>

// ============================================================
// PerfCase 基类
// ============================================================
class PerfCase {
public:
    virtual ~PerfCase() = default;

    // --- 必须实现 ---
    virtual const char* getCategory()    const = 0;  // "Memory", "Stutter", "Framerate" ...
    virtual const char* getName()        const = 0;  // 用例名称
    virtual const char* getDescription() const = 0;  // 简短描述

    // --- 生命周期 (可选覆盖) ---
    virtual void onActivate()   {}      // 被选中激活时
    virtual void onDeactivate() {}      // 被取消激活时

    // --- 每帧调用 (激活状态) ---
    virtual void onUpdate(float deltaTime) {}  // 每帧的工作负载

    // --- ImGui 控制面板 (激活状态) ---
    virtual void onDrawUI() = 0;        // 绘制参数控制和结果显示

    // --- 框架管理的状态 ---
    bool  active      = false;
    float lastUpdateMs = 0.0f;  // 上一次 onUpdate 的耗时 (ms)

    // --- 工具方法: 测量代码块耗时 ---
    template<typename Fn>
    float measure(Fn&& fn) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float, std::milli>(end - start).count();
    }
};

// ============================================================
// PerfRegistry - 全局注册中心
// ============================================================
class PerfRegistry {
public:
    static PerfRegistry& get() {
        static PerfRegistry instance;
        return instance;
    }

    void registerCase(PerfCase* pc) {
        cases_.push_back(pc);
    }

    std::vector<PerfCase*>& allCases() {
        return cases_;
    }

    // 按 category 分组
    std::map<std::string, std::vector<PerfCase*>> byCategory() {
        std::map<std::string, std::vector<PerfCase*>> groups;
        for (auto* c : cases_) {
            groups[c->getCategory()].push_back(c);
        }
        return groups;
    }

private:
    PerfRegistry() = default;
    std::vector<PerfCase*> cases_;
};

// ============================================================
// ADD_PERF_CASE 宏 - 自动注册
//
// 用法: 在 .cpp 文件末尾写 ADD_PERF_CASE(YourClassName)
// 链接时自动注册到 PerfRegistry
// ============================================================
#define ADD_PERF_CASE(ClassName) \
    namespace { \
        struct AutoReg_##ClassName { \
            ClassName instance; \
            AutoReg_##ClassName() { PerfRegistry::get().registerCase(&instance); } \
        }; \
        static AutoReg_##ClassName _autoreg_##ClassName; \
    }
