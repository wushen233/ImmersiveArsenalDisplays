#include "pch.h"
#include "UIEditCoordinator.h"

#include "System/HolsterManager.h"
#include "UICommitManager.h"

namespace IAD::UI {
    void UIEditCoordinator::RequestConfigChange()
    {
        RequestConfigSave();
        RequestRuntimeRefresh();
    }

    void UIEditCoordinator::RequestConfigSave()
    {
        UICommitManager::RequestConfigSave();
    }

    void UIEditCoordinator::RequestINIChange(bool a_refreshRuntime)
    {
        RequestINISettingsSave();
        if (a_refreshRuntime) {
            RequestRuntimeRefresh();
        }
    }

    void UIEditCoordinator::RequestINISettingsSave()
    {
        UICommitManager::RequestINISettingsSave();
    }

    void UIEditCoordinator::RequestRuntimeRefresh()
    {
        HolsterManager::GetSingleton()->ForceRefreshAll();
    }

    void UIEditCoordinator::CommitNow()
    {
        UICommitManager::CommitNow();
    }
}
