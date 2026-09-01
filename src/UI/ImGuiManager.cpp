#include "pch.h" 
#include "ImGuiManager.h" 
#include "Data/ConfigManager.h"
#include "System/HolsterManager.h"
#include "System/KeyBindStateManager.h"
#include "UISettingsWindow.h"
#include "UIConfigWindows.h" 
#include "UILogWindow.h" 

#include <RE/C/ControlMap.h>
#include <RE/M/MenuCursor.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/P/ProcessLists.h>
#include <RE/N/NiCamera.h>
#include <RE/T/TESCamera.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <spdlog/spdlog.h> 
#include <detours.h> // 引入微软的 Detours 库

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace IAD::UI {
    ImGuiManager::Present_t ImGuiManager::m_originalPresent = nullptr;
    ImGuiManager::Present1_t ImGuiManager::m_originalPresent1 = nullptr;
    ImGuiManager::ResizeBuffers_t ImGuiManager::m_originalResizeBuffers = nullptr;
    ImGuiManager::WndProc_t ImGuiManager::m_originalWndProc = nullptr;
    ImGuiManager::ClipCursor_t ImGuiManager::m_originalClipCursor = nullptr;
    ImGuiManager::SetCursorPos_t ImGuiManager::m_originalSetCursorPos = nullptr;
    HWND ImGuiManager::m_windowHandle = nullptr;
    ID3D11Device* ImGuiManager::m_pDevice = nullptr;
    ID3D11DeviceContext* ImGuiManager::m_pContext = nullptr;
    ID3D11RenderTargetView* ImGuiManager::m_pRenderTargetView = nullptr;

    std::string ImGuiManager::s_selectedSlot = "";
    std::string ImGuiManager::s_selectedNode = "";
    std::string ImGuiManager::s_selectedCustom = "";
    bool ImGuiManager::s_activeUIIsRotation = false;

    ImGuiManager::WindowState ImGuiManager::s_slotState;
    ImGuiManager::WindowState ImGuiManager::s_nodeState;
    ImGuiManager::WindowState ImGuiManager::s_customState;

    static bool s_isCameraPanning = false;
    static std::string s_ieSelectedFile = "";
    static char s_ieInputBuffer[64] = "";
    static uint32_t s_matrixFlags = 0xFFFFFFFF;
    static int s_importMode = 0;
    static bool s_skipTempRef = true;

    namespace {
        std::uintptr_t* FindGameIATSlot(const char* dllName, const char* functionName) {
            auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
            if (!base) return nullptr;

            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

            const auto& importDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (!importDirectory.VirtualAddress) return nullptr;

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
            for (; descriptor->Name; ++descriptor) {
                const auto* importedModule = reinterpret_cast<const char*>(base + descriptor->Name);
                if (_stricmp(importedModule, dllName) != 0) continue;

                auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
                auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
                if (!nameThunk || !addressThunk) return nullptr;

                for (; nameThunk->u1.AddressOfData; ++nameThunk, ++addressThunk) {
                    if (nameThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
                    auto* importName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
                    if (std::strcmp(reinterpret_cast<const char*>(importName->Name), functionName) == 0) {
                        return reinterpret_cast<std::uintptr_t*>(&addressThunk->u1.Function);
                    }
                }
            }
            return nullptr;
        }
    }

    static bool s_triggerImport = false;
    static bool s_triggerExportNew = false;
    static bool s_triggerExportOverwrite = false;
    static bool s_triggerRename = false;
    static bool s_triggerDelete = false;
    static bool s_triggerDefaultImport = false;
    static bool s_triggerDefaultExport = false;
    static bool s_showImportExportWindow = false;

    static void DrawSplashScreen() {
        static float s_splashStartTime = -1.0f;
        static bool s_showSplash = true;
        if (!s_showSplash) return;
        if (s_splashStartTime < 0.0f) s_splashStartTime = static_cast<float>(ImGui::GetTime());
        float elapsed = static_cast<float>(ImGui::GetTime()) - s_splashStartTime;
        const float fadeIn = 0.5f; const float stay = 15.0f; const float fadeOut = 1.5f;
        if (elapsed > fadeIn + stay + fadeOut) { s_showSplash = false; return; }

        float alpha = 1.0f;
        if (elapsed < fadeIn) alpha = elapsed / fadeIn;
        else if (elapsed > fadeIn + stay) alpha = 1.0f - ((elapsed - fadeIn - stay) / fadeOut);

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 30.0f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.8f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 15.0f));

        ImGuiWindowFlags splashFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("IAD_Splash", nullptr, splashFlags)) {
            if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                ImVec2 currentPos = ImGui::GetWindowPos();
                ImGui::SetWindowPos("IAD_Splash", { currentPos.x + delta.x, currentPos.y + delta.y });
            }
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Immersive Arsenal Displays (IAD) v3.0");
            ImGui::TextDisabled("底层框架与挂载模块已成功加载。");
            ImGui::Separator();
            auto* config = ConfigManager::GetSingleton();
            const auto inputSettings = config->GetInputSettingsSnapshot();
            std::string hotkeyStr = "";
            if (inputSettings.editorModifier & 1) hotkeyStr += "Ctrl + ";
            if (inputSettings.editorModifier & 2) hotkeyStr += "Shift + ";
            if (inputSettings.editorModifier & 4) hotkeyStr += "Alt + ";
            struct KeyMap { const char* name; uint32_t vk; };
            static const KeyMap availableKeys[] = {
                {"F1", 0x70}, {"F2", 0x71}, {"F3", 0x72}, {"F4", 0x73}, {"F5", 0x74}, {"F6", 0x75},
                {"F7", 0x76}, {"F8", 0x77}, {"F9", 0x78}, {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
                {"Tab", 0x09}, {"Tilde (~)", 0xC0}, {"Insert", 0x2D}, {"Delete", 0x2E},
                {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22},
                {"A", 0x41}, {"B", 0x42}, {"C", 0x43}, {"D", 0x44}, {"E", 0x45}, {"F", 0x46}, {"G", 0x47},
                {"H", 0x48}, {"I", 0x49}, {"J", 0x4A}, {"K", 0x4B}, {"L", 0x4C}, {"M", 0x4D}, {"N", 0x4E},
                {"O", 0x4F}, {"P", 0x50}, {"Q", 0x51}, {"R", 0x52}, {"S", 0x53}, {"T", 0x54}, {"U", 0x55},
                {"V", 0x56}, {"W", 0x57}, {"X", 0x58}, {"Y", 0x59}, {"Z", 0x5A}
            };
            std::string currentKeyName = "未知按键";
            for (auto& k : availableKeys) { if (k.vk == inputSettings.editorHotkey) { currentKeyName = k.name; break; } }
            hotkeyStr += currentKeyName;
            ImGui::Text("请按下 [ %s ] 键打开可视化配置菜单", hotkeyStr.c_str());
            if (alpha > 0.8f) {
                ImGui::Spacing(); ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.8f * alpha), "按住此处可拖动此提示框");
            }
        }
        ImGui::End(); ImGui::PopStyleVar(4);
    }

    void ImGuiManager::DrawMainMenuBar() {
        auto* config = ConfigManager::GetSingleton();
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("文件")) {
                if (ImGui::MenuItem("导入 / 导出全局快照")) {
                    config->uiProfileRequestedTab = 1;
                    config->uiShowProfiles = true;
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("用户基准配置")) {
                    if (ImGui::MenuItem("恢复已保存的用户基准")) s_triggerDefaultImport = true;
                    if (ImGui::MenuItem("将当前配置保存为用户基准")) s_triggerDefaultExport = true;
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("保存全部配置")) {
                    config->SaveConfig();
                    config->SaveINISettings();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("关闭配置菜单")) ToggleDisplay();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("视图")) {
                if (ImGui::BeginMenu("显示管理")) {
                    ImGui::MenuItem("装备插槽", nullptr, &config->uiShowSlots);
                    ImGui::MenuItem("专属展示", nullptr, &config->uiShowCustoms);
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("挂载节点", nullptr, &config->uiShowNodes);
                ImGui::MenuItem("表单过滤器", nullptr, &config->uiShowFilters);
                ImGui::Separator();
                ImGui::MenuItem("骨骼可视化", nullptr, &config->uiShowVisualizer);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("工具")) {
                if (ImGui::BeginMenu("预设编辑器")) {
                    if (ImGui::MenuItem("打开预设库")) { config->uiProfileRequestedTab = 0; config->uiShowProfiles = true; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("装备插槽预设")) { config->uiProfileManagedCategory = 0; config->uiProfileRequestedTab = 0; config->uiShowProfiles = true; }
                    if (ImGui::MenuItem("挂载节点预设")) { config->uiProfileManagedCategory = 1; config->uiProfileRequestedTab = 0; config->uiShowProfiles = true; }
                    if (ImGui::MenuItem("专属展示预设")) { config->uiProfileManagedCategory = 2; config->uiProfileRequestedTab = 0; config->uiShowProfiles = true; }
                    if (ImGui::MenuItem("表单过滤器预设")) { config->uiProfileManagedCategory = 8; config->uiProfileRequestedTab = 0; config->uiShowProfiles = true; }
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("IAD 系统设置", nullptr, &config->uiShowSettings);
                ImGui::MenuItem("日志", nullptr, &UILogWindow::GetSingleton()->m_show);
                if (ImGui::BeginMenu("诊断")) {
                    ImGui::MenuItem("骨骼扫描仪", nullptr, &config->uiShowBoneScanner);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("帮助")) {
                ImGui::TextDisabled("Immersive Arsenal Displays");
                ImGui::TextDisabled("Fallout 4 F4SE Plugin");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("操作")) {
                if (ImGui::MenuItem("强制刷新显示")) HolsterManager::GetSingleton()->ForceRefreshAll();
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        DrawAllModalsAndPopups();
    }

    bool ImGuiManager::DrawFlagMatrix(uint32_t& flags) {
        bool changed = false;
        auto DrawCb = [&](const char* label, ConfigManager::SerFlags flag) {
            bool checked = (flags & static_cast<uint32_t>(flag)) != 0;
            if (ImGui::Checkbox(label, &checked)) {
                if (checked) flags |= static_cast<uint32_t>(flag); else flags &= ~static_cast<uint32_t>(flag); changed = true;
            }
            };
        if (ImGui::BeginTable("FlagMatrix", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("类别"); ImGui::TableSetupColumn("全局"); ImGui::TableSetupColumn("角色"); ImGui::TableSetupColumn("NPC / 种族"); ImGui::TableHeadersRow();
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("插槽 (Slots)"); ImGui::TableSetColumnIndex(1); DrawCb("##sg", ConfigManager::SerFlags::kSlotGlobal); ImGui::TableSetColumnIndex(2); DrawCb("##sa", ConfigManager::SerFlags::kSlotActor); ImGui::TableSetColumnIndex(3); DrawCb("NPC##sn", ConfigManager::SerFlags::kSlotNPC); ImGui::SameLine(); DrawCb("Race##sr", ConfigManager::SerFlags::kSlotRace);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("节点 (Nodes)"); ImGui::TableSetColumnIndex(1); DrawCb("##ng", ConfigManager::SerFlags::kNodeGlobal); ImGui::TableSetColumnIndex(2); DrawCb("##na", ConfigManager::SerFlags::kNodeActor); ImGui::TableSetColumnIndex(3); DrawCb("NPC##nn", ConfigManager::SerFlags::kNodeNPC); ImGui::SameLine(); DrawCb("Race##nr", ConfigManager::SerFlags::kNodeRace);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("专属 (Customs)"); ImGui::TableSetColumnIndex(1); DrawCb("##cg", ConfigManager::SerFlags::kCustomGlobal); ImGui::TableSetColumnIndex(2); DrawCb("##ca", ConfigManager::SerFlags::kCustomActor); ImGui::TableSetColumnIndex(3); DrawCb("NPC##cn", ConfigManager::SerFlags::kCustomNPC); ImGui::SameLine(); DrawCb("Race##cr", ConfigManager::SerFlags::kCustomRace);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "词典 (Filters)"); ImGui::TableSetColumnIndex(1); DrawCb("##ff", ConfigManager::SerFlags::kFormFilters);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        bool toggleAll = (flags == static_cast<uint32_t>(ConfigManager::SerFlags::kAll));
        if (ImGui::Checkbox("全选 / 全不选", &toggleAll)) { flags = toggleAll ? static_cast<uint32_t>(ConfigManager::SerFlags::kAll) : 0; changed = true; }
        return changed;
    }

    void ImGuiManager::DrawImportExportWindow() {
        if (!s_showImportExportWindow) return;
        ImGui::SetNextWindowSize({ 380.0f, 0.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("导入 / 导出 预设配置", &s_showImportExportWindow, ImGuiWindowFlags_NoCollapse)) {
            auto* config = ConfigManager::GetSingleton();
            std::vector<std::string> files = config->GetAvailableExports();
            ImGui::SetNextItemWidth(250.0f);
            if (ImGui::BeginCombo("##filecombo", s_ieSelectedFile.empty() ? "" : s_ieSelectedFile.c_str())) {
                for (auto& f : files) if (ImGui::Selectable(f.c_str(), s_ieSelectedFile == f)) s_ieSelectedFile = f;
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::Text("文件"); ImGui::Spacing();
            if (s_ieSelectedFile.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("删除", { 50.0f, 0.0f })) s_triggerDelete = true; ImGui::SameLine();
            if (ImGui::Button("重命名", { 60.0f, 0.0f })) s_triggerRename = true; ImGui::SameLine();
            if (s_ieSelectedFile.empty()) ImGui::EndDisabled();
            if (ImGui::Button("刷新目录", { 70.0f, 0.0f })) s_ieSelectedFile = "";
            ImGui::Separator(); ImGui::Spacing();
            if (s_ieSelectedFile.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("导入 (Import)", { 100.0f, 0.0f })) s_triggerImport = true; ImGui::SameLine();
            if (s_ieSelectedFile.empty()) ImGui::EndDisabled();
            if (ImGui::Button("导出 (Export)", { 100.0f, 0.0f })) ImGui::OpenPopup("ExportContext"); ImGui::SameLine();
            if (ImGui::Button("关闭 (Close)", { 100.0f, 0.0f })) s_showImportExportWindow = false;
            if (ImGui::BeginPopup("ExportContext")) {
                if (ImGui::MenuItem("新建预设文件")) s_triggerExportNew = true;
                if (!s_ieSelectedFile.empty()) { ImGui::Separator(); if (ImGui::MenuItem("覆盖当前选中文件")) s_triggerExportOverwrite = true; }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    void ImGuiManager::DrawAllModalsAndPopups() {
        auto* config = ConfigManager::GetSingleton();
        if (s_triggerImport) { ImGui::OpenPopup("Confirm##IEImport"); s_matrixFlags = 0xFFFFFFFF; s_triggerImport = false; }
        if (s_triggerExportNew) { ImGui::OpenPopup("Confirm##IENew"); strcpy_s(s_ieInputBuffer, "NewPreset"); s_matrixFlags = 0xFFFFFFFF; s_triggerExportNew = false; }
        if (s_triggerExportOverwrite) { ImGui::OpenPopup("Confirm##IEOvw"); s_matrixFlags = 0xFFFFFFFF; s_triggerExportOverwrite = false; }
        if (s_triggerRename) { ImGui::OpenPopup("Rename"); strcpy_s(s_ieInputBuffer, s_ieSelectedFile.c_str()); s_triggerRename = false; }
        if (s_triggerDelete) { ImGui::OpenPopup("Confirm##IEDelete"); s_triggerDelete = false; }
        if (s_triggerDefaultImport) { ImGui::OpenPopup("Confirm##DCImport"); s_triggerDefaultImport = false; }
        if (s_triggerDefaultExport) { ImGui::OpenPopup("Confirm##DCExport"); s_matrixFlags = 0xFFFFFFFF; s_triggerDefaultExport = false; }

        if (ImGui::BeginPopupModal("Confirm##IEImport", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("确定要导入预设吗？ [%s]", s_ieSelectedFile.c_str()); ImGui::Separator(); DrawFlagMatrix(s_matrixFlags); ImGui::Separator();
            ImGui::Checkbox("跳过覆盖临时引用 (Skip temp ref)", &s_skipTempRef); ImGui::Separator();
            ImGui::RadioButton("覆盖模式 (Overwrite)", &s_importMode, 0); ImGui::SameLine(); ImGui::RadioButton("合并模式 (Merge)", &s_importMode, 1); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (s_matrixFlags == 0) ImGui::BeginDisabled();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { config->ImportPreset(s_ieSelectedFile, s_matrixFlags, s_importMode == 1); HolsterManager::GetSingleton()->ForceRefreshAll(); ImGui::CloseCurrentPopup(); }
            if (s_matrixFlags == 0) ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm##IENew", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("导出到新文件:"); ImGui::Separator(); ImGui::SetNextItemWidth(250.0f); ImGui::InputText("##nf", s_ieInputBuffer, 64); ImGui::Separator(); DrawFlagMatrix(s_matrixFlags); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (s_matrixFlags == 0 || strlen(s_ieInputBuffer) == 0) ImGui::BeginDisabled();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { config->ExportPreset(s_ieInputBuffer, s_matrixFlags); s_ieSelectedFile = s_ieInputBuffer; ImGui::CloseCurrentPopup(); }
            if (s_matrixFlags == 0 || strlen(s_ieInputBuffer) == 0) ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm##IEOvw", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("确定要覆盖该预设吗？ [%s]", s_ieSelectedFile.c_str()); ImGui::Separator(); DrawFlagMatrix(s_matrixFlags); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (s_matrixFlags == 0) ImGui::BeginDisabled();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { config->ExportPreset(s_ieSelectedFile, s_matrixFlags); ImGui::CloseCurrentPopup(); }
            if (s_matrixFlags == 0) ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Rename", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("重命名该预设:"); ImGui::SetNextItemWidth(250.0f); ImGui::InputText("##rn", s_ieInputBuffer, 64); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { if (config->RenameExport(s_ieSelectedFile, s_ieInputBuffer)) s_ieSelectedFile = s_ieInputBuffer; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm##IEDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("确定彻底删除此文件吗？ [%s]", s_ieSelectedFile.c_str()); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { if (config->DeleteExport(s_ieSelectedFile)) s_ieSelectedFile = ""; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm##DCImport", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("恢复用户基准配置");
            ImGui::TextDisabled("这会用你保存的 IAD_DefaultConfigUser 覆盖当前显示规则。");
            ImGui::Separator();
            const auto exports = config->GetAvailableExports();
            const bool hasUserDef = std::find(exports.begin(), exports.end(), "IAD_DefaultConfigUser") != exports.end();
            if (!hasUserDef) ImGui::BeginDisabled();
            if (ImGui::Button("恢复", { 120.0f, 0.0f })) { config->ImportPreset("IAD_DefaultConfigUser", static_cast<uint32_t>(ConfigManager::SerFlags::kAll), false); HolsterManager::GetSingleton()->ForceRefreshAll(); ImGui::CloseCurrentPopup(); }
            if (!hasUserDef) ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm##DCExport", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("确定将当前状态保存为用户默认配置吗？"); ImGui::Separator(); DrawFlagMatrix(s_matrixFlags); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            if (ImGui::Button("确定", { 120.0f, 0.0f })) { config->ExportPreset("IAD_DefaultConfigUser", s_matrixFlags); ImGui::CloseCurrentPopup(); } ImGui::SameLine(); if (ImGui::Button("取消", { 120.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        DrawImportExportWindow();
    }

    void ImGuiManager::RegisterWindows() {
        m_windows.push_back(std::make_unique<UISettingsWindow>());
        m_windows.push_back(std::make_unique<UISlotsWindow>());
        m_windows.push_back(std::make_unique<UINodesWindow>());
        m_windows.push_back(std::make_unique<UICustomsWindow>());
        m_windows.push_back(std::make_unique<UIProfileEditorWindow>());
        m_windows.push_back(std::make_unique<UIProfileSlotsWindow>());
        m_windows.push_back(std::make_unique<UIProfileCustomsWindow>());
        m_windows.push_back(std::make_unique<UIProfileNodesWindow>());
        m_windows.push_back(std::make_unique<UIProfileFormFiltersWindow>());
        m_windows.push_back(std::make_unique<UIProfileModelGroupsWindow>());
        m_windows.push_back(std::make_unique<UIProfileNodeMonitorsWindow>());
        m_windows.push_back(std::make_unique<UIProfileConditionsWindow>());
        m_windows.push_back(std::make_unique<UIProfileTransformsWindow>());
        m_windows.push_back(std::make_unique<UIProfilePhysicsWindow>());
        m_windows.push_back(std::make_unique<UIFiltersWindow>());
        m_windows.push_back(std::make_unique<UIBoneScannerWindow>());
        m_windows.push_back(std::make_unique<UIVisualizerWindow>());

        m_windows.push_back(std::unique_ptr<UIWindow>(UILogWindow::GetSingleton()));

        auto ui_sink = std::make_shared<ImGuiSink<std::mutex>>();
        ui_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        spdlog::default_logger()->sinks().push_back(ui_sink);

        REX::INFO("[IAD] 面向对象 UI 窗口池及日志控制台注册完成！");
    }

    HRESULT WINAPI ImGuiManager::Present_Hook(IDXGISwapChain* sc, UINT si, UINT f) { GetSingleton().RenderCore(sc); return m_originalPresent(sc, si, f); }
    HRESULT WINAPI ImGuiManager::ResizeBuffers_Hook(IDXGISwapChain* sc, UINT bc, UINT w, UINT h, DXGI_FORMAT nf, UINT sf) {
        return m_originalResizeBuffers(sc, bc, w, h, nf, sf);
    }

    void ImGuiManager::ToggleDisplay() {
        m_isVisible = !m_isVisible;
        auto cm = RE::ControlMap::GetSingleton();
        auto mc = RE::MenuCursor::GetSingleton();
        auto* config = ConfigManager::GetSingleton();

        if (m_isVisible) {
			// The consolidated preset library supersedes the old per-category editor
			// windows.  Clear persisted legacy window state so reopening the UI cannot
			// resurrect a second, conflicting preset workflow.
			config->uiShowProfileSlots = false;
			config->uiShowProfileCustoms = false;
			config->uiShowProfileNodes = false;
			config->uiShowProfileFormFilters = false;
			config->uiShowProfileModelGroups = false;
			config->uiShowProfileNodeMonitors = false;
			config->uiShowProfileConditions = false;
			config->uiShowProfileTransforms = false;
			config->uiShowProfilePhysics = false;
			if (config->uiLastClosedWindow >= 9) {
				config->uiLastClosedWindow = 8;
			}

            if (!config->uiShowSlots && !config->uiShowNodes && !config->uiShowCustoms &&
                !config->uiShowProfiles && !config->uiShowFilters && !config->uiShowSettings &&
				!config->uiShowBoneScanner && !config->uiShowVisualizer) {
                switch (config->uiLastClosedWindow) {
                case 1: config->uiShowSlots = true; break;
                case 2: config->uiShowNodes = true; break;
                case 3: config->uiShowCustoms = true; break;
                case 4: config->uiShowFilters = true; break;
                case 5: config->uiShowSettings = true; break;
                case 6: config->uiShowBoneScanner = true; break;
                case 7: config->uiShowVisualizer = true; break;
                case 8: config->uiShowProfiles = true; break;
                default: config->uiShowSlots = true; break;
                }
            }

            ImGui::GetIO().MouseDrawCursor = true;
            if (cm) cm->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor);
            if (mc) mc->RegisterCursor();
            RefreshCursorClip();
        }
        else {
            s_isCameraPanning = false;
            ImGui::GetIO().MouseDrawCursor = false;
            if (cm) { cm->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor); cm->SetIgnoreKeyboardMouse(false); }
            if (mc) mc->UnregisterCursor();
            if (m_originalClipCursor) m_originalClipCursor(nullptr);

            config->SaveConfig();
            config->SaveINISettings();
        }
    }

    void ImGuiManager::InstallCursorHooks() {
        if (!m_originalClipCursor) {
            if (auto* slot = FindGameIATSlot("user32.dll", "ClipCursor")) {
                m_originalClipCursor = reinterpret_cast<ClipCursor_t>(*slot);
                if (m_originalClipCursor) {
                    REL::WriteSafeData(reinterpret_cast<std::uintptr_t>(slot),
                        reinterpret_cast<std::uintptr_t>(&ClipCursor_Hook));
                    REX::INFO("[IAD ImGui] ClipCursor IAT hook installed");
                }
                else {
                    REX::WARN("[IAD ImGui] ClipCursor IAT entry is null");
                }
            }
            else {
                REX::WARN("[IAD ImGui] ClipCursor was not found in the game IAT");
            }
        }

        if (!m_originalSetCursorPos) {
            if (auto* slot = FindGameIATSlot("user32.dll", "SetCursorPos")) {
                m_originalSetCursorPos = reinterpret_cast<SetCursorPos_t>(*slot);
                if (m_originalSetCursorPos) {
                    REL::WriteSafeData(reinterpret_cast<std::uintptr_t>(slot),
                        reinterpret_cast<std::uintptr_t>(&SetCursorPos_Hook));
                    REX::INFO("[IAD ImGui] SetCursorPos IAT hook installed");
                }
                else {
                    REX::WARN("[IAD ImGui] SetCursorPos IAT entry is null");
                }
            }
            else {
                REX::WARN("[IAD ImGui] SetCursorPos was not found in the game IAT");
            }
        }
    }

    void ImGuiManager::RefreshCursorClip() {
        if (!m_isVisible.load(std::memory_order_acquire) || !m_windowHandle || !m_originalClipCursor) return;

        RECT windowRect{};
        if (GetWindowRect(m_windowHandle, &windowRect)) {
            m_originalClipCursor(&windowRect);
        }
    }

    BOOL WINAPI ImGuiManager::ClipCursor_Hook(const RECT* rect) {
        auto& manager = GetSingleton();
        if (manager.m_isVisible.load(std::memory_order_acquire) && !s_isCameraPanning && manager.m_originalClipCursor) {
            RECT windowRect{};
            if (manager.m_windowHandle && GetWindowRect(manager.m_windowHandle, &windowRect)) {
                return manager.m_originalClipCursor(&windowRect);
            }
        }

        return manager.m_originalClipCursor ? manager.m_originalClipCursor(rect) : FALSE;
    }

    BOOL WINAPI ImGuiManager::SetCursorPos_Hook(int x, int y) {
        auto& manager = GetSingleton();
        if (manager.m_isVisible.load(std::memory_order_acquire) && !s_isCameraPanning) {
            return TRUE;
        }

        return manager.m_originalSetCursorPos ? manager.m_originalSetCursorPos(x, y) : FALSE;
    }

    bool ImGuiManager::Install() {
        auto rd = RE::BSGraphics::GetRendererData();
        if (!rd) return false;

        auto sc = reinterpret_cast<IDXGISwapChain*>(rd->renderWindow[0].swapChain);
        if (!sc) return false;

        void** vt = *reinterpret_cast<void***>(sc);
        DWORD oldProtect;

        for (int retries = 0; retries < 10; retries++) {
            __try {
                VirtualProtect(&vt[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
                m_originalPresent = (Present_t)vt[8];
                vt[8] = (void*)Present_Hook;
                VirtualProtect(&vt[8], sizeof(void*), oldProtect, &oldProtect);

                VirtualProtect(&vt[13], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
                m_originalResizeBuffers = (ResizeBuffers_t)vt[13];
                vt[13] = (void*)ResizeBuffers_Hook;
                VirtualProtect(&vt[13], sizeof(void*), oldProtect, &oldProtect);

                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        return false;
    }

    void ImGuiManager::RenderCore(IDXGISwapChain* sc) {
        if (!sc) return;

        if (!m_isInit) {
            auto rd = RE::BSGraphics::GetRendererData();
            if (!rd || !rd->device || !rd->context) return;

            m_pDevice = reinterpret_cast<ID3D11Device*>(rd->device);
            m_pContext = reinterpret_cast<ID3D11DeviceContext*>(rd->context);

            DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
            m_windowHandle = sd.OutputWindow;

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;
            io.IniFilename = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/UI.ini";
            std::string font = "C:\\Windows\\Fonts\\msyh.ttc";
            static const ImWchar glyphRanges[] = {
                0x0020, 0x00FF,
                0x2000, 0x206F,
                0x3000, 0x30FF,
                0x31F0, 0x31FF,
                0xFF00, 0xFFEF,
                0xFFFD, 0xFFFD,
                0x4e00, 0x9FFF,
                0,
            };
            if (std::filesystem::exists(font)) io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f, nullptr, glyphRanges);

            ImGui::StyleColorsDark(); // 后面保留你原本的 ImGui 样式设置代码...
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 8.0f; style.ChildRounding = 6.0f; style.FrameRounding = 6.0f; style.PopupRounding = 8.0f; style.ScrollbarRounding = 6.0f; style.GrabRounding = 6.0f; style.TabRounding = 6.0f;
            style.WindowBorderSize = 1.0f; style.FrameBorderSize = 1.0f; style.PopupBorderSize = 1.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f); style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.40f); style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.96f); style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.50f); style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.30f, 0.60f); style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.40f, 0.70f); style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.94f); style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.98f); style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 0.80f); style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.80f); style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.80f); style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 0.80f); style.Colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 0.80f); style.Colors[ImGuiCol_TabActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

            ImGui_ImplWin32_Init(m_windowHandle); ImGui_ImplDX11_Init(m_pDevice, m_pContext);
            m_originalWndProc = (WndProc_t)SetWindowLongPtr(m_windowHandle, GWLP_WNDPROC, (LONG_PTR)WndProc_Hook);
            InstallCursorHooks();

            RegisterWindows();
            m_isInit = true;
        }

        auto* config = ConfigManager::GetSingleton();
        std::lock_guard<std::recursive_mutex> lock(config->_configMutex);
        auto& io = ImGui::GetIO(); auto cm = RE::ControlMap::GetSingleton(); auto mc = RE::MenuCursor::GetSingleton();

        if (m_isVisible) {
            RefreshCursorClip();
            if (ImGui::IsMouseDown(1) && !s_isCameraPanning) { s_isCameraPanning = true; cm->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor); mc->UnregisterCursor(); io.MouseDrawCursor = false; }
            else if (!ImGui::IsMouseDown(1) && s_isCameraPanning) { s_isCameraPanning = false; cm->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kCursor); mc->RegisterCursor(); io.MouseDrawCursor = true; }
            cm->SetIgnoreKeyboardMouse(!s_isCameraPanning);
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        if (m_isVisible) {
            DrawMainMenuBar();

            for (auto& window : m_windows) {
                window->Draw();
            }

            int openCount = 0; int lastActiveIndex = -1;
            if (config->uiShowSlots) { openCount++; lastActiveIndex = 1; }
            if (config->uiShowNodes) { openCount++; lastActiveIndex = 2; }
            if (config->uiShowCustoms) { openCount++; lastActiveIndex = 3; }
            if (config->uiShowFilters) { openCount++; lastActiveIndex = 4; }
            if (config->uiShowSettings) { openCount++; lastActiveIndex = 5; }
            if (config->uiShowBoneScanner) { openCount++; lastActiveIndex = 6; }
            if (config->uiShowVisualizer) { openCount++; lastActiveIndex = 7; }
            if (config->uiShowProfiles) { openCount++; lastActiveIndex = 8; }
            if (config->uiShowProfileSlots) { openCount++; lastActiveIndex = 9; }
            if (config->uiShowProfileCustoms) { openCount++; lastActiveIndex = 10; }
            if (config->uiShowProfileNodes) { openCount++; lastActiveIndex = 11; }
            if (config->uiShowProfileFormFilters) { openCount++; lastActiveIndex = 12; }
            if (config->uiShowProfileModelGroups) { openCount++; lastActiveIndex = 13; }
            if (config->uiShowProfileNodeMonitors) { openCount++; lastActiveIndex = 14; }
            if (config->uiShowProfileConditions) { openCount++; lastActiveIndex = 15; }
            if (config->uiShowProfileTransforms) { openCount++; lastActiveIndex = 16; }
            if (config->uiShowProfilePhysics) { openCount++; lastActiveIndex = 17; }

            static int s_prevOpenCount = -1;
            static int s_prevLastActiveIndex = 1;
            if (s_prevOpenCount == -1) s_prevOpenCount = openCount;
            if (openCount > 0 && lastActiveIndex > 0) s_prevLastActiveIndex = lastActiveIndex;
            if (s_prevOpenCount > 0 && openCount == 0) {
                config->uiLastClosedWindow = s_prevLastActiveIndex;
                ToggleDisplay();
            }
            s_prevOpenCount = openCount;
        }

        auto* hm = HolsterManager::GetSingleton();
        if (!ImGui::IsAnyItemActive()) hm->activeUIItemAxis = ActiveAxis::kNone;

        std::lock_guard<std::mutex> boxLock(hm->debugBoxMutex);
        if (!hm->activeDebugBoxes.empty() || !hm->activeDebugNodes.empty()) {
            RE::NiCamera* niCamera = nullptr; auto playerCam = RE::PlayerCamera::GetSingleton();
            if (playerCam && playerCam->cameraRoot) {
                std::function<void(RE::NiAVObject*)> FindCamera = [&](RE::NiAVObject* obj) {
                    if (niCamera || !obj) return;
                    const RE::NiRTTI* rtti = obj->GetRTTI();
                    if (rtti && rtti->name) {
                        if (strstr(rtti->name, "NiCamera")) { niCamera = reinterpret_cast<RE::NiCamera*>(obj); return; }
                        if (strstr(rtti->name, "Node") || strstr(rtti->name, "FadeNode")) {
                            RE::NiNode* node = reinterpret_cast<RE::NiNode*>(obj);
                            for (auto& child : node->children) { if (child) FindCamera(child.get()); }
                        }
                    }
                    };
                FindCamera(playerCam->cameraRoot.get());
            }

            if (niCamera) {
                auto drawList = ImGui::GetBackgroundDrawList(); ImVec2 screenSize = ImGui::GetIO().DisplaySize;
                auto WorldToScreen = [&](const RE::NiPoint3& wp, ImVec2& sp) -> bool {
                    float w = niCamera->worldToCam[3][0] * wp.x + niCamera->worldToCam[3][1] * wp.y + niCamera->worldToCam[3][2] * wp.z + niCamera->worldToCam[3][3];
                    if (w < 0.001f) return false; float invW = 1.0f / w;
                    float x = (niCamera->worldToCam[0][0] * wp.x + niCamera->worldToCam[0][1] * wp.y + niCamera->worldToCam[0][2] * wp.z + niCamera->worldToCam[0][3]) * invW;
                    float y = (niCamera->worldToCam[1][0] * wp.x + niCamera->worldToCam[1][1] * wp.y + niCamera->worldToCam[1][2] * wp.z + niCamera->worldToCam[1][3]) * invW;
                    sp.x = ((x + 1.0f) * 0.5f) * screenSize.x; sp.y = ((1.0f - y) * 0.5f) * screenSize.y; return true;
                    };

                for (auto& box : hm->activeDebugBoxes) {
                    if (box.drawBox) {
                        ImVec2 pts[8]; bool allValid = true;
                        for (int i = 0; i < 8; i++) { if (!WorldToScreen(box.corners[i], pts[i])) { allValid = false; break; } }
                        if (allValid) {
                            ImU32 colBox = IM_COL32(0, 255, 128, 150); float thick = 1.5f;
                            drawList->AddLine(pts[0], pts[1], colBox, thick); drawList->AddLine(pts[1], pts[3], colBox, thick); drawList->AddLine(pts[3], pts[2], colBox, thick); drawList->AddLine(pts[2], pts[0], colBox, thick);
                            drawList->AddLine(pts[4], pts[5], colBox, thick); drawList->AddLine(pts[5], pts[7], colBox, thick); drawList->AddLine(pts[7], pts[6], colBox, thick); drawList->AddLine(pts[6], pts[4], colBox, thick);
                            drawList->AddLine(pts[0], pts[4], colBox, thick); drawList->AddLine(pts[1], pts[5], colBox, thick); drawList->AddLine(pts[2], pts[6], colBox, thick); drawList->AddLine(pts[3], pts[7], colBox, thick);
                        }
                    }
                    if (box.drawSphere) {
                        ImU32 colSphere = IM_COL32(255, 165, 0, 150);
                        auto Draw3DCircle = [&](const RE::NiPoint3& center, const RE::NiPoint3& axisU, const RE::NiPoint3& axisV, float radius, ImU32 col) {
                            const int segments = 32; ImVec2 prevSp; bool prevValid = false;
                            for (int i = 0; i <= segments; ++i) {
                                float t = (i * 2.0f * 3.14159265f) / segments;
                                RE::NiPoint3 wp = { center.x + (axisU.x * cosf(t) + axisV.x * sinf(t)) * radius, center.y + (axisU.y * cosf(t) + axisV.y * sinf(t)) * radius, center.z + (axisU.z * cosf(t) + axisV.z * sinf(t)) * radius };
                                ImVec2 sp; bool valid = WorldToScreen(wp, sp);
                                if (valid && prevValid) drawList->AddLine(prevSp, sp, col, 1.5f); prevSp = sp; prevValid = valid;
                            }
                            };
                        RE::NiPoint3 ax = hm->debugSettings.useLocalAxesSpace ? box.axisX : RE::NiPoint3{ 1, 0, 0 }; RE::NiPoint3 ay = hm->debugSettings.useLocalAxesSpace ? box.axisY : RE::NiPoint3{ 0, 1, 0 }; RE::NiPoint3 az = hm->debugSettings.useLocalAxesSpace ? box.axisZ : RE::NiPoint3{ 0, 0, 1 };
                        Draw3DCircle(box.sphereCenter, ay, az, box.sphereRadius, colSphere); Draw3DCircle(box.sphereCenter, ax, az, box.sphereRadius, colSphere); Draw3DCircle(box.sphereCenter, ax, ay, box.sphereRadius, colSphere);
                    }
                    ImVec2 centerPt;
                    if (WorldToScreen(box.center, centerPt)) {
                        if (box.drawPendulum) {
                            ImVec2 virtPt, tipPt; bool validVirt = WorldToScreen(box.virtPos, virtPt); bool validTip = WorldToScreen(box.weaponTip, tipPt);
                            float dist_sq = (box.center.x - box.virtPos.x) * (box.center.x - box.virtPos.x) + (box.center.y - box.virtPos.y) * (box.center.y - box.virtPos.y) + (box.center.z - box.virtPos.z) * (box.center.z - box.virtPos.z);
                            if (validVirt && dist_sq > 0.01f) { drawList->AddCircleFilled(centerPt, 4.0f, IM_COL32(0, 150, 255, 255)); drawList->AddLine(centerPt, virtPt, IM_COL32(0, 150, 255, 150), 2.0f); }
                            if (validVirt && validTip) { drawList->AddCircleFilled(virtPt, 5.0f, IM_COL32(255, 50, 50, 255)); drawList->AddLine(virtPt, tipPt, IM_COL32(255, 50, 50, 200), 2.5f); float time = static_cast<float>(ImGui::GetTime()); float pulse = (sinf(time * 10.0f) + 1.0f) * 0.5f; drawList->AddCircle(tipPt, 6.0f + pulse * 4.0f, IM_COL32(255, 200, 50, 200 - static_cast<int>(pulse * 150.0f)), 0, 2.0f); }
                        }
                        if (hm->debugSettings.showCMEAaxes || hm->debugSettings.showMOVAxes) {
                            float axisLen = 12.0f; ImVec2 pX, pY, pZ;
                            RE::NiPoint3 ax = hm->debugSettings.useLocalAxesSpace ? box.axisX : RE::NiPoint3{ 1, 0, 0 }; RE::NiPoint3 ay = hm->debugSettings.useLocalAxesSpace ? box.axisY : RE::NiPoint3{ 0, 1, 0 }; RE::NiPoint3 az = hm->debugSettings.useLocalAxesSpace ? box.axisZ : RE::NiPoint3{ 0, 0, 1 };
                            RE::NiPoint3 wX = { box.center.x + ax.x * axisLen, box.center.y + ax.y * axisLen, box.center.z + ax.z * axisLen }; RE::NiPoint3 wY = { box.center.x + ay.x * axisLen, box.center.y + ay.y * axisLen, box.center.z + ay.z * axisLen }; RE::NiPoint3 wZ = { box.center.x + az.x * axisLen, box.center.y + az.y * axisLen, box.center.z + az.z * axisLen };
                            if (WorldToScreen(wX, pX)) drawList->AddLine(centerPt, pX, IM_COL32(255, 50, 50, 255), 2.5f);
                            if (WorldToScreen(wY, pY)) drawList->AddLine(centerPt, pY, IM_COL32(50, 255, 50, 255), 2.5f);
                            if (WorldToScreen(wZ, pZ)) drawList->AddLine(centerPt, pZ, IM_COL32(50, 100, 255, 255), 2.5f);
                        }
                    }
                    if (box.drawAngular) {
                        float L = box.visualProbeLength; RE::NiPoint3 apexPos = box.virtPos; RE::NiPoint3 coneBaseCenter = apexPos + (box.axisZ * -L); ImU32 coneCol = IM_COL32(50, 255, 150, 200);
                        const int segments = 32; ImVec2 prevSp; bool prevValid = false;
                        for (int i = 0; i <= segments; ++i) {
                            float t = (static_cast<float>(i) * 2.0f * 3.14159265f) / static_cast<float>(segments);
                            float currentYawLimit = (cosf(t) > 0) ? box.maxYaw : std::abs(box.minYaw); float currentPitchLimit = (sinf(t) > 0) ? box.maxPitch : std::abs(box.minPitch);
                            float rYaw = L * tanf(currentYawLimit * (3.14159265f / 180.0f)); float rPitch = L * tanf(currentPitchLimit * (3.14159265f / 180.0f));
                            RE::NiPoint3 wp = coneBaseCenter + (box.axisX * (rYaw * cosf(t))) + (box.axisY * (rPitch * sinf(t))); ImVec2 sp; bool valid = WorldToScreen(wp, sp);
                            if (valid && prevValid) drawList->AddLine(prevSp, sp, coneCol, 1.5f);
                            if (i % 8 == 0 && valid) { ImVec2 apexSp; if (WorldToScreen(apexPos, apexSp)) drawList->AddLine(apexSp, sp, IM_COL32(50, 255, 150, 60), 1.0f); }
                            prevSp = sp; prevValid = valid;
                        }
                        int numRings = static_cast<int>(L / 10.0f);
                        for (int r = 1; r < numRings; ++r) {
                            float dist = static_cast<float>(r) * 10.0f; RE::NiPoint3 ringCenter = apexPos + (box.axisZ * -dist); ImVec2 pSpRing; bool pVRing = false;
                            for (int i = 0; i <= segments; ++i) {
                                float t = (static_cast<float>(i) * 2.0f * 3.14159265f) / static_cast<float>(segments);
                                float curRYaw = dist * tanf(((cosf(t) > 0) ? box.maxYaw : std::abs(box.minYaw)) * (3.14159265f / 180.0f)); float curRPitch = dist * tanf(((sinf(t) > 0) ? box.maxPitch : std::abs(box.minPitch)) * (3.14159265f / 180.0f));
                                RE::NiPoint3 wp = ringCenter + (box.axisX * (curRYaw * cosf(t))) + (box.axisY * (curRPitch * sinf(t))); ImVec2 sp; bool valid = WorldToScreen(wp, sp);
                                if (valid && pVRing) drawList->AddLine(pSpRing, sp, IM_COL32(50, 255, 150, 80), 1.0f); pSpRing = sp; pVRing = valid;
                            }
                        }
                        RE::NiPoint3 rollCenter = apexPos + (box.axisZ * -(L * 1.1f)); float rollRadius = L * 0.4f; ImU32 rollCol = IM_COL32(200, 100, 255, 200);
                        float tMin = box.minRoll * (3.14159265f / 180.0f); float tMax = box.maxRoll * (3.14159265f / 180.0f);
                        auto getRollPoint = [&](float angle) { return rollCenter + (box.axisY * (rollRadius * cosf(angle))) + (box.axisX * (rollRadius * sinf(angle))); };
                        ImVec2 spCenter;
                        if (WorldToScreen(rollCenter, spCenter)) {
                            int rollSegments = 16; ImVec2 prSp; bool prV = false;
                            for (int i = 0; i <= rollSegments; ++i) {
                                float t = tMin + (tMax - tMin) * (i / (float)rollSegments); RE::NiPoint3 wp = getRollPoint(t); ImVec2 sp; bool valid = WorldToScreen(wp, sp);
                                if (valid && prV) drawList->AddLine(prSp, sp, rollCol, 2.5f);
                                if (valid && i % 4 == 0) drawList->AddLine(spCenter, sp, IM_COL32(200, 100, 255, 60), 1.0f);
                                prSp = sp; prV = valid;
                            }
                        }
                    }
                }

                for (auto& dn : hm->activeDebugNodes) {
                    ImVec2 screenPos, parentScreenPos;
                    if (WorldToScreen(dn.pos, screenPos)) {
                        if (dn.hasParent && WorldToScreen(dn.parentPos, parentScreenPos)) { drawList->AddLine(parentScreenPos, screenPos, IM_COL32(200, 200, 200, 50), 1.0f); }
                        ImU32 nodeCol = IM_COL32(200, 200, 200, 150); bool showName = hm->debugSettings.showVanillaNames; bool showAxis = hm->debugSettings.showVanillaAxes;
                        if (dn.type == DebugNodeType::kCME) { nodeCol = IM_COL32(255, 255, 0, 255); showName = hm->debugSettings.showCMENames; showAxis = hm->debugSettings.showCMEAaxes; }
                        else if (dn.type == DebugNodeType::kMOV) { nodeCol = IM_COL32(0, 255, 255, 255); showName = hm->debugSettings.showMOVNames; showAxis = hm->debugSettings.showMOVAxes; }

                        bool isSelected = false;
                        if (dn.type == DebugNodeType::kCME && dn.name == "IAD_CME_" + s_selectedNode) isSelected = true;
                        if (dn.type == DebugNodeType::kMOV && dn.name == "IAD_MOV_" + s_selectedSlot) isSelected = true;
                        float baseRadius = (dn.type == DebugNodeType::kVanilla) ? 2.0f : 4.0f;
                        if (isSelected) {
                            float time = static_cast<float>(ImGui::GetTime()); float pulse = (sinf(time * 8.0f) + 1.0f) * 0.5f; nodeCol = IM_COL32(255, 128, 0, 255);
                            drawList->AddCircleFilled(screenPos, baseRadius + 1.0f, nodeCol); drawList->AddCircle(screenPos, baseRadius + 3.0f + pulse * 5.0f, IM_COL32(255, 128, 0, 200 - static_cast<int>(pulse * 150.0f)), 0, 2.0f);
                            drawList->AddText(ImVec2(screenPos.x + 8.0f, screenPos.y - 8.0f), IM_COL32(255, 200, 50, 255), dn.name.c_str());
                        }
                        else {
                            drawList->AddCircleFilled(screenPos, baseRadius, nodeCol);
                            if (showName) drawList->AddText(ImVec2(screenPos.x + 5.0f, screenPos.y - 5.0f), nodeCol, dn.name.c_str());
                        }

                        if (showAxis || isSelected) {
                            float axisLen = isSelected ? 18.0f : 8.0f; ImVec2 pX, pY, pZ;
                            RE::NiPoint3 ax = hm->debugSettings.useLocalAxesSpace ? dn.axisX : RE::NiPoint3{ 1, 0, 0 }; RE::NiPoint3 ay = hm->debugSettings.useLocalAxesSpace ? dn.axisY : RE::NiPoint3{ 0, 1, 0 }; RE::NiPoint3 az = hm->debugSettings.useLocalAxesSpace ? dn.axisZ : RE::NiPoint3{ 0, 0, 1 };
                            if (hm->debugSettings.showCMEAaxes || hm->debugSettings.showMOVAxes) {
                                RE::NiPoint3 wX = { dn.pos.x + ax.x * axisLen, dn.pos.y + ax.y * axisLen, dn.pos.z + ax.z * axisLen }; RE::NiPoint3 wY = { dn.pos.x + ay.x * axisLen, dn.pos.y + ay.y * axisLen, dn.pos.z + ay.z * axisLen }; RE::NiPoint3 wZ = { dn.pos.x + az.x * axisLen, dn.pos.y + az.y * axisLen, dn.pos.z + az.z * axisLen };
                                float thickX = (isSelected && hm->activeUIItemAxis == ActiveAxis::kX) ? 6.0f : (isSelected ? 3.0f : 2.0f); float thickY = (isSelected && hm->activeUIItemAxis == ActiveAxis::kY) ? 6.0f : (isSelected ? 3.0f : 2.0f); float thickZ = (isSelected && hm->activeUIItemAxis == ActiveAxis::kZ) ? 6.0f : (isSelected ? 3.0f : 2.0f);
                                ImU32 colX = (isSelected && hm->activeUIItemAxis == ActiveAxis::kX) ? IM_COL32(255, 150, 150, 255) : IM_COL32(255, 50, 50, 255); ImU32 colY = (isSelected && hm->activeUIItemAxis == ActiveAxis::kY) ? IM_COL32(150, 255, 150, 255) : IM_COL32(50, 255, 50, 255); ImU32 colZ = (isSelected && hm->activeUIItemAxis == ActiveAxis::kZ) ? IM_COL32(150, 200, 255, 255) : IM_COL32(50, 100, 255, 255);
                                if (WorldToScreen(wX, pX)) drawList->AddLine(screenPos, pX, colX, thickX); if (WorldToScreen(wY, pY)) drawList->AddLine(screenPos, pY, colY, thickY); if (WorldToScreen(wZ, pZ)) drawList->AddLine(screenPos, pZ, colZ, thickZ);
                            }
                            if (isSelected && s_activeUIIsRotation && hm->activeUIItemAxis != ActiveAxis::kNone) {
                                float ringRadius = 25.0f; ImU32 ringCol = IM_COL32(255, 255, 255, 180); RE::NiPoint3 u, v;
                                if (hm->activeUIItemAxis == ActiveAxis::kX) { u = ay; v = az; ringCol = IM_COL32(255, 50, 50, 200); }
                                else if (hm->activeUIItemAxis == ActiveAxis::kY) { u = ax; v = az; ringCol = IM_COL32(50, 255, 50, 200); }
                                else { u = ax; v = ay; ringCol = IM_COL32(50, 150, 255, 200); }
                                const int segs = 48; ImVec2 p0; bool v0 = false;
                                for (int i = 0; i <= segs; ++i) {
                                    float t = (i * 2.0f * 3.14159265f) / segs; RE::NiPoint3 wp = dn.pos + (u * cosf(t) + v * sinf(t)) * ringRadius; ImVec2 sp;
                                    if (WorldToScreen(wp, sp)) { if (v0) drawList->AddLine(p0, sp, ringCol, 3.0f); p0 = sp; v0 = true; }
                                    else v0 = false;
                                }
                                drawList->AddText(ImVec2(screenPos.x - 30, screenPos.y + 35), ringCol, s_activeUIIsRotation ? "ROTATING" : "MOVING");
                            }
                        }
                    }
                }
            }
        }
        DrawSplashScreen();
        ImGui::Render();
        if (m_pContext) { ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); }
    }

    LRESULT WINAPI ImGuiManager::WndProc_Hook(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
        auto* config = ConfigManager::GetSingleton();
        auto& imGuiMgr = GetSingleton();

		const bool isKeyDown = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
		const bool isKeyUp = msg == WM_KEYUP || msg == WM_SYSKEYUP;
		if (isKeyDown || isKeyUp) {
			const bool isAutoRepeat = isKeyDown && (lp & (1LL << 30)) != 0;
			bool namedKeybindChanged = false;
			if (!isAutoRepeat) {
				// Do not advance display states while the editor owns keyboard focus, but
				// always accept key-up so a UI transition cannot leave a key latched.
				namedKeybindChanged = !imGuiMgr.m_isVisible || isKeyUp ?
					IAD::KeyBindStateManager::GetSingleton()->ProcessKeyEvent(static_cast<std::uint32_t>(wp), isKeyDown) :
					false;
			}
			if (namedKeybindChanged) {
				if (auto* taskInterface = F4SE::GetTaskInterface()) {
					taskInterface->AddTask([]() {
						HolsterManager::GetSingleton()->RequestEvaluateAll();
					});
				}
			}
		}

		if (isKeyDown) {
			const auto modifiersMatch = [](std::uint32_t modifier) {
				if ((modifier & 1) && !(GetKeyState(VK_CONTROL) & 0x8000)) return false;
				if ((modifier & 2) && !(GetKeyState(VK_SHIFT) & 0x8000)) return false;
				if ((modifier & 4) && !(GetKeyState(VK_MENU) & 0x8000)) return false;
				return true;
			};
			const bool isAutoRepeat = (lp & (1LL << 30)) != 0;
			const auto inputSettings = config->GetInputSettingsSnapshot();
			const auto playerBlockHotkey = inputSettings.playerBlockHotkey;
			const auto playerBlockModifier = inputSettings.playerBlockModifier;
			const auto editorHotkey = inputSettings.editorHotkey;
			const auto editorModifier = inputSettings.editorModifier;
			if (!imGuiMgr.m_isVisible && !isAutoRepeat && playerBlockHotkey != 0 &&
				modifiersMatch(playerBlockModifier) && wp == playerBlockHotkey) {
                const auto togglePlayerDisplayBlock = []() {
                    auto* runtimeConfig = ConfigManager::GetSingleton();
                    runtimeConfig->SetPlayerDisplaysBlocked(!runtimeConfig->IsPlayerDisplaysBlocked());
                    runtimeConfig->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                };
                if (auto* taskInterface = F4SE::GetTaskInterface()) {
                    taskInterface->AddTask(togglePlayerDisplayBlock);
                }
                else {
                    togglePlayerDisplayBlock();
                }
                return true;
			}
			if (!imGuiMgr.m_isVisible) {
				if (modifiersMatch(editorModifier) && wp == editorHotkey) { imGuiMgr.ToggleDisplay(); return true; }
            }
            else if (wp == VK_ESCAPE) { imGuiMgr.ToggleDisplay(); return true; }
        }

        if (ImGui::GetCurrentContext()) {
            ImGui_ImplWin32_WndProcHandler(hw, msg, wp, lp);
        }

        if (imGuiMgr.m_isVisible) {
            if (msg == WM_INPUT) {
                RAWINPUT raw;
                UINT dwSize = sizeof(RAWINPUT);
                if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                    if (raw.header.dwType == RIM_TYPEKEYBOARD) {
                        if (raw.data.keyboard.Flags & RI_KEY_BREAK) {
                            return CallWindowProc(m_originalWndProc, hw, msg, wp, lp);
                        }
                    }
                }
                return true;
            }

            if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) {
                if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
                    return CallWindowProc(m_originalWndProc, hw, msg, wp, lp);
                }
                return true;
            }
            if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL || msg == WM_CHAR) return true;
        }
        else {
            if (ImGui::GetCurrentContext()) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.WantCaptureMouse) {
                    if ((msg >= WM_LBUTTONDOWN && msg <= WM_RBUTTONDBLCLK) || msg == WM_MOUSEWHEEL) {
                        return true;
                    }
                }
            }
        }

        return CallWindowProc(m_originalWndProc, hw, msg, wp, lp);
    }
}
