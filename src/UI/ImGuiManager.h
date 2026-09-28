#pragma once
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include "Data/ConfigManager.h" 
#include "UIWindow.h"
#include "UIWindowShell.h"
#include "UIEditorContextStore.h"
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <atomic>

namespace IAD::UI {
    class ImGuiManager {
    public:
        static ImGuiManager& GetSingleton() { static ImGuiManager instance; return instance; }

        // Compatibility name for existing window implementations. Ownership
        // lives in UIEditorContextStore, not in ImGuiManager.
        using WindowState = UIEditorContext;

        // 🌟 跨文件共享的交互状态变量
        static WindowState& s_slotState;
        static WindowState& s_nodeState;
        static WindowState& s_customState;
        static bool s_activeUIIsRotation;

        using Present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        using Present1_t = HRESULT(WINAPI*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
        using ResizeBuffers_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using WndProc_t = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);

        bool Install();
        void ToggleDisplay();
        void RenderCore(IDXGISwapChain* pSwapChain);
        bool IsVisible() const { return m_isVisible; }

        // Window visibility is independent from the docked tab that currently
        // has focus.  UI windows report focus here so reopening the menu can
        // restore the page the user actually used.
        void NotifyWindowFocused(int a_windowIndex, bool a_focused);

        static HRESULT WINAPI Present_Hook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT WINAPI Present1_Hook(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
        static HRESULT WINAPI ResizeBuffers_Hook(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
        static LRESULT WINAPI WndProc_Hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
        static void InstallCursorHooks();
        static BOOL WINAPI ClipCursor_Hook(const RECT* rect);
        static BOOL WINAPI SetCursorPos_Hook(int x, int y);

        void RegisterWindows();

    private:
        ImGuiManager() = default;

        void DrawMainMenuBar();
        void DrawImportExportWindow();
        void DrawAllModalsAndPopups();
        bool DrawFlagMatrix(uint32_t& flags);

        bool m_isInit = false;
        std::atomic_bool m_isVisible = false;
        int m_lastFocusedWindowIndex = 1;
        int m_pendingFocusWindowIndex = -1;
        ImFont* m_font = nullptr;

        static Present_t m_originalPresent;
        static Present1_t m_originalPresent1;
        static ResizeBuffers_t m_originalResizeBuffers;
        static WndProc_t m_originalWndProc;
        using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
        using SetCursorPos_t = BOOL(WINAPI*)(int, int);
        static ClipCursor_t m_originalClipCursor;
        static SetCursorPos_t m_originalSetCursorPos;

        static HWND m_windowHandle;
        static ID3D11Device* m_pDevice;
        static ID3D11DeviceContext* m_pContext;
        static ID3D11RenderTargetView* m_pRenderTargetView;

        void InitImGui(IDXGISwapChain* pSwapChain);
        void RefreshCursorClip();

        UIWindowShell m_windowShell;
    };
}
