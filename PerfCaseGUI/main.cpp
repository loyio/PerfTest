/*
 * 05_imgui_demo/main.cpp - ImGui 性能测试平台
 *
 * 基于 PerfCase 注册框架的窗口化性能测试工具。
 * 所有性能用例通过 ADD_PERF_CASE 宏自动注册，
 * 链接后即可在界面中选择和执行。
 *
 * 后端: Win32 + DirectX 11 + Dear ImGui
 */

#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "perf_case.h"
#include "tracy_integration.h"

#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================
// DirectX 11 全局状态
// ============================================================
static ID3D11Device*            g_pd3dDevice           = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*          g_pSwapChain           = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static UINT                     g_ResizeWidth  = 0;
static UINT                     g_ResizeHeight = 0;

bool  CreateDeviceD3D(HWND hWnd);
void  CleanupDeviceD3D();
void  CreateRenderTarget();
void  CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// 帧时间历史
// ============================================================
struct FrameHistory {
    static constexpr int MAX_SAMPLES = 512;
    float samples[MAX_SAMPLES] = {};
    int   offset = 0;
    int   count  = 0;

    void push(float value) {
        samples[offset] = value;
        offset = (offset + 1) % MAX_SAMPLES;
        if (count < MAX_SAMPLES) count++;
    }

    float average() const {
        if (count == 0) return 0;
        float sum = 0;
        for (int i = 0; i < count; i++) sum += samples[i];
        return sum / count;
    }

    float percentile(float p) const {
        if (count == 0) return 0;
        std::vector<float> sorted(samples, samples + count);
        std::sort(sorted.begin(), sorted.end());
        int idx = static_cast<int>(p / 100.0f * (count - 1));
        return sorted[idx];
    }

    void getOrdered(float* out, int& outCount) const {
        outCount = count;
        for (int i = 0; i < count; i++) {
            int idx = (offset - count + i + MAX_SAMPLES) % MAX_SAMPLES;
            out[i] = samples[idx];
        }
    }
};

static FrameHistory g_frameHistory;

// ============================================================
// 主界面
// ============================================================
static PerfCase* g_selectedCase   = nullptr;
static bool      g_showDemoWindow = false;

void DrawUI(float deltaTime) {
    PERF_ZONE_SCOPED;
    auto& registry = PerfRegistry::get();
    auto  groups   = registry.byCategory();

    float framerate = ImGui::GetIO().Framerate;
    float frametime = 1000.0f / framerate;
    g_frameHistory.push(frametime);

    // --- 左侧: 用例列表 ---
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Perf Cases");

    // 帧率状态栏
    ImGui::Text("FPS: %.0f | Frame: %.2f ms", framerate, frametime);
    ImGui::Text("P99: %.2f ms | Max: %.2f ms",
        g_frameHistory.percentile(99), g_frameHistory.percentile(100));

    // 帧时间图
    {
        float arr[FrameHistory::MAX_SAMPLES];
        int n = 0;
        g_frameHistory.getOrdered(arr, n);
        ImGui::PlotLines("##ft", arr, n, 0, nullptr, 0.0f, 50.0f, ImVec2(-1, 50));
    }

    ImGui::Separator();

    // 分类列表
    for (auto& [category, cases] : groups) {
        if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto* pc : cases) {
                bool selected = (g_selectedCase == pc);
                char label[256];
                if (pc->active) {
                    snprintf(label, sizeof(label), "[ON] %s (%.1f ms)###%p",
                             pc->getName(), pc->lastUpdateMs, (void*)pc);
                } else {
                    snprintf(label, sizeof(label), "%s###%p",
                             pc->getName(), (void*)pc);
                }
                if (ImGui::Selectable(label, selected)) {
                    g_selectedCase = pc;
                }
                // 右键菜单: 快速开关
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(pc->active ? "Deactivate" : "Activate")) {
                        if (pc->active) {
                            pc->onDeactivate();
                            pc->active = false;
                        } else {
                            pc->onActivate();
                            pc->active = true;
                        }
                    }
                    ImGui::EndPopup();
                }
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Deactivate All")) {
        for (auto* pc : registry.allCases()) {
            if (pc->active) {
                pc->onDeactivate();
                pc->active = false;
            }
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("ImGui Demo", &g_showDemoWindow);

    ImGui::End();

    // --- 右侧: 选中用例的控制面板 ---
    ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Case Detail");

    if (g_selectedCase) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[%s]",
                           g_selectedCase->getCategory());
        ImGui::SameLine();
        ImGui::Text("%s", g_selectedCase->getName());
        ImGui::TextWrapped("%s", g_selectedCase->getDescription());
        ImGui::Separator();

        // 开关
        bool wasActive = g_selectedCase->active;
        ImGui::Checkbox("Active", &g_selectedCase->active);
        if (g_selectedCase->active && !wasActive)
            g_selectedCase->onActivate();
        if (!g_selectedCase->active && wasActive)
            g_selectedCase->onDeactivate();

        if (g_selectedCase->active) {
            ImGui::SameLine();
            ImGui::Text("| Update: %.2f ms", g_selectedCase->lastUpdateMs);
        }

        ImGui::Separator();

        // 用例自定义 UI
        g_selectedCase->onDrawUI();

    } else {
        ImGui::TextWrapped("Select a perf case from the left panel.");
        ImGui::TextWrapped("Right-click a case to quickly activate/deactivate.");
    }

    ImGui::End();

    // --- 更新所有激活的用例 ---
    for (auto* pc : registry.allCases()) {
        if (pc->active) {
            PERF_ZONE_SCOPED_N("PerfCase::onUpdate");
            auto start = std::chrono::high_resolution_clock::now();
            pc->onUpdate(deltaTime);
            auto end = std::chrono::high_resolution_clock::now();
            pc->lastUpdateMs = std::chrono::duration<float, std::milli>(end - start).count();
        }
    }

    if (g_showDemoWindow)
        ImGui::ShowDemoWindow(&g_showDemoWindow);
}

// ============================================================
// WinMain
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    SetConsoleOutputCP(CP_UTF8);
    timeBeginPeriod(1);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.lpszClassName  = L"PerfTestImGui";
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"PerfTest - Performance Case Runner",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1400, 800,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 2.0f;
    style.GrabRounding   = 2.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    auto lastTime = std::chrono::high_resolution_clock::now();
    bool running  = true;

    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawUI(dt);

        ImGui::Render();
        const float clear_color[] = { 0.06f, 0.06f, 0.08f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
        PERF_FRAME_MARK;
    }

    for (auto* pc : PerfRegistry::get().allCases()) {
        if (pc->active) { pc->onDeactivate(); pc->active = false; }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    timeEndPeriod(1);
    return 0;
}

// ============================================================
// DirectX 11
// ============================================================
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.Flags             = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hWnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, fls, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext)))
        return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)       { g_pSwapChain->Release();       g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBuf = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBuf));
    if (pBuf) {
        g_pd3dDevice->CreateRenderTargetView(pBuf, nullptr, &g_mainRenderTargetView);
        pBuf->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth  = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
