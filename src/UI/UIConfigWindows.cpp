#include "pch.h"
#include "UIConfigWindows.h"
#include "ImGuiManager.h"
#include "Data/ConfigManager.h"
#include "Profile/GlobalProfileManager.h"
#include "System/HolsterManager.h"
#include "Engine/ConditionSystem.h"
#include "Engine/NodeManager.h"
#include "UIEditCoordinator.h"
#include "UIEditorInteraction.h"
#include "UIConditionTreeSignature.h"
#include "UIConditionTreeEditor.h"
#include "UIFormEditorControls.h"
#include "UITransformEditorControls.h"
#include "UIModelEditorControls.h"
#include "UIProfileEditorState.h"
#include "UIManagedProfileControls.h"
#include "UIProfileWorkflow.h"
#include "UIWorldPreviewEditor.h"
#include "UIWorldPreviewSession.h"
#include "UILocalization.h"
#include "UIEditorContextStore.h"

#include <RE/C/ControlMap.h>
#include <RE/M/MenuCursor.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>          // 注意：旧库叫 TESForms，新库单数化为 TESForm
#include <RE/P/ProcessLists.h>
#include <RE/T/TESDataHandler.h>
#include <RE/E/ENUM_FORM_ID.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace IAD::UI {
	static UIEditorContext& GetSlotEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Slot(); }
	static UIEditorContext& GetNodeEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Node(); }
	static UIEditorContext& GetCustomEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Custom(); }

    static IAD::TransformData s_clipboardTransform;
    static bool s_hasClipboardTransform = false;
    static IAD::PhysicsValues s_clipboardPhysics;
    static bool s_hasClipboardPhysics = false;

    struct BoneNode { std::string name; std::vector<BoneNode> children; };
    static BoneNode s_boneTree;
    static std::vector<std::string> s_flatBoneList;
    static std::string s_selectedBoneName = "";
    static bool s_hasCachedTree = false;

    // 🌟 全局辅助函数声明，强制使用 ImGuiManager:: 作用域的结构体
    static uint32_t GetQueryID(const ImGuiManager::WindowState& state);
    static bool DrawBoneScannerBox(const char* a_label, std::string& a_targetNode);
    static bool DrawCMENodeSelector(const char* a_label, std::string& a_targetNode, const std::vector<NodeDefinition>& localNodes, ConfigScope currentScope, const char* emptyLabel = nullptr);
    bool DrawConditionTreeEditor(IAD::ConditionNode& rootNode, bool showProfileControls);
    static bool DrawWindowHeader(const char* idStr, ImGuiManager::WindowState& state, bool showTarget, bool showGender);
    static bool DrawSkeletonMatchEditor(NodeDefinition::SkeletonMatchConfig& a_match);
    static bool DrawModelGroupAdvancedConfig(ModelGroupEntry& a_group);
    static void DrawFormFilterUI(FormFilter& a_filter, const char* a_id);
    static bool DrawTabEquipment(SlotDefinition& a_slot);
    static bool DrawTabDisplay(SlotDefinition& a_slot);
	static bool DrawSlotEditorTabs(SlotDefinition& a_slot, ImGuiManager::WindowState& a_state, const char* a_idSuffix);
	static bool DrawNodeEditorTabs(NodeDefinition& a_node, ImGuiManager::WindowState& a_state, const char* a_idSuffix);
    static bool DrawCustomEditorTabs(CustomDefinition& a_custom, ImGuiManager::WindowState& a_state, const std::vector<NodeDefinition>& a_nodes, const char* a_idSuffix, bool a_compactSections = false);
    static bool DrawStateMachineEditor(ConfigBase& a_config, const char* a_idSuffix, bool a_isNodeState = false);
    static bool DrawConfigBaseTransform(ConfigBase& a_config, ImGuiManager::WindowState& a_state, const char* idSuffix, bool isNode = false);
    static bool DrawSlotMeshTransform(SlotDefinition& a_slot, ImGuiManager::WindowState& state, const char* idSuffix);
    static bool DrawCustomMeshTransform(CustomDefinition& a_custom, ImGuiManager::WindowState& state, const std::vector<NodeDefinition>& localNodes, const char* idSuffix);
    static bool DrawPhysicsConstraintParams(PhysicsConstraintParams& a_params);
    static bool DrawPhysicsPanel(PhysicsValues& a_phys, ImGuiManager::WindowState* a_state = nullptr);
    static bool DrawConfigBasePhysics(ConfigBase& a_config, ImGuiManager::WindowState& a_state, const char* idSuffix, bool isNode = false);
    static bool DrawPriorityInput(const char* a_id, int& a_priority, float a_width = -1.0f) {
        ImGui::SetNextItemWidth(a_width);
        const bool changed = ImGui::InputScalar(
            a_id,
            ImGuiDataType_S32,
            &a_priority,
            nullptr,
            nullptr,
            "P:%d");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(TextLiteral("优先级：点击数字直接输入整数；本级条目按从高到低排列"));
        }
        return changed;
    }

    static bool ParseInlineInteger(const char* a_text, int& a_value) {
        if (a_text == nullptr || *a_text == '\0') return false;
        const char* end = a_text + std::strlen(a_text);
        const auto result = std::from_chars(a_text, end, a_value);
        return result.ec == std::errc{} && result.ptr == end;
    }

    static bool IsInlineEditClickedOutside() {
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        return clicked && !ImGui::IsItemHovered();
    }

    static bool IsLiveInspector(const char* a_idSuffix, const char* a_expected) {
        return a_idSuffix != nullptr && a_expected != nullptr && std::strcmp(a_idSuffix, a_expected) == 0;
    }

    static void TrackTopLevelWindowFocus(int a_windowIndex) {
        ImGuiManager::GetSingleton().NotifyWindowFocused(
            a_windowIndex,
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    }

    // 👇========== 🌟 辅助组件实现区域 ==========👇
    static uint32_t GetQueryID(const ImGuiManager::WindowState& state) {
        return (state.scope == ConfigScope::kGlobal) ? state.targetFilter : state.id;
    }

    static std::string_view StripManagedName(std::string_view name) {
        if (name.rfind("IAD_CME_", 0) == 0 || name.rfind("IAD_MOV_", 0) == 0) {
            return name.substr(8);
        }
        return name;
    }

    static bool IsSameManagedName(std::string_view lhs, std::string_view rhs) {
        return lhs == rhs || StripManagedName(lhs) == StripManagedName(rhs);
    }

    template <class T>
    static auto FindManagedMapEntry(std::map<std::string, T>& values, std::string_view selected) {
        return std::find_if(values.begin(), values.end(), [&](const auto& entry) {
            return IsSameManagedName(entry.first, selected);
        });
    }

    static bool DrawProfileFlagMatrix(uint32_t& flags) {
        bool changed = false;
        auto drawFlag = [&](const char* label, ConfigManager::SerFlags flag) {
            bool checked = (flags & static_cast<uint32_t>(flag)) != 0;
            if (ImGui::Checkbox(label, &checked)) {
                if (checked) {
                    flags |= static_cast<uint32_t>(flag);
                }
                else {
                    flags &= ~static_cast<uint32_t>(flag);
                }
                changed = true;
            }
            };

        if (ImGui::BeginTable("ProfileFlagMatrix", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Data");
            ImGui::TableSetupColumn("Global");
            ImGui::TableSetupColumn("Actor");
            ImGui::TableSetupColumn("NPC");
            ImGui::TableSetupColumn("Race");
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Slots");
            ImGui::TableSetColumnIndex(1); drawFlag("##slotGlobal", ConfigManager::SerFlags::kSlotGlobal);
            ImGui::TableSetColumnIndex(2); drawFlag("##slotActor", ConfigManager::SerFlags::kSlotActor);
            ImGui::TableSetColumnIndex(3); drawFlag("##slotNpc", ConfigManager::SerFlags::kSlotNPC);
            ImGui::TableSetColumnIndex(4); drawFlag("##slotRace", ConfigManager::SerFlags::kSlotRace);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Nodes");
            ImGui::TableSetColumnIndex(1); drawFlag("##nodeGlobal", ConfigManager::SerFlags::kNodeGlobal);
            ImGui::TableSetColumnIndex(2); drawFlag("##nodeActor", ConfigManager::SerFlags::kNodeActor);
            ImGui::TableSetColumnIndex(3); drawFlag("##nodeNpc", ConfigManager::SerFlags::kNodeNPC);
            ImGui::TableSetColumnIndex(4); drawFlag("##nodeRace", ConfigManager::SerFlags::kNodeRace);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Customs");
            ImGui::TableSetColumnIndex(1); drawFlag("##customGlobal", ConfigManager::SerFlags::kCustomGlobal);
            ImGui::TableSetColumnIndex(2); drawFlag("##customActor", ConfigManager::SerFlags::kCustomActor);
            ImGui::TableSetColumnIndex(3); drawFlag("##customNpc", ConfigManager::SerFlags::kCustomNPC);
            ImGui::TableSetColumnIndex(4); drawFlag("##customRace", ConfigManager::SerFlags::kCustomRace);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Form filters");
            ImGui::TableSetColumnIndex(1); drawFlag("##formFilters", ConfigManager::SerFlags::kFormFilters);
            ImGui::EndTable();
        }

        bool all = flags == static_cast<uint32_t>(ConfigManager::SerFlags::kAll);
        if (ImGui::Checkbox("All data", &all)) {
            flags = all ? static_cast<uint32_t>(ConfigManager::SerFlags::kAll) : 0;
            changed = true;
        }
        return changed;
    }

    static bool DrawProfileCombo(const char* id, const std::vector<std::string>& profiles, std::string& selectedProfile) {
        bool changed = false;
        const char* preview = selectedProfile.empty() ? "<none>" : selectedProfile.c_str();
        if (ImGui::BeginCombo(id, preview)) {
            for (const auto& profile : profiles) {
                bool selected = selectedProfile == profile;
                if (ImGui::Selectable(profile.c_str(), selected)) {
                    selectedProfile = profile;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    static void DrawProfileFileActions(
        const char* id,
        ConfigManager* config,
        const char* folderName,
        std::string& selectedProfile,
        char* targetName,
        std::size_t targetNameSize,
        std::string& statusText)
    {
        ImGui::PushID(id);
        ImGui::Spacing();
        const bool hasSelection = !selectedProfile.empty();
        const bool hasTargetName = targetName != nullptr && targetNameSize > 0 && std::strlen(targetName) > 0;

        if (!hasSelection || !hasTargetName) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("重命名所选"), { 130.0f, 0.0f })) {
            const bool renamed = folderName ?
                config->RenameProfile(folderName, selectedProfile, targetName) :
                config->RenameExport(selectedProfile, targetName);
            if (renamed) {
                selectedProfile = targetName;
                statusText = TextLiteral("已重命名所选预设。");
            }
            else {
                statusText = TextLiteral("重命名失败。");
            }
        }
        if (!hasSelection || !hasTargetName) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("删除所选"), { 130.0f, 0.0f })) {
            ImGui::OpenPopup("ConfirmDeleteSelectedProfile");
        }
        if (!hasSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteSelectedProfile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(TextLiteral("删除预设: %s"), selectedProfile.c_str());
            ImGui::Separator();
            if (ImGui::Button(TextLiteral("删除"), { 120.0f, 0.0f })) {
                const bool deleted = folderName ?
                    config->DeleteProfile(folderName, selectedProfile) :
                    config->DeleteExport(selectedProfile);
                statusText = deleted ? TextLiteral("已删除所选预设。") : TextLiteral("删除失败。");
                if (deleted) {
                    selectedProfile.clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("取消"), { 120.0f, 0.0f })) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    template <class T, class DrawItemFn>
    static void DrawManagedProfileEditor(
        const char* id,
        IAD::Profile::ProfileManager<T>& manager,
        const char* defaultName,
        DrawItemFn drawItem,
        bool* openState = nullptr)
    {
        auto& editorState = UIProfileEditorStateStore::Get(id);
        auto& selected = editorState.selected;
        auto& status = editorState.status;
        auto& filter = editorState.filter;
        auto& desc = editorState.description;
        auto& lastSelected = editorState.lastSelected;
        auto& nameBuffer = editorState.name;

        if (nameBuffer[0] == '\0') {
            strcpy_s(nameBuffer.data(), nameBuffer.size(), defaultName);
        }

        if (!manager.IsInitialized()) {
            manager.Load();
        }

        ImGui::PushID(id);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu(TextLiteral("文件"))) {
                if (openState && ImGui::MenuItem(TextLiteral("关闭"))) {
                    *openState = false;
                }
                if (openState) {
                    ImGui::Separator();
                }
                if (ImGui::MenuItem(TextLiteral("重新读取预设目录"))) {
                    manager.Load();
                    selected.clear();
                    status = TextLiteral("已重新读取预设目录。");
                }
                ImGui::EndMenu();
            }
            if (auto* record = manager.Find(selected)) {
                if (ImGui::BeginMenu(TextLiteral("预设设置"))) {
                    bool mergeOnly = record->IsMergeOnly();
                    if (ImGui::MenuItem(TextLiteral("仅允许合并"), nullptr, mergeOnly)) {
                        record->SetMergeOnly(!mergeOnly);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("说明"))) {
                        const auto text = record->description.value_or("");
                        strcpy_s(desc.data(), desc.size(), text.c_str());
                        ImGui::OpenPopup("EditProfileDescription");
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenuBar();
        }

        ImGui::SetNextItemWidth(210.0f);
        ImGui::InputText(TextLiteral("筛选"), filter.data(), filter.size());
        ImGui::SameLine();
        if (ImGui::Button(TextLiteral("刷新列表"), { 90.0f, 0.0f })) {
            manager.Load();
            selected.clear();
            status = TextLiteral("已重新读取预设目录。");
        }

        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo(TextLiteral("预设"), selected.empty() ? TextLiteral("未选择") : selected.c_str(), ImGuiComboFlags_HeightLarge)) {
            const std::string filterText = filter.data();
            for (auto& [name, record] : manager.Data()) {
                if (!filterText.empty() && name.find(filterText) == std::string::npos) {
                    continue;
                }
                bool isSelected = selected == name;
                std::string label = record.modified ? ("* " + name) : name;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selected = name;
                    strcpy_s(nameBuffer.data(), nameBuffer.size(), name.c_str());
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText(TextLiteral("名称"), nameBuffer.data(), nameBuffer.size());
        const bool hasName = std::strlen(nameBuffer.data()) > 0;
        const bool hasSelection = !selected.empty();

        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("新建"), { 72.0f, 0.0f })) {
            if (manager.CreateProfile(nameBuffer.data())) {
                selected = nameBuffer.data();
                status = TextLiteral("已创建预设。");
            }
            else {
                status = manager.LastError();
            }
        }
        if (!hasName) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("保存"), { 72.0f, 0.0f })) {
            if (manager.SaveProfile(selected)) {
                status = TextLiteral("已保存预设。");
            }
            else {
                status = manager.LastError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TextLiteral("重新读取"), { 72.0f, 0.0f })) {
            if (manager.ReloadProfile(selected)) {
                status = TextLiteral("已重新读取预设。");
            }
            else {
                status = manager.LastError();
            }
        }
        ImGui::SameLine();
        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("重命名"), { 82.0f, 0.0f })) {
            const std::string oldName = selected;
            const bool renamed = [&]() {
                if constexpr (std::is_same_v<T, FormFilter>) {
                    return UIProfileWorkflow::RenameFormFilter(manager, oldName, nameBuffer.data());
                }
                else {
                    return manager.RenameProfile(oldName, nameBuffer.data());
                }
            }();
            if (renamed) {
                selected = nameBuffer.data();
                status = TextLiteral("已重命名预设。");
            }
            else {
                status = manager.LastError();
            }
        }
        if (!hasName) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TextLiteral("删除"), { 72.0f, 0.0f })) {
            ImGui::OpenPopup("ConfirmManagedProfileDelete");
        }
        if (!hasSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmManagedProfileDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(TextLiteral("删除预设: %s"), selected.c_str());
            ImGui::Separator();
            if (ImGui::Button(TextLiteral("删除"), { 120.0f, 0.0f })) {
                const std::string deletedName = selected;
                const bool deleted = [&]() {
                    if constexpr (std::is_same_v<T, FormFilter>) {
                        return UIProfileWorkflow::DeleteFormFilter(manager, deletedName);
                    }
                    else {
                        return manager.DeleteProfile(deletedName);
                    }
                }();
                if (deleted) {
                    selected.clear();
                    status = TextLiteral("已删除预设。");
                }
                else {
                    status = manager.LastError();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("取消"), { 120.0f, 0.0f })) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!status.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", status.c_str());
        }

        ImGui::Separator();
        if (auto* record = manager.Find(selected)) {
            if (lastSelected != selected) {
                const auto text = record->description.value_or("");
                strcpy_s(desc.data(), desc.size(), text.c_str());
                lastSelected = selected;
            }

            if (ImGui::BeginPopupModal("EditProfileDescription", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("预设说明"));
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputTextMultiline("##ProfileDescription", desc.data(), desc.size(), ImVec2(420.0f, 140.0f));
                ImGui::Separator();
                if (ImGui::Button(TextLiteral("应用"), { 120.0f, 0.0f })) {
                    if (std::strlen(desc.data()) > 0) {
                        record->description = desc.data();
                    }
                    else {
                        record->description.reset();
                    }
                    record->MarkModified();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 120.0f, 0.0f })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (record->description && !record->description->empty()) {
                ImGui::TextWrapped("%s", record->description->c_str());
                ImGui::Separator();
            }

            ImGui::Separator();
            if (record->parserErrors) {
                ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, TextLiteral("预设读取时出现解析警告。"));
                if (!record->lastError.empty()) {
                    ImGui::TextWrapped("%s", record->lastError.c_str());
                }
                ImGui::Separator();
            }
            if (drawItem(*record)) {
                record->MarkModified();
            }
        }
        else {
            ImGui::TextDisabled(TextLiteral("请选择一个预设。 "));
        }
        ImGui::PopID();
    }

    static CustomDefinition* GetSelectedCustomForProfile(ConfigManager* config) {
        auto& customs = config->GetCustoms(GetCustomEditorContext().scope, GetQueryID(GetCustomEditorContext()));
        if (!GetCustomEditorContext().selection.Empty()) {
            auto it = std::find_if(customs.begin(), customs.end(), [](const CustomDefinition& custom) {
                return GetCustomEditorContext().selection.IsSelected(custom.customName);
                });
            if (it != customs.end()) {
                return &(*it);
            }
        }
        return customs.empty() ? nullptr : &customs.front();
    }

    static SlotDefinition* GetSelectedSlotForProfile(ConfigManager* config) {
            auto& slots = config->GetSlots(GetSlotEditorContext().scope, GetQueryID(GetSlotEditorContext()));
            if (!GetSlotEditorContext().selection.Empty()) {
                auto it = std::find_if(slots.begin(), slots.end(), [](const SlotDefinition& slot) {
                return GetSlotEditorContext().selection.IsSelected(slot.slotName);
                });
            if (it != slots.end()) {
                return &(*it);
            }
        }
        return slots.empty() ? nullptr : &slots.front();
    }

    static NodeDefinition* GetSelectedNodeForProfile(ConfigManager* config) {
            auto& nodes = config->GetNodes(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext()));
            if (!GetNodeEditorContext().selection.Empty()) {
                auto it = std::find_if(nodes.begin(), nodes.end(), [](const NodeDefinition& node) {
                return GetNodeEditorContext().selection.IsSelected(node.nodeName);
                });
            if (it != nodes.end()) {
                return &(*it);
            }
        }
        return nodes.empty() ? nullptr : &nodes.front();
    }

    static void ApplyTransformToConfigBase(ConfigBase& target, const TransformData& transform, const ImGuiManager::WindowState& state) {
        if (state.syncGender) {
            target.transforms.m = transform;
            target.transforms.f = transform;
        }
        else if (state.genderEdit == 1) {
            target.transforms.f = transform;
        }
        else {
            target.transforms.m = transform;
        }
    }

    static bool DrawSlotProfileRecord(IAD::Profile::ProfileRecord<std::vector<SlotDefinition>>& record) {
        bool changed = false;
        auto& globalNodes = ConfigManager::GetSingleton()->GetNodes(ConfigScope::kGlobal, 0);
        static std::map<std::string, std::string> selectedSlotByProfile;
        static ImGuiManager::WindowState profileState;
        profileState.scope = ConfigScope::kGlobal;
        profileState.id = 0;

        auto& selectedSlot = selectedSlotByProfile[record.name];
        if (!record.data.empty() &&
            std::none_of(record.data.begin(), record.data.end(), [&](const SlotDefinition& slot) { return slot.slotName == selectedSlot; })) {
            selectedSlot = record.data.front().slotName;
        }

        ImGui::TextDisabled(TextLiteral("%zu 个插槽"), record.data.size());
        ImGui::Separator();

        auto& layout = ConfigManager::GetSingleton()->uiLayout;
        UIEditorInteraction::ClampPaneWidth(layout.profileSlotLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);
        ImGui::BeginChild("ProfileSlotLeft", ImVec2(layout.profileSlotLeftPaneWidth, 0.0f), true);
        if (ImGui::Button(TextLiteral("新建装备槽位"), ImVec2(-1.0f, 32.0f))) {
            SlotDefinition slot;
            slot.slotName = "IAD_MOV_ProfileSlot_" + std::to_string(record.data.size() + 1);
            record.data.push_back(slot);
            selectedSlot = slot.slotName;
            changed = true;
        }
        ImGui::Separator();
        for (auto& slot : record.data) {
            const std::string displayName = (slot.slotName.find("IAD_MOV_") == 0) ? slot.slotName.substr(8) : slot.slotName;
            if (ImGui::Selectable((displayName + "###ProfileSlot_" + slot.slotName).c_str(), selectedSlot == slot.slotName)) {
                selectedSlot = slot.slotName;
            }
        }
        ImGui::EndChild();

        UIEditorInteraction::DrawPaneSplitter("##profileSlotSplitter", layout.profileSlotLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);

        ImGui::BeginChild("ProfileSlotRight", ImVec2(0.0f, 0.0f), true);
        auto it = std::find_if(record.data.begin(), record.data.end(), [&](const SlotDefinition& slot) { return slot.slotName == selectedSlot; });
        if (it != record.data.end()) {
            auto& slot = *it;
            ImGui::PushID(("ProfileSlotEditor_" + slot.slotName).c_str());

            std::string shortName = (slot.slotName.find("IAD_MOV_") == 0) ? slot.slotName.substr(8) : slot.slotName;
            char nameBuf[64];
            strcpy_s(nameBuf, shortName.c_str());
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, TextLiteral("槽位名称:"));
            ImGui::SameLine();
            ImGui::TextDisabled("IAD_MOV_");
            ImGui::SameLine(0, 0);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##profileSlotRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string newName = "IAD_MOV_" + std::string(nameBuf);
                if (!newName.empty() && newName != slot.slotName) {
                    const bool exists = std::any_of(record.data.begin(), record.data.end(), [&](const SlotDefinition& other) { return other.slotName == newName; });
                    if (!exists) {
                        slot.slotName = newName;
                        selectedSlot = newName;
                        changed = true;
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("删除槽位"))) {
                ImGui::OpenPopup("DeleteProfileSlot");
            }
            if (ImGui::BeginPopupModal("DeleteProfileSlot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("从此预设删除槽位 [%s]？"), slot.slotName.c_str());
                if (ImGui::Button(TextLiteral("删除"), { 100.0f, 0.0f })) {
                    record.data.erase(it);
                    selectedSlot.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    ImGui::EndChild();
                    return changed;
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Checkbox(TextLiteral("全局启用"), &slot.isEnabled)) changed = true;
            ImGui::SameLine();
            if (DrawPriorityInput(TextLiteral("渲染优先级"), slot.priority, 120.0f)) changed = true;
            ImGui::Separator();

            ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("绑定目标节点:"));
            ImGui::SameLine();
            const auto oldNode = slot.targetNode;
            DrawCMENodeSelector("##profileSlotTargetNode", slot.targetNode, globalNodes, ConfigScope::kGlobal, TextLiteral("未绑定"));
            if (slot.targetNode != oldNode) changed = true;
            ImGui::Separator();

            changed |= DrawSlotEditorTabs(slot, profileState, "profileSlotTabs");

            ImGui::PopID();
        }
        else {
            ImGui::TextDisabled(TextLiteral("请在左侧选择或新建一个装备槽位。 "));
        }
        ImGui::EndChild();
        return changed;
    }

    static bool DrawNodeProfileRecord(IAD::Profile::ProfileRecord<std::vector<NodeDefinition>>& record) {
        bool changed = false;
        static std::map<std::string, std::string> selectedNodeByProfile;
        static ImGuiManager::WindowState profileState;
        profileState.scope = ConfigScope::kGlobal;
        profileState.id = 0;

        auto& selectedNode = selectedNodeByProfile[record.name];
        if (!record.data.empty() &&
            std::none_of(record.data.begin(), record.data.end(), [&](const NodeDefinition& node) { return node.nodeName == selectedNode; })) {
            selectedNode = record.data.front().nodeName;
        }

        ImGui::TextDisabled("%zu node entries", record.data.size());
        ImGui::Separator();

        auto& layout = ConfigManager::GetSingleton()->uiLayout;
        UIEditorInteraction::ClampPaneWidth(layout.profileNodeLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);
        ImGui::BeginChild("ProfileNodeLeft", ImVec2(layout.profileNodeLeftPaneWidth, 0.0f), true);
        if (ImGui::Button("New node", ImVec2(-1.0f, 32.0f))) {
            NodeDefinition node;
            node.nodeName = "IAD_CME_ProfileNode_" + std::to_string(record.data.size() + 1);
            record.data.push_back(node);
            selectedNode = node.nodeName;
            changed = true;
        }
        ImGui::Separator();
        for (auto& node : record.data) {
            const std::string displayName = (node.nodeName.find("IAD_CME_") == 0) ? node.nodeName.substr(8) : node.nodeName;
            if (ImGui::Selectable((displayName + "###ProfileNode_" + node.nodeName).c_str(), selectedNode == node.nodeName)) {
                selectedNode = node.nodeName;
            }
        }
        ImGui::EndChild();

        UIEditorInteraction::DrawPaneSplitter("##profileNodeSplitter", layout.profileNodeLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);

        ImGui::BeginChild("ProfileNodeRight", ImVec2(0.0f, 0.0f), true);
        auto it = std::find_if(record.data.begin(), record.data.end(), [&](const NodeDefinition& node) { return node.nodeName == selectedNode; });
        if (it != record.data.end()) {
            auto& node = *it;
            ImGui::PushID(("ProfileNodeEditor_" + node.nodeName).c_str());

            std::string shortName = (node.nodeName.find("IAD_CME_") == 0) ? node.nodeName.substr(8) : node.nodeName;
            char nameBuf[64];
            strcpy_s(nameBuf, shortName.c_str());
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "Node name:");
            ImGui::SameLine();
            ImGui::TextDisabled("IAD_CME_");
            ImGui::SameLine(0, 0);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##profileNodeRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string newName = "IAD_CME_" + std::string(nameBuf);
                if (!newName.empty() && newName != node.nodeName) {
                    const bool exists = std::any_of(record.data.begin(), record.data.end(), [&](const NodeDefinition& other) { return other.nodeName == newName; });
                    if (!exists) {
                        node.nodeName = newName;
                        selectedNode = newName;
                        changed = true;
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Delete node")) {
                ImGui::OpenPopup("DeleteProfileNode");
            }
            if (ImGui::BeginPopupModal("DeleteProfileNode", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete node [%s] from this profile?", node.nodeName.c_str());
                if (ImGui::Button("Delete", { 100.0f, 0.0f })) {
                    record.data.erase(it);
                    selectedNode.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    ImGui::EndChild();
                    return changed;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", { 100.0f, 0.0f })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Checkbox("Enabled", &node.isEnabled)) changed = true;
            ImGui::Separator();

            if (ImGui::TreeNodeEx("Target bones", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= UIFormEditorControls::DrawStringVectorEditor("Fallback hosts", node.fallbackHosts, "NPC Root [Root]");
                ImGui::TreePop();
            }

            if (ImGui::Checkbox("Absolute position", &node.absolutePosition)) changed = true;
            ImGui::SameLine();
            ImGui::TextDisabled("Weapon/Weight adjust: unsupported by current transform path");
            ImGui::SameLine();
            ImGui::TextDisabled("legacy config fields retained");
            ImGui::Separator();

            changed |= DrawNodeEditorTabs(node, profileState, "profileNodeTabs");

            ImGui::PopID();
        }
        else {
            ImGui::TextDisabled("Select or create a node.");
        }
        ImGui::EndChild();
        return changed;
    }

    static bool DrawCustomProfileRecord(IAD::Profile::ProfileRecord<std::vector<CustomDefinition>>& record) {
        bool changed = false;
        auto& globalNodes = ConfigManager::GetSingleton()->GetNodes(ConfigScope::kGlobal, 0);
        static std::map<std::string, std::string> selectedCustomByProfile;
        static ImGuiManager::WindowState profileState;
        profileState.scope = ConfigScope::kGlobal;
        profileState.id = 0;

        auto& selectedCustom = selectedCustomByProfile[record.name];
        if (!record.data.empty() &&
            std::none_of(record.data.begin(), record.data.end(), [&](const CustomDefinition& custom) { return custom.customName == selectedCustom; })) {
            selectedCustom = record.data.front().customName;
        }

        ImGui::TextDisabled("%zu custom display entries", record.data.size());
        ImGui::Separator();

        auto& layout = ConfigManager::GetSingleton()->uiLayout;
        UIEditorInteraction::ClampPaneWidth(layout.profileCustomLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);
        ImGui::BeginChild("ProfileCustomLeft", ImVec2(layout.profileCustomLeftPaneWidth, 0.0f), true);
        if (ImGui::Button("New custom display", ImVec2(-1.0f, 32.0f))) {
            CustomDefinition custom;
            custom.customName = "Profile_Custom_" + std::to_string(record.data.size() + 1);
            record.data.push_back(custom);
            selectedCustom = custom.customName;
            changed = true;
        }
        ImGui::Separator();
        for (auto& custom : record.data) {
            if (ImGui::Selectable((custom.customName + "###ProfileCustom_" + custom.customName).c_str(), selectedCustom == custom.customName)) {
                selectedCustom = custom.customName;
            }
        }
        ImGui::EndChild();

        UIEditorInteraction::DrawPaneSplitter("##profileCustomSplitter", layout.profileCustomLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 180.0f);

        ImGui::BeginChild("ProfileCustomRight", ImVec2(0.0f, 0.0f), true);
        auto it = std::find_if(record.data.begin(), record.data.end(), [&](const CustomDefinition& custom) { return custom.customName == selectedCustom; });
        if (it != record.data.end()) {
            auto& custom = *it;
            ImGui::PushID(("ProfileCustomEditor_" + custom.customName).c_str());

            char nameBuf[64];
            strcpy_s(nameBuf, custom.customName.c_str());
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "Rule name:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("##profileCustomRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string newName = nameBuf;
                if (!newName.empty() && newName != custom.customName) {
                    const bool exists = std::any_of(record.data.begin(), record.data.end(), [&](const CustomDefinition& other) { return other.customName == newName; });
                    if (!exists) {
                        custom.customName = newName;
                        selectedCustom = newName;
                        changed = true;
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Delete rule")) {
                ImGui::OpenPopup("DeleteProfileCustom");
            }
            if (ImGui::BeginPopupModal("DeleteProfileCustom", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete custom display [%s] from this profile?", custom.customName.c_str());
                if (ImGui::Button("Delete", { 100.0f, 0.0f })) {
                    record.data.erase(it);
                    selectedCustom.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    ImGui::EndChild();
                    return changed;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", { 100.0f, 0.0f })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Checkbox("Enabled", &custom.isEnabled)) changed = true;
            ImGui::SameLine();
            if (DrawPriorityInput("Priority", custom.priority, 120.0f)) changed = true;
            if (ImGui::Checkbox("Ignore player", &custom.ignorePlayer)) changed = true;
            ImGui::Separator();

            changed |= UIFormEditorControls::DrawFormIDField("Target FormID", custom.targetFormID);
            ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "Target node:");
            ImGui::SameLine();
            const auto oldNode = custom.targetNode;
            DrawCMENodeSelector("##profileCustomTargetNode", custom.targetNode, globalNodes, ConfigScope::kGlobal, "[slot default]");
            if (custom.targetNode != oldNode) changed = true;
            ImGui::Separator();

            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Display logic");
            if (ImGui::Checkbox("Override equipment mode", &custom.overrideEquipmentMode)) changed = true;
            if (custom.overrideEquipmentMode) {
                ImGui::Indent();
                if (ImGui::Checkbox("Display favorites only", &custom.displayFavoritesOnly)) changed = true;
                ImGui::Unindent();
            }
            if (ImGui::Checkbox("Always unload while equipped", &custom.alwaysUnload)) changed = true;
            if (ImGui::Checkbox("Group mode (model groups only)", &custom.groupMode)) changed = true;
            if (custom.groupMode) {
                ImGui::TextDisabled("Primary item model and custom holster are suppressed; active model group entries remain visible.");
                ImGui::TextDisabled("FormID model group sources also participate in inventory candidate selection.");
            }
            if (ImGui::Checkbox("Last equipped mode", &custom.lastEquippedMode)) changed = true;
            if (custom.lastEquippedMode) {
                ImGui::Indent();
                if (ImGui::Checkbox("Prioritize recent biped slots", &custom.lastEquippedPrioritizeRecentBipedSlots)) changed = true;
                if (ImGui::Checkbox("Disable if listed biped slot is occupied", &custom.lastEquippedDisableIfBipedSlotOccupied)) changed = true;
                if (!custom.lastEquippedDisableIfBipedSlotOccupied) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("Skip occupied biped slots", &custom.lastEquippedSkipOccupiedBipedSlots)) changed = true;
                    ImGui::Unindent();
                }
                changed |= UIFormEditorControls::DrawBipedSlotVectorEditor("Last equipped biped slots", custom.lastEquippedBipedSlots);
                if (ImGui::Checkbox("Prioritize recent display slot", &custom.lastEquippedPrioritizeRecentDisplaySlot)) changed = true;
                if (ImGui::Checkbox("Skip occupied display slots", &custom.lastEquippedSkipOccupiedDisplaySlots)) changed = true;
                if (ImGui::Checkbox("Disable if listed display slot is occupied", &custom.lastEquippedDisableIfDisplaySlotOccupied)) changed = true;
                if (ImGui::Checkbox("Fallback to last slotted display item", &custom.lastEquippedFallbackToSlotted)) changed = true;
                if (ImGui::Checkbox("Fallback to any available slot", &custom.lastEquippedFallbackToAnySlot)) changed = true;
                if (ImGui::Checkbox("Fallback to recent acquired", &custom.lastEquippedFallbackToRecentAcquired)) changed = true;
                if (custom.lastEquippedFallbackToRecentAcquired) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("Prioritize recent acquired types", &custom.lastEquippedPrioritizeRecentAcquiredTypes)) changed = true;
                    changed |= UIFormEditorControls::DrawFormTypeVectorEditor("Recent acquired form types", custom.lastEquippedRecentAcquiredFormTypes);
                    ImGui::Unindent();
                }
                changed |= UIFormEditorControls::DrawStringVectorEditor("Last equipped slots", custom.lastEquippedDisplaySlots, "Backpack_Right");
                if (ImGui::TreeNodeEx("Last equipped filter conditions", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Additional candidate filter evaluated only by Last Equipped mode.");
                    if (ImGui::Button("Reset last equipped filter##profileCustomLastEquipped")) {
                        custom.lastEquippedFilterConditionTree = ConditionNode(true, true);
                        changed = true;
                    }
                    ImGui::PushID("ProfileCustomLastEquippedFilter");
                    changed |= DrawConditionTreeEditor(custom.lastEquippedFilterConditionTree);
                    ImGui::PopID();
                    ImGui::TreePop();
                }
                ImGui::Unindent();
            }
            if (ImGui::Checkbox("Disable if equipped", &custom.disableIfEquipped)) changed = true;
            ImGui::Separator();

            if (ImGui::TreeNodeEx("Inventory selection", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (custom.lastEquippedMode) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Select inventory random", &custom.selectInventoryRandom)) {
                    if (custom.selectInventoryRandom) custom.selectInventoryStrongest = false;
                    changed = true;
                }
                if (ImGui::Checkbox("Select inventory strongest", &custom.selectInventoryStrongest)) {
                    if (custom.selectInventoryStrongest) custom.selectInventoryRandom = false;
                    changed = true;
                }
                if (custom.lastEquippedMode) {
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Last Equipped mode uses equip and display history instead.");
                }
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragFloat("Spawn chance", &custom.spawnChance, 0.5f, 0.0f, 100.0f, "%.1f%%")) {
                    custom.spawnChance = std::clamp(custom.spawnChance, 0.0f, 100.0f);
                    changed = true;
                }
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Count min", &custom.countMin)) {
                    if (custom.countMin < 0) custom.countMin = 0;
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputInt("Count max", &custom.countMax)) {
                    if (custom.countMax < 0) custom.countMax = 0;
                    changed = true;
                }
                changed |= UIFormEditorControls::DrawFormVectorUI(custom.extraItems, "ProfileCustomExtraItems");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Candidate conditions", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Reset candidate conditions##profileCustomInventory")) {
                    custom.inventoryConditionTree = ConditionNode(true, true);
                    changed = true;
                }
                changed |= DrawConditionTreeEditor(custom.inventoryConditionTree);
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Model overrides", ImGuiTreeNodeFlags_DefaultOpen)) {
                char modelPath[256];
                strcpy_s(modelPath, custom.modelSwapPath.c_str());
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputText("Model swap path", modelPath, sizeof(modelPath));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    custom.modelSwapPath = modelPath;
                    changed = true;
                }
            changed |= UIFormEditorControls::DrawFormIDField("Model swap FormID", custom.modelSwapFormID);
                changed |= UIModelEditorControls::DrawModelSwapVariableSource(custom.modelSwapVariableSource, "profile_custom_model_var_source");
                if (ImGui::Checkbox("Extract magazine", &custom.extractMagazine)) {
                    if (custom.extractMagazine) custom.useProjectileForAmmo = false;
                    changed = true;
                }
                if (custom.extractMagazine) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Use projectile for ammo", &custom.useProjectileForAmmo)) changed = true;
                if (custom.extractMagazine) ImGui::EndDisabled();
                if (ImGui::Checkbox("Use world/base model", &custom.useWorldModel)) changed = true;
                if (ImGui::Checkbox("Invisible / alpha 0", &custom.invisible)) changed = true;
                if (ImGui::Checkbox("Hide geometry only", &custom.hideGeometry)) changed = true;
                if (ImGui::Checkbox("Hide extra light", &custom.hideLight)) changed = true;
                if (ImGui::Checkbox("Load 1P weapon model", &custom.load1pWeaponModel)) changed = true;
                if (ImGui::Checkbox("Keep torch/flame FX", &custom.keepTorchFlame)) changed = true;
                if (ImGui::Checkbox("Remove scabbard/sheath", &custom.removeScabbard)) changed = true;
                if (UIModelEditorControls::DrawModelCleanupSettings(custom.disableHavok, custom.removeEditorMarker, custom.removeProjectileTracers, "profile_custom_cleanup")) {
                    changed = true;
                }
                if (UIModelEditorControls::DrawModelAnimationSettings(custom.animation, "profile_custom_animation")) {
                    changed = true;
                }
                if (UIModelEditorControls::DrawModelEffectShaderSettings(custom.effectShader, "profile_custom_effect")) {
                    changed = true;
                }
                if (UIModelEditorControls::DrawModelLightSettings(custom.light, "profile_custom_light")) {
                    changed = true;
                }
                ImGui::TreePop();
            }
            ImGui::Separator();

            changed |= DrawCustomEditorTabs(custom, profileState, globalNodes, "profileCustomTabs");

            ImGui::PopID();
        }
        else {
            ImGui::TextDisabled("Select or create a custom display.");
        }
        ImGui::EndChild();
        return changed;
    }

    static bool DrawFormFilterProfileRecord(IAD::Profile::ProfileRecord<FormFilter>& record) {
        const auto oldDenyAll = record.data.denyAll;
        const auto oldAllowList = record.data.allowList;
        const auto oldDenyList = record.data.denyList;
		const auto oldUseProfile = record.data.useProfile;
		const auto oldProfileName = record.data.profileName;
		// A named profile is source data, not another runtime reference. Flattening
		// this legacy edge case also prevents reference chains or cycles.
		record.data.useProfile = false;
		record.data.profileName.clear();
        DrawFormFilterUI(record.data, "ManagedStandaloneFormFilter");
        const bool changed = oldDenyAll != record.data.denyAll ||
            oldAllowList != record.data.allowList ||
            oldDenyList != record.data.denyList ||
			oldUseProfile != record.data.useProfile ||
			oldProfileName != record.data.profileName;
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static std::string MakeUniqueModelGroupName(const std::vector<ModelGroupEntry>& groups, const std::string& sourceName);

    static bool DrawModelGroupProfileRecord(IAD::Profile::ProfileRecord<std::vector<ModelGroupEntry>>& record) {
        bool changed = false;
        ImGui::TextDisabled("%zu model group entries", record.data.size());

        if (ImGui::Button("Add model group##profileAddModelGroup", { 150.0f, 0.0f })) {
            ModelGroupEntry group;
            group.name = "Group_" + std::to_string(record.data.size() + 1);
            record.data.push_back(group);
            changed = true;
        }

        const auto& globalNodes = ConfigManager::GetSingleton()->GetNodes(ConfigScope::kGlobal, 0);
        int removeIndex = -1;
        int moveIndex = -1;
        int moveDelta = 0;
        int duplicateIndex = -1;
        for (std::size_t i = 0; i < record.data.size(); ++i) {
            auto& group = record.data[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNode(group.name.empty() ? "<unnamed model group>" : group.name.c_str())) {
                char nameBuf[64];
                strncpy_s(nameBuf, group.name.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    group.name = nameBuf;
                    changed = true;
                }

                if (ImGui::Checkbox("Enabled", &group.isEnabled)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Hide with weapon", &group.hideWithWeapon)) changed = true;
                if (ImGui::Checkbox("Continue after visible match", &group.continueAfterMatch)) changed = true;
                if (ImGui::Checkbox("Load only when visible", &group.loadOnlyWhenVisible)) changed = true;

                const char* sourceModes[] = { "NIF path", "FormID model" };
                int sourceMode = std::clamp(group.sourceMode, 0, 1);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Source mode", &sourceMode, sourceModes, 2)) {
                    group.sourceMode = sourceMode;
                    changed = true;
                }
                if (group.sourceMode == 0) {
                    char pathBuf[260];
                    strncpy_s(pathBuf, group.modelPath.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(440.0f);
                    ImGui::InputText("Model path", pathBuf, sizeof(pathBuf));
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        group.modelPath = pathBuf;
                        changed = true;
                    }
                }
                else {
                    changed |= UIFormEditorControls::DrawFormIDField("Source FormID", group.sourceFormID);
                }

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "Target node:");
                ImGui::SameLine();
                const auto oldTargetNode = group.targetNode;
                DrawCMENodeSelector("##profileModelGroupTargetNode", group.targetNode, globalNodes, ConfigScope::kGlobal, "[inherit]");
                if (group.targetNode != oldTargetNode) changed = true;

                if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    changed |= UITransformEditorControls::DrawTransformWidget("Position", group.transforms.m.pos, { 0,0,0 }, 0.5f, false);
                    changed |= UITransformEditorControls::DrawTransformWidget("Rotation", group.transforms.m.rot, { 0,0,0 }, 1.0f, true);
                    changed |= UITransformEditorControls::DrawTransformWidget("Pivot", group.transforms.m.pivot, { 0,0,0 }, 0.5f, false);
                    if (ImGui::DragFloat("Scale", &group.transforms.m.scale, 0.01f, 0.01f, 20.0f, "%.3f")) changed = true;
                    if (ImGui::Button("Copy male to female##profileModelGroupCopyTransform")) {
                        group.transforms.f = group.transforms.m;
                        changed = true;
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx("Visibility conditions", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Button("Reset conditions##profileModelGroupResetConditions", { 150.0f, 0.0f })) {
                        group.displayConditionTree = ConditionNode(true, true);
                        changed = true;
                    }
                    changed |= DrawConditionTreeEditor(group.displayConditionTree);
                    ImGui::TreePop();
                }

                changed |= DrawModelGroupAdvancedConfig(group);
                if (i == 0) ImGui::BeginDisabled();
                if (ImGui::Button("Move up##profileMoveModelGroupUp", { 90.0f, 0.0f })) {
                    moveIndex = static_cast<int>(i);
                    moveDelta = -1;
                }
                if (i == 0) ImGui::EndDisabled();
                ImGui::SameLine();
                if (i + 1 >= record.data.size()) ImGui::BeginDisabled();
                if (ImGui::Button("Move down##profileMoveModelGroupDown", { 110.0f, 0.0f })) {
                    moveIndex = static_cast<int>(i);
                    moveDelta = 1;
                }
                if (i + 1 >= record.data.size()) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Duplicate##profileDuplicateModelGroup", { 105.0f, 0.0f })) {
                    duplicateIndex = static_cast<int>(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete model group##profileDeleteModelGroup", { 150.0f, 0.0f })) {
                    removeIndex = static_cast<int>(i);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (moveIndex >= 0 && moveDelta != 0) {
            const int targetIndex = moveIndex + moveDelta;
            if (targetIndex >= 0 && targetIndex < static_cast<int>(record.data.size())) {
                std::swap(record.data[moveIndex], record.data[targetIndex]);
                changed = true;
            }
        }
        if (duplicateIndex >= 0 && duplicateIndex < static_cast<int>(record.data.size())) {
            auto copy = record.data[duplicateIndex];
            copy.name = MakeUniqueModelGroupName(record.data, copy.name);
            record.data.insert(record.data.begin() + duplicateIndex + 1, std::move(copy));
            changed = true;
        }
        if (removeIndex >= 0 && removeIndex < static_cast<int>(record.data.size())) {
            record.data.erase(record.data.begin() + removeIndex);
            changed = true;
        }
        return changed;
    }

    static bool DrawNodeMonitorProfileRecord(IAD::Profile::ProfileRecord<std::vector<std::string>>& record) {
        return UIFormEditorControls::DrawStringVectorEditor("Node names", record.data, "NPC Root [Root]");
    }

    static bool DrawConditionProfileRecord(IAD::Profile::ProfileRecord<ConditionNode>& record) {
        return DrawConditionTreeEditor(record.data, false);
    }

    static bool DrawTransformProfileRecord(IAD::Profile::ProfileRecord<TransformData>& record) {
        bool changed = false;
        changed |= UITransformEditorControls::DrawTransformWidget("Position", record.data.pos, { 0,0,0 }, 0.5f, false);
        changed |= UITransformEditorControls::DrawTransformWidget("Rotation", record.data.rot, { 0,0,0 }, 1.0f, true);
        changed |= UITransformEditorControls::DrawTransformWidget("Pivot", record.data.pivot, { 0,0,0 }, 0.5f, false);
        if (ImGui::DragFloat("Scale", &record.data.scale, 0.01f, 0.01f, 20.0f, "%.3f")) {
            changed = true;
        }
        return changed;
    }

    static bool DrawPhysicsProfileRecord(IAD::Profile::ProfileRecord<PhysicsValues>& record) {
        return DrawPhysicsPanel(record.data);
    }

    void BuildTree(RE::NiAVObject* node, BoneNode& out) {
        if (!node) return; out.name = node->name.c_str() ? node->name.c_str() : "None";
        if (node->name.c_str()) s_flatBoneList.push_back(node->name.c_str());
        if (auto ni = node->IsNode()) { for (auto& child : ni->children) if (child) { BoneNode cn; BuildTree(child.get(), cn); out.children.push_back(cn); } }
    }

    void DrawTree(const BoneNode& node) {
        bool isAnonymous = node.name.empty() || node.name == "None";
        if (isAnonymous) { for (const auto& child : node.children) DrawTree(child); return; }

        ImGui::PushID(static_cast<const void*>(&node));
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (s_selectedBoneName == node.name) flags |= ImGuiTreeNodeFlags_Selected;

        bool isOpen = ImGui::TreeNodeEx(node.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { s_selectedBoneName = node.name; }
        if (isOpen) { for (const auto& child : node.children) DrawTree(child); ImGui::TreePop(); }
        ImGui::PopID();
    }

    static bool DrawBoneScannerBox(const char* a_label, std::string& a_targetNode) {
        bool changed = false;
        if (!s_hasCachedTree) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (player && player->Get3D(false)) {
                s_flatBoneList.clear(); s_boneTree.children.clear();
                BuildTree(player->Get3D(false), s_boneTree); s_hasCachedTree = true;
            }
        }
        ImGui::PushID(a_label);
        char buf[128]; strcpy_s(buf, a_targetNode.c_str()); ImGui::SetNextItemWidth(240.0f);
        if (ImGui::InputText("##input", buf, 128)) { a_targetNode = buf; changed = true; }
        ImGui::SameLine(0, 2.0f);
        if (ImGui::Button("▼")) ImGui::OpenPopup("BoneListPopup");

        if (ImGui::BeginPopup("BoneListPopup")) {
            static char searchBuf[64] = ""; ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##search", TextLiteral("🔍 搜索节点名称..."), searchBuf, 64); ImGui::Separator();
            ImGui::BeginChild("##list", ImVec2(250.0f, 200.0f), true);
            {
                std::string searchStr = searchBuf; std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                for (std::size_t boneIndex = 0; boneIndex < s_flatBoneList.size(); ++boneIndex) {
                    const auto& boneName = s_flatBoneList[boneIndex];
                    if (boneName.empty() || boneName == "None") continue;
                    if (!searchStr.empty()) {
                        std::string lowerBone = boneName; std::transform(lowerBone.begin(), lowerBone.end(), lowerBone.begin(), ::tolower);
                        if (lowerBone.find(searchStr) == std::string::npos) continue;
                    }
                    ImGui::PushID(static_cast<int>(boneIndex));
                    if (ImGui::Selectable(boneName.c_str(), a_targetNode == boneName)) {
                        a_targetNode = boneName; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup();
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawCMENodeSelector(const char* a_label, std::string& a_targetNode, const std::vector<NodeDefinition>& localNodes, ConfigScope currentScope, const char* emptyLabel) {
        bool changed = false;
        ImGui::PushID(a_label);
        char buf[128]; strcpy_s(buf, a_targetNode.c_str()); ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##input", buf, 128)) { a_targetNode = buf; changed = true; }
        ImGui::SameLine(0, 2.0f);
        if (ImGui::Button("▼")) ImGui::OpenPopup("CMEListPopup");

        if (ImGui::BeginPopup("CMEListPopup")) {
            static char searchBuf[64] = ""; ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##search", TextLiteral("🔍 搜索 CME 节点..."), searchBuf, 64); ImGui::Separator();
            ImGui::BeginChild("##list", ImVec2(250.0f, 200.0f), true);
            {
                std::string searchStr = searchBuf; std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                if (emptyLabel != nullptr) {
                    if (searchStr.empty() || std::string(emptyLabel).find(searchStr) != std::string::npos) {
                        if (ImGui::Selectable(emptyLabel, a_targetNode.empty())) { a_targetNode = ""; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup(); changed = true; }
                        ImGui::Separator();
                    }
                }
                auto drawNodeItem = [&](const std::string& nodeName, bool isGlobal) {
                    if (nodeName.empty()) return;
                    if (!searchStr.empty()) { std::string lowerNode = nodeName; std::transform(lowerNode.begin(), lowerNode.end(), lowerNode.begin(), ::tolower); if (lowerNode.find(searchStr) == std::string::npos) return; }
                    std::string dispName = isGlobal ? TextLiteral("[继承] ") + nodeName : nodeName;
                    if (isGlobal) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    const std::string nodeID = (isGlobal ? "global_" : "local_") + nodeName;
                    ImGui::PushID(nodeID.c_str());
                    if (ImGui::Selectable(dispName.c_str(), a_targetNode == nodeName)) { a_targetNode = nodeName; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup(); changed = true; }
                    ImGui::PopID();
                    if (isGlobal) ImGui::PopStyleColor();
                    };
                for (const auto& n : localNodes) drawNodeItem(n.nodeName, false);
                if (currentScope != ConfigScope::kGlobal) {
                    ImGui::Separator(); ImGui::TextDisabled(TextLiteral("-- 全局继承节点 --"));
                    for (const auto& n : ConfigManager::GetSingleton()->GetNodes(ConfigScope::kGlobal, 0)) drawNodeItem(n.nodeName, true);
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    static void AppendSignatureString(std::string& out, const std::string& value) {
        out += std::to_string(value.size());
        out.push_back(':');
        out += value;
        out.push_back('|');
    }

    static void AppendSignaturePoint(std::string& out, const RE::NiPoint3& value) {
        out += std::to_string(value.x);
        out.push_back(',');
        out += std::to_string(value.y);
        out.push_back(',');
        out += std::to_string(value.z);
        out.push_back('|');
    }

    static void AppendSignatureColor(std::string& out, const ColorRGBA& value) {
        out += std::to_string(value.r);
        out.push_back(',');
        out += std::to_string(value.g);
        out.push_back(',');
        out += std::to_string(value.b);
        out.push_back(',');
        out += std::to_string(value.a);
        out.push_back('|');
    }

    static std::string BuildTransformSignature(const TransformData& value) {
        std::string out;
        AppendSignaturePoint(out, value.pos);
        AppendSignaturePoint(out, value.rot);
        AppendSignaturePoint(out, value.pivot);
        out += std::to_string(value.scale);
        out.push_back('|');
        return out;
    }

    static std::string BuildGenderedTransformSignature(const GenderedTransform& value) {
        return BuildTransformSignature(value.m) + BuildTransformSignature(value.f);
    }

    static void AppendConstraintSignature(std::string& out, const PhysicsConstraintParams& value) {
        out += std::to_string(value.velocityResponseScale);
        out.push_back('|');
        out += std::to_string(value.penBiasFactor);
        out.push_back('|');
        out += std::to_string(value.penBiasDepthLimit);
        out.push_back('|');
        out += std::to_string(value.restitutionCoefficient);
        out.push_back('|');
    }

    static std::string BuildPhysicsSignature(const PhysicsValues& value) {
        std::string out;
        out += value.disabled ? '1' : '0';
        out += value.drawConstraints ? '1' : '0';
        out += value.drawPendulum ? '1' : '0';
        out += value.enableAngularConstraint ? '1' : '0';
        out += value.enableBoxConstraint ? '1' : '0';
        out += value.enableSphereConstraint ? '1' : '0';
        out.push_back('|');
        out += std::to_string(value.stiffness); out.push_back('|');
        out += std::to_string(value.stiffness2); out.push_back('|');
        out += std::to_string(value.springSlackOffset); out.push_back('|');
        out += std::to_string(value.springSlackMag); out.push_back('|');
        out += std::to_string(value.damping); out.push_back('|');
        out += std::to_string(value.resistance); out.push_back('|');
        out += std::to_string(value.maxVelocity); out.push_back('|');
        AppendSignaturePoint(out, value.linear);
        AppendSignaturePoint(out, value.rotational);
        AppendSignaturePoint(out, value.cogOffset);
        out += std::to_string(value.mass); out.push_back('|');
        out += std::to_string(value.gravityBias); out.push_back('|');
        out += std::to_string(value.minPitch); out.push_back('|');
        out += std::to_string(value.maxPitch); out.push_back('|');
        out += std::to_string(value.minYaw); out.push_back('|');
        out += std::to_string(value.maxYaw); out.push_back('|');
        out += std::to_string(value.minRoll); out.push_back('|');
        out += std::to_string(value.maxRoll); out.push_back('|');
        out += std::to_string(value.visualProbeLength); out.push_back('|');
        AppendSignaturePoint(out, value.maxOffsetN);
        AppendSignaturePoint(out, value.maxOffsetP);
        out += std::to_string(value.maxOffsetBoxFriction); out.push_back('|');
        AppendConstraintSignature(out, value.boxParams);
        AppendSignaturePoint(out, value.maxOffsetSphereOffset);
        out += std::to_string(value.maxOffsetSphereRadius); out.push_back('|');
        out += std::to_string(value.maxOffsetSphereFriction); out.push_back('|');
        AppendConstraintSignature(out, value.sphereParams);
        return out;
    }

    static std::string BuildModelGroupSignature(const ModelGroupEntry& group) {
        std::string out;
        AppendSignatureString(out, group.name);
        out += group.isEnabled ? '1' : '0';
        out += group.hideWithWeapon ? '1' : '0';
        out += group.continueAfterMatch ? '1' : '0';
        out += group.loadOnlyWhenVisible ? '1' : '0';
        out += group.removeEditorMarker ? '1' : '0';
        out += group.removeProjectileTracers ? '1' : '0';
        out += group.invisible ? '1' : '0';
        out += group.hideGeometry ? '1' : '0';
        out += group.hideLight ? '1' : '0';
        out += group.load1pWeaponModel ? '1' : '0';
        out += group.keepTorchFlame ? '1' : '0';
        out += group.removeScabbard ? '1' : '0';
        out += group.overrideGeometryTransform ? '1' : '0';
        out += group.extractMagazine ? '1' : '0';
        out += group.useProjectileForAmmo ? '1' : '0';
        out.push_back('|');
        out += std::to_string(group.sourceMode); out.push_back('|');
        out += std::to_string(group.sourceFormID); out.push_back('|');
        AppendSignatureString(out, group.modelPath);
        AppendSignatureString(out, group.targetNode);
        out += BuildGenderedTransformSignature(group.transforms);
        out += BuildGenderedTransformSignature(group.geometryTransforms);
        out += UIConditionTreeSignature::Build(group.displayConditionTree);

        out += group.light.enabled ? '1' : '0';
        out.push_back('|');
        out += BuildTransformSignature(group.light.transform);
        AppendSignatureColor(out, group.light.diffuse);
        out += std::to_string(group.light.radius); out.push_back('|');
        out += std::to_string(group.light.dimmer); out.push_back('|');

        out += group.effectShader.enabled ? '1' : '0';
        out += group.effectShader.targetRoot ? '1' : '0';
        out += group.effectShader.force ? '1' : '0';
        out += group.effectShader.lighting ? '1' : '0';
        out += group.effectShader.alpha ? '1' : '0';
        out.push_back('|');
        AppendSignatureColor(out, group.effectShader.fillColor);
        AppendSignatureColor(out, group.effectShader.rimColor);
        out += std::to_string(group.effectShader.baseFillScale); out.push_back('|');
        out += std::to_string(group.effectShader.baseFillAlpha); out.push_back('|');
        out += std::to_string(group.effectShader.baseRimAlpha); out.push_back('|');
        out += std::to_string(group.effectShader.edgeExponent); out.push_back('|');
        out += std::to_string(group.effectShader.alphaMultiplier); out.push_back('|');
        AppendSignatureString(out, group.effectShader.baseTexturePath);
        AppendSignatureString(out, group.effectShader.paletteTexturePath);
        AppendSignatureString(out, group.effectShader.blockOutTexturePath);

        out += group.animation.playSequence ? '1' : '0';
        out += group.animation.forwardAnimationEvents ? '1' : '0';
        out += group.animation.disableBehaviorGraphAnims ? '1' : '0';
        out.push_back('|');
        AppendSignatureString(out, group.animation.sequenceName);
        AppendSignatureString(out, group.animation.animationEvent);
        return out;
    }

    static std::string BuildModelGroupListSignature(const std::vector<ModelGroupEntry>& groups) {
        std::string out;
        out += std::to_string(groups.size());
        out.push_back('|');
        for (const auto& group : groups) {
            out += BuildModelGroupSignature(group);
        }
        return out;
    }

    static std::string MakeUniqueModelGroupName(const std::vector<ModelGroupEntry>& groups, const std::string& sourceName) {
        const std::string base = sourceName.empty() ? "ModelGroup" : sourceName;
        auto exists = [&](const std::string& candidate) {
            return std::any_of(groups.begin(), groups.end(), [&](const ModelGroupEntry& group) {
                return group.name == candidate;
            });
        };

        std::string candidate = base + "_Copy";
        int suffix = 2;
        while (exists(candidate)) {
            candidate = base + "_Copy" + std::to_string(suffix++);
        }
        return candidate;
    }

    static std::string BuildConfigBaseTransformSignature(const ConfigBase& config) {
        std::string out;
        out += config.overrideTransform ? '1' : '0';
        out += config.overrideMeshTransform ? '1' : '0';
        out += config.overrideGeometryTransform ? '1' : '0';
        out.push_back('|');
        out += BuildGenderedTransformSignature(config.transforms);
        out += BuildGenderedTransformSignature(config.meshTransforms);
        out += BuildGenderedTransformSignature(config.geometryTransforms);
        return out;
    }

    static std::string BuildConfigBasePhysicsSignature(const ConfigBase& config) {
        std::string out;
        out += config.overridePhysics ? '1' : '0';
        out.push_back('|');
        out += BuildPhysicsSignature(config.physics);
        return out;
    }

    static std::string BuildSlotMeshSignature(const SlotDefinition& slot) {
        std::string out = BuildConfigBaseTransformSignature(slot);
        AppendSignatureString(out, slot.holsterModelPath);
        out += slot.keepHolsterWhenDrawn ? '1' : '0';
        out.push_back('|');
        out += BuildGenderedTransformSignature(slot.holsterTransforms);
        return out;
    }

    static std::string BuildModelAnimationSignature(const ModelAnimationConfig& animation) {
        std::string out;
        out += animation.playSequence ? '1' : '0';
        out += animation.forwardAnimationEvents ? '1' : '0';
        out += animation.disableBehaviorGraphAnims ? '1' : '0';
        out.push_back('|');
        AppendSignatureString(out, animation.sequenceName);
        AppendSignatureString(out, animation.animationEvent);
        return out;
    }

    static std::string BuildModelLightConfigSignature(const ModelLightConfig& light) {
        std::string out;
        out += light.enabled ? '1' : '0';
        out.push_back('|');
        out += BuildTransformSignature(light.transform);
        AppendSignatureColor(out, light.diffuse);
        out += std::to_string(light.radius); out.push_back('|');
        out += std::to_string(light.dimmer); out.push_back('|');
        return out;
    }

    static std::string BuildModelEffectShaderSignature(const ModelEffectShaderConfig& effect) {
        std::string out;
        out += effect.enabled ? '1' : '0';
        out += effect.targetRoot ? '1' : '0';
        out += effect.force ? '1' : '0';
        out += effect.lighting ? '1' : '0';
        out += effect.alpha ? '1' : '0';
        out.push_back('|');
        AppendSignatureColor(out, effect.fillColor);
        AppendSignatureColor(out, effect.rimColor);
        out += std::to_string(effect.baseFillScale); out.push_back('|');
        out += std::to_string(effect.baseFillAlpha); out.push_back('|');
        out += std::to_string(effect.baseRimAlpha); out.push_back('|');
        out += std::to_string(effect.edgeExponent); out.push_back('|');
        out += std::to_string(effect.alphaMultiplier); out.push_back('|');
        AppendSignatureString(out, effect.baseTexturePath);
        AppendSignatureString(out, effect.paletteTexturePath);
        AppendSignatureString(out, effect.blockOutTexturePath);
        return out;
    }

    static void AppendModelSwapVariableSourceSignature(std::string& out, const ModelSwapVariableSource& source) {
        out += source.enabled ? '1' : '0';
        out.push_back('|');
        AppendSignatureString(out, source.pathVariable);
        AppendSignatureString(out, source.formIDVariable);
    }

    static std::string BuildCustomMeshSignature(const CustomDefinition& custom) {
        std::string out = BuildConfigBaseTransformSignature(custom);
        out += custom.removeEditorMarker ? '1' : '0';
        out += custom.removeProjectileTracers ? '1' : '0';
        out += custom.useWorldModel ? '1' : '0';
        out += custom.invisible ? '1' : '0';
        out += custom.hideGeometry ? '1' : '0';
        out += custom.hideLight ? '1' : '0';
        out += custom.load1pWeaponModel ? '1' : '0';
        out += custom.keepTorchFlame ? '1' : '0';
        out += custom.removeScabbard ? '1' : '0';
        out.push_back('|');
        AppendModelSwapVariableSourceSignature(out, custom.modelSwapVariableSource);
        out += BuildModelAnimationSignature(custom.animation);
        out += BuildModelEffectShaderSignature(custom.effectShader);
        out += BuildModelLightConfigSignature(custom.light);
        AppendSignatureString(out, custom.holsterModelPath);
        out += custom.keepHolsterWhenDrawn ? '1' : '0';
        out.push_back('|');
        out += BuildGenderedTransformSignature(custom.holsterTransforms);
        out += BuildModelGroupListSignature(custom.modelGroups);
        return out;
    }

    template <class T>
    static void AppendNumericVectorSignature(std::string& out, const std::vector<T>& values) {
        out += std::to_string(values.size());
        out.push_back('|');
        for (auto value : values) {
            out += std::to_string(static_cast<std::uint64_t>(value));
            out.push_back(',');
        }
        out.push_back('|');
    }

    static std::string BuildFormFilterSignature(const FormFilter& filter) {
        std::string out;
        out += filter.denyAll ? '1' : '0';
        out.push_back('|');
        out += std::to_string(filter.allowList.size());
        out.push_back('|');
        for (auto formID : filter.allowList) {
            out += std::to_string(formID);
            out.push_back(',');
        }
        out.push_back('|');
        out += std::to_string(filter.denyList.size());
        out.push_back('|');
        for (auto formID : filter.denyList) {
            out += std::to_string(formID);
            out.push_back(',');
        }
        out.push_back('|');
        out += filter.useProfile ? '1' : '0';
        out.push_back('|');
        AppendSignatureString(out, filter.profileName);
        return out;
    }

    static std::string BuildKeywordGroupsSignature(const std::vector<KeywordGroup>& groups) {
        std::string out;
        out += std::to_string(groups.size());
        out.push_back('|');
        for (const auto& group : groups) {
            AppendSignatureString(out, group.groupName);
            out += group.isAnd ? '1' : '0';
            out.push_back('|');
            out += std::to_string(group.keywords.size());
            out.push_back('|');
            for (const auto& keyword : group.keywords) {
                AppendSignatureString(out, keyword);
            }
        }
        return out;
    }

    static std::string BuildSlotEquipmentSignature(const SlotDefinition& slot) {
        std::string out;
        AppendNumericVectorSignature(out, slot.preferredItems);
        out += BuildFormFilterSignature(slot.itemFilter);
        out += UIConditionTreeSignature::Build(slot.itemFilterConditionTree);
        out += slot.extractMagazine ? '1' : '0';
        out += slot.useProjectileForAmmo ? '1' : '0';
        out += slot.removeEditorMarker ? '1' : '0';
        out += slot.removeProjectileTracers ? '1' : '0';
        out += slot.useWorldModel ? '1' : '0';
        out += slot.invisible ? '1' : '0';
        out += slot.hideGeometry ? '1' : '0';
        out += slot.load1pWeaponModel ? '1' : '0';
        out += slot.keepTorchFlame ? '1' : '0';
        out += slot.removeScabbard ? '1' : '0';
        out.push_back('|');
        AppendSignatureString(out, slot.modelSwapPath);
        out += std::to_string(slot.modelSwapFormID);
        out.push_back('|');
        out += BuildModelAnimationSignature(slot.animation);
        out += BuildModelEffectShaderSignature(slot.effectShader);
        out += BuildModelLightConfigSignature(slot.light);
        AppendModelSwapVariableSourceSignature(out, slot.modelSwapVariableSource);
        AppendNumericVectorSignature(out, slot.allowedFormTypes);
        AppendNumericVectorSignature(out, slot.formTypePriority);
        out += std::to_string(slot.formTypePriorityLimit);
        out.push_back('|');
        out += slot.formTypePriorityAccountForEquipped ? '1' : '0';
        out.push_back('|');
        out += std::to_string(static_cast<int>(slot.keywordMode));
        out.push_back('|');
        out += BuildKeywordGroupsSignature(slot.keywordGroups);
        out += slot.advancedFilters.useBaseFilters ? '1' : '0';
        out += slot.advancedFilters.allowMelee ? '1' : '0';
        out += slot.advancedFilters.allowGun ? '1' : '0';
        out += slot.advancedFilters.allowThrown ? '1' : '0';
        out += slot.advancedFilters.allowOneHanded ? '1' : '0';
        out += slot.advancedFilters.allowTwoHanded ? '1' : '0';
        out += slot.advancedFilters.allowArmor ? '1' : '0';
        out += slot.advancedFilters.allowShield ? '1' : '0';
        out += slot.advancedFilters.allowAmmo ? '1' : '0';
        out += slot.advancedFilters.allowMedicine ? '1' : '0';
        out += slot.advancedFilters.allowFood ? '1' : '0';
        out += slot.advancedFilters.allowWater ? '1' : '0';
        out += slot.advancedFilters.allowKeys ? '1' : '0';
        out.push_back('|');
        out += slot.ammoRig.isDedicatedAmmoSlot ? '1' : '0';
        out += slot.ammoRig.dynamicAmmoLogic ? '1' : '0';
        out.push_back('|');
        out += std::to_string(static_cast<int>(slot.ammoRig.displayMode));
        out.push_back('|');
        out += std::to_string(slot.ammoRig.maxMags);
        out.push_back('|');
        out += std::to_string(slot.ammoRig.magSpacing);
        out.push_back('|');
        AppendSignaturePoint(out, slot.ammoRig.arrayDirection);
        return out;
    }

    bool DrawConditionTreeEditor(IAD::ConditionNode& rootNode, bool showProfileControls) {
        const auto beforeSignature = UIConditionTreeSignature::Build(rootNode);

        if (showProfileControls) {
            if (UIManagedProfileControls::DrawSelector(
                "ConditionProfileSelector",
                TextLiteral("条件预设 (Condition Profile)"),
                IAD::Profile::GlobalProfileManager::GetSingleton().Conditions(),
                rootNode,
                "MyCondition",
                [](ConditionNode& target, const ConditionNode& source) {
                    target = source;
                },
                [](ConditionNode& target, const ConditionNode& source) {
                    if (!target.isGroup) {
                        ConditionNode oldRoot = target;
                        target = ConditionNode(true, true);
                        target.children.push_back(oldRoot);
                    }
                    target.children.push_back(source);
                },
                true)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::Button("Reset to empty##conditionProfile")) {
                rootNode = ConditionNode(true, true);
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::Separator();
        }

        ImGui::TextDisabled(TextLiteral("条件组可包含子组；组内可选择“任意满足”或“全部满足”。展开条件后再编辑类型和参数。"));
        ImGui::Separator();
        UIConditionTreeEditor::Draw(rootNode, true, 0);

        const bool changed = beforeSignature != UIConditionTreeSignature::Build(rootNode);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    template <class T>
    static bool DrawProfileNameSelector(const char* a_label, IAD::Profile::ProfileManager<T>& a_manager, std::string& a_value, const char* a_emptyLabel) {
        if (!a_manager.IsInitialized()) {
            a_manager.Load();
        }

        bool changed = false;
        ImGui::PushID(a_label);

        char nameBuf[128];
        strcpy_s(nameBuf, a_value.c_str());
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText(TextLiteral("预设名"), nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            a_value = nameBuf;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const char* preview = a_value.empty() ? a_emptyLabel : a_value.c_str();
        if (ImGui::BeginCombo(TextLiteral("选择预设"), preview, ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(a_emptyLabel, a_value.empty())) {
                a_value.clear();
                changed = true;
            }
            for (const auto& [name, record] : a_manager.Data()) {
                const bool selected = a_value == name;
                if (ImGui::Selectable(name.c_str(), selected)) {
                    a_value = name;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawStateMachineEditor(ConfigBase& a_config, const char* a_idSuffix, bool a_isNodeState) {
        bool changed = false;
        ImGui::PushID(a_idSuffix);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("状态覆写 (State Overrides)"));
        if (a_isNodeState) {
            ImGui::TextDisabled(TextLiteral("条件命中时可隐藏节点、切换宿主骨骼、覆写节点姿态/物理。"));
        }
        else {
            ImGui::TextDisabled(TextLiteral("条件命中时可隐藏模型、切换目标挂点、覆写模型姿态，或临时改用指定 NIF。"));
        }

        if (ImGui::Button(TextLiteral("添加状态覆写##addState"), { 160.0f, 0.0f })) {
            StateOverride state;
            state.description = TextLiteral("新状态");
            a_config.stateMachine.push_back(state);
            changed = true;
            UIEditCoordinator::RequestRuntimeRefresh();
        }

		int deleteIndex = -1;
		int moveIndex = -1;
		int moveDelta = 0;
		ImGui::SameLine();
		ImGui::TextDisabled(TextLiteral("%zu 条规则；仅第一条默认展开"), a_config.stateMachine.size());
		for (int i = 0; i < static_cast<int>(a_config.stateMachine.size()); ++i) {
			auto& state = a_config.stateMachine[i];
			ImGui::PushID(i);
            const std::string label = state.description.empty() ? (TextLiteral("状态覆写 ") + std::to_string(i + 1)) : state.description;
            const auto stateFlags = ImGuiTreeNodeFlags_SpanAvailWidth | (i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            if (ImGui::TreeNodeEx("stateOverride", stateFlags, "%s", label.c_str())) {
                char descBuf[128];
                strcpy_s(descBuf, state.description.c_str());
                ImGui::SetNextItemWidth(260.0f);
                ImGui::InputText(TextLiteral("描述"), descBuf, sizeof(descBuf));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    state.description = descBuf;
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }

                if (ImGui::Checkbox(TextLiteral("隐藏模型"), &state.hideModel)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }

                if (ImGui::Checkbox(TextLiteral("命中后继续评估后续状态"), &state.continueAfterMatch)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }

                if (!a_isNodeState && ImGui::TreeNodeEx(TextLiteral("模型与显示"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    if (ImGui::Checkbox(TextLiteral("覆写模型替换路径"), &state.overrideModelSwap)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideModelSwap) {
                        ImGui::Indent();
                        char pathBuf[260];
                        strcpy_s(pathBuf, state.modelSwapPath.c_str());
                        ImGui::SetNextItemWidth(440.0f);
                        ImGui::InputText(TextLiteral("模型路径"), pathBuf, sizeof(pathBuf));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            state.modelSwapPath = pathBuf;
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (UIFormEditorControls::DrawFormIDField(TextLiteral("模型 FormID"), state.modelSwapFormID)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (UIModelEditorControls::DrawModelSwapVariableSource(state.modelSwapVariableSource, "state_model_var_source")) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::TextDisabled(TextLiteral("路径优先于 FormID；两者为空时清除前面的模型替换。"));
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox(TextLiteral("覆写模型显示/清理开关"), &state.overrideDisplayFlags)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideDisplayFlags) {
                        ImGui::Indent();
                        if (ImGui::Checkbox(TextLiteral("提取武器弹匣"), &state.extractMagazine)) {
                            if (state.extractMagazine) state.useProjectileForAmmo = false;
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (state.extractMagazine) ImGui::BeginDisabled();
                        if (ImGui::Checkbox(TextLiteral("弹药改用射弹模型"), &state.useProjectileForAmmo)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (state.extractMagazine) ImGui::EndDisabled();
                        if (ImGui::Checkbox(TextLiteral("使用世界/基础模型"), &state.useWorldModel)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("隐藏几何 / alpha 0"), &state.invisible)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("仅隐藏几何，保留挂载节点"), &state.hideGeometry)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("隐藏附加灯光"), &state.hideLight)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("加载第一人称武器模型"), &state.load1pWeaponModel)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("保留火焰/喷焰 FX"), &state.keepTorchFlame)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (ImGui::Checkbox(TextLiteral("移除刀鞘/枪套节点"), &state.removeScabbard)) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (UIModelEditorControls::DrawModelCleanupSettings(state.disableHavok, state.removeEditorMarker, state.removeProjectileTracers, "state_display_cleanup")) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::TextDisabled(TextLiteral("开启后这些值会替换 Slot/Custom 的对应模型显示与清理设置。"));
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox(TextLiteral("覆写模型动画"), &state.overrideAnimation)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideAnimation) {
                        ImGui::Indent();
                        if (UIModelEditorControls::DrawModelAnimationSettings(state.animation, "state_animation")) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox(TextLiteral("覆写模型 Effect Shader"), &state.overrideEffectShader)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideEffectShader) {
                        ImGui::Indent();
                        if (UIModelEditorControls::DrawModelEffectShaderSettings(state.effectShader, "state_effect")) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox(TextLiteral("覆写模型附加灯光"), &state.overrideLight)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideLight) {
                        ImGui::Indent();
                        if (UIModelEditorControls::DrawModelLightSettings(state.light, "state_light")) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::Unindent();
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(TextLiteral("挂点、姿态与物理"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (ImGui::Checkbox(TextLiteral("覆写目标挂点"), &state.overrideTargetNode)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (state.overrideTargetNode) {
                    ImGui::Indent();
                    char nodeBuf[128];
                    strcpy_s(nodeBuf, state.targetNode.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText(TextLiteral("目标节点"), nodeBuf, sizeof(nodeBuf));
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        state.targetNode = nodeBuf;
                        changed = true;
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::Unindent();
                }

                if (ImGui::Checkbox(TextLiteral("覆写姿态"), &state.overrideTransform)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (state.overrideTransform) {
                    ImGui::Indent();
                    if (ImGui::Checkbox(TextLiteral("使用姿态预设"), &state.useTransformPreset)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.useTransformPreset) {
                        if (DrawProfileNameSelector("stateTransformPreset", IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(), state.targetTransformPreset, TextLiteral("[未选择]"))) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    else {
                        ImGui::PushID("stateTransform");
                        TransformData defaultTransform;
                        bool transformChanged = false;
                        transformChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("位置"), state.independentTransform.pos, defaultTransform.pos, 0.04f, false);
                        transformChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("旋转"), state.independentTransform.rot, defaultTransform.rot, 0.04f, true);
                        transformChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("轴心"), state.independentTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat(TextLiteral("缩放"), &state.independentTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            transformChanged = true;
                        }
                        if (transformChanged) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::PopID();
                    }
                    if (a_isNodeState) {
                        if (ImGui::Checkbox(TextLiteral("绝对坐标"), &state.absolutePosition)) {
                            changed = true;
                            NodeManager::InvalidateForConfigRefresh();
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled(TextLiteral("武器/身形自适应：当前变换路径不支持"));
                    }
                    ImGui::Unindent();
                }

                if (!a_isNodeState) {
                    if (ImGui::Checkbox(TextLiteral("覆写模型网格变换"), &state.overrideMeshTransform)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideMeshTransform) {
                        ImGui::Indent();
                        ImGui::PushID("stateMeshTransform");
                        TransformData defaultTransform;
                        bool meshChanged = false;
                        meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("位置"), state.independentMeshTransform.pos, defaultTransform.pos, 0.04f, false);
                        meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("旋转"), state.independentMeshTransform.rot, defaultTransform.rot, 0.04f, true);
                        meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("轴心"), state.independentMeshTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat(TextLiteral("缩放"), &state.independentMeshTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            meshChanged = true;
                        }
                        if (meshChanged) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::PopID();
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox(TextLiteral("覆写几何层变换"), &state.overrideGeometryTransform)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.overrideGeometryTransform) {
                        ImGui::Indent();
                        ImGui::PushID("stateGeometryTransform");
                        TransformData defaultTransform;
                        bool geometryChanged = false;
                        geometryChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何位置"), state.independentGeometryTransform.pos, defaultTransform.pos, 0.04f, false);
                        geometryChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何旋转"), state.independentGeometryTransform.rot, defaultTransform.rot, 0.04f, true);
                        geometryChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何轴心"), state.independentGeometryTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat(TextLiteral("几何缩放"), &state.independentGeometryTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            geometryChanged = true;
                        }
                        if (geometryChanged) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::PopID();
                        ImGui::Unindent();
                    }
                }

                if (ImGui::Checkbox(TextLiteral("覆写物理"), &state.overridePhysics)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (state.overridePhysics) {
                    ImGui::Indent();
                    if (ImGui::Checkbox(TextLiteral("使用物理预设"), &state.usePhysicsPreset)) {
                        changed = true;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (state.usePhysicsPreset) {
                        if (DrawProfileNameSelector("statePhysicsPreset", IAD::Profile::GlobalProfileManager::GetSingleton().Physics(), state.targetPhysicsPreset, TextLiteral("[未选择]"))) {
                            changed = true;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    else if (ImGui::TreeNodeEx(TextLiteral("独立物理参数"), ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (DrawPhysicsPanel(state.independentPhysics)) {
                            changed = true;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("命中条件:"));
                if (DrawConditionTreeEditor(state.conditionTree)) {
                    changed = true;
                }

                ImGui::TreePop();
                }

				if (i <= 0) ImGui::BeginDisabled();
				if (ImGui::Button(TextLiteral("上移##moveStateUp"), { 70.0f, 0.0f })) {
					moveIndex = i;
					moveDelta = -1;
				}
				if (i <= 0) ImGui::EndDisabled();
				ImGui::SameLine();
				if (i >= static_cast<int>(a_config.stateMachine.size()) - 1) ImGui::BeginDisabled();
				if (ImGui::Button(TextLiteral("下移##moveStateDown"), { 70.0f, 0.0f })) {
					moveIndex = i;
					moveDelta = 1;
				}
				if (i >= static_cast<int>(a_config.stateMachine.size()) - 1) ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button(TextLiteral("删除状态覆写##deleteState"), { 140.0f, 0.0f })) {
					deleteIndex = i;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (moveIndex >= 0 && moveDelta != 0) {
			const int targetIndex = moveIndex + moveDelta;
			if (targetIndex >= 0 && targetIndex < static_cast<int>(a_config.stateMachine.size())) {
				std::swap(a_config.stateMachine[moveIndex], a_config.stateMachine[targetIndex]);
				changed = true;
				UIEditCoordinator::RequestRuntimeRefresh();
			}
		}

		if (deleteIndex >= 0 && deleteIndex < static_cast<int>(a_config.stateMachine.size())) {
			a_config.stateMachine.erase(a_config.stateMachine.begin() + deleteIndex);
            changed = true;
            UIEditCoordinator::RequestRuntimeRefresh();
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawWindowHeader(const char* idStr, ImGuiManager::WindowState& state, bool showTarget, bool showGender) {
        bool propagateTriggered = false;
        ImGui::PushID(idStr);

        // Keep the scope selector visible, but move the verbose target controls
        // behind one compact disclosure. The editor list should remain near the
        // top of the window instead of being pushed below rarely changed filters.
        auto drawScopeTab = [&state](const char* label, ConfigScope scope) {
            const auto flags = !state.scopeTabInitialized && state.scope == scope ?
                ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label, nullptr, flags)) {
                state.scope = scope;
                state.scopeTabInitialized = true;
                ImGui::EndTabItem();
            }
        };

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
        if (ImGui::BeginTabBar("ScopeTabBar", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoTooltip)) {
            drawScopeTab(TextLiteral("种族"), ConfigScope::kRace);
            drawScopeTab("NPC", ConfigScope::kNPC);
            drawScopeTab(TextLiteral("角色"), ConfigScope::kActor);
            drawScopeTab(TextLiteral("全局"), ConfigScope::kGlobal);
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();

        const char* scopeName = TextLiteral("全局");
        if (state.scope == ConfigScope::kRace) scopeName = TextLiteral("种族");
        else if (state.scope == ConfigScope::kNPC) scopeName = "NPC";
        else if (state.scope == ConfigScope::kActor) scopeName = TextLiteral("角色");

        std::string summary = TextLiteral("范围: ");
        summary += scopeName;
        if (state.scope == ConfigScope::kGlobal) {
            summary += " | ";
            summary += showTarget ? (state.targetFilter == 1 ? TextLiteral("仅玩家") : state.targetFilter == 2 ? TextLiteral("仅 NPC") : TextLiteral("全部实体")) : TextLiteral("全局兜底");
        }
        else {
            char idBuffer[16];
            sprintf_s(idBuffer, "%08X", state.id);
            summary += " | ";
            summary += idBuffer;
        }
        if (showGender) {
            summary += " | ";
            summary += state.genderEdit == 1 ? TextLiteral("女性") : TextLiteral("男性");
            if (state.syncGender) summary += TextLiteral(" (同步)");
        }
        ImGui::TextDisabled("%s", summary.c_str());

        const std::string contextID = std::string(TextLiteral("范围与目标##")) + idStr;
        if (ImGui::CollapsingHeader(contextID.c_str(), ImGuiTreeNodeFlags_None)) {
            // This section only contains one or two compact rows. Rendering it
            // directly avoids a nested child scroll bar when the gender row is
            // present.
            ImGui::BeginGroup();
            if (state.scope == ConfigScope::kGlobal) {
                if (showTarget) {
                    ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, TextLiteral("目标"));
                    ImGui::SameLine();
                    ImGui::RadioButton(TextLiteral("全部实体"), &state.targetFilter, 0);
                    ImGui::SameLine(0, 12.0f);
                    ImGui::RadioButton(TextLiteral("仅限玩家"), &state.targetFilter, 1);
                    ImGui::SameLine(0, 12.0f);
                    ImGui::RadioButton(TextLiteral("仅限 NPC"), &state.targetFilter, 2);
                }
                else {
                    ImGui::TextDisabled(TextLiteral("全局配置会作为所有实体的最终兜底。"));
                }
            }
            else {
                ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, TextLiteral("目标 FormID"));
                ImGui::SameLine();
                char buf[16];
                sprintf_s(buf, "%08X", state.id);
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::InputText("##IDInput", buf, 16, ImGuiInputTextFlags_CharsHexadecimal)) {
                    try {
                        state.id = std::stoul(buf, nullptr, 16);
                    }
                    catch (...) {
                        state.id = 0;
                    }
                }
                ImGui::SameLine();

                if (state.scope == ConfigScope::kRace) {
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::BeginCombo("##raceCombo", TextLiteral("点击选择游戏种族..."))) {
                        auto dataHandler = RE::TESDataHandler::GetSingleton();
                        if (dataHandler) {
                            for (auto race : dataHandler->GetFormArray<RE::TESRace>()) {
                                if (race && race->GetFormID() > 0) {
                                    char rLabel[128];
                                    sprintf_s(rLabel, "[%08X] %s", race->GetFormID(), race->GetFormEditorID());
                                    if (ImGui::Selectable(rLabel)) state.id = race->GetFormID();
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else {
                    std::string popupName = std::string("ScannerPopup##") + std::string(idStr);
                    if (ImGui::Button(TextLiteral("扫描附近角色"))) ImGui::OpenPopup(popupName.c_str());
                    if (ImGui::BeginPopup(popupName.c_str())) {
                        auto player = RE::PlayerCharacter::GetSingleton();
                        if (player) {
                            auto pPos = player->GetPosition();
                            uint32_t pid = state.scope == ConfigScope::kNPC && player->data.objectReference ?
                                player->data.objectReference->GetFormID() : player->GetFormID();
                            const std::string playerLabel = TextLiteral("[玩家本身] ") + std::string(player->GetDisplayFullName()) + "###scanner_actor_" + std::to_string(pid);
                            if (ImGui::Selectable(playerLabel.c_str())) state.id = pid;
                            ImGui::Separator();
                            if (auto pl = RE::ProcessLists::GetSingleton()) {
                                for (auto& handle : pl->highActorHandles) {
                                    auto act = handle.get();
                                    if (act && act.get() && act.get() != player) {
                                        const float dx = pPos.x - act->GetPosition().x;
                                        const float dy = pPos.y - act->GetPosition().y;
                                        const float dz = pPos.z - act->GetPosition().z;
                                        const float dist = std::sqrtf(dx * dx + dy * dy + dz * dz);
                                        if (dist < 2500.0f) {
                                            uint32_t cid = state.scope == ConfigScope::kNPC && act->data.objectReference ?
                                                act->data.objectReference->GetFormID() : act->GetFormID();
                                            char label[128];
                                            sprintf_s(label, TextLiteral("[距离:%d] %s"), static_cast<int>(dist), act->GetDisplayFullName());
                                            const std::string actorLabel = std::string(label) + "###scanner_actor_" + std::to_string(cid);
                                            if (ImGui::Selectable(actorLabel.c_str())) state.id = cid;
                                        }
                                    }
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            if (showGender) {
                ImGui::Separator();
                ImGui::TextColored({ 1.0f, 0.6f, 0.8f, 1.0f }, TextLiteral("性别"));
                ImGui::SameLine();
                ImGui::RadioButton(TextLiteral("男性"), &state.genderEdit, 0);
                ImGui::SameLine(0, 12.0f);
                ImGui::RadioButton(TextLiteral("女性"), &state.genderEdit, 1);
                ImGui::SameLine(0, 14.0f);
                ImGui::Checkbox(TextLiteral("同步至另一性别"), &state.syncGender);
                if (state.scope != ConfigScope::kGlobal) {
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("向上层传播"))) propagateTriggered = true;
                }
            }
            ImGui::EndGroup();
        }

        ImGui::PopID();
        return propagateTriggered;
    }

    static std::string GetScopeName(ConfigScope scope) {
        switch (scope) {
        case ConfigScope::kActor: return "Actor";
        case ConfigScope::kNPC: return "NPC";
        case ConfigScope::kRace: return "Race";
        default: return "Global";
        }
    }

    static std::string FormatScopedTargetLabel(ConfigScope scope, std::uint32_t id) {
        std::string name;
        if (auto form = RE::TESForm::GetFormByID(id)) {
            if (auto fullName = form->As<RE::TESFullName>(); fullName && fullName->GetFullName()) {
                name = fullName->GetFullName();
            }
            if (name.empty()) {
                if (auto race = form->As<RE::TESRace>()) {
                    if (auto editorID = race->GetFormEditorID(); editorID && editorID[0] != '\0') {
                        name = editorID;
                    }
                }
            }
        }

        char idBuf[16];
        sprintf_s(idBuf, "%08X", id);
        std::string label = "[" + std::string(idBuf) + "] " + GetScopeName(scope);
        if (!name.empty()) {
            label += " - ";
            label += name;
        }
        else {
            label += TextLiteral(" - [未加载]");
        }
        return label;
    }

    static void DrawConfiguredTargetBrowser(const char* idStr, ImGuiManager::WindowState& state, const std::vector<std::uint32_t>& configuredIDs) {
        if (state.scope == ConfigScope::kGlobal) return;

        ImGui::PushID(idStr);
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("配置目标"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        std::string preview = configuredIDs.empty() ? TextLiteral("当前层暂无配置目标") : FormatScopedTargetLabel(state.scope, state.id);
        if (ImGui::BeginCombo("##configuredTargetCombo", preview.c_str())) {
            for (auto id : configuredIDs) {
                auto label = FormatScopedTargetLabel(state.scope, id);
                bool selected = (state.id == id);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    state.id = id;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(TextLiteral("%zu 个目标"), configuredIDs.size());
        ImGui::PopID();
    }

    static bool DrawTransformProfileControls(const char* idStr, TransformData& transform) {
        const bool changed = UIManagedProfileControls::DrawSelector(
            idStr,
            TextLiteral("姿态预设 (Transform Profile)"),
            IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(),
            transform,
            "MyTransform",
            [](TransformData& target, const TransformData& source) {
                target = source;
            },
            [](TransformData&, const TransformData&) {},
            false);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static bool DrawPhysicsProfileControls(const char* idStr, PhysicsValues& physics) {
        const bool changed = UIManagedProfileControls::DrawSelector(
            idStr,
            TextLiteral("物理预设 (Physics Profile)"),
            IAD::Profile::GlobalProfileManager::GetSingleton().Physics(),
            physics,
            "MyPhysics",
            [](PhysicsValues& target, const PhysicsValues& source) {
                target = source;
            },
            [](PhysicsValues&, const PhysicsValues&) {},
            false);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    template <class T, class KeyGetter>
    static int MergeConfigListByKey(std::vector<T>& target, const std::vector<T>& source, KeyGetter keyGetter, bool overwriteExisting) {
        int changed = 0;
        for (const auto& sourceEntry : source) {
            auto sourceKey = keyGetter(sourceEntry);
            auto it = std::find_if(target.begin(), target.end(), [&](const T& targetEntry) {
                return keyGetter(targetEntry) == sourceKey;
                });
            if (it == target.end()) {
                target.push_back(sourceEntry);
                ++changed;
            }
            else if (overwriteExisting) {
                *it = sourceEntry;
                ++changed;
            }
        }
        return changed;
    }

    static bool DrawSkeletonMatchEditor(NodeDefinition::SkeletonMatchConfig& a_match) {
        bool changed = false;
        if (ImGui::Checkbox(TextLiteral("启用骨架匹配"), &a_match.enabled)) changed = true;
        if (!a_match.enabled) return changed;

        ImGui::Indent();
        if (ImGui::Checkbox(TextLiteral("反向匹配"), &a_match.invert)) changed = true;

        char pathBuf[160];
        strcpy_s(pathBuf, a_match.skeletonPathContains.c_str());
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputText(TextLiteral("骨架路径包含"), pathBuf, 160)) {
            a_match.skeletonPathContains = pathBuf;
            changed = true;
        }

        changed |= UIFormEditorControls::DrawFormIDField("Race FormID", a_match.raceFormID);
        changed |= UIFormEditorControls::DrawFormIDField("NPC FormID", a_match.npcFormID);
        changed |= UIFormEditorControls::DrawStringVectorEditor(TextLiteral("必须存在的骨骼节点"), a_match.requiredNodes, "SPINE2");
        changed |= UIFormEditorControls::DrawStringVectorEditor(TextLiteral("不能存在的骨骼节点"), a_match.forbiddenNodes, "");
        ImGui::Unindent();
        return changed;
    }

    static bool DrawModelGroupAdvancedConfig(ModelGroupEntry& a_group) {
        bool changed = false;
        if (ImGui::CollapsingHeader(TextLiteral("高级模型组功能"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("模型来源"));
            const char* sourceModes[] = { TextLiteral("NIF 路径"), TextLiteral("FormID 模型") };
            int sourceMode = std::clamp(a_group.sourceMode, 0, 1);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo(TextLiteral("来源模式"), &sourceMode, sourceModes, 2)) {
                a_group.sourceMode = sourceMode;
                changed = true;
            }
            if (a_group.sourceMode == 1) {
                changed |= UIFormEditorControls::DrawFormIDField(TextLiteral("来源 FormID"), a_group.sourceFormID);
                if (ImGui::Checkbox(TextLiteral("从来源武器提取弹匣"), &a_group.extractMagazine)) changed = true;
                bool disableProjectile = a_group.extractMagazine;
                if (disableProjectile) ImGui::BeginDisabled();
                if (ImGui::Checkbox(TextLiteral("弹药来源使用射弹模型"), &a_group.useProjectileForAmmo)) changed = true;
                if (disableProjectile) ImGui::EndDisabled();
            }
            else {
                char pathBuf[256];
                strncpy_s(pathBuf, a_group.modelPath.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(450.0f);
                ImGui::InputText(TextLiteral("模型路径 (NIF)"), pathBuf, 256);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    a_group.modelPath = pathBuf;
                    changed = true;
                }
            }

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("模型标志"));
            ImGui::TextDisabled(TextLiteral("Havok/碰撞：展示模型始终为渲染副本"));
            ImGui::SameLine();
            if (ImGui::Checkbox(TextLiteral("移除编辑器标记"), &a_group.removeEditorMarker)) changed = true;
            if (ImGui::Checkbox(TextLiteral("移除射弹轨迹/特效节点"), &a_group.removeProjectileTracers)) changed = true;
            if (ImGui::Checkbox(TextLiteral("隐藏几何 / alpha 0 (Invisible)"), &a_group.invisible)) changed = true;
            if (ImGui::Checkbox(TextLiteral("仅隐藏几何，保留挂载节点 (Hide Geometry)"), &a_group.hideGeometry)) changed = true;
            if (ImGui::Checkbox(TextLiteral("隐藏附加灯光 (Hide Light)"), &a_group.hideLight)) changed = true;
            if (ImGui::Checkbox(TextLiteral("加载第一人称武器模型 (Load 1P Weapon Model)"), &a_group.load1pWeaponModel)) changed = true;
            if (ImGui::Checkbox(TextLiteral("保留火焰/喷焰 FX (Keep Torch Flame)"), &a_group.keepTorchFlame)) changed = true;
            if (ImGui::Checkbox(TextLiteral("移除刀鞘/枪套节点 (Remove Scabbard)"), &a_group.removeScabbard)) changed = true;

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("Effect Shader 数据"));
            if (ImGui::Checkbox(TextLiteral("启用效果数据"), &a_group.effectShader.enabled)) changed = true;
            if (a_group.effectShader.enabled) {
                ImGui::Indent();
                if (ImGui::Checkbox("Target Root", &a_group.effectShader.targetRoot)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Force", &a_group.effectShader.force)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Lighting", &a_group.effectShader.lighting)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Alpha", &a_group.effectShader.alpha)) changed = true;
                changed |= UITransformEditorControls::DrawColorRGBA("Fill Color", a_group.effectShader.fillColor);
                changed |= UITransformEditorControls::DrawColorRGBA("Rim Color", a_group.effectShader.rimColor);
                if (ImGui::DragFloat(TextLiteral("透明度倍率"), &a_group.effectShader.alphaMultiplier, 0.01f, 0.0f, 1.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Fill Scale", &a_group.effectShader.baseFillScale, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Fill Alpha", &a_group.effectShader.baseFillAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Rim Alpha", &a_group.effectShader.baseRimAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Edge Exponent", &a_group.effectShader.edgeExponent, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                char texBuf[256];
                strcpy_s(texBuf, a_group.effectShader.baseTexturePath.c_str());
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputText("Base Texture", texBuf, 256);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    a_group.effectShader.baseTexturePath = texBuf;
                    changed = true;
                }
                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("Extra Light 数据"));
            if (ImGui::Checkbox(TextLiteral("启用附加灯光数据"), &a_group.light.enabled)) changed = true;
            if (a_group.light.enabled) {
                ImGui::Indent();
                ImGui::TextDisabled(TextLiteral("Target/Water/Landscape/Shadows：当前渲染路径不支持"));
                changed |= UITransformEditorControls::DrawColorRGBA("Diffuse", a_group.light.diffuse);
                if (ImGui::DragFloat("Radius", &a_group.light.radius, 1.0f, 0.0f, 4096.0f, "%.1f")) changed = true;
                if (ImGui::DragFloat("Dimmer", &a_group.light.dimmer, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                ImGui::TextDisabled(TextLiteral("FOV/Shadow Bias：当前渲染路径不支持"));
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("灯光位置偏移"), a_group.light.transform.pos, { 0,0,0 }, 0.5f, false);
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("灯光旋转"), a_group.light.transform.rot, { 0,0,0 }, 1.0f, true);
                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("动画/Sequence 数据"));
            if (ImGui::Checkbox(TextLiteral("播放 NiController Sequence"), &a_group.animation.playSequence)) changed = true;
            if (ImGui::Checkbox(TextLiteral("转发动画事件"), &a_group.animation.forwardAnimationEvents)) changed = true;
            ImGui::SameLine();
            ImGui::TextDisabled(TextLiteral("SubGraphs：FO4 展示副本不支持"));
            if (ImGui::Checkbox(TextLiteral("禁用行为图动画"), &a_group.animation.disableBehaviorGraphAnims)) changed = true;
            char seqBuf[128];
            strcpy_s(seqBuf, a_group.animation.sequenceName.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText(TextLiteral("Sequence 名称"), seqBuf, 128)) {
                a_group.animation.sequenceName = seqBuf;
                changed = true;
            }
            char eventBuf[128];
            strcpy_s(eventBuf, a_group.animation.animationEvent.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText(TextLiteral("动画事件"), eventBuf, 128)) {
                a_group.animation.animationEvent = eventBuf;
                changed = true;
            }
        }
        return changed;
    }

    static void DrawFormFilterUI(FormFilter& a_filter, const char* a_id) {
        ImGui::PushID(a_id);
        ImGui::Checkbox(TextLiteral("一键拒绝所有物品 (Deny All)"), &a_filter.denyAll); ImGui::TextDisabled(TextLiteral("开启后，该槽位将被强制清空，无视以下列表。"));
        ImGui::BeginDisabled(a_filter.denyAll);
        if (ImGui::CollapsingHeader(TextLiteral("允许列表 (Allow List) - 强制允许上背的物品"))) UIFormEditorControls::DrawFormSetUI(a_filter.allowList, "AllowList");
        if (ImGui::CollapsingHeader(TextLiteral("拒绝列表 (Deny List) - 排除黑名单物品"))) UIFormEditorControls::DrawFormSetUI(a_filter.denyList, "DenyList");
        ImGui::EndDisabled(); ImGui::PopID();
    }

	static bool DrawSlotFormFilterProfileSelector(FormFilter& a_filter) {
		auto& manager = IAD::Profile::GlobalProfileManager::GetSingleton().FormFilters();
		if (!manager.IsInitialized()) {
			manager.Load();
		}

		bool changed = false;
		if (ImGui::Checkbox(TextLiteral("动态引用过滤器预设 (IAD)"), &a_filter.useProfile)) {
			changed = true;
			if (!a_filter.useProfile) {
				a_filter.profileName.clear();
			}
		}

		if (!a_filter.useProfile) {
			changed |= UIManagedProfileControls::DrawSelector(
				"SlotItemFilterProfileSelector",
				TextLiteral("过滤器预设 (Form Filter Profile)"),
				manager,
				a_filter,
				"MyFormFilter",
				[](FormFilter& target, const FormFilter& source) {
					target = source;
					target.useProfile = false;
					target.profileName.clear();
				},
				[](FormFilter& target, const FormFilter& source) {
					target.denyAll = target.denyAll || source.denyAll;
					target.allowList.insert(source.allowList.begin(), source.allowList.end());
					target.denyList.insert(source.denyList.begin(), source.denyList.end());
				},
				true);
			return changed;
		}

		const auto* activeRecord = manager.Find(a_filter.profileName);
		ImGui::SetNextItemWidth(260.0f);
		const char* preview = a_filter.profileName.empty() ? TextLiteral("选择预设...") : a_filter.profileName.c_str();
		if (ImGui::BeginCombo(TextLiteral("运行时预设"), preview, ImGuiComboFlags_HeightLarge)) {
			for (const auto& [name, record] : manager.Data()) {
				const bool selected = a_filter.profileName == name;
				if (ImGui::Selectable(name.c_str(), selected)) {
					a_filter.profileName = name;
					activeRecord = manager.Find(name);
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (!activeRecord || activeRecord->parserErrors) {
			ImGui::TextColored({ 1.0f, 0.65f, 0.25f, 1.0f }, TextLiteral("预设不存在或无法解析；该插槽不会接受候选物品。"));
		}
		else {
			ImGui::TextDisabled(TextLiteral("运行时读取此预设；保存该预设后，所有链接插槽会同时更新。"));
			if (ImGui::Button(TextLiteral("解除引用并复制当前预设"))) {
				a_filter = activeRecord->data;
				a_filter.useProfile = false;
				a_filter.profileName.clear();
				changed = true;
			}
		}

		return changed;
	}

    static bool DrawTabEquipment(SlotDefinition& a_slot) {
        const auto beforeSignature = BuildSlotEquipmentSignature(a_slot);
        ImGui::Spacing();

        if (ImGui::TreeNodeEx(TextLiteral("优先物品"), ImGuiTreeNodeFlags_None)) {
			ImGui::TextDisabled(TextLiteral("只要背包里有列表中的物品，无视其他所有过滤条件，直接强制上背！"));
			if (UIFormEditorControls::DrawFormVectorUI(a_slot.preferredItems, "PrefItems")) {
				UIEditCoordinator::RequestRuntimeRefresh();
			}
			ImGui::TreePop();
		}
		ImGui::Spacing();
		if (ImGui::TreeNodeEx(TextLiteral("候选选择模式"), ImGuiTreeNodeFlags_None)) {
			ImGui::TextDisabled(TextLiteral("首选物品始终优先。选择模式只作用于其余合格候选；无结果时回退到常规候选。\n"));
            const char* modes[] = { TextLiteral("最近装备 (IAD 默认)"), TextLiteral("最强 (按当前改装后的评分)"), TextLiteral("随机 (稳定选择)") };
			int mode = static_cast<int>(a_slot.selectionMode);
			ImGui::SetNextItemWidth(260.0f);
			if (ImGui::Combo(TextLiteral("选择模式"), &mode, modes, IM_ARRAYSIZE(modes))) {
				a_slot.selectionMode = static_cast<SlotSelectionMode>(mode);
				UIEditCoordinator::RequestRuntimeRefresh();
			}
			ImGui::TreePop();
		}
        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("物品过滤"), ImGuiTreeNodeFlags_None)) {
			if (DrawSlotFormFilterProfileSelector(a_slot.itemFilter)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::Button(TextLiteral("清空重置"))) {
                a_slot.itemFilter = IAD::FormFilter();
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::Separator();

			if (a_slot.itemFilter.useProfile) ImGui::BeginDisabled();
			DrawFormFilterUI(a_slot.itemFilter, "ItemFilter");
			if (a_slot.itemFilter.useProfile) ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::Spacing();

        if (ImGui::TreeNodeEx(TextLiteral("候选条件"), ImGuiTreeNodeFlags_None)) {
            ImGui::TextDisabled(TextLiteral("这些条件会在每个候选物品被分配到该槽位前评估。"));
            if (ImGui::Button(TextLiteral("清空候选条件##slotCandidateConditions"))) {
                a_slot.itemFilterConditionTree = IAD::ConditionNode(true, true);
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (DrawConditionTreeEditor(a_slot.itemFilterConditionTree)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::TreePop();
        }
        ImGui::Spacing();

        if (ImGui::TreeNodeEx(TextLiteral("类型优先级"), ImGuiTreeNodeFlags_None)) {
            ImGui::TextDisabled(TextLiteral("按槽位优先级控制该槽优先接收哪类物品；空列表表示保持全局候选顺序。"));
            const char* typeNames[] = { "WEAP", "ARMO", "AMMO", "ALCH", "MISC" };
            for (const auto* typeName : typeNames) {
                const auto type = ConfigManager::StringToFormType(typeName);
                bool enabled = std::find(a_slot.formTypePriority.begin(), a_slot.formTypePriority.end(), type) != a_slot.formTypePriority.end();
                if (ImGui::Checkbox(typeName, &enabled)) {
                    if (enabled) {
                        a_slot.formTypePriority.push_back(type);
                    }
                    else {
                        a_slot.formTypePriority.erase(std::remove(a_slot.formTypePriority.begin(), a_slot.formTypePriority.end(), type), a_slot.formTypePriority.end());
                    }
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            for (std::size_t i = 0; i < a_slot.formTypePriority.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%zu. %s", i + 1, ConfigManager::FormTypeToString(a_slot.formTypePriority[i]).c_str());
                ImGui::SameLine();
                if (ImGui::Button("Up") && i > 0) {
                    std::swap(a_slot.formTypePriority[i], a_slot.formTypePriority[i - 1]);
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && i + 1 < a_slot.formTypePriority.size()) {
                    std::swap(a_slot.formTypePriority[i], a_slot.formTypePriority[i + 1]);
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt(TextLiteral("活跃类型上限 (0=不限)"), &a_slot.formTypePriorityLimit)) {
                if (a_slot.formTypePriorityLimit < 0) a_slot.formTypePriorityLimit = 0;
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::Checkbox(TextLiteral("已装备类型优先计入 (Account For Equipped)"), &a_slot.formTypePriorityAccountForEquipped)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::Button(TextLiteral("清空类型优先级##clearTypePriority"))) {
                a_slot.formTypePriority.clear();
                a_slot.formTypePriorityLimit = 0;
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::TreePop();
        }
        ImGui::Spacing();

        auto& af = a_slot.advancedFilters;
        if (ImGui::CollapsingHeader(TextLiteral("高级候选过滤"))) {
        if (ImGui::TreeNodeEx(TextLiteral("基础类型过滤"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::Checkbox(TextLiteral("启用大类过滤 (Enable Base Filters)"), &af.useBaseFilters);
            if (af.useBaseFilters) {
                ImGui::Indent();
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("武器 (WEAP):")); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("枪械 (Guns)"), &af.allowGun); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("单手近战 (1H Melee)"), &af.allowOneHanded); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("双手近战 (2H Melee)"), &af.allowTwoHanded); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("徒手/拳套 (Unarmed)"), &af.allowMelee); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("投掷物 (Thrown)"), &af.allowThrown);

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("护甲 (ARMO):")); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("普通护甲"), &af.allowArmor); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("盾牌 (Shield)"), &af.allowShield);

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("消耗/杂项:")); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("弹药 (AMMO)"), &af.allowAmmo); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("药品 (Medicine)"), &af.allowMedicine); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("食物 (Food)"), &af.allowFood); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("水 (Water)"), &af.allowWater); ImGui::SameLine();
                ImGui::Checkbox(TextLiteral("钥匙 (Key)"), &af.allowKeys);
                ImGui::Unindent();
            }
            else {
                ImGui::TextDisabled(TextLiteral("  (未启用：允许任何类型的物品，包括衣服)"));
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("关键字过滤组"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            const char* modeNames[] = { TextLiteral("禁用 (无视关键字)"), TextLiteral("白名单模式 (必须命中下方至少一组)"), TextLiteral("黑名单模式 (命中下方任意一组即被隐藏)") };
            int modeInt = static_cast<int>(a_slot.keywordMode);
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo(TextLiteral("过滤模式"), &modeInt, modeNames, 3)) {
                a_slot.keywordMode = static_cast<IAD::KeywordFilterMode>(modeInt);
            }

            if (a_slot.keywordMode != IAD::KeywordFilterMode::kNone) {
                ImGui::Separator();
                auto& groups = a_slot.keywordGroups;
                for (size_t gIdx = 0; gIdx < groups.size(); gIdx++) {
                    auto& grp = groups[gIdx];
                    ImGui::PushID(static_cast<int>(gIdx));

                    char gName[64]; strcpy_s(gName, grp.groupName.c_str());
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::InputText("##gname", gName, 64)) grp.groupName = gName;
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("删除此组"))) { groups.erase(groups.begin() + gIdx); gIdx--; ImGui::PopID(); continue; }

                    ImGui::Indent();
                    ImGui::PushStyleColor(ImGuiCol_Text, grp.isAnd ? ImVec4(0.2f, 0.8f, 1.0f, 1.0f) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                    const char* opTypes[] = { TextLiteral("▶ 组内逻辑: 满足其一 (OR)"), TextLiteral("▶ 组内逻辑: 必须全含 (AND)") };
                    int opIdx = grp.isAnd ? 1 : 0;
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::Combo("##op", &opIdx, opTypes, 2)) grp.isAnd = (opIdx == 1);
                    ImGui::PopStyleColor();

                    for (size_t kIdx = 0; kIdx < grp.keywords.size(); kIdx++) {
                        ImGui::PushID(static_cast<int>(kIdx));
                        char kwBuf[128]; strcpy_s(kwBuf, grp.keywords[kIdx].c_str());
                        ImGui::SetNextItemWidth(250.0f);
                        if (ImGui::InputText("##kwEdit", kwBuf, 128)) grp.keywords[kIdx] = kwBuf;
                        ImGui::SameLine();
                        if (ImGui::Button("X")) { grp.keywords.erase(grp.keywords.begin() + kIdx); kIdx--; }
                        ImGui::PopID();
                    }

                    static std::string s_newKwBuf = "";
                    UIConditionTreeEditor::DrawKeywordScannerBox("##scanKwBox", s_newKwBuf); ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("添加至本组"))) {
                        if (!s_newKwBuf.empty() && std::find(grp.keywords.begin(), grp.keywords.end(), s_newKwBuf) == grp.keywords.end()) {
                            grp.keywords.push_back(s_newKwBuf); s_newKwBuf.clear();
                        }
                    }
                    ImGui::Unindent();
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (ImGui::Button(TextLiteral(" + 添加新关键字分组"), { -1, 30 })) { groups.push_back({}); }
            }
            ImGui::TreePop();
        }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("模型显示与清理"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled(TextLiteral("控制模型来源、几何显示和附加节点清理方式。"));
            if (ImGui::Checkbox(TextLiteral("提取武器弹匣 (Extract Magazine)"), &a_slot.extractMagazine)) {
                if (a_slot.extractMagazine) a_slot.useProjectileForAmmo = false;
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("使用世界/基础模型 (Use World Model)"), &a_slot.useWorldModel)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("隐藏几何 / alpha 0 (Invisible)"), &a_slot.invisible)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("仅隐藏几何，保留挂载节点 (Hide Geometry)"), &a_slot.hideGeometry)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("隐藏附加灯光 (Hide Light)"), &a_slot.hideLight)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("加载第一人称武器模型 (Load 1P Weapon Model)"), &a_slot.load1pWeaponModel)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("保留火焰/喷焰 FX (Keep Torch Flame)"), &a_slot.keepTorchFlame)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (ImGui::Checkbox(TextLiteral("移除刀鞘/枪套节点 (Remove Scabbard)"), &a_slot.removeScabbard)) {
                UIEditCoordinator::RequestConfigChange();
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("模型强制替换"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            char slotModelPath[256]; strcpy_s(slotModelPath, a_slot.modelSwapPath.c_str());
            ImGui::SetNextItemWidth(400.0f);
            ImGui::InputText(TextLiteral("槽位模型路径"), slotModelPath, 256);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                a_slot.modelSwapPath = slotModelPath;
                UIEditCoordinator::RequestConfigChange();
            }
            if (UIFormEditorControls::DrawFormIDField(TextLiteral("槽位模型 FormID"), a_slot.modelSwapFormID)) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (UIModelEditorControls::DrawModelSwapVariableSource(a_slot.modelSwapVariableSource, "slot_model_var_source")) {
                UIEditCoordinator::RequestConfigChange();
            }
            ImGui::TextDisabled(TextLiteral("路径优先于 FormID；Custom 和 StateOverride 可继续覆盖槽位强制模型。"));
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("附加效果与清理"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            if (UIModelEditorControls::DrawModelCleanupSettings(a_slot.disableHavok, a_slot.removeEditorMarker, a_slot.removeProjectileTracers, "slot_cleanup")) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (UIModelEditorControls::DrawModelAnimationSettings(a_slot.animation, "slot_animation")) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (UIModelEditorControls::DrawModelEffectShaderSettings(a_slot.effectShader, "slot_effect")) {
                UIEditCoordinator::RequestConfigChange();
            }
            if (UIModelEditorControls::DrawModelLightSettings(a_slot.light, "slot_light")) {
                UIEditCoordinator::RequestConfigChange();
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("插槽工作模式"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled(TextLiteral("常规物品插槽使用候选筛选；战术弹药阵列会跟随当前武器弹药。"));
            int slotMode = a_slot.ammoRig.isDedicatedAmmoSlot ? 1 : 0;
            if (ImGui::RadioButton(TextLiteral("常规物品展示 (Weapons/Apparel/Misc)"), &slotMode, 0)) {
                a_slot.ammoRig.isDedicatedAmmoSlot = false;
                UIEditCoordinator::RequestConfigChange();
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(TextLiteral("战术弹药阵列 (Tactical Ammo Rig)"), &slotMode, 1)) {
                a_slot.ammoRig.isDedicatedAmmoSlot = true;
                UIEditCoordinator::RequestConfigChange();
            }
            ImGui::Spacing();

            if (a_slot.ammoRig.isDedicatedAmmoSlot) {
                ImGui::TextColored(ImVec4(1.0f, 0.64f, 0.0f, 1.0f), TextLiteral("🎒 战术弹药舱配置"));
                ImGui::TextWrapped(TextLiteral("此模式下，插槽会自动追踪你当前手持武器的弹药，完全无需配置下方的过滤器！"));

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
                ImGui::BeginChild("AmmoRigConfig", ImVec2(0, 160), true);

                int mode = static_cast<int>(a_slot.ammoRig.displayMode);
                if (ImGui::Combo(TextLiteral("挂载模式 (Mode)"), &mode, TextLiteral("单弹匣独立微调 (Single)\0自动克隆阵列 (Dynamic Array)\0"))) {
                    a_slot.ammoRig.displayMode = static_cast<AmmoDisplayMode>(mode);
                    UIEditCoordinator::RequestConfigChange();
                }

                if (a_slot.ammoRig.displayMode == AmmoDisplayMode::kDynamicArray) {
                    ImGui::SliderInt(TextLiteral("最大备用弹匣数"), &a_slot.ammoRig.maxMags, 1, 10);
                    if (ImGui::IsItemDeactivatedAfterEdit()) UIEditCoordinator::RequestConfigSave();

                    ImGui::SliderFloat(TextLiteral("阵列间距"), &a_slot.ammoRig.magSpacing, -20.0f, 20.0f, "%.1f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) UIEditCoordinator::RequestConfigSave();

                    int dirIdx = (a_slot.ammoRig.arrayDirection.x != 0.0f) ? 0 : (a_slot.ammoRig.arrayDirection.y != 0.0f ? 1 : 2);
                    if (ImGui::Combo(TextLiteral("阵列延伸轴向"), &dirIdx, TextLiteral("X 轴 (左右)\0Y 轴 (前后)\0Z 轴 (上下)\0"))) {
                        a_slot.ammoRig.arrayDirection = { 0, 0, 0 };
                        if (dirIdx == 0) a_slot.ammoRig.arrayDirection.x = 1.0f;
                        if (dirIdx == 1) a_slot.ammoRig.arrayDirection.y = 1.0f;
                        if (dirIdx == 2) a_slot.ammoRig.arrayDirection.z = 1.0f;
                        UIEditCoordinator::RequestConfigSave();
                    }

                    if (ImGui::Checkbox(TextLiteral("开启弹尽粮绝隐身"), &a_slot.ammoRig.dynamicAmmoLogic)) {
                        UIEditCoordinator::RequestConfigSave();
                    }
                }
                else {
                    ImGui::TextWrapped(TextLiteral("【手动布局模式】\n当前插槽只会显示 1 个弹匣。"));
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            else {
                bool disableSlotAmmo = a_slot.extractMagazine;
                if (disableSlotAmmo) ImGui::BeginDisabled();
                ImGui::Checkbox(TextLiteral("【弹药】提取射弹(单发子弹)模型，代替默认的地面弹药盒"), &a_slot.useProjectileForAmmo);
                if (disableSlotAmmo) ImGui::EndDisabled();
            }
            ImGui::TreePop();
        }

        const bool changed = beforeSignature != BuildSlotEquipmentSignature(a_slot);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static bool DrawTabDisplay(SlotDefinition& a_slot) {
        bool changed = false;
        ImGui::Spacing();

        if (ImGui::TreeNodeEx(TextLiteral("基础显隐规则"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled(TextLiteral("控制插槽是否参与装备模式、动作和姿态相关的基础显隐判断。"));
            if (ImGui::Checkbox(TextLiteral("覆盖全局装备模式限制 (Override Equipment Mode)"), &a_slot.overrideEquipmentMode)) {
                changed = true;
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (a_slot.overrideEquipmentMode) {
                ImGui::Indent();
                if (ImGui::Checkbox(TextLiteral("仅显示已装备或收藏的武器 (Favorites or Equipped Only)"), &a_slot.displayFavoritesOnly)) {
                    changed = true;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::Unindent();
            }
            ImGui::Separator();

            if (ImGui::Checkbox(TextLiteral("拔出武器时隐藏背部模型 (Always Unload)"), &a_slot.alwaysUnload)) { changed = true; UIEditCoordinator::RequestRuntimeRefresh(); }

            if (ImGui::Checkbox(TextLiteral("冲突隐藏 (Check Cannot Wear)"), &a_slot.checkCannotWear)) { changed = true; UIEditCoordinator::RequestRuntimeRefresh(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("如果该部位穿了封闭式服装或动力甲导致无法佩戴，则自动隐藏背上的模型！"));
            if (ImGui::Checkbox(TextLiteral("使用家具时隐藏 (Hide If Using Furniture)"), &a_slot.hideIfUsingFurniture)) { changed = true; UIEditCoordinator::RequestRuntimeRefresh(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("坐下、睡觉、躺卧或使用制作台时隐藏该插槽。"));
            if (ImGui::Checkbox(TextLiteral("躺卧/睡眠时隐藏 (Hide Laying Down)"), &a_slot.hideLayingDown)) { changed = true; UIEditCoordinator::RequestRuntimeRefresh(); }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx(TextLiteral("条件显隐"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled(TextLiteral("条件不满足时隐藏插槽上的模型，但底层物理 CME 节点仍会保留在身上。"));
            if (ImGui::Button(TextLiteral("💥 一键清空所有条件 (Reset to Empty)##clearSlot"), { 220.0f, 0.0f })) {
                a_slot.displayConditionTree = IAD::ConditionNode(true, true);
                changed = true;
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::Spacing();
            changed |= DrawConditionTreeEditor(a_slot.displayConditionTree);
            ImGui::TreePop();
        }
        return changed;
    }

    static bool DrawConfigBaseTransform(ConfigBase& a_config, ImGuiManager::WindowState& a_state, const char* idSuffix, bool isNode) {
        const auto beforeSignature = BuildConfigBaseTransformSignature(a_config);
        const int genderEdit = a_state.genderEdit;
        const bool syncGender = a_state.syncGender;
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        if (!isNode) {
            ImGui::TextDisabled(TextLiteral("此处设置该物品相对于其挂载节点的偏移量："));
            ImGui::Spacing();
            if (ImGui::Checkbox(TextLiteral("启用姿态覆写 (Override Transform)"), &a_config.overrideTransform)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (!a_config.overrideTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 💡 当前未开启覆写，姿态将回退继承自其目标挂载节点 (Node)。"));
                ImGui::BeginDisabled();
            }
            ImGui::Separator();
        }

        auto& currentData = (genderEdit == 1) ? a_config.transforms.f : a_config.transforms.m;
        const bool syncJustToggled = syncGender && !a_state.lastSyncGender;
        a_state.lastSyncGender = syncGender;

        if (DrawTransformProfileControls("baseTransformProfile", currentData) && syncGender) {
            if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
            else a_config.transforms.m = a_config.transforms.f;
        }

        if (ImGui::TreeNodeEx(TextLiteral("姿态微调 (Transform Adjustment)"), ImGuiTreeNodeFlags_DefaultOpen)) {

            if (ImGui::BeginPopupContextItem("TransformBlockContextMenu")) {
                if (ImGui::MenuItem(TextLiteral("📋 复制全部坐标 (Copy Transform)"))) {
                    s_clipboardTransform = currentData;
                    s_hasClipboardTransform = true;
                }
                if (ImGui::MenuItem(TextLiteral("📋 粘贴全部坐标 (Paste Transform)"), nullptr, false, s_hasClipboardTransform)) {
                    currentData = s_clipboardTransform;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("💡 右键点击此处可进行复制/粘贴坐标！"));

            bool changed = false;
            if (syncJustToggled) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("位置偏移 (Position)"), currentData.pos, { 0,0,0 }, 0.5f, false);
            changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("旋转偏移 (Rotation)"), currentData.rot, { 0,0,0 }, 1.0f, true);

            float dragSpeedScale = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat(TextLiteral("缩放倍率 (Scale)"), &currentData.scale, dragSpeedScale, 0.01f, 10.0f, "%.3f")) changed = true;

            if (ImGui::Button(TextLiteral("重置当前坐标"))) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                changed = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("强行同步到另一性别"))) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (changed && syncGender) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
            }

            if (changed) {
                a_state.wasDraggingTransform = true;
                if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
            }
            if (a_state.wasDraggingTransform && ImGui::IsMouseReleased(0)) {
                UIEditCoordinator::RequestRuntimeRefresh();
                a_state.wasDraggingTransform = false;
            }
            ImGui::TreePop();
        }

        if (!isNode && !a_config.overrideTransform) {
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildConfigBaseTransformSignature(a_config);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static bool DrawGeometryTransformBlock(
        ConfigBase& a_config,
        int genderEdit,
        bool syncGender,
        const char* idSuffix,
        const char* title)
    {
        bool changed = false;
        ImGui::PushID(idSuffix);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored({ 0.6f, 1.0f, 0.8f, 1.0f }, "%s", title);
        ImGui::TextDisabled(TextLiteral("几何层变换作用在模型内容包装节点上；不会改变挂载 MOV 或主模型根偏移。"));

        if (ImGui::Checkbox(TextLiteral("启用几何层变换 (IAD Geometry Transform)"), &a_config.overrideGeometryTransform)) {
            changed = true;
        }
        if (!a_config.overrideGeometryTransform) {
            ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 当前未开启，几何包装节点保持 identity。"));
            ImGui::BeginDisabled();
        }

        auto& currentData = (genderEdit == 1) ? a_config.geometryTransforms.f : a_config.geometryTransforms.m;
        if (DrawTransformProfileControls("geometryTransformProfile", currentData)) {
            changed = true;
        }
        changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何位置偏移"), currentData.pos, { 0,0,0 }, 0.5f, false);
        changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何旋转"), currentData.rot, { 0,0,0 }, 1.0f, true);
        changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("几何旋转轴心"), currentData.pivot, { 0,0,0 }, 0.5f, false);

        float scaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
        if (ImGui::DragFloat(TextLiteral("几何缩放倍率"), &currentData.scale, scaleSpeed, 0.01f, 10.0f, "%.3f")) changed = true;

        if (ImGui::Button(TextLiteral("重置几何坐标"))) {
            currentData.pos = { 0,0,0 };
            currentData.rot = { 0,0,0 };
            currentData.pivot = { 0,0,0 };
            currentData.scale = 1.0f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(TextLiteral("同步几何到另一性别"))) {
            if (genderEdit == 0) a_config.geometryTransforms.f = a_config.geometryTransforms.m;
            else a_config.geometryTransforms.m = a_config.geometryTransforms.f;
            changed = true;
        }

        if (changed && syncGender) {
            if (genderEdit == 0) a_config.geometryTransforms.f = a_config.geometryTransforms.m;
            else a_config.geometryTransforms.m = a_config.geometryTransforms.f;
        }

        if (!a_config.overrideGeometryTransform) {
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawSlotMeshTransform(SlotDefinition& a_slot, ImGuiManager::WindowState& state, const char* idSuffix) {
        const auto beforeSignature = BuildSlotMeshSignature(a_slot);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, TextLiteral("📦 模型组装车间 (Model Assembly)"));
        ImGui::TextDisabled(TextLiteral("在此选择要微调的网格层。旋转这些网格绝对不会影响 CME 物理摆锤的真实受力轴向！"));
        ImGui::Spacing();

        int currentMode = static_cast<int>(state.meshMode);
        ImGui::RadioButton(TextLiteral("🗡️ 武器本体 (Weapon)"), &currentMode, 0); ImGui::SameLine();
        ImGui::RadioButton(TextLiteral("🎒 枪套/刀鞘 (Holster)"), &currentMode, 1); ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::RadioButton(TextLiteral("🔋 独立弹匣 (Mag - WIP)"), &currentMode, 2);
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::RadioButton(TextLiteral("🧩 模型组 (Groups)"), &currentMode, 3);
        state.meshMode = static_cast<MeshEditMode>(currentMode);

        ImGui::Separator();
        ImGui::Spacing();

        bool meshChanged = false;

        if (state.meshMode == MeshEditMode::kWeapon) {
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, TextLiteral("【当前选中：🗡️ 武器本体】"));
            ImGui::Spacing();

            if (ImGui::Checkbox(TextLiteral("启用武器独立网格校准 (Override Weapon Mesh)"), &a_slot.overrideMeshTransform)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (!a_slot.overrideMeshTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 💡 当前未开启独立校准，武器模型将默认对齐至插槽中心。"));
                ImGui::BeginDisabled();
            }

            auto& currentData = (state.genderEdit == 1) ? a_slot.meshTransforms.f : a_slot.meshTransforms.m;

            if (DrawTransformProfileControls("slotWeaponMeshTransformProfile", currentData)) {
                meshChanged = true;
            }

            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("网格位置偏移"), currentData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("网格旋转 (掰弯模型)"), currentData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("网格旋转轴心 (Pivot)"), currentData.pivot, { 0,0,0 }, 0.5f, false);

            float mScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat(TextLiteral("网格缩放倍率"), &currentData.scale, mScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            if (ImGui::Button(TextLiteral("重置武器坐标"))) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("同步到另一性别##weap"))) {
                if (state.genderEdit == 0) a_slot.meshTransforms.f = a_slot.meshTransforms.m;
                else a_slot.meshTransforms.m = a_slot.meshTransforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_slot.meshTransforms.f = a_slot.meshTransforms.m;
                else a_slot.meshTransforms.m = a_slot.meshTransforms.f;
            }

            if (!a_slot.overrideMeshTransform) ImGui::EndDisabled();
                meshChanged |= DrawGeometryTransformBlock(a_slot, state.genderEdit, state.syncGender, "slotWeaponGeometryTransform", TextLiteral("【IAD 几何层变换】"));
        }
        else if (state.meshMode == MeshEditMode::kHolster) {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, TextLiteral("【当前选中：🎒 枪套/刀鞘】"));
            ImGui::Spacing();

            char hpBuf[256]; strcpy_s(hpBuf, a_slot.holsterModelPath.c_str());
            ImGui::SetNextItemWidth(450.0f);
            ImGui::InputText(TextLiteral("枪套模型路径 (NIF)"), hpBuf, 256);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                a_slot.holsterModelPath = hpBuf;
                // 👇 直接全局强制刷新，确保旧模型被彻底回收！
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("留空则不生成枪套。\n例: Weapons/Holsters/MyHolster.nif"));

            if (ImGui::Checkbox(TextLiteral("拔出武器时，将此枪套保留在身上 (Keep when drawn)"), &a_slot.keepHolsterWhenDrawn)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (a_slot.holsterModelPath.empty()) {
                ImGui::TextDisabled(TextLiteral("👆 请先输入模型路径，才可进行坐标微调。"));
                ImGui::BeginDisabled();
            }

            auto& currentHData = (state.genderEdit == 1) ? a_slot.holsterTransforms.f : a_slot.holsterTransforms.m;

            if (DrawTransformProfileControls("slotHolsterTransformProfile", currentHData)) {
                meshChanged = true;
            }

            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("枪套位置偏移"), currentHData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("枪套旋转倾斜"), currentHData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("枪套旋转轴心"), currentHData.pivot, { 0,0,0 }, 0.5f, false);

            float hScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat(TextLiteral("枪套缩放倍率 (Scale)"), &currentHData.scale, hScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            ImGui::Spacing();

            if (ImGui::Button(TextLiteral("📍 一键吸附原点 (归零位置与轴心)"))) {
                currentHData.pos = { 0,0,0 };
                currentHData.pivot = { 0,0,0 };
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("重置全部坐标"))) {
                currentHData.pos = { 0,0,0 }; currentHData.rot = { 0,0,0 }; currentHData.pivot = { 0,0,0 }; currentHData.scale = 1.0f;
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("同步到另一性别##holster"))) {
                if (state.genderEdit == 0) a_slot.holsterTransforms.f = a_slot.holsterTransforms.m;
                else a_slot.holsterTransforms.m = a_slot.holsterTransforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_slot.holsterTransforms.f = a_slot.holsterTransforms.m;
                else a_slot.holsterTransforms.m = a_slot.holsterTransforms.f;
            }

            if (a_slot.holsterModelPath.empty()) ImGui::EndDisabled();
        }

        if (meshChanged) {
            state.wasDraggingMesh = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (state.wasDraggingMesh && ImGui::IsMouseReleased(0)) {
            UIEditCoordinator::RequestRuntimeRefresh();
            state.wasDraggingMesh = false;
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildSlotMeshSignature(a_slot);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static bool DrawCustomMeshTransform(CustomDefinition& a_custom, ImGuiManager::WindowState& state, const std::vector<NodeDefinition>& localNodes, const char* idSuffix) {
        const auto beforeSignature = BuildCustomMeshSignature(a_custom);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, TextLiteral("📦 专属模型组装车间 (Custom Model Assembly)"));
        ImGui::TextDisabled(TextLiteral("在此微调专属武器的网格偏移，以及给它选配一个独一无二的枪套！"));
        ImGui::Spacing();

        int currentMode = static_cast<int>(state.meshMode);
        ImGui::RadioButton(TextLiteral("🗡️ 武器本体 (Weapon)"), &currentMode, 0); ImGui::SameLine();
        ImGui::RadioButton(TextLiteral("🎒 枪套/刀鞘 (Holster)"), &currentMode, 1); ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::RadioButton(TextLiteral("🔋 独立弹匣 (Mag - WIP)"), &currentMode, 2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::RadioButton(TextLiteral("🧩 模型组 (Groups)"), &currentMode, 3);
        state.meshMode = static_cast<MeshEditMode>(currentMode);

        ImGui::Separator();
        ImGui::Spacing();

        bool meshChanged = false;

        if (state.meshMode == MeshEditMode::kWeapon) {
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, TextLiteral("【当前选中：🗡️ 武器本体】"));
            ImGui::Spacing();

            if (ImGui::Checkbox(TextLiteral("启用专属武器网格覆写 (Override Custom Mesh)"), &a_custom.overrideMeshTransform)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (!a_custom.overrideMeshTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 💡 当前未开启专属覆写，将继承底层插槽 (Slot) 的网格坐标。"));
                ImGui::BeginDisabled();
            }

            auto& currentData = (state.genderEdit == 1) ? a_custom.meshTransforms.f : a_custom.meshTransforms.m;

            if (DrawTransformProfileControls("customWeaponMeshTransformProfile", currentData)) {
                meshChanged = true;
            }

            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属网格位置偏移"), currentData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属网格旋转 (掰弯)"), currentData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属网格旋转轴心"), currentData.pivot, { 0,0,0 }, 0.5f, false);

            float mScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat(TextLiteral("专属网格缩放倍率"), &currentData.scale, mScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            if (ImGui::Button(TextLiteral("重置武器坐标"))) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("同步到另一性别##weap"))) {
                if (state.genderEdit == 0) a_custom.meshTransforms.f = a_custom.meshTransforms.m;
                else a_custom.meshTransforms.m = a_custom.meshTransforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_custom.meshTransforms.f = a_custom.meshTransforms.m;
                else a_custom.meshTransforms.m = a_custom.meshTransforms.f;
            }

            if (!a_custom.overrideMeshTransform) ImGui::EndDisabled();
                meshChanged |= DrawGeometryTransformBlock(a_custom, state.genderEdit, state.syncGender, "customWeaponGeometryTransform", TextLiteral("【IAD 专属几何层变换】"));
        }
        else if (state.meshMode == MeshEditMode::kHolster) {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, TextLiteral("【当前选中：🎒 专属枪套/刀鞘】"));
            ImGui::Spacing();

            char hpBuf[256]; strcpy_s(hpBuf, a_custom.holsterModelPath.c_str());
            ImGui::SetNextItemWidth(450.0f);
            ImGui::InputText(TextLiteral("专属枪套模型路径 (NIF)"), hpBuf, 256);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                a_custom.holsterModelPath = hpBuf;
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("为这把武器单独指定一个枪套！\n留空则尝试继承插槽配置。"));

            if (ImGui::Checkbox(TextLiteral("拔出此武器时，将枪套保留在身上 (Keep when drawn)"), &a_custom.keepHolsterWhenDrawn)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (a_custom.holsterModelPath.empty()) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 💡 当前没有配置专属枪套模型，坐标将无效或继承底层插槽。"));
                ImGui::BeginDisabled();
            }

            auto& currentHData = (state.genderEdit == 1) ? a_custom.holsterTransforms.f : a_custom.holsterTransforms.m;

            if (DrawTransformProfileControls("customHolsterTransformProfile", currentHData)) {
                meshChanged = true;
            }

            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属枪套位置偏移"), currentHData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属枪套旋转倾斜"), currentHData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("专属枪套旋转轴心"), currentHData.pivot, { 0,0,0 }, 0.5f, false);

            float hScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat(TextLiteral("专属枪套缩放倍率 (Scale)"), &currentHData.scale, hScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            ImGui::Spacing();

            if (ImGui::Button(TextLiteral("📍 一键吸附原点 (归零位置与轴心)"))) {
                currentHData.pos = { 0,0,0 };
                currentHData.pivot = { 0,0,0 };
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("重置枪套坐标"))) {
                currentHData.pos = { 0,0,0 }; currentHData.rot = { 0,0,0 }; currentHData.pivot = { 0,0,0 }; currentHData.scale = 1.0f;
                meshChanged = true; UIEditCoordinator::RequestRuntimeRefresh();
            }
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("同步到另一性别##holster"))) {
                if (state.genderEdit == 0) a_custom.holsterTransforms.f = a_custom.holsterTransforms.m;
                else a_custom.holsterTransforms.m = a_custom.holsterTransforms.f;
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_custom.holsterTransforms.f = a_custom.holsterTransforms.m;
                else a_custom.holsterTransforms.m = a_custom.holsterTransforms.f;
            }

            if (a_custom.holsterModelPath.empty()) ImGui::EndDisabled();
        }
        else if (state.meshMode == MeshEditMode::kModelGroup) {
            ImGui::TextColored({ 0.6f, 1.0f, 0.8f, 1.0f }, TextLiteral("【当前选中：🧩 附加模型组】"));
            ImGui::TextDisabled(TextLiteral("给这条专属规则附加多个 NIF 模型；它们会跟随同一个 MOV 节点，但拥有独立偏移。"));
            ImGui::Spacing();

            if (UIManagedProfileControls::DrawSelector(
                "CustomModelGroupProfileSelector",
                TextLiteral("模型组预设 (Model Group Profile)"),
                IAD::Profile::GlobalProfileManager::GetSingleton().ModelGroups(),
                a_custom.modelGroups,
                "MyModelGroupSet",
                [](std::vector<ModelGroupEntry>& target, const std::vector<ModelGroupEntry>& source) {
                    target = source;
                },
                [](std::vector<ModelGroupEntry>& target, const std::vector<ModelGroupEntry>& source) {
                    MergeConfigListByKey(target, source, [](const ModelGroupEntry& group) { return group.name; }, true);
                },
                true)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (ImGui::Button(TextLiteral("添加模型组条目"), { 140.0f, 0.0f })) {
                ModelGroupEntry group;
                group.name = "Group_" + std::to_string(a_custom.modelGroups.size() + 1);
                a_custom.modelGroups.push_back(group);
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            int removeIndex = -1;
            int moveIndex = -1;
            int moveDelta = 0;
            int duplicateIndex = -1;
            for (int i = 0; i < static_cast<int>(a_custom.modelGroups.size()); ++i) {
                auto& group = a_custom.modelGroups[i];
                ImGui::PushID(i);
                std::string header = group.name.empty() ? TextLiteral("未命名模型组") : group.name;
                const auto groupFlags = ImGuiTreeNodeFlags_SpanAvailWidth | (i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (ImGui::TreeNodeEx("##modelGroup", groupFlags, "%s", header.c_str())) {
                    char nameBuf[64]; strncpy_s(nameBuf, group.name.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(220.0f);
                    if (ImGui::InputText(TextLiteral("名称"), nameBuf, 64)) group.name = nameBuf;

                    if (ImGui::Checkbox(TextLiteral("启用"), &group.isEnabled)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox(TextLiteral("跟随武器显隐"), &group.hideWithWeapon)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("勾选后，拔枪隐藏武器本体时也隐藏这个附加模型；取消后只受插槽整体显隐影响。"));
                    if (ImGui::Checkbox(TextLiteral("可见命中后继续评估后续模型组"), &group.continueAfterMatch)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("关闭后，此模型组条件通过且实际可见时，后面的模型组不会进入本次显示列表。"));
                    if (ImGui::Checkbox(TextLiteral("仅可见时加载模型"), &group.loadOnlyWhenVisible)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("开启后，条件不满足或目标节点隐藏时不会预加载此模型组；关闭时保持旧行为，预加载后按条件隐藏。"));

                    if (group.sourceMode == 0) {
                        char pathBuf[256]; strncpy_s(pathBuf, group.modelPath.c_str(), _TRUNCATE);
                        ImGui::SetNextItemWidth(450.0f);
                        ImGui::InputText(TextLiteral("模型路径 (NIF)"), pathBuf, 256);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            group.modelPath = pathBuf;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    else {
                        if (UIFormEditorControls::DrawFormIDField(TextLiteral("模型来源 FormID"), group.sourceFormID)) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }

                    if (DrawModelGroupAdvancedConfig(group)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }

                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("模型组目标节点:")); ImGui::SameLine();
                    DrawCMENodeSelector("##modelGroupTargetNode", group.targetNode, localNodes, state.scope, TextLiteral("[不覆盖] 跟随 Custom/Slot 默认节点"));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("留空时挂到当前 Custom/Slot 的默认 MOV；选择节点后会为此模型组创建独立 MOV。"));

                    bool groupChanged = false;
                    auto& currentGData = (state.genderEdit == 1) ? group.transforms.f : group.transforms.m;
                    if (DrawTransformProfileControls("modelGroupTransformProfile", currentGData)) {
                        groupChanged = true;
                    }
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组位置偏移"), currentGData.pos, { 0,0,0 }, 0.5f, false);
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组旋转"), currentGData.rot, { 0,0,0 }, 1.0f, true);
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组旋转轴心"), currentGData.pivot, { 0,0,0 }, 0.5f, false);

                    float gScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
                    if (ImGui::DragFloat(TextLiteral("模型组缩放倍率"), &currentGData.scale, gScaleSpeed, 0.01f, 10.0f, "%.3f")) groupChanged = true;

                    ImGui::Separator();
                    if (ImGui::Checkbox(TextLiteral("启用模型组几何层变换 (IAD Geometry Transform)"), &group.overrideGeometryTransform)) {
                        groupChanged = true;
                    }
                    if (!group.overrideGeometryTransform) {
                        ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 当前未开启，模型组几何包装节点保持 identity。"));
                        ImGui::BeginDisabled();
                    }

                    auto& currentGGeometry = (state.genderEdit == 1) ? group.geometryTransforms.f : group.geometryTransforms.m;
                    if (DrawTransformProfileControls("modelGroupGeometryTransformProfile", currentGGeometry)) {
                        groupChanged = true;
                    }
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组几何位置偏移"), currentGGeometry.pos, { 0,0,0 }, 0.5f, false);
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组几何旋转"), currentGGeometry.rot, { 0,0,0 }, 1.0f, true);
                    groupChanged |= UITransformEditorControls::DrawTransformWidget(TextLiteral("模型组几何旋转轴心"), currentGGeometry.pivot, { 0,0,0 }, 0.5f, false);

                    if (ImGui::DragFloat(TextLiteral("模型组几何缩放倍率"), &currentGGeometry.scale, gScaleSpeed, 0.01f, 10.0f, "%.3f")) groupChanged = true;

                    if (ImGui::Button(TextLiteral("重置模型组几何坐标"))) {
                        currentGGeometry.pos = { 0,0,0 };
                        currentGGeometry.rot = { 0,0,0 };
                        currentGGeometry.pivot = { 0,0,0 };
                        currentGGeometry.scale = 1.0f;
                        groupChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("同步几何到另一性别"))) {
                        if (state.genderEdit == 0) group.geometryTransforms.f = group.geometryTransforms.m;
                        else group.geometryTransforms.m = group.geometryTransforms.f;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }

                    if (!group.overrideGeometryTransform) {
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader(TextLiteral("模型组显隐条件"), ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Button(TextLiteral("清空此模型组条件"), { 160.0f, 0.0f })) {
                            group.displayConditionTree = IAD::ConditionNode(true, true);
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(TextLiteral("应用条件更改"), { 140.0f, 0.0f })) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        groupChanged |= DrawConditionTreeEditor(group.displayConditionTree);
                    }

                    if (ImGui::Button(TextLiteral("重置模型组坐标"))) {
                        currentGData.pos = { 0,0,0 };
                        currentGData.rot = { 0,0,0 };
                        currentGData.pivot = { 0,0,0 };
                        currentGData.scale = 1.0f;
                        groupChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("同步到另一性别"))) {
                        if (state.genderEdit == 0) group.transforms.f = group.transforms.m;
                        else group.transforms.m = group.transforms.f;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::SameLine();
                    if (i <= 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("上移##moveModelGroupUp"))) {
                        moveIndex = i;
                        moveDelta = -1;
                    }
                    if (i <= 0) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (i >= static_cast<int>(a_custom.modelGroups.size()) - 1) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("下移##moveModelGroupDown"))) {
                        moveIndex = i;
                        moveDelta = 1;
                    }
                    if (i >= static_cast<int>(a_custom.modelGroups.size()) - 1) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("复制条目##duplicateModelGroup"))) {
                        duplicateIndex = i;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("删除条目"))) {
                        removeIndex = i;
                    }

                    if (groupChanged && state.syncGender) {
                        if (state.genderEdit == 0) group.transforms.f = group.transforms.m;
                        else group.transforms.m = group.transforms.f;
                        if (state.genderEdit == 0) group.geometryTransforms.f = group.geometryTransforms.m;
                        else group.geometryTransforms.m = group.geometryTransforms.f;
                    }
                    meshChanged |= groupChanged;
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (moveIndex >= 0 && moveDelta != 0) {
                const int targetIndex = moveIndex + moveDelta;
                if (targetIndex >= 0 && targetIndex < static_cast<int>(a_custom.modelGroups.size())) {
                    std::swap(a_custom.modelGroups[moveIndex], a_custom.modelGroups[targetIndex]);
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
            }

            if (duplicateIndex >= 0 && duplicateIndex < static_cast<int>(a_custom.modelGroups.size())) {
                auto copy = a_custom.modelGroups[duplicateIndex];
                copy.name = MakeUniqueModelGroupName(a_custom.modelGroups, copy.name);
                a_custom.modelGroups.insert(a_custom.modelGroups.begin() + duplicateIndex + 1, std::move(copy));
                UIEditCoordinator::RequestRuntimeRefresh();
            }

            if (removeIndex >= 0 && removeIndex < static_cast<int>(a_custom.modelGroups.size())) {
                a_custom.modelGroups.erase(a_custom.modelGroups.begin() + removeIndex);
                UIEditCoordinator::RequestRuntimeRefresh();
            }
        }

        if (meshChanged) {
            state.wasDraggingMesh = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (state.wasDraggingMesh && ImGui::IsMouseReleased(0)) {
            UIEditCoordinator::RequestRuntimeRefresh();
            state.wasDraggingMesh = false;
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildCustomMeshSignature(a_custom);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

    static bool DrawPhysicsConstraintParams(PhysicsConstraintParams& a_params) {
        auto dragSpeed = ImGui::GetIO().KeyShift ? 0.0001f : 0.04f;
        bool changed = false;

        ImGui::PushID("opar");
        if (ImGui::DragFloat(TextLiteral("速度响应比例 (Velocity Response Scale)"), &a_params.velocityResponseScale, dragSpeed, 0.0f, 1.0f)) changed = true;
        if (ImGui::DragFloat(TextLiteral("穿模偏移因素 (Pen Bias Factor)"), &a_params.penBiasFactor, dragSpeed, 0.0f, 20.0f)) changed = true;
        if (ImGui::DragFloat(TextLiteral("穿模偏移极限 (Pen Bias Depth Limit)"), &a_params.penBiasDepthLimit, dragSpeed, 0.5f, 50000.0f)) changed = true;
        if (ImGui::DragFloat(TextLiteral("恢复系数/弹性 (Restitution Coefficient)"), &a_params.restitutionCoefficient, dragSpeed, 0.0f, 1.0f)) changed = true;
        ImGui::PopID();
        return changed;
    }

    static bool DrawPhysicsPanel(PhysicsValues& a_phys, ImGuiManager::WindowState* a_state) {
        ImGui::Spacing();
        bool changed = false;

        ImGui::PushID("pv");

        if (ImGui::Checkbox(TextLiteral("禁用物理效果 (Disable)"), &a_phys.disabled)) changed = true;

        ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
        if (ImGui::Checkbox(TextLiteral("显示约束"), &a_phys.drawConstraints)) changed = true; ImGui::SameLine();
        if (ImGui::Checkbox(TextLiteral("显示摆锤"), &a_phys.drawPendulum)) changed = true;

        ImGui::Spacing();
        const bool disabled = a_phys.disabled;
        if (disabled) ImGui::BeginDisabled();

        if (ImGui::TreeNodeEx(TextLiteral("一般参数 (General)"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::Spacing();

            auto dragSpeed = ImGui::GetIO().KeyShift ? 0.0001f : 0.04f;

            if (ImGui::DragFloat(TextLiteral("刚度 (Stiffness)"), &a_phys.stiffness, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat(TextLiteral("二次刚度 (Stiffness 2)"), &a_phys.stiffness2, dragSpeed, 0.0f, 500.0f)) changed = true;

            if (ImGui::DragFloat(TextLiteral("弹簧松弛偏移 (Spring Slack Offset)"), &a_phys.springSlackOffset, dragSpeed, 0.0f, 5000.0f)) changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("极其重要：允许武器在此距离内自由滑动而不受弹簧拉力，完美解决下蹲时的微小错位！"));

            if (ImGui::DragFloat(TextLiteral("弹簧松弛幅度 (Spring Slack Mag)"), &a_phys.springSlackMag, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat(TextLiteral("阻尼 (Damping)"), &a_phys.damping, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat(TextLiteral("动态阻力 (Resistance)"), &a_phys.resistance, dragSpeed, 0.0f, 20.0f)) changed = true;
            if (ImGui::DragFloat(TextLiteral("最大速度 (Max Velocity)"), &a_phys.maxVelocity, dragSpeed, 10.0f, 50000.0f)) changed = true;

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), TextLiteral("物理力响应参数 (Physical Feedback)"));

            changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("线性位移比例 (Linear Scale)"), a_phys.linear, { 0,0,0 }, dragSpeed, false);
            ImGui::TextDisabled(TextLiteral("设定阻力产生的位移范围 (X=左右, Y=前后, Z=上下)"));

            changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("旋转摇晃比例 (Rotation Scale)"), a_phys.rotational, { 0,0,0 }, dragSpeed, false);
            ImGui::TextDisabled(TextLiteral("设定随阻力摇摆的角度系数 (X=前后点头Pitch, Y=自身扭转Roll, Z=左右摇摆Yaw)"));

            changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("重心偏移 (Cog Offset)"), a_phys.cogOffset, { 0,0,0 }, dragSpeed, false);

            ImGui::Separator();

            if (ImGui::DragFloat(TextLiteral("质量 (Mass)"), &a_phys.mass, dragSpeed, 0.001f, 1000.0f)) changed = true;
            if (ImGui::DragFloat(TextLiteral("重力拉扯 (Gravity Bias)"), &a_phys.gravityBias, dragSpeed, 0.0f, 8000.0f)) changed = true;

            ImGui::Spacing();
            ImGui::TreePop();
        }

        ImGui::Spacing();

        if (ImGui::Checkbox(TextLiteral("球面约束 (Sphere Constraint)"), &a_phys.enableSphereConstraint)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox(TextLiteral("盒形约束 (Box Constraint)"), &a_phys.enableBoxConstraint)) changed = true;

        if (a_phys.enableSphereConstraint || a_phys.enableBoxConstraint) {
            ImGui::Spacing();

            if (a_phys.enableSphereConstraint && ImGui::TreeNodeEx(TextLiteral("球面约束参数 (Sphere Constraint)"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("球面偏移 (Sphere Offset)"), a_phys.maxOffsetSphereOffset, { 0,0,0 }, 0.04f, false);
                ImGui::TextDisabled(TextLiteral("球面中心坐标系: X=右, Y=前, Z=上"));
                if (ImGui::DragFloat(TextLiteral("球面半径 (Sphere Radius)"), &a_phys.maxOffsetSphereRadius, 0.04f, 0.0f, 500.0f)) changed = true;
                if (ImGui::DragFloat(TextLiteral("摩擦力 (Friction)##sphere"), &a_phys.maxOffsetSphereFriction, 0.04f, 0.0f, 1.0f)) changed = true;

                ImGui::Spacing();
                changed |= DrawPhysicsConstraintParams(a_phys.sphereParams);
                ImGui::Spacing();
                ImGui::TreePop();
            }

            if (a_phys.enableBoxConstraint && ImGui::TreeNodeEx(TextLiteral("盒形约束参数 (Box Constraint)"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("物理盒最小边界 (Box Min)"), a_phys.maxOffsetN, { 0,0,0 }, 0.04f, false);
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("物理盒最大边界 (Box Max)"), a_phys.maxOffsetP, { 0,0,0 }, 0.04f, false);
                ImGui::TextDisabled(TextLiteral("约束盒坐标系: X=左右边界(右正), Y=前后边界(前正), Z=上下边界(上正)"));
                if (ImGui::DragFloat(TextLiteral("摩擦力 (Friction)##box"), &a_phys.maxOffsetBoxFriction, 0.04f, 0.0f, 1.0f)) changed = true;

                ImGui::Spacing();
                changed |= DrawPhysicsConstraintParams(a_phys.boxParams);
                ImGui::Spacing();
                ImGui::TreePop();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox(TextLiteral("非对称角度极限 (Asymmetric Angular Limits)"), &a_phys.enableAngularConstraint)) changed = true;
        if (a_phys.enableAngularConstraint) {
            ImGui::Spacing();
            if (ImGui::TreeNodeEx(TextLiteral("角度极限区间 (Angle Ranges)"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                ImGui::TextDisabled(TextLiteral("通过滑块设定武器摇摆的安全死区，避免模型插进身体"));

                if (ImGui::DragFloatRange2(TextLiteral("前后点头 (Pitch Range)"), &a_phys.minPitch, &a_phys.maxPitch, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;
                if (ImGui::DragFloatRange2(TextLiteral("左右摇摆 (Yaw Range)"), &a_phys.minYaw, &a_phys.maxYaw, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;
                if (ImGui::DragFloatRange2(TextLiteral("自身扭转 (Roll Range)"), &a_phys.minRoll, &a_phys.maxRoll, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextDisabled(TextLiteral("辅助可视化设置"));
                if (ImGui::DragFloat(TextLiteral("可视化探针/圆锥长度"), &a_phys.visualProbeLength, 0.5f, 1.0f, 150.0f, TextLiteral("%.1f 单位"))) changed = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("根据当前武器的长短调节此滑块，借此刻度线精准观察武器末端是否会穿模。"));

                ImGui::Spacing();
                ImGui::TreePop();
            }
        }

        if (disabled) ImGui::EndDisabled();

        if (changed) {
            if (a_state) a_state->wasDraggingPhysics = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (a_state && a_state->wasDraggingPhysics && ImGui::IsMouseReleased(0)) {
            UIEditCoordinator::RequestRuntimeRefresh();
            a_state->wasDraggingPhysics = false;
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawConfigBasePhysics(ConfigBase& a_config, ImGuiManager::WindowState& a_state, const char* idSuffix, bool isNode) {
        const auto beforeSignature = BuildConfigBasePhysicsSignature(a_config);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        if (!isNode) {
            if (ImGui::Checkbox(TextLiteral("启用物理覆写 (Override Physics)"), &a_config.overridePhysics)) {
                UIEditCoordinator::RequestRuntimeRefresh();
            }
            if (!a_config.overridePhysics) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral(" 💡 当前未开启覆写，物理效果将回退继承下级配置。"));
                ImGui::BeginDisabled();
            }
            ImGui::Separator();
        }

        if (ImGui::Button(TextLiteral("📋 复制整套物理配置"))) {
            s_clipboardPhysics = a_config.physics;
            s_hasClipboardPhysics = true;
        }
        ImGui::SameLine();
        if (!s_hasClipboardPhysics) ImGui::BeginDisabled();
        if (ImGui::Button(TextLiteral("📋 粘贴物理配置"))) {
            a_config.physics = s_clipboardPhysics;
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        if (!s_hasClipboardPhysics) ImGui::EndDisabled();

        DrawPhysicsProfileControls("basePhysicsProfile", a_config.physics);
        DrawPhysicsPanel(a_config.physics, &a_state);

        if (!isNode && !a_config.overridePhysics) {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
        const bool changed = beforeSignature != BuildConfigBasePhysicsSignature(a_config);
        if (changed) {
            UIEditCoordinator::RequestRuntimeRefresh();
        }
        return changed;
    }

	// Shared by the live editor and the profile editor.  The caller decides
	// where the SlotDefinition comes from; this component only edits it.
	static bool DrawSlotEditorTabs(SlotDefinition& a_slot, ImGuiManager::WindowState& a_state, const char* a_idSuffix) {
		bool changed = false;
		const bool persistTab = IsLiveInspector(a_idSuffix, "liveSlotTabs");
		const bool selectPersistedTab = persistTab && UIInspectorNavigation::InitializeIndex(
			a_state.inspectorNavigation.tabIndex,
			a_state.inspectorNavigation.tabInitialized,
			ConfigManager::GetSingleton()->uiLayout.slotInspectorTab,
			5);
		ImGui::PushID(a_idSuffix);
		if (ImGui::BeginTabBar("SlotTabs", ImGuiTabBarFlags_None)) {
			const int requestedTab = UIInspectorNavigation::Pending(a_state.inspectorNavigation);
			auto beginTab = [&](const char* label, int index) {
				const auto flags = UIInspectorNavigation::ShouldSelect(requestedTab, a_state.inspectorNavigation.tabIndex, selectPersistedTab, index) ?
					ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const bool open = ImGui::BeginTabItem(label, nullptr, flags);
				if (open && UIInspectorNavigation::ActivateIndex(a_state.inspectorNavigation.tabIndex, index)) {
					if (persistTab) {
						ConfigManager::GetSingleton()->uiLayout.slotInspectorTab = index;
						UIEditCoordinator::RequestINISettingsSave();
					}
				}
				return open;
			};
			if (beginTab(TextLiteral("挂载与姿态"), 0)) { changed |= DrawConfigBaseTransform(a_slot, a_state, "transform", false); ImGui::EndTabItem(); }
			if (beginTab(TextLiteral("筛选与候选"), 1)) { changed |= DrawTabEquipment(a_slot); ImGui::EndTabItem(); }
			if (beginTab(TextLiteral("可见性"), 2)) { changed |= DrawTabDisplay(a_slot); ImGui::EndTabItem(); }
			if (beginTab(TextLiteral("状态覆盖"), 3)) { changed |= DrawStateMachineEditor(a_slot, "slot_state_machine"); ImGui::EndTabItem(); }
			if (beginTab(TextLiteral("模型与外观"), 4)) { changed |= DrawSlotMeshTransform(a_slot, a_state, "mesh"); ImGui::EndTabItem(); }
			if (beginTab(TextLiteral("物理"), 5)) { changed |= DrawConfigBasePhysics(a_slot, a_state, "physics", false); ImGui::EndTabItem(); }
			ImGui::EndTabBar();
			UIInspectorNavigation::Consume(a_state.inspectorNavigation);
		}
		ImGui::PopID();
		return changed;
	}

	static bool DrawNodeEditorTabs(NodeDefinition& a_node, ImGuiManager::WindowState& a_state, const char* a_idSuffix) {
		bool changed = false;
		const bool persistTab = IsLiveInspector(a_idSuffix, "liveNodeTabs");
		const bool selectPersistedTab = persistTab && UIInspectorNavigation::InitializeIndex(
			a_state.inspectorNavigation.tabIndex,
			a_state.inspectorNavigation.tabInitialized,
			ConfigManager::GetSingleton()->uiLayout.nodeInspectorTab,
			4);
		ImGui::PushID(a_idSuffix);
		if (ImGui::BeginTabBar("NodeTabs", ImGuiTabBarFlags_None)) {
			const int requestedTab = UIInspectorNavigation::Pending(a_state.inspectorNavigation);
			auto beginTab = [&](const char* label, int index) {
				const auto flags = UIInspectorNavigation::ShouldSelect(requestedTab, a_state.inspectorNavigation.tabIndex, selectPersistedTab, index) ?
					ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const bool open = ImGui::BeginTabItem(label, nullptr, flags);
				if (open && UIInspectorNavigation::ActivateIndex(a_state.inspectorNavigation.tabIndex, index)) {
					if (persistTab) {
						ConfigManager::GetSingleton()->uiLayout.nodeInspectorTab = index;
						UIEditCoordinator::RequestINISettingsSave();
					}
				}
				return open;
			};
			if (beginTab(TextLiteral("位置与姿态"), 0)) {
				changed |= DrawConfigBaseTransform(a_node, a_state, "transform", true);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("目标骨骼"), 1)) {
				changed |= DrawSkeletonMatchEditor(a_node.skeletonMatch);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("可见性"), 2)) {
				if (ImGui::Button(TextLiteral("清空显示条件"))) {
					a_node.displayConditionTree = IAD::ConditionNode(true, true);
					changed = true;
				}
				changed |= DrawConditionTreeEditor(a_node.displayConditionTree);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("状态覆盖"), 3)) {
				changed |= DrawStateMachineEditor(a_node, "stateMachine", true);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("物理"), 4)) {
				changed |= DrawConfigBasePhysics(a_node, a_state, "physics", true);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			UIInspectorNavigation::Consume(a_state.inspectorNavigation);
		}
		ImGui::PopID();
		return changed;
	}

	// The shared tab body deliberately has no runtime side effects. Live callers
	// refresh displays after a change; profile callers only mark their record dirty.
	static bool DrawCustomEditorTabs(CustomDefinition& a_custom, ImGuiManager::WindowState& a_state, const std::vector<NodeDefinition>& a_nodes, const char* a_idSuffix, bool a_compactSections) {
		bool changed = false;
		const bool persistSection = IsLiveInspector(a_idSuffix, "liveCustomTabs");
		const bool selectPersistedSection = a_compactSections && persistSection && UIInspectorNavigation::InitializeIndex(
			a_state.inspectorNavigation.sectionIndex,
			a_state.inspectorNavigation.sectionInitialized,
			ConfigManager::GetSingleton()->uiLayout.customInspectorSection,
			4);
		ImGui::PushID(a_idSuffix);
		const int requestedTab = UIInspectorNavigation::Pending(a_state.inspectorNavigation);
		if (a_compactSections) {
			auto beginSection = [&](const char* label, int index) {
				if (UIInspectorNavigation::ShouldSelect(requestedTab, a_state.inspectorNavigation.sectionIndex, selectPersistedSection, index)) {
					ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				}
				const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
				if (ImGui::IsItemToggledOpen() || requestedTab == index) {
					if (UIInspectorNavigation::ActivateIndex(a_state.inspectorNavigation.sectionIndex, index)) {
						if (persistSection) {
							ConfigManager::GetSingleton()->uiLayout.customInspectorSection = index;
							UIEditCoordinator::RequestINISettingsSave();
						}
					}
				}
				return open;
			};
			if (beginSection(TextLiteral("位置与姿态"), 0)) {
				changed |= DrawConfigBaseTransform(a_custom, a_state, "transform", false);
				ImGui::TreePop();
			}
			if (beginSection(TextLiteral("模型与外观"), 1)) {
				changed |= DrawCustomMeshTransform(a_custom, a_state, a_nodes, "mesh");
				ImGui::TreePop();
			}
			if (beginSection(TextLiteral("可见性"), 2)) {
				changed |= ImGui::Checkbox(TextLiteral("使用家具时隐藏"), &a_custom.hideIfUsingFurniture);
				changed |= ImGui::Checkbox(TextLiteral("躺卧/睡眠时隐藏"), &a_custom.hideLayingDown);
				ImGui::Separator();
				if (ImGui::Button(TextLiteral("清空显示条件"))) {
					a_custom.displayConditionTree = IAD::ConditionNode(true, true);
					changed = true;
				}
				changed |= DrawConditionTreeEditor(a_custom.displayConditionTree);
				ImGui::TreePop();
			}
			if (beginSection(TextLiteral("状态覆盖"), 3)) {
				changed |= DrawStateMachineEditor(a_custom, "stateMachine");
				ImGui::TreePop();
			}
			if (beginSection(TextLiteral("物理"), 4)) {
				changed |= DrawConfigBasePhysics(a_custom, a_state, "physics", false);
				ImGui::TreePop();
			}
			UIInspectorNavigation::Consume(a_state.inspectorNavigation);
		}
		else if (ImGui::BeginTabBar("CustomTabs", ImGuiTabBarFlags_None)) {
			auto beginTab = [&](const char* label, int index) {
				const bool open = ImGui::BeginTabItem(
					label,
					nullptr,
					UIInspectorNavigation::ShouldSelect(requestedTab, a_state.inspectorNavigation.tabIndex, false, index) ?
						ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
				if (open) UIInspectorNavigation::ActivateIndex(a_state.inspectorNavigation.tabIndex, index);
				return open;
			};
			if (beginTab(TextLiteral("位置与姿态"), 0)) {
				changed |= DrawConfigBaseTransform(a_custom, a_state, "transform", false);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("模型与外观"), 1)) {
				changed |= DrawCustomMeshTransform(a_custom, a_state, a_nodes, "mesh");
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("可见性"), 2)) {
				changed |= ImGui::Checkbox(TextLiteral("使用家具时隐藏"), &a_custom.hideIfUsingFurniture);
				changed |= ImGui::Checkbox(TextLiteral("躺卧/睡眠时隐藏"), &a_custom.hideLayingDown);
				ImGui::Separator();
				if (ImGui::Button(TextLiteral("清空显示条件"))) {
					a_custom.displayConditionTree = IAD::ConditionNode(true, true);
					changed = true;
				}
				changed |= DrawConditionTreeEditor(a_custom.displayConditionTree);
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("状态覆盖"), 3)) {
				changed |= DrawStateMachineEditor(a_custom, "stateMachine");
				ImGui::EndTabItem();
			}
			if (beginTab(TextLiteral("物理"), 4)) {
				changed |= DrawConfigBasePhysics(a_custom, a_state, "physics", false);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			UIInspectorNavigation::Consume(a_state.inspectorNavigation);
		}
		ImGui::PopID();
		return changed;
	}

    // 👇========== 🌟 核心面板类实现：装备插槽 ==========👇
    void UISlotsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowSlots) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.slots"), &config->uiShowSlots, ImGuiWindowFlags_MenuBar)) {
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            ImGuiManager::GetSingleton().NotifyWindowFocused(1, focused);
            UIWorldPreviewEditor::GetSingleton().NotifyEditorWindow(
                UIWorldPreviewEditor::EditorWindow::kSlots,
                focused);
            bool doPropagate = DrawWindowHeader("SlotHdr", GetSlotEditorContext(), true, true);
            DrawConfiguredTargetBrowser("SlotConfiguredTargets", GetSlotEditorContext(), config->GetConfiguredSlotTargetIDs(GetSlotEditorContext().scope));
            uint32_t queryID = GetQueryID(GetSlotEditorContext());
            auto& a_slots = config->GetSlots(GetSlotEditorContext().scope, queryID); auto& a_nodes = config->GetNodes(GetSlotEditorContext().scope, queryID);
            if (doPropagate) { auto& globalSlots = config->GetSlots(ConfigScope::kGlobal, 0); for (auto& s : a_slots) { auto git = std::find_if(globalSlots.begin(), globalSlots.end(), [&](const SlotDefinition& gs) { return gs.slotName == s.slotName; }); if (git != globalSlots.end()) *git = s; else globalSlots.push_back(s); } UIEditCoordinator::RequestRuntimeRefresh(); }
            bool openAddSlotPopup = false;
            bool openCloneSlotPopup = false;
            static std::string s_pendingDeleteSlot;
            static std::string s_pendingRenameSlot;
            static char s_slotRenameBuffer[64] = {};
            static std::string s_inlineRenameSlot;
            static char s_inlineSlotRenameBuffer[64] = {};
            static bool s_inlineSlotRenameFocus = false;
            static bool s_inlineSlotRenameInvalid = false;
            static std::string s_inlinePrioritySlot;
            static char s_inlineSlotPriorityBuffer[32] = {};
            static bool s_inlineSlotPriorityFocus = false;
            static bool s_inlineSlotPriorityInvalid = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(TextLiteral("文件"))) {
                    if (ImGui::MenuItem(TextLiteral("保存配置"))) UIEditCoordinator::CommitNow();
                    if (ImGui::MenuItem(TextLiteral("关闭"))) config->uiShowSlots = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TextLiteral("操作"))) {
                    if (ImGui::MenuItem(TextLiteral("新建插槽"))) openAddSlotPopup = true;

                    const bool hasSelectedSlot = !GetSlotEditorContext().selection.Empty();
                    if (!hasSelectedSlot) ImGui::BeginDisabled();
                    if (ImGui::MenuItem(TextLiteral("复制选中插槽"))) openCloneSlotPopup = true;
                    if (!hasSelectedSlot) ImGui::EndDisabled();

                    if (ImGui::MenuItem(TextLiteral("强制刷新显示"))) UIEditCoordinator::RequestRuntimeRefresh();
					ImGui::Separator();
					if (ImGui::MenuItem(TextLiteral("编辑插槽预设"))) {
						config->uiProfileManagedCategory = 0;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    if (GetSlotEditorContext().scope != ConfigScope::kGlobal) {
                        auto& globalSlotsForMenu = config->GetSlots(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global slots")) {
                            if (MergeConfigListByKey(a_slots, globalSlotsForMenu, [](const SlotDefinition& slot) { return slot.slotName; }, false) > 0) {
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global slots")) {
                            if (MergeConfigListByKey(a_slots, globalSlotsForMenu, [](const SlotDefinition& slot) { return slot.slotName; }, true) > 0) {
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (ImGui::CollapsingHeader(TextLiteral("预设与批量操作##slotTools"), ImGuiTreeNodeFlags_None)) {
                if (UIManagedProfileControls::DrawSelector(
                    "SlotModuleProfileSelector",
                    TextLiteral("插槽预设"),
                    IAD::Profile::GlobalProfileManager::GetSingleton().Slots(),
                    a_slots,
                    "MySlotTemplate",
                    [](std::vector<SlotDefinition>& target, const std::vector<SlotDefinition>& source) {
                        target = source;
                    },
                    [](std::vector<SlotDefinition>& target, const std::vector<SlotDefinition>& source) {
                        MergeConfigListByKey(target, source, [](const SlotDefinition& slot) { return slot.slotName; }, true);
                    },
                    true)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (GetSlotEditorContext().scope != ConfigScope::kGlobal) {
                    auto& globalSlots = config->GetSlots(ConfigScope::kGlobal, 0);
                    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("从全局层同步"));
                    if (ImGui::Button(TextLiteral("导入 Global 缺失槽位"))) {
                        if (MergeConfigListByKey(a_slots, globalSlots, [](const SlotDefinition& slot) { return slot.slotName; }, false) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新 Global 同名槽位"))) {
                        if (MergeConfigListByKey(a_slots, globalSlots, [](const SlotDefinition& slot) { return slot.slotName; }, true) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("只覆盖当前层已有的同名槽位，并添加缺失槽位；不会删除当前层独有槽位。"));
                }
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            UIEditorInteraction::ClampPaneWidth(config->uiLayout.slotLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);
            ImGui::BeginChild("SlotLeft", ImVec2(config->uiLayout.slotLeftPaneWidth, 0.0f), true);
            if (openAddSlotPopup) ImGui::OpenPopup("AddSlotPopup");
            if (ImGui::Button("+##newSlot", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("AddSlotPopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("新建装备槽位"));
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasSelectedSlot = !GetSlotEditorContext().selection.Empty();
            if (!hasSelectedSlot) ImGui::BeginDisabled();
            if (ImGui::Button("⧉##cloneSlot", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("CloneSlotPopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("复制选中装备槽位"));
            if (!hasSelectedSlot) ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasLocalSelectedSlot = std::any_of(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) {
                return GetSlotEditorContext().selection.IsSelected(slot.slotName);
            });
            if (!hasLocalSelectedSlot) ImGui::BeginDisabled();
            if (ImGui::Button("×##deleteSlot", ImVec2(34.0f, 30.0f))) {
                s_pendingDeleteSlot = GetSlotEditorContext().selection.Selected();
                ImGui::OpenPopup("DeleteSlotFromList");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("删除选中装备槽位"));
            if (!hasLocalSelectedSlot) ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("AddSlotPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newName[64] = "LeftHip"; static bool isDuplicate = false;
                ImGui::Text(TextLiteral("输入槽位唯一标识名:")); ImGui::TextDisabled("IAD_MOV_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##nsn", newName, 64)) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，请更换！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    if (strlen(newName) > 0) {
                        std::string finalName = "IAD_MOV_" + std::string(newName); isDuplicate = false;
                        for (auto& s : a_slots) { if (s.slotName == finalName) { isDuplicate = true; break; } }
                        if (!isDuplicate) {
                            SlotDefinition ns; ns.slotName = finalName; ns.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                            a_slots.push_back(ns); GetSlotEditorContext().selection.Select(finalName); ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine(); if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) { isDuplicate = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, SlotDefinition*> inheritedMap;
            if (GetSlotEditorContext().scope != ConfigScope::kGlobal) { for (auto& gs : config->GetSlots(ConfigScope::kGlobal, 0)) inheritedMap[gs.slotName] = &gs; }
            for (auto& s : a_slots) inheritedMap.erase(s.slotName);

            auto commitSlotRename = [&](const std::string& oldName, const std::string& newName) {
                // A Windows-style rename accepts an unchanged value and exits edit mode.
                if (newName == oldName) return true;
                if (newName == "IAD_MOV_") return false;
                const bool duplicate = std::any_of(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) {
                    return slot.slotName == newName && slot.slotName != oldName;
                });
                if (duplicate || inheritedMap.contains(newName)) return false;

                auto slotIt = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) {
                    return slot.slotName == oldName;
                });
                if (slotIt == a_slots.end()) return false;

                slotIt->slotName = newName;
                if (!GetSlotEditorContext().selection.Rename(oldName, newName)) {
                    GetSlotEditorContext().selection.Select(newName);
                }
                auto updateDisplaySlotReferences = [&](std::vector<CustomDefinition>& customs) {
                    for (auto& custom : customs) {
                        if (IsSameManagedName(custom.targetDisplaySlot, oldName)) {
                            custom.targetDisplaySlot = newName;
                        }
                    }
                };
                updateDisplaySlotReferences(config->GetCustoms(GetSlotEditorContext().scope, GetQueryID(GetSlotEditorContext())));
                if (GetSlotEditorContext().scope != ConfigScope::kGlobal) {
                    updateDisplaySlotReferences(config->GetCustoms(ConfigScope::kGlobal, 0));
                }
                UIEditCoordinator::RequestConfigSave();
                NodeManager::InvalidateForConfigRefresh();
                UIEditCoordinator::RequestRuntimeRefresh();
                return true;
            };

            auto beginInlineSlotRename = [&](SlotDefinition& slot) {
                s_inlineRenameSlot = slot.slotName;
                strcpy_s(s_inlineSlotRenameBuffer, sizeof(s_inlineSlotRenameBuffer), std::string(StripManagedName(slot.slotName)).c_str());
                s_inlineSlotRenameFocus = true;
                s_inlineSlotRenameInvalid = false;
            };

            auto beginInlineSlotPriorityEdit = [&](SlotDefinition& slot) {
                s_inlinePrioritySlot = slot.slotName;
                strcpy_s(s_inlineSlotPriorityBuffer, sizeof(s_inlineSlotPriorityBuffer), std::to_string(slot.priority).c_str());
                s_inlineSlotPriorityFocus = true;
                s_inlineSlotPriorityInvalid = false;
            };

            auto commitInlineSlotPriority = [&](SlotDefinition& slot) {
                int priority = 0;
                if (!ParseInlineInteger(s_inlineSlotPriorityBuffer, priority)) {
                    s_inlineSlotPriorityFocus = true;
                    s_inlineSlotPriorityInvalid = true;
                    return false;
                }
                if (slot.priority != priority) {
                    slot.priority = priority;
                    UIEditCoordinator::RequestConfigChange();
                }
                s_inlinePrioritySlot.clear();
                s_inlineSlotPriorityFocus = false;
                s_inlineSlotPriorityInvalid = false;
                return true;
            };

            if (openCloneSlotPopup) ImGui::OpenPopup("CloneSlotPopup");
            if (ImGui::BeginPopupModal("CloneSlotPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char cloneSlotName[64] = "CopiedSlot";
                static bool cloneDuplicate = false;
                ImGui::Text(TextLiteral("输入新装备槽位唯一标识名:"));
                ImGui::TextDisabled("IAD_MOV_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##cslot", cloneSlotName, sizeof(cloneSlotName))) cloneDuplicate = false;
                if (cloneDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，或没有可复制的槽位！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    const std::string finalName = "IAD_MOV_" + std::string(cloneSlotName);
                    cloneDuplicate = finalName == "IAD_MOV_" ||
                        std::any_of(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) { return slot.slotName == finalName; }) ||
                        inheritedMap.contains(finalName);

                    SlotDefinition* sourceSlot = nullptr;
                    auto sourceIt = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) {
                        return GetSlotEditorContext().selection.IsSelected(slot.slotName);
                    });
                    if (sourceIt != a_slots.end()) sourceSlot = &(*sourceIt);
                    else {
                        auto inheritedIt = FindManagedMapEntry(inheritedMap, GetSlotEditorContext().selection.Selected());
                        if (inheritedIt != inheritedMap.end()) sourceSlot = inheritedIt->second;
                    }

                    if (!cloneDuplicate && sourceSlot) {
                        SlotDefinition clone = *sourceSlot;
                        clone.slotName = finalName;
                        clone.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                        a_slots.push_back(clone);
                        GetSlotEditorContext().selection.Select(finalName);
                        UIEditCoordinator::RequestConfigSave();
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                        ImGui::CloseCurrentPopup();
                    } else if (!sourceSlot) {
                        cloneDuplicate = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    cloneDuplicate = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            std::string pendingCloneSlot;
            std::string pendingExtractSlot;

            auto requestInspectorTab = [&](std::string_view slotName, int tab) {
                GetSlotEditorContext().selection.Select(slotName);
                UIInspectorNavigation::Request(GetSlotEditorContext().inspectorNavigation, tab);
            };

            auto drawSlotContextMenu = [&](SlotDefinition& slot, bool inherited) {
                const std::string popupID = "SlotListContext##" + slot.slotName;
                if (!ImGui::BeginPopupContextItem(popupID.c_str())) return;

                GetSlotEditorContext().selection.Select(slot.slotName);
                if (inherited) {
                    if (ImGui::MenuItem(TextLiteral("提取到当前层级"))) {
                        pendingExtractSlot = slot.slotName;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("打开挂载与姿态"))) requestInspectorTab(slot.slotName, 0);
                    if (ImGui::MenuItem(TextLiteral("打开筛选与候选"))) requestInspectorTab(slot.slotName, 1);
                    if (ImGui::MenuItem(TextLiteral("打开模型与外观"))) requestInspectorTab(slot.slotName, 4);
                }
                else {
                    if (ImGui::MenuItem(slot.isEnabled ? TextLiteral("禁用槽位") : TextLiteral("启用槽位"))) {
                        slot.isEnabled = !slot.isEnabled;
                        UIEditCoordinator::RequestConfigChange();
                    }
                    if (ImGui::MenuItem(TextLiteral("重命名"))) {
                        s_pendingRenameSlot = slot.slotName;
                        strcpy_s(s_slotRenameBuffer, std::string(StripManagedName(slot.slotName)).c_str());
                    }
                    if (ImGui::MenuItem(TextLiteral("复制为新槽位"))) {
                        pendingCloneSlot = slot.slotName;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("打开挂载与姿态"))) requestInspectorTab(slot.slotName, 0);
                    if (ImGui::MenuItem(TextLiteral("打开筛选与候选"))) requestInspectorTab(slot.slotName, 1);
                    if (ImGui::MenuItem(TextLiteral("打开可见性"))) requestInspectorTab(slot.slotName, 2);
                    if (ImGui::MenuItem(TextLiteral("打开状态覆盖"))) requestInspectorTab(slot.slotName, 3);
                    if (ImGui::MenuItem(TextLiteral("打开模型与外观"))) requestInspectorTab(slot.slotName, 4);
                    if (ImGui::MenuItem(TextLiteral("打开物理"))) requestInspectorTab(slot.slotName, 5);
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("删除槽位"))) {
                        s_pendingDeleteSlot = slot.slotName;
                    }
                }
                ImGui::EndPopup();
            };

            auto drawSlotRow = [&](SlotDefinition& slot, bool inherited) {
                ImGui::PushID(slot.slotName.c_str());
                const bool selected = GetSlotEditorContext().selection.IsSelected(slot.slotName);
                const std::string displayName = std::string(StripManagedName(slot.slotName));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (inherited) ImGui::BeginDisabled();
                if (ImGui::Checkbox("##slotEnabled", &slot.isEnabled)) {
                    UIEditCoordinator::RequestConfigChange();
                }
                if (inherited) ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 5.0f);
                if (inherited) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                const bool editingName = !inherited && s_inlineRenameSlot == slot.slotName;
                if (editingName) {
                    if (s_inlineSlotRenameFocus) {
                        ImGui::SetKeyboardFocusHere();
                        s_inlineSlotRenameFocus = false;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool submitted = ImGui::InputText(
                        "##slotNameInlineEdit",
                        s_inlineSlotRenameBuffer,
                        sizeof(s_inlineSlotRenameBuffer),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool deactivated = ImGui::IsItemDeactivated() || IsInlineEditClickedOutside();
                    if (submitted || deactivated) {
                        if (!cancelled) {
                            const std::string newName = "IAD_MOV_" + std::string(s_inlineSlotRenameBuffer);
                            if (!commitSlotRename(slot.slotName, newName)) {
                                s_inlineSlotRenameFocus = true;
                                s_inlineSlotRenameInvalid = true;
                            }
                            else {
                                s_inlineRenameSlot.clear();
                                s_inlineSlotRenameInvalid = false;
                            }
                        }
                        else {
                            s_inlineRenameSlot.clear();
                            s_inlineSlotRenameFocus = false;
                            s_inlineSlotRenameInvalid = false;
                        }
                    }
                    if (s_inlineSlotRenameInvalid && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TextLiteral("名称为空、重复或与继承配置冲突"));
                    }
                }
                else {
                    if (ImGui::Selectable((displayName + "##slotListRow").c_str(), selected)) {
                        GetSlotEditorContext().selection.Select(slot.slotName);
                    }
                    if (!inherited && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        beginInlineSlotRename(slot);
                    }
                    drawSlotContextMenu(slot, inherited);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TextLiteral("目标节点: %s\n左键选择，双击重命名，右键打开快速操作"), slot.targetNode.empty() ? TextLiteral("未绑定") : slot.targetNode.c_str());
                    }
                }
                if (inherited) ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(1);
                const bool editingPriority = !inherited && s_inlinePrioritySlot == slot.slotName;
                if (editingPriority) {
                    if (s_inlineSlotPriorityFocus) {
                        ImGui::SetKeyboardFocusHere();
                        s_inlineSlotPriorityFocus = false;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool submitted = ImGui::InputText(
                        "##slotPriorityInlineEdit",
                        s_inlineSlotPriorityBuffer,
                        sizeof(s_inlineSlotPriorityBuffer),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool deactivated = ImGui::IsItemDeactivated() || IsInlineEditClickedOutside();
                    if (submitted || deactivated) {
                        if (!cancelled) {
                            commitInlineSlotPriority(slot);
                        }
                        else {
                            s_inlinePrioritySlot.clear();
                            s_inlineSlotPriorityFocus = false;
                            s_inlineSlotPriorityInvalid = false;
                        }
                    }
                    if (s_inlineSlotPriorityInvalid && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TextLiteral("请输入有效的整数"));
                    }
                }
                else if (inherited) ImGui::TextDisabled("P:%d", slot.priority);
                else {
                    // Use a plain text item for the display state. Selectable()
                    // can leave the label clipped to a single glyph in this
                    // table when a previous table width was persisted.
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("P:%d", slot.priority);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        beginInlineSlotPriorityEdit(slot);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("优先级：双击后输入整数"));
                }
                ImGui::PopID();
            };

            std::vector<SlotDefinition*> localSlotOrder;
            localSlotOrder.reserve(a_slots.size());
            for (auto& slot : a_slots) localSlotOrder.push_back(&slot);
            std::sort(localSlotOrder.begin(), localSlotOrder.end(), [](const SlotDefinition* lhs, const SlotDefinition* rhs) {
                if (lhs->priority != rhs->priority) return lhs->priority > rhs->priority;
                return lhs->slotName < rhs->slotName;
            });

            std::vector<SlotDefinition*> inheritedSlotOrder;
            inheritedSlotOrder.reserve(inheritedMap.size());
            for (auto& [name, slot] : inheritedMap) inheritedSlotOrder.push_back(slot);
            std::sort(inheritedSlotOrder.begin(), inheritedSlotOrder.end(), [](const SlotDefinition* lhs, const SlotDefinition* rhs) {
                if (lhs->priority != rhs->priority) return lhs->priority > rhs->priority;
                return lhs->slotName < rhs->slotName;
            });

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
            if (ImGui::BeginTable("SlotListRowsV2", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(TextLiteral("槽位"), ImGuiTableColumnFlags_WidthStretch);
                // The initial width keeps the value readable, while the header
                // separator remains draggable so users can choose the balance.
                ImGui::TableSetupColumn(TextLiteral("优先级"), ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableHeadersRow();

                if (!localSlotOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(TextLiteral("本级配置"));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(TextLiteral("高 → 低"));
                    for (auto* slot : localSlotOrder) drawSlotRow(*slot, false);
                }
                if (!inheritedSlotOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(TextLiteral("继承配置"));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(TextLiteral("只读"));
                    for (auto* slot : inheritedSlotOrder) drawSlotRow(*slot, true);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if (!s_pendingRenameSlot.empty()) ImGui::OpenPopup("RenameSlotFromList");
            if (ImGui::BeginPopupModal("RenameSlotFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("重命名槽位"));
                ImGui::TextDisabled("IAD_MOV_");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetNextItemWidth(240.0f);
                ImGui::InputText("##slotRenameFromList", s_slotRenameBuffer, sizeof(s_slotRenameBuffer));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    std::string newName = "IAD_MOV_" + std::string(s_slotRenameBuffer);
                    if (commitSlotRename(s_pendingRenameSlot, newName)) {
                        s_pendingRenameSlot.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingRenameSlot.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!pendingCloneSlot.empty()) {
                auto sourceIt = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) { return slot.slotName == pendingCloneSlot; });
                if (sourceIt == a_slots.end()) {
                    auto inheritedIt = inheritedMap.find(pendingCloneSlot);
                    if (inheritedIt != inheritedMap.end()) {
                        SlotDefinition clone = *inheritedIt->second;
                        std::string base = "IAD_MOV_" + std::string(StripManagedName(clone.slotName)) + "_Copy";
                        std::string candidate = base;
                        int suffix = 2;
                        while (std::any_of(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) { return slot.slotName == candidate; })) {
                            candidate = base + std::to_string(suffix++);
                        }
                        clone.slotName = candidate;
                        a_slots.push_back(clone);
                        GetSlotEditorContext().selection.Select(candidate);
                    }
                }
                else {
                    SlotDefinition clone = *sourceIt;
                    std::string base = "IAD_MOV_" + std::string(StripManagedName(clone.slotName)) + "_Copy";
                    std::string candidate = base;
                    int suffix = 2;
                    while (std::any_of(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) { return slot.slotName == candidate; })) {
                        candidate = base + std::to_string(suffix++);
                    }
                    clone.slotName = candidate;
                    a_slots.push_back(clone);
                    GetSlotEditorContext().selection.Select(candidate);
                }
                UIEditCoordinator::RequestConfigChange();
            }
            if (!pendingExtractSlot.empty()) {
                auto inheritedIt = inheritedMap.find(pendingExtractSlot);
                if (inheritedIt != inheritedMap.end()) {
                    a_slots.push_back(*inheritedIt->second);
                    GetSlotEditorContext().selection.Select(pendingExtractSlot);
                    UIEditCoordinator::RequestConfigChange();
                }
            }
            if (!s_pendingDeleteSlot.empty()) {
                ImGui::OpenPopup("DeleteSlotFromList");
            }
            if (ImGui::BeginPopupModal("DeleteSlotFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("确定删除槽位 [%s] 吗？"), s_pendingDeleteSlot.c_str());
                if (ImGui::Button(TextLiteral("删除"), { 100.0f, 0.0f })) {
                    auto deleteIt = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& slot) { return slot.slotName == s_pendingDeleteSlot; });
                    if (deleteIt != a_slots.end()) a_slots.erase(deleteIt);
                    GetSlotEditorContext().selection.ClearIfSelected(s_pendingDeleteSlot);
                    UIEditCoordinator::RequestConfigSave();
                            NodeManager::InvalidateForConfigRefresh();
                    UIEditCoordinator::RequestRuntimeRefresh();
                    s_pendingDeleteSlot.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingDeleteSlot.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::EndChild();

            UIEditorInteraction::DrawPaneSplitter("##vsplitter_slot", config->uiLayout.slotLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);

            ImGui::BeginChild("SlotRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& s) { return IsSameManagedName(s.slotName, GetSlotEditorContext().selection.Selected()); });
            SlotDefinition* activeSlot = nullptr; bool isInherited = false;
            if (it != a_slots.end()) { activeSlot = &(*it); }
            else {
                auto inheritedIt = FindManagedMapEntry(inheritedMap, GetSlotEditorContext().selection.Selected());
                if (inheritedIt != inheritedMap.end()) { activeSlot = inheritedIt->second; isInherited = true; }
            }

            if (activeSlot) {
                auto& s = *activeSlot; ImGui::PushID(("Slot_" + s.slotName).c_str());
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("💡 此配置继承自全局 (Global)，当前层级未进行覆写。"));
                    if (ImGui::Button(TextLiteral("➕ 提取到当前层级并覆写 (Add Override)"), { -1, 40 })) { a_slots.push_back(s); GetSlotEditorContext().selection.Select(s.slotName); UIEditCoordinator::RequestConfigSave(); UIEditCoordinator::RequestRuntimeRefresh(); }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }
                else {
                    // Name, priority, and enabled state are managed directly in
                    // the left list. Keeping a single source of truth here
                    // prevents the detail panel from duplicating row controls.
                }

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("绑定目标节点 (Target Node):")); ImGui::SameLine();
                if (DrawCMENodeSelector("##targetNode", s.targetNode, a_nodes, GetSlotEditorContext().scope, TextLiteral("未绑定 (不可见)"))) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::Separator();

                // Keep inspector navigation state at the window level. The selected
                // record still scopes its own controls above, but changing records
                // must not create a new ImGui tab-bar identity.
                ImGui::PopID();
                ImGui::PushID("SlotDetailStable");
                DrawSlotEditorTabs(s, GetSlotEditorContext(), "liveSlotTabs");
                ImGui::PopID();
                if (isInherited) ImGui::EndDisabled();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled(TextLiteral("请在左侧选择一个装备槽位")); }
            ImGui::EndChild();
            UIEditorInteraction::TrackLiveConfigEdits(GetSlotEditorContext().pendingConfigSave);
        }
        ImGui::End();
    }

    // 👇========== 🌟 核心面板类实现：挂载节点 ==========👇
    void UINodesWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowNodes) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.nodes"), &config->uiShowNodes, ImGuiWindowFlags_MenuBar)) {
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            ImGuiManager::GetSingleton().NotifyWindowFocused(2, focused);
            UIWorldPreviewEditor::GetSingleton().NotifyEditorWindow(
                UIWorldPreviewEditor::EditorWindow::kNodes,
                focused);
            bool doPropagate = DrawWindowHeader("NodeHdr", GetNodeEditorContext(), true, true);
            DrawConfiguredTargetBrowser("NodeConfiguredTargets", GetNodeEditorContext(), config->GetConfiguredNodeTargetIDs(GetNodeEditorContext().scope));
            uint32_t queryID = GetQueryID(GetNodeEditorContext());
            auto& a_nodes = config->GetNodes(GetNodeEditorContext().scope, queryID);
            if (doPropagate) { auto& globalNodes = config->GetNodes(ConfigScope::kGlobal, 0); for (auto& n : a_nodes) { auto git = std::find_if(globalNodes.begin(), globalNodes.end(), [&](const NodeDefinition& gn) { return gn.nodeName == n.nodeName; }); if (git != globalNodes.end()) *git = n; else globalNodes.push_back(n); } UIEditCoordinator::RequestRuntimeRefresh(); }
            bool openAddNodePopup = false;
            bool openCloneNodePopup = false;
            static std::string s_pendingDeleteNode;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(TextLiteral("文件"))) {
                    if (ImGui::MenuItem(TextLiteral("保存配置"))) UIEditCoordinator::CommitNow();
                    if (ImGui::MenuItem(TextLiteral("关闭"))) config->uiShowNodes = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TextLiteral("操作"))) {
                    if (ImGui::MenuItem(TextLiteral("新建节点"))) openAddNodePopup = true;
					if (ImGui::MenuItem(TextLiteral("编辑节点预设"))) {
						config->uiProfileManagedCategory = 1;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    const bool hasSelectedNode = !GetNodeEditorContext().selection.Empty();
                    if (!hasSelectedNode) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Clone selected node")) openCloneNodePopup = true;
                    if (!hasSelectedNode) ImGui::EndDisabled();
                    if (ImGui::MenuItem("Refresh node bindings")) NodeManager::InvalidateForConfigRefresh();
                    if (ImGui::MenuItem("Force refresh displays")) {
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (GetNodeEditorContext().scope != ConfigScope::kGlobal) {
                        auto& globalNodesForMenu = config->GetNodes(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global nodes")) {
                            if (MergeConfigListByKey(a_nodes, globalNodesForMenu, [](const NodeDefinition& node) { return node.nodeName; }, false) > 0) {
                                NodeManager::InvalidateForConfigRefresh();
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global nodes")) {
                            if (MergeConfigListByKey(a_nodes, globalNodesForMenu, [](const NodeDefinition& node) { return node.nodeName; }, true) > 0) {
                                NodeManager::InvalidateForConfigRefresh();
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (ImGui::CollapsingHeader(TextLiteral("预设、转换与批量操作##nodeTools"), ImGuiTreeNodeFlags_None)) {
                if (UIManagedProfileControls::DrawSelector(
                    "NodeModuleProfileSelector",
                    TextLiteral("节点预设"),
                    IAD::Profile::GlobalProfileManager::GetSingleton().Nodes(),
                    a_nodes,
                    "MyNodeTemplate",
                    [](std::vector<NodeDefinition>& target, const std::vector<NodeDefinition>& source) {
                        target = source;
                    },
                    [](std::vector<NodeDefinition>& target, const std::vector<NodeDefinition>& source) {
                    MergeConfigListByKey(target, source, [](const NodeDefinition& node) { return node.nodeName; }, true);
                    },
                    true)) {
                    NodeManager::InvalidateForConfigRefresh();
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("节点转换配置 (ConvertNodes)"));
                static char convertProfName[64] = "MyConvertNodes";
                static std::string selConvertProf = "";
                static bool convertV2 = false;
                ImGui::SetNextItemWidth(150.0f);
                ImGui::InputText("##cnpn", convertProfName, 64);
                ImGui::SameLine();
                ImGui::Checkbox("ConvertNodes2", &convertV2);
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("保存转换配置"))) {
                    if (convertProfName[0] != '\0') {
                        config->SaveNodeConversionProfile(convertProfName, a_nodes, convertV2);
                        selConvertProf = convertProfName;
                    }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::BeginCombo("##cnlp", selConvertProf.empty() ? TextLiteral("选择转换配置...") : selConvertProf.c_str())) {
                    for (auto& p : config->GetAvailableNodeConversionProfiles(convertV2)) {
                        if (ImGui::Selectable(p.c_str())) selConvertProf = p;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("导入合并##cn"))) {
                    if (!selConvertProf.empty() && config->LoadNodeConversionProfile(selConvertProf, a_nodes, false, convertV2)) {
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                }
                ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("导入覆盖##cn"))) {
                    if (!selConvertProf.empty() && config->LoadNodeConversionProfile(selConvertProf, a_nodes, true, convertV2)) {
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                }
                if (GetNodeEditorContext().scope != ConfigScope::kGlobal) {
                    auto& globalNodes = config->GetNodes(ConfigScope::kGlobal, 0);
                    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("从全局层同步"));
                    if (ImGui::Button(TextLiteral("导入 Global 缺失节点"))) {
                        if (MergeConfigListByKey(a_nodes, globalNodes, [](const NodeDefinition& node) { return node.nodeName; }, false) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新 Global 同名节点"))) {
                        if (MergeConfigListByKey(a_nodes, globalNodes, [](const NodeDefinition& node) { return node.nodeName; }, true) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("只覆盖当前层已有的同名节点，并添加缺失节点；不会删除当前层独有节点。"));
                }
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            UIEditorInteraction::ClampPaneWidth(config->uiLayout.nodeLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);
            ImGui::BeginChild("NodeLeft", ImVec2(config->uiLayout.nodeLeftPaneWidth, 0.0f), true);
            if (openAddNodePopup) ImGui::OpenPopup("AddNodePopup");
            if (ImGui::Button("+##newNode", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("AddNodePopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("新建挂载节点"));
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasSelectedNode = !GetNodeEditorContext().selection.Empty();
            if (!hasSelectedNode) ImGui::BeginDisabled();
            if (ImGui::Button("⧉##cloneNode", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("CloneNodePopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("复制选中挂载节点"));
            if (!hasSelectedNode) ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasLocalSelectedNode = std::any_of(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& node) {
                return GetNodeEditorContext().selection.IsSelected(node.nodeName);
            });
            if (!hasLocalSelectedNode) ImGui::BeginDisabled();
            if (ImGui::Button("×##deleteNode", ImVec2(34.0f, 30.0f))) {
                s_pendingDeleteNode = GetNodeEditorContext().selection.Selected();
                ImGui::OpenPopup("DeleteNodeFromList");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("删除选中挂载节点"));
            if (!hasLocalSelectedNode) ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("AddNodePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newNodeName[64] = "LeftHip"; static bool isDuplicate = false;
                ImGui::Text(TextLiteral("输入节点唯一标识名:")); ImGui::TextDisabled("IAD_CME_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##nnn", newNodeName, 64)) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，请更换！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    if (strlen(newNodeName) > 0) {
                        std::string finalName = "IAD_CME_" + std::string(newNodeName); isDuplicate = false;
                        for (auto& n : a_nodes) { if (n.nodeName == finalName) { isDuplicate = true; break; } }
                        if (!isDuplicate) {
                            NodeDefinition nn; nn.nodeName = finalName; nn.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                            a_nodes.push_back(nn); GetNodeEditorContext().selection.Select(finalName); ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine(); if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) { isDuplicate = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, NodeDefinition*> inheritedMap;
            if (GetNodeEditorContext().scope != ConfigScope::kGlobal) { for (auto& gn : config->GetNodes(ConfigScope::kGlobal, 0)) inheritedMap[gn.nodeName] = &gn; }
            for (auto& n : a_nodes) inheritedMap.erase(n.nodeName);

            if (openCloneNodePopup) ImGui::OpenPopup("CloneNodePopup");
            if (ImGui::BeginPopupModal("CloneNodePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char cloneNodeName[64] = "CopiedNode";
                static bool cloneDuplicate = false;
                ImGui::Text(TextLiteral("输入新节点唯一标识名:"));
                ImGui::TextDisabled("IAD_CME_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##cnn", cloneNodeName, 64)) cloneDuplicate = false;
                if (cloneDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，请更换！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    std::string finalName = "IAD_CME_" + std::string(cloneNodeName);
                    cloneDuplicate = finalName == "IAD_CME_";
                    for (auto& existing : a_nodes) {
                        if (existing.nodeName == finalName) {
                            cloneDuplicate = true;
                            break;
                        }
                    }
                    if (!cloneDuplicate && inheritedMap.count(finalName)) cloneDuplicate = true;

                    NodeDefinition* sourceNode = nullptr;
                    auto srcIt = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& n) { return GetNodeEditorContext().selection.IsSelected(n.nodeName); });
                    if (srcIt != a_nodes.end()) sourceNode = &(*srcIt);
                    else {
                        auto inheritedIt = FindManagedMapEntry(inheritedMap, GetNodeEditorContext().selection.Selected());
                        if (inheritedIt != inheritedMap.end()) sourceNode = inheritedIt->second;
                    }

                    if (!cloneDuplicate && sourceNode) {
                        NodeDefinition clone = *sourceNode;
                        clone.nodeName = finalName;
                        clone.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                        a_nodes.push_back(clone);
                        GetNodeEditorContext().selection.Select(finalName);
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    cloneDuplicate = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            static std::string s_pendingRenameNode;
            static char s_nodeRenameBuffer[64] = {};
            static std::string s_inlineRenameNode;
            static char s_inlineNodeRenameBuffer[64] = {};
            static bool s_inlineNodeRenameFocus = false;
            static bool s_inlineNodeRenameInvalid = false;
            std::string pendingExtractNode;
            auto requestNodeInspectorTab = [&](std::string_view nodeName, int tab) {
                GetNodeEditorContext().selection.Select(nodeName);
                UIInspectorNavigation::Request(GetNodeEditorContext().inspectorNavigation, tab);
            };

            auto commitNodeRename = [&](const std::string& oldName, const std::string& newName) {
                // A Windows-style rename accepts an unchanged value and exits edit mode.
                if (newName == oldName) return true;
                if (newName == "IAD_CME_") return false;
                const bool duplicate = std::any_of(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& node) {
                    return node.nodeName == newName && node.nodeName != oldName;
                });
                if (duplicate || inheritedMap.contains(newName)) return false;

                auto nodeIt = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& node) {
                    return node.nodeName == oldName;
                });
                if (nodeIt == a_nodes.end()) return false;

                nodeIt->nodeName = newName;
                if (!GetNodeEditorContext().selection.Rename(oldName, newName)) {
                    GetNodeEditorContext().selection.Select(newName);
                }
                auto& allSlots = config->GetSlots(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext()));
                for (auto& slot : allSlots) {
                    if (slot.targetNode == oldName) slot.targetNode = newName;
                }
                auto updateCustomTargets = [&](std::vector<CustomDefinition>& customs) {
                    for (auto& custom : customs) {
                        if (custom.targetNode == oldName) custom.targetNode = newName;
                        for (auto& group : custom.modelGroups) {
                            if (group.targetNode == oldName) group.targetNode = newName;
                        }
                    }
                };
                updateCustomTargets(config->GetCustoms(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext())));
                if (GetNodeEditorContext().scope != ConfigScope::kGlobal) {
                    for (auto& slot : config->GetSlots(ConfigScope::kGlobal, 0)) {
                        if (slot.targetNode == oldName) slot.targetNode = newName;
                    }
                    updateCustomTargets(config->GetCustoms(ConfigScope::kGlobal, 0));
                }
                        UIEditCoordinator::RequestConfigSave();
                        NodeManager::InvalidateForConfigRefresh();
                        UIEditCoordinator::RequestRuntimeRefresh();
                return true;
            };

            auto beginInlineNodeRename = [&](NodeDefinition& node) {
                s_inlineRenameNode = node.nodeName;
                strcpy_s(s_inlineNodeRenameBuffer, sizeof(s_inlineNodeRenameBuffer), std::string(StripManagedName(node.nodeName)).c_str());
                s_inlineNodeRenameFocus = true;
                s_inlineNodeRenameInvalid = false;
            };

            auto deleteNodeByName = [&](const std::string& nodeName) {
                auto deleteIt = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& node) {
                    return IsSameManagedName(node.nodeName, nodeName);
                });
                if (deleteIt == a_nodes.end()) return false;

                const std::string deletedNodeName = deleteIt->nodeName;
                a_nodes.erase(deleteIt);
                if (GetNodeEditorContext().selection.IsSelected(nodeName)) GetNodeEditorContext().selection.Clear();

                auto clearCustomTargets = [&](std::vector<CustomDefinition>& customs) {
                    for (auto& custom : customs) {
                        if (custom.targetNode == deletedNodeName) custom.targetNode.clear();
                        for (auto& group : custom.modelGroups) {
                            if (group.targetNode == deletedNodeName) group.targetNode.clear();
                        }
                    }
                };
                auto clearReferences = [&](ConfigScope scope, uint32_t targetID) {
                    for (auto& slot : config->GetSlots(scope, targetID)) {
                        if (slot.targetNode == deletedNodeName) slot.targetNode.clear();
                    }
                    clearCustomTargets(config->GetCustoms(scope, targetID));
                };

                clearReferences(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext()));
                if (GetNodeEditorContext().scope != ConfigScope::kGlobal) {
                    clearReferences(ConfigScope::kGlobal, 0);
                }
                UIEditCoordinator::RequestConfigSave();
                NodeManager::InvalidateForConfigRefresh();
                UIEditCoordinator::RequestRuntimeRefresh();
                return true;
            };

            auto drawNodeContextMenu = [&](NodeDefinition& node, bool inherited) {
                const std::string popupID = "NodeListContext##" + node.nodeName;
                if (!ImGui::BeginPopupContextItem(popupID.c_str())) return;

                GetNodeEditorContext().selection.Select(node.nodeName);
                if (inherited) {
                    if (ImGui::MenuItem(TextLiteral("提取到当前层级"))) pendingExtractNode = node.nodeName;
                }
                else {
                    if (ImGui::MenuItem(node.isEnabled ? TextLiteral("禁用节点") : TextLiteral("启用节点"))) {
                        node.isEnabled = !node.isEnabled;
                        UIEditCoordinator::RequestConfigSave();
                        // Enabling/disabling a node only changes visibility. Keep the
                        // actor-owned CME/MOV cache intact so the transform pass can
                        // cull existing models and restore them without losing the
                        // scene-node association.
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (ImGui::MenuItem(TextLiteral("重命名"))) {
                        s_pendingRenameNode = node.nodeName;
                        strcpy_s(s_nodeRenameBuffer, sizeof(s_nodeRenameBuffer), std::string(StripManagedName(node.nodeName)).c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("删除节点"))) s_pendingDeleteNode = node.nodeName;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TextLiteral("打开位置与姿态"))) requestNodeInspectorTab(node.nodeName, 0);
                if (ImGui::MenuItem(TextLiteral("打开目标骨骼"))) requestNodeInspectorTab(node.nodeName, 1);
                if (ImGui::MenuItem(TextLiteral("打开可见性"))) requestNodeInspectorTab(node.nodeName, 2);
                if (ImGui::MenuItem(TextLiteral("打开状态覆盖"))) requestNodeInspectorTab(node.nodeName, 3);
                if (ImGui::MenuItem(TextLiteral("打开物理"))) requestNodeInspectorTab(node.nodeName, 4);
                ImGui::EndPopup();
            };

            auto drawNodeRow = [&](NodeDefinition& node, bool inherited) {
                ImGui::PushID(node.nodeName.c_str());
                const bool selected = GetNodeEditorContext().selection.IsSelected(node.nodeName);
                const std::string displayName = std::string(StripManagedName(node.nodeName));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (inherited) ImGui::BeginDisabled();
                if (ImGui::Checkbox("##nodeEnabled", &node.isEnabled)) {
                    UIEditCoordinator::RequestConfigSave();
                    // Do not invalidate the scene-node cache for a visibility-only
                    // change. Clearing it makes UpdateActorTransforms lose the MOV
                    // handle before it can cull the existing display models.
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (inherited) ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 5.0f);
                if (inherited) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                const bool editingName = !inherited && s_inlineRenameNode == node.nodeName;
                if (editingName) {
                    if (s_inlineNodeRenameFocus) {
                        ImGui::SetKeyboardFocusHere();
                        s_inlineNodeRenameFocus = false;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool submitted = ImGui::InputText(
                        "##nodeNameInlineEdit",
                        s_inlineNodeRenameBuffer,
                        sizeof(s_inlineNodeRenameBuffer),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    const bool deactivated = ImGui::IsItemDeactivated() || IsInlineEditClickedOutside();
                    if (submitted || deactivated) {
                        if (!cancelled) {
                            const std::string newName = "IAD_CME_" + std::string(s_inlineNodeRenameBuffer);
                            if (!commitNodeRename(node.nodeName, newName)) {
                                s_inlineNodeRenameFocus = true;
                                s_inlineNodeRenameInvalid = true;
                            }
                            else {
                                s_inlineRenameNode.clear();
                                s_inlineNodeRenameInvalid = false;
                            }
                        }
                        else {
                            s_inlineRenameNode.clear();
                            s_inlineNodeRenameFocus = false;
                            s_inlineNodeRenameInvalid = false;
                        }
                    }
                    if (s_inlineNodeRenameInvalid && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TextLiteral("名称为空、重复或与继承配置冲突"));
                    }
                }
                else {
                    if (ImGui::Selectable((displayName + "##nodeListRow").c_str(), selected)) {
                        GetNodeEditorContext().selection.Select(node.nodeName);
                    }
                    if (!inherited && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        beginInlineNodeRename(node);
                    }
                    drawNodeContextMenu(node, inherited);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(TextLiteral("目标骨骼: %zu 个\n左键选择，双击重命名，右键打开快速操作"), node.fallbackHosts.size());
                    }
                }
                if (inherited) ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(1);
                if (node.fallbackHosts.empty()) ImGui::TextDisabled(TextLiteral("未设置"));
                else ImGui::TextDisabled(TextLiteral("%zu 个"), node.fallbackHosts.size());
                ImGui::PopID();
            };

            std::vector<NodeDefinition*> localNodeOrder;
            localNodeOrder.reserve(a_nodes.size());
            for (auto& node : a_nodes) localNodeOrder.push_back(&node);
            std::sort(localNodeOrder.begin(), localNodeOrder.end(), [](const NodeDefinition* lhs, const NodeDefinition* rhs) {
                return lhs->nodeName < rhs->nodeName;
            });

            std::vector<NodeDefinition*> inheritedNodeOrder;
            inheritedNodeOrder.reserve(inheritedMap.size());
            for (auto& [name, node] : inheritedMap) inheritedNodeOrder.push_back(node);
            std::sort(inheritedNodeOrder.begin(), inheritedNodeOrder.end(), [](const NodeDefinition* lhs, const NodeDefinition* rhs) {
                return lhs->nodeName < rhs->nodeName;
            });

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
            if (ImGui::BeginTable("NodeListRowsV2", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(TextLiteral("节点"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(TextLiteral("目标"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
                ImGui::TableHeadersRow();
                if (!localNodeOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(TextLiteral("本级配置"));
                    for (auto* node : localNodeOrder) drawNodeRow(*node, false);
                }
                if (!inheritedNodeOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(TextLiteral("继承配置"));
                    for (auto* node : inheritedNodeOrder) drawNodeRow(*node, true);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if (!s_pendingRenameNode.empty()) ImGui::OpenPopup("RenameNodeFromList");
            if (ImGui::BeginPopupModal("RenameNodeFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("重命名节点"));
                ImGui::TextDisabled("IAD_CME_");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetNextItemWidth(240.0f);
                ImGui::InputText("##nodeRenameFromList", s_nodeRenameBuffer, sizeof(s_nodeRenameBuffer));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    const std::string newName = "IAD_CME_" + std::string(s_nodeRenameBuffer);
                    if (commitNodeRename(s_pendingRenameNode, newName)) {
                        s_pendingRenameNode.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingRenameNode.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!s_pendingDeleteNode.empty()) ImGui::OpenPopup("DeleteNodeFromList");
            if (ImGui::BeginPopupModal("DeleteNodeFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("确定删除节点 [%s] 吗？"), s_pendingDeleteNode.c_str());
                ImGui::TextDisabled(TextLiteral("节点引用也会从当前层级和全局层级中清理。"));
                if (ImGui::Button(TextLiteral("删除"), { 100.0f, 0.0f })) {
                    deleteNodeByName(s_pendingDeleteNode);
                    s_pendingDeleteNode.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingDeleteNode.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!pendingExtractNode.empty()) {
                auto inheritedIt = inheritedMap.find(pendingExtractNode);
                if (inheritedIt != inheritedMap.end()) {
                    a_nodes.push_back(*inheritedIt->second);
                    GetNodeEditorContext().selection.Select(pendingExtractNode);
                    UIEditCoordinator::RequestConfigSave();
                    NodeManager::InvalidateForConfigRefresh();
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
            }
            ImGui::EndChild();

            UIEditorInteraction::DrawPaneSplitter("##vsplitter_node", config->uiLayout.nodeLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);

            ImGui::BeginChild("NodeRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& n) { return GetNodeEditorContext().selection.IsSelected(n.nodeName); });
            NodeDefinition* activeNode = nullptr; bool isInherited = false;
            if (it != a_nodes.end()) { activeNode = &(*it); }
            else {
                auto inheritedIt = FindManagedMapEntry(inheritedMap, GetNodeEditorContext().selection.Selected());
                if (inheritedIt != inheritedMap.end()) { activeNode = inheritedIt->second; isInherited = true; }
            }

            if (activeNode) {
                auto& n = *activeNode; ImGui::PushID(("Node_" + n.nodeName).c_str());
                bool nodeEdited = false;
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("💡 此配置继承自全局 (Global)，当前层级未进行覆写。"));
                    if (ImGui::Button(TextLiteral("➕ 提取到当前层级并覆写 (Add Override)"), { -1, 40 })) { a_nodes.push_back(n); GetNodeEditorContext().selection.Select(n.nodeName); nodeEdited = true; UIEditCoordinator::RequestConfigSave(); NodeManager::InvalidateForConfigRefresh(); UIEditCoordinator::RequestRuntimeRefresh(); }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }
                {
                    auto& nodes = n.fallbackHosts;
                    bool requestAddBone = false;
                    size_t removeBoneIndex = nodes.size();
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("目标骨骼"));

                    if (nodes.empty()) {
                        ImGui::SameLine(0.0f, 8.0f);
                        if (ImGui::SmallButton("+##addTargetBoneEmpty")) { requestAddBone = true; nodeEdited = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("添加目标骨骼"));
                    }

                    for (size_t x = 0; x < nodes.size(); ++x) {
                        if (x > 0) {
                            ImGui::NewLine();
                            ImGui::Indent();
                        } else {
                            ImGui::SameLine(0.0f, 8.0f);
                        }

                        ImGui::PushID(static_cast<int>(x));
                        if (DrawBoneScannerBox(("##bone" + std::to_string(x)).c_str(), nodes[x])) nodeEdited = true;

                        ImGui::SameLine(0.0f, 4.0f);
                        if (ImGui::SmallButton("↑##moveBoneUp")) {
                            if (x > 0) { std::swap(nodes[x], nodes[x - 1]); nodeEdited = true; }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("上移目标骨骼"));

                        ImGui::SameLine(0.0f, 2.0f);
                        const bool removeBone = ImGui::SmallButton("×##removeBone");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("删除目标骨骼"));
                        if (removeBone) { removeBoneIndex = x; nodeEdited = true; }

                        if (!removeBone && x + 1 == nodes.size()) {
                            ImGui::SameLine(0.0f, 4.0f);
                            if (ImGui::SmallButton("+##addTargetBone")) { requestAddBone = true; nodeEdited = true; }
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("添加目标骨骼"));
                        }
                        ImGui::PopID();

                        if (x > 0) ImGui::Unindent();
                    }
                    if (removeBoneIndex < nodes.size()) { nodes.erase(nodes.begin() + removeBoneIndex); nodeEdited = true; }
                    if (requestAddBone) { nodes.push_back("Weapon"); nodeEdited = true; }
                }
                ImGui::Spacing();

                if (ImGui::Checkbox(TextLiteral("绝对坐标 (Absolute)"), &n.absolutePosition)) { nodeEdited = true; UIEditCoordinator::RequestRuntimeRefresh(); }
                ImGui::SameLine(); ImGui::TextDisabled(TextLiteral("武器/身形自适应：当前变换路径不支持"));
                ImGui::Separator();

                // Keep inspector navigation state at the window level. The selected
                // record still scopes its own controls above, but changing records
                // must not create a new ImGui tab-bar identity.
                ImGui::PopID();
                ImGui::PushID("NodeDetailStable");
                if (DrawNodeEditorTabs(n, GetNodeEditorContext(), "liveNodeTabs")) {
                    nodeEdited = true;
                    NodeManager::InvalidateForConfigRefresh();
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::PopID();
                if (nodeEdited) UIEditCoordinator::RequestConfigSave();
                if (isInherited) ImGui::EndDisabled();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled(TextLiteral("请在左侧选择一个挂载节点")); }
            ImGui::EndChild();
            UIEditorInteraction::TrackLiveConfigEdits(GetNodeEditorContext().pendingConfigSave);
        }
        ImGui::End();
    }

    // 👇========== 🌟 核心面板类实现：专属神兵 ==========👇
    void UICustomsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowCustoms) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.customs"), &config->uiShowCustoms, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(3);
            bool doPropagate = DrawWindowHeader("CustomHdr", GetCustomEditorContext(), true, true);
            DrawConfiguredTargetBrowser("CustomConfiguredTargets", GetCustomEditorContext(), config->GetConfiguredCustomTargetIDs(GetCustomEditorContext().scope));
            uint32_t queryID = GetQueryID(GetCustomEditorContext());
            auto& a_customs = config->GetCustoms(GetCustomEditorContext().scope, queryID); auto& a_nodes = config->GetNodes(GetCustomEditorContext().scope, queryID); auto& a_slots = config->GetSlots(GetCustomEditorContext().scope, queryID);
            if (doPropagate) { auto& globalCustoms = config->GetCustoms(ConfigScope::kGlobal, 0); for (auto& c : a_customs) { auto git = std::find_if(globalCustoms.begin(), globalCustoms.end(), [&](const CustomDefinition& gc) { return gc.customName == c.customName; }); if (git != globalCustoms.end()) *git = c; else globalCustoms.push_back(c); } UIEditCoordinator::RequestRuntimeRefresh(); }
            bool openAddCustomPopup = false;
            bool openCloneCustomPopup = false;
            static std::string s_pendingDeleteCustom;
            static std::string s_pendingRenameCustom;
            static char s_customRenameBuffer[96] = {};
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(TextLiteral("文件"))) {
                    if (ImGui::MenuItem(TextLiteral("保存配置"))) UIEditCoordinator::CommitNow();
                    if (ImGui::MenuItem(TextLiteral("关闭"))) config->uiShowCustoms = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(TextLiteral("操作"))) {
                    if (ImGui::MenuItem(TextLiteral("新建专属展示"))) openAddCustomPopup = true;
                    const bool hasSelectedCustom = !GetCustomEditorContext().selection.Empty();
                    if (!hasSelectedCustom) ImGui::BeginDisabled();
                    if (ImGui::MenuItem(TextLiteral("复制选中专属展示"))) openCloneCustomPopup = true;
                    if (!hasSelectedCustom) ImGui::EndDisabled();
                    if (ImGui::MenuItem("Force refresh displays")) UIEditCoordinator::RequestRuntimeRefresh();
					ImGui::Separator();
					if (ImGui::MenuItem(TextLiteral("编辑专属展示预设"))) {
						config->uiProfileManagedCategory = 2;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    if (GetCustomEditorContext().scope != ConfigScope::kGlobal) {
                        auto& globalCustomsForMenu = config->GetCustoms(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global custom displays")) {
                            if (MergeConfigListByKey(a_customs, globalCustomsForMenu, [](const CustomDefinition& custom) { return custom.customName; }, false) > 0) {
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global custom displays")) {
                            if (MergeConfigListByKey(a_customs, globalCustomsForMenu, [](const CustomDefinition& custom) { return custom.customName; }, true) > 0) {
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (ImGui::CollapsingHeader(TextLiteral("预设与批量操作##customTools"), ImGuiTreeNodeFlags_None)) {
                if (UIManagedProfileControls::DrawSelector(
                    "CustomModuleProfileSelector",
                    TextLiteral("专属展示预设"),
                    IAD::Profile::GlobalProfileManager::GetSingleton().Customs(),
                    a_customs,
                    "MyCustomTemplate",
                    [](std::vector<CustomDefinition>& target, const std::vector<CustomDefinition>& source) {
                        target = source;
                    },
                    [](std::vector<CustomDefinition>& target, const std::vector<CustomDefinition>& source) {
                        MergeConfigListByKey(target, source, [](const CustomDefinition& custom) { return custom.customName; }, true);
                    },
                    true)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (GetCustomEditorContext().scope != ConfigScope::kGlobal) {
                    auto& globalCustoms = config->GetCustoms(ConfigScope::kGlobal, 0);
                    ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("从全局层同步"));
                    if (ImGui::Button(TextLiteral("导入 Global 缺失规则"))) {
                        if (MergeConfigListByKey(a_customs, globalCustoms, [](const CustomDefinition& custom) { return custom.customName; }, false) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新 Global 同名规则"))) {
                        if (MergeConfigListByKey(a_customs, globalCustoms, [](const CustomDefinition& custom) { return custom.customName; }, true) > 0) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("只覆盖当前层已有的同名 Custom 规则，并添加缺失规则；不会删除当前层独有规则。"));
                }
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            UIEditorInteraction::ClampPaneWidth(config->uiLayout.customLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);
            ImGui::BeginChild("CustomLeft", ImVec2(config->uiLayout.customLeftPaneWidth, 0.0f), true);
            if (openAddCustomPopup) ImGui::OpenPopup("AddCustomPopup");
            if (ImGui::Button("+##newCustom", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("AddCustomPopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("新建专属展示"));
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasSelectedCustom = !GetCustomEditorContext().selection.Empty();
            if (!hasSelectedCustom) ImGui::BeginDisabled();
            if (ImGui::Button("⧉##cloneCustom", ImVec2(34.0f, 30.0f))) ImGui::OpenPopup("CloneCustomPopup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("复制选中专属展示"));
            if (!hasSelectedCustom) ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 4.0f);
            const bool hasLocalSelectedCustom = std::any_of(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                return GetCustomEditorContext().selection.IsSelected(custom.customName);
            });
            if (!hasLocalSelectedCustom) ImGui::BeginDisabled();
            if (ImGui::Button("×##deleteCustom", ImVec2(34.0f, 30.0f))) {
                s_pendingDeleteCustom = GetCustomEditorContext().selection.Selected();
                ImGui::OpenPopup("DeleteCustomFromList");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(TextLiteral("删除选中专属展示"));
            if (!hasLocalSelectedCustom) ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("AddCustomPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newCName[64] = "My_Special_Weapon";
                static bool isDuplicate = false;
                ImGui::Text(TextLiteral("输入该覆盖规则的备注名称:"));
                if (ImGui::InputText("##ncn", newCName, sizeof(newCName))) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，请更换！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    if (strlen(newCName) > 0) {
                        isDuplicate = std::any_of(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                            return custom.customName == newCName;
                        });
                        if (!isDuplicate) {
                            CustomDefinition cd;
                            cd.customName = newCName;
                            cd.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                            a_customs.push_back(cd);
                            GetCustomEditorContext().selection.Select(newCName);
                            UIEditCoordinator::RequestConfigChange();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    isDuplicate = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, CustomDefinition*> inheritedMap;
            if (GetCustomEditorContext().scope != ConfigScope::kGlobal) { for (auto& gc : config->GetCustoms(ConfigScope::kGlobal, 0)) inheritedMap[gc.customName] = &gc; }
            for (auto& c : a_customs) inheritedMap.erase(c.customName);

            std::string pendingCloneCustom;
            std::string pendingExtractCustom;

            if (openCloneCustomPopup) ImGui::OpenPopup("CloneCustomPopup");
            if (ImGui::BeginPopupModal("CloneCustomPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char cloneCustomName[96] = "CopiedCustom";
                static bool cloneDuplicate = false;
                ImGui::Text(TextLiteral("输入新专属展示唯一名称:"));
                if (ImGui::InputText("##cloneCustomName", cloneCustomName, sizeof(cloneCustomName))) cloneDuplicate = false;
                if (cloneDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 该名称已被使用，或没有可复制的规则！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    const std::string finalName = cloneCustomName;
                    cloneDuplicate = finalName.empty() ||
                        std::any_of(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) { return custom.customName == finalName; }) ||
                        inheritedMap.contains(finalName);

                    CustomDefinition* sourceCustom = nullptr;
                    auto sourceIt = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                        return GetCustomEditorContext().selection.IsSelected(custom.customName);
                    });
                    if (sourceIt != a_customs.end()) sourceCustom = &(*sourceIt);
                    else {
                        auto inheritedIt = inheritedMap.find(GetCustomEditorContext().selection.Selected());
                        if (inheritedIt != inheritedMap.end()) sourceCustom = inheritedIt->second;
                    }
                    if (!cloneDuplicate && sourceCustom) {
                        CustomDefinition clone = *sourceCustom;
                        clone.customName = finalName;
                        clone.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                        a_customs.push_back(clone);
                        GetCustomEditorContext().selection.Select(finalName);
                        UIEditCoordinator::RequestConfigChange();
                        ImGui::CloseCurrentPopup();
                    } else if (!sourceCustom) {
                        cloneDuplicate = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    cloneDuplicate = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            auto requestCustomInspectorTab = [&](std::string_view customName, int tab) {
                GetCustomEditorContext().selection.Select(customName);
                UIInspectorNavigation::Request(GetCustomEditorContext().inspectorNavigation, tab);
            };

            auto drawCustomContextMenu = [&](CustomDefinition& custom, bool inherited) {
                const std::string popupID = "CustomListContext##" + custom.customName;
                if (!ImGui::BeginPopupContextItem(popupID.c_str())) return;

                GetCustomEditorContext().selection.Select(custom.customName);
                if (inherited) {
                    if (ImGui::MenuItem(TextLiteral("提取到当前层级"))) pendingExtractCustom = custom.customName;
                } else {
                    if (ImGui::MenuItem(custom.isEnabled ? TextLiteral("禁用专属展示") : TextLiteral("启用专属展示"))) {
                        custom.isEnabled = !custom.isEnabled;
                        UIEditCoordinator::RequestConfigChange();
                    }
                    if (ImGui::MenuItem(TextLiteral("重命名"))) {
                        s_pendingRenameCustom = custom.customName;
                        strcpy_s(s_customRenameBuffer, sizeof(s_customRenameBuffer), custom.customName.c_str());
                    }
                    if (ImGui::MenuItem(TextLiteral("复制为新规则"))) pendingCloneCustom = custom.customName;
                    ImGui::Separator();
                    if (ImGui::MenuItem(TextLiteral("删除专属展示"))) s_pendingDeleteCustom = custom.customName;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TextLiteral("打开位置与姿态"))) requestCustomInspectorTab(custom.customName, 0);
                if (ImGui::MenuItem(TextLiteral("打开模型与外观"))) requestCustomInspectorTab(custom.customName, 1);
                if (ImGui::MenuItem(TextLiteral("打开可见性"))) requestCustomInspectorTab(custom.customName, 2);
                if (ImGui::MenuItem(TextLiteral("打开状态覆盖"))) requestCustomInspectorTab(custom.customName, 3);
                if (ImGui::MenuItem(TextLiteral("打开物理"))) requestCustomInspectorTab(custom.customName, 4);
                ImGui::EndPopup();
            };

            auto drawCustomRow = [&](CustomDefinition& custom, bool inherited) {
                ImGui::PushID(custom.customName.c_str());
                const bool selected = GetCustomEditorContext().selection.IsSelected(custom.customName);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (inherited) ImGui::BeginDisabled();
                if (ImGui::Checkbox("##customEnabled", &custom.isEnabled)) {
                    UIEditCoordinator::RequestConfigChange();
                }
                if (inherited) ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 5.0f);
                if (inherited) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                const std::string rowLabel = (inherited ? TextLiteral("[继承] ") : "") + custom.customName + "##customListRow";
                if (ImGui::Selectable(rowLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    GetCustomEditorContext().selection.Select(custom.customName);
                }
                drawCustomContextMenu(custom, inherited);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(TextLiteral("目标 FormID: %08X\n左键选择，右键打开快速操作"), custom.targetFormID);
                }
                if (inherited) ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(1);
                if (inherited) ImGui::TextDisabled("P:%d", custom.priority);
                else {
                    if (DrawPriorityInput("##customPriority", custom.priority)) {
                        UIEditCoordinator::RequestConfigChange();
                    }
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(custom.isEnabled ? ImVec4(0.2f, 1.0f, 0.45f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 1.0f), custom.isEnabled ? "●" : "○");
                ImGui::PopID();
            };

            std::vector<CustomDefinition*> localCustomOrder;
            localCustomOrder.reserve(a_customs.size());
            for (auto& custom : a_customs) localCustomOrder.push_back(&custom);
            std::sort(localCustomOrder.begin(), localCustomOrder.end(), [](const CustomDefinition* lhs, const CustomDefinition* rhs) {
                if (lhs->priority != rhs->priority) return lhs->priority > rhs->priority;
                return lhs->customName < rhs->customName;
            });

            std::vector<CustomDefinition*> inheritedCustomOrder;
            inheritedCustomOrder.reserve(inheritedMap.size());
            for (auto& [name, custom] : inheritedMap) inheritedCustomOrder.push_back(custom);
            std::sort(inheritedCustomOrder.begin(), inheritedCustomOrder.end(), [](const CustomDefinition* lhs, const CustomDefinition* rhs) {
                if (lhs->priority != rhs->priority) return lhs->priority > rhs->priority;
                return lhs->customName < rhs->customName;
            });

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
            if (ImGui::BeginTable("CustomListRows", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(TextLiteral("规则"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(TextLiteral("优先级"), ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn(TextLiteral("状态"), ImGuiTableColumnFlags_WidthFixed, 42.0f);
                ImGui::TableHeadersRow();
                if (!localCustomOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(TextLiteral("本级配置"));
                    ImGui::TableSetColumnIndex(1); ImGui::TextDisabled(TextLiteral("高 → 低"));
                    for (auto* custom : localCustomOrder) drawCustomRow(*custom, false);
                }
                if (!inheritedCustomOrder.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(TextLiteral("继承配置"));
                    ImGui::TableSetColumnIndex(1); ImGui::TextDisabled(TextLiteral("只读"));
                    for (auto* custom : inheritedCustomOrder) drawCustomRow(*custom, true);
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if (!s_pendingRenameCustom.empty()) ImGui::OpenPopup("RenameCustomFromList");
            if (ImGui::BeginPopupModal("RenameCustomFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static bool renameInvalid = false;
                ImGui::Text(TextLiteral("重命名专属展示"));
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputText("##customRenameFromList", s_customRenameBuffer, sizeof(s_customRenameBuffer))) renameInvalid = false;
                if (renameInvalid) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, TextLiteral("错误: 名称为空、重复或与继承规则冲突！"));
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    const std::string newName = s_customRenameBuffer;
                    auto renameIt = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                        return custom.customName == s_pendingRenameCustom;
                    });
                    const bool duplicate = std::any_of(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                        return custom.customName == newName && custom.customName != s_pendingRenameCustom;
                    }) || inheritedMap.contains(newName);
                    renameInvalid = newName.empty() || duplicate || renameIt == a_customs.end();
                    if (!renameInvalid) {
                        renameIt->customName = newName;
                        if (!GetCustomEditorContext().selection.Rename(s_pendingRenameCustom, newName)) {
                            GetCustomEditorContext().selection.Select(newName);
                        }
                        UIEditCoordinator::RequestConfigChange();
                        s_pendingRenameCustom.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingRenameCustom.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!s_pendingDeleteCustom.empty()) ImGui::OpenPopup("DeleteCustomFromList");
            if (ImGui::BeginPopupModal("DeleteCustomFromList", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(TextLiteral("确定删除专属展示 [%s] 吗？"), s_pendingDeleteCustom.c_str());
                if (ImGui::Button(TextLiteral("删除"), { 100.0f, 0.0f })) {
                    auto deleteIt = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                        return custom.customName == s_pendingDeleteCustom;
                    });
                    if (deleteIt != a_customs.end()) {
                        a_customs.erase(deleteIt);
                        GetCustomEditorContext().selection.ClearIfSelected(s_pendingDeleteCustom);
                        UIEditCoordinator::RequestConfigChange();
                    }
                    s_pendingDeleteCustom.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) {
                    s_pendingDeleteCustom.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (!pendingCloneCustom.empty()) {
                CustomDefinition* sourceCustom = nullptr;
                auto sourceIt = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) {
                    return custom.customName == pendingCloneCustom;
                });
                if (sourceIt != a_customs.end()) sourceCustom = &(*sourceIt);
                else {
                    auto inheritedIt = inheritedMap.find(pendingCloneCustom);
                    if (inheritedIt != inheritedMap.end()) sourceCustom = inheritedIt->second;
                }
                if (sourceCustom) {
                    const std::string base = sourceCustom->customName + "_Copy";
                    std::string candidate = base;
                    int suffix = 2;
                    auto nameUsed = [&](const std::string& name) {
                        return std::any_of(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& custom) { return custom.customName == name; }) || inheritedMap.contains(name);
                    };
                    while (nameUsed(candidate)) candidate = base + std::to_string(suffix++);
                    CustomDefinition clone = *sourceCustom;
                    clone.customName = candidate;
                    clone.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                    a_customs.push_back(clone);
                    GetCustomEditorContext().selection.Select(candidate);
                    UIEditCoordinator::RequestConfigChange();
                }
            }
            if (!pendingExtractCustom.empty()) {
                auto inheritedIt = inheritedMap.find(pendingExtractCustom);
                if (inheritedIt != inheritedMap.end()) {
                    a_customs.push_back(*inheritedIt->second);
                    GetCustomEditorContext().selection.Select(pendingExtractCustom);
                    UIEditCoordinator::RequestConfigChange();
                }
            }

            ImGui::EndChild();

            UIEditorInteraction::DrawPaneSplitter("##vsplitter_custom", config->uiLayout.customLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);

            ImGui::BeginChild("CustomRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& c) { return GetCustomEditorContext().selection.IsSelected(c.customName); });
            CustomDefinition* activeCustom = nullptr; bool isInherited = false;
            if (it != a_customs.end()) { activeCustom = &(*it); }
            else if (inheritedMap.count(GetCustomEditorContext().selection.Selected())) { activeCustom = inheritedMap[GetCustomEditorContext().selection.Selected()]; isInherited = true; }

            if (activeCustom) {
                auto& c = *activeCustom; ImGui::PushID(("Custom_" + c.customName).c_str());
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("💡 此配置继承自全局 (Global)，当前层级未进行覆写。"));
                    if (ImGui::Button(TextLiteral("➕ 提取到当前层级并覆写 (Add Override)"), { -1, 40 })) {
                        a_customs.push_back(c);
                        GetCustomEditorContext().selection.Select(c.customName);
                        UIEditCoordinator::RequestConfigChange();
                    }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }

                // The selected custom record scopes its data controls, while the
                // inspector navigation belongs to this window and must survive
                // switching to another record.
                ImGui::PopID();
                ImGui::PushID("CustomDetailStable");
                auto& customNavigation = GetCustomEditorContext().inspectorNavigation;
                const bool selectPersistedPrimary = UIInspectorNavigation::InitializeIndex(
                    customNavigation.primaryTabIndex,
                    customNavigation.primaryTabInitialized,
                    config->uiLayout.customPrimaryTab,
                    2);
                auto beginCustomPrimaryTab = [&](const char* label, int index) {
                    const auto flags = UIInspectorNavigation::ShouldSelect(
                        UIInspectorNavigation::Pending(customNavigation),
                        customNavigation.primaryTabIndex,
                        selectPersistedPrimary,
                        index) ?
                        ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                    const bool open = ImGui::BeginTabItem(label, nullptr, flags);
                    if (open && UIInspectorNavigation::ActivateIndex(customNavigation.primaryTabIndex, index)) {
                        config->uiLayout.customPrimaryTab = index;
                        UIEditCoordinator::RequestINISettingsSave();
                    }
                    return open;
                };
				if (ImGui::BeginTabBar("CustomDetailSections", ImGuiTabBarFlags_FittingPolicyScroll)) {
                if (beginCustomPrimaryTab(TextLiteral("目标与规则"), 0)) {
                ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, TextLiteral("锁定目标物品 (FormID):")); char formBuf[16]; sprintf_s(formBuf, "%08X", c.targetFormID); ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("##customForm", formBuf, 16, ImGuiInputTextFlags_CharsHexadecimal)) { try { c.targetFormID = std::stoul(formBuf, nullptr, 16); } catch (...) { c.targetFormID = 0; } } ImGui::SameLine();
                if (ImGui::Button(TextLiteral("捕获玩家当前装备的武器"))) {
                    auto player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        auto items = IAD::Scanner::GetActiveItems(player, false);
                        RE::TESForm* targetWeapon = nullptr;
                        for (auto& item : items) {
                            if (item.isEquipped && item.object && item.object->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
                                targetWeapon = item.object;
                                break;
                            }
                        }
                        if (targetWeapon) {
                            c.targetFormID = targetWeapon->GetFormID();
                            auto fullName = targetWeapon->As<RE::TESFullName>();
                            if (fullName && fullName->GetFullName() != nullptr) {
                                c.customName = std::string(TextLiteral("专属: ")) + fullName->GetFullName();
                            }
                            else {
                                c.customName = TextLiteral("专属: 未知武器");
                            }
                            GetCustomEditorContext().selection.Select(c.customName);
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                    }
                }
                ImGui::Separator();

                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("候选与高级逻辑"));
                if (ImGui::Checkbox(TextLiteral("使用运行时目标 FormID (IAD Variable Mode)"), &c.useRuntimeTargetForm)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (c.useRuntimeTargetForm) {
                    ImGui::Indent();
                    char runtimeFormVar[64];
                    strncpy_s(runtimeFormVar, c.runtimeTargetFormVariable.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(180.0f);
                    if (ImGui::InputText(TextLiteral("目标 FormID 变量"), runtimeFormVar, sizeof(runtimeFormVar))) {
                        c.runtimeTargetFormVariable = runtimeFormVar;
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##customRuntimeTargetFormPick", TextLiteral("选择"))) {
                        const auto variables = ConfigManager::GetSingleton()->GetRuntimeFormVariablesSnapshot();
                        for (const auto& [name, value] : variables) {
                            if (ImGui::Selectable(name.c_str(), c.runtimeTargetFormVariable == name)) {
                                c.runtimeTargetFormVariable = name;
                                UIEditCoordinator::RequestRuntimeRefresh();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled(TextLiteral("变量值非 0 时替代固定目标 FormID；无值时回退固定目标。"));
                    ImGui::Unindent();
                }
                ImGui::Separator();

                if (ImGui::Checkbox(TextLiteral("直接展示目标 Form 基础模型（无需在背包中）"), &c.displayFormWithoutInventory)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (c.displayFormWithoutInventory) {
                    ImGui::TextDisabled(TextLiteral("需要设置目标展示插槽；只读取 Form 的基础 NIF，不读取实例 OMOD。"));
                }

                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("🎒 高级展现逻辑 (IAD Logic)"));
                if (ImGui::Checkbox(TextLiteral("忽略玩家 (Ignore Player)"), &c.ignorePlayer)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (ImGui::Checkbox(TextLiteral("覆盖全局装备模式限制 (Override Equipment Mode)"), &c.overrideEquipmentMode)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (c.overrideEquipmentMode) { ImGui::Indent(); if (ImGui::Checkbox(TextLiteral("该专属规则仅显示已装备或收藏的武器"), &c.displayFavoritesOnly)) { UIEditCoordinator::RequestRuntimeRefresh(); } ImGui::Unindent(); }
                if (ImGui::Checkbox(TextLiteral("拔出武器时隐藏背部模型 (Always Unload)"), &c.alwaysUnload)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (ImGui::Checkbox(TextLiteral("模型组模式：仅显示模型组 (Group Mode)"), &c.groupMode)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (c.groupMode) {
                    ImGui::TextDisabled(TextLiteral("开启后不生成主物品模型和专属枪套，只显示下方模型组条目。"));
                    ImGui::TextDisabled(TextLiteral("模型组里的 FormID 模型来源会自动参与背包候选选择。"));
                }
                if (ImGui::Checkbox(TextLiteral("遗留模式 (Last Equipped Mode)"), &c.lastEquippedMode)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (c.lastEquippedMode) {
                    ImGui::Indent();
                    if (ImGui::Checkbox(TextLiteral("优先最近装备过的 Biped 槽"), &c.lastEquippedPrioritizeRecentBipedSlots)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("指定 Biped 槽已占用时禁用"), &c.lastEquippedDisableIfBipedSlotOccupied)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (!c.lastEquippedDisableIfBipedSlotOccupied) {
                        ImGui::Indent();
                        if (ImGui::Checkbox(TextLiteral("跳过已占用 Biped 槽"), &c.lastEquippedSkipOccupiedBipedSlots)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                        ImGui::Unindent();
                    }
                    if (UIFormEditorControls::DrawBipedSlotVectorEditor(TextLiteral("Last Equipped Biped 槽列表"), c.lastEquippedBipedSlots)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (ImGui::Checkbox(TextLiteral("优先回到最近展示插槽"), &c.lastEquippedPrioritizeRecentDisplaySlot)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("最近展示插槽已占用时跳过"), &c.lastEquippedSkipOccupiedDisplaySlots)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("指定展示插槽已占用时禁用"), &c.lastEquippedDisableIfDisplaySlotOccupied)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("回退到最近显示过的展示槽物品"), &c.lastEquippedFallbackToSlotted)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("允许回退到其他可用插槽"), &c.lastEquippedFallbackToAnySlot)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (ImGui::Checkbox(TextLiteral("最近装备无匹配时回退到最近获得"), &c.lastEquippedFallbackToRecentAcquired)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                    if (c.lastEquippedFallbackToRecentAcquired) {
                        ImGui::Indent();
                        if (ImGui::Checkbox(TextLiteral("按最近获得类型排序"), &c.lastEquippedPrioritizeRecentAcquiredTypes)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                        if (UIFormEditorControls::DrawFormTypeVectorEditor(TextLiteral("最近获得类型列表 (空=全局)"), c.lastEquippedRecentAcquiredFormTypes)) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::Unindent();
                    }
                    if (UIFormEditorControls::DrawStringVectorEditor(TextLiteral("Last Equipped 展示插槽列表"), c.lastEquippedDisplaySlots, "Backpack_Right")) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::TextDisabled(TextLiteral("列表为空时表示不限制展示插槽；填入 Slot 名称可限制 Last Equipped 回退范围。"));
                    if (ImGui::TreeNodeEx(TextLiteral("Last Equipped 专用过滤条件"), ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled(TextLiteral("仅 Last Equipped 模式额外评估；普通专属候选仍使用下方候选物品条件。"));
                        if (ImGui::Button(TextLiteral("清空 Last Equipped 过滤条件##customLastEquippedConditions"))) {
                            c.lastEquippedFilterConditionTree = IAD::ConditionNode(true, true);
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::PushID("CustomLastEquippedFilter");
                        if (DrawConditionTreeEditor(c.lastEquippedFilterConditionTree)) {
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        ImGui::PopID();
                        ImGui::TreePop();
                    }
                    ImGui::Unindent();
                }
                if (ImGui::Checkbox(TextLiteral("已装备时禁用 (Disable If Equipped)"), &c.disableIfEquipped)) { UIEditCoordinator::RequestRuntimeRefresh(); }
                if (ImGui::TreeNodeEx(TextLiteral("额外候选物品 (Extra Items)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled(TextLiteral("这些 FormID 会和锁定目标物品共用同一条专属规则。"));
                if (UIFormEditorControls::DrawFormVectorUI(c.extraItems, "CustomExtraItems")) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::TreePop();
                }
                if (c.lastEquippedMode) ImGui::BeginDisabled();
                if (ImGui::Checkbox(TextLiteral("随机选择候选物品 (Select Inventory Random)"), &c.selectInventoryRandom)) {
                    if (c.selectInventoryRandom) c.selectInventoryStrongest = false;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("选择最强候选物品 (Select Inventory Strongest)"), &c.selectInventoryStrongest)) {
                    if (c.selectInventoryStrongest) c.selectInventoryRandom = false;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (c.lastEquippedMode) {
                    ImGui::EndDisabled();
                    ImGui::TextDisabled(TextLiteral("Last Equipped 模式固定使用最近装备/展示历史，不使用随机或最强候选排序。"));
                }
                if (ImGui::TreeNodeEx(TextLiteral("候选物品条件 (Candidate Item Conditions)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled(TextLiteral("这些条件会在专属规则选择背包候选物品时评估，也会作用于 Last Equipped 模式。"));
                    if (ImGui::Button(TextLiteral("清空候选条件##customCandidateConditions"))) {
                        c.inventoryConditionTree = IAD::ConditionNode(true, true);
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    if (DrawConditionTreeEditor(c.inventoryConditionTree)) {
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    ImGui::TreePop();
                }
                ImGui::Spacing(); ImGui::SetNextItemWidth(200.0f); ImGui::SliderFloat(TextLiteral("生成概率 (Spawn Chance)%"), &c.spawnChance, 0.0f, 100.0f, "%.1f%%");
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, TextLiteral("🔢 数量感知系统 (Count Range)"));
                ImGui::SetNextItemWidth(100.0f); ImGui::InputInt(TextLiteral("最少需要 (Min)"), &c.countMin); ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f); ImGui::InputInt(TextLiteral("最多显示 (Max)"), &c.countMax);
                ImGui::EndTabItem();
                }

                if (beginCustomPrimaryTab(TextLiteral("模型来源与显示"), 1)) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, TextLiteral("🎭 模型强制替换 (Model Swap)"));
                char modelPath[256]; strcpy_s(modelPath, c.modelSwapPath.c_str()); ImGui::SetNextItemWidth(400.0f);
                ImGui::InputText(TextLiteral("模型路径"), modelPath, 256);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    c.modelSwapPath = modelPath;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIFormEditorControls::DrawFormIDField(TextLiteral("模型替换 FormID"), c.modelSwapFormID)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIModelEditorControls::DrawModelSwapVariableSource(c.modelSwapVariableSource, "custom_model_var_source")) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::TextDisabled(TextLiteral("路径优先于 FormID；FormID 用指定物品的模型替换当前 Custom 主模型。"));
                ImGui::Separator();

                ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, TextLiteral("特殊物品渲染设置"));
                if (ImGui::Checkbox(TextLiteral("提取武器弹匣 (Extract Magazine)##Custom"), &c.extractMagazine)) {
                    if (c.extractMagazine) c.useProjectileForAmmo = false;
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                bool disableCustomAmmo = c.extractMagazine; if (disableCustomAmmo) ImGui::BeginDisabled();
                if (ImGui::Checkbox(TextLiteral("【仅限弹药】提取射弹(单发子弹)模型，代替默认弹药盒"), &c.useProjectileForAmmo)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (disableCustomAmmo) ImGui::EndDisabled();
                if (ImGui::Checkbox(TextLiteral("使用世界/基础模型 (Use World Model)##Custom"), &c.useWorldModel)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("隐藏几何 / alpha 0 (Invisible)##Custom"), &c.invisible)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("仅隐藏几何，保留挂载节点 (Hide Geometry)##Custom"), &c.hideGeometry)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("隐藏附加灯光 (Hide Light)##Custom"), &c.hideLight)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("加载第一人称武器模型 (Load 1P Weapon Model)##Custom"), &c.load1pWeaponModel)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("保留火焰/喷焰 FX (Keep Torch Flame)##Custom"), &c.keepTorchFlame)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (ImGui::Checkbox(TextLiteral("移除刀鞘/枪套节点 (Remove Scabbard)##Custom"), &c.removeScabbard)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIModelEditorControls::DrawModelCleanupSettings(c.disableHavok, c.removeEditorMarker, c.removeProjectileTracers, "custom_cleanup")) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIModelEditorControls::DrawModelAnimationSettings(c.animation, "custom_animation")) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIModelEditorControls::DrawModelEffectShaderSettings(c.effectShader, "custom_effect")) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                if (UIModelEditorControls::DrawModelLightSettings(c.light, "custom_light")) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::EndTabItem();
                }

                if (beginCustomPrimaryTab(TextLiteral("绑定与挂载"), 2)) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("强制修改挂载节点 (Target Node Overwrite):")); ImGui::SameLine();
                if (DrawCMENodeSelector("##custTargetNode", c.targetNode, a_nodes, GetCustomEditorContext().scope, TextLiteral("[不覆盖] 跟随该物品的 Slot 默认节点"))) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::Separator();

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, TextLiteral("绑定展示插槽 (Display Slot):")); ImGui::SameLine();
                const char* selectedSlotLabel = c.targetDisplaySlot.empty() ? TextLiteral("[自动] 匹配任意合格 Slot") : c.targetDisplaySlot.c_str();
                if (ImGui::BeginCombo("##custTargetDisplaySlot", selectedSlotLabel)) {
                    if (ImGui::Selectable(TextLiteral("[自动] 匹配任意合格 Slot"), c.targetDisplaySlot.empty())) {
                        c.targetDisplaySlot.clear();
                        UIEditCoordinator::RequestRuntimeRefresh();
                    }
                    for (const auto& slot : a_slots) {
                        const bool selected = c.targetDisplaySlot == slot.slotName;
                        if (ImGui::Selectable(slot.slotName.c_str(), selected)) {
                            c.targetDisplaySlot = slot.slotName;
                            UIEditCoordinator::RequestRuntimeRefresh();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                 }
                 ImGui::EndTabItem();
                 }
                 ImGui::EndTabBar();
                }
                ImGui::Separator();

                if (DrawCustomEditorTabs(c, GetCustomEditorContext(), a_nodes, "liveCustomTabs", true)) {
                    UIEditCoordinator::RequestRuntimeRefresh();
                }
                ImGui::PopID();
                if (isInherited) ImGui::EndDisabled();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled(TextLiteral("请在左侧选择一个专属物品规则")); }
            ImGui::EndChild();
            UIEditorInteraction::TrackLiveConfigEdits(GetCustomEditorContext().pendingConfigSave);
        }
        ImGui::End();
    }
    // 👇========== 🌟 核心面板类实现：全局过滤器 ==========👇
    void UIProfileSlotsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileSlots) return;

        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_slots"), &config->uiShowProfileSlots, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(9);
            DrawManagedProfileEditor(
                "StandaloneSlotProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Slots(),
                "NewSlotProfile",
                DrawSlotProfileRecord,
                &config->uiShowProfileSlots);
        }
        ImGui::End();
    }

    void UIProfileCustomsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileCustoms) return;

        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_customs"), &config->uiShowProfileCustoms, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(10);
            DrawManagedProfileEditor(
                "StandaloneCustomProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Customs(),
                "NewCustomProfile",
                DrawCustomProfileRecord,
                &config->uiShowProfileCustoms);
        }
        ImGui::End();
    }

    void UIProfileNodesWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileNodes) return;

        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_nodes"), &config->uiShowProfileNodes, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(11);
            DrawManagedProfileEditor(
                "StandaloneNodeProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Nodes(),
                "NewNodeProfile",
                DrawNodeProfileRecord,
                &config->uiShowProfileNodes);
        }
        ImGui::End();
    }

    void UIProfileFormFiltersWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileFormFilters) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_filters"), &config->uiShowProfileFormFilters, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(12);
            DrawManagedProfileEditor(
                "StandaloneFormFilterProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().FormFilters(),
                "NewFormFilterProfile",
                DrawFormFilterProfileRecord,
                &config->uiShowProfileFormFilters);
        }
        ImGui::End();
    }

    void UIProfileModelGroupsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileModelGroups) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_model_groups"), &config->uiShowProfileModelGroups, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(13);
            DrawManagedProfileEditor(
                "StandaloneModelGroupProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().ModelGroups(),
                "NewModelGroupProfile",
                DrawModelGroupProfileRecord,
                &config->uiShowProfileModelGroups);
        }
        ImGui::End();
    }

    void UIProfileNodeMonitorsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileNodeMonitors) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_node_monitors"), &config->uiShowProfileNodeMonitors, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(14);
            DrawManagedProfileEditor(
                "StandaloneNodeMonitorProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().NodeMonitors(),
                "NewNodeMonitorProfile",
                DrawNodeMonitorProfileRecord,
                &config->uiShowProfileNodeMonitors);
        }
        ImGui::End();
    }

    void UIProfileConditionsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileConditions) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_conditions"), &config->uiShowProfileConditions, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(15);
            DrawManagedProfileEditor(
                "StandaloneConditionProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Conditions(),
                "NewConditionProfile",
                DrawConditionProfileRecord,
                &config->uiShowProfileConditions);
        }
        ImGui::End();
    }

    void UIProfileTransformsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileTransforms) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_transforms"), &config->uiShowProfileTransforms, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(16);
            DrawManagedProfileEditor(
                "StandaloneTransformProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(),
                "NewTransformProfile",
                DrawTransformProfileRecord,
                &config->uiShowProfileTransforms);
        }
        ImGui::End();
    }

    void UIProfilePhysicsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfilePhysics) return;

        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profile_physics"), &config->uiShowProfilePhysics, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(17);
            DrawManagedProfileEditor(
                "StandalonePhysicsProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Physics(),
                "NewPhysicsProfile",
                DrawPhysicsProfileRecord,
                &config->uiShowProfilePhysics);
        }
        ImGui::End();
    }

    void UIProfileEditorWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfiles) return;

        static char snapshotName[64] = "NewPreset";
        static std::string selectedSnapshot;
        static uint32_t snapshotFlags = static_cast<uint32_t>(ConfigManager::SerFlags::kAll);
        static int snapshotImportMode = 0;
        static std::string snapshotStatusText;

        static char profileName[64] = "NewProfile";
        static std::string selectedProfile;
        static int profileCategory = 0;
        static std::string statusText;
        static char contentProfileName[64] = "NewContentProfile";
        static std::string selectedContentProfile;
        static int contentCategory = 0;
        static int contentApplyTarget = 0;
        static std::string contentStatusText;
        static ConditionNode contentCondition(true, true);
        static TransformData contentTransform;
        static PhysicsValues contentPhysics;
        static FormFilter contentFormFilter;

        struct ProfileCategoryDesc {
            const char* label;
            const char* folder;
        };

		static ProfileCategoryDesc categories[] = {
			{ TextLiteral("装备插槽集合"), "Slot" },
			{ TextLiteral("挂载节点覆写集合"), "NodeOverrides" },
			{ TextLiteral("专属展示集合"), "Custom" },
			{ TextLiteral("当前专属展示的模型组"), "ModelGroups" },
			{ TextLiteral("节点监控列表"), "NodeMonitors" },
			{ TextLiteral("自动条件变量"), "ConditionalVariables" }
		};

        static ProfileCategoryDesc contentCategories[] = {
            { TextLiteral("条件树"), "Conditions" },
            { TextLiteral("变换"), "Transforms" },
            { TextLiteral("物理"), "Physics" },
            { TextLiteral("表单过滤器"), "FormFilters" }
        };

        ImGui::SetNextWindowSize({ 560.0f, 480.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.profiles"), &config->uiShowProfiles, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(8);
            if (ImGui::BeginTabBar("IADProfileEditorTabs")) {
                const bool selectModuleTab = config->uiProfileRequestedTab == 0;
                if (ImGui::BeginTabItem(TextLiteral("模块预设库"), nullptr, selectModuleTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (selectModuleTab) config->uiProfileRequestedTab = -1;
                    auto& profileManagers = IAD::Profile::GlobalProfileManager::GetSingleton();
                    if (!profileManagers.IsLoaded()) {
                        profileManagers.LoadAll();
                    }

                    static const char* managedCategories[] = {
                        TextLiteral("装备插槽"),
                        TextLiteral("挂载节点"),
                        TextLiteral("专属展示"),
                        TextLiteral("模型组"),
                        TextLiteral("节点监控"),
                        TextLiteral("条件"),
                        TextLiteral("变换"),
                        TextLiteral("物理"),
                        TextLiteral("表单过滤器")
                    };

                    const int managedCategoryCount = static_cast<int>(sizeof(managedCategories) / sizeof(managedCategories[0]));
                    config->uiProfileManagedCategory = std::clamp(config->uiProfileManagedCategory, 0, managedCategoryCount - 1);
                    ImGui::SetNextItemWidth(210.0f);
                    ImGui::Combo(TextLiteral("预设类型"), &config->uiProfileManagedCategory, managedCategories, managedCategoryCount);
                    ImGui::Separator();

                    switch (config->uiProfileManagedCategory) {
                    case 0:
                        DrawManagedProfileEditor("ManagedSlots", profileManagers.Slots(), "NewProfile", DrawSlotProfileRecord);
                        break;
                    case 1:
                        DrawManagedProfileEditor("ManagedNodes", profileManagers.Nodes(), "NewProfile", DrawNodeProfileRecord);
                        break;
                    case 2:
                        DrawManagedProfileEditor("ManagedCustoms", profileManagers.Customs(), "NewProfile", DrawCustomProfileRecord);
                        break;
                    case 3:
                        DrawManagedProfileEditor("ManagedModelGroups", profileManagers.ModelGroups(), "NewProfile", DrawModelGroupProfileRecord);
                        break;
                    case 4:
                        DrawManagedProfileEditor("ManagedNodeMonitors", profileManagers.NodeMonitors(), "NewProfile", DrawNodeMonitorProfileRecord);
                        break;
                    case 5:
                        DrawManagedProfileEditor("ManagedConditions", profileManagers.Conditions(), "NewProfile", DrawConditionProfileRecord);
                        break;
                    case 6:
                        DrawManagedProfileEditor("ManagedTransforms", profileManagers.Transforms(), "NewProfile", DrawTransformProfileRecord);
                        break;
                    case 7:
                        DrawManagedProfileEditor("ManagedPhysics", profileManagers.Physics(), "NewProfile", DrawPhysicsProfileRecord);
                        break;
                    case 8:
                        DrawManagedProfileEditor("ManagedFormFilters", profileManagers.FormFilters(), "NewProfile", DrawFormFilterProfileRecord);
                        break;
                    default:
                        break;
                    }

                    ImGui::EndTabItem();
                }

                const bool selectSnapshotTab = config->uiProfileRequestedTab == 1;
                if (ImGui::BeginTabItem(TextLiteral("全局快照"), nullptr, selectSnapshotTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (selectSnapshotTab) config->uiProfileRequestedTab = -1;
                    auto exports = config->GetAvailableExports();
                    ImGui::SetNextItemWidth(260.0f);
                    DrawProfileCombo("Snapshot##IADProfileSnapshot", exports, selectedSnapshot);
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新##IADProfileSnapshot"))) {
                        selectedSnapshot.clear();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText(TextLiteral("名称##IADProfileSnapshotName"), snapshotName, sizeof(snapshotName));
                    DrawProfileFlagMatrix(snapshotFlags);

                    ImGui::RadioButton(TextLiteral("覆盖导入"), &snapshotImportMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton(TextLiteral("合并导入"), &snapshotImportMode, 1);
                    ImGui::Separator();

                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("导入所选"), { 130.0f, 0.0f })) {
                        config->ImportPreset(selectedSnapshot, snapshotFlags, snapshotImportMode == 1);
                        UIEditCoordinator::RequestRuntimeRefresh();
                        snapshotStatusText = TextLiteral("已导入所选快照。");
                    }
                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (std::strlen(snapshotName) == 0 || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("按名称导出"), { 130.0f, 0.0f })) {
                        config->ExportPreset(snapshotName, snapshotFlags);
                        selectedSnapshot = snapshotName;
                        snapshotStatusText = TextLiteral("已导出快照。");
                    }
                    if (std::strlen(snapshotName) == 0 || snapshotFlags == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("覆盖所选"), { 140.0f, 0.0f })) {
                        config->ExportPreset(selectedSnapshot, snapshotFlags);
                        snapshotStatusText = TextLiteral("已覆盖所选快照。");
                    }
                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::EndDisabled();

                    DrawProfileFileActions("SnapshotFileActions", config, nullptr, selectedSnapshot, snapshotName, sizeof(snapshotName), snapshotStatusText);
                    if (!snapshotStatusText.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", snapshotStatusText.c_str());
                    }

                    ImGui::EndTabItem();
                }

                // Legacy direct-import workflow. Module profiles now provide the
                // same operations from the corresponding live editor.
                static bool showLegacyDirectImport = false;
                if (showLegacyDirectImport && ImGui::BeginTabItem(TextLiteral("当前配置"))) {
                    const int categoryCount = static_cast<int>(sizeof(categories) / sizeof(categories[0]));
                    if (ImGui::BeginCombo(TextLiteral("预设类型##IADProfileCategory"), categories[profileCategory].label)) {
                        for (int i = 0; i < categoryCount; ++i) {
                            bool selected = profileCategory == i;
                            if (ImGui::Selectable(categories[i].label, selected)) {
                                profileCategory = i;
                                selectedProfile.clear();
                                statusText.clear();
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (profileCategory == 0) {
                        DrawWindowHeader("ProfileSlotScope", GetSlotEditorContext(), true, false);
                    }
                    else if (profileCategory == 1) {
                        DrawWindowHeader("ProfileNodeScope", GetNodeEditorContext(), true, false);
                    }
                    else if (profileCategory == 2 || profileCategory == 3) {
                        DrawWindowHeader("ProfileCustomScope", GetCustomEditorContext(), true, false);
                    }

                    auto profiles = config->GetAvailableProfiles(categories[profileCategory].folder);
                    ImGui::SetNextItemWidth(260.0f);
                    DrawProfileCombo("Profile##IADModuleProfile", profiles, selectedProfile);
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新##IADModuleProfile"))) {
                        selectedProfile.clear();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("Name##IADModuleProfileName", profileName, sizeof(profileName));
                    ImGui::Separator();

                    auto saveProfile = [&]() {
                        switch (profileCategory) {
                        case 0:
                            config->SaveSlotProfile(profileName, config->GetSlots(GetSlotEditorContext().scope, GetQueryID(GetSlotEditorContext())));
                            statusText = "Saved slot profile.";
                            break;
                        case 1:
                            config->SaveNodeProfile(profileName, config->GetNodes(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext())));
                            statusText = "Saved node override profile.";
                            break;
                        case 2:
                            config->SaveCustomProfile(profileName, config->GetCustoms(GetCustomEditorContext().scope, GetQueryID(GetCustomEditorContext())));
                            statusText = "Saved custom display profile.";
                            break;
                        case 3:
                            if (auto* custom = GetSelectedCustomForProfile(config)) {
                                config->SaveModelGroupProfile(profileName, custom->modelGroups);
                                statusText = "Saved model group profile.";
                            }
                            else {
                                statusText = "No custom display is available in the current scope.";
                            }
                            break;
						case 4:
							config->SaveNodeMonitorProfile(profileName, config->GetNodeMonitorNamesSnapshot());
							statusText = "Saved node monitor profile.";
							break;
						case 5:
							config->SaveConditionalVariableProfile(profileName, config->GetConditionalVariablesSnapshot());
							statusText = "Saved conditional variable profile.";
							break;
                        default:
                            break;
                        }
                        };

                    auto loadProfile = [&](bool overwrite) {
                        bool loaded = false;
                        switch (profileCategory) {
                        case 0:
                            loaded = config->LoadSlotProfile(selectedProfile, config->GetSlots(GetSlotEditorContext().scope, GetQueryID(GetSlotEditorContext())), overwrite);
                            break;
                        case 1:
                            loaded = config->LoadNodeProfile(selectedProfile, config->GetNodes(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext())), overwrite);
                            break;
                        case 2:
                            loaded = config->LoadCustomProfile(selectedProfile, config->GetCustoms(GetCustomEditorContext().scope, GetQueryID(GetCustomEditorContext())), overwrite);
                            break;
                        case 3:
                            if (auto* custom = GetSelectedCustomForProfile(config)) {
                                loaded = config->LoadModelGroupProfile(selectedProfile, custom->modelGroups, overwrite);
                            }
                            break;
						case 4:
							loaded = config->LoadNodeMonitorProfile(selectedProfile, config->nodeMonitorNames, overwrite);
							break;
						case 5: {
							auto variables = config->GetConditionalVariablesSnapshot();
							loaded = config->LoadConditionalVariableProfile(selectedProfile, variables, overwrite);
							if (loaded) config->SetConditionalVariables(std::move(variables));
							break;
						}
                        default:
                            break;
                        }

                        if (loaded) {
                            UIEditCoordinator::RequestConfigChange();
                            statusText = overwrite ? "Loaded profile with overwrite." : "Loaded profile with merge.";
                        }
                        else {
                            statusText = "Profile load failed.";
                        }
                        };

                    if (std::strlen(profileName) == 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("保存当前配置"), { 120.0f, 0.0f })) {
                        saveProfile();
                        selectedProfile = profileName;
                    }
                    if (std::strlen(profileName) == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (selectedProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("合并加载"), { 120.0f, 0.0f })) {
                        loadProfile(false);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("覆盖加载"), { 130.0f, 0.0f })) {
                        loadProfile(true);
                    }
                    if (selectedProfile.empty()) ImGui::EndDisabled();

                    DrawProfileFileActions("ModuleProfileFileActions", config, categories[profileCategory].folder, selectedProfile, profileName, sizeof(profileName), statusText);

                    if (!statusText.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", statusText.c_str());
                    }

                    ImGui::EndTabItem();
                }

                // Retained for file-format compatibility while the new managed
                // profile categories own the visible editing workflow.
                static bool showLegacyContentProfiles = false;
                if (showLegacyContentProfiles && ImGui::BeginTabItem(TextLiteral("内容预设"))) {
                    const int contentCategoryCount = static_cast<int>(sizeof(contentCategories) / sizeof(contentCategories[0]));
                    if (ImGui::BeginCombo(TextLiteral("内容类型##IADContentProfileCategory"), contentCategories[contentCategory].label)) {
                        for (int i = 0; i < contentCategoryCount; ++i) {
                            bool selected = contentCategory == i;
                            if (ImGui::Selectable(contentCategories[i].label, selected)) {
                                contentCategory = i;
                                selectedContentProfile.clear();
                                contentStatusText.clear();
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    auto contentProfiles = config->GetAvailableProfiles(contentCategories[contentCategory].folder);
                    ImGui::SetNextItemWidth(260.0f);
                    DrawProfileCombo("Profile##IADContentProfile", contentProfiles, selectedContentProfile);
                    ImGui::SameLine();
                    if (ImGui::Button(TextLiteral("刷新##IADContentProfile"))) {
                        selectedContentProfile.clear();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("Name##IADContentProfileName", contentProfileName, sizeof(contentProfileName));
                    ImGui::Separator();

                    auto resetContent = [&]() {
                        switch (contentCategory) {
                        case 0:
                            contentCondition = ConditionNode(true, true);
                            break;
                        case 1:
                            contentTransform = TransformData{};
                            break;
                        case 2:
                            contentPhysics = PhysicsValues{};
                            break;
                        case 3:
                            contentFormFilter = FormFilter{};
                            break;
                        default:
                            break;
                        }
                        contentStatusText = "Reset editor buffer.";
                        };

                    auto loadContent = [&]() {
                        bool loaded = false;
                        switch (contentCategory) {
                        case 0:
                            loaded = config->LoadConditionProfile(selectedContentProfile, contentCondition);
                            break;
                        case 1:
                            loaded = config->LoadTransformProfile(selectedContentProfile, contentTransform);
                            break;
                        case 2:
                            loaded = config->LoadPhysicsProfile(selectedContentProfile, contentPhysics);
                            break;
                        case 3:
                            loaded = config->LoadFormFilterProfile(selectedContentProfile, contentFormFilter);
                            break;
                        default:
                            break;
                        }
                        contentStatusText = loaded ? "Loaded profile into editor." : "Profile load failed.";
                        };

                    auto saveContent = [&](const std::string& name) {
                        switch (contentCategory) {
                        case 0:
                            config->SaveConditionProfile(name, contentCondition);
                            break;
                        case 1:
                            config->SaveTransformProfile(name, contentTransform);
                            break;
                        case 2:
                            config->SavePhysicsProfile(name, contentPhysics);
                            break;
                        case 3:
                            config->SaveFormFilterProfile(name, contentFormFilter);
                            break;
                        default:
                            break;
                        }
                        selectedContentProfile = name;
                        contentStatusText = "Saved profile content.";
                        };

                    if (ImGui::Button(TextLiteral("新建空白内容"), { 100.0f, 0.0f })) {
                        resetContent();
                    }
                    ImGui::SameLine();
                    if (selectedContentProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("加载选中内容"), { 120.0f, 0.0f })) {
                        loadContent();
                    }
                    if (selectedContentProfile.empty()) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (std::strlen(contentProfileName) == 0) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("按名称保存"), { 120.0f, 0.0f })) {
                        saveContent(contentProfileName);
                    }
                    if (std::strlen(contentProfileName) == 0) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (selectedContentProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button(TextLiteral("覆盖选中内容"), { 140.0f, 0.0f })) {
                        saveContent(selectedContentProfile);
                    }
                    if (selectedContentProfile.empty()) ImGui::EndDisabled();

                    DrawProfileFileActions("ContentProfileFileActions", config, contentCategories[contentCategory].folder, selectedContentProfile, contentProfileName, sizeof(contentProfileName), contentStatusText);

                    ImGui::Separator();
                    static const char* applyTargets[] = { TextLiteral("当前选中插槽"), TextLiteral("当前选中节点"), TextLiteral("当前选中专属展示") };
                    if (contentCategory == 3) {
                        ImGui::TextDisabled(TextLiteral("应用目标：当前选中插槽的物品过滤器。"));
                    }
                    else {
                        ImGui::SetNextItemWidth(180.0f);
                        ImGui::Combo(TextLiteral("应用目标##IADContentApplyTarget"), &contentApplyTarget, applyTargets, 3);
                    }

                    auto applyContentToTarget = [&]() {
                        bool applied = false;

                        if (contentCategory == 3) {
                            if (auto* slot = GetSelectedSlotForProfile(config)) {
                                slot->itemFilter = contentFormFilter;
                                applied = true;
                            }
                        }
                        else if (contentApplyTarget == 0) {
                            if (auto* slot = GetSelectedSlotForProfile(config)) {
                                if (contentCategory == 0) {
                                    slot->displayConditionTree = contentCondition;
                                }
                                else if (contentCategory == 1) {
                                    ApplyTransformToConfigBase(*slot, contentTransform, GetSlotEditorContext());
                                }
                                else if (contentCategory == 2) {
                                    slot->physics = contentPhysics;
                                }
                                applied = true;
                            }
                        }
                        else if (contentApplyTarget == 1) {
                            if (auto* node = GetSelectedNodeForProfile(config)) {
                                if (contentCategory == 0) {
                                    node->displayConditionTree = contentCondition;
                                }
                                else if (contentCategory == 1) {
                                    ApplyTransformToConfigBase(*node, contentTransform, GetNodeEditorContext());
                                }
                                else if (contentCategory == 2) {
                                    node->physics = contentPhysics;
                                }
                                applied = true;
                            }
                        }
                        else if (contentApplyTarget == 2) {
                            if (auto* custom = GetSelectedCustomForProfile(config)) {
                                if (contentCategory == 0) {
                                    custom->displayConditionTree = contentCondition;
                                }
                                else if (contentCategory == 1) {
                                    ApplyTransformToConfigBase(*custom, contentTransform, GetCustomEditorContext());
                                }
                                else if (contentCategory == 2) {
                                    custom->physics = contentPhysics;
                                }
                                applied = true;
                            }
                        }

                        if (applied) {
                            UIEditCoordinator::RequestConfigChange();
                            contentStatusText = "Applied profile content to active config.";
                        }
                        else {
                            contentStatusText = "No target is available in the current scope.";
                        }
                        };

                    if (ImGui::Button(TextLiteral("应用编辑器内容"), { 150.0f, 0.0f })) {
                        applyContentToTarget();
                    }

                    if (!contentStatusText.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", contentStatusText.c_str());
                    }

                    ImGui::Separator();
                    ImGui::BeginChild("IADContentProfileEditorBody", ImVec2(0.0f, 0.0f), true);
                    switch (contentCategory) {
                    case 0:
                        DrawConditionTreeEditor(contentCondition, false);
                        break;
                    case 1:
                        UITransformEditorControls::DrawTransformWidget("Position", contentTransform.pos, { 0,0,0 }, 0.5f, false);
                        UITransformEditorControls::DrawTransformWidget("Rotation", contentTransform.rot, { 0,0,0 }, 1.0f, true);
                        UITransformEditorControls::DrawTransformWidget("Pivot", contentTransform.pivot, { 0,0,0 }, 0.5f, false);
                        ImGui::DragFloat("Scale", &contentTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f");
                        break;
                    case 2:
                        DrawPhysicsPanel(contentPhysics);
                        break;
                    case 3:
                        DrawFormFilterUI(contentFormFilter, "ProfileEditorFormFilter");
                        break;
                    default:
                        break;
                    }
                    ImGui::EndChild();

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(TextLiteral("预设文件库"))) {
                    static constexpr const char* folders[] = {
                        "Slot",
                        "NodeOverrides",
                        "Custom",
                        "ModelGroups",
                        "NodeMonitors",
						"ConditionalVariables",
                        "Conditions",
                        "Transforms",
                        "Physics",
                        "FormFilters"
                    };

                    for (const auto* folder : folders) {
                        auto profiles = config->GetAvailableProfiles(folder);
                        if (ImGui::TreeNode(folder, "%s (%zu)", folder, profiles.size())) {
                            for (const auto& profile : profiles) {
                                ImGui::BulletText("%s", profile.c_str());
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void UIFiltersWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowFilters) return;
        auto& managedProfiles = IAD::Profile::GlobalProfileManager::GetSingleton().FormFilters();
        if (!managedProfiles.IsInitialized()) {
            managedProfiles.Load();
        }
        auto reloadManagedProfile = [&]() {
            if (!m_selectedProf.empty()) {
                UIProfileWorkflow::ReloadFormFilter(managedProfiles, m_selectedProf, m_filterData);
            }
            UIEditCoordinator::RequestRuntimeRefresh();
        };
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.filters"), &config->uiShowFilters, ImGuiWindowFlags_MenuBar)) {
            TrackTopLevelWindowFocus(4);
            bool openAddFilterPopup = false;
            bool openDeleteFilterPopup = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    const bool hasSelection = !m_selectedProf.empty();
                    if (!hasSelection) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Save filter")) {
                        UIProfileWorkflow::SaveFormFilter(managedProfiles, m_selectedProf, m_filterData);
                        reloadManagedProfile();
                    }
                    if (ImGui::MenuItem("Reload selected")) {
                        UIProfileWorkflow::ReloadFormFilter(managedProfiles, m_selectedProf, m_filterData);
                        reloadManagedProfile();
                    }
                    if (!hasSelection) ImGui::EndDisabled();
                    if (ImGui::MenuItem("Close")) config->uiShowFilters = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Actions")) {
                    if (ImGui::MenuItem("New form filter")) openAddFilterPopup = true;
                    const bool hasSelection = !m_selectedProf.empty();
                    if (!hasSelection) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Delete selected")) openDeleteFilterPopup = true;
                    if (!hasSelection) ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::MenuItem("Open form filter profile editor", nullptr, &config->uiShowProfileFormFilters);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("全局黑白名单预设库 (Form Filter Profiles)"));
            ImGui::TextDisabled(TextLiteral("你在这里建立的各种黑白名单词典，可以在【装备插槽】面板中被无限次一键导入。")); ImGui::Separator(); ImGui::Spacing();

            UIEditorInteraction::ClampPaneWidth(config->uiLayout.filterLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);
            ImGui::BeginChild("FiltLeft", ImVec2(config->uiLayout.filterLeftPaneWidth, 0.0f), true);
            if (openAddFilterPopup) ImGui::OpenPopup("AddFilterPopup");
            if (ImGui::Button(TextLiteral("新建过滤器预设"), ImVec2(-1.0f, 35.0f))) ImGui::OpenPopup("AddFilterPopup");
            if (ImGui::BeginPopupModal("AddFilterPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newFiltName[64] = "My_Blacklist"; static bool isDuplicate = false;
                ImGui::Text(TextLiteral("输入该过滤器的唯一标识名:")); if (ImGui::InputText("##nfn", newFiltName, 64)) isDuplicate = false;
                if (ImGui::Button(TextLiteral("确认"), { 100.0f, 0.0f })) {
                    if (strlen(newFiltName) > 0) {
                        const std::string newName = newFiltName;
                        if (managedProfiles.CreateProfile(newName, IAD::FormFilter{})) {
                            m_selectedProf = newName;
                            m_filterData = IAD::FormFilter{};
                            reloadManagedProfile();
                            ImGui::CloseCurrentPopup();
                        }
                        else {
                            isDuplicate = true;
                        }
                    }
                }
                if (isDuplicate) {
                    ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, TextLiteral("名称已存在或无效。"));
                }
                ImGui::SameLine(); if (ImGui::Button(TextLiteral("取消"), { 100.0f, 0.0f })) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Separator();

            auto profiles = config->GetAvailableProfiles("FormFilters");
            for (const auto& p : profiles) {
                if (ImGui::Selectable((p + "###" + p).c_str(), m_selectedProf == p)) {
                    m_selectedProf = p;
                    UIProfileWorkflow::ReloadFormFilter(managedProfiles, p, m_filterData);
                }
            }
            ImGui::EndChild();

            UIEditorInteraction::DrawPaneSplitter("##vsplitter_filt", config->uiLayout.filterLeftPaneWidth, 120.0f, ImGui::GetWindowWidth() - 150.0f);

            ImGui::BeginChild("FiltRight", ImVec2(0.0f, 0.0f), true);
            if (!m_selectedProf.empty()) {
                ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, TextLiteral("当前正在编辑: %s"), m_selectedProf.c_str()); ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
                if (ImGui::Button(TextLiteral("💾 保存词典"))) {
                    UIProfileWorkflow::SaveFormFilter(managedProfiles, m_selectedProf, m_filterData);
                    reloadManagedProfile();
                }
                ImGui::Separator();

                DrawFormFilterUI(m_filterData, "GlobalFiltEdit");

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (openDeleteFilterPopup) ImGui::OpenPopup("DelFiltConfirm");
                if (ImGui::Button(TextLiteral("删除此词典 (Delete)"), { 150, 30 })) ImGui::OpenPopup("DelFiltConfirm");
                if (ImGui::BeginPopupModal("DelFiltConfirm", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text(TextLiteral("确定要彻底删除预设 [%s] 吗？"), m_selectedProf.c_str());
                    if (ImGui::Button(TextLiteral("删除"), { 100,0 })) {
                        if (!managedProfiles.IsInitialized()) {
                            managedProfiles.Load();
                        }
                        if (UIProfileWorkflow::DeleteFormFilter(managedProfiles, m_selectedProf)) {
                            m_selectedProf = "";
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine(); if (ImGui::Button(TextLiteral("取消"), { 100,0 })) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            else {
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled(TextLiteral("请在左侧选择或新建一个过滤器词典"));
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // 👇========== 🌟 辅助：骨骼扫描仪导出过滤逻辑 ==========👇
    struct ExportFilters {
        bool excludeIAD = true;
        bool excludePhysics = true;
        bool excludeFaceGen = true;
        bool excludeEquipments = true;
    };

    static bool IsDynamicAttachedForm(const std::string& name) {
        auto pos = name.find(" (");
        if (pos != std::string::npos) {
            if (name.find(")[") != std::string::npos || name.back() == ')') return true;
        }
        return false;
    }

    static bool ShouldFilterNode(const std::string& name, const ExportFilters& filters) {
        if (name.empty() || name == "None") return false;
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        if (filters.excludeIAD && name.find("IAD_") != std::string::npos) return true;
        if (filters.excludePhysics) {
            if (lowerName.find("cbp") != std::string::npos || lowerName.find("hdt") != std::string::npos ||
                lowerName.find("smp") != std::string::npos || lowerName.find("vagina") != std::string::npos ||
                lowerName.find("penis") != std::string::npos || lowerName.find("tail") != std::string::npos ||
                lowerName.find("breast") != std::string::npos || lowerName.find("butt") != std::string::npos ||
                lowerName.find("belly") != std::string::npos || lowerName.find("tongue") != std::string::npos ||
                lowerName.find("extra_head") != std::string::npos || lowerName.find("fat") != std::string::npos ||
                lowerName.find("_skin") != std::string::npos) {
                return true;
            }
        }
        if (filters.excludeFaceGen) {
            if (name == "BSFaceGenNiNodeSkinned" || name == "[Overlays]") return true;
        }
        if (filters.excludeEquipments) {
            if (IsDynamicAttachedForm(name)) return true;
        }
        return false;
    }

    static void ExportBoneTreeRecursive(const BoneNode& node, std::ofstream& outFile, int depth, const ExportFilters& filters) {
        if (ShouldFilterNode(node.name, filters)) return;
        bool isAnonymous = node.name.empty() || node.name == "None";
        if (isAnonymous) {
            for (const auto& child : node.children) ExportBoneTreeRecursive(child, outFile, depth, filters);
            return;
        }
        std::string indent(depth * 4, ' ');
        if (depth > 0) indent.replace(indent.size() - 2, 2, "├─");
        outFile << indent << node.name << "\n";
        for (const auto& child : node.children) ExportBoneTreeRecursive(child, outFile, depth + 1, filters);
    }

    // 👇========== 🌟 核心面板类实现：骨骼扫描仪 ==========👇
    void UIBoneScannerWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowBoneScanner) return;
        ImGui::SetNextWindowSize({ 400.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.bone_scanner"), &config->uiShowBoneScanner)) {
            TrackTopLevelWindowFocus(6);
            if (ImGui::Button(TextLiteral("扫描当前玩家骨骼"), { ImGui::GetContentRegionAvail().x * 0.65f, 30 })) {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (player && player->Get3D(false)) {
                    s_flatBoneList.clear(); s_boneTree.children.clear();
                    BuildTree(player->Get3D(false), s_boneTree); s_hasCachedTree = true;
                }
            }
            ImGui::SameLine();
            if (!s_hasCachedTree) ImGui::BeginDisabled();
            if (ImGui::Button(TextLiteral("💾 导出文本"), { -1, 30 })) ImGui::OpenPopup("ExportTreeFilters");
            if (!s_hasCachedTree) ImGui::EndDisabled();

            if (ImGui::BeginPopupModal("ExportTreeFilters", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static ExportFilters s_currentFilters;
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, TextLiteral("选择要排除(屏蔽)的骨骼类型："));
                ImGui::TextDisabled(TextLiteral("被排除的骨骼及其下属所有层级都不会被导出，极大精简列表。")); ImGui::Separator(); ImGui::Spacing();
                ImGui::Checkbox(TextLiteral("屏蔽 IAD 自定义挂载点 (IAD_*)"), &s_currentFilters.excludeIAD);
                ImGui::Checkbox(TextLiteral("屏蔽 物理器官/皮肤变形节点 (CBP/乳/臀/私密/Skin等)"), &s_currentFilters.excludePhysics);
                ImGui::Checkbox(TextLiteral("屏蔽 头部/面部/表情/头发树 (BSFaceGenNiNodeSkinned)"), &s_currentFilters.excludeFaceGen);
                ImGui::Checkbox(TextLiteral("屏蔽 动态附加模型 (当前穿着的护甲/拿着的武器/哔哔小子)"), &s_currentFilters.excludeEquipments);
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                if (ImGui::Button(TextLiteral("生成并导出 (Export)"), { 150.0f, 30.0f })) {
                    std::filesystem::create_directories("Data/F4SE/Plugins/ImmersiveArsenalDisplays/Exports");
                    std::string exportPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/Exports/BoneTree_Export.txt";
                    std::ofstream outFile(exportPath);
                    if (outFile.is_open()) {
                        outFile << TextLiteral("==========================================\n Immersive Arsenal Displays - 纯净骨架快照\n==========================================\n过滤规则启用状态：\n");
                        outFile << TextLiteral(" - IAD节点: ") << (s_currentFilters.excludeIAD ? TextLiteral("已屏蔽") : TextLiteral("保留")) << TextLiteral("\n - 物理骨骼: ") << (s_currentFilters.excludePhysics ? TextLiteral("已屏蔽") : TextLiteral("保留")) << TextLiteral("\n - 面部骨架: ") << (s_currentFilters.excludeFaceGen ? TextLiteral("已屏蔽") : TextLiteral("保留")) << TextLiteral("\n - 动态装备: ") << (s_currentFilters.excludeEquipments ? TextLiteral("已屏蔽") : TextLiteral("保留")) << "\n==========================================\n\n";
                        ExportBoneTreeRecursive(s_boneTree, outFile, 0, s_currentFilters); outFile.close();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button(TextLiteral("取消 (Cancel)"), { 150.0f, 30.0f })) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Separator(); ImGui::Text(TextLiteral("搜索节点:")); ImGui::SameLine();
            static char searchFilter[64] = ""; ImGui::InputText("##boneSearch", searchFilter, 64);
            ImGui::BeginChild("BoneList", { 0, 0 }, true);
            {
                if (s_hasCachedTree) DrawTree(s_boneTree);
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // 👇========== 🌟 核心面板类实现：透视可视化选项 ==========👇
    void UIVisualizerWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowVisualizer) return;

        ImGui::SetNextWindowSize({ 560.0f, 640.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Text("window.visualizer"), &config->uiShowVisualizer)) {
            auto* hm = HolsterManager::GetSingleton();
			std::lock_guard<std::mutex> debugSettingsLock(hm->debugSettingsMutex);
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            ImGuiManager::GetSingleton().NotifyWindowFocused(7, focused);
            UIWorldPreviewEditor::GetSingleton().NotifyEditorWindow(
                UIWorldPreviewEditor::EditorWindow::kVisualizer,
                focused);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

            // Compact context bar: the active selection is always visible while
            // the detailed target and layer controls stay in their own sections.
            std::string selection = TextLiteral("未选择");
            if (!GetSlotEditorContext().selection.Empty()) {
                selection = TextLiteral("插槽: ") + std::string(StripManagedName(GetSlotEditorContext().selection.Selected()));
            }
            if (!GetNodeEditorContext().selection.Empty()) {
                if (selection != TextLiteral("未选择")) selection += "  |  ";
                selection += TextLiteral("节点: ") + std::string(StripManagedName(GetNodeEditorContext().selection.Selected()));
            }
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), TextLiteral("当前对象"));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", selection.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", hm->debugSettings.useLocalAxesSpace ? TextLiteral("局部坐标") : TextLiteral("世界坐标"));

            if (ImGui::BeginTable("PreviewEditorQuickActions", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                if (ImGui::Button(TextLiteral("打开插槽编辑"), ImVec2(-1.0f, 0.0f))) {
                    config->uiShowSlots = true;
                    UIWorldPreviewEditor::GetSingleton().SetActiveEditorWindow(UIWorldPreviewEditor::EditorWindow::kSlots);
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(TextLiteral("打开节点编辑"), ImVec2(-1.0f, 0.0f))) {
                    config->uiShowNodes = true;
                    UIWorldPreviewEditor::GetSingleton().SetActiveEditorWindow(UIWorldPreviewEditor::EditorWindow::kNodes);
                }
                ImGui::TableNextColumn();
                if (ImGui::Button(TextLiteral("清除选择"), ImVec2(-1.0f, 0.0f))) {
                    GetSlotEditorContext().selection.Clear();
                    GetNodeEditorContext().selection.Clear();
                }
                ImGui::EndTable();
            }

            if (ImGui::CollapsingHeader(TextLiteral("角色预览编辑##previewEditor"), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox(TextLiteral("启用角色预览编辑"), &hm->debugSettings.worldPreviewEdit)) {
                    if (!hm->debugSettings.worldPreviewEdit) {
                        UIWorldPreviewEditor::GetSingleton().Reset();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Checkbox(TextLiteral("自动切换显示"), &hm->debugSettings.worldPreviewAutoWindow)) {
                    UIWorldPreviewEditor::GetSingleton().SetActiveEditorWindow(UIWorldPreviewEditor::EditorWindow::kNone);
                }
				ImGui::SameLine();
				ImGui::TextDisabled(TextLiteral("独立场景预览：暂时停用"));
                ImGui::TextDisabled(TextLiteral("左键选中节点或武器轮廓；拖动 XYZ 轴修改位置。右键旋转，中键平移，滚轮缩放。"));
                ImGui::TextDisabled(TextLiteral("Shift 精细，Ctrl 粗略，Ctrl+Shift 超精细。镜头以打开编辑时的游戏视角为基础。"));
                if (hm->debugSettings.worldPreviewEdit) {
                    UIWorldPreviewEditor::GetSingleton().DrawStatus();
                    UIWorldPreviewSession::GetSingleton().DrawStatus();
                }
            }

            if (ImGui::CollapsingHeader(TextLiteral("可视化图层##visualLayers"), ImGuiTreeNodeFlags_None)) {
                if (ImGui::BeginTable("IADVisualLayerTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn(TextLiteral("图层"), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(TextLiteral("显示"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
                    ImGui::TableSetupColumn(TextLiteral("名称"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
                    ImGui::TableSetupColumn(TextLiteral("轴"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
                    ImGui::TableHeadersRow();
                    auto drawLayer = [](const char* label, bool& show, bool& names, bool& axes) {
                        ImGui::PushID(label);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(label);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Checkbox("##show", &show);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Checkbox("##names", &names);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Checkbox("##axes", &axes);
                        ImGui::PopID();
                    };
                    drawLayer(TextLiteral("原生骨骼"), hm->debugSettings.showVanilla, hm->debugSettings.showVanillaNames, hm->debugSettings.showVanillaAxes);
                    drawLayer(TextLiteral("挂载基座 CME"), hm->debugSettings.showCME, hm->debugSettings.showCMENames, hm->debugSettings.showCMEAaxes);
                    drawLayer(TextLiteral("展示插槽 MOV"), hm->debugSettings.showMOV, hm->debugSettings.showMOVNames, hm->debugSettings.showMOVAxes);
                    ImGui::EndTable();
                }
                ImGui::TextDisabled(TextLiteral("预览编辑开启时，CME/MOV 会由当前编辑窗口自动筛选；关闭编辑后恢复此处的调试图层设置。"));
            }

            if (ImGui::CollapsingHeader(TextLiteral("节点监控##nodeMonitor"), ImGuiTreeNodeFlags_None)) {
                if (ImGui::Checkbox(TextLiteral("只显示监控列表中的节点"), &config->nodeMonitorUseFilter)) {
                    UIEditCoordinator::RequestConfigSave();
                }
                ImGui::SameLine();
                ImGui::TextDisabled(TextLiteral("列表为空时不会过滤。"));

                if (UIManagedProfileControls::DrawSelector(
                    "NodeMonitorProfileSelector",
                    TextLiteral("节点监控预设"),
                    IAD::Profile::GlobalProfileManager::GetSingleton().NodeMonitors(),
                    config->nodeMonitorNames,
                    "MyNodeMonitor",
                    [](std::vector<std::string>& target, const std::vector<std::string>& source) {
                        target = source;
                    },
                    [](std::vector<std::string>& target, const std::vector<std::string>& source) {
                        for (const auto& name : source) {
                            if (!name.empty() && std::find(target.begin(), target.end(), name) == target.end()) {
                                target.push_back(name);
                            }
                        }
                    },
                    true)) {
                    UIEditCoordinator::RequestConfigSave();
                }

                static char monitorNodeName[96] = "";
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("##monitorNodeName", monitorNodeName, 96);
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("添加名称"))) {
                    config->AddNodeMonitorName(monitorNodeName);
                    UIEditCoordinator::RequestConfigSave();
                    monitorNodeName[0] = '\0';
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("添加当前选择"))) {
                    const std::string& selected = !GetNodeEditorContext().selection.Empty() ?
                        GetNodeEditorContext().selection.Selected() : GetSlotEditorContext().selection.Selected();
                    if (!selected.empty()) {
                        config->AddNodeMonitorName(selected);
                        UIEditCoordinator::RequestConfigSave();
                    }
                }

                auto monitorNames = config->GetNodeMonitorNamesSnapshot();
                if (ImGui::BeginTable("NodeMonitorTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn(TextLiteral("节点"), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(TextLiteral("操作"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < monitorNames.size(); ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", monitorNames[i].c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Button(TextLiteral("移除"))) {
                            config->RemoveNodeMonitorName(i);
                            UIEditCoordinator::RequestConfigSave();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader(TextLiteral("高级显示与坐标##advancedVisualizer"), ImGuiTreeNodeFlags_None)) {
                ImGui::TextDisabled(TextLiteral("坐标轴图例: 红 X，绿 Y，蓝 Z。预览编辑会优先显示选中对象的操作柄。"));
                if (ImGui::RadioButton(TextLiteral("局部坐标系 (随骨骼翻转)"), hm->debugSettings.useLocalAxesSpace)) {
                    hm->debugSettings.useLocalAxesSpace = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(TextLiteral("世界坐标系 (绝对方向)"), !hm->debugSettings.useLocalAxesSpace)) {
                    hm->debugSettings.useLocalAxesSpace = false;
                }
            }

            // Keep edit history at the bottom of the inspector so the frequent
            // object controls remain above it and the rollback actions are easy
            // to find after a drag.
            if (hm->debugSettings.worldPreviewEdit) {
                UIWorldPreviewEditor::GetSingleton().DrawTransactionControls();
            }

            ImGui::PopStyleVar();
        }
        ImGui::End();
    }

} // 🌟 别忘了这个大括号，把 IAD::UI 命名空间闭合！
