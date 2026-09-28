#pragma once

#include <algorithm>

namespace IAD::UI {
    struct InspectorNavigationState {
        int requestedIndex = -1;
        int tabIndex = 0;
        int primaryTabIndex = 0;
        int sectionIndex = 0;
        bool tabInitialized = false;
        bool primaryTabInitialized = false;
        bool sectionInitialized = false;
    };

    class UIInspectorNavigation final {
    public:
        static int ClampIndex(int a_index, int a_maxIndex) noexcept;

        // Returns true only on the first initialization, allowing the caller
        // to select the persisted value exactly once without fighting ImGui.
        static bool InitializeIndex(
            int& a_currentIndex,
            bool& a_initialized,
            int a_persistedIndex,
            int a_maxIndex) noexcept;

        static bool ShouldSelect(
            int a_requestedIndex,
            int a_currentIndex,
            bool a_selectPersisted,
            int a_index) noexcept;

        static bool ActivateIndex(int& a_currentIndex, int a_index) noexcept;
        static void Request(InspectorNavigationState& a_state, int a_index) noexcept;
        static int Pending(const InspectorNavigationState& a_state) noexcept;
        static void Consume(InspectorNavigationState& a_state) noexcept;
    };
}
