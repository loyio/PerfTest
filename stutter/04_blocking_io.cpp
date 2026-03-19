/*
 * 04_blocking_io.cpp - 阻塞 I/O 导致的卡顿
 *
 * 学习要点:
 * 1. 同步文件 I/O 如何阻塞主线程
 * 2. 异步 I/O (Windows Overlapped I/O)
 * 3. 缓冲策略: 批量 vs 逐次写入
 * 4. 内存映射文件 (Memory-mapped files)
 *
 * 文件 I/O 是主线程卡顿的常见原因 (比如自动保存、日志)
 */

#include "profiler.h"
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#include <Windows.h>
#endif

// ============================================================
// 测试1: 逐次写入 vs 批量写入
// ============================================================
void test_write_strategies() {
    perf::printHeader("Write Strategy: Per-item vs Batched");

    constexpr int LINES = 50000;
    std::string line = "This is a test log line with some data: 1234567890 ABCDEF\n";

    // BAD: 每行写入 + 每次 flush
    auto unbuffered_result = perf::benchmark("Per-line write + flush", [&]() {
        std::ofstream file("test_unbuffered.tmp");
        for (int i = 0; i < LINES; ++i) {
            file << line;
            file.flush(); // 每次都刷盘 - 极慢!
        }
    }, 3);

    // BETTER: 逐行写但不手动 flush
    auto buffered_result = perf::benchmark("Per-line write (no flush)", [&]() {
        std::ofstream file("test_buffered.tmp");
        for (int i = 0; i < LINES; ++i) {
            file << line;
        }
        // 文件关闭时自动 flush
    }, 3);

    // BEST: 先拼接到内存，一次写入
    auto batched_result = perf::benchmark("Batched write (memory first)", [&]() {
        std::string buffer;
        buffer.reserve(LINES * line.size());
        for (int i = 0; i < LINES; ++i) {
            buffer += line;
        }
        std::ofstream file("test_batched.tmp");
        file.write(buffer.data(), buffer.size());
    }, 3);

    std::cout << "\n--- Comparison ---\n";
    perf::printComparison("Unbuffered", unbuffered_result.avg_us,
                          "Batched", batched_result.avg_us);

    // 清理临时文件
    std::remove("test_unbuffered.tmp");
    std::remove("test_buffered.tmp");
    std::remove("test_batched.tmp");
}

// ============================================================
// 测试2: 同步读取 vs 异步读取 (使用 std::async)
// ============================================================
void test_sync_vs_async_read() {
    perf::printHeader("Sync vs Async File Read");

    // 先创建测试文件
    constexpr int FILE_COUNT = 5;
    constexpr int FILE_SIZE = 1024 * 512; // 512KB per file
    std::string data(FILE_SIZE, 'X');

    for (int i = 0; i < FILE_COUNT; ++i) {
        std::ofstream f("test_async_" + std::to_string(i) + ".tmp", std::ios::binary);
        f.write(data.data(), data.size());
    }

    // 同步: 逐个读取
    auto sync_result = perf::benchmark("Sync sequential read", [&]() {
        for (int i = 0; i < FILE_COUNT; ++i) {
            std::ifstream f("test_async_" + std::to_string(i) + ".tmp", std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        }
    }, 10);

    // 异步: 并行读取
    auto async_result = perf::benchmark("Async parallel read", [&]() {
        std::vector<std::future<std::string>> futures;
        for (int i = 0; i < FILE_COUNT; ++i) {
            futures.push_back(std::async(std::launch::async, [i]() {
                std::ifstream f("test_async_" + std::to_string(i) + ".tmp", std::ios::binary);
                return std::string((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
            }));
        }
        for (auto& fut : futures) {
            std::string result = fut.get();
        }
    }, 10);

    perf::printComparison("Sync", sync_result.avg_us,
                          "Async", async_result.avg_us);

    // 清理
    for (int i = 0; i < FILE_COUNT; ++i) {
        std::remove(("test_async_" + std::to_string(i) + ".tmp").c_str());
    }
}

// ============================================================
// 测试3: 读取方式对比 - fread vs ifstream vs ReadFile
// ============================================================
void test_read_methods() {
    perf::printHeader("Read Methods Comparison");

    constexpr int FILE_SIZE = 1024 * 1024 * 4; // 4MB
    std::string data(FILE_SIZE, 'A');
    {
        std::ofstream f("test_read_method.tmp", std::ios::binary);
        f.write(data.data(), data.size());
    }

    // C++ ifstream
    auto ifstream_result = perf::benchmark("std::ifstream", [&]() {
        std::ifstream f("test_read_method.tmp", std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    }, 10);

    // C fread
    auto fread_result = perf::benchmark("C fread", [&]() {
        FILE* f = fopen("test_read_method.tmp", "rb");
        if (f) {
            std::vector<char> buf(FILE_SIZE);
            fread(buf.data(), 1, FILE_SIZE, f);
            fclose(f);
        }
    }, 10);

#ifdef _WIN32
    // Windows ReadFile
    auto readfile_result = perf::benchmark("Win32 ReadFile", [&]() {
        HANDLE hFile = CreateFileA(
            "test_read_method.tmp",
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, // 提示系统优化预读
            nullptr
        );
        if (hFile != INVALID_HANDLE_VALUE) {
            std::vector<char> buf(FILE_SIZE);
            DWORD bytesRead;
            ReadFile(hFile, buf.data(), FILE_SIZE, &bytesRead, nullptr);
            CloseHandle(hFile);
        }
    }, 10);

    std::cout << "\n  Win32 ReadFile + FILE_FLAG_SEQUENTIAL_SCAN 通常最快\n";
    std::cout << "  这个 flag 告诉系统可以积极预读\n";
#endif

    std::remove("test_read_method.tmp");
}

#ifdef _WIN32
// ============================================================
// 测试4: Windows 内存映射文件
// ============================================================
void test_memory_mapped_file() {
    perf::printHeader("Memory-Mapped File (Windows)");

    constexpr int FILE_SIZE = 1024 * 1024 * 4; // 4MB
    std::string data(FILE_SIZE, 'M');
    {
        std::ofstream f("test_mmap.tmp", std::ios::binary);
        f.write(data.data(), data.size());
    }

    // 普通读取
    auto normal_result = perf::benchmark("Normal ReadFile", [&]() {
        HANDLE hFile = CreateFileA("test_mmap.tmp", GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            std::vector<char> buf(FILE_SIZE);
            DWORD bytesRead;
            ReadFile(hFile, buf.data(), FILE_SIZE, &bytesRead, nullptr);
            CloseHandle(hFile);
        }
    }, 10);

    // 内存映射
    auto mmap_result = perf::benchmark("Memory-mapped file", [&]() {
        HANDLE hFile = CreateFileA("test_mmap.tmp", GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            HANDLE hMapping = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (hMapping) {
                void* pView = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
                if (pView) {
                    // 直接访问映射内存，无需复制
                    volatile char c = static_cast<const char*>(pView)[FILE_SIZE - 1];
                    (void)c;
                    UnmapViewOfFile(pView);
                }
                CloseHandle(hMapping);
            }
            CloseHandle(hFile);
        }
    }, 10);

    perf::printComparison("Normal read", normal_result.avg_us,
                          "Memory-mapped", mmap_result.avg_us);

    std::cout << "  内存映射优势: 延迟加载，只读取实际访问的页面\n"
              << "  适合: 大文件随机读取、多进程共享数据\n";

    std::remove("test_mmap.tmp");
}
#endif

int main() {
    perf::initConsole();
    std::cout << "===== 04: Blocking I/O Tutorial =====\n";

    test_write_strategies();
    test_sync_vs_async_read();
    test_read_methods();

#ifdef _WIN32
    test_memory_mapped_file();
#endif

    std::cout << "\n===== 优化建议 =====\n"
              << "1. 不要在主线程做文件 I/O (用 async 或后台线程)\n"
              << "2. 批量写入: 先缓冲到内存，再一次写入\n"
              << "3. 避免频繁 flush (只在关键数据写入后 flush)\n"
              << "4. Windows: 使用 FILE_FLAG_SEQUENTIAL_SCAN 优化顺序读取\n"
              << "5. 大文件考虑内存映射 (Memory-mapped file)\n"
              << "6. 使用 Windows Performance Recorder 分析 I/O 延迟\n";

    return 0;
}
