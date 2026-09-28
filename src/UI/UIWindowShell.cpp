#include "pch.h"
#include "UIWindowShell.h"
#include "UILocalization.h"

namespace IAD::UI {
    void UIWindowShell::Register(
        std::unique_ptr<UIWindow> a_window,
        int a_focusIndex,
        const char* a_titleKey,
        VisibilityPredicate a_isOpen)
    {
        if (!a_window || !a_isOpen) {
            return;
        }

        Entry entry;
        entry.window = std::move(a_window);
        entry.focusIndex = a_focusIndex;
        entry.titleKey = a_titleKey ? a_titleKey : "";
        entry.isOpen = std::move(a_isOpen);
        m_entries.push_back(std::move(entry));
    }

    void UIWindowShell::Initialize()
    {
        for (auto& entry : m_entries) {
            entry.window->Initialize();
        }
    }

    void UIWindowShell::Reset()
    {
        for (auto& entry : m_entries) {
            if (entry.visibilityKnown && entry.open) {
                entry.window->OnClose();
            }
            entry.window->Reset();
            entry.visibilityKnown = false;
            entry.open = false;
        }
    }

    void UIWindowShell::Draw()
    {
        for (auto& entry : m_entries) {
            const bool open = entry.isOpen();
            if (!entry.visibilityKnown) {
                entry.visibilityKnown = true;
                entry.open = open;
                if (open) {
                    entry.window->OnOpen();
                }
            }
            else if (entry.open != open) {
                entry.open = open;
                if (open) {
                    entry.window->OnOpen();
                }
                else {
                    entry.window->OnClose();
                }
            }

            if (open) {
                entry.window->Draw();
            }
        }
    }

    std::size_t UIWindowShell::OpenWindowCount() const
    {
        std::size_t count = 0;
        for (const auto& entry : m_entries) {
            if (entry.isOpen()) {
                ++count;
            }
        }
        return count;
    }

    const char* UIWindowShell::GetTopLevelWindowTitle(int a_focusIndex) const
    {
        for (const auto& entry : m_entries) {
            if (entry.focusIndex == a_focusIndex && !entry.titleKey.empty()) {
                return Text(entry.titleKey.c_str());
            }
        }
        return nullptr;
    }
}
