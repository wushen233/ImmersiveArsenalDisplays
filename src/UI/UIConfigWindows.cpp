#include "pch.h"
#include "UIConfigWindows.h"
#include "ImGuiManager.h"
#include "Data/ConfigManager.h"
#include "Profile/GlobalProfileManager.h"
#include "System/HolsterManager.h"
#include "Engine/ConditionSystem.h"
#include "Engine/NodeManager.h"

#include <RE/C/ControlMap.h>
#include <RE/M/MenuCursor.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>          // 注意：旧库叫 TESForms，新库单数化为 TESForm
#include <RE/P/ProcessLists.h>
#include <RE/T/TESDataHandler.h>
#include <RE/E/ENUM_FORM_ID.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>

namespace IAD::UI {

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
    static void DrawBoneScannerBox(const char* a_label, std::string& a_targetNode);
    static void DrawCMENodeSelector(const char* a_label, std::string& a_targetNode, const std::vector<NodeDefinition>& localNodes, ConfigScope currentScope, const char* emptyLabel = nullptr);
    static bool DrawConditionNodeRecursive(IAD::ConditionNode& node, bool isRoot, int depth);
    bool DrawConditionTreeEditor(IAD::ConditionNode& rootNode, bool showProfileControls);
    static std::string BuildConditionTreeSignature(const IAD::ConditionNode& node);
    static bool DrawIEDTransformWidget(const char* label, RE::NiPoint3& val, const RE::NiPoint3& defaultVal, float baseSpeed, bool isRotation = false);
    static bool DrawWindowHeader(const char* idStr, ImGuiManager::WindowState& state, bool showTarget, bool showGender);
    static void DrawFormSetUI(std::set<std::uint32_t>& a_set, const char* a_id);
    static bool DrawFormVectorUI(std::vector<std::uint32_t>& a_vec, const char* a_id);
    static bool DrawFormIDField(const char* a_label, std::uint32_t& a_formID);
    static bool DrawStringVectorEditor(const char* a_label, std::vector<std::string>& a_values, const char* a_defaultValue);
    static bool DrawFormTypeVectorEditor(const char* a_label, std::vector<std::uint8_t>& a_values);
    static bool DrawBipedSlotVectorEditor(const char* a_label, std::vector<std::uint32_t>& a_values);
    static bool DrawColorRGBA(const char* a_label, ColorRGBA& a_color);
    static bool DrawSkeletonMatchEditor(NodeDefinition::SkeletonMatchConfig& a_match);
    static bool DrawModelGroupAdvancedConfig(ModelGroupEntry& a_group);
    static bool DrawModelCleanupSettings(bool& disableHavok, bool& removeEditorMarker, bool& removeProjectileTracers, const char* idSuffix);
    static bool DrawModelAnimationSettings(ModelAnimationConfig& animation, const char* idSuffix);
    static bool DrawModelEffectShaderSettings(ModelEffectShaderConfig& effect, const char* idSuffix);
    static bool DrawModelLightSettings(ModelLightConfig& light, const char* idSuffix);
    static bool DrawModelSwapVariableSource(ModelSwapVariableSource& source, const char* idSuffix);
    static void DrawFormFilterUI(FormFilter& a_filter, const char* a_id);
    static bool DrawTabEquipment(SlotDefinition& a_slot);
    static bool DrawTabDisplay(SlotDefinition& a_slot);
	static bool DrawSlotEditorTabs(SlotDefinition& a_slot, ImGuiManager::WindowState& a_state, const char* a_idSuffix);
	static bool DrawNodeEditorTabs(NodeDefinition& a_node, ImGuiManager::WindowState& a_state, const char* a_idSuffix);
	static bool DrawCustomEditorTabs(CustomDefinition& a_custom, ImGuiManager::WindowState& a_state, const std::vector<NodeDefinition>& a_nodes, const char* a_idSuffix);
    static bool DrawStateMachineEditor(ConfigBase& a_config, const char* a_idSuffix, bool a_isNodeState = false);
    static bool DrawConfigBaseTransform(ConfigBase& a_config, int genderEdit, bool syncGender, const char* idSuffix, bool isNode = false);
    static bool DrawSlotMeshTransform(SlotDefinition& a_slot, ImGuiManager::WindowState& state, const char* idSuffix);
    static bool DrawCustomMeshTransform(CustomDefinition& a_custom, ImGuiManager::WindowState& state, const std::vector<NodeDefinition>& localNodes, const char* idSuffix);
    static bool DrawPhysicsConstraintParams(PhysicsConstraintParams& a_params);
    static bool DrawPhysicsPanel(PhysicsValues& a_phys);
    static bool DrawConfigBasePhysics(ConfigBase& a_config, const char* idSuffix, bool isNode = false);

    // 👇========== 🌟 辅助组件实现区域 ==========👇
    static uint32_t GetQueryID(const ImGuiManager::WindowState& state) {
        return (state.scope == ConfigScope::kGlobal) ? state.targetFilter : state.id;
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
        if (ImGui::Button("重命名所选", { 130.0f, 0.0f })) {
            const bool renamed = folderName ?
                config->RenameProfile(folderName, selectedProfile, targetName) :
                config->RenameExport(selectedProfile, targetName);
            if (renamed) {
                selectedProfile = targetName;
                statusText = "已重命名所选预设。";
            }
            else {
                statusText = "重命名失败。";
            }
        }
        if (!hasSelection || !hasTargetName) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button("删除所选", { 130.0f, 0.0f })) {
            ImGui::OpenPopup("ConfirmDeleteSelectedProfile");
        }
        if (!hasSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteSelectedProfile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("删除预设: %s", selectedProfile.c_str());
            ImGui::Separator();
            if (ImGui::Button("删除", { 120.0f, 0.0f })) {
                const bool deleted = folderName ?
                    config->DeleteProfile(folderName, selectedProfile) :
                    config->DeleteExport(selectedProfile);
                statusText = deleted ? "已删除所选预设。" : "删除失败。";
                if (deleted) {
                    selectedProfile.clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", { 120.0f, 0.0f })) {
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
        char* nameBuffer,
        std::size_t nameBufferSize,
        DrawItemFn drawItem,
        bool* openState = nullptr)
    {
        static std::map<std::string, std::string> selectedById;
        static std::map<std::string, std::string> statusById;
        static std::map<std::string, std::array<char, 64>> filterById;
        static std::map<std::string, std::array<char, 512>> descById;
        static std::map<std::string, std::string> lastSelectedById;

        const std::string key = id;
        auto& selected = selectedById[key];
        auto& status = statusById[key];
        auto& filter = filterById[key];
        auto& desc = descById[key];
        auto& lastSelected = lastSelectedById[key];

        if (!manager.IsInitialized()) {
            manager.Load();
        }

        ImGui::PushID(id);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("文件")) {
                if (openState && ImGui::MenuItem("关闭")) {
                    *openState = false;
                }
                if (openState) {
                    ImGui::Separator();
                }
                if (ImGui::MenuItem("重新读取预设目录")) {
                    manager.Load();
                    selected.clear();
                    status = "已重新读取预设目录。";
                }
                ImGui::EndMenu();
            }
            if (auto* record = manager.Find(selected)) {
                if (ImGui::BeginMenu("预设设置")) {
                    bool mergeOnly = record->IsMergeOnly();
                    if (ImGui::MenuItem("仅允许合并", nullptr, mergeOnly)) {
                        record->SetMergeOnly(!mergeOnly);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("说明")) {
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
        ImGui::InputText("筛选", filter.data(), filter.size());
        ImGui::SameLine();
        if (ImGui::Button("刷新列表", { 90.0f, 0.0f })) {
            manager.Load();
            selected.clear();
            status = "已重新读取预设目录。";
        }

        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("预设", selected.empty() ? "未选择" : selected.c_str(), ImGuiComboFlags_HeightLarge)) {
            const std::string filterText = filter.data();
            for (auto& [name, record] : manager.Data()) {
                if (!filterText.empty() && name.find(filterText) == std::string::npos) {
                    continue;
                }
                bool isSelected = selected == name;
                std::string label = record.modified ? ("* " + name) : name;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selected = name;
                    strcpy_s(nameBuffer, nameBufferSize, name.c_str());
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("名称", nameBuffer, nameBufferSize);
        const bool hasName = std::strlen(nameBuffer) > 0;
        const bool hasSelection = !selected.empty();

        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button("新建", { 72.0f, 0.0f })) {
            if (manager.CreateProfile(nameBuffer)) {
                selected = nameBuffer;
                status = "已创建预设。";
            }
            else {
                status = manager.LastError();
            }
        }
        if (!hasName) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button("保存", { 72.0f, 0.0f })) {
            if (manager.SaveProfile(selected)) {
                status = "已保存预设。";
            }
            else {
                status = manager.LastError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("重新读取", { 72.0f, 0.0f })) {
            if (manager.ReloadProfile(selected)) {
                status = "已重新读取预设。";
            }
            else {
                status = manager.LastError();
            }
        }
        ImGui::SameLine();
        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button("重命名", { 82.0f, 0.0f })) {
            const std::string oldName = selected;
            if (manager.RenameProfile(oldName, nameBuffer)) {
                if constexpr (std::is_same_v<T, FormFilter>) {
                    auto* config = ConfigManager::GetSingleton();
                    if (config->RenameFormFilterReferences(oldName, nameBuffer)) {
                        config->SaveConfig();
                    }
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                selected = nameBuffer;
                status = "已重命名预设。";
            }
            else {
                status = manager.LastError();
            }
        }
        if (!hasName) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("删除", { 72.0f, 0.0f })) {
            ImGui::OpenPopup("ConfirmManagedProfileDelete");
        }
        if (!hasSelection) ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmManagedProfileDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("删除预设: %s", selected.c_str());
            ImGui::Separator();
            if (ImGui::Button("删除", { 120.0f, 0.0f })) {
                const std::string deletedName = selected;
                if (manager.DeleteProfile(deletedName)) {
                    if constexpr (std::is_same_v<T, FormFilter>) {
                        auto* config = ConfigManager::GetSingleton();
                        if (config->ClearFormFilterReferences(deletedName)) {
                            config->SaveConfig();
                        }
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    selected.clear();
                    status = "已删除预设。";
                }
                else {
                    status = manager.LastError();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", { 120.0f, 0.0f })) {
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
                ImGui::Text("预设说明");
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputTextMultiline("##ProfileDescription", desc.data(), desc.size(), ImVec2(420.0f, 140.0f));
                ImGui::Separator();
                if (ImGui::Button("应用", { 120.0f, 0.0f })) {
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
                if (ImGui::Button("取消", { 120.0f, 0.0f })) {
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
                ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, "预设读取时出现解析警告。");
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
            ImGui::TextDisabled("请选择一个预设。 ");
        }
        ImGui::PopID();
    }

    template <class T, class ApplyFn, class MergeFn>
    static bool DrawManagedProfileSelector(
        const char* id,
        const char* label,
        IAD::Profile::ProfileManager<T>& manager,
        T& target,
        const char* defaultName,
        ApplyFn applyFn,
        MergeFn mergeFn,
        bool enableMerge)
    {
        static std::map<std::string, std::string> selectedById;
        static std::map<std::string, std::string> statusById;
        static std::map<std::string, std::array<char, 64>> nameById;

        const std::string key = id;
        auto& selected = selectedById[key];
        auto& status = statusById[key];
        auto& name = nameById[key];

        if (name[0] == '\0') {
            strcpy_s(name.data(), name.size(), defaultName);
        }

        if (!manager.IsInitialized()) {
            manager.Load();
        }

        bool changed = false;

        ImGui::PushID(id);
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "%s", label);

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("预设", selected.empty() ? "选择预设..." : selected.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (auto& [nameKey, record] : manager.Data()) {
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
        if (ImGui::Button("刷新")) {
            manager.Load();
            selected.clear();
            status = "已重新读取预设列表。";
        }

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("名称", name.data(), name.size());

        const bool hasName = std::strlen(name.data()) > 0;
        auto* record = manager.Find(selected);

        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button("新建", { 70.0f, 0.0f })) {
            if (manager.CreateProfile(name.data(), target)) {
                selected = name.data();
                status = "已从当前配置创建预设。";
            }
            else {
                status = manager.LastError();
            }
        }
        if (!hasName) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!record) ImGui::BeginDisabled();
        if (ImGui::Button("保存", { 70.0f, 0.0f })) {
            record->data = target;
            record->MarkModified();
            if (manager.SaveProfile(selected)) {
                status = "Saved current values to profile.";
            }
            else {
                status = manager.LastError();
            }
        }

        ImGui::SameLine();
        const bool applyDisabled = !record || record->IsMergeOnly();
        if (applyDisabled) ImGui::BeginDisabled();
        if (ImGui::Button("Apply", { 70.0f, 0.0f })) {
            applyFn(target, record->data);
            changed = true;
            status = "Applied profile.";
        }
        if (applyDisabled) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!record || !enableMerge) ImGui::BeginDisabled();
        if (ImGui::Button("Merge", { 70.0f, 0.0f })) {
            mergeFn(target, record->data);
            changed = true;
            status = "Merged profile.";
        }
        if (!record || !enableMerge) ImGui::EndDisabled();
        if (!record) ImGui::EndDisabled();

        if (record) {
            if (record->parserErrors) {
                ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, "Profile loaded with parser warnings.");
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

    static CustomDefinition* GetSelectedCustomForProfile(ConfigManager* config) {
        auto& customs = config->GetCustoms(ImGuiManager::s_customState.scope, GetQueryID(ImGuiManager::s_customState));
        if (!ImGuiManager::s_selectedCustom.empty()) {
            auto it = std::find_if(customs.begin(), customs.end(), [](const CustomDefinition& custom) {
                return custom.customName == ImGuiManager::s_selectedCustom;
                });
            if (it != customs.end()) {
                return &(*it);
            }
        }
        return customs.empty() ? nullptr : &customs.front();
    }

    static SlotDefinition* GetSelectedSlotForProfile(ConfigManager* config) {
        auto& slots = config->GetSlots(ImGuiManager::s_slotState.scope, GetQueryID(ImGuiManager::s_slotState));
        if (!ImGuiManager::s_selectedSlot.empty()) {
            auto it = std::find_if(slots.begin(), slots.end(), [](const SlotDefinition& slot) {
                return slot.slotName == ImGuiManager::s_selectedSlot;
                });
            if (it != slots.end()) {
                return &(*it);
            }
        }
        return slots.empty() ? nullptr : &slots.front();
    }

    static NodeDefinition* GetSelectedNodeForProfile(ConfigManager* config) {
        auto& nodes = config->GetNodes(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState));
        if (!ImGuiManager::s_selectedNode.empty()) {
            auto it = std::find_if(nodes.begin(), nodes.end(), [](const NodeDefinition& node) {
                return node.nodeName == ImGuiManager::s_selectedNode;
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

        ImGui::TextDisabled("%zu 个插槽", record.data.size());
        ImGui::Separator();

        static float s_leftPaneW_profileSlot = 220.0f;
        ImGui::BeginChild("ProfileSlotLeft", ImVec2(s_leftPaneW_profileSlot, 0.0f), true);
        if (ImGui::Button("新建装备槽位", ImVec2(-1.0f, 32.0f))) {
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

        ImGui::SameLine();
        ImGui::InvisibleButton("##profileSlotSplitter", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive()) {
            s_leftPaneW_profileSlot += ImGui::GetIO().MouseDelta.x;
            if (s_leftPaneW_profileSlot < 120.0f) s_leftPaneW_profileSlot = 120.0f;
            if (s_leftPaneW_profileSlot > ImGui::GetWindowWidth() - 180.0f) s_leftPaneW_profileSlot = ImGui::GetWindowWidth() - 180.0f;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine();

        ImGui::BeginChild("ProfileSlotRight", ImVec2(0.0f, 0.0f), true);
        auto it = std::find_if(record.data.begin(), record.data.end(), [&](const SlotDefinition& slot) { return slot.slotName == selectedSlot; });
        if (it != record.data.end()) {
            auto& slot = *it;
            ImGui::PushID(("ProfileSlotEditor_" + slot.slotName).c_str());

            std::string shortName = (slot.slotName.find("IAD_MOV_") == 0) ? slot.slotName.substr(8) : slot.slotName;
            char nameBuf[64];
            strcpy_s(nameBuf, shortName.c_str());
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "槽位名称:");
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
            if (ImGui::Button("删除槽位")) {
                ImGui::OpenPopup("DeleteProfileSlot");
            }
            if (ImGui::BeginPopupModal("DeleteProfileSlot", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("从此预设删除槽位 [%s]？", slot.slotName.c_str());
                if (ImGui::Button("删除", { 100.0f, 0.0f })) {
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
                if (ImGui::Button("取消", { 100.0f, 0.0f })) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Checkbox("全局启用", &slot.isEnabled)) changed = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt("渲染优先级", &slot.priority, 1, 0, 100)) changed = true;
            ImGui::Separator();

            ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "绑定目标节点:");
            ImGui::SameLine();
            const auto oldNode = slot.targetNode;
            DrawCMENodeSelector("##profileSlotTargetNode", slot.targetNode, globalNodes, ConfigScope::kGlobal, "未绑定");
            if (slot.targetNode != oldNode) changed = true;
            ImGui::Separator();

            changed |= DrawSlotEditorTabs(slot, profileState, "profileSlotTabs");

            ImGui::PopID();
        }
        else {
            ImGui::TextDisabled("请在左侧选择或新建一个装备槽位。 ");
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

        static float s_leftPaneW_profileNode = 220.0f;
        ImGui::BeginChild("ProfileNodeLeft", ImVec2(s_leftPaneW_profileNode, 0.0f), true);
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

        ImGui::SameLine();
        ImGui::InvisibleButton("##profileNodeSplitter", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive()) {
            s_leftPaneW_profileNode += ImGui::GetIO().MouseDelta.x;
            if (s_leftPaneW_profileNode < 120.0f) s_leftPaneW_profileNode = 120.0f;
            if (s_leftPaneW_profileNode > ImGui::GetWindowWidth() - 180.0f) s_leftPaneW_profileNode = ImGui::GetWindowWidth() - 180.0f;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine();

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
                changed |= DrawStringVectorEditor("Fallback hosts", node.fallbackHosts, "NPC Root [Root]");
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

        static float s_leftPaneW_profileCustom = 220.0f;
        ImGui::BeginChild("ProfileCustomLeft", ImVec2(s_leftPaneW_profileCustom, 0.0f), true);
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

        ImGui::SameLine();
        ImGui::InvisibleButton("##profileCustomSplitter", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive()) {
            s_leftPaneW_profileCustom += ImGui::GetIO().MouseDelta.x;
            if (s_leftPaneW_profileCustom < 120.0f) s_leftPaneW_profileCustom = 120.0f;
            if (s_leftPaneW_profileCustom > ImGui::GetWindowWidth() - 180.0f) s_leftPaneW_profileCustom = ImGui::GetWindowWidth() - 180.0f;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine();

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
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt("Priority", &custom.priority, 1, 0, 100)) changed = true;
            if (ImGui::Checkbox("Ignore player", &custom.ignorePlayer)) changed = true;
            ImGui::Separator();

            changed |= DrawFormIDField("Target FormID", custom.targetFormID);
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
                changed |= DrawBipedSlotVectorEditor("Last equipped biped slots", custom.lastEquippedBipedSlots);
                if (ImGui::Checkbox("Prioritize recent display slot", &custom.lastEquippedPrioritizeRecentDisplaySlot)) changed = true;
                if (ImGui::Checkbox("Skip occupied display slots", &custom.lastEquippedSkipOccupiedDisplaySlots)) changed = true;
                if (ImGui::Checkbox("Disable if listed display slot is occupied", &custom.lastEquippedDisableIfDisplaySlotOccupied)) changed = true;
                if (ImGui::Checkbox("Fallback to last slotted display item", &custom.lastEquippedFallbackToSlotted)) changed = true;
                if (ImGui::Checkbox("Fallback to any available slot", &custom.lastEquippedFallbackToAnySlot)) changed = true;
                if (ImGui::Checkbox("Fallback to recent acquired", &custom.lastEquippedFallbackToRecentAcquired)) changed = true;
                if (custom.lastEquippedFallbackToRecentAcquired) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("Prioritize recent acquired types", &custom.lastEquippedPrioritizeRecentAcquiredTypes)) changed = true;
                    changed |= DrawFormTypeVectorEditor("Recent acquired form types", custom.lastEquippedRecentAcquiredFormTypes);
                    ImGui::Unindent();
                }
                changed |= DrawStringVectorEditor("Last equipped slots", custom.lastEquippedDisplaySlots, "Backpack_Right");
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
                changed |= DrawFormVectorUI(custom.extraItems, "ProfileCustomExtraItems");
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
                changed |= DrawFormIDField("Model swap FormID", custom.modelSwapFormID);
                changed |= DrawModelSwapVariableSource(custom.modelSwapVariableSource, "profile_custom_model_var_source");
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
                if (DrawModelCleanupSettings(custom.disableHavok, custom.removeEditorMarker, custom.removeProjectileTracers, "profile_custom_cleanup")) {
                    changed = true;
                }
                if (DrawModelAnimationSettings(custom.animation, "profile_custom_animation")) {
                    changed = true;
                }
                if (DrawModelEffectShaderSettings(custom.effectShader, "profile_custom_effect")) {
                    changed = true;
                }
                if (DrawModelLightSettings(custom.light, "profile_custom_light")) {
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
            HolsterManager::GetSingleton()->ForceRefreshAll();
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
                    changed |= DrawFormIDField("Source FormID", group.sourceFormID);
                }

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "Target node:");
                ImGui::SameLine();
                const auto oldTargetNode = group.targetNode;
                DrawCMENodeSelector("##profileModelGroupTargetNode", group.targetNode, globalNodes, ConfigScope::kGlobal, "[inherit]");
                if (group.targetNode != oldTargetNode) changed = true;

                if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    changed |= DrawIEDTransformWidget("Position", group.transforms.m.pos, { 0,0,0 }, 0.5f, false);
                    changed |= DrawIEDTransformWidget("Rotation", group.transforms.m.rot, { 0,0,0 }, 1.0f, true);
                    changed |= DrawIEDTransformWidget("Pivot", group.transforms.m.pivot, { 0,0,0 }, 0.5f, false);
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
        return DrawStringVectorEditor("Node names", record.data, "NPC Root [Root]");
    }

    static bool DrawConditionProfileRecord(IAD::Profile::ProfileRecord<ConditionNode>& record) {
        return DrawConditionTreeEditor(record.data, false);
    }

    static bool DrawTransformProfileRecord(IAD::Profile::ProfileRecord<TransformData>& record) {
        bool changed = false;
        changed |= DrawIEDTransformWidget("Position", record.data.pos, { 0,0,0 }, 0.5f, false);
        changed |= DrawIEDTransformWidget("Rotation", record.data.rot, { 0,0,0 }, 1.0f, true);
        changed |= DrawIEDTransformWidget("Pivot", record.data.pivot, { 0,0,0 }, 0.5f, false);
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

    static void DrawBoneScannerBox(const char* a_label, std::string& a_targetNode) {
        if (!s_hasCachedTree) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (player && player->Get3D(false)) {
                s_flatBoneList.clear(); s_boneTree.children.clear();
                BuildTree(player->Get3D(false), s_boneTree); s_hasCachedTree = true;
            }
        }
        ImGui::PushID(a_label);
        char buf[128]; strcpy_s(buf, a_targetNode.c_str()); ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##input", buf, 128)) a_targetNode = buf;
        ImGui::SameLine(0, 2.0f);
        if (ImGui::Button("▼")) ImGui::OpenPopup("BoneListPopup");
        ImGui::SameLine(); ImGui::Text("%s", a_label);

        if (ImGui::BeginPopup("BoneListPopup")) {
            static char searchBuf[64] = ""; ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##search", "🔍 搜索节点名称...", searchBuf, 64); ImGui::Separator();
            ImGui::BeginChild("##list", ImVec2(250.0f, 200.0f), true);
            {
                std::string searchStr = searchBuf; std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                for (const auto& boneName : s_flatBoneList) {
                    if (boneName.empty() || boneName == "None") continue;
                    if (!searchStr.empty()) {
                        std::string lowerBone = boneName; std::transform(lowerBone.begin(), lowerBone.end(), lowerBone.begin(), ::tolower);
                        if (lowerBone.find(searchStr) == std::string::npos) continue;
                    }
                    if (ImGui::Selectable(boneName.c_str(), a_targetNode == boneName)) {
                        a_targetNode = boneName; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    static void DrawCMENodeSelector(const char* a_label, std::string& a_targetNode, const std::vector<NodeDefinition>& localNodes, ConfigScope currentScope, const char* emptyLabel) {
        ImGui::PushID(a_label);
        char buf[128]; strcpy_s(buf, a_targetNode.c_str()); ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##input", buf, 128)) { a_targetNode = buf; HolsterManager::GetSingleton()->ForceRefreshAll(); }
        ImGui::SameLine(0, 2.0f);
        if (ImGui::Button("▼")) ImGui::OpenPopup("CMEListPopup");
        ImGui::SameLine(); ImGui::Text("%s", a_label);

        if (ImGui::BeginPopup("CMEListPopup")) {
            static char searchBuf[64] = ""; ImGui::SetNextItemWidth(250.0f);
            ImGui::InputTextWithHint("##search", "🔍 搜索 CME 节点...", searchBuf, 64); ImGui::Separator();
            ImGui::BeginChild("##list", ImVec2(250.0f, 200.0f), true);
            {
                std::string searchStr = searchBuf; std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                if (emptyLabel != nullptr) {
                    if (searchStr.empty() || std::string(emptyLabel).find(searchStr) != std::string::npos) {
                        if (ImGui::Selectable(emptyLabel, a_targetNode.empty())) { a_targetNode = ""; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup(); HolsterManager::GetSingleton()->ForceRefreshAll(); }
                        ImGui::Separator();
                    }
                }
                auto drawNodeItem = [&](const std::string& nodeName, bool isGlobal) {
                    if (nodeName.empty()) return;
                    if (!searchStr.empty()) { std::string lowerNode = nodeName; std::transform(lowerNode.begin(), lowerNode.end(), lowerNode.begin(), ::tolower); if (lowerNode.find(searchStr) == std::string::npos) return; }
                    std::string dispName = isGlobal ? "[继承] " + nodeName : nodeName;
                    if (isGlobal) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    if (ImGui::Selectable(dispName.c_str(), a_targetNode == nodeName)) { a_targetNode = nodeName; searchBuf[0] = '\0'; ImGui::CloseCurrentPopup(); HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (isGlobal) ImGui::PopStyleColor();
                    };
                for (const auto& n : localNodes) drawNodeItem(n.nodeName, false);
                if (currentScope != ConfigScope::kGlobal) {
                    ImGui::Separator(); ImGui::TextDisabled("-- 全局继承节点 --");
                    for (const auto& n : ConfigManager::GetSingleton()->GetNodes(ConfigScope::kGlobal, 0)) drawNodeItem(n.nodeName, true);
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    static void DrawKeywordScannerBox(const char* id, std::string& outKeyword) {
        char buf[128]; strcpy_s(buf, outKeyword.c_str()); ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText(id, buf, 128)) outKeyword = buf;
        ImGui::SameLine(); ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo((std::string("扫描身上关键字##") + id).c_str(), "展开选择...")) {
            auto player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                // 🌟 调用咱们底层的背包扫描器
                auto items = IAD::Scanner::GetActiveItems(player, false);
                std::set<std::string> kwSet;
                for (auto& item : items) {
                    if (item.isEquipped && item.object) {
                        auto kwdForm = item.object->As<RE::BGSKeywordForm>();
                        if (kwdForm && kwdForm->keywords) {
                            for (uint32_t i = 0; i < kwdForm->numKeywords; ++i) {
                                if (kwdForm->keywords[i] && kwdForm->keywords[i]->GetFormEditorID()) {
                                    kwSet.insert(kwdForm->keywords[i]->GetFormEditorID());
                                }
                            }
                        }
                    }
                }
                // 动态渲染获取到的关键字
                for (const auto& kw : kwSet) {
                    if (ImGui::Selectable(kw.c_str())) {
                        outKeyword = kw;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    static bool DrawConditionNodeRecursive(IAD::ConditionNode& node, bool isRoot, int depth) {
        bool deleteMe = false;
        ImGui::PushID(&node);
        bool isNodeOpen = false;

        if (node.isGroup) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            const auto groupFlags = ImGuiTreeNodeFlags_SpanAvailWidth | (isRoot ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
            isNodeOpen = ImGui::TreeNodeEx("##grp", groupFlags, node.children.empty() ? "条件组 (空)" : "条件组");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("+##add")) ImGui::OpenPopup("GrpAdd");
            if (ImGui::BeginPopup("GrpAdd")) {
                if (ImGui::MenuItem("添加条件")) node.children.push_back(IAD::ConditionNode(false));
                if (ImGui::MenuItem("添加子组")) node.children.push_back(IAD::ConditionNode(true));
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130.0f);
            const char* logicModes[] = { "满足任意条件", "必须全部满足" };
            int logicMode = node.isAnd ? 1 : 0;
            if (ImGui::Combo("##logic", &logicMode, logicModes, IM_ARRAYSIZE(logicModes))) node.isAnd = logicMode == 1;
            ImGui::SameLine();
            ImGui::Checkbox("取反##not", &node.isNot);
            if (!isRoot) { ImGui::SameLine(); if (ImGui::Button("删除##del")) deleteMe = true; }
        }
        else {
            const std::string title = node.type.empty() ? "选择条件" : node.type;
            const auto conditionFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
            isNodeOpen = ImGui::TreeNodeEx("##cond", conditionFlags, "%s  [%s]", title.c_str(), node.expected ? "真" : "假");
            ImGui::SameLine();
            if (ImGui::Button("删除##del")) deleteMe = true;
        }

        if (!node.isGroup && isNodeOpen) {
            const char* condTypes[] = { "IsSneaking", "IsSprinting", "IsInCombat", "IsWeaponDrawn", "IsWeaponDrawing", "IsWeaponDrawnStrict", "IsWeaponSheathing", "IsWeaponSheathed", "IsDrawn_Pistol", "IsDrawn_Rifle", "IsDrawn_Melee", "IsInPowerArmor", "IsInInterior", "InPublicCell", "InOwnedCell", "IsCellOwner", "IsNPCCellOwner", "IsInWater", "IsUnderwater", "IsUsingPipboy", "IsSitting", "IsSleeping", "IsLayingDown", "IsSwimming", "IsInvisible", "IsVisible", "IsCrafting", "IsInDialogue", "IsInVertibird", "IsPlayer", "IsPlayerTeammate", "IsPlayerEnemy", "InPlayerEnemyFaction", "PlayerEnemiesNearby", "IsFemale", "IsDead", "IsChild", "IsUnconscious", "IsRestrained", "IsBleedingOut", "IsInBleedoutAnimation", "IsTrespassing", "IsInKillmove", "IsBribedByPlayer", "IsAngryWithPlayer", "IsEssential", "IsProtected", "IsUnique", "IsSummonable", "IsInvulnerable", "IsCommanded", "IsParalyzed", "IsWaitingForPlayer", "IsGuard", "InMerchantFaction", "IsFollowing", "IsPathing", "IsPathingComplete", "IsQuadruped", "IsFlying", "IsFlightBlocked", "CanFly", "IsForceRun", "IsForceSneak", "IsHeadTracking", "WantsBlocking", "IsStaggered", "IsInSyncAnim", "IsReanimating", "IsScenePackage", "IsInRandomScene", "CanSpeak", "CanDoFavor", "CanSpeakToEssentialDown", "IsAttackOnSight", "IsAttackingDisabled", "IsCastingDisabled", "IsMovementBlocked", "DoNotShowOnStealthMeter", "ActorFormID", "ActorBaseFormID", "ActorRaceFormID", "ActorCombatStyle", "ActorClass", "ActorInFaction", "ActorInCell", "ActorInLocation", "ActorInLocationOrChild", "ActorInWorldspace", "GlobalValue", "ActorLevel", "ActorValue", "ActorPerkRank", "QuestStage", "DayOfWeek", "ActorLifeState", "HumanoidSkeleton", "SkeletonPathContains", "NodeMonitor", "RandomPercent", "KeyBindState", "IsFirstPerson", "IsInAir", "IsAiming", "IsTimeOfDay", "IsSunAboveHorizon", "SunAngle", "TimeOfDayPhase", "CurrentWeather", "WeatherClass", "LightingTemplate", "HasActiveEffect", "HasMagicEffect", "HasSpell", "InDarkArea", "InDarkness", "InteriorAmbientLightLevel", "RuntimeVariable", "RuntimeNumberVariable", "HasKeywordEquipped", "ActorHasKeyword", "HasEquippedBipedSlot", "HasEquippedFormID", "InventoryItemCount", "CandidateHasKeyword", "CandidateFormID", "CandidateFormType", "CandidateHasOMOD", "CandidateCount", "CandidateInventoryCount", "CandidateIsEquipped", "CandidateIsFavorited" };
            int numTypes = sizeof(condTypes) / sizeof(condTypes[0]); int curT = 0;
            for (int n = 0; n < numTypes; n++) if (node.type == condTypes[n]) curT = n;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::Combo("条件类型", &curT, condTypes, numTypes)) node.type = condTypes[curT];
            ImGui::PushStyleColor(ImGuiCol_Text, node.expected ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::BeginCombo("期望结果", node.expected ? "为真" : "为假")) {
                if (ImGui::Selectable("为真", node.expected == true)) node.expected = true;
                if (ImGui::Selectable("为假", node.expected == false)) node.expected = false;
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Checkbox("取反", &node.isNot);
            ImGui::Separator();

            if (node.type == "RuntimeVariable") {
                ImGui::SameLine();
                char varBuf[64]; strncpy_s(varBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##var", varBuf, 64)) node.keyword = varBuf;
                ImGui::SameLine();
                if (ImGui::BeginCombo("##varPick", "变量")) {
                    auto variables = ConfigManager::GetSingleton()->GetRuntimeVariablesSnapshot();
                    for (const auto& [name, value] : variables) {
                        if (ImGui::Selectable(name.c_str(), node.keyword == name)) node.keyword = name;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.c_str());
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText("比较##runtimeVariableExpr", exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "RuntimeNumberVariable") {
                ImGui::SameLine();
                char varBuf[64]; strncpy_s(varBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##numberVar", varBuf, 64)) node.keyword = varBuf;
                ImGui::SameLine();
                if (ImGui::BeginCombo("##numberVarPick", "数值变量")) {
                    auto variables = ConfigManager::GetSingleton()->GetRuntimeNumberVariablesSnapshot();
                    for (const auto& [name, value] : variables) {
                        if (ImGui::Selectable(name.c_str(), node.keyword == name)) node.keyword = name;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.c_str());
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText("比较##runtimeNumberVariableExpr", exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "NodeMonitor") {
                ImGui::SameLine();
                char nodeBuf[96]; strncpy_s(nodeBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("节点名##nodeMonitorCond", nodeBuf, 96)) node.keyword = nodeBuf;
                ImGui::SameLine();
                static const char* monitorModes[] = {
                    "对象 (可见)", "节点 (可见)", "几何体 (可见)", "节点含几何子项",
                    "节点含递归几何", "对象 (包含隐藏)", "节点 (包含隐藏)", "递归节点含几何 (包含隐藏)"
                };
                static const char* monitorModeValues[] = {
                    "Object", "Node", "Geometry", "NodeWithGeometryChild",
                    "RecursiveNodeWithGeometryChild", "ObjectIncludeInvisible", "NodeIncludeInvisible", "RecursiveNodeWithGeometryChildIncludeInvisible"
                };
                int modeIndex = 0;
                for (int i = 0; i < IM_ARRAYSIZE(monitorModeValues); ++i) {
                    if (node.keyword2 == monitorModeValues[i]) {
                        modeIndex = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("类型##nodeMonitorMode", &modeIndex, monitorModes, IM_ARRAYSIZE(monitorModes))) {
                    node.keyword2 = monitorModeValues[modeIndex];
                }
                ImGui::SameLine();
                if (ImGui::BeginCombo("##nodeMonitorPick", "监控")) {
                    auto monitorNames = ConfigManager::GetSingleton()->GetNodeMonitorNamesSnapshot();
                    for (const auto& name : monitorNames) {
                        if (!name.empty() && ImGui::Selectable(name.c_str(), node.keyword == name)) {
                            node.keyword = name;
                        }
                    }
                    if (!ImGuiManager::s_selectedNode.empty() || !ImGuiManager::s_selectedSlot.empty()) {
                        ImGui::Separator();
                    }
                    if (!ImGuiManager::s_selectedNode.empty()) {
                        const auto label = std::string("当前 Node: ") + ImGuiManager::s_selectedNode;
                        if (ImGui::Selectable(label.c_str(), node.keyword == ImGuiManager::s_selectedNode)) {
                            node.keyword = ImGuiManager::s_selectedNode;
                        }
                    }
                    if (!ImGuiManager::s_selectedSlot.empty()) {
                        const auto movName = std::string("IAD_MOV_") + ImGuiManager::s_selectedSlot;
                        const auto label = std::string("当前 Slot MOV: ") + movName;
                        if (ImGui::Selectable(label.c_str(), node.keyword == movName)) {
                            node.keyword = movName;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else if (node.type == "SkeletonPathContains") {
                ImGui::SameLine();
                char pathBuf[128]; strncpy_s(pathBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::InputText("路径包含##skeletonPathContainsCond", pathBuf, 128)) node.keyword = pathBuf;
            }
            else if (node.type == "PlayerEnemiesNearby") {
                ImGui::SameLine();
                float radius = 4096.0f;
                try { if (!node.keyword.empty()) radius = std::stof(node.keyword); } catch (...) {}
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputFloat("半径##playerEnemiesNearbyRadius", &radius, 128.0f, 512.0f, "%.0f")) {
                    radius = std::clamp(radius, 128.0f, 65536.0f);
                    node.keyword = std::to_string(radius);
                }
            }
            else if (node.type == "KeyBindState") {
                ImGui::SameLine();
                char keyBuf[64]; strncpy_s(keyBuf, node.keyword.empty() ? "F8" : node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::InputText("按键绑定名称##keyBindStateKey", keyBuf, 64)) node.keyword = keyBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText("状态##keyBindStateExpr", exprBuf, 32)) node.keyword2 = exprBuf;
				ImGui::TextDisabled("使用 IAD 设置中的命名按键绑定；旧配置的 F1/F8 等直接键名仍兼容。状态 0 为默认，示例：==1。 ");
            }
            else if (node.type == "IsTimeOfDay") {
                ImGui::SameLine();
                float startHour = 0.0f;
                float endHour = 24.0f;
                try { if (!node.keyword.empty()) startHour = std::stof(node.keyword); } catch (...) {}
                try { if (!node.keyword2.empty()) endHour = std::stof(node.keyword2); } catch (...) {}
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputFloat("Start##todStart", &startHour, 0.25f, 1.0f, "%.2f")) {
                    startHour = std::clamp(startHour, 0.0f, 24.0f);
                    node.keyword = std::to_string(startHour);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputFloat("End##todEnd", &endHour, 0.25f, 1.0f, "%.2f")) {
                    endHour = std::clamp(endHour, 0.0f, 24.0f);
                    node.keyword2 = std::to_string(endHour);
                }
            }
            else if (node.type == "SunAngle") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? "<1.5708" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("弧度##sunAngleExpr", exprBuf, 32)) node.keyword = exprBuf;
                ImGui::SameLine();
                bool absolute = node.keyword2 == "abs";
                if (ImGui::Checkbox("abs##sunAngleAbs", &absolute)) {
                    node.keyword2 = absolute ? "abs" : "";
                }
            }
            else if (node.type == "TimeOfDayPhase") {
                ImGui::SameLine();
                const char* phases[] = { "Night", "Sunrise", "Day", "Sunset" };
                int currentPhase = 2;
                for (int i = 0; i < static_cast<int>(std::size(phases)); ++i) {
                    if (node.keyword == phases[i]) {
                        currentPhase = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("阶段##timeOfDayPhaseCond", &currentPhase, phases, static_cast<int>(std::size(phases)))) {
                    node.keyword = phases[currentPhase];
                }
            }
            else if (node.type == "WeatherClass") {
                ImGui::SameLine();
                const char* weatherClasses[] = { "Pleasant", "Cloudy", "Rainy", "Snow", "Precipitation" };
                int currentWeatherClass = 0;
                for (int i = 0; i < static_cast<int>(std::size(weatherClasses)); ++i) {
                    if (node.keyword == weatherClasses[i]) {
                        currentWeatherClass = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("天气类型##weatherClassCond", &currentWeatherClass, weatherClasses, static_cast<int>(std::size(weatherClasses)))) {
                    node.keyword = weatherClasses[currentWeatherClass];
                }
            }
            else if (node.type == "InteriorAmbientLightLevel") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? "<0.425" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("ambient##interiorAmbientExpr", exprBuf, 32)) node.keyword = exprBuf;
            }
            else if (node.type == "HasKeywordEquipped" || node.type == "ActorHasKeyword" || node.type == "CandidateHasKeyword") { ImGui::SameLine(); DrawKeywordScannerBox("##kw", node.keyword); }
            else if (node.type == "HasEquippedBipedSlot") {
                ImGui::SameLine(); int slot = 33; if (!node.keyword.empty()) { try { slot = std::stoi(node.keyword); } catch (...) {} }
                ImGui::SetNextItemWidth(90.0f); if (ImGui::InputInt("##slotInt", &slot, 1, 1)) { if (slot < 30) slot = 30; if (slot > 61) slot = 61; node.keyword = std::to_string(slot); }
            }
            else if (node.type == "HasEquippedFormID") {
                ImGui::SameLine(); if (ImGui::Button("编辑ID...")) ImGui::OpenPopup("EditFormID");
                if (ImGui::BeginPopup("EditFormID")) {
                    char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str()); if (ImGui::InputText("目标 FormID (Hex)", formBuf, 32)) node.keyword = formBuf;
                    char omodBuf[32]; strcpy_s(omodBuf, node.keyword2.c_str()); if (ImGui::InputText("所需 OMOD (Hex)", omodBuf, 32)) node.keyword2 = omodBuf;
                    ImGui::EndPopup();
                }
            }
            else if (node.type == "ActorFormID" || node.type == "ActorBaseFormID" || node.type == "ActorRaceFormID" ||
                node.type == "ActorCombatStyle" || node.type == "ActorClass" ||
                node.type == "ActorInFaction" || node.type == "ActorInCell" || node.type == "ActorInLocation" ||
                node.type == "ActorInLocationOrChild" || node.type == "ActorInWorldspace" ||
                node.type == "CurrentWeather" || node.type == "LightingTemplate" ||
                node.type == "HasActiveEffect" || node.type == "HasMagicEffect" || node.type == "HasSpell") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("目标 FormID##actorFormCond", formBuf, 32)) node.keyword = formBuf;
            }
            else if (node.type == "GlobalValue") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText("Global FormID##globalFormCond", formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText("比较##globalExpr", exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "ActorLevel") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? ">=1" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("等级比较##actorLevelExpr", exprBuf, 32)) node.keyword = exprBuf;
            }
            else if (node.type == "ActorValue") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText("AVIF FormID##actorValueFormCond", formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText("比较##actorValueExpr", exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "ActorPerkRank" || node.type == "QuestStage") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                const char* formLabel = node.type == "QuestStage" ? "Quest FormID##questStageFormCond" : "Perk FormID##actorPerkFormCond";
                if (ImGui::InputText(formLabel, formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">=1" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                const char* expressionLabel = node.type == "QuestStage" ? "阶段##questStageExpr" : "Rank##actorPerkRankExpr";
                if (ImGui::InputText(expressionLabel, exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "DayOfWeek") {
                ImGui::SameLine();
                const char* days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
                int currentDay = 0;
                for (int i = 0; i < static_cast<int>(std::size(days)); ++i) {
                    if (node.keyword == days[i]) {
                        currentDay = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("星期##dayOfWeekCond", &currentDay, days, static_cast<int>(std::size(days)))) {
                    node.keyword = days[currentDay];
                }
            }
            else if (node.type == "ActorLifeState") {
                ImGui::SameLine();
                const char* states[] = { "Alive", "Dying", "Dead", "Unconscious", "Reanimate", "Recycle", "Restrained", "EssentialDown", "Bleedout" };
                int currentState = 0;
                for (int i = 0; i < static_cast<int>(std::size(states)); ++i) {
                    if (node.keyword == states[i]) {
                        currentState = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::Combo("生命状态##actorLifeStateCond", &currentState, states, static_cast<int>(std::size(states)))) {
                    node.keyword = states[currentState];
                }
            }
            else if (node.type == "RandomPercent") {
                ImGui::SameLine();
                float percent = 100.0f;
                try { if (!node.keyword.empty()) percent = std::stof(node.keyword); } catch (...) {}
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputFloat("概率%##randomPercent", &percent, 1.0f, 10.0f, "%.1f")) {
                    percent = std::clamp(percent, 0.0f, 100.0f);
                    node.keyword = std::to_string(percent);
                }
                ImGui::SameLine();
                char seedBuf[64]; strcpy_s(seedBuf, node.keyword2.c_str());
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("Seed##randomPercentSeed", seedBuf, 64)) node.keyword2 = seedBuf;
            }
            else if (node.type == "CandidateFormID") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("候选 FormID##candidateForm", formBuf, 32)) node.keyword = formBuf;
            }
            else if (node.type == "CandidateHasOMOD") {
                ImGui::SameLine();
                char omodBuf[32]; strcpy_s(omodBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("候选 OMOD##candidateOmod", omodBuf, 32)) node.keyword = omodBuf;
            }
            else if (node.type == "InventoryItemCount") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText("物品 FormID##inventoryItemCountForm", formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText("数量##inventoryItemCountExpr", exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "CandidateCount" || node.type == "CandidateInventoryCount") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? ">0" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText(node.type == "CandidateCount" ? "堆叠数量##candidateCountExpr" : "背包总数##candidateInventoryCountExpr", exprBuf, 32)) node.keyword = exprBuf;
            }
            else if (node.type == "CandidateFormType") {
                ImGui::SameLine();
                const char* formTypes[] = { "WEAP", "ARMO", "AMMO", "ALCH", "MISC" };
                int curFormType = 0;
                for (int i = 0; i < static_cast<int>(std::size(formTypes)); ++i) {
                    if (node.keyword == formTypes[i]) {
                        curFormType = i;
                        break;
                    }
                }
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::Combo("##candidateFormType", &curFormType, formTypes, static_cast<int>(std::size(formTypes)))) {
                    node.keyword = formTypes[curFormType];
                }
            }
        }

        if (node.isGroup && isNodeOpen) {
            for (int i = 0; i < (int)node.children.size(); ) {
                if (DrawConditionNodeRecursive(node.children[i], false, depth + 1)) { node.children.erase(node.children.begin() + i); }
                else { i++; }
            }
        }
        if (isNodeOpen) ImGui::TreePop();
        ImGui::PopID(); return deleteMe;
    }

    static void AppendConditionTreeSignature(std::string& out, const IAD::ConditionNode& node) {
        out.push_back(node.isGroup ? 'G' : 'C');
        out.push_back(node.isAnd ? 'A' : 'O');
        out.push_back(node.isNot ? 'N' : 'P');
        out.push_back(node.expected ? 'T' : 'F');

        auto appendString = [&out](const std::string& value) {
            out += std::to_string(value.size());
            out.push_back(':');
            out += value;
            out.push_back('|');
        };

        appendString(node.type);
        appendString(node.keyword);
        appendString(node.keyword2);
        out += std::to_string(node.children.size());
        out.push_back('[');
        for (const auto& child : node.children) {
            AppendConditionTreeSignature(out, child);
        }
        out.push_back(']');
    }

    static std::string BuildConditionTreeSignature(const IAD::ConditionNode& node) {
        std::string signature;
        AppendConditionTreeSignature(signature, node);
        return signature;
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
        out += BuildConditionTreeSignature(group.displayConditionTree);

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

    static bool DrawModelCleanupSettings(bool& disableHavok, bool& removeEditorMarker, bool& removeProjectileTracers, const char* idSuffix) {
        bool changed = false;
        (void)disableHavok;
        ImGui::PushID(idSuffix);
        ImGui::TextDisabled("Havok/碰撞：展示模型始终为渲染副本");
        if (ImGui::Checkbox("Remove editor markers", &removeEditorMarker)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strip editor marker/helper nodes from the cloned model.");
        if (ImGui::Checkbox("Remove projectile tracers/lights", &removeProjectileTracers)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strip projectile tracer and related light nodes from the cloned model.");
        ImGui::PopID();
        return changed;
    }

    static bool DrawModelAnimationSettings(ModelAnimationConfig& animation, const char* idSuffix) {
        bool changed = false;
        ImGui::PushID(idSuffix);
        if (ImGui::TreeNodeEx("主模型动画 / Sequence", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("播放 NiController Sequence", &animation.playSequence)) changed = true;
            if (ImGui::Checkbox("转发动画事件", &animation.forwardAnimationEvents)) changed = true;
            ImGui::SameLine();
            ImGui::TextDisabled("SubGraphs：FO4 展示副本不支持");
            if (ImGui::Checkbox("禁用行为图动画", &animation.disableBehaviorGraphAnims)) changed = true;
            char seqBuf[128];
            strcpy_s(seqBuf, animation.sequenceName.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText("Sequence 名称", seqBuf, 128)) {
                animation.sequenceName = seqBuf;
                changed = true;
            }
            char eventBuf[128];
            strcpy_s(eventBuf, animation.animationEvent.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText("动画事件", eventBuf, 128)) {
                animation.animationEvent = eventBuf;
                changed = true;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawModelEffectShaderSettings(ModelEffectShaderConfig& effect, const char* idSuffix) {
        bool changed = false;
        ImGui::PushID(idSuffix);
        if (ImGui::TreeNodeEx("主模型 Effect Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("启用效果数据", &effect.enabled)) changed = true;
            if (effect.enabled) {
                ImGui::Indent();
                if (ImGui::Checkbox("Target Root", &effect.targetRoot)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Force", &effect.force)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Lighting", &effect.lighting)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Alpha", &effect.alpha)) changed = true;
                changed |= DrawColorRGBA("Fill Color", effect.fillColor);
                changed |= DrawColorRGBA("Rim Color", effect.rimColor);
                if (ImGui::DragFloat("透明度倍率", &effect.alphaMultiplier, 0.01f, 0.0f, 1.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Fill Scale", &effect.baseFillScale, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Fill Alpha", &effect.baseFillAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Rim Alpha", &effect.baseRimAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) changed = true;
                if (ImGui::DragFloat("Edge Exponent", &effect.edgeExponent, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                char texBuf[256];
                strcpy_s(texBuf, effect.baseTexturePath.c_str());
                ImGui::SetNextItemWidth(420.0f);
                if (ImGui::InputText("Base Texture", texBuf, 256)) {
                    effect.baseTexturePath = texBuf;
                    changed = true;
                }
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawModelLightSettings(ModelLightConfig& light, const char* idSuffix) {
        bool changed = false;
        ImGui::PushID(idSuffix);
        if (ImGui::TreeNodeEx("主模型 Extra Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("启用附加灯光数据", &light.enabled)) changed = true;
            if (light.enabled) {
                ImGui::Indent();
                ImGui::TextDisabled("Target/Water/Landscape/Shadows：当前渲染路径不支持");
                changed |= DrawColorRGBA("Diffuse", light.diffuse);
                if (ImGui::DragFloat("Radius", &light.radius, 1.0f, 0.0f, 4096.0f, "%.1f")) changed = true;
                if (ImGui::DragFloat("Dimmer", &light.dimmer, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                ImGui::TextDisabled("FOV/Shadow Bias：当前渲染路径不支持");
                changed |= DrawIEDTransformWidget("灯光位置偏移", light.transform.pos, { 0,0,0 }, 0.5f, false);
                changed |= DrawIEDTransformWidget("灯光旋转", light.transform.rot, { 0,0,0 }, 1.0f, true);
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
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
        out += BuildConditionTreeSignature(slot.itemFilterConditionTree);
        out += slot.extractMagazine ? '1' : '0';
        out += slot.useProjectileForAmmo ? '1' : '0';
        out += slot.removeEditorMarker ? '1' : '0';
        out += slot.removeProjectileTracers ? '1' : '0';
        out += slot.useWorldModel ? '1' : '0';
        out += slot.invisible ? '1' : '0';
        out += slot.hideGeometry ? '1' : '0';
        out += slot.hideLight ? '1' : '0';
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
        const auto beforeSignature = BuildConditionTreeSignature(rootNode);

        if (showProfileControls) {
            if (DrawManagedProfileSelector(
                "ConditionProfileSelector",
                "条件预设 (Condition Profile)",
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
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::Button("Reset to empty##conditionProfile")) {
                rootNode = ConditionNode(true, true);
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Separator();
        }

        ImGui::TextDisabled("条件组可包含子组；组内可选择“任意满足”或“全部满足”。展开条件后再编辑类型和参数。");
        ImGui::Separator();
        DrawConditionNodeRecursive(rootNode, true, 0);

        const bool changed = beforeSignature != BuildConditionTreeSignature(rootNode);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
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
        ImGui::InputText("预设名", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            a_value = nameBuf;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const char* preview = a_value.empty() ? a_emptyLabel : a_value.c_str();
        if (ImGui::BeginCombo("选择预设", preview, ImGuiComboFlags_HeightLarge)) {
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
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "状态覆写 (State Overrides)");
        if (a_isNodeState) {
            ImGui::TextDisabled("条件命中时可隐藏节点、切换宿主骨骼、覆写节点姿态/物理。");
        }
        else {
            ImGui::TextDisabled("条件命中时可隐藏模型、切换目标挂点、覆写模型姿态，或临时改用指定 NIF。");
        }

        if (ImGui::Button("添加状态覆写##addState", { 160.0f, 0.0f })) {
            StateOverride state;
            state.description = "新状态";
            a_config.stateMachine.push_back(state);
            changed = true;
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }

		int deleteIndex = -1;
		int moveIndex = -1;
		int moveDelta = 0;
		for (int i = 0; i < static_cast<int>(a_config.stateMachine.size()); ++i) {
			auto& state = a_config.stateMachine[i];
			ImGui::PushID(i);
            const std::string label = state.description.empty() ? ("状态覆写 " + std::to_string(i + 1)) : state.description;
            if (ImGui::TreeNodeEx("stateOverride", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str())) {
                char descBuf[128];
                strcpy_s(descBuf, state.description.c_str());
                ImGui::SetNextItemWidth(260.0f);
                ImGui::InputText("描述", descBuf, sizeof(descBuf));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    state.description = descBuf;
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }

                if (ImGui::Checkbox("隐藏模型", &state.hideModel)) {
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }

                if (ImGui::Checkbox("命中后继续评估后续状态", &state.continueAfterMatch)) {
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }

                if (!a_isNodeState) {
                    if (ImGui::Checkbox("覆写模型替换路径", &state.overrideModelSwap)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideModelSwap) {
                        ImGui::Indent();
                        char pathBuf[260];
                        strcpy_s(pathBuf, state.modelSwapPath.c_str());
                        ImGui::SetNextItemWidth(440.0f);
                        ImGui::InputText("模型路径", pathBuf, sizeof(pathBuf));
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            state.modelSwapPath = pathBuf;
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (DrawFormIDField("模型 FormID", state.modelSwapFormID)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (DrawModelSwapVariableSource(state.modelSwapVariableSource, "state_model_var_source")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::TextDisabled("路径优先于 FormID；两者为空时清除前面的模型替换。");
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox("覆写模型显示/清理开关", &state.overrideDisplayFlags)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideDisplayFlags) {
                        ImGui::Indent();
                        if (ImGui::Checkbox("提取武器弹匣", &state.extractMagazine)) {
                            if (state.extractMagazine) state.useProjectileForAmmo = false;
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (state.extractMagazine) ImGui::BeginDisabled();
                        if (ImGui::Checkbox("弹药改用射弹模型", &state.useProjectileForAmmo)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (state.extractMagazine) ImGui::EndDisabled();
                        if (ImGui::Checkbox("使用世界/基础模型", &state.useWorldModel)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("隐藏几何 / alpha 0", &state.invisible)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("仅隐藏几何，保留挂载节点", &state.hideGeometry)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("隐藏附加灯光", &state.hideLight)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("加载第一人称武器模型", &state.load1pWeaponModel)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("保留火焰/喷焰 FX", &state.keepTorchFlame)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (ImGui::Checkbox("移除刀鞘/枪套节点", &state.removeScabbard)) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (DrawModelCleanupSettings(state.disableHavok, state.removeEditorMarker, state.removeProjectileTracers, "state_display_cleanup")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::TextDisabled("开启后这些值会替换 Slot/Custom 的对应模型显示与清理设置。");
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox("覆写模型动画", &state.overrideAnimation)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideAnimation) {
                        ImGui::Indent();
                        if (DrawModelAnimationSettings(state.animation, "state_animation")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox("覆写模型 Effect Shader", &state.overrideEffectShader)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideEffectShader) {
                        ImGui::Indent();
                        if (DrawModelEffectShaderSettings(state.effectShader, "state_effect")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox("覆写模型附加灯光", &state.overrideLight)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideLight) {
                        ImGui::Indent();
                        if (DrawModelLightSettings(state.light, "state_light")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::Unindent();
                    }
                }

                if (ImGui::Checkbox("覆写目标挂点", &state.overrideTargetNode)) {
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (state.overrideTargetNode) {
                    ImGui::Indent();
                    char nodeBuf[128];
                    strcpy_s(nodeBuf, state.targetNode.c_str());
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("目标节点", nodeBuf, sizeof(nodeBuf));
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        state.targetNode = nodeBuf;
                        changed = true;
                        NodeManager::ClearAllCaches();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::Unindent();
                }

                if (ImGui::Checkbox("覆写姿态", &state.overrideTransform)) {
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (state.overrideTransform) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("使用姿态预设", &state.useTransformPreset)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.useTransformPreset) {
                        if (DrawProfileNameSelector("stateTransformPreset", IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(), state.targetTransformPreset, "[未选择]")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                    }
                    else {
                        ImGui::PushID("stateTransform");
                        TransformData defaultTransform;
                        bool transformChanged = false;
                        transformChanged |= DrawIEDTransformWidget("位置", state.independentTransform.pos, defaultTransform.pos, 0.04f, false);
                        transformChanged |= DrawIEDTransformWidget("旋转", state.independentTransform.rot, defaultTransform.rot, 0.04f, true);
                        transformChanged |= DrawIEDTransformWidget("轴心", state.independentTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("缩放", &state.independentTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            transformChanged = true;
                        }
                        if (transformChanged) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::PopID();
                    }
                    if (a_isNodeState) {
                        if (ImGui::Checkbox("绝对坐标", &state.absolutePosition)) {
                            changed = true;
                            NodeManager::ClearAllCaches();
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("武器/身形自适应：当前变换路径不支持");
                    }
                    ImGui::Unindent();
                }

                if (!a_isNodeState) {
                    if (ImGui::Checkbox("覆写模型网格变换", &state.overrideMeshTransform)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideMeshTransform) {
                        ImGui::Indent();
                        ImGui::PushID("stateMeshTransform");
                        TransformData defaultTransform;
                        bool meshChanged = false;
                        meshChanged |= DrawIEDTransformWidget("位置", state.independentMeshTransform.pos, defaultTransform.pos, 0.04f, false);
                        meshChanged |= DrawIEDTransformWidget("旋转", state.independentMeshTransform.rot, defaultTransform.rot, 0.04f, true);
                        meshChanged |= DrawIEDTransformWidget("轴心", state.independentMeshTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("缩放", &state.independentMeshTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            meshChanged = true;
                        }
                        if (meshChanged) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::PopID();
                        ImGui::Unindent();
                    }

                    if (ImGui::Checkbox("覆写几何层变换", &state.overrideGeometryTransform)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.overrideGeometryTransform) {
                        ImGui::Indent();
                        ImGui::PushID("stateGeometryTransform");
                        TransformData defaultTransform;
                        bool geometryChanged = false;
                        geometryChanged |= DrawIEDTransformWidget("几何位置", state.independentGeometryTransform.pos, defaultTransform.pos, 0.04f, false);
                        geometryChanged |= DrawIEDTransformWidget("几何旋转", state.independentGeometryTransform.rot, defaultTransform.rot, 0.04f, true);
                        geometryChanged |= DrawIEDTransformWidget("几何轴心", state.independentGeometryTransform.pivot, defaultTransform.pivot, 0.04f, false);
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("几何缩放", &state.independentGeometryTransform.scale, 0.01f, 0.01f, 10.0f, "%.3f")) {
                            geometryChanged = true;
                        }
                        if (geometryChanged) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::PopID();
                        ImGui::Unindent();
                    }
                }

                if (ImGui::Checkbox("覆写物理", &state.overridePhysics)) {
                    changed = true;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (state.overridePhysics) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("使用物理预设", &state.usePhysicsPreset)) {
                        changed = true;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (state.usePhysicsPreset) {
                        if (DrawProfileNameSelector("statePhysicsPreset", IAD::Profile::GlobalProfileManager::GetSingleton().Physics(), state.targetPhysicsPreset, "[未选择]")) {
                            changed = true;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                    }
                    else if (ImGui::TreeNodeEx("独立物理参数", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (DrawPhysicsPanel(state.independentPhysics)) {
                            changed = true;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "命中条件:");
                if (DrawConditionTreeEditor(state.conditionTree)) {
                    changed = true;
                }

				if (i <= 0) ImGui::BeginDisabled();
				if (ImGui::Button("上移##moveStateUp", { 70.0f, 0.0f })) {
					moveIndex = i;
					moveDelta = -1;
				}
				if (i <= 0) ImGui::EndDisabled();
				ImGui::SameLine();
				if (i >= static_cast<int>(a_config.stateMachine.size()) - 1) ImGui::BeginDisabled();
				if (ImGui::Button("下移##moveStateDown", { 70.0f, 0.0f })) {
					moveIndex = i;
					moveDelta = 1;
				}
				if (i >= static_cast<int>(a_config.stateMachine.size()) - 1) ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("删除状态覆写##deleteState", { 140.0f, 0.0f })) {
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
				HolsterManager::GetSingleton()->ForceRefreshAll();
			}
		}

		if (deleteIndex >= 0 && deleteIndex < static_cast<int>(a_config.stateMachine.size())) {
			a_config.stateMachine.erase(a_config.stateMachine.begin() + deleteIndex);
            changed = true;
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawIEDTransformWidget(const char* label, RE::NiPoint3& val, const RE::NiPoint3& defaultVal, float baseSpeed, bool isRotation) {
        auto* hm = HolsterManager::GetSingleton(); bool changed = false;
        ImGui::PushID(label); ImGui::Text("%s", label);
        if (ImGui::BeginPopupContextItem("TransformContextMenu")) { if (ImGui::MenuItem("🔄 重置到默认值 (Reset)")) { val = defaultVal; changed = true; ImGui::CloseCurrentPopup(); } ImGui::EndPopup(); }
        bool isDefault = (std::abs(val.x - defaultVal.x) < 0.001f && std::abs(val.y - defaultVal.y) < 0.001f && std::abs(val.z - defaultVal.z) < 0.001f);
        if (isDefault) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
        float dragSpeed = ImGui::GetIO().KeyShift ? baseSpeed * 0.1f : baseSpeed; float spacing = ImGui::GetStyle().ItemSpacing.x; float totalW = ImGui::GetContentRegionAvail().x; float itemW = (totalW - spacing * 2.0f) / 3.0f;
        auto HandleActive = [&](ActiveAxis axis) { if (ImGui::IsItemActive()) { hm->activeUIItemAxis = axis; ImGuiManager::s_activeUIIsRotation = isRotation; } };

        ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 0.5f, 0.5f, 1.0f }); ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.2f, 0.1f, 0.1f, 1.0f }); ImGui::SetNextItemWidth(itemW);
        if (ImGui::DragFloat("##x", &val.x, dragSpeed, 0, 0, "X: %.3f")) changed = true; HandleActive(ActiveAxis::kX); ImGui::PopStyleColor(2); ImGui::SameLine(0, spacing);
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 1.0f, 0.5f, 1.0f }); ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.2f, 0.1f, 1.0f }); ImGui::SetNextItemWidth(itemW);
        if (ImGui::DragFloat("##y", &val.y, dragSpeed, 0, 0, "Y: %.3f")) changed = true; HandleActive(ActiveAxis::kY); ImGui::PopStyleColor(2); ImGui::SameLine(0, spacing);
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.8f, 1.0f, 1.0f }); ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0.1f, 0.15f, 0.2f, 1.0f }); ImGui::SetNextItemWidth(itemW);
        if (ImGui::DragFloat("##z", &val.z, dragSpeed, 0, 0, "Z: %.3f")) changed = true; HandleActive(ActiveAxis::kZ); ImGui::PopStyleColor(2);
        if (isDefault) ImGui::PopStyleVar(); ImGui::PopID(); return changed;
    }

    static bool DrawWindowHeader(const char* idStr, ImGuiManager::WindowState& state, bool showTarget, bool showGender) {
        bool propagateTriggered = false;
        ImGui::PushID(idStr);

        // Match IED's editor hierarchy: scope first, then the active scope context.
        // Set the initial Global tab exactly once; continuous SetSelected calls would
        // override user clicks on the other scope tabs.
        auto drawScopeTab = [&state](const char* label, ConfigScope scope) {
            const auto flags = !state.scopeTabInitialized && state.scope == scope ?
                ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label, nullptr, flags)) {
                state.scope = scope;
				state.scopeTabInitialized = true;
                ImGui::EndTabItem();
            }
        };

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
        if (ImGui::BeginTabBar("ScopeTabBar", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoTooltip)) {
            drawScopeTab("种族", ConfigScope::kRace);
            drawScopeTab("NPC", ConfigScope::kNPC);
            drawScopeTab("角色", ConfigScope::kActor);
            drawScopeTab("全局", ConfigScope::kGlobal);
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 0.5f));
        ImGui::BeginChild("ScopeContext", ImVec2(0, 38), true);
        if (state.scope == ConfigScope::kGlobal) {
            if (showTarget) {
                ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "目标:"); ImGui::SameLine();
                ImGui::RadioButton("全部实体", &state.targetFilter, 0); ImGui::SameLine(0, 15.0f);
                ImGui::RadioButton("仅限玩家", &state.targetFilter, 1); ImGui::SameLine(0, 15.0f);
                ImGui::RadioButton("仅限 NPC", &state.targetFilter, 2);
            }
            else { ImGui::TextDisabled("全局配置会作为所有实体的最终兜底。 "); }
        }
        else {
            ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "目标 FormID:"); ImGui::SameLine();
            char buf[16]; sprintf_s(buf, "%08X", state.id); ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##IDInput", buf, 16, ImGuiInputTextFlags_CharsHexadecimal)) { try { state.id = std::stoul(buf, nullptr, 16); } catch (...) { state.id = 0; } } ImGui::SameLine();

            if (state.scope == ConfigScope::kRace) {
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("##raceCombo", "点击选择游戏种族...")) {
                    auto dataHandler = RE::TESDataHandler::GetSingleton();
                    if (dataHandler) {
                        for (auto race : dataHandler->GetFormArray<RE::TESRace>()) {
                            if (race && race->GetFormID() > 0) {
                                char rLabel[128]; sprintf_s(rLabel, "[%08X] %s", race->GetFormID(), race->GetFormEditorID());
                                if (ImGui::Selectable(rLabel)) state.id = race->GetFormID();
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else {
                std::string popupName = std::string("ScannerPopup##") + std::string(idStr);
                if (ImGui::Button("🔍 扫描附近角色")) ImGui::OpenPopup(popupName.c_str());
                if (ImGui::BeginPopup(popupName.c_str())) {
                    auto player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        auto pPos = player->GetPosition(); uint32_t pid = (state.scope == ConfigScope::kNPC && player->data.objectReference ? player->data.objectReference->GetFormID() : player->GetFormID());
                        if (ImGui::Selectable(("[玩家本身] " + std::string(player->GetDisplayFullName())).c_str())) state.id = pid; ImGui::Separator();
                        if (auto pl = RE::ProcessLists::GetSingleton()) {
                            for (auto& handle : pl->highActorHandles) {
                                auto act = handle.get();
                                if (act && act.get() && act.get() != player) {
                                    float dx = pPos.x - act->GetPosition().x; float dy = pPos.y - act->GetPosition().y; float dz = pPos.z - act->GetPosition().z; float dist = std::sqrtf(dx * dx + dy * dy + dz * dz);
                                    if (dist < 2500.0f) {
                                        uint32_t cid = (state.scope == ConfigScope::kNPC && act->data.objectReference ? act->data.objectReference->GetFormID() : act->GetFormID());
                                        char label[128]; sprintf_s(label, "[距离:%d] %s", (int)dist, act->GetDisplayFullName());
                                        if (ImGui::Selectable(label)) state.id = cid;
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::EndChild(); ImGui::PopStyleColor();

        if (showGender) {
            ImGui::TextColored({ 1.0f, 0.6f, 0.8f, 1.0f }, "性别:"); ImGui::SameLine();
            ImGui::RadioButton("男性", &state.genderEdit, 0); ImGui::SameLine(0, 12.0f);
            ImGui::RadioButton("女性", &state.genderEdit, 1); ImGui::SameLine(0, 18.0f);
            ImGui::Checkbox("同步至另一性别", &state.syncGender);
            if (state.scope != ConfigScope::kGlobal) {
                ImGui::SameLine();
                if (ImGui::Button("向上层传播")) propagateTriggered = true;
            }
        }
        ImGui::Separator();
        ImGui::Spacing();
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
            label += " - [未加载]";
        }
        return label;
    }

    static void DrawConfiguredTargetBrowser(const char* idStr, ImGuiManager::WindowState& state, const std::vector<std::uint32_t>& configuredIDs) {
        if (state.scope == ConfigScope::kGlobal) return;

        ImGui::PushID(idStr);
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "已配置目标 (Configured Targets):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        std::string preview = configuredIDs.empty() ? "当前层暂无配置目标" : FormatScopedTargetLabel(state.scope, state.id);
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
        ImGui::TextDisabled("%zu 个目标", configuredIDs.size());
        ImGui::Spacing();
        ImGui::PopID();
    }

    static bool DrawTransformProfileControls(const char* idStr, TransformData& transform) {
        const bool changed = DrawManagedProfileSelector(
            idStr,
            "姿态预设 (Transform Profile)",
            IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(),
            transform,
            "MyTransform",
            [](TransformData& target, const TransformData& source) {
                target = source;
            },
            [](TransformData&, const TransformData&) {},
            false);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        return changed;
    }

    static bool DrawPhysicsProfileControls(const char* idStr, PhysicsValues& physics) {
        const bool changed = DrawManagedProfileSelector(
            idStr,
            "物理预设 (Physics Profile)",
            IAD::Profile::GlobalProfileManager::GetSingleton().Physics(),
            physics,
            "MyPhysics",
            [](PhysicsValues& target, const PhysicsValues& source) {
                target = source;
            },
            [](PhysicsValues&, const PhysicsValues&) {},
            false);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
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

    static void DrawFormSetUI(std::set<std::uint32_t>& a_set, const char* a_id) {
        ImGui::PushID(a_id);
        if (ImGui::BeginPopupContextItem("Context_AddForm")) {
            static char hexInput[16] = ""; ImGui::Text("输入 FormID (16进制, 例如 14):"); ImGui::SetNextItemWidth(150.0f); ImGui::InputText("##HexInput", hexInput, 16, ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::Button("添加 (Add)", { -1, 0 })) { if (strlen(hexInput) > 0) { try { uint32_t formID = std::stoul(hexInput, nullptr, 16); a_set.insert(formID); hexInput[0] = '\0'; ImGui::CloseCurrentPopup(); } catch (...) {} } }
            ImGui::EndPopup();
        }
        if (ImGui::Button("⚙️ 操作 (右键此处添加物品)")) ImGui::OpenPopup("Context_AddForm");
        if (ImGui::BeginTable("FormTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 40.0f); ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 80.0f); ImGui::TableSetupColumn("物品名称", ImGuiTableColumnFlags_WidthStretch); ImGui::TableHeadersRow();
            std::uint32_t toRemove = 0; bool doRemove = false;
            for (auto formID : a_set) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(formID); if (ImGui::Button("X")) { toRemove = formID; doRemove = true; }
                ImGui::TableNextColumn(); ImGui::Text("%08X", formID); ImGui::TableNextColumn();
                auto form = RE::TESForm::GetFormByID(formID);
                if (form) { auto fullName = form->As<RE::TESFullName>(); if (fullName && fullName->GetFullName()) ImGui::Text("%s", fullName->GetFullName()); else ImGui::TextDisabled("[未知名称]"); }
                else ImGui::TextColored({ 1,0,0,1 }, "[未加载]");
                ImGui::PopID();
            }
            ImGui::EndTable(); if (doRemove) a_set.erase(toRemove);
        }
        ImGui::PopID();
    }

    static bool DrawFormVectorUI(std::vector<std::uint32_t>& a_vec, const char* a_id) {
        bool changed = false;
        ImGui::PushID(a_id);
        if (ImGui::BeginPopupContextItem("Context_AddFormVec")) {
            static char hexInput[16] = ""; ImGui::Text("输入 FormID (16进制):"); ImGui::SetNextItemWidth(150.0f); ImGui::InputText("##HexInputVec", hexInput, 16, ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::Button("添加 (Add)", { -1, 0 })) { if (strlen(hexInput) > 0) { try { uint32_t formID = std::stoul(hexInput, nullptr, 16); if (std::find(a_vec.begin(), a_vec.end(), formID) == a_vec.end()) { a_vec.push_back(formID); changed = true; } hexInput[0] = '\0'; ImGui::CloseCurrentPopup(); } catch (...) {} } }
            ImGui::EndPopup();
        }
        if (ImGui::Button("⚙️ 操作 (右键此处添加物品)")) ImGui::OpenPopup("Context_AddFormVec");
        if (ImGui::BeginTable("FormVecTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 80.0f); ImGui::TableSetupColumn("排序", ImGuiTableColumnFlags_WidthFixed, 40.0f); ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 80.0f); ImGui::TableSetupColumn("物品名称", ImGuiTableColumnFlags_WidthStretch); ImGui::TableHeadersRow();
            for (size_t i = 0; i < a_vec.size(); ++i) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(i)); if (ImGui::Button("X")) { a_vec.erase(a_vec.begin() + i); changed = true; ImGui::PopID(); break; }
                ImGui::TableNextColumn(); if (ImGui::Button("^") && i > 0) { std::swap(a_vec[i], a_vec[i - 1]); changed = true; } ImGui::SameLine(); if (ImGui::Button("v") && i < a_vec.size() - 1) { std::swap(a_vec[i], a_vec[i + 1]); changed = true; }
                ImGui::TableNextColumn(); ImGui::Text("%08X", a_vec[i]); ImGui::TableNextColumn();
                auto form = RE::TESForm::GetFormByID(a_vec[i]);
                if (form) { auto fullName = form->As<RE::TESFullName>(); if (fullName && fullName->GetFullName()) ImGui::Text("%s", fullName->GetFullName()); else ImGui::TextDisabled("[未知名称]"); }
                else ImGui::TextColored({ 1,0,0,1 }, "[未加载]");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawFormIDField(const char* a_label, std::uint32_t& a_formID) {
        bool changed = false;
        char formBuf[16];
        sprintf_s(formBuf, "%08X", a_formID);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText(a_label, formBuf, 16, ImGuiInputTextFlags_CharsHexadecimal)) {
            try {
                a_formID = std::stoul(formBuf, nullptr, 16);
                changed = true;
            }
            catch (...) {
                a_formID = 0;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (auto form = RE::TESForm::GetFormByID(a_formID)) {
            if (auto fullName = form->As<RE::TESFullName>(); fullName && fullName->GetFullName()) {
                ImGui::TextDisabled("%s", fullName->GetFullName());
            }
            else if (auto editorID = form->GetFormEditorID(); editorID && editorID[0] != '\0') {
                ImGui::TextDisabled("%s", editorID);
            }
            else {
                ImGui::TextDisabled("[已加载]");
            }
        }
        else {
            ImGui::TextDisabled("[未加载]");
        }
        return changed;
    }

    static bool DrawModelSwapVariableSource(ModelSwapVariableSource& source, const char* idSuffix) {
        bool changed = false;
        ImGui::PushID(idSuffix);
        if (ImGui::Checkbox("使用运行时变量源 (IED Variable Source)", &source.enabled)) {
            changed = true;
        }
        if (source.enabled) {
            ImGui::Indent();
            char pathVar[64];
            strncpy_s(pathVar, source.pathVariable.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("路径变量", pathVar, sizeof(pathVar))) {
                source.pathVariable = pathVar;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("##pathVariablePick", "选择")) {
                auto variables = ConfigManager::GetSingleton()->GetRuntimeModelPathVariablesSnapshot();
                for (const auto& [name, value] : variables) {
                    if (ImGui::Selectable(name.c_str(), source.pathVariable == name)) {
                        source.pathVariable = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            char formVar[64];
            strncpy_s(formVar, source.formIDVariable.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("FormID 变量", formVar, sizeof(formVar))) {
                source.formIDVariable = formVar;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("##formVariablePick", "选择")) {
                auto variables = ConfigManager::GetSingleton()->GetRuntimeFormVariablesSnapshot();
                for (const auto& [name, value] : variables) {
                    if (ImGui::Selectable(name.c_str(), source.formIDVariable == name)) {
                        source.formIDVariable = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("路径变量优先；变量无有效值时保留手动模型路径/FormID。");
            ImGui::Unindent();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawStringVectorEditor(const char* a_label, std::vector<std::string>& a_values, const char* a_defaultValue) {
        bool changed = false;
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                ImGui::PushID(i);
                char buf[128];
                strcpy_s(buf, a_values[i].c_str());
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::InputText("##nodeName", buf, 128)) {
                    a_values[i] = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("上移") && i > 0) {
                    std::swap(a_values[i], a_values[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) removeIndex = i;
                ImGui::PopID();
            }
            if (removeIndex >= 0) {
                a_values.erase(a_values.begin() + removeIndex);
                changed = true;
            }
            if (ImGui::Button("添加")) {
                a_values.push_back(a_defaultValue ? a_defaultValue : "");
                changed = true;
            }
            ImGui::TreePop();
        }
        return changed;
    }

    static bool DrawFormTypeVectorEditor(const char* a_label, std::vector<std::uint8_t>& a_values) {
        bool changed = false;
        const char* labels[] = { "WEAP", "ARMO", "AMMO", "ALCH", "MISC" };
        const std::uint8_t types[] = {
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kWEAP),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kARMO),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kAMMO),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kALCH),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kMISC)
        };

        ImGui::PushID(a_label);
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < static_cast<int>(std::size(types)); ++i) {
                bool enabled = std::find(a_values.begin(), a_values.end(), types[i]) != a_values.end();
                if (ImGui::Checkbox(labels[i], &enabled)) {
                    if (enabled) {
                        if (std::find(a_values.begin(), a_values.end(), types[i]) == a_values.end()) {
                            a_values.push_back(types[i]);
                        }
                    }
                    else {
                        a_values.erase(std::remove(a_values.begin(), a_values.end(), types[i]), a_values.end());
                    }
                    changed = true;
                }
                if (i + 1 < static_cast<int>(std::size(types))) {
                    ImGui::SameLine();
                }
            }
            ImGui::NewLine();

            if (!a_values.empty()) {
                ImGui::TextDisabled("Evaluation order:");
                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                    ImGui::PushID(i);
                    ImGui::Text("%d. %s", i + 1, ConfigManager::FormTypeToString(a_values[i]).c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Up") && i > 0) {
                        std::swap(a_values[i], a_values[i - 1]);
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Down") && i + 1 < static_cast<int>(a_values.size())) {
                        std::swap(a_values[i], a_values[i + 1]);
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        removeIndex = i;
                    }
                    ImGui::PopID();
                }
                if (removeIndex >= 0) {
                    a_values.erase(a_values.begin() + removeIndex);
                    changed = true;
                }
            }
            else {
                ImGui::TextDisabled("Empty list uses global recent-acquired form types.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    static std::uint32_t NormalizeUIBipedSlot(int a_value) {
        if (a_value < 0) a_value = 0;
        if (a_value < 32) return static_cast<std::uint32_t>(a_value + 30);
        if (a_value < 30) a_value = 30;
        if (a_value > 61) a_value = 61;
        return static_cast<std::uint32_t>(a_value);
    }

    static bool DrawBipedSlotVectorEditor(const char* a_label, std::vector<std::uint32_t>& a_values) {
        bool changed = false;
        ImGui::PushID(a_label);
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                ImGui::PushID(i);
                int slot = static_cast<int>(a_values[i]);
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputInt("Slot", &slot, 1, 1)) {
                    a_values[i] = NormalizeUIBipedSlot(slot);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("index %u", a_values[i] >= 30 ? a_values[i] - 30 : a_values[i]);
                ImGui::SameLine();
                if (ImGui::Button("Up") && i > 0) {
                    std::swap(a_values[i], a_values[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && i + 1 < static_cast<int>(a_values.size())) {
                    std::swap(a_values[i], a_values[i + 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0) {
                a_values.erase(a_values.begin() + removeIndex);
                changed = true;
            }
            if (ImGui::Button("Add biped slot")) {
                if (std::find(a_values.begin(), a_values.end(), 33u) == a_values.end()) {
                    a_values.push_back(33);
                }
                else {
                    a_values.push_back(30);
                }
                changed = true;
            }
            ImGui::TextDisabled("FO4 biped slots use 30-61. Manual 0-31 input is normalized to 30-61.");
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawSkeletonMatchEditor(NodeDefinition::SkeletonMatchConfig& a_match) {
        bool changed = false;
        if (ImGui::Checkbox("启用骨架匹配", &a_match.enabled)) changed = true;
        if (!a_match.enabled) return changed;

        ImGui::Indent();
        if (ImGui::Checkbox("反向匹配", &a_match.invert)) changed = true;

        char pathBuf[160];
        strcpy_s(pathBuf, a_match.skeletonPathContains.c_str());
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputText("骨架路径包含", pathBuf, 160)) {
            a_match.skeletonPathContains = pathBuf;
            changed = true;
        }

        changed |= DrawFormIDField("Race FormID", a_match.raceFormID);
        changed |= DrawFormIDField("NPC FormID", a_match.npcFormID);
        changed |= DrawStringVectorEditor("必须存在的骨骼节点", a_match.requiredNodes, "SPINE2");
        changed |= DrawStringVectorEditor("不能存在的骨骼节点", a_match.forbiddenNodes, "");
        ImGui::Unindent();
        return changed;
    }

    static bool DrawColorRGBA(const char* a_label, ColorRGBA& a_color) {
        float color[4] = { a_color.r, a_color.g, a_color.b, a_color.a };
        if (ImGui::ColorEdit4(a_label, color)) {
            a_color.r = color[0];
            a_color.g = color[1];
            a_color.b = color[2];
            a_color.a = color[3];
            return true;
        }
        return false;
    }

    static bool DrawModelGroupAdvancedConfig(ModelGroupEntry& a_group) {
        bool changed = false;
        if (ImGui::CollapsingHeader("高级模型组功能", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "模型来源");
            const char* sourceModes[] = { "NIF 路径", "FormID 模型" };
            int sourceMode = std::clamp(a_group.sourceMode, 0, 1);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("来源模式", &sourceMode, sourceModes, 2)) {
                a_group.sourceMode = sourceMode;
                changed = true;
            }
            if (a_group.sourceMode == 1) {
                changed |= DrawFormIDField("来源 FormID", a_group.sourceFormID);
                if (ImGui::Checkbox("从来源武器提取弹匣", &a_group.extractMagazine)) changed = true;
                bool disableProjectile = a_group.extractMagazine;
                if (disableProjectile) ImGui::BeginDisabled();
                if (ImGui::Checkbox("弹药来源使用射弹模型", &a_group.useProjectileForAmmo)) changed = true;
                if (disableProjectile) ImGui::EndDisabled();
            }
            else {
                char pathBuf[256];
                strncpy_s(pathBuf, a_group.modelPath.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(450.0f);
                ImGui::InputText("模型路径 (NIF)", pathBuf, 256);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    a_group.modelPath = pathBuf;
                    changed = true;
                }
            }

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "模型标志");
            ImGui::TextDisabled("Havok/碰撞：展示模型始终为渲染副本");
            ImGui::SameLine();
            if (ImGui::Checkbox("移除编辑器标记", &a_group.removeEditorMarker)) changed = true;
            if (ImGui::Checkbox("移除射弹轨迹/特效节点", &a_group.removeProjectileTracers)) changed = true;
            if (ImGui::Checkbox("隐藏几何 / alpha 0 (Invisible)", &a_group.invisible)) changed = true;
            if (ImGui::Checkbox("仅隐藏几何，保留挂载节点 (Hide Geometry)", &a_group.hideGeometry)) changed = true;
            if (ImGui::Checkbox("隐藏附加灯光 (Hide Light)", &a_group.hideLight)) changed = true;
            if (ImGui::Checkbox("加载第一人称武器模型 (Load 1P Weapon Model)", &a_group.load1pWeaponModel)) changed = true;
            if (ImGui::Checkbox("保留火焰/喷焰 FX (Keep Torch Flame)", &a_group.keepTorchFlame)) changed = true;
            if (ImGui::Checkbox("移除刀鞘/枪套节点 (Remove Scabbard)", &a_group.removeScabbard)) changed = true;

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Effect Shader 数据");
            if (ImGui::Checkbox("启用效果数据", &a_group.effectShader.enabled)) changed = true;
            if (a_group.effectShader.enabled) {
                ImGui::Indent();
                if (ImGui::Checkbox("Target Root", &a_group.effectShader.targetRoot)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Force", &a_group.effectShader.force)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Lighting", &a_group.effectShader.lighting)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Alpha", &a_group.effectShader.alpha)) changed = true;
                changed |= DrawColorRGBA("Fill Color", a_group.effectShader.fillColor);
                changed |= DrawColorRGBA("Rim Color", a_group.effectShader.rimColor);
                if (ImGui::DragFloat("透明度倍率", &a_group.effectShader.alphaMultiplier, 0.01f, 0.0f, 1.0f, "%.2f")) changed = true;
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
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "Extra Light 数据");
            if (ImGui::Checkbox("启用附加灯光数据", &a_group.light.enabled)) changed = true;
            if (a_group.light.enabled) {
                ImGui::Indent();
                ImGui::TextDisabled("Target/Water/Landscape/Shadows：当前渲染路径不支持");
                changed |= DrawColorRGBA("Diffuse", a_group.light.diffuse);
                if (ImGui::DragFloat("Radius", &a_group.light.radius, 1.0f, 0.0f, 4096.0f, "%.1f")) changed = true;
                if (ImGui::DragFloat("Dimmer", &a_group.light.dimmer, 0.01f, 0.0f, 20.0f, "%.2f")) changed = true;
                ImGui::TextDisabled("FOV/Shadow Bias：当前渲染路径不支持");
                changed |= DrawIEDTransformWidget("灯光位置偏移", a_group.light.transform.pos, { 0,0,0 }, 0.5f, false);
                changed |= DrawIEDTransformWidget("灯光旋转", a_group.light.transform.rot, { 0,0,0 }, 1.0f, true);
                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "动画/Sequence 数据");
            if (ImGui::Checkbox("播放 NiController Sequence", &a_group.animation.playSequence)) changed = true;
            if (ImGui::Checkbox("转发动画事件", &a_group.animation.forwardAnimationEvents)) changed = true;
            ImGui::SameLine();
            ImGui::TextDisabled("SubGraphs：FO4 展示副本不支持");
            if (ImGui::Checkbox("禁用行为图动画", &a_group.animation.disableBehaviorGraphAnims)) changed = true;
            char seqBuf[128];
            strcpy_s(seqBuf, a_group.animation.sequenceName.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText("Sequence 名称", seqBuf, 128)) {
                a_group.animation.sequenceName = seqBuf;
                changed = true;
            }
            char eventBuf[128];
            strcpy_s(eventBuf, a_group.animation.animationEvent.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText("动画事件", eventBuf, 128)) {
                a_group.animation.animationEvent = eventBuf;
                changed = true;
            }
        }
        return changed;
    }

    static void DrawFormFilterUI(FormFilter& a_filter, const char* a_id) {
        ImGui::PushID(a_id);
        ImGui::Checkbox("一键拒绝所有物品 (Deny All)", &a_filter.denyAll); ImGui::TextDisabled("开启后，该槽位将被强制清空，无视以下列表。");
        ImGui::BeginDisabled(a_filter.denyAll);
        if (ImGui::CollapsingHeader("允许列表 (Allow List) - 强制允许上背的物品")) DrawFormSetUI(a_filter.allowList, "AllowList");
        if (ImGui::CollapsingHeader("拒绝列表 (Deny List) - 排除黑名单物品")) DrawFormSetUI(a_filter.denyList, "DenyList");
        ImGui::EndDisabled(); ImGui::PopID();
    }

	static bool DrawSlotFormFilterProfileSelector(FormFilter& a_filter) {
		auto& manager = IAD::Profile::GlobalProfileManager::GetSingleton().FormFilters();
		if (!manager.IsInitialized()) {
			manager.Load();
		}

		bool changed = false;
		if (ImGui::Checkbox("动态引用过滤器预设 (IED-style)", &a_filter.useProfile)) {
			changed = true;
			if (!a_filter.useProfile) {
				a_filter.profileName.clear();
			}
		}

		if (!a_filter.useProfile) {
			changed |= DrawManagedProfileSelector(
				"SlotItemFilterProfileSelector",
				"过滤器预设 (Form Filter Profile)",
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
		const char* preview = a_filter.profileName.empty() ? "选择预设..." : a_filter.profileName.c_str();
		if (ImGui::BeginCombo("运行时预设", preview, ImGuiComboFlags_HeightLarge)) {
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
			ImGui::TextColored({ 1.0f, 0.65f, 0.25f, 1.0f }, "预设不存在或无法解析；该插槽不会接受候选物品。");
		}
		else {
			ImGui::TextDisabled("运行时读取此预设；保存该预设后，所有链接插槽会同时更新。");
			if (ImGui::Button("解除引用并复制当前预设")) {
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

        if (ImGui::TreeNodeEx("优先物品", ImGuiTreeNodeFlags_None)) {
			ImGui::TextDisabled("只要背包里有列表中的物品，无视其他所有过滤条件，直接强制上背！");
			if (DrawFormVectorUI(a_slot.preferredItems, "PrefItems")) {
				HolsterManager::GetSingleton()->ForceRefreshAll();
			}
			ImGui::TreePop();
		}
		ImGui::Spacing();
		if (ImGui::TreeNodeEx("候选选择模式", ImGuiTreeNodeFlags_None)) {
			ImGui::TextDisabled("首选物品始终优先。选择模式只作用于其余合格候选；无结果时回退到常规候选。\n");
			const char* modes[] = { "最近装备 (IED 默认)", "最强 (按当前改装后的评分)", "随机 (稳定选择)" };
			int mode = static_cast<int>(a_slot.selectionMode);
			ImGui::SetNextItemWidth(260.0f);
			if (ImGui::Combo("选择模式", &mode, modes, IM_ARRAYSIZE(modes))) {
				a_slot.selectionMode = static_cast<SlotSelectionMode>(mode);
				HolsterManager::GetSingleton()->ForceRefreshAll();
			}
			ImGui::TreePop();
		}
        ImGui::Spacing();
        if (ImGui::TreeNodeEx("物品过滤", ImGuiTreeNodeFlags_None)) {
			if (DrawSlotFormFilterProfileSelector(a_slot.itemFilter)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::Button("清空重置")) {
                a_slot.itemFilter = IAD::FormFilter();
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Separator();

			if (a_slot.itemFilter.useProfile) ImGui::BeginDisabled();
			DrawFormFilterUI(a_slot.itemFilter, "ItemFilter");
			if (a_slot.itemFilter.useProfile) ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::Spacing();

        if (ImGui::TreeNodeEx("候选条件", ImGuiTreeNodeFlags_None)) {
            ImGui::TextDisabled("这些条件会在每个候选物品被分配到该槽位前评估，类似 IED 的 item filter conditions。");
            if (ImGui::Button("清空候选条件##slotCandidateConditions")) {
                a_slot.itemFilterConditionTree = IAD::ConditionNode(true, true);
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (DrawConditionTreeEditor(a_slot.itemFilterConditionTree)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::TreePop();
        }
        ImGui::Spacing();

        if (ImGui::TreeNodeEx("类型优先级", ImGuiTreeNodeFlags_None)) {
            ImGui::TextDisabled("按 IED slot priority 思路控制该槽优先接收哪类物品；空列表表示保持全局候选顺序。");
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
                    HolsterManager::GetSingleton()->ForceRefreshAll();
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
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && i + 1 < a_slot.formTypePriority.size()) {
                    std::swap(a_slot.formTypePriority[i], a_slot.formTypePriority[i + 1]);
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt("活跃类型上限 (0=不限)", &a_slot.formTypePriorityLimit)) {
                if (a_slot.formTypePriorityLimit < 0) a_slot.formTypePriorityLimit = 0;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::Checkbox("已装备类型优先计入 (Account For Equipped)", &a_slot.formTypePriorityAccountForEquipped)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::Button("清空类型优先级##clearTypePriority")) {
                a_slot.formTypePriority.clear();
                a_slot.formTypePriorityLimit = 0;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::TreePop();
        }
        ImGui::Spacing();

        auto& af = a_slot.advancedFilters;
        if (ImGui::CollapsingHeader("高级候选过滤")) {
        ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "基础类型过滤");
        ImGui::BeginChild("BaseTypeFilters", ImVec2(0, 120), true);
        {
            ImGui::Checkbox("启用大类过滤 (Enable Base Filters)", &af.useBaseFilters);
            if (af.useBaseFilters) {
                ImGui::Indent();
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "武器 (WEAP):"); ImGui::SameLine();
                ImGui::Checkbox("枪械 (Guns)", &af.allowGun); ImGui::SameLine();
                ImGui::Checkbox("单手近战 (1H Melee)", &af.allowOneHanded); ImGui::SameLine();
                ImGui::Checkbox("双手近战 (2H Melee)", &af.allowTwoHanded); ImGui::SameLine();
                ImGui::Checkbox("徒手/拳套 (Unarmed)", &af.allowMelee); ImGui::SameLine();
                ImGui::Checkbox("投掷物 (Thrown)", &af.allowThrown);

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "护甲 (ARMO):"); ImGui::SameLine();
                ImGui::Checkbox("普通护甲", &af.allowArmor); ImGui::SameLine();
                ImGui::Checkbox("盾牌 (Shield)", &af.allowShield);

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "消耗/杂项:"); ImGui::SameLine();
                ImGui::Checkbox("弹药 (AMMO)", &af.allowAmmo); ImGui::SameLine();
                ImGui::Checkbox("药品 (Medicine)", &af.allowMedicine); ImGui::SameLine();
                ImGui::Checkbox("食物 (Food)", &af.allowFood); ImGui::SameLine();
                ImGui::Checkbox("水 (Water)", &af.allowWater); ImGui::SameLine();
                ImGui::Checkbox("钥匙 (Key)", &af.allowKeys);
                ImGui::Unindent();
            }
            else {
                ImGui::TextDisabled("  (未启用：允许任何类型的物品，包括衣服)");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "关键字过滤组");
        ImGui::BeginChild("KeywordFilters", ImVec2(0, 0), true);
        {
            const char* modeNames[] = { "禁用 (无视关键字)", "白名单模式 (必须命中下方至少一组)", "黑名单模式 (命中下方任意一组即被隐藏)" };
            int modeInt = static_cast<int>(a_slot.keywordMode);
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("过滤模式", &modeInt, modeNames, 3)) {
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
                    if (ImGui::Button("删除此组")) { groups.erase(groups.begin() + gIdx); gIdx--; ImGui::PopID(); continue; }

                    ImGui::Indent();
                    ImGui::PushStyleColor(ImGuiCol_Text, grp.isAnd ? ImVec4(0.2f, 0.8f, 1.0f, 1.0f) : ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                    const char* opTypes[] = { "▶ 组内逻辑: 满足其一 (OR)", "▶ 组内逻辑: 必须全含 (AND)" };
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
                    DrawKeywordScannerBox("##scanKwBox", s_newKwBuf); ImGui::SameLine();
                    if (ImGui::Button("添加至本组")) {
                        if (!s_newKwBuf.empty() && std::find(grp.keywords.begin(), grp.keywords.end(), s_newKwBuf) == grp.keywords.end()) {
                            grp.keywords.push_back(s_newKwBuf); s_newKwBuf.clear();
                        }
                    }
                    ImGui::Unindent();
                    ImGui::Separator();
                    ImGui::PopID();
                }

                if (ImGui::Button(" + 添加新关键字分组", { -1, 30 })) { groups.push_back({}); }
            }
        }
        ImGui::EndChild();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "特殊物品渲染设置 (Special Item Settings)");

        if (ImGui::Checkbox("提取武器弹匣 (Extract Magazine)", &a_slot.extractMagazine)) {
            if (a_slot.extractMagazine) a_slot.useProjectileForAmmo = false;
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("使用世界/基础模型 (Use World Model)", &a_slot.useWorldModel)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("隐藏几何 / alpha 0 (Invisible)", &a_slot.invisible)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("仅隐藏几何，保留挂载节点 (Hide Geometry)", &a_slot.hideGeometry)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("隐藏附加灯光 (Hide Light)", &a_slot.hideLight)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("加载第一人称武器模型 (Load 1P Weapon Model)", &a_slot.load1pWeaponModel)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("保留火焰/喷焰 FX (Keep Torch Flame)", &a_slot.keepTorchFlame)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (ImGui::Checkbox("移除刀鞘/枪套节点 (Remove Scabbard)", &a_slot.removeScabbard)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, "模型强制替换 (Force Model)");
        char slotModelPath[256]; strcpy_s(slotModelPath, a_slot.modelSwapPath.c_str());
        ImGui::SetNextItemWidth(400.0f);
        ImGui::InputText("槽位模型路径", slotModelPath, 256);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            a_slot.modelSwapPath = slotModelPath;
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (DrawFormIDField("槽位模型 FormID", a_slot.modelSwapFormID)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (DrawModelSwapVariableSource(a_slot.modelSwapVariableSource, "slot_model_var_source")) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        ImGui::TextDisabled("路径优先于 FormID；Custom 和 StateOverride 可继续覆盖槽位强制模型。");
        ImGui::Spacing();
        if (DrawModelCleanupSettings(a_slot.disableHavok, a_slot.removeEditorMarker, a_slot.removeProjectileTracers, "slot_cleanup")) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (DrawModelAnimationSettings(a_slot.animation, "slot_animation")) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (DrawModelEffectShaderSettings(a_slot.effectShader, "slot_effect")) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        if (DrawModelLightSettings(a_slot.light, "slot_light")) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "⚙️ 插槽工作模式 (Slot Logic Mode)");

        int slotMode = a_slot.ammoRig.isDedicatedAmmoSlot ? 1 : 0;
        if (ImGui::RadioButton("常规物品展示 (Weapons/Apparel/Misc)", &slotMode, 0)) {
            a_slot.ammoRig.isDedicatedAmmoSlot = false;
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("战术弹药阵列 (Tactical Ammo Rig)", &slotMode, 1)) {
            a_slot.ammoRig.isDedicatedAmmoSlot = true;
            HolsterManager::GetSingleton()->ForceRefreshAll();
            ConfigManager::GetSingleton()->SaveConfig();
        }
        ImGui::Spacing();
        ImGui::Separator();

        if (a_slot.ammoRig.isDedicatedAmmoSlot) {
            ImGui::TextColored(ImVec4(1.0f, 0.64f, 0.0f, 1.0f), "🎒 战术弹药舱配置");
            ImGui::TextWrapped("此模式下，插槽会自动追踪你当前手持武器的弹药，完全无需配置下方的过滤器！");

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
            ImGui::BeginChild("AmmoRigConfig", ImVec2(0, 160), true);

            int mode = static_cast<int>(a_slot.ammoRig.displayMode);
            if (ImGui::Combo("挂载模式 (Mode)", &mode, "单弹匣独立微调 (Single)\0自动克隆阵列 (Dynamic Array)\0")) {
                a_slot.ammoRig.displayMode = static_cast<AmmoDisplayMode>(mode);
                HolsterManager::GetSingleton()->ForceRefreshAll();
                ConfigManager::GetSingleton()->SaveConfig();
            }

            if (a_slot.ammoRig.displayMode == AmmoDisplayMode::kDynamicArray) {
                ImGui::SliderInt("最大备用弹匣数", &a_slot.ammoRig.maxMags, 1, 10);
                if (ImGui::IsItemDeactivatedAfterEdit()) ConfigManager::GetSingleton()->SaveConfig();

                ImGui::SliderFloat("阵列间距", &a_slot.ammoRig.magSpacing, -20.0f, 20.0f, "%.1f");
                if (ImGui::IsItemDeactivatedAfterEdit()) ConfigManager::GetSingleton()->SaveConfig();

                int dirIdx = (a_slot.ammoRig.arrayDirection.x != 0.0f) ? 0 : (a_slot.ammoRig.arrayDirection.y != 0.0f ? 1 : 2);
                if (ImGui::Combo("阵列延伸轴向", &dirIdx, "X 轴 (左右)\0Y 轴 (前后)\0Z 轴 (上下)\0")) {
                    a_slot.ammoRig.arrayDirection = { 0, 0, 0 };
                    if (dirIdx == 0) a_slot.ammoRig.arrayDirection.x = 1.0f;
                    if (dirIdx == 1) a_slot.ammoRig.arrayDirection.y = 1.0f;
                    if (dirIdx == 2) a_slot.ammoRig.arrayDirection.z = 1.0f;
                    ConfigManager::GetSingleton()->SaveConfig();
                }

                if (ImGui::Checkbox("开启弹尽粮绝隐身", &a_slot.ammoRig.dynamicAmmoLogic)) {
                    ConfigManager::GetSingleton()->SaveConfig();
                }
            }
            else {
                ImGui::TextWrapped("【手动布局模式】\n当前插槽只会显示 1 个弹匣。");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            const bool changed = beforeSignature != BuildSlotEquipmentSignature(a_slot);
            if (changed) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            return changed;
        }

        bool disableSlotAmmo = a_slot.extractMagazine;
        if (disableSlotAmmo) ImGui::BeginDisabled();
        ImGui::Checkbox("【弹药】提取射弹(单发子弹)模型，代替默认的地面弹药盒", &a_slot.useProjectileForAmmo);
        if (disableSlotAmmo) ImGui::EndDisabled();

        const bool changed = beforeSignature != BuildSlotEquipmentSignature(a_slot);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        return changed;
    }

    static bool DrawTabDisplay(SlotDefinition& a_slot) {
        bool changed = false;
        ImGui::Spacing();
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "🎒 基础展现逻辑 (Base Logic)");

        if (ImGui::Checkbox("覆盖全局装备模式限制 (Override Equipment Mode)", &a_slot.overrideEquipmentMode)) {
            changed = true;
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        if (a_slot.overrideEquipmentMode) {
            ImGui::Indent();
            if (ImGui::Checkbox("仅显示已装备或收藏的武器 (Favorites or Equipped Only)", &a_slot.displayFavoritesOnly)) {
                changed = true;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Unindent();
        }
        ImGui::Separator();

        if (ImGui::Checkbox("拔出武器时隐藏背部模型 (Always Unload)", &a_slot.alwaysUnload)) { changed = true; HolsterManager::GetSingleton()->ForceRefreshAll(); }

        if (ImGui::Checkbox("冲突隐藏 (Check Cannot Wear)", &a_slot.checkCannotWear)) { changed = true; HolsterManager::GetSingleton()->ForceRefreshAll(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("如果该部位穿了封闭式服装或动力甲导致无法佩戴，则自动隐藏背上的模型！");
        if (ImGui::Checkbox("使用家具时隐藏 (Hide If Using Furniture)", &a_slot.hideIfUsingFurniture)) { changed = true; HolsterManager::GetSingleton()->ForceRefreshAll(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("坐下、睡觉、躺卧或使用制作台时隐藏该插槽。");
        if (ImGui::Checkbox("躺卧/睡眠时隐藏 (Hide Laying Down)", &a_slot.hideLayingDown)) { changed = true; HolsterManager::GetSingleton()->ForceRefreshAll(); }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "装备显隐条件 (Visibility Conditions):");
        ImGui::TextDisabled("若条件不满足，该槽位上的武器将被隐藏（但其底层的物理 CME 节点仍会保留在身上）。");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("💥 一键清空所有条件 (Reset to Empty)##clearSlot", { 220.0f, 0.0f })) {
            a_slot.displayConditionTree = IAD::ConditionNode(true, true);
            changed = true;
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        ImGui::Spacing();

        changed |= DrawConditionTreeEditor(a_slot.displayConditionTree);
        if (DrawStateMachineEditor(a_slot, "slot_state_machine")) changed = true;
        return changed;
    }

    static bool DrawConfigBaseTransform(ConfigBase& a_config, int genderEdit, bool syncGender, const char* idSuffix, bool isNode) {
        const auto beforeSignature = BuildConfigBaseTransformSignature(a_config);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        if (!isNode) {
            ImGui::TextDisabled("此处设置该物品相对于其挂载节点的偏移量：");
            ImGui::Spacing();
            if (ImGui::Checkbox("启用姿态覆写 (Override Transform)", &a_config.overrideTransform)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (!a_config.overrideTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 💡 当前未开启覆写，姿态将回退继承自其目标挂载节点 (Node)。");
                ImGui::BeginDisabled();
            }
            ImGui::Separator();
        }

        auto& currentData = (genderEdit == 1) ? a_config.transforms.f : a_config.transforms.m;
        static bool s_lastSyncNode = false;
        bool syncJustToggled = (syncGender && !s_lastSyncNode);
        s_lastSyncNode = syncGender;

        if (DrawTransformProfileControls("baseTransformProfile", currentData) && syncGender) {
            if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
            else a_config.transforms.m = a_config.transforms.f;
        }

        if (ImGui::TreeNodeEx("姿态微调 (Transform Adjustment)", ImGuiTreeNodeFlags_DefaultOpen)) {

            if (ImGui::BeginPopupContextItem("TransformBlockContextMenu")) {
                if (ImGui::MenuItem("📋 复制全部坐标 (Copy Transform)")) {
                    s_clipboardTransform = currentData;
                    s_hasClipboardTransform = true;
                }
                if (ImGui::MenuItem("📋 粘贴全部坐标 (Paste Transform)", nullptr, false, s_hasClipboardTransform)) {
                    currentData = s_clipboardTransform;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("💡 右键点击此处可进行复制/粘贴坐标！");

            bool changed = false;
            if (syncJustToggled) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            changed |= DrawIEDTransformWidget("位置偏移 (Position)", currentData.pos, { 0,0,0 }, 0.5f, false);
            changed |= DrawIEDTransformWidget("旋转偏移 (Rotation)", currentData.rot, { 0,0,0 }, 1.0f, true);

            float dragSpeedScale = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat("缩放倍率 (Scale)", &currentData.scale, dragSpeedScale, 0.01f, 10.0f, "%.3f")) changed = true;

            if (ImGui::Button("重置当前坐标")) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                changed = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("强行同步到另一性别")) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (changed && syncGender) {
                if (genderEdit == 0) a_config.transforms.f = a_config.transforms.m;
                else a_config.transforms.m = a_config.transforms.f;
            }

            static bool s_wasDragging = false;
            if (changed) {
                s_wasDragging = true;
                if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
            }
            if (s_wasDragging && ImGui::IsMouseReleased(0)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
                s_wasDragging = false;
            }
            ImGui::TreePop();
        }

        if (!isNode && !a_config.overrideTransform) {
            ImGui::EndDisabled();
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildConfigBaseTransformSignature(a_config);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
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
        ImGui::TextDisabled("几何层变换作用在模型内容包装节点上；不会改变挂载 MOV 或主模型根偏移。");

        if (ImGui::Checkbox("启用几何层变换 (IED Geometry Transform)", &a_config.overrideGeometryTransform)) {
            changed = true;
        }
        if (!a_config.overrideGeometryTransform) {
            ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 当前未开启，几何包装节点保持 identity。");
            ImGui::BeginDisabled();
        }

        auto& currentData = (genderEdit == 1) ? a_config.geometryTransforms.f : a_config.geometryTransforms.m;
        if (DrawTransformProfileControls("geometryTransformProfile", currentData)) {
            changed = true;
        }
        changed |= DrawIEDTransformWidget("几何位置偏移", currentData.pos, { 0,0,0 }, 0.5f, false);
        changed |= DrawIEDTransformWidget("几何旋转", currentData.rot, { 0,0,0 }, 1.0f, true);
        changed |= DrawIEDTransformWidget("几何旋转轴心", currentData.pivot, { 0,0,0 }, 0.5f, false);

        float scaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
        if (ImGui::DragFloat("几何缩放倍率", &currentData.scale, scaleSpeed, 0.01f, 10.0f, "%.3f")) changed = true;

        if (ImGui::Button("重置几何坐标")) {
            currentData.pos = { 0,0,0 };
            currentData.rot = { 0,0,0 };
            currentData.pivot = { 0,0,0 };
            currentData.scale = 1.0f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("同步几何到另一性别")) {
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

        ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, "📦 模型组装车间 (Model Assembly)");
        ImGui::TextDisabled("在此选择要微调的网格层。旋转这些网格绝对不会影响 CME 物理摆锤的真实受力轴向！");
        ImGui::Spacing();

        int currentMode = static_cast<int>(state.meshMode);
        ImGui::RadioButton("🗡️ 武器本体 (Weapon)", &currentMode, 0); ImGui::SameLine();
        ImGui::RadioButton("🎒 枪套/刀鞘 (Holster)", &currentMode, 1); ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::RadioButton("🔋 独立弹匣 (Mag - WIP)", &currentMode, 2);
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::RadioButton("🧩 模型组 (Groups)", &currentMode, 3);
        state.meshMode = static_cast<MeshEditMode>(currentMode);

        ImGui::Separator();
        ImGui::Spacing();

        bool meshChanged = false;

        if (state.meshMode == MeshEditMode::kWeapon) {
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "【当前选中：🗡️ 武器本体】");
            ImGui::Spacing();

            if (ImGui::Checkbox("启用武器独立网格校准 (Override Weapon Mesh)", &a_slot.overrideMeshTransform)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (!a_slot.overrideMeshTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 💡 当前未开启独立校准，武器模型将默认对齐至插槽中心。");
                ImGui::BeginDisabled();
            }

            auto& currentData = (state.genderEdit == 1) ? a_slot.meshTransforms.f : a_slot.meshTransforms.m;

            if (DrawTransformProfileControls("slotWeaponMeshTransformProfile", currentData)) {
                meshChanged = true;
            }

            meshChanged |= DrawIEDTransformWidget("网格位置偏移", currentData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= DrawIEDTransformWidget("网格旋转 (掰弯模型)", currentData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= DrawIEDTransformWidget("网格旋转轴心 (Pivot)", currentData.pivot, { 0,0,0 }, 0.5f, false);

            float mScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat("网格缩放倍率", &currentData.scale, mScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            if (ImGui::Button("重置武器坐标")) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("同步到另一性别##weap")) {
                if (state.genderEdit == 0) a_slot.meshTransforms.f = a_slot.meshTransforms.m;
                else a_slot.meshTransforms.m = a_slot.meshTransforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_slot.meshTransforms.f = a_slot.meshTransforms.m;
                else a_slot.meshTransforms.m = a_slot.meshTransforms.f;
            }

            if (!a_slot.overrideMeshTransform) ImGui::EndDisabled();
            meshChanged |= DrawGeometryTransformBlock(a_slot, state.genderEdit, state.syncGender, "slotWeaponGeometryTransform", "【IED 几何层变换】");
        }
        else if (state.meshMode == MeshEditMode::kHolster) {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "【当前选中：🎒 枪套/刀鞘】");
            ImGui::Spacing();

            char hpBuf[256]; strcpy_s(hpBuf, a_slot.holsterModelPath.c_str());
            ImGui::SetNextItemWidth(450.0f);
            ImGui::InputText("枪套模型路径 (NIF)", hpBuf, 256);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                a_slot.holsterModelPath = hpBuf;
                // 👇 直接全局强制刷新，确保旧模型被彻底回收！
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("留空则不生成枪套。\n例: Weapons/Holsters/MyHolster.nif");

            if (ImGui::Checkbox("拔出武器时，将此枪套保留在身上 (Keep when drawn)", &a_slot.keepHolsterWhenDrawn)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (a_slot.holsterModelPath.empty()) {
                ImGui::TextDisabled("👆 请先输入模型路径，才可进行坐标微调。");
                ImGui::BeginDisabled();
            }

            auto& currentHData = (state.genderEdit == 1) ? a_slot.holsterTransforms.f : a_slot.holsterTransforms.m;

            if (DrawTransformProfileControls("slotHolsterTransformProfile", currentHData)) {
                meshChanged = true;
            }

            meshChanged |= DrawIEDTransformWidget("枪套位置偏移", currentHData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= DrawIEDTransformWidget("枪套旋转倾斜", currentHData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= DrawIEDTransformWidget("枪套旋转轴心", currentHData.pivot, { 0,0,0 }, 0.5f, false);

            float hScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat("枪套缩放倍率 (Scale)", &currentHData.scale, hScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            ImGui::Spacing();

            if (ImGui::Button("📍 一键吸附原点 (归零位置与轴心)")) {
                currentHData.pos = { 0,0,0 };
                currentHData.pivot = { 0,0,0 };
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("重置全部坐标")) {
                currentHData.pos = { 0,0,0 }; currentHData.rot = { 0,0,0 }; currentHData.pivot = { 0,0,0 }; currentHData.scale = 1.0f;
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("同步到另一性别##holster")) {
                if (state.genderEdit == 0) a_slot.holsterTransforms.f = a_slot.holsterTransforms.m;
                else a_slot.holsterTransforms.m = a_slot.holsterTransforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_slot.holsterTransforms.f = a_slot.holsterTransforms.m;
                else a_slot.holsterTransforms.m = a_slot.holsterTransforms.f;
            }

            if (a_slot.holsterModelPath.empty()) ImGui::EndDisabled();
        }

        static bool s_wasDraggingMesh = false;
        if (meshChanged) {
            s_wasDraggingMesh = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (s_wasDraggingMesh && ImGui::IsMouseReleased(0)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            s_wasDraggingMesh = false;
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildSlotMeshSignature(a_slot);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        return changed;
    }

    static bool DrawCustomMeshTransform(CustomDefinition& a_custom, ImGuiManager::WindowState& state, const std::vector<NodeDefinition>& localNodes, const char* idSuffix) {
        const auto beforeSignature = BuildCustomMeshSignature(a_custom);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, "📦 专属模型组装车间 (Custom Model Assembly)");
        ImGui::TextDisabled("在此微调专属武器的网格偏移，以及给它选配一个独一无二的枪套！");
        ImGui::Spacing();

        int currentMode = static_cast<int>(state.meshMode);
        ImGui::RadioButton("🗡️ 武器本体 (Weapon)", &currentMode, 0); ImGui::SameLine();
        ImGui::RadioButton("🎒 枪套/刀鞘 (Holster)", &currentMode, 1); ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::RadioButton("🔋 独立弹匣 (Mag - WIP)", &currentMode, 2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::RadioButton("🧩 模型组 (Groups)", &currentMode, 3);
        state.meshMode = static_cast<MeshEditMode>(currentMode);

        ImGui::Separator();
        ImGui::Spacing();

        bool meshChanged = false;

        if (state.meshMode == MeshEditMode::kWeapon) {
            ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "【当前选中：🗡️ 武器本体】");
            ImGui::Spacing();

            if (ImGui::Checkbox("启用专属武器网格覆写 (Override Custom Mesh)", &a_custom.overrideMeshTransform)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (!a_custom.overrideMeshTransform) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 💡 当前未开启专属覆写，将继承底层插槽 (Slot) 的网格坐标。");
                ImGui::BeginDisabled();
            }

            auto& currentData = (state.genderEdit == 1) ? a_custom.meshTransforms.f : a_custom.meshTransforms.m;

            if (DrawTransformProfileControls("customWeaponMeshTransformProfile", currentData)) {
                meshChanged = true;
            }

            meshChanged |= DrawIEDTransformWidget("专属网格位置偏移", currentData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= DrawIEDTransformWidget("专属网格旋转 (掰弯)", currentData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= DrawIEDTransformWidget("专属网格旋转轴心", currentData.pivot, { 0,0,0 }, 0.5f, false);

            float mScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat("专属网格缩放倍率", &currentData.scale, mScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            if (ImGui::Button("重置武器坐标")) {
                currentData.pos = { 0,0,0 }; currentData.rot = { 0,0,0 }; currentData.pivot = { 0,0,0 }; currentData.scale = 1.0f;
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("同步到另一性别##weap")) {
                if (state.genderEdit == 0) a_custom.meshTransforms.f = a_custom.meshTransforms.m;
                else a_custom.meshTransforms.m = a_custom.meshTransforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_custom.meshTransforms.f = a_custom.meshTransforms.m;
                else a_custom.meshTransforms.m = a_custom.meshTransforms.f;
            }

            if (!a_custom.overrideMeshTransform) ImGui::EndDisabled();
            meshChanged |= DrawGeometryTransformBlock(a_custom, state.genderEdit, state.syncGender, "customWeaponGeometryTransform", "【IED 专属几何层变换】");
        }
        else if (state.meshMode == MeshEditMode::kHolster) {
            ImGui::TextColored({ 1.0f, 0.6f, 0.2f, 1.0f }, "【当前选中：🎒 专属枪套/刀鞘】");
            ImGui::Spacing();

            char hpBuf[256]; strcpy_s(hpBuf, a_custom.holsterModelPath.c_str());
            ImGui::SetNextItemWidth(450.0f);
            ImGui::InputText("专属枪套模型路径 (NIF)", hpBuf, 256);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                a_custom.holsterModelPath = hpBuf;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("为这把武器单独指定一个枪套！\n留空则尝试继承插槽配置。");

            if (ImGui::Checkbox("拔出此武器时，将枪套保留在身上 (Keep when drawn)", &a_custom.keepHolsterWhenDrawn)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (a_custom.holsterModelPath.empty()) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 💡 当前没有配置专属枪套模型，坐标将无效或继承底层插槽。");
                ImGui::BeginDisabled();
            }

            auto& currentHData = (state.genderEdit == 1) ? a_custom.holsterTransforms.f : a_custom.holsterTransforms.m;

            if (DrawTransformProfileControls("customHolsterTransformProfile", currentHData)) {
                meshChanged = true;
            }

            meshChanged |= DrawIEDTransformWidget("专属枪套位置偏移", currentHData.pos, { 0,0,0 }, 0.5f, false);
            meshChanged |= DrawIEDTransformWidget("专属枪套旋转倾斜", currentHData.rot, { 0,0,0 }, 1.0f, true);
            meshChanged |= DrawIEDTransformWidget("专属枪套旋转轴心", currentHData.pivot, { 0,0,0 }, 0.5f, false);

            float hScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
            if (ImGui::DragFloat("专属枪套缩放倍率 (Scale)", &currentHData.scale, hScaleSpeed, 0.01f, 10.0f, "%.3f")) meshChanged = true;

            ImGui::Spacing();

            if (ImGui::Button("📍 一键吸附原点 (归零位置与轴心)")) {
                currentHData.pos = { 0,0,0 };
                currentHData.pivot = { 0,0,0 };
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("重置枪套坐标")) {
                currentHData.pos = { 0,0,0 }; currentHData.rot = { 0,0,0 }; currentHData.pivot = { 0,0,0 }; currentHData.scale = 1.0f;
                meshChanged = true; HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::SameLine();
            if (ImGui::Button("同步到另一性别##holster")) {
                if (state.genderEdit == 0) a_custom.holsterTransforms.f = a_custom.holsterTransforms.m;
                else a_custom.holsterTransforms.m = a_custom.holsterTransforms.f;
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (meshChanged && state.syncGender) {
                if (state.genderEdit == 0) a_custom.holsterTransforms.f = a_custom.holsterTransforms.m;
                else a_custom.holsterTransforms.m = a_custom.holsterTransforms.f;
            }

            if (a_custom.holsterModelPath.empty()) ImGui::EndDisabled();
        }
        else if (state.meshMode == MeshEditMode::kModelGroup) {
            ImGui::TextColored({ 0.6f, 1.0f, 0.8f, 1.0f }, "【当前选中：🧩 附加模型组】");
            ImGui::TextDisabled("给这条专属规则附加多个 NIF 模型；它们会跟随同一个 MOV 节点，但拥有独立偏移。");
            ImGui::Spacing();

            if (DrawManagedProfileSelector(
                "CustomModelGroupProfileSelector",
                "模型组预设 (Model Group Profile)",
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
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (ImGui::Button("添加模型组条目", { 140.0f, 0.0f })) {
                ModelGroupEntry group;
                group.name = "Group_" + std::to_string(a_custom.modelGroups.size() + 1);
                a_custom.modelGroups.push_back(group);
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            int removeIndex = -1;
            int moveIndex = -1;
            int moveDelta = 0;
            int duplicateIndex = -1;
            for (int i = 0; i < static_cast<int>(a_custom.modelGroups.size()); ++i) {
                auto& group = a_custom.modelGroups[i];
                ImGui::PushID(i);
                std::string header = group.name.empty() ? "未命名模型组" : group.name;
                if (ImGui::TreeNodeEx("##modelGroup", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str())) {
                    char nameBuf[64]; strncpy_s(nameBuf, group.name.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(220.0f);
                    if (ImGui::InputText("名称", nameBuf, 64)) group.name = nameBuf;

                    if (ImGui::Checkbox("启用", &group.isEnabled)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("跟随武器显隐", &group.hideWithWeapon)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("勾选后，拔枪隐藏武器本体时也隐藏这个附加模型；取消后只受插槽整体显隐影响。");
                    if (ImGui::Checkbox("可见命中后继续评估后续模型组", &group.continueAfterMatch)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("关闭后，此模型组条件通过且实际可见时，后面的模型组不会进入本次显示列表。");
                    if (ImGui::Checkbox("仅可见时加载模型", &group.loadOnlyWhenVisible)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("开启后，条件不满足或目标节点隐藏时不会预加载此模型组；关闭时保持旧行为，预加载后按条件隐藏。");

                    if (group.sourceMode == 0) {
                        char pathBuf[256]; strncpy_s(pathBuf, group.modelPath.c_str(), _TRUNCATE);
                        ImGui::SetNextItemWidth(450.0f);
                        ImGui::InputText("模型路径 (NIF)", pathBuf, 256);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            group.modelPath = pathBuf;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                    }
                    else {
                        if (DrawFormIDField("模型来源 FormID", group.sourceFormID)) {
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                    }

                    if (DrawModelGroupAdvancedConfig(group)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }

                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "模型组目标节点:"); ImGui::SameLine();
                    DrawCMENodeSelector("##modelGroupTargetNode", group.targetNode, localNodes, state.scope, "[不覆盖] 跟随 Custom/Slot 默认节点");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("留空时挂到当前 Custom/Slot 的默认 MOV；选择节点后会为此模型组创建独立 MOV。");

                    bool groupChanged = false;
                    auto& currentGData = (state.genderEdit == 1) ? group.transforms.f : group.transforms.m;
                    if (DrawTransformProfileControls("modelGroupTransformProfile", currentGData)) {
                        groupChanged = true;
                    }
                    groupChanged |= DrawIEDTransformWidget("模型组位置偏移", currentGData.pos, { 0,0,0 }, 0.5f, false);
                    groupChanged |= DrawIEDTransformWidget("模型组旋转", currentGData.rot, { 0,0,0 }, 1.0f, true);
                    groupChanged |= DrawIEDTransformWidget("模型组旋转轴心", currentGData.pivot, { 0,0,0 }, 0.5f, false);

                    float gScaleSpeed = ImGui::GetIO().KeyShift ? 0.001f : 0.01f;
                    if (ImGui::DragFloat("模型组缩放倍率", &currentGData.scale, gScaleSpeed, 0.01f, 10.0f, "%.3f")) groupChanged = true;

                    ImGui::Separator();
                    if (ImGui::Checkbox("启用模型组几何层变换 (IED Geometry Transform)", &group.overrideGeometryTransform)) {
                        groupChanged = true;
                    }
                    if (!group.overrideGeometryTransform) {
                        ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 当前未开启，模型组几何包装节点保持 identity。");
                        ImGui::BeginDisabled();
                    }

                    auto& currentGGeometry = (state.genderEdit == 1) ? group.geometryTransforms.f : group.geometryTransforms.m;
                    if (DrawTransformProfileControls("modelGroupGeometryTransformProfile", currentGGeometry)) {
                        groupChanged = true;
                    }
                    groupChanged |= DrawIEDTransformWidget("模型组几何位置偏移", currentGGeometry.pos, { 0,0,0 }, 0.5f, false);
                    groupChanged |= DrawIEDTransformWidget("模型组几何旋转", currentGGeometry.rot, { 0,0,0 }, 1.0f, true);
                    groupChanged |= DrawIEDTransformWidget("模型组几何旋转轴心", currentGGeometry.pivot, { 0,0,0 }, 0.5f, false);

                    if (ImGui::DragFloat("模型组几何缩放倍率", &currentGGeometry.scale, gScaleSpeed, 0.01f, 10.0f, "%.3f")) groupChanged = true;

                    if (ImGui::Button("重置模型组几何坐标")) {
                        currentGGeometry.pos = { 0,0,0 };
                        currentGGeometry.rot = { 0,0,0 };
                        currentGGeometry.pivot = { 0,0,0 };
                        currentGGeometry.scale = 1.0f;
                        groupChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("同步几何到另一性别")) {
                        if (state.genderEdit == 0) group.geometryTransforms.f = group.geometryTransforms.m;
                        else group.geometryTransforms.m = group.geometryTransforms.f;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }

                    if (!group.overrideGeometryTransform) {
                        ImGui::EndDisabled();
                    }

                    if (ImGui::CollapsingHeader("模型组显隐条件", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Button("清空此模型组条件", { 160.0f, 0.0f })) {
                            group.displayConditionTree = IAD::ConditionNode(true, true);
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("应用条件更改", { 140.0f, 0.0f })) {
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        groupChanged |= DrawConditionTreeEditor(group.displayConditionTree);
                    }

                    if (ImGui::Button("重置模型组坐标")) {
                        currentGData.pos = { 0,0,0 };
                        currentGData.rot = { 0,0,0 };
                        currentGData.pivot = { 0,0,0 };
                        currentGData.scale = 1.0f;
                        groupChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("同步到另一性别")) {
                        if (state.genderEdit == 0) group.transforms.f = group.transforms.m;
                        else group.transforms.m = group.transforms.f;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine();
                    if (i <= 0) ImGui::BeginDisabled();
                    if (ImGui::Button("上移##moveModelGroupUp")) {
                        moveIndex = i;
                        moveDelta = -1;
                    }
                    if (i <= 0) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (i >= static_cast<int>(a_custom.modelGroups.size()) - 1) ImGui::BeginDisabled();
                    if (ImGui::Button("下移##moveModelGroupDown")) {
                        moveIndex = i;
                        moveDelta = 1;
                    }
                    if (i >= static_cast<int>(a_custom.modelGroups.size()) - 1) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("复制条目##duplicateModelGroup")) {
                        duplicateIndex = i;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("删除条目")) {
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
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
            }

            if (duplicateIndex >= 0 && duplicateIndex < static_cast<int>(a_custom.modelGroups.size())) {
                auto copy = a_custom.modelGroups[duplicateIndex];
                copy.name = MakeUniqueModelGroupName(a_custom.modelGroups, copy.name);
                a_custom.modelGroups.insert(a_custom.modelGroups.begin() + duplicateIndex + 1, std::move(copy));
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }

            if (removeIndex >= 0 && removeIndex < static_cast<int>(a_custom.modelGroups.size())) {
                a_custom.modelGroups.erase(a_custom.modelGroups.begin() + removeIndex);
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
        }

        static bool s_wasDraggingMesh = false;
        if (meshChanged) {
            s_wasDraggingMesh = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (s_wasDraggingMesh && ImGui::IsMouseReleased(0)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            s_wasDraggingMesh = false;
        }

        ImGui::PopID();
        const bool changed = beforeSignature != BuildCustomMeshSignature(a_custom);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        return changed;
    }

    static bool DrawPhysicsConstraintParams(PhysicsConstraintParams& a_params) {
        auto dragSpeed = ImGui::GetIO().KeyShift ? 0.0001f : 0.04f;
        bool changed = false;

        ImGui::PushID("opar");
        if (ImGui::DragFloat("速度响应比例 (Velocity Response Scale)", &a_params.velocityResponseScale, dragSpeed, 0.0f, 1.0f)) changed = true;
        if (ImGui::DragFloat("穿模偏移因素 (Pen Bias Factor)", &a_params.penBiasFactor, dragSpeed, 0.0f, 20.0f)) changed = true;
        if (ImGui::DragFloat("穿模偏移极限 (Pen Bias Depth Limit)", &a_params.penBiasDepthLimit, dragSpeed, 0.5f, 50000.0f)) changed = true;
        if (ImGui::DragFloat("恢复系数/弹性 (Restitution Coefficient)", &a_params.restitutionCoefficient, dragSpeed, 0.0f, 1.0f)) changed = true;
        ImGui::PopID();
        return changed;
    }

    static bool DrawPhysicsPanel(PhysicsValues& a_phys) {
        ImGui::Spacing();
        bool changed = false;

        ImGui::PushID("pv");

        if (ImGui::Checkbox("禁用物理效果 (Disable)", &a_phys.disabled)) changed = true;

        ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
        if (ImGui::Checkbox("显示约束", &a_phys.drawConstraints)) changed = true; ImGui::SameLine();
        if (ImGui::Checkbox("显示摆锤", &a_phys.drawPendulum)) changed = true;

        ImGui::Spacing();
        const bool disabled = a_phys.disabled;
        if (disabled) ImGui::BeginDisabled();

        if (ImGui::TreeNodeEx("一般参数 (General)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::Spacing();

            auto dragSpeed = ImGui::GetIO().KeyShift ? 0.0001f : 0.04f;

            if (ImGui::DragFloat("刚度 (Stiffness)", &a_phys.stiffness, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat("二次刚度 (Stiffness 2)", &a_phys.stiffness2, dragSpeed, 0.0f, 500.0f)) changed = true;

            if (ImGui::DragFloat("弹簧松弛偏移 (Spring Slack Offset)", &a_phys.springSlackOffset, dragSpeed, 0.0f, 5000.0f)) changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("极其重要：允许武器在此距离内自由滑动而不受弹簧拉力，完美解决下蹲时的微小错位！");

            if (ImGui::DragFloat("弹簧松弛幅度 (Spring Slack Mag)", &a_phys.springSlackMag, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat("阻尼 (Damping)", &a_phys.damping, dragSpeed, 0.0f, 500.0f)) changed = true;
            if (ImGui::DragFloat("动态阻力 (Resistance)", &a_phys.resistance, dragSpeed, 0.0f, 20.0f)) changed = true;
            if (ImGui::DragFloat("最大速度 (Max Velocity)", &a_phys.maxVelocity, dragSpeed, 10.0f, 50000.0f)) changed = true;

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "物理力响应参数 (Physical Feedback)");

            changed |= DrawIEDTransformWidget("线性位移比例 (Linear Scale)", a_phys.linear, { 0,0,0 }, dragSpeed, false);
            ImGui::TextDisabled("设定阻力产生的位移范围 (X=左右, Y=前后, Z=上下)");

            changed |= DrawIEDTransformWidget("旋转摇晃比例 (Rotation Scale)", a_phys.rotational, { 0,0,0 }, dragSpeed, false);
            ImGui::TextDisabled("设定随阻力摇摆的角度系数 (X=前后点头Pitch, Y=自身扭转Roll, Z=左右摇摆Yaw)");

            changed |= DrawIEDTransformWidget("重心偏移 (Cog Offset)", a_phys.cogOffset, { 0,0,0 }, dragSpeed, false);

            ImGui::Separator();

            if (ImGui::DragFloat("质量 (Mass)", &a_phys.mass, dragSpeed, 0.001f, 1000.0f)) changed = true;
            if (ImGui::DragFloat("重力拉扯 (Gravity Bias)", &a_phys.gravityBias, dragSpeed, 0.0f, 8000.0f)) changed = true;

            ImGui::Spacing();
            ImGui::TreePop();
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("球面约束 (Sphere Constraint)", &a_phys.enableSphereConstraint)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("盒形约束 (Box Constraint)", &a_phys.enableBoxConstraint)) changed = true;

        if (a_phys.enableSphereConstraint || a_phys.enableBoxConstraint) {
            ImGui::Spacing();

            if (a_phys.enableSphereConstraint && ImGui::TreeNodeEx("球面约束参数 (Sphere Constraint)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                changed |= DrawIEDTransformWidget("球面偏移 (Sphere Offset)", a_phys.maxOffsetSphereOffset, { 0,0,0 }, 0.04f, false);
                ImGui::TextDisabled("球面中心坐标系: X=右, Y=前, Z=上");
                if (ImGui::DragFloat("球面半径 (Sphere Radius)", &a_phys.maxOffsetSphereRadius, 0.04f, 0.0f, 500.0f)) changed = true;
                if (ImGui::DragFloat("摩擦力 (Friction)##sphere", &a_phys.maxOffsetSphereFriction, 0.04f, 0.0f, 1.0f)) changed = true;

                ImGui::Spacing();
                changed |= DrawPhysicsConstraintParams(a_phys.sphereParams);
                ImGui::Spacing();
                ImGui::TreePop();
            }

            if (a_phys.enableBoxConstraint && ImGui::TreeNodeEx("盒形约束参数 (Box Constraint)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                changed |= DrawIEDTransformWidget("物理盒最小边界 (Box Min)", a_phys.maxOffsetN, { 0,0,0 }, 0.04f, false);
                changed |= DrawIEDTransformWidget("物理盒最大边界 (Box Max)", a_phys.maxOffsetP, { 0,0,0 }, 0.04f, false);
                ImGui::TextDisabled("约束盒坐标系: X=左右边界(右正), Y=前后边界(前正), Z=上下边界(上正)");
                if (ImGui::DragFloat("摩擦力 (Friction)##box", &a_phys.maxOffsetBoxFriction, 0.04f, 0.0f, 1.0f)) changed = true;

                ImGui::Spacing();
                changed |= DrawPhysicsConstraintParams(a_phys.boxParams);
                ImGui::Spacing();
                ImGui::TreePop();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Checkbox("非对称角度极限 (Asymmetric Angular Limits)", &a_phys.enableAngularConstraint)) changed = true;
        if (a_phys.enableAngularConstraint) {
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("角度极限区间 (Angle Ranges)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ImGui::Spacing();
                ImGui::TextDisabled("通过滑块设定武器摇摆的安全死区，避免模型插进身体");

                if (ImGui::DragFloatRange2("前后点头 (Pitch Range)", &a_phys.minPitch, &a_phys.maxPitch, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;
                if (ImGui::DragFloatRange2("左右摇摆 (Yaw Range)", &a_phys.minYaw, &a_phys.maxYaw, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;
                if (ImGui::DragFloatRange2("自身扭转 (Roll Range)", &a_phys.minRoll, &a_phys.maxRoll, 0.5f, -180.0f, 180.0f, "Min: %.1f", "Max: %.1f")) changed = true;

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextDisabled("辅助可视化设置");
                if (ImGui::DragFloat("可视化探针/圆锥长度", &a_phys.visualProbeLength, 0.5f, 1.0f, 150.0f, "%.1f 单位")) changed = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("根据当前武器的长短调节此滑块，借此刻度线精准观察武器末端是否会穿模。");

                ImGui::Spacing();
                ImGui::TreePop();
            }
        }

        if (disabled) ImGui::EndDisabled();

        static bool s_wasDraggingPhys = false;
        if (changed) {
            s_wasDraggingPhys = true;
            if (auto player = RE::PlayerCharacter::GetSingleton()) HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
        }
        if (s_wasDraggingPhys && ImGui::IsMouseReleased(0)) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
            s_wasDraggingPhys = false;
        }

        ImGui::PopID();
        return changed;
    }

    static bool DrawConfigBasePhysics(ConfigBase& a_config, const char* idSuffix, bool isNode) {
        const auto beforeSignature = BuildConfigBasePhysicsSignature(a_config);
        ImGui::Spacing();
        ImGui::PushID(idSuffix);

        if (!isNode) {
            if (ImGui::Checkbox("启用物理覆写 (Override Physics)", &a_config.overridePhysics)) {
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (!a_config.overridePhysics) {
                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, " 💡 当前未开启覆写，物理效果将回退继承下级配置。");
                ImGui::BeginDisabled();
            }
            ImGui::Separator();
        }

        if (ImGui::Button("📋 复制整套物理配置")) {
            s_clipboardPhysics = a_config.physics;
            s_hasClipboardPhysics = true;
        }
        ImGui::SameLine();
        if (!s_hasClipboardPhysics) ImGui::BeginDisabled();
        if (ImGui::Button("📋 粘贴物理配置")) {
            a_config.physics = s_clipboardPhysics;
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        if (!s_hasClipboardPhysics) ImGui::EndDisabled();

        DrawPhysicsProfileControls("basePhysicsProfile", a_config.physics);
        DrawPhysicsPanel(a_config.physics);

        if (!isNode && !a_config.overridePhysics) {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
        const bool changed = beforeSignature != BuildConfigBasePhysicsSignature(a_config);
        if (changed) {
            HolsterManager::GetSingleton()->ForceRefreshAll();
        }
        return changed;
    }

	// Shared by the live editor and the profile editor.  The caller decides
	// where the SlotDefinition comes from; this component only edits it.
	static bool DrawSlotEditorTabs(SlotDefinition& a_slot, ImGuiManager::WindowState& a_state, const char* a_idSuffix) {
		bool changed = false;
		ImGui::PushID(a_idSuffix);
		if (ImGui::BeginTabBar("SlotTabs", ImGuiTabBarFlags_None)) {
			if (ImGui::BeginTabItem("筛选")) { changed |= DrawTabEquipment(a_slot); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("显示")) { changed |= DrawTabDisplay(a_slot); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("变换")) { changed |= DrawConfigBaseTransform(a_slot, a_state.genderEdit, a_state.syncGender, "transform", false); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("模型")) { changed |= DrawSlotMeshTransform(a_slot, a_state, "mesh"); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("物理")) { changed |= DrawConfigBasePhysics(a_slot, "physics", false); ImGui::EndTabItem(); }
			ImGui::EndTabBar();
		}
		ImGui::PopID();
		return changed;
	}

	static bool DrawNodeEditorTabs(NodeDefinition& a_node, ImGuiManager::WindowState& a_state, const char* a_idSuffix) {
		bool changed = false;
		ImGui::PushID(a_idSuffix);
		if (ImGui::BeginTabBar("NodeTabs", ImGuiTabBarFlags_None)) {
			if (ImGui::BeginTabItem("变换")) {
				changed |= DrawConfigBaseTransform(a_node, a_state.genderEdit, a_state.syncGender, "transform", true);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("显示")) {
				if (ImGui::Button("清空显示条件")) {
					a_node.displayConditionTree = IAD::ConditionNode(true, true);
					changed = true;
				}
				changed |= DrawConditionTreeEditor(a_node.displayConditionTree);
				changed |= DrawStateMachineEditor(a_node, "stateMachine", true);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("骨架")) {
				changed |= DrawSkeletonMatchEditor(a_node.skeletonMatch);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("物理")) {
				changed |= DrawConfigBasePhysics(a_node, "physics", true);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::PopID();
		return changed;
	}

	// The shared tab body deliberately has no runtime side effects. Live callers
	// refresh displays after a change; profile callers only mark their record dirty.
	static bool DrawCustomEditorTabs(CustomDefinition& a_custom, ImGuiManager::WindowState& a_state, const std::vector<NodeDefinition>& a_nodes, const char* a_idSuffix) {
		bool changed = false;
		ImGui::PushID(a_idSuffix);
		if (ImGui::BeginTabBar("CustomTabs", ImGuiTabBarFlags_None)) {
			if (ImGui::BeginTabItem("变换")) {
				changed |= DrawConfigBaseTransform(a_custom, a_state.genderEdit, a_state.syncGender, "transform", false);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("模型")) {
				changed |= DrawCustomMeshTransform(a_custom, a_state, a_nodes, "mesh");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("显示")) {
				changed |= ImGui::Checkbox("使用家具时隐藏", &a_custom.hideIfUsingFurniture);
				changed |= ImGui::Checkbox("躺卧/睡眠时隐藏", &a_custom.hideLayingDown);
				ImGui::Separator();
				if (ImGui::Button("清空显示条件")) {
					a_custom.displayConditionTree = IAD::ConditionNode(true, true);
					changed = true;
				}
				changed |= DrawConditionTreeEditor(a_custom.displayConditionTree);
				changed |= DrawStateMachineEditor(a_custom, "stateMachine");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("物理")) {
				changed |= DrawConfigBasePhysics(a_custom, "physics", false);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::PopID();
		return changed;
	}

    // 👇========== 🌟 核心面板类实现：装备插槽 ==========👇
    void UISlotsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowSlots) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("装备插槽", &config->uiShowSlots, ImGuiWindowFlags_MenuBar)) {
            bool doPropagate = DrawWindowHeader("SlotHdr", ImGuiManager::s_slotState, true, true);
            DrawConfiguredTargetBrowser("SlotConfiguredTargets", ImGuiManager::s_slotState, config->GetConfiguredSlotTargetIDs(ImGuiManager::s_slotState.scope));
            uint32_t queryID = GetQueryID(ImGuiManager::s_slotState);
            auto& a_slots = config->GetSlots(ImGuiManager::s_slotState.scope, queryID); auto& a_nodes = config->GetNodes(ImGuiManager::s_slotState.scope, queryID);
            if (doPropagate) { auto& globalSlots = config->GetSlots(ConfigScope::kGlobal, 0); for (auto& s : a_slots) { auto git = std::find_if(globalSlots.begin(), globalSlots.end(), [&](const SlotDefinition& gs) { return gs.slotName == s.slotName; }); if (git != globalSlots.end()) *git = s; else globalSlots.push_back(s); } HolsterManager::GetSingleton()->ForceRefreshAll(); }
            bool openAddSlotPopup = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("文件")) {
                    if (ImGui::MenuItem("保存配置")) config->SaveConfig();
                    if (ImGui::MenuItem("关闭")) config->uiShowSlots = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("操作")) {
                    if (ImGui::MenuItem("新建插槽")) openAddSlotPopup = true;
                    if (ImGui::MenuItem("强制刷新显示")) HolsterManager::GetSingleton()->ForceRefreshAll();
					ImGui::Separator();
					if (ImGui::MenuItem("编辑插槽预设")) {
						config->uiProfileManagedCategory = 0;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    if (ImGuiManager::s_slotState.scope != ConfigScope::kGlobal) {
                        auto& globalSlotsForMenu = config->GetSlots(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global slots")) {
                            if (MergeConfigListByKey(a_slots, globalSlotsForMenu, [](const SlotDefinition& slot) { return slot.slotName; }, false) > 0) {
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global slots")) {
                            if (MergeConfigListByKey(a_slots, globalSlotsForMenu, [](const SlotDefinition& slot) { return slot.slotName; }, true) > 0) {
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Spacing();
            if (DrawManagedProfileSelector(
                "SlotModuleProfileSelector",
                "插槽预设",
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
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGuiManager::s_slotState.scope != ConfigScope::kGlobal) {
                auto& globalSlots = config->GetSlots(ConfigScope::kGlobal, 0);
                ImGui::Spacing();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "从全局层同步");
                if (ImGui::Button("导入 Global 缺失槽位")) {
                    if (MergeConfigListByKey(a_slots, globalSlots, [](const SlotDefinition& slot) { return slot.slotName; }, false) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("刷新 Global 同名槽位")) {
                    if (MergeConfigListByKey(a_slots, globalSlots, [](const SlotDefinition& slot) { return slot.slotName; }, true) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("只覆盖当前层已有的同名槽位，并添加缺失槽位；不会删除当前层独有槽位。");
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            static float s_leftPaneW_slot = 240.0f;
            ImGui::BeginChild("SlotLeft", ImVec2(s_leftPaneW_slot, 0.0f), true);
            if (openAddSlotPopup) ImGui::OpenPopup("AddSlotPopup");
            if (ImGui::Button("新建装备槽位", ImVec2(-1.0f, 35.0f))) ImGui::OpenPopup("AddSlotPopup");
            if (ImGui::BeginPopupModal("AddSlotPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newName[64] = "LeftHip"; static bool isDuplicate = false;
                ImGui::Text("输入槽位唯一标识名:"); ImGui::TextDisabled("IAD_MOV_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##nsn", newName, 64)) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "错误: 该名称已被使用，请更换！");
                if (ImGui::Button("确认", { 100.0f, 0.0f })) {
                    if (strlen(newName) > 0) {
                        std::string finalName = "IAD_MOV_" + std::string(newName); isDuplicate = false;
                        for (auto& s : a_slots) { if (s.slotName == finalName) { isDuplicate = true; break; } }
                        if (!isDuplicate) {
                            SlotDefinition ns; ns.slotName = finalName; ns.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                            a_slots.push_back(ns); ImGuiManager::s_selectedSlot = finalName; ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) { isDuplicate = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, SlotDefinition*> inheritedMap;
            if (ImGuiManager::s_slotState.scope != ConfigScope::kGlobal) { for (auto& gs : config->GetSlots(ConfigScope::kGlobal, 0)) inheritedMap[gs.slotName] = &gs; }
            for (auto& s : a_slots) inheritedMap.erase(s.slotName);

            for (size_t i = 0; i < a_slots.size(); i++) {
                auto& s = a_slots[i]; std::string dispName = (s.slotName.find("IAD_MOV_") == 0) ? s.slotName.substr(8) : s.slotName;
                if (ImGui::Selectable((dispName + "###" + s.slotName).c_str(), ImGuiManager::s_selectedSlot == s.slotName)) ImGuiManager::s_selectedSlot = s.slotName;
            }
            for (auto& [name, sPtr] : inheritedMap) {
                std::string dispName = (name.find("IAD_MOV_") == 0) ? name.substr(8) : name;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                if (ImGui::Selectable(("[继承] " + dispName + "###" + name).c_str(), ImGuiManager::s_selectedSlot == name)) ImGuiManager::s_selectedSlot = name;
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::SameLine(); ImGui::InvisibleButton("##vsplitter_slot", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
            if (ImGui::IsItemActive()) { s_leftPaneW_slot += ImGui::GetIO().MouseDelta.x; if (s_leftPaneW_slot < 120.0f) s_leftPaneW_slot = 120.0f; if (s_leftPaneW_slot > ImGui::GetWindowWidth() - 150.0f) s_leftPaneW_slot = ImGui::GetWindowWidth() - 150.0f; }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); ImGui::SameLine();

            ImGui::BeginChild("SlotRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_slots.begin(), a_slots.end(), [&](const SlotDefinition& s) { return s.slotName == ImGuiManager::s_selectedSlot; });
            SlotDefinition* activeSlot = nullptr; bool isInherited = false;
            if (it != a_slots.end()) { activeSlot = &(*it); }
            else if (inheritedMap.count(ImGuiManager::s_selectedSlot)) { activeSlot = inheritedMap[ImGuiManager::s_selectedSlot]; isInherited = true; }

            if (activeSlot) {
                auto& s = *activeSlot; ImGui::PushID(("Slot_" + s.slotName).c_str());
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "💡 此配置继承自全局 (Global)，当前层级未进行覆写。");
                    if (ImGui::Button("➕ 提取到当前层级并覆写 (Add Override)", { -1, 40 })) { a_slots.push_back(s); ImGuiManager::s_selectedSlot = s.slotName; HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }
                else {
                    std::string shortName = (s.slotName.find("IAD_MOV_") == 0) ? s.slotName.substr(8) : s.slotName;
                    char nameBuf[64]; strcpy_s(nameBuf, shortName.c_str());
                    ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "槽位名称:"); ImGui::SameLine(); ImGui::TextDisabled("IAD_MOV_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::InputText("##slotRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::string newName = std::string("IAD_MOV_") + nameBuf;
                        if (!newName.empty() && newName != s.slotName) {
                            bool exists = false; for (auto& other : a_slots) if (other.slotName == newName) exists = true;
                            if (!exists) { s.slotName = newName; ImGuiManager::s_selectedSlot = newName; NodeManager::ClearAllCaches(); HolsterManager::GetSingleton()->ForceRefreshAll(); }
                        }
                    }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f); if (ImGui::Button("删除槽位")) ImGui::OpenPopup("DeleteConfirmSlot");
                    if (ImGui::BeginPopupModal("DeleteConfirmSlot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("确定要彻底删除槽位 [%s] 吗？", s.slotName.c_str());
                        if (ImGui::Button("删除", { 100.0f, 0.0f })) { a_slots.erase(it); ImGuiManager::s_selectedSlot = ""; ConfigManager::GetSingleton()->SaveConfig(); NodeManager::ClearAllCaches(); HolsterManager::GetSingleton()->ForceRefreshAll(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::PopID(); ImGui::EndChild(); ImGui::End(); return; }
                        ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    if (ImGui::Checkbox("全局启用", &s.isEnabled)) HolsterManager::GetSingleton()->ForceRefreshAll();
                    ImGui::SameLine(); ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragInt("渲染优先级", &s.priority, 1, 0, 100)) {
                        if (ImGui::IsItemDeactivatedAfterEdit()) HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::Separator();
                }

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "绑定目标节点 (Target Node):"); ImGui::SameLine();
                DrawCMENodeSelector("##targetNode", s.targetNode, a_nodes, ImGuiManager::s_slotState.scope, "未绑定 (不可见)");
                ImGui::Separator();

                DrawSlotEditorTabs(s, ImGuiManager::s_slotState, "liveSlotTabs");
                if (isInherited) ImGui::EndDisabled(); ImGui::PopID();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled("请在左侧选择一个装备槽位"); }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // 👇========== 🌟 核心面板类实现：挂载节点 ==========👇
    void UINodesWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowNodes) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("挂载节点", &config->uiShowNodes, ImGuiWindowFlags_MenuBar)) {
            bool doPropagate = DrawWindowHeader("NodeHdr", ImGuiManager::s_nodeState, true, true);
            DrawConfiguredTargetBrowser("NodeConfiguredTargets", ImGuiManager::s_nodeState, config->GetConfiguredNodeTargetIDs(ImGuiManager::s_nodeState.scope));
            uint32_t queryID = GetQueryID(ImGuiManager::s_nodeState);
            auto& a_nodes = config->GetNodes(ImGuiManager::s_nodeState.scope, queryID);
            if (doPropagate) { auto& globalNodes = config->GetNodes(ConfigScope::kGlobal, 0); for (auto& n : a_nodes) { auto git = std::find_if(globalNodes.begin(), globalNodes.end(), [&](const NodeDefinition& gn) { return gn.nodeName == n.nodeName; }); if (git != globalNodes.end()) *git = n; else globalNodes.push_back(n); } HolsterManager::GetSingleton()->ForceRefreshAll(); }
            bool openAddNodePopup = false;
            bool openCloneNodePopup = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("文件")) {
                    if (ImGui::MenuItem("保存配置")) config->SaveConfig();
                    if (ImGui::MenuItem("关闭")) config->uiShowNodes = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("操作")) {
                    if (ImGui::MenuItem("新建节点")) openAddNodePopup = true;
					if (ImGui::MenuItem("编辑节点预设")) {
						config->uiProfileManagedCategory = 1;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    const bool hasSelectedNode = !ImGuiManager::s_selectedNode.empty();
                    if (!hasSelectedNode) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Clone selected node")) openCloneNodePopup = true;
                    if (!hasSelectedNode) ImGui::EndDisabled();
                    if (ImGui::MenuItem("Clear node caches")) NodeManager::ClearAllCaches();
                    if (ImGui::MenuItem("Force refresh displays")) {
                        NodeManager::ClearAllCaches();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (ImGuiManager::s_nodeState.scope != ConfigScope::kGlobal) {
                        auto& globalNodesForMenu = config->GetNodes(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global nodes")) {
                            if (MergeConfigListByKey(a_nodes, globalNodesForMenu, [](const NodeDefinition& node) { return node.nodeName; }, false) > 0) {
                                NodeManager::ClearAllCaches();
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global nodes")) {
                            if (MergeConfigListByKey(a_nodes, globalNodesForMenu, [](const NodeDefinition& node) { return node.nodeName; }, true) > 0) {
                                NodeManager::ClearAllCaches();
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Spacing();
            if (DrawManagedProfileSelector(
                "NodeModuleProfileSelector",
                "节点预设",
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
                NodeManager::ClearAllCaches();
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            ImGui::Spacing();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "节点转换配置 (ConvertNodes)");
            static char convertProfName[64] = "MyConvertNodes";
            static std::string selConvertProf = "";
            static bool convertV2 = false;
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputText("##cnpn", convertProfName, 64);
            ImGui::SameLine();
            ImGui::Checkbox("ConvertNodes2", &convertV2);
            ImGui::SameLine();
            if (ImGui::Button("保存转换配置")) {
                if (convertProfName[0] != '\0') {
                    config->SaveNodeConversionProfile(convertProfName, a_nodes, convertV2);
                    selConvertProf = convertProfName;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::BeginCombo("##cnlp", selConvertProf.empty() ? "选择转换配置..." : selConvertProf.c_str())) {
                for (auto& p : config->GetAvailableNodeConversionProfiles(convertV2)) {
                    if (ImGui::Selectable(p.c_str())) selConvertProf = p;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("导入合并##cn")) {
                if (!selConvertProf.empty() && config->LoadNodeConversionProfile(selConvertProf, a_nodes, false, convertV2)) {
                    NodeManager::ClearAllCaches();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("导入覆盖##cn")) {
                if (!selConvertProf.empty() && config->LoadNodeConversionProfile(selConvertProf, a_nodes, true, convertV2)) {
                    NodeManager::ClearAllCaches();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
            }
            if (ImGuiManager::s_nodeState.scope != ConfigScope::kGlobal) {
                auto& globalNodes = config->GetNodes(ConfigScope::kGlobal, 0);
                ImGui::Spacing();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "从全局层同步");
                if (ImGui::Button("导入 Global 缺失节点")) {
                    if (MergeConfigListByKey(a_nodes, globalNodes, [](const NodeDefinition& node) { return node.nodeName; }, false) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("刷新 Global 同名节点")) {
                    if (MergeConfigListByKey(a_nodes, globalNodes, [](const NodeDefinition& node) { return node.nodeName; }, true) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("只覆盖当前层已有的同名节点，并添加缺失节点；不会删除当前层独有节点。");
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            static float s_leftPaneW_node = 240.0f;
            ImGui::BeginChild("NodeLeft", ImVec2(s_leftPaneW_node, 0.0f), true);
            if (openAddNodePopup) ImGui::OpenPopup("AddNodePopup");
            if (ImGui::Button("新建挂载节点", ImVec2(-1.0f, 35.0f))) ImGui::OpenPopup("AddNodePopup");
            if (ImGui::BeginPopupModal("AddNodePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newNodeName[64] = "LeftHip"; static bool isDuplicate = false;
                ImGui::Text("输入节点唯一标识名:"); ImGui::TextDisabled("IAD_CME_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##nnn", newNodeName, 64)) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "错误: 该名称已被使用，请更换！");
                if (ImGui::Button("确认", { 100.0f, 0.0f })) {
                    if (strlen(newNodeName) > 0) {
                        std::string finalName = "IAD_CME_" + std::string(newNodeName); isDuplicate = false;
                        for (auto& n : a_nodes) { if (n.nodeName == finalName) { isDuplicate = true; break; } }
                        if (!isDuplicate) {
                            NodeDefinition nn; nn.nodeName = finalName; nn.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                            a_nodes.push_back(nn); ImGuiManager::s_selectedNode = finalName; ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) { isDuplicate = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, NodeDefinition*> inheritedMap;
            if (ImGuiManager::s_nodeState.scope != ConfigScope::kGlobal) { for (auto& gn : config->GetNodes(ConfigScope::kGlobal, 0)) inheritedMap[gn.nodeName] = &gn; }
            for (auto& n : a_nodes) inheritedMap.erase(n.nodeName);

            if (openCloneNodePopup) ImGui::OpenPopup("CloneNodePopup");
            if (ImGui::Button("复制选中节点", ImVec2(-1.0f, 30.0f))) ImGui::OpenPopup("CloneNodePopup");
            if (ImGui::BeginPopupModal("CloneNodePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char cloneNodeName[64] = "CopiedNode";
                static bool cloneDuplicate = false;
                ImGui::Text("输入新节点唯一标识名:");
                ImGui::TextDisabled("IAD_CME_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##cnn", cloneNodeName, 64)) cloneDuplicate = false;
                if (cloneDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "错误: 该名称已被使用，请更换！");
                if (ImGui::Button("确认", { 100.0f, 0.0f })) {
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
                    auto srcIt = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& n) { return n.nodeName == ImGuiManager::s_selectedNode; });
                    if (srcIt != a_nodes.end()) sourceNode = &(*srcIt);
                    else if (inheritedMap.count(ImGuiManager::s_selectedNode)) sourceNode = inheritedMap[ImGuiManager::s_selectedNode];

                    if (!cloneDuplicate && sourceNode) {
                        NodeDefinition clone = *sourceNode;
                        clone.nodeName = finalName;
                        clone.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json";
                        a_nodes.push_back(clone);
                        ImGuiManager::s_selectedNode = finalName;
                        NodeManager::ClearAllCaches();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("取消", { 100.0f, 0.0f })) {
                    cloneDuplicate = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            for (size_t i = 0; i < a_nodes.size(); i++) {
                auto& n = a_nodes[i]; std::string dispName = (n.nodeName.find("IAD_CME_") == 0) ? n.nodeName.substr(8) : n.nodeName;
                if (ImGui::Selectable((dispName + "###" + n.nodeName).c_str(), ImGuiManager::s_selectedNode == n.nodeName)) ImGuiManager::s_selectedNode = n.nodeName;
            }
            for (auto& [name, nPtr] : inheritedMap) {
                std::string dispName = (name.find("IAD_CME_") == 0) ? name.substr(8) : name;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                if (ImGui::Selectable(("[继承] " + dispName + "###" + name).c_str(), ImGuiManager::s_selectedNode == name)) ImGuiManager::s_selectedNode = name;
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::SameLine(); ImGui::InvisibleButton("##vsplitter_node", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
            if (ImGui::IsItemActive()) { s_leftPaneW_node += ImGui::GetIO().MouseDelta.x; if (s_leftPaneW_node < 120.0f) s_leftPaneW_node = 120.0f; if (s_leftPaneW_node > ImGui::GetWindowWidth() - 150.0f) s_leftPaneW_node = ImGui::GetWindowWidth() - 150.0f; }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); ImGui::SameLine();

            ImGui::BeginChild("NodeRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const NodeDefinition& n) { return n.nodeName == ImGuiManager::s_selectedNode; });
            NodeDefinition* activeNode = nullptr; bool isInherited = false;
            if (it != a_nodes.end()) { activeNode = &(*it); }
            else if (inheritedMap.count(ImGuiManager::s_selectedNode)) { activeNode = inheritedMap[ImGuiManager::s_selectedNode]; isInherited = true; }

            if (activeNode) {
                auto& n = *activeNode; ImGui::PushID(("Node_" + n.nodeName).c_str());
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "💡 此配置继承自全局 (Global)，当前层级未进行覆写。");
                    if (ImGui::Button("➕ 提取到当前层级并覆写 (Add Override)", { -1, 40 })) { a_nodes.push_back(n); ImGuiManager::s_selectedNode = n.nodeName; HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }
                else {
                    std::string shortName = (n.nodeName.find("IAD_CME_") == 0) ? n.nodeName.substr(8) : n.nodeName;
                    char nameBuf[64]; strcpy_s(nameBuf, shortName.c_str());
                    ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "节点名称:"); ImGui::SameLine(); ImGui::TextDisabled("IAD_CME_"); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::InputText("##nodeRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::string newName = std::string("IAD_CME_") + nameBuf;
                        if (!newName.empty() && newName != n.nodeName) {
                            bool exists = false; for (auto& other : a_nodes) if (other.nodeName == newName) exists = true;
                            if (!exists) {
                                std::string oldName = n.nodeName; n.nodeName = newName; ImGuiManager::s_selectedNode = newName;
                                auto& allSlots = config->GetSlots(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState));
                                for (auto& slot : allSlots) { if (slot.targetNode == oldName) slot.targetNode = newName; }
                                auto updateCustomTargets = [&](std::vector<CustomDefinition>& customs) {
                                    for (auto& custom : customs) {
                                        if (custom.targetNode == oldName) custom.targetNode = newName;
                                        for (auto& group : custom.modelGroups) {
                                            if (group.targetNode == oldName) group.targetNode = newName;
                                        }
                                    }
                                    };
                                updateCustomTargets(config->GetCustoms(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState)));
                                if (ImGuiManager::s_nodeState.scope != ConfigScope::kGlobal) {
                                    for (auto& slot : config->GetSlots(ConfigScope::kGlobal, 0)) { if (slot.targetNode == oldName) slot.targetNode = newName; }
                                    updateCustomTargets(config->GetCustoms(ConfigScope::kGlobal, 0));
                                }
                                NodeManager::ClearAllCaches(); HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                    }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f); if (ImGui::Button("删除此节点")) ImGui::OpenPopup("DeleteConfirmNode");
                    if (ImGui::BeginPopupModal("DeleteConfirmNode", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("确定要彻底删除节点 [%s] 吗？", n.nodeName.c_str());
                        if (ImGui::Button("删除", { 100.0f, 0.0f })) {
                            std::string oldName = n.nodeName; a_nodes.erase(it); ImGuiManager::s_selectedNode = "";
                            auto& allSlots = config->GetSlots(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState));
                            for (auto& slot : allSlots) { if (slot.targetNode == oldName) slot.targetNode = ""; }
                            auto clearCustomTargets = [&](std::vector<CustomDefinition>& customs) {
                                for (auto& custom : customs) {
                                    if (custom.targetNode == oldName) custom.targetNode = "";
                                    for (auto& group : custom.modelGroups) {
                                        if (group.targetNode == oldName) group.targetNode = "";
                                    }
                                }
                                };
                            clearCustomTargets(config->GetCustoms(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState)));
                            if (ImGuiManager::s_nodeState.scope != ConfigScope::kGlobal) {
                                for (auto& slot : config->GetSlots(ConfigScope::kGlobal, 0)) { if (slot.targetNode == oldName) slot.targetNode = ""; }
                                clearCustomTargets(config->GetCustoms(ConfigScope::kGlobal, 0));
                            }
                            ConfigManager::GetSingleton()->SaveConfig(); NodeManager::ClearAllCaches(); HolsterManager::GetSingleton()->ForceRefreshAll();
                            ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::PopID(); ImGui::EndChild(); ImGui::End(); return;
                        }
                        ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::Separator();
                    if (ImGui::Checkbox("全局启用", &n.isEnabled)) HolsterManager::GetSingleton()->ForceRefreshAll();
                    ImGui::Separator();
                }

                if (ImGui::TreeNodeEx("目标骨骼", ImGuiTreeNodeFlags_None)) { auto& nodes = n.fallbackHosts; for (size_t x = 0; x < nodes.size(); x++) { ImGui::PushID(static_cast<int>(x)); DrawBoneScannerBox(("##bone" + std::to_string(x)).c_str(), nodes[x]); ImGui::SameLine(); if (ImGui::Button("上移") && x > 0) std::swap(nodes[x], nodes[x - 1]); ImGui::SameLine(); if (ImGui::Button("X")) { nodes.erase(nodes.begin() + x); x--; } ImGui::PopID(); } if (ImGui::Button("添加目标骨骼", { 150.0f, 30.0f })) nodes.push_back("Weapon"); ImGui::TreePop(); }
                ImGui::Spacing();

                if (ImGui::Checkbox("绝对坐标 (Absolute)", &n.absolutePosition)) HolsterManager::GetSingleton()->ForceRefreshAll();
                ImGui::SameLine(); ImGui::TextDisabled("武器/身形自适应：当前变换路径不支持");
                ImGui::Separator();

                if (DrawNodeEditorTabs(n, ImGuiManager::s_nodeState, "liveNodeTabs")) {
                    NodeManager::ClearAllCaches();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (isInherited) ImGui::EndDisabled(); ImGui::PopID();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled("请在左侧选择一个挂载节点"); }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    // 👇========== 🌟 核心面板类实现：专属神兵 ==========👇
    void UICustomsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowCustoms) return;
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("专属展示", &config->uiShowCustoms, ImGuiWindowFlags_MenuBar)) {
            bool doPropagate = DrawWindowHeader("CustomHdr", ImGuiManager::s_customState, true, true);
            DrawConfiguredTargetBrowser("CustomConfiguredTargets", ImGuiManager::s_customState, config->GetConfiguredCustomTargetIDs(ImGuiManager::s_customState.scope));
            uint32_t queryID = GetQueryID(ImGuiManager::s_customState);
            auto& a_customs = config->GetCustoms(ImGuiManager::s_customState.scope, queryID); auto& a_nodes = config->GetNodes(ImGuiManager::s_customState.scope, queryID); auto& a_slots = config->GetSlots(ImGuiManager::s_customState.scope, queryID);
            if (doPropagate) { auto& globalCustoms = config->GetCustoms(ConfigScope::kGlobal, 0); for (auto& c : a_customs) { auto git = std::find_if(globalCustoms.begin(), globalCustoms.end(), [&](const CustomDefinition& gc) { return gc.customName == c.customName; }); if (git != globalCustoms.end()) *git = c; else globalCustoms.push_back(c); } HolsterManager::GetSingleton()->ForceRefreshAll(); }
            bool openAddCustomPopup = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("文件")) {
                    if (ImGui::MenuItem("保存配置")) config->SaveConfig();
                    if (ImGui::MenuItem("关闭")) config->uiShowCustoms = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("操作")) {
                    if (ImGui::MenuItem("New custom display")) openAddCustomPopup = true;
                    if (ImGui::MenuItem("Force refresh displays")) HolsterManager::GetSingleton()->ForceRefreshAll();
					ImGui::Separator();
					if (ImGui::MenuItem("编辑专属展示预设")) {
						config->uiProfileManagedCategory = 2;
						config->uiProfileRequestedTab = 0;
						config->uiShowProfiles = true;
					}
                    if (ImGuiManager::s_customState.scope != ConfigScope::kGlobal) {
                        auto& globalCustomsForMenu = config->GetCustoms(ConfigScope::kGlobal, 0);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import missing Global custom displays")) {
                            if (MergeConfigListByKey(a_customs, globalCustomsForMenu, [](const CustomDefinition& custom) { return custom.customName; }, false) > 0) {
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                        if (ImGui::MenuItem("Refresh matching Global custom displays")) {
                            if (MergeConfigListByKey(a_customs, globalCustomsForMenu, [](const CustomDefinition& custom) { return custom.customName; }, true) > 0) {
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Spacing();
            if (DrawManagedProfileSelector(
                "CustomModuleProfileSelector",
                "专属展示预设",
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
                HolsterManager::GetSingleton()->ForceRefreshAll();
            }
            if (ImGuiManager::s_customState.scope != ConfigScope::kGlobal) {
                auto& globalCustoms = config->GetCustoms(ConfigScope::kGlobal, 0);
                ImGui::Spacing();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "从全局层同步");
                if (ImGui::Button("导入 Global 缺失规则")) {
                    if (MergeConfigListByKey(a_customs, globalCustoms, [](const CustomDefinition& custom) { return custom.customName; }, false) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("刷新 Global 同名规则")) {
                    if (MergeConfigListByKey(a_customs, globalCustoms, [](const CustomDefinition& custom) { return custom.customName; }, true) > 0) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("只覆盖当前层已有的同名 Custom 规则，并添加缺失规则；不会删除当前层独有规则。");
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            static float s_leftPaneW_custom = 240.0f;
            ImGui::BeginChild("CustomLeft", ImVec2(s_leftPaneW_custom, 0.0f), true);
            if (openAddCustomPopup) ImGui::OpenPopup("AddCustomPopup");
            if (ImGui::Button("新建专属物品覆盖", ImVec2(-1.0f, 35.0f))) ImGui::OpenPopup("AddCustomPopup");
            if (ImGui::BeginPopupModal("AddCustomPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newCName[64] = "My_Special_Weapon"; static bool isDuplicate = false; ImGui::Text("输入该覆盖规则的备注名称:"); if (ImGui::InputText("##ncn", newCName, 64)) isDuplicate = false;
                if (isDuplicate) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "错误: 该名称已被使用，请更换！");
                if (ImGui::Button("确认", { 100.0f, 0.0f })) { if (strlen(newCName) > 0) { isDuplicate = false; for (auto& c : a_customs) { if (c.customName == newCName) { isDuplicate = true; break; } } if (!isDuplicate) { CustomDefinition cd; cd.customName = newCName; cd.originPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json"; a_customs.push_back(cd); ImGuiManager::s_selectedCustom = newCName; ImGui::CloseCurrentPopup(); } } } ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) { isDuplicate = false; ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
            }
            ImGui::Separator();

            std::map<std::string, CustomDefinition*> inheritedMap;
            if (ImGuiManager::s_customState.scope != ConfigScope::kGlobal) { for (auto& gc : config->GetCustoms(ConfigScope::kGlobal, 0)) inheritedMap[gc.customName] = &gc; }
            for (auto& c : a_customs) inheritedMap.erase(c.customName);

            for (size_t i = 0; i < a_customs.size(); i++) {
                auto& c = a_customs[i]; if (ImGui::Selectable((c.customName + "###" + c.customName).c_str(), ImGuiManager::s_selectedCustom == c.customName)) ImGuiManager::s_selectedCustom = c.customName;
            }
            for (auto& [name, cPtr] : inheritedMap) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                if (ImGui::Selectable(("[继承] " + name + "###" + name).c_str(), ImGuiManager::s_selectedCustom == name)) ImGuiManager::s_selectedCustom = name;
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::SameLine(); ImGui::InvisibleButton("##vsplitter_custom", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
            if (ImGui::IsItemActive()) { s_leftPaneW_custom += ImGui::GetIO().MouseDelta.x; if (s_leftPaneW_custom < 120.0f) s_leftPaneW_custom = 120.0f; if (s_leftPaneW_custom > ImGui::GetWindowWidth() - 150.0f) s_leftPaneW_custom = ImGui::GetWindowWidth() - 150.0f; }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); ImGui::SameLine();

            ImGui::BeginChild("CustomRight", ImVec2(0.0f, 0.0f), true);
            auto it = std::find_if(a_customs.begin(), a_customs.end(), [&](const CustomDefinition& c) { return c.customName == ImGuiManager::s_selectedCustom; });
            CustomDefinition* activeCustom = nullptr; bool isInherited = false;
            if (it != a_customs.end()) { activeCustom = &(*it); }
            else if (inheritedMap.count(ImGuiManager::s_selectedCustom)) { activeCustom = inheritedMap[ImGuiManager::s_selectedCustom]; isInherited = true; }

            if (activeCustom) {
                auto& c = *activeCustom; ImGui::PushID(("Custom_" + c.customName).c_str());
                if (isInherited) {
                    ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "💡 此配置继承自全局 (Global)，当前层级未进行覆写。");
                    if (ImGui::Button("➕ 提取到当前层级并覆写 (Add Override)", { -1, 40 })) { a_customs.push_back(c); ImGuiManager::s_selectedCustom = c.customName; HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    ImGui::Separator(); ImGui::BeginDisabled();
                }
                else {
                    char nameBuf[64]; strcpy_s(nameBuf, c.customName.c_str()); ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "规则名称:"); ImGui::SameLine(); ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::InputText("##custRename", nameBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::string newName = nameBuf;
                        if (!newName.empty() && newName != c.customName) {
                            bool exists = false; for (auto& other : a_customs) if (other.customName == newName) exists = true;
                            if (!exists) { c.customName = newName; ImGuiManager::s_selectedCustom = newName; HolsterManager::GetSingleton()->ForceRefreshAll(); }
                        }
                    }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f); if (ImGui::Button("删除规则")) ImGui::OpenPopup("DeleteConfirmCust");
                    if (ImGui::BeginPopupModal("DeleteConfirmCust", NULL, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::Text("确定要彻底删除专属规则 [%s] 吗？", c.customName.c_str()); if (ImGui::Button("删除", { 100.0f, 0.0f })) { a_customs.erase(it); ImGuiManager::s_selectedCustom = ""; HolsterManager::GetSingleton()->ForceRefreshAll(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::PopID(); ImGui::EndChild(); ImGui::End(); return; } ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
                    if (ImGui::Checkbox("全局启用", &c.isEnabled)) HolsterManager::GetSingleton()->ForceRefreshAll();
                    ImGui::Separator();
                }

                ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "锁定目标物品 (FormID):"); char formBuf[16]; sprintf_s(formBuf, "%08X", c.targetFormID); ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("##customForm", formBuf, 16, ImGuiInputTextFlags_CharsHexadecimal)) { try { c.targetFormID = std::stoul(formBuf, nullptr, 16); } catch (...) { c.targetFormID = 0; } } ImGui::SameLine();
                if (ImGui::Button("捕获玩家当前装备的武器")) {
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
                                c.customName = std::string("专属: ") + fullName->GetFullName();
                            }
                            else {
                                c.customName = "专属: 未知武器";
                            }
                            ImGuiManager::s_selectedCustom = c.customName;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                    }
                }
                ImGui::Separator();

                if (ImGui::Checkbox("使用运行时目标 FormID (IED Variable Mode)", &c.useRuntimeTargetForm)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (c.useRuntimeTargetForm) {
                    ImGui::Indent();
                    char runtimeFormVar[64];
                    strncpy_s(runtimeFormVar, c.runtimeTargetFormVariable.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(180.0f);
                    if (ImGui::InputText("目标 FormID 变量", runtimeFormVar, sizeof(runtimeFormVar))) {
                        c.runtimeTargetFormVariable = runtimeFormVar;
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##customRuntimeTargetFormPick", "选择")) {
                        const auto variables = ConfigManager::GetSingleton()->GetRuntimeFormVariablesSnapshot();
                        for (const auto& [name, value] : variables) {
                            if (ImGui::Selectable(name.c_str(), c.runtimeTargetFormVariable == name)) {
                                c.runtimeTargetFormVariable = name;
                                HolsterManager::GetSingleton()->ForceRefreshAll();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled("变量值非 0 时替代固定目标 FormID；无值时回退固定目标。");
                    ImGui::Unindent();
                }
                ImGui::Separator();

                if (ImGui::Checkbox("直接展示目标 Form 基础模型（无需在背包中）", &c.displayFormWithoutInventory)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (c.displayFormWithoutInventory) {
                    ImGui::TextDisabled("需要设置目标展示插槽；只读取 Form 的基础 NIF，不读取实例 OMOD。");
                }

                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "🎒 高级展现逻辑 (IED Logic)");
                if (ImGui::Checkbox("忽略玩家 (Ignore Player)", &c.ignorePlayer)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (ImGui::Checkbox("覆盖全局装备模式限制 (Override Equipment Mode)", &c.overrideEquipmentMode)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (c.overrideEquipmentMode) { ImGui::Indent(); if (ImGui::Checkbox("该专属规则仅显示已装备或收藏的武器", &c.displayFavoritesOnly)) { HolsterManager::GetSingleton()->ForceRefreshAll(); } ImGui::Unindent(); }
                if (ImGui::Checkbox("拔出武器时隐藏背部模型 (Always Unload)", &c.alwaysUnload)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (ImGui::Checkbox("模型组模式：仅显示模型组 (Group Mode)", &c.groupMode)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (c.groupMode) {
                    ImGui::TextDisabled("开启后不生成主物品模型和专属枪套，只显示下方模型组条目。");
                    ImGui::TextDisabled("模型组里的 FormID 模型来源会自动参与背包候选选择。");
                }
                if (ImGui::Checkbox("遗留模式 (Last Equipped Mode)", &c.lastEquippedMode)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (c.lastEquippedMode) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("优先最近装备过的 Biped 槽", &c.lastEquippedPrioritizeRecentBipedSlots)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("指定 Biped 槽已占用时禁用", &c.lastEquippedDisableIfBipedSlotOccupied)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (!c.lastEquippedDisableIfBipedSlotOccupied) {
                        ImGui::Indent();
                        if (ImGui::Checkbox("跳过已占用 Biped 槽", &c.lastEquippedSkipOccupiedBipedSlots)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                        ImGui::Unindent();
                    }
                    if (DrawBipedSlotVectorEditor("Last Equipped Biped 槽列表", c.lastEquippedBipedSlots)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (ImGui::Checkbox("优先回到最近展示插槽", &c.lastEquippedPrioritizeRecentDisplaySlot)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("最近展示插槽已占用时跳过", &c.lastEquippedSkipOccupiedDisplaySlots)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("指定展示插槽已占用时禁用", &c.lastEquippedDisableIfDisplaySlotOccupied)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("回退到最近显示过的展示槽物品", &c.lastEquippedFallbackToSlotted)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("允许回退到其他可用插槽", &c.lastEquippedFallbackToAnySlot)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (ImGui::Checkbox("最近装备无匹配时回退到最近获得", &c.lastEquippedFallbackToRecentAcquired)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                    if (c.lastEquippedFallbackToRecentAcquired) {
                        ImGui::Indent();
                        if (ImGui::Checkbox("按最近获得类型排序", &c.lastEquippedPrioritizeRecentAcquiredTypes)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                        if (DrawFormTypeVectorEditor("最近获得类型列表 (空=全局)", c.lastEquippedRecentAcquiredFormTypes)) {
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::Unindent();
                    }
                    if (DrawStringVectorEditor("Last Equipped 展示插槽列表", c.lastEquippedDisplaySlots, "Backpack_Right")) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::TextDisabled("列表为空时表示不限制展示插槽；填入 Slot 名称可限制 Last Equipped 回退范围。");
                    if (ImGui::TreeNodeEx("Last Equipped 专用过滤条件", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("仅 Last Equipped 模式额外评估；普通专属候选仍使用下方候选物品条件。");
                        if (ImGui::Button("清空 Last Equipped 过滤条件##customLastEquippedConditions")) {
                            c.lastEquippedFilterConditionTree = IAD::ConditionNode(true, true);
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::PushID("CustomLastEquippedFilter");
                        if (DrawConditionTreeEditor(c.lastEquippedFilterConditionTree)) {
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        ImGui::PopID();
                        ImGui::TreePop();
                    }
                    ImGui::Unindent();
                }
                if (ImGui::Checkbox("已装备时禁用 (Disable If Equipped)", &c.disableIfEquipped)) { HolsterManager::GetSingleton()->ForceRefreshAll(); }
                if (ImGui::TreeNodeEx("额外候选物品 (Extra Items)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("这些 FormID 会和锁定目标物品共用同一条专属规则。");
                    if (DrawFormVectorUI(c.extraItems, "CustomExtraItems")) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::TreePop();
                }
                if (c.lastEquippedMode) ImGui::BeginDisabled();
                if (ImGui::Checkbox("随机选择候选物品 (Select Inventory Random)", &c.selectInventoryRandom)) {
                    if (c.selectInventoryRandom) c.selectInventoryStrongest = false;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("选择最强候选物品 (Select Inventory Strongest)", &c.selectInventoryStrongest)) {
                    if (c.selectInventoryStrongest) c.selectInventoryRandom = false;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (c.lastEquippedMode) {
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Last Equipped 模式固定使用最近装备/展示历史，不使用随机或最强候选排序。");
                }
                if (ImGui::TreeNodeEx("候选物品条件 (Candidate Item Conditions)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("这些条件会在专属规则选择背包候选物品时评估，也会作用于 Last Equipped 模式。");
                    if (ImGui::Button("清空候选条件##customCandidateConditions")) {
                        c.inventoryConditionTree = IAD::ConditionNode(true, true);
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    if (DrawConditionTreeEditor(c.inventoryConditionTree)) {
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::TreePop();
                }
                ImGui::Spacing(); ImGui::SetNextItemWidth(200.0f); ImGui::SliderFloat("生成概率 (Spawn Chance)%", &c.spawnChance, 0.0f, 100.0f, "%.1f%%");
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "🔢 数量感知系统 (Count Range)");
                ImGui::SetNextItemWidth(100.0f); ImGui::InputInt("最少需要 (Min)", &c.countMin); ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f); ImGui::InputInt("最多显示 (Max)", &c.countMax);
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored({ 1.0f, 0.4f, 0.8f, 1.0f }, "🎭 模型强制替换 (Model Swap)");
                char modelPath[256]; strcpy_s(modelPath, c.modelSwapPath.c_str()); ImGui::SetNextItemWidth(400.0f);
                ImGui::InputText("模型路径", modelPath, 256);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    c.modelSwapPath = modelPath;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawFormIDField("模型替换 FormID", c.modelSwapFormID)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawModelSwapVariableSource(c.modelSwapVariableSource, "custom_model_var_source")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::TextDisabled("路径优先于 FormID；FormID 用指定物品的模型替换当前 Custom 主模型。");
                ImGui::Separator();

                ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "特殊物品渲染设置");
                if (ImGui::Checkbox("提取武器弹匣 (Extract Magazine)##Custom", &c.extractMagazine)) {
                    if (c.extractMagazine) c.useProjectileForAmmo = false;
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                bool disableCustomAmmo = c.extractMagazine; if (disableCustomAmmo) ImGui::BeginDisabled();
                if (ImGui::Checkbox("【仅限弹药】提取射弹(单发子弹)模型，代替默认弹药盒", &c.useProjectileForAmmo)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (disableCustomAmmo) ImGui::EndDisabled();
                if (ImGui::Checkbox("使用世界/基础模型 (Use World Model)##Custom", &c.useWorldModel)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("隐藏几何 / alpha 0 (Invisible)##Custom", &c.invisible)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("仅隐藏几何，保留挂载节点 (Hide Geometry)##Custom", &c.hideGeometry)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("隐藏附加灯光 (Hide Light)##Custom", &c.hideLight)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("加载第一人称武器模型 (Load 1P Weapon Model)##Custom", &c.load1pWeaponModel)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("保留火焰/喷焰 FX (Keep Torch Flame)##Custom", &c.keepTorchFlame)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (ImGui::Checkbox("移除刀鞘/枪套节点 (Remove Scabbard)##Custom", &c.removeScabbard)) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawModelCleanupSettings(c.disableHavok, c.removeEditorMarker, c.removeProjectileTracers, "custom_cleanup")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawModelAnimationSettings(c.animation, "custom_animation")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawModelEffectShaderSettings(c.effectShader, "custom_effect")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (DrawModelLightSettings(c.light, "custom_light")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::Separator();

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "强制修改挂载节点 (Target Node Overwrite):"); ImGui::SameLine();
                DrawCMENodeSelector("##custTargetNode", c.targetNode, a_nodes, ImGuiManager::s_customState.scope, "[不覆盖] 跟随该物品的 Slot 默认节点"); ImGui::Separator();

                ImGui::TextColored({ 1.0f, 0.8f, 0.2f, 1.0f }, "绑定展示插槽 (Display Slot):"); ImGui::SameLine();
                const char* selectedSlotLabel = c.targetDisplaySlot.empty() ? "[自动] 匹配任意合格 Slot" : c.targetDisplaySlot.c_str();
                if (ImGui::BeginCombo("##custTargetDisplaySlot", selectedSlotLabel)) {
                    if (ImGui::Selectable("[自动] 匹配任意合格 Slot", c.targetDisplaySlot.empty())) {
                        c.targetDisplaySlot.clear();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    for (const auto& slot : a_slots) {
                        const bool selected = c.targetDisplaySlot == slot.slotName;
                        if (ImGui::Selectable(slot.slotName.c_str(), selected)) {
                            c.targetDisplaySlot = slot.slotName;
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Separator();

                if (DrawCustomEditorTabs(c, ImGuiManager::s_customState, a_nodes, "liveCustomTabs")) {
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                if (isInherited) ImGui::EndDisabled(); ImGui::PopID();
            }
            else { ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled("请在左侧选择一个专属物品规则"); }
            ImGui::EndChild();
        }
        ImGui::End();
    }
    // 👇========== 🌟 核心面板类实现：全局过滤器 ==========👇
    void UIProfileSlotsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileSlots) return;

        static char profileName[64] = "NewSlotProfile";
        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Equipment Slot Profile Editor", &config->uiShowProfileSlots, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneSlotProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Slots(),
                profileName,
                sizeof(profileName),
                DrawSlotProfileRecord,
                &config->uiShowProfileSlots);
        }
        ImGui::End();
    }

    void UIProfileCustomsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileCustoms) return;

        static char profileName[64] = "NewCustomProfile";
        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Custom Display Profile Editor", &config->uiShowProfileCustoms, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneCustomProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Customs(),
                profileName,
                sizeof(profileName),
                DrawCustomProfileRecord,
                &config->uiShowProfileCustoms);
        }
        ImGui::End();
    }

    void UIProfileNodesWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileNodes) return;

        static char profileName[64] = "NewNodeProfile";
        ImGui::SetNextWindowSize({ 900.0f, 650.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Gear Positioning Profile Editor", &config->uiShowProfileNodes, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneNodeProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Nodes(),
                profileName,
                sizeof(profileName),
                DrawNodeProfileRecord,
                &config->uiShowProfileNodes);
        }
        ImGui::End();
    }

    void UIProfileFormFiltersWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileFormFilters) return;

        static char profileName[64] = "NewFormFilterProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Form Filter Profile Editor", &config->uiShowProfileFormFilters, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneFormFilterProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().FormFilters(),
                profileName,
                sizeof(profileName),
                DrawFormFilterProfileRecord,
                &config->uiShowProfileFormFilters);
        }
        ImGui::End();
    }

    void UIProfileModelGroupsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileModelGroups) return;

        static char profileName[64] = "NewModelGroupProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Model Group Profile Editor", &config->uiShowProfileModelGroups, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneModelGroupProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().ModelGroups(),
                profileName,
                sizeof(profileName),
                DrawModelGroupProfileRecord,
                &config->uiShowProfileModelGroups);
        }
        ImGui::End();
    }

    void UIProfileNodeMonitorsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileNodeMonitors) return;

        static char profileName[64] = "NewNodeMonitorProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Node Monitor Profile Editor", &config->uiShowProfileNodeMonitors, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneNodeMonitorProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().NodeMonitors(),
                profileName,
                sizeof(profileName),
                DrawNodeMonitorProfileRecord,
                &config->uiShowProfileNodeMonitors);
        }
        ImGui::End();
    }

    void UIProfileConditionsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileConditions) return;

        static char profileName[64] = "NewConditionProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Condition Profile Editor", &config->uiShowProfileConditions, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneConditionProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Conditions(),
                profileName,
                sizeof(profileName),
                DrawConditionProfileRecord,
                &config->uiShowProfileConditions);
        }
        ImGui::End();
    }

    void UIProfileTransformsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfileTransforms) return;

        static char profileName[64] = "NewTransformProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Transform Profile Editor", &config->uiShowProfileTransforms, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandaloneTransformProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Transforms(),
                profileName,
                sizeof(profileName),
                DrawTransformProfileRecord,
                &config->uiShowProfileTransforms);
        }
        ImGui::End();
    }

    void UIProfilePhysicsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        if (!config->uiShowProfilePhysics) return;

        static char profileName[64] = "NewPhysicsProfile";
        ImGui::SetNextWindowSize({ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD Physics Profile Editor", &config->uiShowProfilePhysics, ImGuiWindowFlags_MenuBar)) {
            DrawManagedProfileEditor(
                "StandalonePhysicsProfileEditor",
                IAD::Profile::GlobalProfileManager::GetSingleton().Physics(),
                profileName,
                sizeof(profileName),
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
        static char managedProfileName[64] = "NewProfile";

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

		static constexpr ProfileCategoryDesc categories[] = {
			{ "装备插槽集合", "Slot" },
			{ "挂载节点覆写集合", "NodeOverrides" },
			{ "专属展示集合", "Custom" },
			{ "当前专属展示的模型组", "ModelGroups" },
			{ "节点监控列表", "NodeMonitors" },
			{ "自动条件变量", "ConditionalVariables" }
		};

        static constexpr ProfileCategoryDesc contentCategories[] = {
            { "条件树", "Conditions" },
            { "变换", "Transforms" },
            { "物理", "Physics" },
            { "表单过滤器", "FormFilters" }
        };

        ImGui::SetNextWindowSize({ 560.0f, 480.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD 预设库", &config->uiShowProfiles, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginTabBar("IADProfileEditorTabs")) {
                const bool selectModuleTab = config->uiProfileRequestedTab == 0;
                if (ImGui::BeginTabItem("模块预设库", nullptr, selectModuleTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (selectModuleTab) config->uiProfileRequestedTab = -1;
                    auto& profileManagers = IAD::Profile::GlobalProfileManager::GetSingleton();
                    if (!profileManagers.IsLoaded()) {
                        profileManagers.LoadAll();
                    }

                    static constexpr const char* managedCategories[] = {
                        "装备插槽",
                        "挂载节点",
                        "专属展示",
                        "模型组",
                        "节点监控",
                        "条件",
                        "变换",
                        "物理",
                        "表单过滤器"
                    };

                    const int managedCategoryCount = static_cast<int>(sizeof(managedCategories) / sizeof(managedCategories[0]));
                    config->uiProfileManagedCategory = std::clamp(config->uiProfileManagedCategory, 0, managedCategoryCount - 1);
                    ImGui::SetNextItemWidth(210.0f);
                    ImGui::Combo("预设类型", &config->uiProfileManagedCategory, managedCategories, managedCategoryCount);
                    ImGui::Separator();

                    switch (config->uiProfileManagedCategory) {
                    case 0:
                        DrawManagedProfileEditor("ManagedSlots", profileManagers.Slots(), managedProfileName, sizeof(managedProfileName), DrawSlotProfileRecord);
                        break;
                    case 1:
                        DrawManagedProfileEditor("ManagedNodes", profileManagers.Nodes(), managedProfileName, sizeof(managedProfileName), DrawNodeProfileRecord);
                        break;
                    case 2:
                        DrawManagedProfileEditor("ManagedCustoms", profileManagers.Customs(), managedProfileName, sizeof(managedProfileName), DrawCustomProfileRecord);
                        break;
                    case 3:
                        DrawManagedProfileEditor("ManagedModelGroups", profileManagers.ModelGroups(), managedProfileName, sizeof(managedProfileName), DrawModelGroupProfileRecord);
                        break;
                    case 4:
                        DrawManagedProfileEditor("ManagedNodeMonitors", profileManagers.NodeMonitors(), managedProfileName, sizeof(managedProfileName), DrawNodeMonitorProfileRecord);
                        break;
                    case 5:
                        DrawManagedProfileEditor("ManagedConditions", profileManagers.Conditions(), managedProfileName, sizeof(managedProfileName), DrawConditionProfileRecord);
                        break;
                    case 6:
                        DrawManagedProfileEditor("ManagedTransforms", profileManagers.Transforms(), managedProfileName, sizeof(managedProfileName), DrawTransformProfileRecord);
                        break;
                    case 7:
                        DrawManagedProfileEditor("ManagedPhysics", profileManagers.Physics(), managedProfileName, sizeof(managedProfileName), DrawPhysicsProfileRecord);
                        break;
                    case 8:
                        DrawManagedProfileEditor("ManagedFormFilters", profileManagers.FormFilters(), managedProfileName, sizeof(managedProfileName), DrawFormFilterProfileRecord);
                        break;
                    default:
                        break;
                    }

                    ImGui::EndTabItem();
                }

                const bool selectSnapshotTab = config->uiProfileRequestedTab == 1;
                if (ImGui::BeginTabItem("全局快照", nullptr, selectSnapshotTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (selectSnapshotTab) config->uiProfileRequestedTab = -1;
                    auto exports = config->GetAvailableExports();
                    ImGui::SetNextItemWidth(260.0f);
                    DrawProfileCombo("Snapshot##IADProfileSnapshot", exports, selectedSnapshot);
                    ImGui::SameLine();
                    if (ImGui::Button("刷新##IADProfileSnapshot")) {
                        selectedSnapshot.clear();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("名称##IADProfileSnapshotName", snapshotName, sizeof(snapshotName));
                    DrawProfileFlagMatrix(snapshotFlags);

                    ImGui::RadioButton("覆盖导入", &snapshotImportMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("合并导入", &snapshotImportMode, 1);
                    ImGui::Separator();

                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("导入所选", { 130.0f, 0.0f })) {
                        config->ImportPreset(selectedSnapshot, snapshotFlags, snapshotImportMode == 1);
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                        snapshotStatusText = "已导入所选快照。";
                    }
                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (std::strlen(snapshotName) == 0 || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("按名称导出", { 130.0f, 0.0f })) {
                        config->ExportPreset(snapshotName, snapshotFlags);
                        selectedSnapshot = snapshotName;
                        snapshotStatusText = "已导出快照。";
                    }
                    if (std::strlen(snapshotName) == 0 || snapshotFlags == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (selectedSnapshot.empty() || snapshotFlags == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("覆盖所选", { 140.0f, 0.0f })) {
                        config->ExportPreset(selectedSnapshot, snapshotFlags);
                        snapshotStatusText = "已覆盖所选快照。";
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
                if (showLegacyDirectImport && ImGui::BeginTabItem("当前配置")) {
                    const int categoryCount = static_cast<int>(sizeof(categories) / sizeof(categories[0]));
                    if (ImGui::BeginCombo("预设类型##IADProfileCategory", categories[profileCategory].label)) {
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
                        DrawWindowHeader("ProfileSlotScope", ImGuiManager::s_slotState, true, false);
                    }
                    else if (profileCategory == 1) {
                        DrawWindowHeader("ProfileNodeScope", ImGuiManager::s_nodeState, true, false);
                    }
                    else if (profileCategory == 2 || profileCategory == 3) {
                        DrawWindowHeader("ProfileCustomScope", ImGuiManager::s_customState, true, false);
                    }

                    auto profiles = config->GetAvailableProfiles(categories[profileCategory].folder);
                    ImGui::SetNextItemWidth(260.0f);
                    DrawProfileCombo("Profile##IADModuleProfile", profiles, selectedProfile);
                    ImGui::SameLine();
                    if (ImGui::Button("刷新##IADModuleProfile")) {
                        selectedProfile.clear();
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("Name##IADModuleProfileName", profileName, sizeof(profileName));
                    ImGui::Separator();

                    auto saveProfile = [&]() {
                        switch (profileCategory) {
                        case 0:
                            config->SaveSlotProfile(profileName, config->GetSlots(ImGuiManager::s_slotState.scope, GetQueryID(ImGuiManager::s_slotState)));
                            statusText = "Saved slot profile.";
                            break;
                        case 1:
                            config->SaveNodeProfile(profileName, config->GetNodes(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState)));
                            statusText = "Saved node override profile.";
                            break;
                        case 2:
                            config->SaveCustomProfile(profileName, config->GetCustoms(ImGuiManager::s_customState.scope, GetQueryID(ImGuiManager::s_customState)));
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
                            loaded = config->LoadSlotProfile(selectedProfile, config->GetSlots(ImGuiManager::s_slotState.scope, GetQueryID(ImGuiManager::s_slotState)), overwrite);
                            break;
                        case 1:
                            loaded = config->LoadNodeProfile(selectedProfile, config->GetNodes(ImGuiManager::s_nodeState.scope, GetQueryID(ImGuiManager::s_nodeState)), overwrite);
                            break;
                        case 2:
                            loaded = config->LoadCustomProfile(selectedProfile, config->GetCustoms(ImGuiManager::s_customState.scope, GetQueryID(ImGuiManager::s_customState)), overwrite);
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
                            config->SaveConfig();
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                            statusText = overwrite ? "Loaded profile with overwrite." : "Loaded profile with merge.";
                        }
                        else {
                            statusText = "Profile load failed.";
                        }
                        };

                    if (std::strlen(profileName) == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("保存当前配置", { 120.0f, 0.0f })) {
                        saveProfile();
                        selectedProfile = profileName;
                    }
                    if (std::strlen(profileName) == 0) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (selectedProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button("合并加载", { 120.0f, 0.0f })) {
                        loadProfile(false);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("覆盖加载", { 130.0f, 0.0f })) {
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
                if (showLegacyContentProfiles && ImGui::BeginTabItem("内容预设")) {
                    const int contentCategoryCount = static_cast<int>(sizeof(contentCategories) / sizeof(contentCategories[0]));
                    if (ImGui::BeginCombo("内容类型##IADContentProfileCategory", contentCategories[contentCategory].label)) {
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
                    if (ImGui::Button("刷新##IADContentProfile")) {
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

                    if (ImGui::Button("新建空白内容", { 100.0f, 0.0f })) {
                        resetContent();
                    }
                    ImGui::SameLine();
                    if (selectedContentProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button("加载选中内容", { 120.0f, 0.0f })) {
                        loadContent();
                    }
                    if (selectedContentProfile.empty()) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (std::strlen(contentProfileName) == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("按名称保存", { 120.0f, 0.0f })) {
                        saveContent(contentProfileName);
                    }
                    if (std::strlen(contentProfileName) == 0) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (selectedContentProfile.empty()) ImGui::BeginDisabled();
                    if (ImGui::Button("覆盖选中内容", { 140.0f, 0.0f })) {
                        saveContent(selectedContentProfile);
                    }
                    if (selectedContentProfile.empty()) ImGui::EndDisabled();

                    DrawProfileFileActions("ContentProfileFileActions", config, contentCategories[contentCategory].folder, selectedContentProfile, contentProfileName, sizeof(contentProfileName), contentStatusText);

                    ImGui::Separator();
                    static constexpr const char* applyTargets[] = { "当前选中插槽", "当前选中节点", "当前选中专属展示" };
                    if (contentCategory == 3) {
                        ImGui::TextDisabled("应用目标：当前选中插槽的物品过滤器。");
                    }
                    else {
                        ImGui::SetNextItemWidth(180.0f);
                        ImGui::Combo("应用目标##IADContentApplyTarget", &contentApplyTarget, applyTargets, 3);
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
                                    ApplyTransformToConfigBase(*slot, contentTransform, ImGuiManager::s_slotState);
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
                                    ApplyTransformToConfigBase(*node, contentTransform, ImGuiManager::s_nodeState);
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
                                    ApplyTransformToConfigBase(*custom, contentTransform, ImGuiManager::s_customState);
                                }
                                else if (contentCategory == 2) {
                                    custom->physics = contentPhysics;
                                }
                                applied = true;
                            }
                        }

                        if (applied) {
                            config->SaveConfig();
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                            contentStatusText = "Applied profile content to active config.";
                        }
                        else {
                            contentStatusText = "No target is available in the current scope.";
                        }
                        };

                    if (ImGui::Button("应用编辑器内容", { 150.0f, 0.0f })) {
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
                        DrawIEDTransformWidget("Position", contentTransform.pos, { 0,0,0 }, 0.5f, false);
                        DrawIEDTransformWidget("Rotation", contentTransform.rot, { 0,0,0 }, 1.0f, true);
                        DrawIEDTransformWidget("Pivot", contentTransform.pivot, { 0,0,0 }, 0.5f, false);
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

                if (ImGui::BeginTabItem("预设文件库")) {
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
        auto reloadManagedProfile = [&]() {
            if (!managedProfiles.IsInitialized()) {
                managedProfiles.Load();
            }
            if (!m_selectedProf.empty()) {
                managedProfiles.ReloadProfile(m_selectedProf);
            }
            HolsterManager::GetSingleton()->ForceRefreshAll();
        };
        ImGui::SetNextWindowSize({ 800.0f, 600.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("📋 全局过滤器 (Form Filters)", &config->uiShowFilters, ImGuiWindowFlags_MenuBar)) {
            bool openAddFilterPopup = false;
            bool openDeleteFilterPopup = false;
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    const bool hasSelection = !m_selectedProf.empty();
                    if (!hasSelection) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Save filter")) {
                        config->SaveFormFilterProfile(m_selectedProf, m_filterData);
                        reloadManagedProfile();
                    }
                    if (ImGui::MenuItem("Reload selected")) {
                        config->LoadFormFilterProfile(m_selectedProf, m_filterData);
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
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "全局黑白名单预设库 (Form Filter Profiles)");
            ImGui::TextDisabled("你在这里建立的各种黑白名单词典，可以在【装备插槽】面板中被无限次一键导入。"); ImGui::Separator(); ImGui::Spacing();

            static float s_leftPaneW_filt = 240.0f;
            ImGui::BeginChild("FiltLeft", ImVec2(s_leftPaneW_filt, 0.0f), true);
            if (openAddFilterPopup) ImGui::OpenPopup("AddFilterPopup");
            if (ImGui::Button("新建过滤器预设", ImVec2(-1.0f, 35.0f))) ImGui::OpenPopup("AddFilterPopup");
            if (ImGui::BeginPopupModal("AddFilterPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char newFiltName[64] = "My_Blacklist"; static bool isDuplicate = false;
                ImGui::Text("输入该过滤器的唯一标识名:"); if (ImGui::InputText("##nfn", newFiltName, 64)) isDuplicate = false;
                if (ImGui::Button("确认", { 100.0f, 0.0f })) {
                    if (strlen(newFiltName) > 0) {
                        m_selectedProf = newFiltName;
                        m_filterData = IAD::FormFilter();
                        config->SaveFormFilterProfile(m_selectedProf, m_filterData);
                        reloadManagedProfile();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine(); if (ImGui::Button("取消", { 100.0f, 0.0f })) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Separator();

            auto profiles = config->GetAvailableProfiles("FormFilters");
            for (const auto& p : profiles) {
                if (ImGui::Selectable((p + "###" + p).c_str(), m_selectedProf == p)) {
                    m_selectedProf = p;
                    config->LoadFormFilterProfile(p, m_filterData);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine(); ImGui::InvisibleButton("##vsplitter_filt", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
            if (ImGui::IsItemActive()) { s_leftPaneW_filt += ImGui::GetIO().MouseDelta.x; if (s_leftPaneW_filt < 120.0f) s_leftPaneW_filt = 120.0f; if (s_leftPaneW_filt > ImGui::GetWindowWidth() - 150.0f) s_leftPaneW_filt = ImGui::GetWindowWidth() - 150.0f; }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); ImGui::SameLine();

            ImGui::BeginChild("FiltRight", ImVec2(0.0f, 0.0f), true);
            if (!m_selectedProf.empty()) {
                ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "当前正在编辑: %s", m_selectedProf.c_str()); ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
                if (ImGui::Button("💾 保存词典")) {
                    config->SaveFormFilterProfile(m_selectedProf, m_filterData);
                    reloadManagedProfile();
                }
                ImGui::Separator();

                DrawFormFilterUI(m_filterData, "GlobalFiltEdit");

                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                if (openDeleteFilterPopup) ImGui::OpenPopup("DelFiltConfirm");
                if (ImGui::Button("删除此词典 (Delete)", { 150, 30 })) ImGui::OpenPopup("DelFiltConfirm");
                if (ImGui::BeginPopupModal("DelFiltConfirm", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("确定要彻底删除预设 [%s] 吗？", m_selectedProf.c_str());
                    if (ImGui::Button("删除", { 100,0 })) {
                        if (!managedProfiles.IsInitialized()) {
                            managedProfiles.Load();
                        }
                        managedProfiles.DeleteProfile(m_selectedProf);
                        m_selectedProf = ""; ImGui::CloseCurrentPopup();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine(); if (ImGui::Button("取消", { 100,0 })) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            else {
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() / 2.0f - 20.0f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2.0f - 120.0f); ImGui::TextDisabled("请在左侧选择或新建一个过滤器词典");
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
        if (ImGui::Begin("骨骼扫描仪 (Bone Scanner)", &config->uiShowBoneScanner)) {
            if (ImGui::Button("扫描当前玩家骨骼", { ImGui::GetContentRegionAvail().x * 0.65f, 30 })) {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (player && player->Get3D(false)) {
                    s_flatBoneList.clear(); s_boneTree.children.clear();
                    BuildTree(player->Get3D(false), s_boneTree); s_hasCachedTree = true;
                }
            }
            ImGui::SameLine();
            if (!s_hasCachedTree) ImGui::BeginDisabled();
            if (ImGui::Button("💾 导出文本", { -1, 30 })) ImGui::OpenPopup("ExportTreeFilters");
            if (!s_hasCachedTree) ImGui::EndDisabled();

            if (ImGui::BeginPopupModal("ExportTreeFilters", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                static ExportFilters s_currentFilters;
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "选择要排除(屏蔽)的骨骼类型：");
                ImGui::TextDisabled("被排除的骨骼及其下属所有层级都不会被导出，极大精简列表。"); ImGui::Separator(); ImGui::Spacing();
                ImGui::Checkbox("屏蔽 IAD 自定义挂载点 (IAD_*)", &s_currentFilters.excludeIAD);
                ImGui::Checkbox("屏蔽 物理器官/皮肤变形节点 (CBP/乳/臀/私密/Skin等)", &s_currentFilters.excludePhysics);
                ImGui::Checkbox("屏蔽 头部/面部/表情/头发树 (BSFaceGenNiNodeSkinned)", &s_currentFilters.excludeFaceGen);
                ImGui::Checkbox("屏蔽 动态附加模型 (当前穿着的护甲/拿着的武器/哔哔小子)", &s_currentFilters.excludeEquipments);
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                if (ImGui::Button("生成并导出 (Export)", { 150.0f, 30.0f })) {
                    std::filesystem::create_directories("Data/F4SE/Plugins/ImmersiveArsenalDisplays/Exports");
                    std::string exportPath = "Data/F4SE/Plugins/ImmersiveArsenalDisplays/Exports/BoneTree_Export.txt";
                    std::ofstream outFile(exportPath);
                    if (outFile.is_open()) {
                        outFile << "==========================================\n Immersive Arsenal Displays - 纯净骨架快照\n==========================================\n过滤规则启用状态：\n";
                        outFile << " - IAD节点: " << (s_currentFilters.excludeIAD ? "已屏蔽" : "保留") << "\n - 物理骨骼: " << (s_currentFilters.excludePhysics ? "已屏蔽" : "保留") << "\n - 面部骨架: " << (s_currentFilters.excludeFaceGen ? "已屏蔽" : "保留") << "\n - 动态装备: " << (s_currentFilters.excludeEquipments ? "已屏蔽" : "保留") << "\n==========================================\n\n";
                        ExportBoneTreeRecursive(s_boneTree, outFile, 0, s_currentFilters); outFile.close();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("取消 (Cancel)", { 150.0f, 30.0f })) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Separator(); ImGui::Text("搜索节点:"); ImGui::SameLine();
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

        if (ImGui::Begin("透视骨骼与可视化系统 (Visualizer)", &config->uiShowVisualizer)) {
            auto* hm = HolsterManager::GetSingleton();

            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "1. 游戏原生骨骼 (Vanilla Bones)");
            ImGui::Checkbox("显示骨骼##v", &hm->debugSettings.showVanilla); ImGui::SameLine();
            ImGui::Checkbox("名字##v", &hm->debugSettings.showVanillaNames); ImGui::SameLine();
            ImGui::Checkbox("坐标轴##v", &hm->debugSettings.showVanillaAxes);
            ImGui::Separator();

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "2. 物理挂载基座 (CME Nodes)");
            ImGui::Checkbox("显示基座##c", &hm->debugSettings.showCME); ImGui::SameLine();
            ImGui::Checkbox("名字##c", &hm->debugSettings.showCMENames); ImGui::SameLine();
            ImGui::Checkbox("坐标轴##c", &hm->debugSettings.showCMEAaxes);
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "3. 武器展示插槽 (MOV Nodes)");
            ImGui::Checkbox("显示插槽##m", &hm->debugSettings.showMOV); ImGui::SameLine();
            ImGui::Checkbox("名字##m", &hm->debugSettings.showMOVNames); ImGui::SameLine();
            ImGui::Checkbox("坐标轴##m", &hm->debugSettings.showMOVAxes);
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "4. 节点监控预设 (Node Monitor)");
            if (ImGui::Checkbox("只显示监控列表中的节点", &config->nodeMonitorUseFilter)) {
                config->SaveConfig();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("列表为空时不会过滤。");

            if (DrawManagedProfileSelector(
                "NodeMonitorProfileSelector",
                "节点监控预设 (Node Monitor Profile)",
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
                config->SaveConfig();
            }

            static char monitorNodeName[96] = "";
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##monitorNodeName", monitorNodeName, 96);
            ImGui::SameLine();
            if (ImGui::Button("添加节点名")) {
                config->AddNodeMonitorName(monitorNodeName);
                config->SaveConfig();
                monitorNodeName[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("添加当前 Node")) {
                if (!ImGuiManager::s_selectedNode.empty()) {
                    config->AddNodeMonitorName(ImGuiManager::s_selectedNode);
                    config->SaveConfig();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("添加当前 Slot MOV")) {
                if (!ImGuiManager::s_selectedSlot.empty()) {
                    config->AddNodeMonitorName("IAD_MOV_" + ImGuiManager::s_selectedSlot);
                    config->SaveConfig();
                }
            }

            auto monitorNames = config->GetNodeMonitorNamesSnapshot();
            if (ImGui::BeginTable("NodeMonitorTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("节点", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < monitorNames.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", monitorNames[i].c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Button("移除")) {
                        config->RemoveNodeMonitorName(i);
                        config->SaveConfig();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::Separator();

            ImGui::TextDisabled("坐标轴图例: 红(Red)=X轴, 绿(Green)=Y轴, 蓝(Blue)=Z轴");
            ImGui::TextDisabled("选中高亮: 在菜单中选中节点时将呈现呼吸灯光环");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 1.0f, 1.0f), "5. 坐标系模式 (Coordinate Space)");
            if (ImGui::RadioButton("局部坐标系 (Local Space - 随骨骼翻转)", hm->debugSettings.useLocalAxesSpace == true)) hm->debugSettings.useLocalAxesSpace = true;
            if (ImGui::RadioButton("世界坐标系 (World Space - 绝对东南西北)", hm->debugSettings.useLocalAxesSpace == false)) hm->debugSettings.useLocalAxesSpace = false;
        }
        ImGui::End();
    }

} // 🌟 别忘了这个大括号，把 IAD::UI 命名空间闭合！
