#pragma once

#include "Profile/ProfileManager.h"
#include "UIProfileEditorState.h"
#include "UIProfileWorkflow.h"
#include "UILocalization.h"

#include <imgui.h>

#include <cstring>
#include <type_traits>

namespace IAD::UI {

    class UIManagedProfileControls final {
    public:
        template <class T, class ApplyFn, class MergeFn>
        static bool DrawSelector(
            const char* a_id,
            const char* a_label,
            IAD::Profile::ProfileManager<T>& a_manager,
            T& a_target,
            const char* a_defaultName,
            ApplyFn a_applyFn,
            MergeFn a_mergeFn,
            bool a_enableMerge)
        {
            auto& editorState = UIProfileEditorStateStore::Get(a_id);
            auto& selected = editorState.selected;
            auto& status = editorState.status;
            auto& name = editorState.name;

            if (name[0] == '\0') {
                strcpy_s(name.data(), name.size(), a_defaultName);
            }

            if (!a_manager.IsInitialized()) {
                a_manager.Load();
            }

            bool changed = false;

            ImGui::PushID(a_id);
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "%s", a_label);
            ImGui::SameLine(0.0f, 8.0f);

            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##profileCombo", selected.empty() ? TextLiteral("选择预设...") : selected.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (auto& [nameKey, record] : a_manager.Data()) {
                    const bool isSelected = selected == nameKey;
                    const std::string rowLabel = record.modified ? ("* " + nameKey) : nameKey;
                    if (ImGui::Selectable(rowLabel.c_str(), isSelected)) {
                        selected = nameKey;
                        strcpy_s(name.data(), name.size(), nameKey.c_str());
                        status.clear();
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("R##refreshProfileList")) {
                a_manager.Load();
                selected.clear();
                status = TextLiteral("已重新读取预设列表。");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TextLiteral("重新读取预设列表"));
            }

            ImGui::TextDisabled(TextLiteral("名称"));
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##profileName", name.data(), name.size());

            const bool hasName = std::strlen(name.data()) > 0;
            auto* record = a_manager.Find(selected);

            if (!hasName) {
                ImGui::BeginDisabled();
            }
            ImGui::SameLine(0.0f, 6.0f);
            if (ImGui::SmallButton("+##createProfile")) {
                if (a_manager.CreateProfile(name.data(), a_target)) {
                    selected = name.data();
                    status = TextLiteral("已从当前值创建预设。");
                }
                else {
                    status = a_manager.LastError();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TextLiteral("从当前值新建预设"));
            }
            if (!hasName) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (!record) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("S##saveProfile")) {
                record->data = a_target;
                record->MarkModified();
                const bool saved = [&]() {
                    if constexpr (std::is_same_v<T, IAD::FormFilter>) {
                        return UIProfileWorkflow::SaveFormFilter(a_manager, selected, a_target);
                    }
                    else {
                        return a_manager.SaveProfile(selected);
                    }
                }();
                if (saved) {
                    status = Text("profile.status_saved");
                }
                else {
                    status = a_manager.LastError();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TextLiteral("保存当前值到所选预设"));
            }

            ImGui::SameLine();
            const bool applyDisabled = !record || record->IsMergeOnly();
            if (applyDisabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("A##applyProfile")) {
                a_applyFn(a_target, record->data);
                changed = true;
                status = Text("profile.status_applied");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TextLiteral("应用所选预设"));
            }
            if (applyDisabled) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (!record || !a_enableMerge) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("M##mergeProfile")) {
                a_mergeFn(a_target, record->data);
                changed = true;
                status = Text("profile.status_merged");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TextLiteral("将所选预设合并到当前值"));
            }
            if (!record || !a_enableMerge) {
                ImGui::EndDisabled();
            }
            if (!record) {
                ImGui::EndDisabled();
            }

            if (record) {
                if (record->parserErrors) {
                    ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, Text("profile.parser_warning"));
                }
                if (record->description && !record->description->empty()) {
                    ImGui::TextWrapped("%s", record->description->c_str());
                }
            }
            if (!status.empty()) {
                ImGui::TextDisabled("%s", status.c_str());
            }

            ImGui::Separator();
            ImGui::PopID();
            return changed;
        }
    };

}
