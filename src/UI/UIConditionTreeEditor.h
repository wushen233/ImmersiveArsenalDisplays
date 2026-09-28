#pragma once

#include "Data/ConfigManager.h"

namespace IAD::UI {
    class UIConditionTreeEditor final {
    public:
        static bool Draw(IAD::ConditionNode& a_node, bool a_isRoot, int a_depth);
        static void DrawKeywordScannerBox(const char* a_id, std::string& a_keyword);
    };
}
