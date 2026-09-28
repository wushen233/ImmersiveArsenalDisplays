#include "pch.h"
#include "UIInspectorNavigation.h"

namespace IAD::UI {
    int UIInspectorNavigation::ClampIndex(int a_index, int a_maxIndex) noexcept
    {
        return std::clamp(a_index, 0, std::max(0, a_maxIndex));
    }

    bool UIInspectorNavigation::InitializeIndex(
        int& a_currentIndex,
        bool& a_initialized,
        int a_persistedIndex,
        int a_maxIndex) noexcept
    {
        if (a_initialized) {
            return false;
        }

        a_currentIndex = ClampIndex(a_persistedIndex, a_maxIndex);
        a_initialized = true;
        return true;
    }

    bool UIInspectorNavigation::ShouldSelect(
        int a_requestedIndex,
        int a_currentIndex,
        bool a_selectPersisted,
        int a_index) noexcept
    {
        return a_requestedIndex == a_index ||
            (a_selectPersisted && a_currentIndex == a_index);
    }

    bool UIInspectorNavigation::ActivateIndex(int& a_currentIndex, int a_index) noexcept
    {
        if (a_currentIndex == a_index) {
            return false;
        }

        a_currentIndex = a_index;
        return true;
    }

    void UIInspectorNavigation::Request(InspectorNavigationState& a_state, int a_index) noexcept
    {
        a_state.requestedIndex = a_index;
    }

    int UIInspectorNavigation::Pending(const InspectorNavigationState& a_state) noexcept
    {
        return a_state.requestedIndex;
    }

    void UIInspectorNavigation::Consume(InspectorNavigationState& a_state) noexcept
    {
        a_state.requestedIndex = -1;
    }
}
