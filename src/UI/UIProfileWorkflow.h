#pragma once

#include "Profile/GlobalProfileManager.h"

namespace IAD::UI {

    class UIProfileWorkflow final {
    public:
        static bool SaveFormFilter(
            IAD::Profile::FormFilterProfileManager& a_manager,
            const std::string& a_name,
            const IAD::FormFilter& a_data);

        static bool ReloadFormFilter(
            IAD::Profile::FormFilterProfileManager& a_manager,
            const std::string& a_name,
            IAD::FormFilter& a_target);

        static bool RenameFormFilter(
            IAD::Profile::FormFilterProfileManager& a_manager,
            const std::string& a_oldName,
            const std::string& a_newName);

        static bool DeleteFormFilter(
            IAD::Profile::FormFilterProfileManager& a_manager,
            const std::string& a_name);
    };

}
