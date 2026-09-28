#include "pch.h"
#include "UIEditorInteraction.h"
#include "UICommitManager.h"

#include <algorithm>

namespace IAD::UI {
    bool UIEditorInteraction::DrawPaneSplitter(
        const char* a_id,
        float& a_width,
        float a_minWidth,
        float a_maxWidth)
    {
        ImGui::SameLine();
        ImGui::InvisibleButton(a_id, ImVec2(4.0f, ImGui::GetContentRegionAvail().y));

        const float maxWidth = std::max(a_minWidth, a_maxWidth);
        bool changed = false;
        if (ImGui::IsItemActive()) {
            const float previousWidth = a_width;
            a_width = std::clamp(a_width + ImGui::GetIO().MouseDelta.x, a_minWidth, maxWidth);
            changed = a_width != previousWidth;
            if (changed) {
                UICommitManager::RequestINISettingsSave();
            }
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SameLine();
        return changed;
    }

    void UIEditorInteraction::ClampPaneWidth(float& a_width, float a_minWidth, float a_maxWidth)
    {
        a_width = std::clamp(a_width, a_minWidth, std::max(a_minWidth, a_maxWidth));
    }

    void UIEditorInteraction::TrackLiveConfigEdits(bool& a_pendingConfigSave)
    {
        const bool itemActive = ImGui::IsAnyItemActive();
        if (itemActive) {
            a_pendingConfigSave = true;
        }
        else if (a_pendingConfigSave) {
            UICommitManager::RequestConfigSave();
            a_pendingConfigSave = false;
        }
    }
}
