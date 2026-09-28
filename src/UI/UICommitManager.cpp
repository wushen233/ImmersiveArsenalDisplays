#include "pch.h"
#include "UICommitManager.h"

#include "Data/ConfigManager.h"
#include "UILocalization.h"

#include <imgui.h>

namespace IAD::UI {
    namespace {
        constexpr auto kCommitDebounce = std::chrono::milliseconds(750);
    }

    UICommitManager& UICommitManager::GetSingleton()
    {
        static UICommitManager instance;
        return instance;
    }

    void UICommitManager::RequestConfigSave()
    {
        GetSingleton().Request(true, false);
    }

    void UICommitManager::RequestINISettingsSave()
    {
        GetSingleton().Request(false, true);
    }

    void UICommitManager::Request(bool a_config, bool a_ini)
    {
        std::lock_guard lock(_stateMutex);
        if (a_config) {
            ++_configRequested;
        }
        if (a_ini) {
            ++_iniRequested;
        }
        _deadline = Clock::now() + kCommitDebounce;
    }

    bool UICommitManager::HasPendingChanges() const
    {
        std::lock_guard lock(_stateMutex);
        return _configRequested != _configCommitted || _iniRequested != _iniCommitted;
    }

    void UICommitManager::CommitNow()
    {
        GetSingleton().Commit(true, true);
    }

    void UICommitManager::Commit(bool a_forceConfig, bool a_forceINI)
    {
        auto& manager = GetSingleton();
        std::lock_guard commitLock(manager._commitMutex);

        std::uint64_t configRequested;
        std::uint64_t configCommitted;
        std::uint64_t iniRequested;
        std::uint64_t iniCommitted;
        {
            std::lock_guard stateLock(manager._stateMutex);
            configRequested = manager._configRequested;
            configCommitted = manager._configCommitted;
            iniRequested = manager._iniRequested;
            iniCommitted = manager._iniCommitted;
        }

        const bool saveConfig = a_forceConfig || configRequested != configCommitted;
        const bool saveINI = a_forceINI || iniRequested != iniCommitted;
        if (!saveConfig && !saveINI) {
            return;
        }

        auto* config = ConfigManager::GetSingleton();
        if (saveConfig) {
            config->SaveConfig();
        }
        if (saveINI) {
            config->SaveINISettings();
        }

        {
            std::lock_guard stateLock(manager._stateMutex);
            if (manager._configCommitted < configRequested) {
                manager._configCommitted = configRequested;
            }
            if (manager._iniCommitted < iniRequested) {
                manager._iniCommitted = iniRequested;
            }
            if (manager._configRequested == configRequested && manager._iniRequested == iniRequested) {
                manager._deadline = {};
            }
        }
    }

    void UICommitManager::FlushIfDue()
    {
        auto& manager = GetSingleton();
        bool due = false;
        {
            std::lock_guard stateLock(manager._stateMutex);
            const bool pending = manager._configRequested != manager._configCommitted || manager._iniRequested != manager._iniCommitted;
            due = pending && manager._deadline != Clock::time_point{} && Clock::now() >= manager._deadline;
        }
        if (due) {
            GetSingleton().Commit(false, false);
        }
    }

    void UICommitManager::DrawStatus() const
    {
        if (!HasPendingChanges()) {
            return;
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), Text("status.unsaved"));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(Text("status.unsaved_tooltip"));
        }
    }
}
