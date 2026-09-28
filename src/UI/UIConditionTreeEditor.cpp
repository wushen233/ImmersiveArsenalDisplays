#include "pch.h"
#include "UILocalization.h"
#include "UIConditionTreeEditor.h"
#include "Engine/ConditionSystem.h"
#include "ImGuiManager.h"
#include "UIConditionCatalog.h"
#include "UIRecordSelectionState.h"
#include "UIEditorContextStore.h"

#include <RE/P/PlayerCharacter.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>

namespace IAD::UI {
    namespace {
        UIEditorContext& GetSlotEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Slot(); }
        UIEditorContext& GetNodeEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Node(); }

        static void DrawKeywordScannerBoxInternal(const char* id, std::string& outKeyword) {
        char buf[128]; strcpy_s(buf, outKeyword.c_str()); ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText(id, buf, 128)) outKeyword = buf;
        ImGui::SameLine(); ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo((std::string(TextLiteral("扫描身上关键字##")) + id).c_str(), TextLiteral("展开选择..."))) {
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
            isNodeOpen = ImGui::TreeNodeEx("##grp", groupFlags, node.children.empty() ? TextLiteral("条件组 (空)") : TextLiteral("条件组"));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("+##add")) ImGui::OpenPopup("GrpAdd");
            if (ImGui::BeginPopup("GrpAdd")) {
                if (ImGui::MenuItem(TextLiteral("添加条件"))) node.children.push_back(IAD::ConditionNode(false));
                if (ImGui::MenuItem(TextLiteral("添加子组"))) node.children.push_back(IAD::ConditionNode(true));
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130.0f);
            const char* logicModes[] = { TextLiteral("满足任意条件"), TextLiteral("必须全部满足") };
            int logicMode = node.isAnd ? 1 : 0;
            if (ImGui::Combo("##logic", &logicMode, logicModes, IM_ARRAYSIZE(logicModes))) node.isAnd = logicMode == 1;
            ImGui::SameLine();
            ImGui::Checkbox(TextLiteral("取反##not"), &node.isNot);
            if (!isRoot) { ImGui::SameLine(); if (ImGui::Button(TextLiteral("删除##del"))) deleteMe = true; }
        }
        else {
            const std::string title = node.type.empty() ? TextLiteral("选择条件") : node.type;
            const auto conditionFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
            isNodeOpen = ImGui::TreeNodeEx("##cond", conditionFlags, "%s  [%s]", title.c_str(), node.expected ? TextLiteral("真") : TextLiteral("假"));
            ImGui::SameLine();
            if (ImGui::Button(TextLiteral("删除##del"))) deleteMe = true;
        }

        if (!node.isGroup && isNodeOpen) {
            const auto& condTypes = UIConditionCatalog::Types();
            const int numTypes = static_cast<int>(condTypes.size());
            int curT = 0;
            for (int n = 0; n < numTypes; n++) if (node.type == condTypes[n]) curT = n;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::Combo(TextLiteral("条件类型"), &curT, condTypes.data(), numTypes)) node.type = condTypes[curT];
            ImGui::PushStyleColor(ImGuiCol_Text, node.expected ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::BeginCombo(TextLiteral("期望结果"), node.expected ? TextLiteral("为真") : TextLiteral("为假"))) {
                if (ImGui::Selectable(TextLiteral("为真"), node.expected == true)) node.expected = true;
                if (ImGui::Selectable(TextLiteral("为假"), node.expected == false)) node.expected = false;
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::Checkbox(TextLiteral("取反"), &node.isNot);
            ImGui::Separator();

            if (node.type == "RuntimeVariable") {
                ImGui::SameLine();
                char varBuf[64]; strncpy_s(varBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##var", varBuf, 64)) node.keyword = varBuf;
                ImGui::SameLine();
                if (ImGui::BeginCombo("##varPick", TextLiteral("变量"))) {
                    auto variables = ConfigManager::GetSingleton()->GetRuntimeVariablesSnapshot();
                    for (const auto& [name, value] : variables) {
                        if (ImGui::Selectable(name.c_str(), node.keyword == name)) node.keyword = name;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.c_str());
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText(TextLiteral("比较##runtimeVariableExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "RuntimeNumberVariable") {
                ImGui::SameLine();
                char varBuf[64]; strncpy_s(varBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText("##numberVar", varBuf, 64)) node.keyword = varBuf;
                ImGui::SameLine();
                if (ImGui::BeginCombo("##numberVarPick", TextLiteral("数值变量"))) {
                    auto variables = ConfigManager::GetSingleton()->GetRuntimeNumberVariablesSnapshot();
                    for (const auto& [name, value] : variables) {
                        if (ImGui::Selectable(name.c_str(), node.keyword == name)) node.keyword = name;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.c_str());
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::InputText(TextLiteral("比较##runtimeNumberVariableExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "NodeMonitor") {
                ImGui::SameLine();
                char nodeBuf[96]; strncpy_s(nodeBuf, node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText(TextLiteral("节点名##nodeMonitorCond"), nodeBuf, 96)) node.keyword = nodeBuf;
                ImGui::SameLine();
                static const char* monitorModes[] = {
                    TextLiteral("对象 (可见)"), TextLiteral("节点 (可见)"), TextLiteral("几何体 (可见)"), TextLiteral("节点含几何子项"),
                    TextLiteral("节点含递归几何"), TextLiteral("对象 (包含隐藏)"), TextLiteral("节点 (包含隐藏)"), TextLiteral("递归节点含几何 (包含隐藏)")
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
                if (ImGui::Combo(TextLiteral("类型##nodeMonitorMode"), &modeIndex, monitorModes, IM_ARRAYSIZE(monitorModes))) {
                    node.keyword2 = monitorModeValues[modeIndex];
                }
                ImGui::SameLine();
                if (ImGui::BeginCombo("##nodeMonitorPick", TextLiteral("监控"))) {
                    auto monitorNames = ConfigManager::GetSingleton()->GetNodeMonitorNamesSnapshot();
                    for (const auto& name : monitorNames) {
                        if (!name.empty() && ImGui::Selectable(name.c_str(), node.keyword == name)) {
                            node.keyword = name;
                        }
                    }
                    if (!GetNodeEditorContext().selection.Empty() || !GetSlotEditorContext().selection.Empty()) {
                        ImGui::Separator();
                    }
                    if (!GetNodeEditorContext().selection.Empty()) {
                        const auto label = std::string(TextLiteral("当前 Node: ")) + GetNodeEditorContext().selection.Selected();
                        if (ImGui::Selectable(label.c_str(), GetNodeEditorContext().selection.IsSelected(node.keyword))) {
                            node.keyword = GetNodeEditorContext().selection.Selected();
                        }
                    }
                    if (!GetSlotEditorContext().selection.Empty()) {
                        const auto movName = std::string("IAD_MOV_") + std::string(UIRecordSelectionState::StripManagedPrefix(GetSlotEditorContext().selection.Selected()));
                        const auto label = std::string(TextLiteral("当前 Slot MOV: ")) + movName;
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
                if (ImGui::InputText(TextLiteral("路径包含##skeletonPathContainsCond"), pathBuf, 128)) node.keyword = pathBuf;
            }
            else if (node.type == "PlayerEnemiesNearby") {
                ImGui::SameLine();
                float radius = 4096.0f;
                try { if (!node.keyword.empty()) radius = std::stof(node.keyword); } catch (...) {}
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::InputFloat(TextLiteral("半径##playerEnemiesNearbyRadius"), &radius, 128.0f, 512.0f, "%.0f")) {
                    radius = std::clamp(radius, 128.0f, 65536.0f);
                    node.keyword = std::to_string(radius);
                }
            }
            else if (node.type == "KeyBindState") {
                ImGui::SameLine();
                char keyBuf[64]; strncpy_s(keyBuf, node.keyword.empty() ? "F8" : node.keyword.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::InputText(TextLiteral("按键绑定名称##keyBindStateKey"), keyBuf, 64)) node.keyword = keyBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText(TextLiteral("状态##keyBindStateExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
				ImGui::TextDisabled(TextLiteral("使用 IAD 设置中的命名按键绑定；旧配置的 F1/F8 等直接键名仍兼容。状态 0 为默认，示例：==1。 "));
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
                if (ImGui::InputText(TextLiteral("弧度##sunAngleExpr"), exprBuf, 32)) node.keyword = exprBuf;
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
                if (ImGui::Combo(TextLiteral("阶段##timeOfDayPhaseCond"), &currentPhase, phases, static_cast<int>(std::size(phases)))) {
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
                if (ImGui::Combo(TextLiteral("天气类型##weatherClassCond"), &currentWeatherClass, weatherClasses, static_cast<int>(std::size(weatherClasses)))) {
                    node.keyword = weatherClasses[currentWeatherClass];
                }
            }
            else if (node.type == "InteriorAmbientLightLevel") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? "<0.425" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("ambient##interiorAmbientExpr", exprBuf, 32)) node.keyword = exprBuf;
            }
            else if (node.type == "HasKeywordEquipped" || node.type == "ActorHasKeyword" || node.type == "CandidateHasKeyword") { ImGui::SameLine(); DrawKeywordScannerBoxInternal("##kw", node.keyword); }
            else if (node.type == "HasEquippedBipedSlot") {
                ImGui::SameLine(); int slot = 33; if (!node.keyword.empty()) { try { slot = std::stoi(node.keyword); } catch (...) {} }
                ImGui::SetNextItemWidth(90.0f); if (ImGui::InputInt("##slotInt", &slot, 1, 1)) { if (slot < 30) slot = 30; if (slot > 61) slot = 61; node.keyword = std::to_string(slot); }
            }
            else if (node.type == "HasEquippedFormID") {
                ImGui::SameLine(); if (ImGui::Button(TextLiteral("编辑ID..."))) ImGui::OpenPopup("EditFormID");
                if (ImGui::BeginPopup("EditFormID")) {
                    char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str()); if (ImGui::InputText(TextLiteral("目标 FormID (Hex)"), formBuf, 32)) node.keyword = formBuf;
                    char omodBuf[32]; strcpy_s(omodBuf, node.keyword2.c_str()); if (ImGui::InputText(TextLiteral("所需 OMOD (Hex)"), omodBuf, 32)) node.keyword2 = omodBuf;
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
                if (ImGui::InputText(TextLiteral("目标 FormID##actorFormCond"), formBuf, 32)) node.keyword = formBuf;
            }
            else if (node.type == "GlobalValue") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText("Global FormID##globalFormCond", formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText(TextLiteral("比较##globalExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "ActorLevel") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? ">=1" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText(TextLiteral("等级比较##actorLevelExpr"), exprBuf, 32)) node.keyword = exprBuf;
            }
            else if (node.type == "ActorValue") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText("AVIF FormID##actorValueFormCond", formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText(TextLiteral("比较##actorValueExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
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
                const char* expressionLabel = node.type == "QuestStage" ? TextLiteral("阶段##questStageExpr") : "Rank##actorPerkRankExpr";
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
                if (ImGui::Combo(TextLiteral("星期##dayOfWeekCond"), &currentDay, days, static_cast<int>(std::size(days)))) {
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
                if (ImGui::Combo(TextLiteral("生命状态##actorLifeStateCond"), &currentState, states, static_cast<int>(std::size(states)))) {
                    node.keyword = states[currentState];
                }
            }
            else if (node.type == "RandomPercent") {
                ImGui::SameLine();
                float percent = 100.0f;
                try { if (!node.keyword.empty()) percent = std::stof(node.keyword); } catch (...) {}
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputFloat(TextLiteral("概率%##randomPercent"), &percent, 1.0f, 10.0f, "%.1f")) {
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
                if (ImGui::InputText(TextLiteral("候选 FormID##candidateForm"), formBuf, 32)) node.keyword = formBuf;
            }
            else if (node.type == "CandidateHasOMOD") {
                ImGui::SameLine();
                char omodBuf[32]; strcpy_s(omodBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::InputText(TextLiteral("候选 OMOD##candidateOmod"), omodBuf, 32)) node.keyword = omodBuf;
            }
            else if (node.type == "InventoryItemCount") {
                ImGui::SameLine();
                char formBuf[32]; strcpy_s(formBuf, node.keyword.c_str());
                ImGui::SetNextItemWidth(115.0f);
                if (ImGui::InputText(TextLiteral("物品 FormID##inventoryItemCountForm"), formBuf, 32)) node.keyword = formBuf;
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword2.empty() ? ">0" : node.keyword2.c_str());
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::InputText(TextLiteral("数量##inventoryItemCountExpr"), exprBuf, 32)) node.keyword2 = exprBuf;
            }
            else if (node.type == "CandidateCount" || node.type == "CandidateInventoryCount") {
                ImGui::SameLine();
                char exprBuf[32]; strcpy_s(exprBuf, node.keyword.empty() ? ">0" : node.keyword.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText(node.type == "CandidateCount" ? TextLiteral("堆叠数量##candidateCountExpr") : TextLiteral("背包总数##candidateInventoryCountExpr"), exprBuf, 32)) node.keyword = exprBuf;
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
    }

    bool UIConditionTreeEditor::Draw(IAD::ConditionNode& a_node, bool a_isRoot, int a_depth)
    {
        return DrawConditionNodeRecursive(a_node, a_isRoot, a_depth);
    }

    void UIConditionTreeEditor::DrawKeywordScannerBox(const char* a_id, std::string& a_keyword)
    {
        DrawKeywordScannerBoxInternal(a_id, a_keyword);
    }
}
