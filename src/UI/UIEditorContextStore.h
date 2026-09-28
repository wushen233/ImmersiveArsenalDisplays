#pragma once

#include "UIEditorContext.h"

namespace IAD::UI {
    // Owns the persistent editor contexts shared by list, inspector, and
    // world-preview modules. Access remains UI-thread-only, matching the
    // existing ImGui state ownership model.
    class UIEditorContextStore final {
    public:
        static UIEditorContextStore& GetSingleton()
        {
            static UIEditorContextStore instance;
            return instance;
        }

        UIEditorContext& Slot() noexcept { return m_slot; }
        UIEditorContext& Node() noexcept { return m_node; }
        UIEditorContext& Custom() noexcept { return m_custom; }

        const UIEditorContext& Slot() const noexcept { return m_slot; }
        const UIEditorContext& Node() const noexcept { return m_node; }
        const UIEditorContext& Custom() const noexcept { return m_custom; }

        void ClearSelections() noexcept;
        void ResetTransientState() noexcept;

    private:
        UIEditorContextStore() = default;

        UIEditorContext m_slot;
        UIEditorContext m_node;
        UIEditorContext m_custom;
    };
}
