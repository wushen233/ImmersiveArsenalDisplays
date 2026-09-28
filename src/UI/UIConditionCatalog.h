#pragma once

#include <array>

namespace IAD::UI {
    class UIConditionCatalog final {
    public:
        static const std::array<const char*, 132>& Types() noexcept;
    };
}
