#pragma once

#include <string>
#include <string_view>

namespace IAD::UI {
    // Window-shell state for a record list. The selected key survives detail
    // redraws and provides one managed-name comparison rule to every caller.
    class UIRecordSelectionState final {
    public:
        UIRecordSelectionState() = default;

        bool Select(std::string_view a_key);
        bool IsSelected(std::string_view a_key) const noexcept;
        bool ClearIfSelected(std::string_view a_key) noexcept;
        bool Rename(std::string_view a_oldKey, std::string_view a_newKey);

        void Clear() noexcept { m_selectedKey.clear(); }
        bool Empty() const noexcept { return m_selectedKey.empty(); }
        const std::string& Selected() const noexcept { return m_selectedKey; }

        static std::string_view StripManagedPrefix(std::string_view a_key) noexcept;

    private:
        std::string m_selectedKey;
    };
}
