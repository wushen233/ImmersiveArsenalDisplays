#include "pch.h"
#include "UIEditorContextStore.h"

namespace IAD::UI {
    void UIEditorContextStore::ClearSelections() noexcept
    {
        m_slot.selection.Clear();
        m_node.selection.Clear();
        m_custom.selection.Clear();
    }

    void UIEditorContextStore::ResetTransientState() noexcept
    {
        m_slot.inspectorNavigation = {};
        m_node.inspectorNavigation = {};
        m_custom.inspectorNavigation = {};

        m_slot.pendingConfigSave = false;
        m_node.pendingConfigSave = false;
        m_custom.pendingConfigSave = false;
        m_slot.wasDraggingTransform = false;
        m_slot.wasDraggingMesh = false;
        m_slot.wasDraggingPhysics = false;
        m_node.wasDraggingTransform = false;
        m_node.wasDraggingMesh = false;
        m_node.wasDraggingPhysics = false;
        m_custom.wasDraggingTransform = false;
        m_custom.wasDraggingMesh = false;
        m_custom.wasDraggingPhysics = false;
    }
}
