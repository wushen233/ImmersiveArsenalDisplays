#include "pch.h"
#include "UILocalization.h"
#include "UITransformEditorControls.h"

#include "ImGuiManager.h"
#include "System/HolsterManager.h"

#include <cmath>

namespace IAD::UI {

    bool UITransformEditorControls::DrawTransformWidget(const char* a_label, RE::NiPoint3& a_value, const RE::NiPoint3& a_defaultValue, float a_baseSpeed, bool a_isRotation)
    {
        auto* holsterManager = HolsterManager::GetSingleton();
        bool changed = false;
        ImGui::PushID(a_label);
        ImGui::Text("%s", a_label);
        if (ImGui::BeginPopupContextItem("TransformContextMenu")) {
            if (ImGui::MenuItem(TextLiteral("🔄 重置到默认值 (Reset)"))) {
                a_value = a_defaultValue;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const bool isDefault =
            std::abs(a_value.x - a_defaultValue.x) < 0.001f &&
            std::abs(a_value.y - a_defaultValue.y) < 0.001f &&
            std::abs(a_value.z - a_defaultValue.z) < 0.001f;
        if (isDefault) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
        }

        const float dragSpeed = ImGui::GetIO().KeyShift ? a_baseSpeed * 0.1f : a_baseSpeed;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float totalWidth = ImGui::GetContentRegionAvail().x;
        const float itemWidth = (totalWidth - spacing * 2.0f) / 3.0f;
        const auto handleActive = [holsterManager, a_isRotation](ActiveAxis a_axis) {
            if (ImGui::IsItemActive()) {
                holsterManager->activeUIItemAxis = a_axis;
                ImGuiManager::s_activeUIIsRotation = a_isRotation;
            }
        };

        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 0.5f, 0.5f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.2f, 0.1f, 0.1f, 1.0f });
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##x", &a_value.x, dragSpeed, 0, 0, "X: %.3f")) {
            changed = true;
        }
        handleActive(ActiveAxis::kX);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, spacing);

        ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 1.0f, 0.5f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.2f, 0.1f, 1.0f });
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##y", &a_value.y, dragSpeed, 0, 0, "Y: %.3f")) {
            changed = true;
        }
        handleActive(ActiveAxis::kY);
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, spacing);

        ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.8f, 1.0f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.15f, 0.2f, 1.0f });
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##z", &a_value.z, dragSpeed, 0, 0, "Z: %.3f")) {
            changed = true;
        }
        handleActive(ActiveAxis::kZ);
        ImGui::PopStyleColor(2);

        if (isDefault) {
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
        return changed;
    }

    bool UITransformEditorControls::DrawColorRGBA(const char* a_label, ColorRGBA& a_color)
    {
        float color[4] = { a_color.r, a_color.g, a_color.b, a_color.a };
        if (!ImGui::ColorEdit4(a_label, color)) {
            return false;
        }
        a_color.r = color[0];
        a_color.g = color[1];
        a_color.b = color[2];
        a_color.a = color[3];
        return true;
    }

}
