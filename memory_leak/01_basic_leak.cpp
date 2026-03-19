/*
 * 01_basic_leak.cpp - 基本内存泄漏示例
 *
 * 学习要点:
 * 1. 最常见的内存泄漏模式: new 了但忘记 delete
 * 2. 使用 MemoryTracker 检测泄漏
 * 3. Windows CRT Debug 内存检测 (_CrtDumpMemoryLeaks)
 *
 * 运行后观察:
 * - MemoryTracker 报告中会显示哪些内存没有被释放
 * - 泄漏发生在哪个文件的哪一行
 */

#include "profiler.h"
#include "memory_tracker.h"
#include <vector>
#include <string>

#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

// ============================================================
// 场景1: 最基本的泄漏 - new 了不 delete
// ============================================================
void scenario_basic_leak() {
    perf::printHeader("Scenario 1: Basic Leak (new without delete)");

    // 泄漏! 分配了内存但没有释放
    int* leakedInt = TRACKED_NEW(int, 42);
    std::cout << "  Allocated int with value: " << *leakedInt << "\n";
    std::cout << "  [BUG] Forgot to delete!\n";
    // 应该: TRACKED_DELETE(leakedInt);
}

// ============================================================
// 场景2: 异常路径泄漏 - 异常导致 delete 没有执行
// ============================================================
void scenario_exception_leak() {
    perf::printHeader("Scenario 2: Exception Path Leak");

    int* data = TRACKED_NEW_ARRAY(int, 1000);

    try {
        // 模拟在处理过程中抛出异常
        for (int i = 0; i < 1000; ++i) {
            data[i] = i * 2;
            if (i == 500) {
                throw std::runtime_error("Something went wrong at index 500!");
            }
        }
        // 如果异常发生，下面的 delete 永远不会执行
        TRACKED_DELETE_ARRAY(data);
    } catch (const std::exception& e) {
        std::cout << "  Exception caught: " << e.what() << "\n";
        std::cout << "  [BUG] data[] was never freed!\n";
        // 修复: 在catch中也要释放
        // TRACKED_DELETE_ARRAY(data);
    }
}

// ============================================================
// 场景3: 容器中的裸指针泄漏
// ============================================================
struct GameObject {
    std::string name;
    float x, y, z;
    GameObject(const std::string& n) : name(n), x(0), y(0), z(0) {}
};

void scenario_container_leak() {
    perf::printHeader("Scenario 3: Leaked Pointers in Container");

    std::vector<GameObject*> objects;

    // 向容器添加动态分配的对象
    for (int i = 0; i < 5; ++i) {
        auto* obj = TRACKED_NEW(GameObject, "Object_" + std::to_string(i));
        objects.push_back(obj);
    }

    std::cout << "  Created " << objects.size() << " game objects\n";

    // 容器被销毁时，裸指针指向的对象不会被自动释放!
    // BUG: 忘记遍历并 delete 每个元素
    objects.clear(); // 只清空了指针，内存还在!

    std::cout << "  [BUG] Cleared vector but objects still in memory!\n";

    // 修复:
    // for (auto* obj : objects) { TRACKED_DELETE(obj); }
    // objects.clear();
}

// ============================================================
// 场景4: 正确的做法 (用于对比)
// ============================================================
void scenario_no_leak() {
    perf::printHeader("Scenario 4: Correctly Managed (No Leak)");

    int* data = TRACKED_NEW(int, 100);
    std::cout << "  Allocated int: " << *data << "\n";
    TRACKED_DELETE(data);
    std::cout << "  Properly deleted.\n";

    // 数组版本
    int* arr = TRACKED_NEW_ARRAY(int, 500);
    for (int i = 0; i < 500; ++i) arr[i] = i;
    TRACKED_DELETE_ARRAY(arr);
    std::cout << "  Array properly deleted.\n";
}

int main() {
    perf::initConsole();
    std::cout << "===== 01: Basic Memory Leak Tutorial =====\n";
    std::cout << "This example demonstrates common memory leak patterns.\n";
    std::cout << "Check the Memory Report at the end for leak details.\n";

#ifdef _WIN32
    // 启用 Windows CRT 调试堆
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    perf::MemoryTracker::printProcessMemory();
#endif

    auto& tracker = perf::MemoryTracker::instance();
    tracker.enable();

    scenario_basic_leak();
    scenario_exception_leak();
    scenario_container_leak();
    scenario_no_leak();

    // 输出内存报告 - 这里应该能看到泄漏
    tracker.report();

#ifdef _WIN32
    std::cout << "\nAfter all operations:\n";
    perf::MemoryTracker::printProcessMemory();
#endif

    return 0;
}
