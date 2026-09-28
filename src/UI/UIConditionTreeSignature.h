#pragma once

#include "Data/ConfigManager.h"

#include <string>

namespace IAD::UI {
    class UIConditionTreeSignature final {
    public:
        static std::string Build(const IAD::ConditionNode& a_node);
    };
}
