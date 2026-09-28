#include "pch.h"
#include "UIProfileWorkflow.h"

#include "Data/ConfigManager.h"
#include "System/HolsterManager.h"
#include "UICommitManager.h"

namespace IAD::UI {

    bool UIProfileWorkflow::SaveFormFilter(
        IAD::Profile::FormFilterProfileManager& a_manager,
        const std::string& a_name,
        const IAD::FormFilter& a_data)
    {
        auto* record = a_manager.Find(a_name);
        if (record == nullptr) {
            return false;
        }

        record->data = a_data;
        record->MarkModified();
        return a_manager.SaveProfile(a_name);
    }

    bool UIProfileWorkflow::ReloadFormFilter(
        IAD::Profile::FormFilterProfileManager& a_manager,
        const std::string& a_name,
        IAD::FormFilter& a_target)
    {
        if (!a_manager.ReloadProfile(a_name)) {
            return false;
        }

        auto* record = a_manager.Find(a_name);
        if (record == nullptr) {
            return false;
        }

        a_target = record->data;
        return true;
    }

    bool UIProfileWorkflow::RenameFormFilter(
        IAD::Profile::FormFilterProfileManager& a_manager,
        const std::string& a_oldName,
        const std::string& a_newName)
    {
        if (!a_manager.RenameProfile(a_oldName, a_newName)) {
            return false;
        }

        auto* config = ConfigManager::GetSingleton();
        if (config->RenameFormFilterReferences(a_oldName, a_newName)) {
            UICommitManager::RequestConfigSave();
        }
        HolsterManager::GetSingleton()->ForceRefreshAll();
        return true;
    }

    bool UIProfileWorkflow::DeleteFormFilter(
        IAD::Profile::FormFilterProfileManager& a_manager,
        const std::string& a_name)
    {
        if (!a_manager.DeleteProfile(a_name)) {
            return false;
        }

        auto* config = ConfigManager::GetSingleton();
        if (config->ClearFormFilterReferences(a_name)) {
            UICommitManager::RequestConfigSave();
        }
        HolsterManager::GetSingleton()->ForceRefreshAll();
        return true;
    }

}
