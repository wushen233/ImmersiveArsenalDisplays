#pragma once
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include "Data/ConfigManager.h" 
#include "UIWindow.h"
#include <vector>
#include <memory>
#include <string>
#include <set>
#include <atomic>

namespace IAD::UI {
    enum class MeshEditMode { kWeapon = 0, kHolster = 1, kMagazine = 2, kModelGroup = 3 };

    class ImGuiManager {
    public:
        static ImGuiManager& GetSingleton() { static ImGuiManager instance; return instance; }

        // 🌟 将 WindowState 放在这里，彻底杜绝找不到标识符的问题
        struct WindowState {
            bool isOpen = false;
            uint32_t currentID = 0;
            ConfigScope scope = ConfigScope::kGlobal;
			bool scopeTabInitialized = false;
            uint32_t id = 0;
            int targetFilter = 0;
            int genderEdit = 0;
            bool syncGender = false;
            MeshEditMode meshMode = MeshEditMode::kWeapon;
        };

        // 🌟 跨文件共享的交互状态变量
        static WindowState s_slotState;
        static WindowState s_nodeState;
        static WindowState s_customState;
        static std::string s_selectedSlot;
        static std::string s_selectedNode;
        static std::string s_selectedCustom;
        static bool s_activeUIIsRotation;

        using Present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        using Present1_t = HRESULT(WINAPI*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
        using ResizeBuffers_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using WndProc_t = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);

        bool Install();
        void ToggleDisplay();
        void RenderCore(IDXGISwapChain* pSwapChain);
        bool IsVisible() const { return m_isVisible; }

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

        std::vector<std::unique_ptr<UIWindow>> m_windows;
    };
}
