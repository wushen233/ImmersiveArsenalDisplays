#include "pch.h"
#include "UIRecordSelectionState.h"

namespace IAD::UI {
    bool UIRecordSelectionState::Select(std::string_view a_key)
    {
        if (m_selectedKey == a_key) {
            return false;
        }

        m_selectedKey.assign(a_key);
        return true;
    }

    bool UIRecordSelectionState::IsSelected(std::string_view a_key) const noexcept
    {
        if (m_selectedKey.empty() || a_key.empty()) {
            return false;
        }

        return m_selectedKey == a_key ||
            StripManagedPrefix(m_selectedKey) == StripManagedPrefix(a_key);
    }

    bool UIRecordSelectionState::ClearIfSelected(std::string_view a_key) noexcept
    {
        if (!IsSelected(a_key)) {
            return false;
        }

        m_selectedKey.clear();
        return true;
    }

    bool UIRecordSelectionState::Rename(std::string_view a_oldKey, std::string_view a_newKey)
    {
        if (!IsSelected(a_oldKey)) {
            return false;
        }

        return Select(a_newKey);
    }

    std::string_view UIRecordSelectionState::StripManagedPrefix(std::string_view a_key) noexcept
    {
        if (a_key.rfind("IAD_CME_", 0) == 0 || a_key.rfind("IAD_MOV_", 0) == 0) {
            return a_key.substr(8);
        }
        return a_key;
    }
}
