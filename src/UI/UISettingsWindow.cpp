#include "pch.h"
#include "UISettingsWindow.h"
#include "UIConfigWindows.h"
#include "Data/ConfigManager.h"
#include "Profile/GlobalProfileManager.h"
#include "Engine/NodeManager.h"
#include "System/HolsterManager.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <RE/E/ENUM_FORM_ID.h>
#include <RE/T/TESForm.h>
#include <algorithm>

namespace IAD::UI {
    // 把原本的 ImGuiManager::DrawSettingsWindow 改成 UISettingsWindow::Draw
    void UISettingsWindow::Draw() {
        auto* config = ConfigManager::GetSingleton();
        
        if (!config->uiShowSettings) return;

        ImGui::SetNextWindowSize({ 620.0f, 680.0f }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IAD 设置", &config->uiShowSettings)) {
            
            if (ImGui::CollapsingHeader("通用", ImGuiTreeNodeFlags_DefaultOpen)) {
                static constexpr const char* logLevels[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("日志等级", &config->logLevel, logLevels, static_cast<int>(std::size(logLevels)))) {
                    spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(config->logLevel));
                    config->SaveINISettings();
                }

                if (ImGui::Checkbox("仅显示已装备或收藏的武器 (全局 Equipment Mode)", &config->displayFavoritesOnly)) {
                    config->SaveINISettings();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::TextDisabled("💡 勾选后，只有被你在快捷菜单中标记星标的武器才会显示。");
            }

            ImGui::Spacing();

			if (ImGui::CollapsingHeader("显示", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (ImGui::Checkbox("启用 NPC 展示", &config->enableNPCDisplays)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->SetNPCDisplaysEnabled(config->enableNPCDisplays);
				}
				ImGui::TextDisabled("关闭后会清理 NPC 身上的 IAD 模型；玩家展示不受影响。");

				bool blockPlayerDisplays = config->IsPlayerDisplaysBlocked();
				if (ImGui::Checkbox("屏蔽玩家展示 (IED Player Toggle)", &blockPlayerDisplays)) {
					config->SetPlayerDisplaysBlocked(blockPlayerDisplays);
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				ImGui::TextDisabled("仅隐藏玩家的 IAD 模型；节点与引擎生命周期仍保持有效。");

				struct PlayerBlockKey { const char* name; std::uint32_t vk; };
				static constexpr PlayerBlockKey playerBlockKeys[] = {
					{ "禁用", 0 }, { "F1", VK_F1 }, { "F2", VK_F2 }, { "F3", VK_F3 },
					{ "F4", VK_F4 }, { "F5", VK_F5 }, { "F6", VK_F6 }, { "F7", VK_F7 },
					{ "F8", VK_F8 }, { "F9", VK_F9 }, { "F10", VK_F10 }, { "F11", VK_F11 }, { "F12", VK_F12 }
				};
				int playerBlockKeyIndex = 0;
				for (int i = 0; i < static_cast<int>(std::size(playerBlockKeys)); ++i) {
					if (playerBlockKeys[i].vk == config->playerBlockHotkey) {
						playerBlockKeyIndex = i;
						break;
					}
				}
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::Combo("玩家展示切换键", &playerBlockKeyIndex, [](void* data, int index) -> const char* {
					const auto* keys = static_cast<const PlayerBlockKey*>(data);
					return keys[index].name;
				}, const_cast<PlayerBlockKey*>(playerBlockKeys), static_cast<int>(std::size(playerBlockKeys)))) {
					config->playerBlockHotkey = playerBlockKeys[playerBlockKeyIndex].vk;
					config->SaveINISettings();
				}
				bool playerBlockCtrl = (config->playerBlockModifier & 1) != 0;
				bool playerBlockShift = (config->playerBlockModifier & 2) != 0;
				bool playerBlockAlt = (config->playerBlockModifier & 4) != 0;
				ImGui::SameLine();
				if (ImGui::Checkbox("Ctrl##playerBlock", &playerBlockCtrl)) {
					if (playerBlockCtrl) config->playerBlockModifier |= 1; else config->playerBlockModifier &= ~1;
					config->SaveINISettings();
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Shift##playerBlock", &playerBlockShift)) {
					if (playerBlockShift) config->playerBlockModifier |= 2; else config->playerBlockModifier &= ~2;
					config->SaveINISettings();
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Alt##playerBlock", &playerBlockAlt)) {
					if (playerBlockAlt) config->playerBlockModifier |= 4; else config->playerBlockModifier &= ~4;
					config->SaveINISettings();
				}
				ImGui::TextDisabled("按键设为“禁用”时仅保留界面开关；快捷键按下后切换玩家展示状态。");

				static std::uint32_t blockedActorFormID = 0;
				ImGui::SetNextItemWidth(150.0f);
				ImGui::InputScalar("屏蔽角色 FormID", ImGuiDataType_U32, &blockedActorFormID, nullptr, nullptr, "%08X");
				ImGui::SameLine();
				if (ImGui::Button("添加屏蔽角色") && config->AddBlockedActorFormID(blockedActorFormID)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				std::uint32_t removeBlockedActorFormID = 0;
				for (const auto formID : config->GetBlockedActorFormIDsSnapshot()) {
					ImGui::PushID(static_cast<int>(formID));
					ImGui::Text("%08X", formID);
					ImGui::SameLine();
					if (ImGui::SmallButton("移除")) removeBlockedActorFormID = formID;
					ImGui::PopID();
				}
				if (removeBlockedActorFormID != 0 && config->RemoveBlockedActorFormID(removeBlockedActorFormID)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				ImGui::TextDisabled("指定角色同样只隐藏展示模型；适合单独排除兼容性有问题的 NPC。");

				int npcEvaluationInterval = static_cast<int>(config->npcEvaluationIntervalTicks);
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderInt("NPC 评估间隔 (tick)", &npcEvaluationInterval, 1, 60)) {
					config->npcEvaluationIntervalTicks = static_cast<std::uint32_t>(npcEvaluationInterval);
					config->SaveConfig();
				}
				ImGui::TextDisabled("默认 4。值越高，NPC 背包和条件扫描越分散；玩家刷新与模型物理不受影响。");

				if (ImGui::Checkbox("普通候选优先当前装备 (Equipped Candidate Priority)", &config->prioritizeEquippedCandidates)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				ImGui::TextDisabled("启用后，普通插槽候选排序会把当前装备物品排在收藏/最近获得之前。");

				if (ImGui::Checkbox("自定义规则的最近展示插槽记忆 (IAD Extension)", &config->useRecentDisplaySlotMemory)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				ImGui::TextDisabled("仅供 Last-Equipped Custom 的展示槽回退使用；普通插槽始终遵循 IED 顺序。");

				if (ImGui::Checkbox("正装备物品保留高优先级插槽 (Equipped Reservation)", &config->reserveEquippedForPositivePrioritySlots)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
				ImGui::TextDisabled("启用后，priority > 0 的插槽会先尝试接收当前装备物品；priority = 0 不受此规则影响。");

				if (ImGui::Checkbox("最近获得物品优先 (IED Acquired Priority)", &config->prioritizeRecentAcquired)) {
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}
                ImGui::TextDisabled("启用后，普通候选会按最近拾取顺序优先展示；已装备和收藏仍然更优先。");

                auto drawTypeToggle = [&](const char* label, RE::ENUM_FORM_ID formType) {
                    auto type = static_cast<std::uint8_t>(formType);
                    bool enabled = std::find(config->recentAcquiredFormTypes.begin(), config->recentAcquiredFormTypes.end(), type) != config->recentAcquiredFormTypes.end();
                    if (ImGui::Checkbox(label, &enabled)) {
                        config->SetRecentAcquiredFormTypeEnabled(type, enabled);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    };

                if (!config->prioritizeRecentAcquired) ImGui::BeginDisabled();
                ImGui::Indent();
                drawTypeToggle("武器 (WEAP)", RE::ENUM_FORM_ID::kWEAP); ImGui::SameLine();
                drawTypeToggle("护甲 (ARMO)", RE::ENUM_FORM_ID::kARMO); ImGui::SameLine();
                drawTypeToggle("弹药 (AMMO)", RE::ENUM_FORM_ID::kAMMO);
                drawTypeToggle("药品/食物 (ALCH)", RE::ENUM_FORM_ID::kALCH); ImGui::SameLine();
                drawTypeToggle("杂项 (MISC)", RE::ENUM_FORM_ID::kMISC);
                ImGui::Unindent();
                if (!config->prioritizeRecentAcquired) ImGui::EndDisabled();
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("装备定位", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("仅评估节点监控列表", &config->nodeMonitorUseFilter)) {
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::TextDisabled("启用后，节点监控和调试只处理下方列表内的节点名称。");

                static char newNodeMonitor[128] = "";
                ImGui::SetNextItemWidth(260.0f);
                ImGui::InputText("节点名称", newNodeMonitor, sizeof(newNodeMonitor));
                ImGui::SameLine();
                if (ImGui::Button("添加节点") && newNodeMonitor[0] != '\0') {
                    const std::string name = newNodeMonitor;
                    if (std::find(config->nodeMonitorNames.begin(), config->nodeMonitorNames.end(), name) == config->nodeMonitorNames.end()) {
                        config->nodeMonitorNames.push_back(name);
                        config->SaveConfig();
                    }
                    newNodeMonitor[0] = '\0';
                }

                std::string removeNodeMonitor;
                for (const auto& name : config->nodeMonitorNames) {
                    ImGui::PushID(name.c_str());
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("删除")) removeNodeMonitor = name;
                    ImGui::PopID();
                }
                if (!removeNodeMonitor.empty()) {
                    std::erase(config->nodeMonitorNames, removeNodeMonitor);
                    config->SaveConfig();
                }
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("效果与物理", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool refresh = false;
                refresh |= ImGui::Checkbox("启用装备物理", &config->enableEquipmentPhysics);
                ImGui::TextDisabled("关闭后，所有插槽和节点保留基础变换，不再模拟摆动。");
                refresh |= ImGui::Checkbox("启用模型效果", &config->enableModelEffects);
                ImGui::TextDisabled("关闭后，所有插槽、专属展示和模型组的效果着色器均不会应用。");
                refresh |= ImGui::Checkbox("启用模型灯光", &config->enableModelLights);
                ImGui::TextDisabled("关闭后，所有插槽、专属展示和模型组均不会添加或保留模型灯光。");
                if (refresh) {
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("条件变量", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "布尔变量 (Boolean Variables)");
                static char newVarName[64] = "MyVariable";
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##newVariableName", newVarName, 64);
                ImGui::SameLine();
                if (ImGui::Button("添加变量", { 100.0f, 0.0f })) {
                    if (newVarName[0] != '\0') {
                        config->SetRuntimeVariable(newVarName, false);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }

                std::string removeVariable;
                auto variables = config->GetRuntimeVariablesSnapshot();
                for (const auto& [name, value] : variables) {
                    ImGui::PushID(name.c_str());
                    bool enabled = value;
                    if (ImGui::Checkbox(name.c_str(), &enabled)) {
                        config->SetRuntimeVariable(name, enabled);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
                    if (ImGui::Button("删除")) {
                        removeVariable = name;
                    }
                    ImGui::PopID();
                }

                if (!removeVariable.empty()) {
                    config->RemoveRuntimeVariable(removeVariable);
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::TextDisabled("条件树中选择 RuntimeVariable，并填入变量名即可引用。");

                ImGui::Separator();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "数值变量 (Number Variables)");
                static char newNumberVarName[64] = "MyNumberVariable";
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##newNumberVariableName", newNumberVarName, 64);
                ImGui::SameLine();
                if (ImGui::Button("添加数值变量", { 120.0f, 0.0f })) {
                    if (newNumberVarName[0] != '\0') {
                        config->SetRuntimeNumberVariable(newNumberVarName, 0.0f);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }

                std::string removeNumberVariable;
                auto numberVariables = config->GetRuntimeNumberVariablesSnapshot();
                for (const auto& [name, value] : numberVariables) {
                    ImGui::PushID(name.c_str());
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::SameLine(170.0f);
                    float numberValue = value;
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::InputFloat("##numberValue", &numberValue, 0.0f, 0.0f, "%.3f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        config->SetRuntimeNumberVariable(name, numberValue);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
                    if (ImGui::Button("删除##number")) {
                        removeNumberVariable = name;
                    }
                    ImGui::PopID();
                }
                if (!removeNumberVariable.empty()) {
                    config->RemoveRuntimeNumberVariable(removeNumberVariable);
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::TextDisabled("条件树中选择 RuntimeNumberVariable，并使用 >=1、<5 等比较表达式。");

                ImGui::Separator();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "模型路径变量 (Model Path Variables)");
                static char newPathVarName[64] = "ModelPathVariable";
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##newPathVariableName", newPathVarName, 64);
                ImGui::SameLine();
                if (ImGui::Button("添加路径变量", { 120.0f, 0.0f })) {
                    if (newPathVarName[0] != '\0') {
                        config->SetRuntimeModelPathVariable(newPathVarName, "");
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }

                std::string removePathVariable;
                auto pathVariables = config->GetRuntimeModelPathVariablesSnapshot();
                for (const auto& [name, value] : pathVariables) {
                    ImGui::PushID(name.c_str());
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::SameLine(170.0f);
                    char pathBuf[260];
                    strncpy_s(pathBuf, value.c_str(), _TRUNCATE);
                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 260.0f);
                    ImGui::InputText("##modelPathValue", pathBuf, sizeof(pathBuf));
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        config->SetRuntimeModelPathVariable(name, pathBuf);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("删除##path")) {
                        removePathVariable = name;
                    }
                    ImGui::PopID();
                }
                if (!removePathVariable.empty()) {
                    config->RemoveRuntimeModelPathVariable(removePathVariable);
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }

                ImGui::Separator();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "模型 FormID 变量 (Model Form Variables)");
                static char newFormVarName[64] = "ModelFormVariable";
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##newFormVariableName", newFormVarName, 64);
                ImGui::SameLine();
                if (ImGui::Button("添加 Form 变量", { 120.0f, 0.0f })) {
                    if (newFormVarName[0] != '\0') {
                        config->SetRuntimeFormVariable(newFormVarName, 0);
                        config->SaveConfig();
                        HolsterManager::GetSingleton()->ForceRefreshAll();
                    }
                }

                std::string removeFormVariable;
                auto formVariables = config->GetRuntimeFormVariablesSnapshot();
                for (const auto& [name, value] : formVariables) {
                    ImGui::PushID(name.c_str());
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::SameLine(170.0f);
                    char formBuf[16];
                    sprintf_s(formBuf, "%08X", value);
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::InputText("##modelFormValue", formBuf, sizeof(formBuf), ImGuiInputTextFlags_CharsHexadecimal)) {
                        try {
                            config->SetRuntimeFormVariable(name, static_cast<std::uint32_t>(std::stoul(formBuf, nullptr, 16)));
                            config->SaveConfig();
                            HolsterManager::GetSingleton()->ForceRefreshAll();
                        }
                        catch (...) {
                        }
                    }
                    ImGui::SameLine();
                    if (auto form = RE::TESForm::GetFormByID(value)) {
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
                    ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
                    if (ImGui::Button("删除##form")) {
                        removeFormVariable = name;
                    }
                    ImGui::PopID();
                }
                if (!removeFormVariable.empty()) {
                    config->RemoveRuntimeFormVariable(removeFormVariable);
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }

                ImGui::TextDisabled("模型替换区域可勾选变量源，并选择这里的路径变量或 FormID 变量。");

                ImGui::Separator();
				ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "命名按键绑定 (IED Keybinds)");
				ImGui::TextDisabled("状态从 0 开始；每次按下主键会循环到 1..状态数。条件使用 KeyBindState，名称填按键绑定名称，状态填 ==1、>0 等。");
				ImGui::TextDisabled("主键/组合键为 Windows 虚拟键码：F1=112，F10=121，Ctrl=17，Shift=16，Alt=18。组合键为 0 时不要求修饰键。");

				static char newKeybindName[64] = "IAD_Toggle";
				static std::uint32_t newKeybindKey = VK_F10;
				static std::uint32_t newKeybindCombo = 0;
				static int newKeybindStateCount = 1;
				ImGui::SetNextItemWidth(180.0f);
				ImGui::InputText("名称##newKeybind", newKeybindName, sizeof(newKeybindName));
				ImGui::SameLine();
				ImGui::SetNextItemWidth(85.0f);
				ImGui::InputScalar("主键##newKeybind", ImGuiDataType_U32, &newKeybindKey);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(85.0f);
				ImGui::InputScalar("组合键##newKeybind", ImGuiDataType_U32, &newKeybindCombo);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(85.0f);
				ImGui::InputInt("状态数##newKeybind", &newKeybindStateCount);
				ImGui::SameLine();
				if (ImGui::Button("添加按键绑定")) {
					auto bindings = config->GetKeyBindDefinitionsSnapshot();
					const std::string name = newKeybindName;
					if (!name.empty() && !bindings.contains(name) && newKeybindKey > 0 && newKeybindKey <= 0xFF) {
						bindings.emplace(name, KeyBindDefinition{
							newKeybindKey,
							newKeybindCombo <= 0xFF ? newKeybindCombo : 0,
							static_cast<std::uint32_t>(std::clamp(newKeybindStateCount, 1, 32))
						});
						config->SetKeyBindDefinitions(std::move(bindings));
						config->SaveConfig();
					}
				}

				auto keybindDefinitions = config->GetKeyBindDefinitionsSnapshot();
				bool keybindDefinitionsChanged = false;
				std::string removeKeybind;
				for (auto& [name, definition] : keybindDefinitions) {
					ImGui::PushID(name.c_str());
					ImGui::TextUnformatted(name.c_str());
					ImGui::SameLine(180.0f);
					ImGui::SetNextItemWidth(85.0f);
					keybindDefinitionsChanged |= ImGui::InputScalar("主键", ImGuiDataType_U32, &definition.key);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(85.0f);
					keybindDefinitionsChanged |= ImGui::InputScalar("组合键", ImGuiDataType_U32, &definition.comboKey);
					ImGui::SameLine();
					int stateCount = static_cast<int>(definition.numStates);
					ImGui::SetNextItemWidth(85.0f);
					if (ImGui::InputInt("状态数", &stateCount)) {
						definition.numStates = static_cast<std::uint32_t>(std::clamp(stateCount, 1, 32));
						keybindDefinitionsChanged = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("删除##keybind")) removeKeybind = name;
					ImGui::PopID();
				}
				if (!removeKeybind.empty()) {
					keybindDefinitions.erase(removeKeybind);
					keybindDefinitionsChanged = true;
				}
				if (keybindDefinitionsChanged) {
					config->SetKeyBindDefinitions(std::move(keybindDefinitions));
					config->SaveConfig();
					HolsterManager::GetSingleton()->ForceRefreshAll();
				}

				ImGui::Separator();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "自动条件变量 (Conditional Variables)");
                ImGui::TextDisabled("按顺序评估规则。命中规则后默认停止，勾选继续评估可让后续规则覆盖结果。");
                ImGui::TextDisabled("变量顺序决定相互引用时的更新顺序；规则顺序决定匹配优先级。");

                auto conditionalVariables = config->GetConditionalVariablesSnapshot();
                auto saveConditionalVariables = [&]() {
                    config->SetConditionalVariables(conditionalVariables);
                    config->SaveConfig();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                };

                auto& conditionalVariableProfiles = IAD::Profile::GlobalProfileManager::GetSingleton().ConditionalVariables();
                if (!conditionalVariableProfiles.IsInitialized()) {
                    conditionalVariableProfiles.Load();
                }

                static std::string selectedConditionalVariableProfile;
                static char conditionalVariableProfileName[64] = "ConditionalVariables";
                static std::string conditionalVariableProfileStatus;

                ImGui::Separator();
                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "条件变量预设");
                ImGui::TextDisabled("预设文件保存在 Profiles/ConditionalVariables；应用会替换全部变量，合并按变量名更新或追加。");

                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo("预设##ConditionalVariables", selectedConditionalVariableProfile.empty() ? "选择预设..." : selectedConditionalVariableProfile.c_str(), ImGuiComboFlags_HeightLarge)) {
                    for (const auto& [name, record] : conditionalVariableProfiles.Data()) {
                        const bool selected = selectedConditionalVariableProfile == name;
                        const std::string label = record.modified ? ("* " + name) : name;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            selectedConditionalVariableProfile = name;
                            strcpy_s(conditionalVariableProfileName, sizeof(conditionalVariableProfileName), name.c_str());
                            conditionalVariableProfileStatus.clear();
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("刷新预设列表##ConditionalVariables")) {
                    conditionalVariableProfiles.Load();
                    selectedConditionalVariableProfile.clear();
                    conditionalVariableProfileStatus = "已重新读取条件变量预设目录。";
                }

                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("名称##ConditionalVariableProfileName", conditionalVariableProfileName, sizeof(conditionalVariableProfileName));

                const bool hasConditionalVariableProfileName = std::strlen(conditionalVariableProfileName) > 0;
                auto* selectedConditionalVariableProfileRecord = conditionalVariableProfiles.Find(selectedConditionalVariableProfile);

                if (!hasConditionalVariableProfileName) ImGui::BeginDisabled();
                if (ImGui::Button("新建为当前配置##ConditionalVariables", { 130.0f, 0.0f })) {
                    if (conditionalVariableProfiles.CreateProfile(conditionalVariableProfileName, conditionalVariables)) {
                        selectedConditionalVariableProfile = conditionalVariableProfileName;
                        conditionalVariableProfileStatus = "已从当前条件变量创建预设。";
                    }
                    else {
                        conditionalVariableProfileStatus = conditionalVariableProfiles.LastError();
                    }
                }
                if (!hasConditionalVariableProfileName) ImGui::EndDisabled();

                ImGui::SameLine();
                if (!selectedConditionalVariableProfileRecord) ImGui::BeginDisabled();
                if (ImGui::Button("保存到预设##ConditionalVariables", { 110.0f, 0.0f })) {
                    selectedConditionalVariableProfileRecord->data = conditionalVariables;
                    selectedConditionalVariableProfileRecord->MarkModified();
                    conditionalVariableProfileStatus = conditionalVariableProfiles.SaveProfile(selectedConditionalVariableProfile) ?
                        "已保存当前条件变量到预设。" : conditionalVariableProfiles.LastError();
                }
                if (!selectedConditionalVariableProfileRecord) ImGui::EndDisabled();

                ImGui::SameLine();
                const bool applyDisabled = !selectedConditionalVariableProfileRecord || selectedConditionalVariableProfileRecord->IsMergeOnly();
                if (applyDisabled) ImGui::BeginDisabled();
                if (ImGui::Button("应用预设##ConditionalVariables", { 95.0f, 0.0f })) {
                    conditionalVariables = selectedConditionalVariableProfileRecord->data;
                    saveConditionalVariables();
                    conditionalVariableProfileStatus = "已应用条件变量预设。";
                }
                if (applyDisabled) ImGui::EndDisabled();

                ImGui::SameLine();
                if (!selectedConditionalVariableProfileRecord) ImGui::BeginDisabled();
                if (ImGui::Button("合并预设##ConditionalVariables", { 95.0f, 0.0f })) {
                    for (const auto& importedVariable : selectedConditionalVariableProfileRecord->data) {
                        auto existing = std::find_if(conditionalVariables.begin(), conditionalVariables.end(), [&](const ConditionalVariableDefinition& variable) {
                            return variable.name == importedVariable.name;
                        });
                        if (existing != conditionalVariables.end()) {
                            *existing = importedVariable;
                        }
                        else {
                            conditionalVariables.push_back(importedVariable);
                        }
                    }
                    saveConditionalVariables();
                    conditionalVariableProfileStatus = "已合并条件变量预设。";
                }
                if (!selectedConditionalVariableProfileRecord) ImGui::EndDisabled();

                if (selectedConditionalVariableProfileRecord) {
                    if (selectedConditionalVariableProfileRecord->IsMergeOnly()) {
                        ImGui::TextDisabled("此预设标记为仅合并，不能使用“应用预设”。");
                    }
                    if (selectedConditionalVariableProfileRecord->description && !selectedConditionalVariableProfileRecord->description->empty()) {
                        ImGui::TextWrapped("%s", selectedConditionalVariableProfileRecord->description->c_str());
                    }
                }
                if (!conditionalVariableProfileStatus.empty()) {
                    ImGui::TextDisabled("%s", conditionalVariableProfileStatus.c_str());
                }

                ImGui::Separator();

                static char newConditionalVariableName[64] = "MyAutoVariable";
                static int newConditionalVariableType = 0;
                static constexpr const char* conditionalVariableTypeNames[] = {
                    "布尔 (Boolean)", "数值 (Number)", "FormID (Form)", "模型路径 (Model Path)"
                };
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputText("##newConditionalVariableName", newConditionalVariableName, sizeof(newConditionalVariableName));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(130.0f);
                ImGui::Combo("##newConditionalVariableType", &newConditionalVariableType, conditionalVariableTypeNames, IM_ARRAYSIZE(conditionalVariableTypeNames));
                ImGui::SameLine();
                if (ImGui::Button("添加自动变量")) {
                    const std::string name = newConditionalVariableName;
                    const bool duplicate = std::any_of(conditionalVariables.begin(), conditionalVariables.end(), [&](const auto& variable) {
                        return variable.name == name;
                    });
                    if (!name.empty() && !duplicate) {
                        ConditionalVariableDefinition variable;
                        variable.name = name;
                        variable.description = name;
                        variable.type = static_cast<ConditionalVariableType>(newConditionalVariableType);
                        conditionalVariables.push_back(std::move(variable));
                        saveConditionalVariables();
                    }
                }

                std::size_t removeConditionalVariable = conditionalVariables.size();
                std::size_t moveConditionalVariable = conditionalVariables.size();
                int moveConditionalVariableDelta = 0;
                for (std::size_t variableIndex = 0; variableIndex < conditionalVariables.size(); ++variableIndex) {
                    auto& variable = conditionalVariables[variableIndex];
                    ImGui::PushID(static_cast<int>(variableIndex));
                    const std::string label = variable.name.empty() ? "未命名自动变量" : variable.name;
                    bool changed = false;
                    if (ImGui::TreeNode(label.c_str())) {
                        changed |= ImGui::Checkbox("启用", &variable.enabled);

                        char descriptionBuffer[256];
                        strncpy_s(descriptionBuffer, variable.description.c_str(), _TRUNCATE);
                        ImGui::SetNextItemWidth(330.0f);
                        if (ImGui::InputText("说明", descriptionBuffer, sizeof(descriptionBuffer))) {
                            variable.description = descriptionBuffer;
                            changed = true;
                        }

                        int typeIndex = static_cast<int>(variable.type);
                        ImGui::SetNextItemWidth(180.0f);
                        if (ImGui::Combo("类型", &typeIndex, conditionalVariableTypeNames, IM_ARRAYSIZE(conditionalVariableTypeNames))) {
                            variable.type = static_cast<ConditionalVariableType>(typeIndex);
                            changed = true;
                        }

                        switch (variable.type) {
                        case ConditionalVariableType::kBoolean:
                            changed |= ImGui::Checkbox("默认值", &variable.defaultBooleanValue);
                            break;
                        case ConditionalVariableType::kNumber:
                            ImGui::SetNextItemWidth(160.0f);
                            changed |= ImGui::InputFloat("默认值", &variable.defaultNumberValue, 0.0f, 0.0f, "%.3f");
                            break;
                        case ConditionalVariableType::kForm: {
							static constexpr const char* formSourceNames[] = { "固定 FormID", "当前装备武器" };
							int sourceIndex = static_cast<int>(variable.defaultFormSource);
							ImGui::SetNextItemWidth(180.0f);
							if (ImGui::Combo("默认 Form 来源", &sourceIndex, formSourceNames, IM_ARRAYSIZE(formSourceNames))) {
								variable.defaultFormSource = static_cast<ConditionalVariableFormSource>(sourceIndex);
								changed = true;
							}
							if (variable.defaultFormSource == ConditionalVariableFormSource::kEquippedWeapon) {
								ImGui::TextDisabled("每次轮询读取当前装备的 WEAP；未装备武器时输出 00000000。");
								break;
							}
                            char formBuffer[16];
                            sprintf_s(formBuffer, "%08X", variable.defaultFormIDValue);
                            ImGui::SetNextItemWidth(150.0f);
                            if (ImGui::InputText("默认 FormID", formBuffer, sizeof(formBuffer), ImGuiInputTextFlags_CharsHexadecimal)) {
                                try {
                                    variable.defaultFormIDValue = static_cast<std::uint32_t>(std::stoul(formBuffer, nullptr, 16));
                                    changed = true;
                                } catch (...) {}
                            }
                            break;
                        }
                        case ConditionalVariableType::kModelPath: {
                            char pathBuffer[260];
                            strncpy_s(pathBuffer, variable.defaultModelPathValue.c_str(), _TRUNCATE);
                            ImGui::SetNextItemWidth(400.0f);
                            if (ImGui::InputText("默认模型路径", pathBuffer, sizeof(pathBuffer))) {
                                variable.defaultModelPathValue = pathBuffer;
                                changed = true;
                            }
                            break;
                        }
                        }

                        ImGui::Separator();
                        ImGui::Text("规则输出");
                        if (ImGui::Button("添加规则")) {
                            ConditionalVariableRule rule;
                            rule.conditionTree = ConditionNode(true, true);
                            variable.rules.push_back(std::move(rule));
                            changed = true;
                        }

                        std::size_t moveRule = variable.rules.size();
                        int moveRuleDelta = 0;
                        std::size_t removeRule = variable.rules.size();
                        for (std::size_t ruleIndex = 0; ruleIndex < variable.rules.size(); ++ruleIndex) {
                            auto& rule = variable.rules[ruleIndex];
                            ImGui::PushID(static_cast<int>(ruleIndex));
                            const std::string ruleLabel = "规则 " + std::to_string(ruleIndex + 1);
                            if (ImGui::TreeNode(ruleLabel.c_str())) {
                                changed |= DrawConditionTreeEditor(rule.conditionTree, false);

                                switch (variable.type) {
                                case ConditionalVariableType::kBoolean:
                                    changed |= ImGui::Checkbox("命中值", &rule.booleanValue);
                                    break;
                                case ConditionalVariableType::kNumber:
                                    ImGui::SetNextItemWidth(160.0f);
                                    changed |= ImGui::InputFloat("命中数值", &rule.numberValue, 0.0f, 0.0f, "%.3f");
                                    break;
                                case ConditionalVariableType::kForm: {
									static constexpr const char* formSourceNames[] = { "固定 FormID", "当前装备武器" };
									int sourceIndex = static_cast<int>(rule.formSource);
									ImGui::SetNextItemWidth(180.0f);
									if (ImGui::Combo("命中 Form 来源", &sourceIndex, formSourceNames, IM_ARRAYSIZE(formSourceNames))) {
										rule.formSource = static_cast<ConditionalVariableFormSource>(sourceIndex);
										changed = true;
									}
									if (rule.formSource == ConditionalVariableFormSource::kEquippedWeapon) {
										ImGui::TextDisabled("命中时使用当前装备的 WEAP；未装备武器时输出 00000000。");
										break;
									}
                                    char formBuffer[16];
                                    sprintf_s(formBuffer, "%08X", rule.formIDValue);
                                    ImGui::SetNextItemWidth(150.0f);
                                    if (ImGui::InputText("命中 FormID", formBuffer, sizeof(formBuffer), ImGuiInputTextFlags_CharsHexadecimal)) {
                                        try {
                                            rule.formIDValue = static_cast<std::uint32_t>(std::stoul(formBuffer, nullptr, 16));
                                            changed = true;
                                        } catch (...) {}
                                    }
                                    break;
                                }
                                case ConditionalVariableType::kModelPath: {
                                    char pathBuffer[260];
                                    strncpy_s(pathBuffer, rule.modelPathValue.c_str(), _TRUNCATE);
                                    ImGui::SetNextItemWidth(400.0f);
                                    if (ImGui::InputText("命中模型路径", pathBuffer, sizeof(pathBuffer))) {
                                        rule.modelPathValue = pathBuffer;
                                        changed = true;
                                    }
                                    break;
                                }
                                }

                                changed |= ImGui::Checkbox("命中后继续评估后续规则", &rule.continueAfterMatch);
                                if (ruleIndex == 0) ImGui::BeginDisabled();
                                if (ImGui::Button("上移规则")) {
                                    moveRule = ruleIndex;
                                    moveRuleDelta = -1;
                                }
                                if (ruleIndex == 0) ImGui::EndDisabled();
                                ImGui::SameLine();
                                if (ruleIndex + 1 >= variable.rules.size()) ImGui::BeginDisabled();
                                if (ImGui::Button("下移规则")) {
                                    moveRule = ruleIndex;
                                    moveRuleDelta = 1;
                                }
                                if (ruleIndex + 1 >= variable.rules.size()) ImGui::EndDisabled();
                                ImGui::SameLine();
                                if (ImGui::Button("删除此规则")) removeRule = ruleIndex;
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        if (moveRule < variable.rules.size() && moveRuleDelta != 0) {
                            const auto targetRule = static_cast<std::ptrdiff_t>(moveRule) + moveRuleDelta;
                            if (targetRule >= 0 && targetRule < static_cast<std::ptrdiff_t>(variable.rules.size())) {
                                std::swap(variable.rules[moveRule], variable.rules[static_cast<std::size_t>(targetRule)]);
                                changed = true;
                            }
                        }
                        if (removeRule < variable.rules.size()) {
                            variable.rules.erase(variable.rules.begin() + static_cast<std::ptrdiff_t>(removeRule));
                            changed = true;
                        }

                        if (variableIndex == 0) ImGui::BeginDisabled();
                        if (ImGui::Button("上移自动变量")) {
                            moveConditionalVariable = variableIndex;
                            moveConditionalVariableDelta = -1;
                        }
                        if (variableIndex == 0) ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (variableIndex + 1 >= conditionalVariables.size()) ImGui::BeginDisabled();
                        if (ImGui::Button("下移自动变量")) {
                            moveConditionalVariable = variableIndex;
                            moveConditionalVariableDelta = 1;
                        }
                        if (variableIndex + 1 >= conditionalVariables.size()) ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("删除此自动变量")) removeConditionalVariable = variableIndex;
                        ImGui::TreePop();
                    }
                    ImGui::PopID();

                    if (changed) saveConditionalVariables();
                }
                if (moveConditionalVariable < conditionalVariables.size() && moveConditionalVariableDelta != 0) {
                    const auto targetVariable = static_cast<std::ptrdiff_t>(moveConditionalVariable) + moveConditionalVariableDelta;
                    if (targetVariable >= 0 && targetVariable < static_cast<std::ptrdiff_t>(conditionalVariables.size())) {
                        std::swap(conditionalVariables[moveConditionalVariable], conditionalVariables[static_cast<std::size_t>(targetVariable)]);
                        saveConditionalVariables();
                    }
                }
                if (removeConditionalVariable < conditionalVariables.size()) {
                    conditionalVariables.erase(conditionalVariables.begin() + static_cast<std::ptrdiff_t>(removeConditionalVariable));
                    saveConditionalVariables();
                }
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("用户界面", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("你可以自由组合修饰键和主按键来唤出菜单。");
                ImGui::Spacing();

                bool modCtrl = (config->editorModifier & 1) != 0;
                bool modShift = (config->editorModifier & 2) != 0;
                bool modAlt = (config->editorModifier & 4) != 0;

                ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "修饰键 (Modifiers):");
                if (ImGui::Checkbox("Ctrl", &modCtrl)) {
                    if (modCtrl) config->editorModifier |= 1; else config->editorModifier &= ~1;
                    config->SaveINISettings();
                }
                ImGui::SameLine(100.0f);
                if (ImGui::Checkbox("Shift", &modShift)) {
                    if (modShift) config->editorModifier |= 2; else config->editorModifier &= ~2;
                    config->SaveINISettings();
                }
                ImGui::SameLine(200.0f);
                if (ImGui::Checkbox("Alt", &modAlt)) {
                    if (modAlt) config->editorModifier |= 4; else config->editorModifier &= ~4;
                    config->SaveINISettings();
                }

                ImGui::Spacing();
                ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "主按键 (Main Key):");

                struct KeyMap { const char* name; uint32_t vk; };
                static const KeyMap availableKeys[] = {
                    {"F1", 0x70}, {"F2", 0x71}, {"F3", 0x72}, {"F4", 0x73}, {"F5", 0x74}, {"F6", 0x75},
                    {"F7", 0x76}, {"F8", 0x77}, {"F9", 0x78}, {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
                    {"Tab", 0x09}, {"Tilde (~)", 0xC0}, {"Insert", 0x2D}, {"Delete", 0x2E},
                    {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22},
                    {"A", 0x41}, {"B", 0x42}, {"C", 0x43}, {"D", 0x44}, {"E", 0x45}, {"F", 0x46}, {"G", 0x47},
                    {"H", 0x48}, {"I", 0x49}, {"J", 0x4A}, {"K", 0x4B}, {"L", 0x4C}, {"M", 0x4D}, {"N", 0x4E},
                    {"O", 0x4F}, {"P", 0x50}, {"Q", 0x51}, {"R", 0x52}, {"S", 0x53}, {"T", 0x54}, {"U", 0x55},
                    {"V", 0x56}, {"W", 0x57}, {"X", 0x58}, {"Y", 0x59}, {"Z", 0x5A}
                };

                std::string currentKeyName = "未知";
                for (auto& k : availableKeys) {
                    if (k.vk == config->editorHotkey) { currentKeyName = k.name; break; }
                }

                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("##MainKeyCombo", currentKeyName.c_str())) {
                    for (auto& k : availableKeys) {
                        if (ImGui::Selectable(k.name, config->editorHotkey == k.vk)) {
                            config->editorHotkey = k.vk;
                            config->SaveINISettings();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                std::string hotkeyDesc = "";
                if (modCtrl) hotkeyDesc += "Ctrl + ";
                if (modShift) hotkeyDesc += "Shift + ";
                if (modAlt) hotkeyDesc += "Alt + ";
                hotkeyDesc += currentKeyName;

                ImGui::Text("最终唤出快捷键:"); ImGui::SameLine();
                ImGui::TextColored({ 1.0f, 1.0f, 0.0f, 1.0f }, "%s", hotkeyDesc.c_str());
                ImGui::TextDisabled("💡 注意：关闭菜单已被强制锁定为 ESC 键。");
            }
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("维护与诊断", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("清空节点缓存并刷新显示")) {
                    NodeManager::ClearAllCaches();
                    HolsterManager::GetSingleton()->ForceRefreshAll();
                }
                ImGui::SameLine();
                if (ImGui::Button("保存当前配置")) {
                    config->SaveConfig();
                    config->SaveINISettings();
                }
            }
            ImGui::Spacing();
        }
        ImGui::End();
    }
}
