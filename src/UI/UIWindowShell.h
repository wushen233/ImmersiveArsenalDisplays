#pragma once

#include "UIWindow.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace IAD::UI {
    // IED UIMain/UIContext equivalent for the IAD ImGui window layer. The
    // shell owns registration and lifecycle; individual windows own drawing.
    class UIWindowShell {
    public:
        using VisibilityPredicate = std::function<bool()>;

        void Register(
            std::unique_ptr<UIWindow> a_window,
            int a_focusIndex,
            const char* a_titleKey,
            VisibilityPredicate a_isOpen);

        void Initialize();
        void Reset();
        void Draw();

        std::size_t OpenWindowCount() const;
        const char* GetTopLevelWindowTitle(int a_focusIndex) const;

    private:
        struct Entry {
            std::unique_ptr<UIWindow> window;
            int focusIndex = 0;
            std::string titleKey;
            VisibilityPredicate isOpen;
            bool visibilityKnown = false;
            bool open = false;
        };

        std::vector<Entry> m_entries;
    };
}
