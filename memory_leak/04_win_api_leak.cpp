/*
 * 04_win_api_leak.cpp - Windows API 资源泄漏检测
 *
 * 学习要点:
 * 1. Windows 句柄 (HANDLE) 泄漏
 * 2. GDI 对象泄漏
 * 3. COM 对象泄漏
 * 4. RAII 包装器解决方案
 *
 * Windows 特有的资源不受 C++ 智能指针管理，
 * 需要自定义 RAII 包装器或使用 unique_ptr + 自定义删除器
 */

#include "profiler.h"
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <Windows.h>

// ============================================================
// RAII 包装器: 自动管理 Windows HANDLE
// ============================================================
class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE h = INVALID_HANDLE_VALUE) : handle_(h) {}

    ~ScopedHandle() {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
            CloseHandle(handle_);
            std::cout << "    [RAII] Handle closed automatically\n";
        }
    }

    // 禁止拷贝
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    // 允许移动
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    HANDLE get() const { return handle_; }
    bool isValid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }

    HANDLE release() {
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return h;
    }

private:
    HANDLE handle_;
};

// ============================================================
// 使用 unique_ptr + 自定义删除器
// ============================================================
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            std::cout << "    [unique_ptr] Handle closed via custom deleter\n";
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

// ============================================================
// 场景1: HANDLE 泄漏示例
// ============================================================
void demo_handle_leak() {
    perf::printHeader("BAD: Handle Leak");

    // 创建一个事件对象
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (hEvent) {
        std::cout << "  Created event handle: " << hEvent << "\n";

        // 模拟: 提前返回导致 CloseHandle 未被调用
        bool earlyReturn = true;
        if (earlyReturn) {
            std::cout << "  [BUG] Early return - handle leaked!\n";
            return; // BUG: 没有 CloseHandle!
        }

        CloseHandle(hEvent); // 永远不会执行到
    }
}

// ============================================================
// 场景2: 使用 RAII 修复
// ============================================================
void demo_handle_raii() {
    perf::printHeader("GOOD: Handle with RAII");

    ScopedHandle hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));

    if (hEvent.isValid()) {
        std::cout << "  Created event handle: " << hEvent.get() << "\n";

        // 即使提前返回也不会泄漏
        bool earlyReturn = true;
        if (earlyReturn) {
            std::cout << "  Early return - but handle is safe!\n";
            return; // ScopedHandle 析构自动调用 CloseHandle
        }
    }
}

// ============================================================
// 场景3: unique_ptr 自定义删除器
// ============================================================
void demo_unique_ptr_handle() {
    perf::printHeader("GOOD: Handle with unique_ptr + custom deleter");

    UniqueHandle hEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));

    if (hEvent) {
        std::cout << "  Created event handle via unique_ptr\n";

        // 等待/使用事件...
        SetEvent(hEvent.get());
        std::cout << "  Event signaled\n";
    }
    // 离开作用域时自动 CloseHandle
}

// ============================================================
// 场景4: 文件操作中的句柄管理
// ============================================================
void demo_file_handle() {
    perf::printHeader("File Handle Management");

    // 创建临时文件用于演示
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempFile = std::wstring(tempPath) + L"perftest_demo.tmp";

    {
        // 使用 RAII 管理文件句柄
        ScopedHandle hFile(CreateFileW(
            tempFile.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        ));

        if (hFile.isValid()) {
            const char* data = "Hello, PerfTest!";
            DWORD written = 0;
            WriteFile(hFile.get(), data, static_cast<DWORD>(strlen(data)), &written, nullptr);
            std::cout << "  Wrote " << written << " bytes to temp file\n";
        }
        // hFile 自动关闭
    }

    // 读取验证
    {
        ScopedHandle hFile(CreateFileW(
            tempFile.c_str(),
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        ));

        if (hFile.isValid()) {
            char buffer[256] = {};
            DWORD read = 0;
            ReadFile(hFile.get(), buffer, sizeof(buffer) - 1, &read, nullptr);
            std::cout << "  Read back: \"" << buffer << "\"\n";
        }
    }

    // 清理临时文件
    DeleteFileW(tempFile.c_str());
}

// ============================================================
// 场景5: 互斥量与关键段
// ============================================================
class ScopedMutex {
public:
    explicit ScopedMutex(HANDLE mutex) : mutex_(mutex) {
        WaitForSingleObject(mutex_, INFINITE);
    }
    ~ScopedMutex() {
        ReleaseMutex(mutex_);
    }
    ScopedMutex(const ScopedMutex&) = delete;
    ScopedMutex& operator=(const ScopedMutex&) = delete;
private:
    HANDLE mutex_;
};

void demo_mutex_raii() {
    perf::printHeader("Mutex RAII Pattern");

    ScopedHandle hMutex(CreateMutexW(nullptr, FALSE, nullptr));

    if (hMutex.isValid()) {
        {
            ScopedMutex lock(hMutex.get());
            std::cout << "  Mutex acquired (RAII)\n";
            // 做一些需要互斥的工作...
        } // 自动 ReleaseMutex

        std::cout << "  Mutex released automatically\n";
    }
    // hMutex 离开作用域自动 CloseHandle
}

#endif // _WIN32

int main() {
    perf::initConsole();
    std::cout << "===== 04: Windows API Resource Leak Tutorial =====\n";

#ifdef _WIN32
    demo_handle_leak();
    demo_handle_raii();
    demo_unique_ptr_handle();
    demo_file_handle();
    demo_mutex_raii();

    std::cout << "\n===== Key Takeaways =====\n"
              << "1. Windows HANDLE 必须用 CloseHandle() 释放\n"
              << "2. 自定义 RAII 类 (ScopedHandle) 确保资源自动释放\n"
              << "3. unique_ptr + 自定义删除器是轻量级方案\n"
              << "4. 任何异常/提前返回都不会导致泄漏\n"
              << "5. 同样的模式适用于 GDI 对象、COM 对象等\n";
#else
    std::cout << "This example is Windows-only.\n";
#endif

    return 0;
}
