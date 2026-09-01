#include "pch.h"
#include "ConfigManager.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <Windows.h> 
#include <RE/T/TESForm.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/E/ENUM_FORM_ID.h> // 🌟 新增：必须包含这个头文件

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace IAD {
	static void ParseModelAnimation(const json& j, ModelAnimationConfig& out);
	static json SerializeModelAnimation(const ModelAnimationConfig& animation);
	static void ParseModelLight(const json& j, ModelLightConfig& out);
	static json SerializeModelLight(const ModelLightConfig& light);
	static void ParseModelEffectShader(const json& j, ModelEffectShaderConfig& out);
	static json SerializeModelEffectShader(const ModelEffectShaderConfig& effect);

	static bool IsSafeConfigFileName(const std::string& name) {
		if (name.empty()) return false;
		return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
	}

	uint8_t ConfigManager::StringToFormType(const std::string& typeStr) {
		if (typeStr == "WEAP") return static_cast<uint8_t>(RE::ENUM_FORM_ID::kWEAP);
		if (typeStr == "ARMO") return static_cast<uint8_t>(RE::ENUM_FORM_ID::kARMO);
		if (typeStr == "AMMO") return static_cast<uint8_t>(RE::ENUM_FORM_ID::kAMMO);
		if (typeStr == "ALCH") return static_cast<uint8_t>(RE::ENUM_FORM_ID::kALCH);
		if (typeStr == "MISC") return static_cast<uint8_t>(RE::ENUM_FORM_ID::kMISC);
		return 0;
	}

	std::string ConfigManager::FormTypeToString(uint8_t type) {
		if (type == static_cast<uint8_t>(RE::ENUM_FORM_ID::kWEAP)) return "WEAP";
		if (type == static_cast<uint8_t>(RE::ENUM_FORM_ID::kARMO)) return "ARMO";
		if (type == static_cast<uint8_t>(RE::ENUM_FORM_ID::kAMMO)) return "AMMO";
		if (type == static_cast<uint8_t>(RE::ENUM_FORM_ID::kALCH)) return "ALCH";
		if (type == static_cast<uint8_t>(RE::ENUM_FORM_ID::kMISC)) return "MISC";
		return std::to_string(type);
	}

	bool ConfigManager::IsRecentAcquiredFormTypeEnabled(std::uint8_t a_formType) const {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		if (!prioritizeRecentAcquired) return false;
		return std::find(recentAcquiredFormTypes.begin(), recentAcquiredFormTypes.end(), a_formType) != recentAcquiredFormTypes.end();
	}

	void ConfigManager::SetRecentAcquiredFormTypeEnabled(std::uint8_t a_formType, bool a_enabled) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		auto it = std::find(recentAcquiredFormTypes.begin(), recentAcquiredFormTypes.end(), a_formType);
		if (a_enabled) {
			if (it == recentAcquiredFormTypes.end()) {
				recentAcquiredFormTypes.push_back(a_formType);
			}
		}
		else if (it != recentAcquiredFormTypes.end()) {
			recentAcquiredFormTypes.erase(it);
		}
	}

	bool ConfigManager::GetRuntimeVariable(const std::string& a_name) {
		if (a_name.empty()) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		auto it = runtimeVariables.find(a_name);
		return it != runtimeVariables.end() ? it->second : false;
	}

	void ConfigManager::SetRuntimeVariable(const std::string& a_name, bool a_value) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeVariables[a_name] = a_value;
	}

	void ConfigManager::RemoveRuntimeVariable(const std::string& a_name) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeVariables.erase(a_name);
	}

	std::map<std::string, bool> ConfigManager::GetRuntimeVariablesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return runtimeVariables;
	}

	float ConfigManager::GetRuntimeNumberVariable(const std::string& a_name) {
		if (a_name.empty()) return 0.0f;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		auto it = runtimeNumberVariables.find(a_name);
		return it != runtimeNumberVariables.end() ? it->second : 0.0f;
	}

	void ConfigManager::SetRuntimeNumberVariable(const std::string& a_name, float a_value) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeNumberVariables[a_name] = a_value;
	}

	void ConfigManager::RemoveRuntimeNumberVariable(const std::string& a_name) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeNumberVariables.erase(a_name);
	}

	std::map<std::string, float> ConfigManager::GetRuntimeNumberVariablesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return runtimeNumberVariables;
	}

	std::string ConfigManager::GetRuntimeModelPathVariable(const std::string& a_name) {
		if (a_name.empty()) return "";
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		auto it = runtimeModelPathVariables.find(a_name);
		return it != runtimeModelPathVariables.end() ? it->second : "";
	}

	void ConfigManager::SetRuntimeModelPathVariable(const std::string& a_name, const std::string& a_value) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeModelPathVariables[a_name] = a_value;
	}

	void ConfigManager::RemoveRuntimeModelPathVariable(const std::string& a_name) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeModelPathVariables.erase(a_name);
	}

	std::map<std::string, std::string> ConfigManager::GetRuntimeModelPathVariablesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return runtimeModelPathVariables;
	}

	std::uint32_t ConfigManager::GetRuntimeFormVariable(const std::string& a_name) {
		if (a_name.empty()) return 0;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		auto it = runtimeFormVariables.find(a_name);
		return it != runtimeFormVariables.end() ? it->second : 0;
	}

	void ConfigManager::SetRuntimeFormVariable(const std::string& a_name, std::uint32_t a_value) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeFormVariables[a_name] = a_value;
	}

	void ConfigManager::RemoveRuntimeFormVariable(const std::string& a_name) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		runtimeFormVariables.erase(a_name);
	}

	std::map<std::string, std::uint32_t> ConfigManager::GetRuntimeFormVariablesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return runtimeFormVariables;
	}

	bool ConfigManager::IsActorDisplayBlocked(RE::Actor* a_actor) {
		if (!a_actor) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return (a_actor->IsPlayerRef() && blockPlayerDisplays) ||
			blockedActorFormIDs.contains(a_actor->GetFormID());
	}

	bool ConfigManager::IsPlayerDisplaysBlocked() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return blockPlayerDisplays;
	}

	bool ConfigManager::SetPlayerDisplaysBlocked(bool a_blocked) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		if (blockPlayerDisplays == a_blocked) return false;
		blockPlayerDisplays = a_blocked;
		return true;
	}

	bool ConfigManager::AddBlockedActorFormID(std::uint32_t a_formID) {
		if (a_formID == 0) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return blockedActorFormIDs.emplace(a_formID).second;
	}

	bool ConfigManager::RemoveBlockedActorFormID(std::uint32_t a_formID) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return blockedActorFormIDs.erase(a_formID) != 0;
	}

	std::vector<std::uint32_t> ConfigManager::GetBlockedActorFormIDsSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return { blockedActorFormIDs.begin(), blockedActorFormIDs.end() };
	}

	void ConfigManager::AddNodeMonitorName(const std::string& a_name) {
		if (a_name.empty()) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		if (std::find(nodeMonitorNames.begin(), nodeMonitorNames.end(), a_name) == nodeMonitorNames.end()) {
			nodeMonitorNames.push_back(a_name);
		}
	}

	void ConfigManager::RemoveNodeMonitorName(std::size_t a_index) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		if (a_index < nodeMonitorNames.size()) {
			nodeMonitorNames.erase(nodeMonitorNames.begin() + static_cast<std::ptrdiff_t>(a_index));
		}
	}

	std::vector<std::string> ConfigManager::GetNodeMonitorNamesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return nodeMonitorNames;
	}

	std::vector<std::string> ConfigManager::GetKeyBindConditionKeysSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		std::set<std::string> keys;

		auto collectTree = [&](const auto& self, const ConditionNode& node) -> void {
			if (node.isGroup) {
				for (const auto& child : node.children) {
					self(self, child);
				}
				return;
			}
			if (node.type == "KeyBindState" && !node.keyword.empty()) {
				keys.insert(node.keyword);
			}
		};

		auto collectBase = [&](const ConfigBase& entry) {
			collectTree(collectTree, entry.displayConditionTree);
			for (const auto& state : entry.stateMachine) {
				collectTree(collectTree, state.conditionTree);
			}
		};

		for (const auto& [scope, entries] : _slots) {
			for (const auto& [target, slots] : entries) {
				for (const auto& slot : slots) {
					collectBase(slot);
					collectTree(collectTree, slot.itemFilterConditionTree);
				}
			}
		}
		for (const auto& [scope, entries] : _nodes) {
			for (const auto& [target, nodes] : entries) {
				for (const auto& node : nodes) {
					collectBase(node);
				}
			}
		}
		for (const auto& [scope, entries] : _customs) {
			for (const auto& [target, customs] : entries) {
				for (const auto& custom : customs) {
					collectBase(custom);
					collectTree(collectTree, custom.inventoryConditionTree);
					collectTree(collectTree, custom.lastEquippedFilterConditionTree);
					for (const auto& group : custom.modelGroups) {
						collectTree(collectTree, group.displayConditionTree);
					}
				}
			}
		}

		return { keys.begin(), keys.end() };
	}

	std::map<std::string, KeyBindDefinition> ConfigManager::GetKeyBindDefinitionsSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return keyBindDefinitions;
	}

	void ConfigManager::SetKeyBindDefinitions(std::map<std::string, KeyBindDefinition> a_definitions) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		keyBindDefinitions.clear();
		for (auto& [name, definition] : a_definitions) {
			if (name.empty() || name.size() > 128 || definition.key == 0 || definition.key > 0xFF) {
				continue;
			}
			definition.comboKey = definition.comboKey <= 0xFF ? definition.comboKey : 0;
			definition.numStates = std::clamp(definition.numStates, 1u, 32u);
			keyBindDefinitions.emplace(std::move(name), definition);
		}
	}

	std::vector<std::uint32_t> ConfigManager::GetQuestStageConditionFormIDsSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		std::set<std::uint32_t> formIDs;

		auto parseFormID = [](const std::string& value) -> std::uint32_t {
			if (value.empty()) return 0;
			try {
				auto text = value;
				if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) text = text.substr(2);
				return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
			}
			catch (...) {
				return 0;
			}
		};

		auto collectTree = [&](const auto& self, const ConditionNode& node) -> void {
			if (node.isGroup) {
				for (const auto& child : node.children) self(self, child);
				return;
			}
			if (node.type == "QuestStage") {
				if (const auto formID = parseFormID(node.keyword); formID != 0) formIDs.insert(formID);
			}
		};

		auto collectBase = [&](const ConfigBase& entry) {
			collectTree(collectTree, entry.displayConditionTree);
			for (const auto& state : entry.stateMachine) collectTree(collectTree, state.conditionTree);
		};

		for (const auto& [scope, entries] : _slots) {
			for (const auto& [target, slots] : entries) {
				for (const auto& slot : slots) {
					collectBase(slot);
					collectTree(collectTree, slot.itemFilterConditionTree);
				}
			}
		}
		for (const auto& [scope, entries] : _nodes) {
			for (const auto& [target, nodes] : entries) {
				for (const auto& node : nodes) collectBase(node);
			}
		}
		for (const auto& [scope, entries] : _customs) {
			for (const auto& [target, customs] : entries) {
				for (const auto& custom : customs) {
					collectBase(custom);
					collectTree(collectTree, custom.inventoryConditionTree);
					collectTree(collectTree, custom.lastEquippedFilterConditionTree);
					for (const auto& group : custom.modelGroups) collectTree(collectTree, group.displayConditionTree);
				}
			}
		}

		return { formIDs.begin(), formIDs.end() };
	}

	std::vector<std::uint32_t> ConfigManager::GetActiveEffectConditionFormIDsSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		std::set<std::uint32_t> formIDs;

		auto parseFormID = [](const std::string& value) -> std::uint32_t {
			if (value.empty()) return 0;
			try {
				auto text = value;
				if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) text = text.substr(2);
				return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
			}
			catch (...) {
				return 0;
			}
		};

		auto collectTree = [&](const auto& self, const ConditionNode& node) -> void {
			if (node.isGroup) {
				for (const auto& child : node.children) self(self, child);
				return;
			}
			if (node.type == "HasActiveEffect" || node.type == "HasMagicEffect" || node.type == "HasSpell") {
				if (const auto formID = parseFormID(node.keyword); formID != 0) formIDs.insert(formID);
			}
		};

		auto collectBase = [&](const ConfigBase& entry) {
			collectTree(collectTree, entry.displayConditionTree);
			for (const auto& state : entry.stateMachine) collectTree(collectTree, state.conditionTree);
		};

		for (const auto& [scope, entries] : _slots) {
			for (const auto& [target, slots] : entries) {
				for (const auto& slot : slots) {
					collectBase(slot);
					collectTree(collectTree, slot.itemFilterConditionTree);
				}
			}
		}
		for (const auto& [scope, entries] : _nodes) {
			for (const auto& [target, nodes] : entries) {
				for (const auto& node : nodes) collectBase(node);
			}
		}
		for (const auto& [scope, entries] : _customs) {
			for (const auto& [target, customs] : entries) {
				for (const auto& custom : customs) {
					collectBase(custom);
					collectTree(collectTree, custom.inventoryConditionTree);
					collectTree(collectTree, custom.lastEquippedFilterConditionTree);
					for (const auto& group : custom.modelGroups) collectTree(collectTree, group.displayConditionTree);
				}
			}
		}
		for (const auto& variable : conditionalVariables) {
			for (const auto& rule : variable.rules) collectTree(collectTree, rule.conditionTree);
		}

		return { formIDs.begin(), formIDs.end() };
	}

	RuntimeSettingsSnapshot ConfigManager::GetRuntimeSettingsSnapshot() const {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		RuntimeSettingsSnapshot snapshot;
		snapshot.displayFavoritesOnly = displayFavoritesOnly;
		snapshot.prioritizeEquippedCandidates = prioritizeEquippedCandidates;
		snapshot.useRecentDisplaySlotMemory = useRecentDisplaySlotMemory;
		snapshot.reserveEquippedForPositivePrioritySlots = reserveEquippedForPositivePrioritySlots;
		snapshot.prioritizeRecentAcquired = prioritizeRecentAcquired;
		snapshot.enableEquipmentPhysics = enableEquipmentPhysics;
		snapshot.enableModelEffects = enableModelEffects;
		snapshot.enableModelLights = enableModelLights;
		snapshot.enableNPCDisplays = enableNPCDisplays;
		snapshot.blockPlayerDisplays = blockPlayerDisplays;
		snapshot.npcEvaluationIntervalTicks = npcEvaluationIntervalTicks;
		snapshot.nodeMonitorUseFilter = nodeMonitorUseFilter;
		return snapshot;
	}

	std::string ConfigManager::GetConfigDir() const {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return _configDir;
	}

	InputSettingsSnapshot ConfigManager::GetInputSettingsSnapshot() const {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return {
			editorHotkey,
			editorModifier,
			playerBlockHotkey,
			playerBlockModifier
		};
	}

	std::vector<ConditionalVariableDefinition> ConfigManager::GetConditionalVariablesSnapshot() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return conditionalVariables;
	}

	void ConfigManager::SetConditionalVariables(std::vector<ConditionalVariableDefinition> a_variables) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		conditionalVariables = std::move(a_variables);
	}

	static void ParseFormFilter(const json& j, FormFilter& out) {
		out.denyAll = j.value("DenyAll", false);
		out.useProfile = j.value("UseProfile", false);
		out.profileName = j.value("ProfileName", "");
		if (j.contains("AllowList") && j["AllowList"].is_array()) {
			out.allowList = j["AllowList"].get<std::set<std::uint32_t>>();
		}
		if (j.contains("DenyList") && j["DenyList"].is_array()) {
			out.denyList = j["DenyList"].get<std::set<std::uint32_t>>();
		}
	}

	static json SerializeFormFilter(const FormFilter& f) {
		json j;
		j["DenyAll"] = f.denyAll;
		j["AllowList"] = f.allowList;
		j["DenyList"] = f.denyList;
		j["UseProfile"] = f.useProfile;
		j["ProfileName"] = f.profileName;
		return j;
	}

	static bool TryParsePoint3(const json& value, RE::NiPoint3& out) {
		if (!value.is_array() || value.size() < 3) return false;
		try {
			out = {
				value.at(0).get<float>(),
				value.at(1).get<float>(),
				value.at(2).get<float>()
			};
			return true;
		}
		catch (...) {
			return false;
		}
	}

	static void ParseTransform(const json& j, TransformData& out) {
		if (!j.is_object()) return;
		if (j.contains("Pos")) TryParsePoint3(j["Pos"], out.pos);
		if (j.contains("Rot")) TryParsePoint3(j["Rot"], out.rot);
		if (j.contains("Pivot")) TryParsePoint3(j["Pivot"], out.pivot);
		try {
			out.scale = j.value("Scale", 1.0f);
		}
		catch (...) {
			// Preserve the default scale when a legacy config contains a wrong type.
		}
	}

	static json SerializeTransform(const TransformData& t) {
		json j; j["Pos"] = { t.pos.x, t.pos.y, t.pos.z }; j["Rot"] = { t.rot.x, t.rot.y, t.rot.z }; j["Scale"] = t.scale;
		j["Pivot"] = { t.pivot.x, t.pivot.y, t.pivot.z };
		return j;
	}

	static std::uint32_t ParseFormIDValue(const json& value, std::uint32_t fallback = 0) {
		try {
			if (value.is_number_unsigned()) return value.get<std::uint32_t>();
			if (value.is_number_integer()) return static_cast<std::uint32_t>(value.get<std::int64_t>());
			if (value.is_string()) {
				auto text = value.get<std::string>();
				if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) text = text.substr(2);
				return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
			}
		}
		catch (...) {}
		return fallback;
	}

	static void ParseModelSwapVariableSource(const json& j, ModelSwapVariableSource& out) {
		out.enabled = j.value("ModelSwapUseRuntimeVariable", out.enabled);
		out.pathVariable = j.value("ModelSwapPathVariable", out.pathVariable);
		out.formIDVariable = j.value("ModelSwapFormIDVariable", out.formIDVariable);
		if (j.contains("ModelSwapVariableSource") && j["ModelSwapVariableSource"].is_object()) {
			const auto& v = j["ModelSwapVariableSource"];
			out.enabled = v.value("Enabled", out.enabled);
			out.pathVariable = v.value("PathVariable", out.pathVariable);
			out.formIDVariable = v.value("FormIDVariable", out.formIDVariable);
		}
	}

	static void SerializeModelSwapVariableSource(const ModelSwapVariableSource& source, json& out) {
		out["ModelSwapUseRuntimeVariable"] = source.enabled;
		out["ModelSwapPathVariable"] = source.pathVariable;
		out["ModelSwapFormIDVariable"] = source.formIDVariable;
	}

	static std::vector<std::string> ParseStringVector(const json& j) {
		std::vector<std::string> out;
		if (!j.is_array()) return out;
		for (const auto& entry : j) {
			if (entry.is_string()) {
				auto value = entry.get<std::string>();
				if (!value.empty()) out.push_back(value);
			}
		}
		return out;
	}

	static json SerializeStringVector(const std::vector<std::string>& values) {
		json out = json::array();
		for (const auto& value : values) {
			if (!value.empty()) out.push_back(value);
		}
		return out;
	}

	static bool ParseBipedSlotValue(const json& value, std::uint32_t& out) {
		std::uint32_t raw = 0;
		try {
			if (value.is_number_unsigned()) {
				raw = value.get<std::uint32_t>();
			}
			else if (value.is_number_integer()) {
				const auto signedValue = value.get<std::int64_t>();
				if (signedValue < 0) return false;
				raw = static_cast<std::uint32_t>(signedValue);
			}
			else if (value.is_string()) {
				const auto text = value.get<std::string>();
				if (text.empty()) return false;
				raw = static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
			}
			else {
				return false;
			}
		}
		catch (...) {
			return false;
		}

		if (raw < 32) {
			out = raw + 30;
			return true;
		}
		if (raw >= 30 && raw <= 61) {
			out = raw;
			return true;
		}
		return false;
	}

	static std::vector<std::uint32_t> ParseBipedSlotVector(const json& j) {
		std::vector<std::uint32_t> out;
		if (!j.is_array()) return out;
		for (const auto& entry : j) {
			std::uint32_t slot = 0;
			if (ParseBipedSlotValue(entry, slot) &&
				std::find(out.begin(), out.end(), slot) == out.end()) {
				out.push_back(slot);
			}
		}
		return out;
	}

	static json SerializeUInt32Vector(const std::vector<std::uint32_t>& values) {
		json out = json::array();
		for (auto value : values) {
			out.push_back(value);
		}
		return out;
	}

	static void ParseColor(const json& j, ColorRGBA& out) {
		if (j.is_array() && j.size() >= 3) {
			try {
				out.r = j.at(0).get<float>();
				out.g = j.at(1).get<float>();
				out.b = j.at(2).get<float>();
				if (j.size() >= 4) out.a = j.at(3).get<float>();
			}
			catch (...) {
				// Keep the previous color when a legacy config contains bad values.
			}
			return;
		}
		if (j.is_object()) {
			try {
				out.r = j.value("R", out.r);
				out.g = j.value("G", out.g);
				out.b = j.value("B", out.b);
				out.a = j.value("A", out.a);
			}
			catch (...) {
				// Keep the previous color when a legacy config contains bad values.
			}
		}
	}

	static json SerializeColor(const ColorRGBA& color) {
		return json::array({ color.r, color.g, color.b, color.a });
	}

	void ParseConditionNode(const json& j, ConditionNode& node) {
		node.isGroup = j.value("IsGroup", false);
		node.isAnd = j.value("IsAnd", true);
		node.isNot = j.value("IsNot", false);
		if (node.isGroup) {
			if (j.contains("Children")) {
				for (const auto& jc : j["Children"]) {
					ConditionNode child; ParseConditionNode(jc, child); node.children.push_back(child);
				}
			}
		}
		else {
			node.type = j.value("Type", "");
			node.keyword = j.value("Keyword", "");
			node.keyword2 = j.value("Keyword2", "");
			node.expected = j.value("Expected", true);
		}
	}

	json SerializeConditionNode(const ConditionNode& node) {
		json j; j["IsGroup"] = node.isGroup; j["IsAnd"] = node.isAnd; j["IsNot"] = node.isNot;
		if (node.isGroup) {
			json childrenArray = json::array();
			for (const auto& child : node.children) childrenArray.push_back(SerializeConditionNode(child));
			j["Children"] = childrenArray;
		}
		else {
			j["Type"] = node.type;
			j["Keyword"] = node.keyword;
			j["Keyword2"] = node.keyword2;
			j["Expected"] = node.expected;
		}
		return j;
	}

	static void ParsePhysicsConstraint(const json& j, PhysicsConstraintParams& out) {
		out.velocityResponseScale = j.value("VelocityResponseScale", 0.1f);
		out.penBiasDepthLimit = j.value("PenBiasDepthLimit", 20000.0f);
		out.restitutionCoefficient = j.value("RestitutionCoefficient", 0.0f);
		out.penBiasFactor = j.value("PenBiasFactor", 1.0f);
	}

	static json SerializePhysicsConstraint(const PhysicsConstraintParams& p) {
		json j; j["VelocityResponseScale"] = p.velocityResponseScale; j["PenBiasDepthLimit"] = p.penBiasDepthLimit; j["RestitutionCoefficient"] = p.restitutionCoefficient; j["PenBiasFactor"] = p.penBiasFactor;
		return j;
	}

	static void ParsePhysics(const json& j, PhysicsValues& out) {
		if (!j.is_object()) return;
		out.disabled = j.value("Disabled", true);
		out.drawConstraints = j.value("DrawConstraints", false);
		out.drawPendulum = j.value("DrawPendulum", false);
		out.stiffness = j.value("Stiffness", 2.0f); out.stiffness2 = j.value("Stiffness2", 1.0f);
		out.springSlackOffset = j.value("SpringSlackOffset", 0.0f); out.springSlackMag = j.value("SpringSlackMag", 0.0f);
		out.damping = j.value("Damping", 0.95f); out.resistance = j.value("Resistance", 0.0f);
		out.mass = j.value("Mass", 1.0f); out.maxVelocity = j.value("MaxVelocity", 20000.0f);
		out.gravityBias = j.value("GravityBias", 1200.0f);

		if (j.contains("Linear")) TryParsePoint3(j["Linear"], out.linear);
		if (j.contains("Rotational")) TryParsePoint3(j["Rotational"], out.rotational);
		if (j.contains("CogOffset")) TryParsePoint3(j["CogOffset"], out.cogOffset);

		out.enableAngularConstraint = j.value("EnableAngularConstraint", false);
		out.minPitch = j.value("MinPitch", -15.0f);
		out.maxPitch = j.value("MaxPitch", 60.0f);
		out.minYaw = j.value("MinYaw", -45.0f);
		out.maxYaw = j.value("MaxYaw", 45.0f);
		out.minRoll = j.value("MinRoll", -45.0f);
		out.maxRoll = j.value("MaxRoll", 45.0f);
		out.visualProbeLength = j.value("VisualProbeLength", 40.0f);

		out.enableBoxConstraint = j.value("EnableBoxConstraint", false);
		if (j.contains("MaxOffsetN")) TryParsePoint3(j["MaxOffsetN"], out.maxOffsetN);
		if (j.contains("MaxOffsetP")) TryParsePoint3(j["MaxOffsetP"], out.maxOffsetP);
		out.maxOffsetBoxFriction = j.value("MaxOffsetBoxFriction", 0.025f);
		if (j.contains("BoxParams")) ParsePhysicsConstraint(j["BoxParams"], out.boxParams);

		out.enableSphereConstraint = j.value("EnableSphereConstraint", false);
		if (j.contains("MaxOffsetSphereOffset")) TryParsePoint3(j["MaxOffsetSphereOffset"], out.maxOffsetSphereOffset);
		out.maxOffsetSphereRadius = j.value("MaxOffsetSphereRadius", 20.0f); out.maxOffsetSphereFriction = j.value("MaxOffsetSphereFriction", 0.025f);
		if (j.contains("SphereParams")) ParsePhysicsConstraint(j["SphereParams"], out.sphereParams);
	}

	static json SerializePhysics(const PhysicsValues& p) {
		json j; j["Disabled"] = p.disabled; j["DrawConstraints"] = p.drawConstraints; j["DrawPendulum"] = p.drawPendulum;
		j["Stiffness"] = p.stiffness; j["Stiffness2"] = p.stiffness2; j["SpringSlackOffset"] = p.springSlackOffset; j["SpringSlackMag"] = p.springSlackMag;
		j["Damping"] = p.damping; j["Resistance"] = p.resistance; j["Mass"] = p.mass; j["MaxVelocity"] = p.maxVelocity;
		j["GravityBias"] = p.gravityBias;

		j["Linear"] = { p.linear.x, p.linear.y, p.linear.z }; j["Rotational"] = { p.rotational.x, p.rotational.y, p.rotational.z };
		j["CogOffset"] = { p.cogOffset.x, p.cogOffset.y, p.cogOffset.z };

		j["EnableAngularConstraint"] = p.enableAngularConstraint;
		j["MinPitch"] = p.minPitch;
		j["MaxPitch"] = p.maxPitch;
		j["MinYaw"] = p.minYaw;
		j["MaxYaw"] = p.maxYaw;
		j["MinRoll"] = p.minRoll;
		j["MaxRoll"] = p.maxRoll;
		j["VisualProbeLength"] = p.visualProbeLength;

		j["EnableBoxConstraint"] = p.enableBoxConstraint; j["MaxOffsetN"] = { p.maxOffsetN.x, p.maxOffsetN.y, p.maxOffsetN.z };
		j["MaxOffsetP"] = { p.maxOffsetP.x, p.maxOffsetP.y, p.maxOffsetP.z }; j["MaxOffsetBoxFriction"] = p.maxOffsetBoxFriction;
		j["BoxParams"] = SerializePhysicsConstraint(p.boxParams);

		j["EnableSphereConstraint"] = p.enableSphereConstraint;
		j["MaxOffsetSphereOffset"] = { p.maxOffsetSphereOffset.x, p.maxOffsetSphereOffset.y, p.maxOffsetSphereOffset.z };
		j["MaxOffsetSphereRadius"] = p.maxOffsetSphereRadius; j["MaxOffsetSphereFriction"] = p.maxOffsetSphereFriction;
		j["SphereParams"] = SerializePhysicsConstraint(p.sphereParams); return j;
	}

	static void ParseStateMachine(const json& j, std::vector<StateOverride>& sm) {
		if (j.contains("StateMachine")) {
			for (auto& stJ : j["StateMachine"]) {
				StateOverride st; st.description = stJ.value("Description", "新状态");
				if (stJ.contains("Condition")) ParseConditionNode(stJ["Condition"], st.conditionTree);
				st.continueAfterMatch = stJ.value("ContinueAfterMatch", true);
				st.hideModel = stJ.value("HideModel", false);
				st.overrideModelSwap = stJ.value("OverrideModelSwap", false);
				st.modelSwapPath = stJ.value("ModelSwapPath", "");
				if (stJ.contains("ModelSwapFormID")) {
					st.modelSwapFormID = ParseFormIDValue(stJ["ModelSwapFormID"], 0);
				}
				ParseModelSwapVariableSource(stJ, st.modelSwapVariableSource);
				st.overrideDisplayFlags = stJ.value("OverrideDisplayFlags", false);
				st.extractMagazine = stJ.value("ExtractMagazine", false);
				st.useProjectileForAmmo = stJ.value("UseProjectileForAmmo", false);
				st.useWorldModel = stJ.value("UseWorldModel", false);
				st.invisible = stJ.value("Invisible", false);
				st.hideGeometry = stJ.value("HideGeometry", false);
				st.hideLight = stJ.value("HideLight", false);
				st.load1pWeaponModel = stJ.value("Load1pWeaponModel", false);
				st.keepTorchFlame = stJ.value("KeepTorchFlame", false);
				st.removeScabbard = stJ.value("RemoveScabbard", false);
				st.disableHavok = stJ.value("DisableHavok", st.disableHavok);
				st.removeEditorMarker = stJ.value("RemoveEditorMarker", st.removeEditorMarker);
				st.removeProjectileTracers = stJ.value("RemoveProjectileTracers", st.removeProjectileTracers);
				st.overrideAnimation = stJ.value("OverrideAnimation", false);
				if (stJ.contains("Animation")) ParseModelAnimation(stJ["Animation"], st.animation);
				st.overrideEffectShader = stJ.value("OverrideEffectShader", false);
				if (stJ.contains("EffectShader")) ParseModelEffectShader(stJ["EffectShader"], st.effectShader);
				st.overrideLight = stJ.value("OverrideLight", false);
				if (stJ.contains("Light")) ParseModelLight(stJ["Light"], st.light);
				st.overrideTransform = stJ.value("OverrideTransform", false);
				st.useTransformPreset = stJ.value("UseTransformPreset", false); st.targetTransformPreset = stJ.value("TargetTransformPreset", "");
				if (stJ.contains("IndependentTransform")) ParseTransform(stJ["IndependentTransform"], st.independentTransform);
				st.overridePhysics = stJ.value("OverridePhysics", false); st.usePhysicsPreset = stJ.value("UsePhysicsPreset", false);
				st.targetPhysicsPreset = stJ.value("TargetPhysicsPreset", "");
				if (stJ.contains("IndependentPhysics")) ParsePhysics(stJ["IndependentPhysics"], st.independentPhysics);

				st.overrideTargetNode = stJ.value("OverrideTargetNode", false);
				st.targetNode = stJ.value("TargetNode", "");
				st.absolutePosition = stJ.value("AbsolutePosition", false);
				st.weaponAdjust = stJ.value("WeaponAdjust", false);
				st.weightAdjust = stJ.value("WeightAdjust", false);

				st.overrideMeshTransform = stJ.value("OverrideMeshTransform", false);
				if (stJ.contains("IndependentMeshTransform")) ParseTransform(stJ["IndependentMeshTransform"], st.independentMeshTransform);

				st.overrideGeometryTransform = stJ.value("OverrideGeometryTransform", false);
				if (stJ.contains("IndependentGeometryTransform")) ParseTransform(stJ["IndependentGeometryTransform"], st.independentGeometryTransform);

				sm.push_back(st);
			}
		}
	}

	static json SerializeStateMachine(const std::vector<StateOverride>& sm) {
		json stArray = json::array();
		for (const auto& st : sm) {
			json stJ; stJ["Description"] = st.description; stJ["Condition"] = SerializeConditionNode(st.conditionTree);
			stJ["ContinueAfterMatch"] = st.continueAfterMatch;
			stJ["HideModel"] = st.hideModel;
			stJ["OverrideModelSwap"] = st.overrideModelSwap;
			stJ["ModelSwapPath"] = st.modelSwapPath;
			stJ["ModelSwapFormID"] = st.modelSwapFormID;
			SerializeModelSwapVariableSource(st.modelSwapVariableSource, stJ);
			stJ["OverrideDisplayFlags"] = st.overrideDisplayFlags;
			stJ["ExtractMagazine"] = st.extractMagazine;
			stJ["UseProjectileForAmmo"] = st.useProjectileForAmmo;
			stJ["UseWorldModel"] = st.useWorldModel;
			stJ["Invisible"] = st.invisible;
			stJ["HideGeometry"] = st.hideGeometry;
			stJ["HideLight"] = st.hideLight;
			stJ["Load1pWeaponModel"] = st.load1pWeaponModel;
			stJ["KeepTorchFlame"] = st.keepTorchFlame;
			stJ["RemoveScabbard"] = st.removeScabbard;
			stJ["DisableHavok"] = st.disableHavok;
			stJ["RemoveEditorMarker"] = st.removeEditorMarker;
			stJ["RemoveProjectileTracers"] = st.removeProjectileTracers;
			stJ["OverrideAnimation"] = st.overrideAnimation;
			stJ["Animation"] = SerializeModelAnimation(st.animation);
			stJ["OverrideEffectShader"] = st.overrideEffectShader;
			stJ["EffectShader"] = SerializeModelEffectShader(st.effectShader);
			stJ["OverrideLight"] = st.overrideLight;
			stJ["Light"] = SerializeModelLight(st.light);
			stJ["OverrideTransform"] = st.overrideTransform;
			stJ["UseTransformPreset"] = st.useTransformPreset; stJ["TargetTransformPreset"] = st.targetTransformPreset;
			stJ["IndependentTransform"] = SerializeTransform(st.independentTransform); stJ["OverridePhysics"] = st.overridePhysics;
			stJ["UsePhysicsPreset"] = st.usePhysicsPreset; stJ["TargetPhysicsPreset"] = st.targetPhysicsPreset;
			stJ["IndependentPhysics"] = SerializePhysics(st.independentPhysics);

			stJ["OverrideTargetNode"] = st.overrideTargetNode;
			stJ["TargetNode"] = st.targetNode;
			stJ["AbsolutePosition"] = st.absolutePosition;
			stJ["WeaponAdjust"] = st.weaponAdjust;
			stJ["WeightAdjust"] = st.weightAdjust;

			stJ["OverrideMeshTransform"] = st.overrideMeshTransform;
			stJ["IndependentMeshTransform"] = SerializeTransform(st.independentMeshTransform);
			stJ["OverrideGeometryTransform"] = st.overrideGeometryTransform;
			stJ["IndependentGeometryTransform"] = SerializeTransform(st.independentGeometryTransform);

			stArray.push_back(stJ);
		}
		return stArray;
	}

	static ConditionalVariableType ParseConditionalVariableType(const std::string& value) {
		if (value == "Number") return ConditionalVariableType::kNumber;
		if (value == "Form") return ConditionalVariableType::kForm;
		if (value == "ModelPath") return ConditionalVariableType::kModelPath;
		return ConditionalVariableType::kBoolean;
	}

	static const char* ConditionalVariableTypeToString(ConditionalVariableType type) {
		switch (type) {
		case ConditionalVariableType::kNumber: return "Number";
		case ConditionalVariableType::kForm: return "Form";
		case ConditionalVariableType::kModelPath: return "ModelPath";
		default: return "Boolean";
		}
	}

	static ConditionalVariableFormSource ParseConditionalVariableFormSource(const std::string& value) {
		if (value == "EquippedWeapon") return ConditionalVariableFormSource::kEquippedWeapon;
		return ConditionalVariableFormSource::kStatic;
	}

	static const char* ConditionalVariableFormSourceToString(ConditionalVariableFormSource source) {
		switch (source) {
		case ConditionalVariableFormSource::kEquippedWeapon: return "EquippedWeapon";
		default: return "Static";
		}
	}

	static void ParseConditionalVariables(const json& input, std::vector<ConditionalVariableDefinition>& output) {
		output.clear();
		if (!input.is_array()) return;
		for (const auto& entry : input) {
			if (!entry.is_object()) continue;
			ConditionalVariableDefinition variable;
			variable.name = entry.value("Name", "");
			if (variable.name.empty()) continue;
			variable.description = entry.value("Description", variable.name);
			variable.type = ParseConditionalVariableType(entry.value("Type", "Boolean"));
			variable.enabled = entry.value("Enabled", true);
			variable.defaultBooleanValue = entry.value("DefaultBoolean", false);
			variable.defaultNumberValue = entry.value("DefaultNumber", 0.0f);
			variable.defaultFormIDValue = entry.contains("DefaultFormID") ? ParseFormIDValue(entry["DefaultFormID"], 0) : 0;
			variable.defaultFormSource = ParseConditionalVariableFormSource(entry.value("DefaultFormSource", "Static"));
			variable.defaultModelPathValue = entry.value("DefaultModelPath", "");
			if (entry.contains("Rules") && entry["Rules"].is_array()) {
				for (const auto& ruleInput : entry["Rules"]) {
					if (!ruleInput.is_object()) continue;
					ConditionalVariableRule rule;
					if (ruleInput.contains("Condition")) ParseConditionNode(ruleInput["Condition"], rule.conditionTree);
					rule.continueAfterMatch = ruleInput.value("ContinueAfterMatch", true);
					rule.booleanValue = ruleInput.value("BooleanValue", false);
					rule.numberValue = ruleInput.value("NumberValue", 0.0f);
					rule.formIDValue = ruleInput.contains("FormIDValue") ? ParseFormIDValue(ruleInput["FormIDValue"], 0) : 0;
					rule.formSource = ParseConditionalVariableFormSource(ruleInput.value("FormSource", "Static"));
					rule.modelPathValue = ruleInput.value("ModelPathValue", "");
					variable.rules.push_back(std::move(rule));
				}
			}
			output.push_back(std::move(variable));
		}
	}

	static json SerializeConditionalVariables(const std::vector<ConditionalVariableDefinition>& variables) {
		json output = json::array();
		for (const auto& variable : variables) {
			if (variable.name.empty()) continue;
			json entry;
			entry["Name"] = variable.name;
			entry["Description"] = variable.description;
			entry["Type"] = ConditionalVariableTypeToString(variable.type);
			entry["Enabled"] = variable.enabled;
			entry["DefaultBoolean"] = variable.defaultBooleanValue;
			entry["DefaultNumber"] = variable.defaultNumberValue;
			entry["DefaultFormID"] = variable.defaultFormIDValue;
			entry["DefaultFormSource"] = ConditionalVariableFormSourceToString(variable.defaultFormSource);
			entry["DefaultModelPath"] = variable.defaultModelPathValue;
			entry["Rules"] = json::array();
			for (const auto& rule : variable.rules) {
				json ruleOutput;
				ruleOutput["Condition"] = SerializeConditionNode(rule.conditionTree);
				ruleOutput["ContinueAfterMatch"] = rule.continueAfterMatch;
				ruleOutput["BooleanValue"] = rule.booleanValue;
				ruleOutput["NumberValue"] = rule.numberValue;
				ruleOutput["FormIDValue"] = rule.formIDValue;
				ruleOutput["FormSource"] = ConditionalVariableFormSourceToString(rule.formSource);
				ruleOutput["ModelPathValue"] = rule.modelPathValue;
				entry["Rules"].push_back(std::move(ruleOutput));
			}
			output.push_back(std::move(entry));
		}
		return output;
	}

	static void ParseConfigBase(const json& j, ConfigBase& out) {
		out.overrideTransform = j.value("OverrideTransform", false);
		out.overridePhysics = j.value("OverridePhysics", false);
		if (j.contains("m")) ParseTransform(j["m"], out.transforms.m);
		if (j.contains("f")) ParseTransform(j["f"], out.transforms.f); else out.transforms.f = out.transforms.m;
		if (j.contains("Physics")) ParsePhysics(j["Physics"], out.physics);
		if (j.contains("DisplayConditionTree")) ParseConditionNode(j["DisplayConditionTree"], out.displayConditionTree);
		ParseStateMachine(j, out.stateMachine);

		out.overrideMeshTransform = j.value("OverrideMeshTransform", false);
		if (j.contains("Mesh_m")) ParseTransform(j["Mesh_m"], out.meshTransforms.m);
		if (j.contains("Mesh_f")) ParseTransform(j["Mesh_f"], out.meshTransforms.f); else out.meshTransforms.f = out.meshTransforms.m;

		out.overrideGeometryTransform = j.value("OverrideGeometryTransform", false);
		if (j.contains("Geometry_m")) ParseTransform(j["Geometry_m"], out.geometryTransforms.m);
		if (j.contains("Geometry_f")) ParseTransform(j["Geometry_f"], out.geometryTransforms.f); else out.geometryTransforms.f = out.geometryTransforms.m;
	}

	static void SerializeConfigBase(const ConfigBase& base, json& j) {
		j["OverrideTransform"] = base.overrideTransform;
		j["OverridePhysics"] = base.overridePhysics;
		j["m"] = SerializeTransform(base.transforms.m);
		j["f"] = SerializeTransform(base.transforms.f);
		j["Physics"] = SerializePhysics(base.physics);
		j["DisplayConditionTree"] = SerializeConditionNode(base.displayConditionTree);
		j["StateMachine"] = SerializeStateMachine(base.stateMachine);

		j["OverrideMeshTransform"] = base.overrideMeshTransform;
		j["Mesh_m"] = SerializeTransform(base.meshTransforms.m);
		j["Mesh_f"] = SerializeTransform(base.meshTransforms.f);

		j["OverrideGeometryTransform"] = base.overrideGeometryTransform;
		j["Geometry_m"] = SerializeTransform(base.geometryTransforms.m);
		j["Geometry_f"] = SerializeTransform(base.geometryTransforms.f);
	}

	static void ParseSkeletonMatch(const json& j, NodeDefinition::SkeletonMatchConfig& out) {
		if (!j.is_object()) return;
		out.enabled = j.value("Enabled", out.enabled);
		out.invert = j.value("Invert", out.invert);
		out.skeletonPathContains = j.value("SkeletonPathContains", out.skeletonPathContains);
		if (j.contains("RaceFormID")) out.raceFormID = ParseFormIDValue(j["RaceFormID"], out.raceFormID);
		if (j.contains("NPCFormID")) out.npcFormID = ParseFormIDValue(j["NPCFormID"], out.npcFormID);
		if (j.contains("RequiredNodes")) out.requiredNodes = ParseStringVector(j["RequiredNodes"]);
		if (j.contains("ForbiddenNodes")) out.forbiddenNodes = ParseStringVector(j["ForbiddenNodes"]);
	}

	static json SerializeSkeletonMatch(const NodeDefinition::SkeletonMatchConfig& match) {
		json j;
		j["Enabled"] = match.enabled;
		j["Invert"] = match.invert;
		j["SkeletonPathContains"] = match.skeletonPathContains;
		j["RaceFormID"] = match.raceFormID;
		j["NPCFormID"] = match.npcFormID;
		j["RequiredNodes"] = SerializeStringVector(match.requiredNodes);
		j["ForbiddenNodes"] = SerializeStringVector(match.forbiddenNodes);
		return j;
	}

	std::vector<SlotDefinition>& ConfigManager::GetSlots(ConfigScope a_scope, std::uint32_t a_idOrFilter) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex); return _slots[a_scope][a_idOrFilter];
	}
	std::vector<NodeDefinition>& ConfigManager::GetNodes(ConfigScope a_scope, std::uint32_t a_idOrFilter) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex); return _nodes[a_scope][a_idOrFilter];
	}
	std::vector<CustomDefinition>& ConfigManager::GetCustoms(ConfigScope a_scope, std::uint32_t a_idOrFilter) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex); return _customs[a_scope][a_idOrFilter];
	}

	template <class T>
	static std::vector<std::uint32_t> GetConfiguredTargetIDsFromMap(const std::map<ConfigScope, std::map<std::uint32_t, std::vector<T>>>& data, ConfigScope scope) {
		std::vector<std::uint32_t> result;
		auto scopeIt = data.find(scope);
		if (scopeIt == data.end()) return result;
		for (const auto& [id, entries] : scopeIt->second) {
			if (!entries.empty()) {
				result.push_back(id);
			}
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	std::vector<std::uint32_t> ConfigManager::GetConfiguredSlotTargetIDs(ConfigScope a_scope) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return GetConfiguredTargetIDsFromMap(_slots, a_scope);
	}

	std::vector<std::uint32_t> ConfigManager::GetConfiguredNodeTargetIDs(ConfigScope a_scope) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return GetConfiguredTargetIDsFromMap(_nodes, a_scope);
	}

	std::vector<std::uint32_t> ConfigManager::GetConfiguredCustomTargetIDs(ConfigScope a_scope) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		return GetConfiguredTargetIDsFromMap(_customs, a_scope);
	}

	void ParseJsonToSlotList(const json& data, std::vector<SlotDefinition>& slotList, const std::string& path) {
		if (!data.contains("Entries")) return;
		for (auto it = data["Entries"].begin(); it != data["Entries"].end(); ++it) {
			SlotDefinition slot; slot.slotName = it.key(); slot.originPath = path;
			const auto& j = it.value();
			slot.priority = j.value("Priority", 0); slot.isEnabled = j.value("Enabled", true); slot.targetNode = j.value("TargetNode", "");

			if (j.contains("PreferredItems") && j["PreferredItems"].is_array()) {
				for (auto& pi : j["PreferredItems"]) {
					auto formID = ParseFormIDValue(pi, 0);
					if (formID != 0) {
						slot.preferredItems.push_back(formID);
					}
				}
			}
			if (j.contains("ItemFilter")) ParseFormFilter(j["ItemFilter"], slot.itemFilter);
			if (j.contains("ItemFilterConditionTree")) ParseConditionNode(j["ItemFilterConditionTree"], slot.itemFilterConditionTree);

			slot.alwaysUnload = j.value("AlwaysUnload", false);
			slot.checkCannotWear = j.value("CheckCannotWear", false);
			slot.hideIfUsingFurniture = j.value("HideIfUsingFurniture", false);
			slot.hideLayingDown = j.value("HideLayingDown", false);
			slot.extractMagazine = j.value("ExtractMagazine", false);
			slot.useWorldModel = j.value("UseWorldModel", false);
			slot.invisible = j.value("Invisible", false);
			slot.hideGeometry = j.value("HideGeometry", false);
			slot.hideLight = j.value("HideLight", false);
			slot.load1pWeaponModel = j.value("Load1pWeaponModel", false);
			slot.keepTorchFlame = j.value("KeepTorchFlame", false);
			slot.removeScabbard = j.value("RemoveScabbard", false);
			slot.modelSwapPath = j.value("ModelSwapPath", "");
			if (j.contains("ModelSwapFormID")) {
				slot.modelSwapFormID = ParseFormIDValue(j["ModelSwapFormID"], 0);
			}
			ParseModelSwapVariableSource(j, slot.modelSwapVariableSource);
			slot.disableHavok = j.value("DisableHavok", slot.disableHavok);
			slot.removeEditorMarker = j.value("RemoveEditorMarker", slot.removeEditorMarker);
			slot.removeProjectileTracers = j.value("RemoveProjectileTracers", slot.removeProjectileTracers);
			if (j.contains("Animation")) ParseModelAnimation(j["Animation"], slot.animation);
			if (j.contains("Light")) ParseModelLight(j["Light"], slot.light);
			if (j.contains("EffectShader")) ParseModelEffectShader(j["EffectShader"], slot.effectShader);
			slot.overrideEquipmentMode = j.value("OverrideEquipmentMode", false);
			slot.displayFavoritesOnly = j.value("DisplayFavoritesOnly", true);
			// Migrate the short-lived strongest checkbox without changing existing
			// user presets. New configs write the explicit SelectionMode instead.
			if (j.contains("SelectionMode")) {
				const auto& mode = j["SelectionMode"];
				std::uint32_t value = 0;
				bool hasNumericMode = false;
				if (mode.is_number_unsigned()) {
					value = mode.get<std::uint32_t>();
					hasNumericMode = true;
				}
				else if (mode.is_number_integer()) {
					const auto signedValue = mode.get<std::int64_t>();
					if (signedValue >= 0) {
						value = static_cast<std::uint32_t>(signedValue);
						hasNumericMode = true;
					}
				}
				if (hasNumericMode && value <= static_cast<std::uint32_t>(SlotSelectionMode::kRandom)) {
					slot.selectionMode = static_cast<SlotSelectionMode>(value);
				}
				else if (mode.is_string()) {
					const auto modeName = mode.get<std::string>();
					if (modeName == "Strongest") slot.selectionMode = SlotSelectionMode::kStrongest;
					else if (modeName == "Random") slot.selectionMode = SlotSelectionMode::kRandom;
				}
			}
			else if (j.value("SelectInventoryStrongest", false)) {
				slot.selectionMode = SlotSelectionMode::kStrongest;
			}

			ParseConfigBase(j, slot);

			if (j.contains("AllowedFormTypes")) {
				for (auto& t : j["AllowedFormTypes"]) {
					if (t.is_number()) slot.allowedFormTypes.push_back(t.get<uint8_t>());
					else slot.allowedFormTypes.push_back(ConfigManager::StringToFormType(t.get<std::string>()));
				}
			}

			if (j.contains("FormTypePriority")) {
				for (auto& t : j["FormTypePriority"]) {
					auto type = t.is_number() ? t.get<uint8_t>() : ConfigManager::StringToFormType(t.get<std::string>());
					if (type != 0 && std::find(slot.formTypePriority.begin(), slot.formTypePriority.end(), type) == slot.formTypePriority.end()) {
						slot.formTypePriority.push_back(type);
					}
				}
			}
			slot.formTypePriorityLimit = j.value("FormTypePriorityLimit", 0);
			slot.formTypePriorityAccountForEquipped = j.value("FormTypePriorityAccountForEquipped", true);

			if (j.contains("AdvancedFilters")) {
				auto& afJ = j["AdvancedFilters"];
				slot.advancedFilters.useBaseFilters = afJ.value("UseBaseFilters", false);
				slot.advancedFilters.allowMelee = afJ.value("AllowMelee", true);
				slot.advancedFilters.allowGun = afJ.value("AllowGun", true);
				slot.advancedFilters.allowThrown = afJ.value("AllowThrown", true);
				slot.advancedFilters.allowOneHanded = afJ.value("AllowOneHanded", true);
				slot.advancedFilters.allowTwoHanded = afJ.value("AllowTwoHanded", true);
				slot.advancedFilters.allowArmor = afJ.value("AllowArmor", true);
				slot.advancedFilters.allowShield = afJ.value("AllowShield", true);
				slot.advancedFilters.allowAmmo = afJ.value("AllowAmmo", true);
				slot.advancedFilters.allowMedicine = afJ.value("AllowMedicine", true);
				slot.advancedFilters.allowFood = afJ.value("AllowFood", true);
				slot.advancedFilters.allowWater = afJ.value("AllowWater", true);
				slot.advancedFilters.allowKeys = afJ.value("AllowKeys", true);
			}

			slot.keywordMode = static_cast<IAD::KeywordFilterMode>(j.value("KeywordMode", 0));
			bool hasAnyValidKeyword = false;

			if (j.contains("KeywordGroups")) {
				for (auto& gJ : j["KeywordGroups"]) {
					KeywordGroup kg; kg.groupName = gJ.value("GroupName", "新组"); kg.isAnd = gJ.value("IsAnd", false);
					if (gJ.contains("Keywords")) {
						for (auto& kw : gJ["Keywords"]) {
							kg.keywords.push_back(kw.get<std::string>());
							hasAnyValidKeyword = true;
						}
					}
					slot.keywordGroups.push_back(kg);
				}
			}

			if (!hasAnyValidKeyword && slot.keywordMode != IAD::KeywordFilterMode::kNone) {
				slot.keywordMode = IAD::KeywordFilterMode::kNone;
			}

			slot.useProjectileForAmmo = j.value("UseProjectileForAmmo", false);

			if (j.contains("AmmoRig")) {
				auto& aj = j["AmmoRig"];
				slot.ammoRig.isDedicatedAmmoSlot = aj.value("IsDedicated", false);
				slot.ammoRig.displayMode = static_cast<AmmoDisplayMode>(aj.value("DisplayMode", 0));
				slot.ammoRig.maxMags = aj.value("MaxMags", 3);
				slot.ammoRig.magSpacing = aj.value("Spacing", 5.0f);
				slot.ammoRig.dynamicAmmoLogic = aj.value("Dynamic", true);
				if (aj.contains("Dir")) {
					TryParsePoint3(aj["Dir"], slot.ammoRig.arrayDirection);
				}
			}

			slot.holsterModelPath = j.value("HolsterModelPath", "");
			slot.keepHolsterWhenDrawn = j.value("KeepHolsterWhenDrawn", true);
			if (j.contains("Holster_m")) ParseTransform(j["Holster_m"], slot.holsterTransforms.m);
			if (j.contains("Holster_f")) ParseTransform(j["Holster_f"], slot.holsterTransforms.f); else slot.holsterTransforms.f = slot.holsterTransforms.m;

			slotList.push_back(slot);
		}
	}

	json SerializeSlot(const SlotDefinition& slot) {
		json j; j["Priority"] = slot.priority; j["Enabled"] = slot.isEnabled; j["TargetNode"] = slot.targetNode;

		j["PreferredItems"] = slot.preferredItems;
		j["ItemFilter"] = SerializeFormFilter(slot.itemFilter);
		j["ItemFilterConditionTree"] = SerializeConditionNode(slot.itemFilterConditionTree);

		j["AlwaysUnload"] = slot.alwaysUnload;
		j["CheckCannotWear"] = slot.checkCannotWear;
		j["HideIfUsingFurniture"] = slot.hideIfUsingFurniture;
		j["HideLayingDown"] = slot.hideLayingDown;
		j["ExtractMagazine"] = slot.extractMagazine;
		j["UseWorldModel"] = slot.useWorldModel;
		j["Invisible"] = slot.invisible;
		j["HideGeometry"] = slot.hideGeometry;
		j["HideLight"] = slot.hideLight;
		j["Load1pWeaponModel"] = slot.load1pWeaponModel;
		j["KeepTorchFlame"] = slot.keepTorchFlame;
		j["RemoveScabbard"] = slot.removeScabbard;
		j["ModelSwapPath"] = slot.modelSwapPath;
		j["ModelSwapFormID"] = slot.modelSwapFormID;
		SerializeModelSwapVariableSource(slot.modelSwapVariableSource, j);
		j["DisableHavok"] = slot.disableHavok;
		j["RemoveEditorMarker"] = slot.removeEditorMarker;
		j["RemoveProjectileTracers"] = slot.removeProjectileTracers;
		j["Animation"] = SerializeModelAnimation(slot.animation);
		j["Light"] = SerializeModelLight(slot.light);
		j["EffectShader"] = SerializeModelEffectShader(slot.effectShader);
		j["OverrideEquipmentMode"] = slot.overrideEquipmentMode;
		j["DisplayFavoritesOnly"] = slot.displayFavoritesOnly;
		j["SelectionMode"] = static_cast<std::uint32_t>(slot.selectionMode);

		j["AmmoRig"]["IsDedicated"] = slot.ammoRig.isDedicatedAmmoSlot;
		j["AmmoRig"]["DisplayMode"] = static_cast<int>(slot.ammoRig.displayMode);
		j["AmmoRig"]["MaxMags"] = slot.ammoRig.maxMags;
		j["AmmoRig"]["Spacing"] = slot.ammoRig.magSpacing;
		j["AmmoRig"]["Dynamic"] = slot.ammoRig.dynamicAmmoLogic;
		j["AmmoRig"]["Dir"] = { slot.ammoRig.arrayDirection.x, slot.ammoRig.arrayDirection.y, slot.ammoRig.arrayDirection.z };

		SerializeConfigBase(slot, j);

		json typesArray = json::array(); for (auto type : slot.allowedFormTypes) typesArray.push_back(ConfigManager::FormTypeToString(type));
		j["AllowedFormTypes"] = typesArray;

		json priorityTypesArray = json::array(); for (auto type : slot.formTypePriority) priorityTypesArray.push_back(ConfigManager::FormTypeToString(type));
		j["FormTypePriority"] = priorityTypesArray;
		j["FormTypePriorityLimit"] = slot.formTypePriorityLimit;
		j["FormTypePriorityAccountForEquipped"] = slot.formTypePriorityAccountForEquipped;

		j["AdvancedFilters"]["UseBaseFilters"] = slot.advancedFilters.useBaseFilters;
		j["AdvancedFilters"]["AllowMelee"] = slot.advancedFilters.allowMelee;
		j["AdvancedFilters"]["AllowGun"] = slot.advancedFilters.allowGun;
		j["AdvancedFilters"]["AllowThrown"] = slot.advancedFilters.allowThrown;
		j["AdvancedFilters"]["AllowOneHanded"] = slot.advancedFilters.allowOneHanded;
		j["AdvancedFilters"]["AllowTwoHanded"] = slot.advancedFilters.allowTwoHanded;
		j["AdvancedFilters"]["AllowArmor"] = slot.advancedFilters.allowArmor;
		j["AdvancedFilters"]["AllowShield"] = slot.advancedFilters.allowShield;
		j["AdvancedFilters"]["AllowAmmo"] = slot.advancedFilters.allowAmmo;
		j["AdvancedFilters"]["AllowMedicine"] = slot.advancedFilters.allowMedicine;
		j["AdvancedFilters"]["AllowFood"] = slot.advancedFilters.allowFood;
		j["AdvancedFilters"]["AllowWater"] = slot.advancedFilters.allowWater;
		j["AdvancedFilters"]["AllowKeys"] = slot.advancedFilters.allowKeys;

		j["KeywordMode"] = static_cast<int>(slot.keywordMode);
		json groupsArray = json::array();
		for (const auto& kg : slot.keywordGroups) {
			json gJ; gJ["GroupName"] = kg.groupName; gJ["IsAnd"] = kg.isAnd; gJ["Keywords"] = kg.keywords;
			groupsArray.push_back(gJ);
		}
		j["KeywordGroups"] = groupsArray;
		j["UseProjectileForAmmo"] = slot.useProjectileForAmmo;

		j["HolsterModelPath"] = slot.holsterModelPath;
		j["KeepHolsterWhenDrawn"] = slot.keepHolsterWhenDrawn;
		j["Holster_m"] = SerializeTransform(slot.holsterTransforms.m);
		j["Holster_f"] = SerializeTransform(slot.holsterTransforms.f);

		return j;
	}

	void ParseJsonToNodeList(const json& data, std::vector<NodeDefinition>& nodeList, const std::string& path) {
		if (!data.contains("Entries")) return;
		for (auto it = data["Entries"].begin(); it != data["Entries"].end(); ++it) {
			NodeDefinition node; node.nodeName = it.key(); node.originPath = path;
			const auto& j = it.value(); node.isEnabled = j.value("Enabled", true);
			node.absolutePosition = j.value("AbsolutePosition", false);
			node.weaponAdjust = j.value("WeaponAdjust", false);
			node.weightAdjust = j.value("WeightAdjust", false);

			ParseConfigBase(j, node);

			if (j.contains("FallbackHosts")) { for (auto& h : j["FallbackHosts"]) node.fallbackHosts.push_back(h); }
			if (j.contains("SkeletonMatch")) ParseSkeletonMatch(j["SkeletonMatch"], node.skeletonMatch);
			nodeList.push_back(node);
		}
	}

	json SerializeNode(const NodeDefinition& node) {
		json j; j["Enabled"] = node.isEnabled;
		j["AbsolutePosition"] = node.absolutePosition; j["WeaponAdjust"] = node.weaponAdjust; j["WeightAdjust"] = node.weightAdjust;

		SerializeConfigBase(node, j);

		j["FallbackHosts"] = node.fallbackHosts;
		j["SkeletonMatch"] = SerializeSkeletonMatch(node.skeletonMatch);
		return j;
	}

	static void ParseModelLight(const json& j, ModelLightConfig& out) {
		if (!j.is_object()) return;
		out.enabled = j.value("Enabled", out.enabled);
		out.targetSelf = j.value("TargetSelf", out.targetSelf);
		out.dontLightWater = j.value("DontLightWater", out.dontLightWater);
		out.dontLightLandscape = j.value("DontLightLandscape", out.dontLightLandscape);
		out.castShadows = j.value("CastShadows", out.castShadows);
		out.radius = j.value("Radius", out.radius);
		out.dimmer = j.value("Dimmer", out.dimmer);
		out.fieldOfView = j.value("FieldOfView", out.fieldOfView);
		out.shadowDepthBias = j.value("ShadowDepthBias", out.shadowDepthBias);
		if (j.contains("Transform")) ParseTransform(j["Transform"], out.transform);
		if (j.contains("Diffuse")) ParseColor(j["Diffuse"], out.diffuse);
	}

	static json SerializeModelLight(const ModelLightConfig& light) {
		json j;
		j["Enabled"] = light.enabled;
		j["TargetSelf"] = light.targetSelf;
		j["DontLightWater"] = light.dontLightWater;
		j["DontLightLandscape"] = light.dontLightLandscape;
		j["CastShadows"] = light.castShadows;
		j["Transform"] = SerializeTransform(light.transform);
		j["Diffuse"] = SerializeColor(light.diffuse);
		j["Radius"] = light.radius;
		j["Dimmer"] = light.dimmer;
		j["FieldOfView"] = light.fieldOfView;
		j["ShadowDepthBias"] = light.shadowDepthBias;
		return j;
	}

	static void ParseModelEffectShader(const json& j, ModelEffectShaderConfig& out) {
		if (!j.is_object()) return;
		out.enabled = j.value("Enabled", out.enabled);
		out.targetRoot = j.value("TargetRoot", out.targetRoot);
		out.force = j.value("Force", out.force);
		out.lighting = j.value("Lighting", out.lighting);
		out.alpha = j.value("Alpha", out.alpha);
		out.baseFillScale = j.value("BaseFillScale", out.baseFillScale);
		out.baseFillAlpha = j.value("BaseFillAlpha", out.baseFillAlpha);
		out.baseRimAlpha = j.value("BaseRimAlpha", out.baseRimAlpha);
		out.edgeExponent = j.value("EdgeExponent", out.edgeExponent);
		out.alphaMultiplier = j.value("AlphaMultiplier", out.alphaMultiplier);
		out.baseTexturePath = j.value("BaseTexturePath", out.baseTexturePath);
		out.paletteTexturePath = j.value("PaletteTexturePath", out.paletteTexturePath);
		out.blockOutTexturePath = j.value("BlockOutTexturePath", out.blockOutTexturePath);
		if (j.contains("FillColor")) ParseColor(j["FillColor"], out.fillColor);
		if (j.contains("RimColor")) ParseColor(j["RimColor"], out.rimColor);
	}

	static json SerializeModelEffectShader(const ModelEffectShaderConfig& effect) {
		json j;
		j["Enabled"] = effect.enabled;
		j["TargetRoot"] = effect.targetRoot;
		j["Force"] = effect.force;
		j["Lighting"] = effect.lighting;
		j["Alpha"] = effect.alpha;
		j["FillColor"] = SerializeColor(effect.fillColor);
		j["RimColor"] = SerializeColor(effect.rimColor);
		j["BaseFillScale"] = effect.baseFillScale;
		j["BaseFillAlpha"] = effect.baseFillAlpha;
		j["BaseRimAlpha"] = effect.baseRimAlpha;
		j["EdgeExponent"] = effect.edgeExponent;
		j["AlphaMultiplier"] = effect.alphaMultiplier;
		j["BaseTexturePath"] = effect.baseTexturePath;
		j["PaletteTexturePath"] = effect.paletteTexturePath;
		j["BlockOutTexturePath"] = effect.blockOutTexturePath;
		return j;
	}

	static void ParseModelAnimation(const json& j, ModelAnimationConfig& out) {
		if (!j.is_object()) return;
		out.playSequence = j.value("PlaySequence", out.playSequence);
		out.forwardAnimationEvents = j.value("ForwardAnimationEvents", out.forwardAnimationEvents);
		out.attachSubGraphs = j.value("AttachSubGraphs", out.attachSubGraphs);
		out.disableBehaviorGraphAnims = j.value("DisableBehaviorGraphAnims", out.disableBehaviorGraphAnims);
		out.sequenceName = j.value("SequenceName", out.sequenceName);
		out.animationEvent = j.value("AnimationEvent", out.animationEvent);
	}

	static json SerializeModelAnimation(const ModelAnimationConfig& animation) {
		json j;
		j["PlaySequence"] = animation.playSequence;
		j["ForwardAnimationEvents"] = animation.forwardAnimationEvents;
		j["AttachSubGraphs"] = animation.attachSubGraphs;
		j["DisableBehaviorGraphAnims"] = animation.disableBehaviorGraphAnims;
		j["SequenceName"] = animation.sequenceName;
		j["AnimationEvent"] = animation.animationEvent;
		return j;
	}

	static void ParseModelGroupEntry(const json& j, ModelGroupEntry& out, const std::string& fallbackName) {
		out.name = j.value("Name", fallbackName.empty() ? "ModelGroup" : fallbackName);
		out.isEnabled = j.value("Enabled", true);
		out.sourceMode = j.value("SourceMode", j.value("UseFormModel", false) ? 1 : 0);
		out.modelPath = j.value("ModelPath", "");
		if (j.contains("SourceFormID")) out.sourceFormID = ParseFormIDValue(j["SourceFormID"], out.sourceFormID);
		out.extractMagazine = j.value("ExtractMagazine", out.extractMagazine);
		out.useProjectileForAmmo = j.value("UseProjectileForAmmo", out.useProjectileForAmmo);
		out.targetNode = j.value("TargetNode", "");
		out.hideWithWeapon = j.value("HideWithWeapon", true);
		out.continueAfterMatch = j.value("ContinueAfterMatch", true);
		out.loadOnlyWhenVisible = j.value("LoadOnlyWhenVisible", false);
		out.disableHavok = j.value("DisableHavok", out.disableHavok);
		out.removeEditorMarker = j.value("RemoveEditorMarker", out.removeEditorMarker);
		out.removeProjectileTracers = j.value("RemoveProjectileTracers", out.removeProjectileTracers);
		out.invisible = j.value("Invisible", out.invisible);
		out.hideGeometry = j.value("HideGeometry", out.hideGeometry);
		out.hideLight = j.value("HideLight", out.hideLight);
		out.load1pWeaponModel = j.value("Load1pWeaponModel", out.load1pWeaponModel);
		out.keepTorchFlame = j.value("KeepTorchFlame", out.keepTorchFlame);
		out.removeScabbard = j.value("RemoveScabbard", out.removeScabbard);
		if (j.contains("DisplayConditionTree")) ParseConditionNode(j["DisplayConditionTree"], out.displayConditionTree);
		if (j.contains("m")) ParseTransform(j["m"], out.transforms.m);
		if (j.contains("f")) ParseTransform(j["f"], out.transforms.f); else out.transforms.f = out.transforms.m;
		out.overrideGeometryTransform = j.value("OverrideGeometryTransform", out.overrideGeometryTransform);
		if (j.contains("Geometry_m")) ParseTransform(j["Geometry_m"], out.geometryTransforms.m);
		if (j.contains("Geometry_f")) ParseTransform(j["Geometry_f"], out.geometryTransforms.f); else out.geometryTransforms.f = out.geometryTransforms.m;
		if (j.contains("Light")) ParseModelLight(j["Light"], out.light);
		if (j.contains("EffectShader")) ParseModelEffectShader(j["EffectShader"], out.effectShader);
		if (j.contains("Animation")) ParseModelAnimation(j["Animation"], out.animation);
	}

	static json SerializeModelGroupEntry(const ModelGroupEntry& group) {
		json j;
		j["Name"] = group.name;
		j["Enabled"] = group.isEnabled;
		j["SourceMode"] = group.sourceMode;
		j["ModelPath"] = group.modelPath;
		j["SourceFormID"] = group.sourceFormID;
		j["ExtractMagazine"] = group.extractMagazine;
		j["UseProjectileForAmmo"] = group.useProjectileForAmmo;
		j["TargetNode"] = group.targetNode;
		j["HideWithWeapon"] = group.hideWithWeapon;
		j["ContinueAfterMatch"] = group.continueAfterMatch;
		j["LoadOnlyWhenVisible"] = group.loadOnlyWhenVisible;
		j["DisableHavok"] = group.disableHavok;
		j["RemoveEditorMarker"] = group.removeEditorMarker;
		j["RemoveProjectileTracers"] = group.removeProjectileTracers;
		j["Invisible"] = group.invisible;
		j["HideGeometry"] = group.hideGeometry;
		j["HideLight"] = group.hideLight;
		j["Load1pWeaponModel"] = group.load1pWeaponModel;
		j["KeepTorchFlame"] = group.keepTorchFlame;
		j["RemoveScabbard"] = group.removeScabbard;
		j["DisplayConditionTree"] = SerializeConditionNode(group.displayConditionTree);
		j["m"] = SerializeTransform(group.transforms.m);
		j["f"] = SerializeTransform(group.transforms.f);
		j["OverrideGeometryTransform"] = group.overrideGeometryTransform;
		j["Geometry_m"] = SerializeTransform(group.geometryTransforms.m);
		j["Geometry_f"] = SerializeTransform(group.geometryTransforms.f);
		j["Light"] = SerializeModelLight(group.light);
		j["EffectShader"] = SerializeModelEffectShader(group.effectShader);
		j["Animation"] = SerializeModelAnimation(group.animation);
		return j;
	}

	void ParseJsonToCustomList(const json& data, std::vector<CustomDefinition>& customList, const std::string& path) {
		if (!data.contains("Entries")) return;
		for (auto it = data["Entries"].begin(); it != data["Entries"].end(); ++it) {
			CustomDefinition cust; cust.customName = it.key(); cust.originPath = path;
			const auto& j = it.value();
			if (j.contains("TargetFormID")) {
				cust.targetFormID = ParseFormIDValue(j["TargetFormID"], 0);
			}
			cust.isEnabled = j.value("Enabled", true);
			cust.priority = j.value("Priority", 0); cust.targetNode = j.value("TargetNode", "");
			cust.targetDisplaySlot = j.value("TargetDisplaySlot", "");
			cust.useRuntimeTargetForm = j.value("UseRuntimeTargetForm", false);
			cust.runtimeTargetFormVariable = j.value("RuntimeTargetFormVariable", "");
			cust.displayFormWithoutInventory = j.value("DisplayFormWithoutInventory", false);
			cust.ignorePlayer = j.value("IgnorePlayer", false);

			cust.alwaysUnload = j.value("AlwaysUnload", false);
			cust.extractMagazine = j.value("ExtractMagazine", false);
			cust.overrideEquipmentMode = j.value("OverrideEquipmentMode", false);
			cust.displayFavoritesOnly = j.value("DisplayFavoritesOnly", true);
			cust.spawnChance = j.value("SpawnChance", 100.0f);
			cust.lastEquippedMode = j.value("LastEquippedMode", false);
			cust.lastEquippedPrioritizeRecentBipedSlots = j.value("LastEquippedPrioritizeRecentBipedSlots", true);
			cust.lastEquippedSkipOccupiedBipedSlots = j.value("LastEquippedSkipOccupiedBipedSlots", false);
			cust.lastEquippedDisableIfBipedSlotOccupied = j.value("LastEquippedDisableIfBipedSlotOccupied", true);
			cust.lastEquippedPrioritizeRecentDisplaySlot = j.value("LastEquippedPrioritizeRecentDisplaySlot", true);
			cust.lastEquippedSkipOccupiedDisplaySlots = j.value("LastEquippedSkipOccupiedDisplaySlots", false);
			cust.lastEquippedDisableIfDisplaySlotOccupied = j.value("LastEquippedDisableIfDisplaySlotOccupied", false);
			cust.lastEquippedFallbackToSlotted = j.value("LastEquippedFallbackToSlotted", false);
			cust.lastEquippedFallbackToAnySlot = j.value("LastEquippedFallbackToAnySlot", true);
			cust.lastEquippedFallbackToRecentAcquired = j.value("LastEquippedFallbackToRecentAcquired", false);
			cust.lastEquippedPrioritizeRecentAcquiredTypes = j.value("LastEquippedPrioritizeRecentAcquiredTypes", true);
			if (j.contains("LastEquippedBipedSlots")) {
				cust.lastEquippedBipedSlots = ParseBipedSlotVector(j["LastEquippedBipedSlots"]);
			}
			if (j.contains("LastEquippedDisplaySlots")) {
				cust.lastEquippedDisplaySlots = ParseStringVector(j["LastEquippedDisplaySlots"]);
			}
			if (j.contains("LastEquippedRecentAcquiredFormTypes") && j["LastEquippedRecentAcquiredFormTypes"].is_array()) {
				for (const auto& typeEntry : j["LastEquippedRecentAcquiredFormTypes"]) {
					auto type = typeEntry.is_number() ? typeEntry.get<std::uint8_t>() : ConfigManager::StringToFormType(typeEntry.get<std::string>());
					if (type != 0 && std::find(cust.lastEquippedRecentAcquiredFormTypes.begin(), cust.lastEquippedRecentAcquiredFormTypes.end(), type) == cust.lastEquippedRecentAcquiredFormTypes.end()) {
						cust.lastEquippedRecentAcquiredFormTypes.push_back(type);
					}
				}
			}
			if (j.contains("LastEquippedFilterConditionTree")) ParseConditionNode(j["LastEquippedFilterConditionTree"], cust.lastEquippedFilterConditionTree);
			cust.disableIfEquipped = j.value("DisableIfEquipped", false);
			cust.selectInventoryRandom = j.value("SelectInventoryRandom", false);
			cust.selectInventoryStrongest = j.value("SelectInventoryStrongest", false);
			// Strongest and random are mutually exclusive.  Prefer strongest when a
			// manually edited config contains both flags.
			if (cust.selectInventoryStrongest) cust.selectInventoryRandom = false;
			if (j.contains("InventoryConditionTree")) ParseConditionNode(j["InventoryConditionTree"], cust.inventoryConditionTree);
			cust.countMin = j.value("CountMin", 1);
			cust.countMax = j.value("CountMax", 0);
			if (j.contains("ExtraItems") && j["ExtraItems"].is_array()) {
				for (auto& item : j["ExtraItems"]) {
					auto formID = ParseFormIDValue(item, 0);
					if (formID != 0) {
						cust.extraItems.push_back(formID);
					}
				}
			}
			cust.modelSwapPath = j.value("ModelSwapPath", "");
			if (j.contains("ModelSwapFormID")) {
				cust.modelSwapFormID = ParseFormIDValue(j["ModelSwapFormID"], 0);
			}
			ParseModelSwapVariableSource(j, cust.modelSwapVariableSource);

			ParseConfigBase(j, cust);

			cust.hideIfUsingFurniture = j.value("HideIfUsingFurniture", false);
			cust.hideLayingDown = j.value("HideLayingDown", false);
			cust.useWorldModel = j.value("UseWorldModel", false);
			cust.invisible = j.value("Invisible", false);
			cust.hideGeometry = j.value("HideGeometry", false);
			cust.hideLight = j.value("HideLight", false);
			cust.load1pWeaponModel = j.value("Load1pWeaponModel", false);
			cust.keepTorchFlame = j.value("KeepTorchFlame", false);
			cust.removeScabbard = j.value("RemoveScabbard", false);
			cust.useProjectileForAmmo = j.value("UseProjectileForAmmo", false);
			cust.disableHavok = j.value("DisableHavok", cust.disableHavok);
			cust.removeEditorMarker = j.value("RemoveEditorMarker", cust.removeEditorMarker);
			cust.removeProjectileTracers = j.value("RemoveProjectileTracers", cust.removeProjectileTracers);
			if (j.contains("Animation")) ParseModelAnimation(j["Animation"], cust.animation);
			if (j.contains("Light")) ParseModelLight(j["Light"], cust.light);
			if (j.contains("EffectShader")) ParseModelEffectShader(j["EffectShader"], cust.effectShader);
			cust.groupMode = j.value("GroupMode", false);

			cust.holsterModelPath = j.value("HolsterModelPath", "");
			cust.keepHolsterWhenDrawn = j.value("KeepHolsterWhenDrawn", true);
			if (j.contains("Holster_m")) ParseTransform(j["Holster_m"], cust.holsterTransforms.m);
			if (j.contains("Holster_f")) ParseTransform(j["Holster_f"], cust.holsterTransforms.f); else cust.holsterTransforms.f = cust.holsterTransforms.m;

			if (j.contains("ModelGroups")) {
				if (j["ModelGroups"].is_array()) {
					for (const auto& gJ : j["ModelGroups"]) {
						ModelGroupEntry group;
						ParseModelGroupEntry(gJ, group, "");
						if (!group.name.empty()) cust.modelGroups.push_back(group);
					}
				}
				else if (j["ModelGroups"].is_object()) {
					for (auto gIt = j["ModelGroups"].begin(); gIt != j["ModelGroups"].end(); ++gIt) {
						ModelGroupEntry group;
						ParseModelGroupEntry(gIt.value(), group, gIt.key());
						if (!group.name.empty()) cust.modelGroups.push_back(group);
					}
				}
			}

			customList.push_back(cust);
		}
	}

	json SerializeCustom(const CustomDefinition& cust) {
		json j; j["TargetFormID"] = cust.targetFormID; j["Enabled"] = cust.isEnabled; j["Priority"] = cust.priority; j["TargetNode"] = cust.targetNode; j["TargetDisplaySlot"] = cust.targetDisplaySlot;
		j["UseRuntimeTargetForm"] = cust.useRuntimeTargetForm;
		j["RuntimeTargetFormVariable"] = cust.runtimeTargetFormVariable;
		j["DisplayFormWithoutInventory"] = cust.displayFormWithoutInventory;
		j["IgnorePlayer"] = cust.ignorePlayer;

		j["AlwaysUnload"] = cust.alwaysUnload;
		j["ExtractMagazine"] = cust.extractMagazine;
		j["OverrideEquipmentMode"] = cust.overrideEquipmentMode;
		j["DisplayFavoritesOnly"] = cust.displayFavoritesOnly;
		j["SpawnChance"] = cust.spawnChance;
		j["LastEquippedMode"] = cust.lastEquippedMode;
		j["LastEquippedPrioritizeRecentBipedSlots"] = cust.lastEquippedPrioritizeRecentBipedSlots;
		j["LastEquippedSkipOccupiedBipedSlots"] = cust.lastEquippedSkipOccupiedBipedSlots;
		j["LastEquippedDisableIfBipedSlotOccupied"] = cust.lastEquippedDisableIfBipedSlotOccupied;
		j["LastEquippedPrioritizeRecentDisplaySlot"] = cust.lastEquippedPrioritizeRecentDisplaySlot;
		j["LastEquippedSkipOccupiedDisplaySlots"] = cust.lastEquippedSkipOccupiedDisplaySlots;
		j["LastEquippedDisableIfDisplaySlotOccupied"] = cust.lastEquippedDisableIfDisplaySlotOccupied;
		j["LastEquippedFallbackToSlotted"] = cust.lastEquippedFallbackToSlotted;
		j["LastEquippedFallbackToAnySlot"] = cust.lastEquippedFallbackToAnySlot;
		j["LastEquippedFallbackToRecentAcquired"] = cust.lastEquippedFallbackToRecentAcquired;
		j["LastEquippedPrioritizeRecentAcquiredTypes"] = cust.lastEquippedPrioritizeRecentAcquiredTypes;
		j["LastEquippedBipedSlots"] = SerializeUInt32Vector(cust.lastEquippedBipedSlots);
		j["LastEquippedDisplaySlots"] = SerializeStringVector(cust.lastEquippedDisplaySlots);
		j["LastEquippedRecentAcquiredFormTypes"] = json::array();
		for (auto type : cust.lastEquippedRecentAcquiredFormTypes) {
			j["LastEquippedRecentAcquiredFormTypes"].push_back(ConfigManager::FormTypeToString(type));
		}
		j["LastEquippedFilterConditionTree"] = SerializeConditionNode(cust.lastEquippedFilterConditionTree);
		j["DisableIfEquipped"] = cust.disableIfEquipped;
		j["SelectInventoryRandom"] = cust.selectInventoryRandom;
		j["SelectInventoryStrongest"] = cust.selectInventoryStrongest;
		j["InventoryConditionTree"] = SerializeConditionNode(cust.inventoryConditionTree);
		j["CountMin"] = cust.countMin;
		j["CountMax"] = cust.countMax;
		j["ExtraItems"] = cust.extraItems;
		j["ModelSwapPath"] = cust.modelSwapPath;
		j["ModelSwapFormID"] = cust.modelSwapFormID;
		SerializeModelSwapVariableSource(cust.modelSwapVariableSource, j);

		SerializeConfigBase(cust, j);

		j["HideIfUsingFurniture"] = cust.hideIfUsingFurniture;
		j["HideLayingDown"] = cust.hideLayingDown;
		j["UseWorldModel"] = cust.useWorldModel;
		j["Invisible"] = cust.invisible;
		j["HideGeometry"] = cust.hideGeometry;
		j["HideLight"] = cust.hideLight;
		j["Load1pWeaponModel"] = cust.load1pWeaponModel;
		j["KeepTorchFlame"] = cust.keepTorchFlame;
		j["RemoveScabbard"] = cust.removeScabbard;
		j["UseProjectileForAmmo"] = cust.useProjectileForAmmo;
		j["DisableHavok"] = cust.disableHavok;
		j["RemoveEditorMarker"] = cust.removeEditorMarker;
		j["RemoveProjectileTracers"] = cust.removeProjectileTracers;
		j["Animation"] = SerializeModelAnimation(cust.animation);
		j["Light"] = SerializeModelLight(cust.light);
		j["EffectShader"] = SerializeModelEffectShader(cust.effectShader);
		j["GroupMode"] = cust.groupMode;

		j["HolsterModelPath"] = cust.holsterModelPath;
		j["KeepHolsterWhenDrawn"] = cust.keepHolsterWhenDrawn;
		j["Holster_m"] = SerializeTransform(cust.holsterTransforms.m);
		j["Holster_f"] = SerializeTransform(cust.holsterTransforms.f);
		j["ModelGroups"] = json::array();
		for (const auto& group : cust.modelGroups) {
			j["ModelGroups"].push_back(SerializeModelGroupEntry(group));
		}

		return j;
	}

	void ConfigManager::LoadConfig() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		_configDir = "Data/F4SE/Plugins/ImmersiveArsenalDisplays";

		fs::create_directories(_configDir + "/Profiles/Slot");
		fs::create_directories(_configDir + "/Profiles/NodeOverrides");
		fs::create_directories(_configDir + "/Profiles/Custom");
		fs::create_directories(_configDir + "/Profiles/ModelGroups");
		fs::create_directories(_configDir + "/Profiles/NodeMonitors");
		fs::create_directories(_configDir + "/Profiles/ConditionalVariables");
		fs::create_directories(_configDir + "/Profiles/Conditions");
		fs::create_directories(_configDir + "/Profiles/Transforms");
		fs::create_directories(_configDir + "/Profiles/Physics");
		fs::create_directories(_configDir + "/Profiles/FormFilters");
		fs::create_directories(_configDir + "/Exports");
		fs::create_directories(_configDir + "/SkeletonExtensions/ExtraGearNodes");
		fs::create_directories(_configDir + "/SkeletonExtensions/ConvertNodes");
		fs::create_directories(_configDir + "/SkeletonExtensions/ConvertNodes2");

		auto resetActiveConfigState = [&]() {
			_slots.clear();
			_nodes.clear();
			_customs.clear();
			// RuntimeSelection belongs to ActiveConfig. Reset every ActiveConfig-owned
			// value before each candidate so a failed candidate cannot leak partial
			// state into the next fallback file.
			prioritizeEquippedCandidates = true;
			useRecentDisplaySlotMemory = true;
			reserveEquippedForPositivePrioritySlots = true;
			prioritizeRecentAcquired = true;
			enableEquipmentPhysics = true;
			enableModelEffects = true;
			enableModelLights = true;
			enableNPCDisplays = true;
			blockPlayerDisplays = false;
			blockedActorFormIDs.clear();
			npcEvaluationIntervalTicks = 4;
			recentAcquiredFormTypes = {
				static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kWEAP),
				static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kARMO),
				static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kAMMO),
				static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kALCH),
				static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kMISC)
			};
			runtimeVariables.clear();
			runtimeNumberVariables.clear();
			runtimeModelPathVariables.clear();
			runtimeFormVariables.clear();
			keyBindDefinitions.clear();
			conditionalVariables.clear();
			nodeMonitorUseFilter = false;
			nodeMonitorNames.clear();
		};
		resetActiveConfigState();

		std::string activeConfigPath = _configDir + "/ActiveConfig.json";
		std::vector<std::string> configCandidates = {
			activeConfigPath,
			_configDir + "/DefaultConfig.json",
			_configDir + "/Exports/IAD_DefaultConfigUser.json"
		};
		std::string loadedConfigPath;
		bool migratedLegacySlotSelectionMode = false;

		for (const auto& configPath : configCandidates) {
			if (!fs::exists(configPath)) {
				continue;
			}

			resetActiveConfigState();
			try {
				std::ifstream f(configPath);
				json j = json::parse(f);

				// Persist the ordinary-slot selection mode upgrade once. Keeping this
				// transformation at the config document boundary means the runtime and
				// exported active config always converge on one canonical format.
				auto migrateLegacySlotSelectionModes = [&](json& data) {
					if (!data.is_object() || !data.contains("Slots") || !data["Slots"].is_object()) return;
					for (auto& [scopeName, scopeData] : data["Slots"].items()) {
						if (!scopeData.is_object()) continue;
						for (auto& [targetID, entries] : scopeData.items()) {
							if (!entries.is_object()) continue;
							for (auto& [slotName, slot] : entries.items()) {
								if (!slot.is_object() || slot.contains("SelectionMode") || !slot.contains("SelectInventoryStrongest")) continue;
								slot["SelectionMode"] = slot.value("SelectInventoryStrongest", false) ?
									static_cast<std::uint32_t>(SlotSelectionMode::kStrongest) :
									static_cast<std::uint32_t>(SlotSelectionMode::kLastEquipped);
								slot.erase("SelectInventoryStrongest");
								migratedLegacySlotSelectionMode = true;
							}
						}
					}
				};
				if (j.contains("Data")) migrateLegacySlotSelectionModes(j["Data"]);

				if (j.contains("RuntimeSelection") && j["RuntimeSelection"].is_object()) {
					auto& rs = j["RuntimeSelection"];
					prioritizeEquippedCandidates = rs.value("PrioritizeEquippedCandidates", prioritizeEquippedCandidates);
					useRecentDisplaySlotMemory = rs.value("UseRecentDisplaySlotMemory", useRecentDisplaySlotMemory);
					reserveEquippedForPositivePrioritySlots = rs.value("ReserveEquippedForPositivePrioritySlots", reserveEquippedForPositivePrioritySlots);
					prioritizeRecentAcquired = rs.value("PrioritizeRecentAcquired", prioritizeRecentAcquired);
					enableEquipmentPhysics = rs.value("EnableEquipmentPhysics", enableEquipmentPhysics);
					enableModelEffects = rs.value("EnableModelEffects", enableModelEffects);
					enableModelLights = rs.value("EnableModelLights", enableModelLights);
					enableNPCDisplays = rs.value("EnableNPCDisplays", enableNPCDisplays);
					blockPlayerDisplays = rs.value("BlockPlayerDisplays", blockPlayerDisplays);
					if (rs.contains("BlockedActorFormIDs") && rs["BlockedActorFormIDs"].is_array()) {
						blockedActorFormIDs.clear();
						for (const auto& formEntry : rs["BlockedActorFormIDs"]) {
							const auto formID = ParseFormIDValue(formEntry, 0);
							if (formID != 0) blockedActorFormIDs.emplace(formID);
						}
					}
					npcEvaluationIntervalTicks = std::clamp(
						rs.value("NPCEvaluationIntervalTicks", npcEvaluationIntervalTicks),
						1u,
						60u);
					if (rs.contains("RecentAcquiredFormTypes") && rs["RecentAcquiredFormTypes"].is_array()) {
						recentAcquiredFormTypes.clear();
						for (const auto& typeEntry : rs["RecentAcquiredFormTypes"]) {
							std::uint8_t type = 0;
							if (typeEntry.is_string()) {
								type = StringToFormType(typeEntry.get<std::string>());
							}
							else if (typeEntry.is_number_unsigned()) {
								type = static_cast<std::uint8_t>(typeEntry.get<std::uint32_t>());
							}
							if (type != 0 && std::find(recentAcquiredFormTypes.begin(), recentAcquiredFormTypes.end(), type) == recentAcquiredFormTypes.end()) {
								recentAcquiredFormTypes.push_back(type);
							}
						}
					}
					if (rs.contains("Variables") && rs["Variables"].is_object()) {
						runtimeVariables.clear();
						for (auto it = rs["Variables"].begin(); it != rs["Variables"].end(); ++it) {
							if (!it.key().empty() && it.value().is_boolean()) {
								runtimeVariables[it.key()] = it.value().get<bool>();
							}
						}
					}
					if (rs.contains("NumberVariables") && rs["NumberVariables"].is_object()) {
						runtimeNumberVariables.clear();
						for (auto it = rs["NumberVariables"].begin(); it != rs["NumberVariables"].end(); ++it) {
							if (!it.key().empty() && it.value().is_number()) {
								runtimeNumberVariables[it.key()] = it.value().get<float>();
							}
						}
					}
					if (rs.contains("ModelPathVariables") && rs["ModelPathVariables"].is_object()) {
						runtimeModelPathVariables.clear();
						for (auto it = rs["ModelPathVariables"].begin(); it != rs["ModelPathVariables"].end(); ++it) {
							if (!it.key().empty() && it.value().is_string()) {
								runtimeModelPathVariables[it.key()] = it.value().get<std::string>();
							}
						}
					}
					if (rs.contains("FormVariables") && rs["FormVariables"].is_object()) {
						runtimeFormVariables.clear();
						for (auto it = rs["FormVariables"].begin(); it != rs["FormVariables"].end(); ++it) {
							if (!it.key().empty()) {
								const auto formID = ParseFormIDValue(it.value(), 0);
								runtimeFormVariables[it.key()] = formID;
							}
						}
					}
					if (rs.contains("Keybinds") && rs["Keybinds"].is_object()) {
						for (auto it = rs["Keybinds"].begin(); it != rs["Keybinds"].end(); ++it) {
							if (it.key().empty() || !it.value().is_object()) continue;
							KeyBindDefinition definition;
							definition.key = it.value().value("Key", 0u);
							definition.comboKey = it.value().value("ComboKey", 0u);
							definition.numStates = std::clamp(it.value().value("NumStates", 1u), 1u, 32u);
							if (definition.key != 0 && definition.key <= 0xFF) {
								definition.comboKey = definition.comboKey <= 0xFF ? definition.comboKey : 0;
								keyBindDefinitions.emplace(it.key(), definition);
							}
						}
					}
					if (rs.contains("ConditionalVariables")) {
						ParseConditionalVariables(rs["ConditionalVariables"], conditionalVariables);
					}
				}

				if (j.contains("Debug") && j["Debug"].is_object()) {
					auto& dbg = j["Debug"];
					if (dbg.contains("NodeMonitor") && dbg["NodeMonitor"].is_object()) {
						auto& nm = dbg["NodeMonitor"];
						nodeMonitorUseFilter = nm.value("UseFilter", nodeMonitorUseFilter);
						if (nm.contains("Names") && nm["Names"].is_array()) {
							nodeMonitorNames.clear();
							for (const auto& nameEntry : nm["Names"]) {
								if (nameEntry.is_string()) {
									auto name = nameEntry.get<std::string>();
									if (!name.empty() && std::find(nodeMonitorNames.begin(), nodeMonitorNames.end(), name) == nodeMonitorNames.end()) {
										nodeMonitorNames.push_back(name);
									}
								}
							}
						}
					}
				}

				if (j.contains("Data")) {
					auto& d = j["Data"];

					auto handleMap = [&](const json& sourceMap, auto& targetScopeMap, auto parseFunc) {
						if (!sourceMap.is_object()) return;
						for (auto it = sourceMap.begin(); it != sourceMap.end(); ++it) {
							uint32_t id = std::stoul(it.key(), nullptr, 16);
							json dummy; dummy["Entries"] = it.value();
							std::vector<std::remove_reference_t<decltype(targetScopeMap[id][0])>> temp;
							parseFunc(dummy, temp, configPath);
							targetScopeMap[id] = std::move(temp);
						}
						};

					if (d.contains("Slots")) {
						if (d["Slots"].contains("Global")) handleMap(d["Slots"]["Global"], _slots[ConfigScope::kGlobal], ParseJsonToSlotList);
						if (d["Slots"].contains("Actor")) handleMap(d["Slots"]["Actor"], _slots[ConfigScope::kActor], ParseJsonToSlotList);
						if (d["Slots"].contains("NPC")) handleMap(d["Slots"]["NPC"], _slots[ConfigScope::kNPC], ParseJsonToSlotList);
						if (d["Slots"].contains("Race")) handleMap(d["Slots"]["Race"], _slots[ConfigScope::kRace], ParseJsonToSlotList);
					}
					if (d.contains("Nodes")) {
						if (d["Nodes"].contains("Global")) handleMap(d["Nodes"]["Global"], _nodes[ConfigScope::kGlobal], ParseJsonToNodeList);
						if (d["Nodes"].contains("Actor")) handleMap(d["Nodes"]["Actor"], _nodes[ConfigScope::kActor], ParseJsonToNodeList);
						if (d["Nodes"].contains("NPC")) handleMap(d["Nodes"]["NPC"], _nodes[ConfigScope::kNPC], ParseJsonToNodeList);
						if (d["Nodes"].contains("Race")) handleMap(d["Nodes"]["Race"], _nodes[ConfigScope::kRace], ParseJsonToNodeList);
					}
					if (d.contains("Customs")) {
						if (d["Customs"].contains("Global")) handleMap(d["Customs"]["Global"], _customs[ConfigScope::kGlobal], ParseJsonToCustomList);
						if (d["Customs"].contains("Actor")) handleMap(d["Customs"]["Actor"], _customs[ConfigScope::kActor], ParseJsonToCustomList);
						if (d["Customs"].contains("NPC")) handleMap(d["Customs"]["NPC"], _customs[ConfigScope::kNPC], ParseJsonToCustomList);
						if (d["Customs"].contains("Race")) handleMap(d["Customs"]["Race"], _customs[ConfigScope::kRace], ParseJsonToCustomList);
					}
				}
				loadedConfigPath = configPath;
				break;
			}
			catch (const std::exception& e) {
				REX::WARN("[IAD] Failed to load config '{}': {}", configPath, e.what());
				resetActiveConfigState();
			}
			catch (...) {
				REX::WARN("[IAD] Failed to load config '{}': unknown error", configPath);
				resetActiveConfigState();
			}
		}

		for (auto& [scope, mapData] : _slots) {
			for (auto& [id, list] : mapData) {
				std::sort(list.begin(), list.end(), [](const SlotDefinition& a, const SlotDefinition& b) { return a.priority > b.priority; });
			}
		}

		if (!loadedConfigPath.empty() && (loadedConfigPath != activeConfigPath || migratedLegacySlotSelectionMode)) {
			if (loadedConfigPath != activeConfigPath) {
				REX::INFO("[IAD] Seeded ActiveConfig.json from '{}'", loadedConfigPath);
			}
			else {
				REX::INFO("[IAD] Migrated legacy SelectInventoryStrongest fields to SelectionMode");
			}
			SaveConfig();
		}
	}

	void ConfigManager::SaveConfig() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);

		json jMaster;
		jMaster["Version"] = 1;
		jMaster["Type"] = "IAD_ActiveState";
		jMaster["RuntimeSelection"]["PrioritizeEquippedCandidates"] = prioritizeEquippedCandidates;
		jMaster["RuntimeSelection"]["UseRecentDisplaySlotMemory"] = useRecentDisplaySlotMemory;
		jMaster["RuntimeSelection"]["ReserveEquippedForPositivePrioritySlots"] = reserveEquippedForPositivePrioritySlots;
		jMaster["RuntimeSelection"]["PrioritizeRecentAcquired"] = prioritizeRecentAcquired;
		jMaster["RuntimeSelection"]["EnableEquipmentPhysics"] = enableEquipmentPhysics;
		jMaster["RuntimeSelection"]["EnableModelEffects"] = enableModelEffects;
		jMaster["RuntimeSelection"]["EnableModelLights"] = enableModelLights;
		jMaster["RuntimeSelection"]["EnableNPCDisplays"] = enableNPCDisplays;
		jMaster["RuntimeSelection"]["BlockPlayerDisplays"] = blockPlayerDisplays;
		jMaster["RuntimeSelection"]["BlockedActorFormIDs"] = json::array();
		for (const auto formID : blockedActorFormIDs) {
			jMaster["RuntimeSelection"]["BlockedActorFormIDs"].push_back(formID);
		}
		jMaster["RuntimeSelection"]["NPCEvaluationIntervalTicks"] = npcEvaluationIntervalTicks;
		jMaster["RuntimeSelection"]["RecentAcquiredFormTypes"] = json::array();
		for (auto type : recentAcquiredFormTypes) {
			jMaster["RuntimeSelection"]["RecentAcquiredFormTypes"].push_back(FormTypeToString(type));
		}
		jMaster["RuntimeSelection"]["Variables"] = json::object();
		for (const auto& [name, value] : runtimeVariables) {
			jMaster["RuntimeSelection"]["Variables"][name] = value;
		}
		jMaster["RuntimeSelection"]["NumberVariables"] = json::object();
		for (const auto& [name, value] : runtimeNumberVariables) {
			jMaster["RuntimeSelection"]["NumberVariables"][name] = value;
		}
		jMaster["RuntimeSelection"]["ModelPathVariables"] = json::object();
		for (const auto& [name, value] : runtimeModelPathVariables) {
			jMaster["RuntimeSelection"]["ModelPathVariables"][name] = value;
		}
		jMaster["RuntimeSelection"]["FormVariables"] = json::object();
		for (const auto& [name, value] : runtimeFormVariables) {
			jMaster["RuntimeSelection"]["FormVariables"][name] = value;
		}
		jMaster["RuntimeSelection"]["Keybinds"] = json::object();
		for (const auto& [name, definition] : keyBindDefinitions) {
			jMaster["RuntimeSelection"]["Keybinds"][name] = {
				{ "Key", definition.key },
				{ "ComboKey", definition.comboKey },
				{ "NumStates", definition.numStates }
			};
		}
		jMaster["RuntimeSelection"]["ConditionalVariables"] = SerializeConditionalVariables(conditionalVariables);
		jMaster["Debug"]["NodeMonitor"]["UseFilter"] = nodeMonitorUseFilter;
		jMaster["Debug"]["NodeMonitor"]["Names"] = json::array();
		for (const auto& name : nodeMonitorNames) {
			if (!name.empty()) {
				jMaster["Debug"]["NodeMonitor"]["Names"].push_back(name);
			}
		}

		auto serializeMapSlot = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.slotName] = SerializeSlot(item); } return out; };
		auto serializeMapNode = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.nodeName] = SerializeNode(item); } return out; };
		auto serializeMapCustom = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.customName] = SerializeCustom(item); } return out; };

		jMaster["Data"]["Slots"]["Global"] = serializeMapSlot(_slots[ConfigScope::kGlobal]);
		jMaster["Data"]["Slots"]["Actor"] = serializeMapSlot(_slots[ConfigScope::kActor]);
		jMaster["Data"]["Slots"]["NPC"] = serializeMapSlot(_slots[ConfigScope::kNPC]);
		jMaster["Data"]["Slots"]["Race"] = serializeMapSlot(_slots[ConfigScope::kRace]);

		jMaster["Data"]["Nodes"]["Global"] = serializeMapNode(_nodes[ConfigScope::kGlobal]);
		jMaster["Data"]["Nodes"]["Actor"] = serializeMapNode(_nodes[ConfigScope::kActor]);
		jMaster["Data"]["Nodes"]["NPC"] = serializeMapNode(_nodes[ConfigScope::kNPC]);
		jMaster["Data"]["Nodes"]["Race"] = serializeMapNode(_nodes[ConfigScope::kRace]);

		jMaster["Data"]["Customs"]["Global"] = serializeMapCustom(_customs[ConfigScope::kGlobal]);
		jMaster["Data"]["Customs"]["Actor"] = serializeMapCustom(_customs[ConfigScope::kActor]);
		jMaster["Data"]["Customs"]["NPC"] = serializeMapCustom(_customs[ConfigScope::kNPC]);
		jMaster["Data"]["Customs"]["Race"] = serializeMapCustom(_customs[ConfigScope::kRace]);

		std::string activeConfigPath = _configDir + "/ActiveConfig.json";
		const std::string tempConfigPath = activeConfigPath + ".tmp";
		{
			std::ofstream f(tempConfigPath, std::ios::binary | std::ios::trunc);
			if (!f.is_open()) {
				REX::WARN("[IAD] Failed to open temporary config '{}' for writing", tempConfigPath);
				return;
			}
			f << jMaster.dump(4);
			f.flush();
			if (!f.good()) {
				REX::WARN("[IAD] Failed while writing temporary config '{}'", tempConfigPath);
				f.close();
				DeleteFileA(tempConfigPath.c_str());
				return;
			}
		}

		if (!MoveFileExA(tempConfigPath.c_str(), activeConfigPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			REX::WARN("[IAD] Failed to atomically replace '{}' with temporary config (Win32 error {})", activeConfigPath, GetLastError());
			DeleteFileA(tempConfigPath.c_str());
		}
	}

	template<typename T, typename GetNameFunc>
	std::vector<ScopedData<T>> ResolveConfigWithFallback(
		RE::Actor* a_actor,
		std::map<ConfigScope, std::map<uint32_t, std::vector<T>>>& dataStore,
		GetNameFunc getName)
	{
		std::vector<ScopedData<T>> result;
		if (!a_actor) return result;

		std::lock_guard<std::recursive_mutex> lock(ConfigManager::GetSingleton()->_configMutex);

		uint32_t actorID = a_actor->GetFormID();
		uint32_t npcID = a_actor->data.objectReference ? a_actor->data.objectReference->GetFormID() : 0;
		uint32_t raceID = a_actor->race ? a_actor->race->GetFormID() : 0;
		uint32_t globalID = a_actor->IsPlayerRef() ? 1 : 2;

		std::set<std::string> keys;
		auto addKeys = [&](ConfigScope scope, uint32_t id) {
			if (dataStore[scope].count(id)) {
				for (auto& item : dataStore[scope][id]) keys.insert(getName(item));
			}
			};
		addKeys(ConfigScope::kGlobal, globalID);
		addKeys(ConfigScope::kGlobal, 0);
		addKeys(ConfigScope::kRace, raceID);
		addKeys(ConfigScope::kNPC, npcID);
		addKeys(ConfigScope::kActor, actorID);

		for (const auto& key : keys) {
			bool found = false;

			if (dataStore[ConfigScope::kActor].count(actorID)) {
				for (auto& item : dataStore[ConfigScope::kActor][actorID]) {
					if (getName(item) == key) { result.push_back({ ConfigScope::kActor, item }); found = true; break; }
				}
			}
			if (found) continue;

			if (dataStore[ConfigScope::kNPC].count(npcID)) {
				for (auto& item : dataStore[ConfigScope::kNPC][npcID]) {
					if (getName(item) == key) { result.push_back({ ConfigScope::kNPC, item }); found = true; break; }
				}
			}
			if (found) continue;

			if (dataStore[ConfigScope::kRace].count(raceID)) {
				for (auto& item : dataStore[ConfigScope::kRace][raceID]) {
					if (getName(item) == key) { result.push_back({ ConfigScope::kRace, item }); found = true; break; }
				}
			}
			if (found) continue;

			if (dataStore[ConfigScope::kGlobal].count(globalID)) {
				for (auto& item : dataStore[ConfigScope::kGlobal][globalID]) {
					if (getName(item) == key) { result.push_back({ ConfigScope::kGlobal, item }); found = true; break; }
				}
			}
			if (found) continue;

			if (dataStore[ConfigScope::kGlobal].count(0)) {
				for (auto& item : dataStore[ConfigScope::kGlobal][0]) {
					if (getName(item) == key) { result.push_back({ ConfigScope::kGlobal, item }); break; }
				}
			}
		}

		return result;
	}

	std::vector<ScopedData<CustomDefinition>> ConfigManager::ResolveCustomsWithScope(RE::Actor* a_actor) {
		return ResolveConfigWithFallback(a_actor, _customs, [](const CustomDefinition& c) { return c.customName; });
	}

	std::vector<ScopedData<SlotDefinition>> ConfigManager::ResolveSlotsWithScope(RE::Actor* a_actor) {
		return ResolveConfigWithFallback(a_actor, _slots, [](const SlotDefinition& s) { return s.slotName; });
	}

	std::vector<ScopedData<NodeDefinition>> ConfigManager::ResolveNodesWithScope(RE::Actor* a_actor) {
		return ResolveConfigWithFallback(a_actor, _nodes, [](const NodeDefinition& n) { return n.nodeName; });
	}

	void ConfigManager::ExportPreset(const std::string& a_presetName, uint32_t a_flags) {
		if (!IsSafeConfigFileName(a_presetName)) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex); std::string path = _configDir + "/Exports/" + a_presetName + ".json";
		json j;
		j["ProfileName"] = a_presetName;
		j["Type"] = "GlobalSnapshot";
		j["format"] = "IAD.GlobalSnapshot";
		j["version"] = 2;
		j["flags"] = a_flags;
		auto serializeMapSlot = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.slotName] = SerializeSlot(item); } return out; };
		auto serializeMapNode = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.nodeName] = SerializeNode(item); } return out; };
		auto serializeMapCustom = [](const auto& mapData) { json out = json::object(); for (const auto& [id, list] : mapData) { char buf[16]; sprintf_s(buf, "%08X", id); out[buf] = json::object(); for (const auto& item : list) out[buf][item.customName] = SerializeCustom(item); } return out; };
		if (a_flags & (uint32_t)SerFlags::kSlotGlobal) j["Data"]["Slots"]["Global"] = serializeMapSlot(_slots[ConfigScope::kGlobal]);
		if (a_flags & (uint32_t)SerFlags::kSlotActor) j["Data"]["Slots"]["Actor"] = serializeMapSlot(_slots[ConfigScope::kActor]);
		if (a_flags & (uint32_t)SerFlags::kSlotNPC) j["Data"]["Slots"]["NPC"] = serializeMapSlot(_slots[ConfigScope::kNPC]);
		if (a_flags & (uint32_t)SerFlags::kSlotRace) j["Data"]["Slots"]["Race"] = serializeMapSlot(_slots[ConfigScope::kRace]);
		if (a_flags & (uint32_t)SerFlags::kNodeGlobal) j["Data"]["Nodes"]["Global"] = serializeMapNode(_nodes[ConfigScope::kGlobal]);
		if (a_flags & (uint32_t)SerFlags::kNodeActor) j["Data"]["Nodes"]["Actor"] = serializeMapNode(_nodes[ConfigScope::kActor]);
		if (a_flags & (uint32_t)SerFlags::kNodeNPC) j["Data"]["Nodes"]["NPC"] = serializeMapNode(_nodes[ConfigScope::kNPC]);
		if (a_flags & (uint32_t)SerFlags::kNodeRace) j["Data"]["Nodes"]["Race"] = serializeMapNode(_nodes[ConfigScope::kRace]);
		if (a_flags & (uint32_t)SerFlags::kCustomGlobal) j["Data"]["Customs"]["Global"] = serializeMapCustom(_customs[ConfigScope::kGlobal]);
		if (a_flags & (uint32_t)SerFlags::kCustomActor) j["Data"]["Customs"]["Actor"] = serializeMapCustom(_customs[ConfigScope::kActor]);
		if (a_flags & (uint32_t)SerFlags::kCustomNPC) j["Data"]["Customs"]["NPC"] = serializeMapCustom(_customs[ConfigScope::kNPC]);
		if (a_flags & (uint32_t)SerFlags::kCustomRace) j["Data"]["Customs"]["Race"] = serializeMapCustom(_customs[ConfigScope::kRace]);
		if (a_flags & (uint32_t)SerFlags::kFormFilters) {
			json ffJson = json::object();
			std::string ffDir = _configDir + "/Profiles/FormFilters";
			if (fs::exists(ffDir)) {
				for (const auto& entry : fs::directory_iterator(ffDir)) {
					if (entry.path().extension() == ".json") {
						try {
							std::ifstream fIn(entry.path());
							ffJson[entry.path().stem().string()] = json::parse(fIn);
						}
						catch (...) {}
					}
				}
			}
			j["Data"]["FormFilters"] = ffJson;
		}
		std::ofstream f(path); if (f.is_open()) f << j.dump(4);
	}

	void ConfigManager::ImportPreset(const std::string& a_presetName, uint32_t a_flags, bool a_merge) {
		if (!IsSafeConfigFileName(a_presetName)) return;
		std::lock_guard<std::recursive_mutex> lock(_configMutex); std::string path = _configDir + "/Exports/" + a_presetName + ".json"; if (!fs::exists(path)) return;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			// Version 1 snapshots did not carry a format envelope.  The payload layout
			// is deliberately unchanged, so both legacy and version 2 files import here.
			if (j.contains("format") && j["format"].is_string() && j["format"] != "IAD.GlobalSnapshot") return;
			if (!j.contains("Data")) return;
			auto& d = j["Data"];
			auto handleMap = [&](const json& sourceMap, auto& targetScopeMap, auto parseFunc, const std::string& destPathPrefix, auto getName) {
				if (!sourceMap.is_object()) return; if (!a_merge) targetScopeMap.clear();
				for (auto it = sourceMap.begin(); it != sourceMap.end(); ++it) {
					uint32_t id = std::stoul(it.key(), nullptr, 16); json dummy; dummy["Entries"] = it.value();
					std::string destPath = _configDir + destPathPrefix + std::to_string(id) + ".json";
					std::vector<std::remove_reference_t<decltype(targetScopeMap[id][0])>> temp; parseFunc(dummy, temp, destPath);
					if (!a_merge) targetScopeMap[id] = std::move(temp);
					else { for (auto& newItem : temp) { auto existingIt = std::find_if(targetScopeMap[id].begin(), targetScopeMap[id].end(), [&](const auto& existingItem) { return getName(existingItem) == getName(newItem); }); if (existingIt != targetScopeMap[id].end()) *existingIt = std::move(newItem); else targetScopeMap[id].push_back(std::move(newItem)); } }
				}
				};
			auto getSlotName = [](const auto& s) { return s.slotName; }; auto getNodeName = [](const auto& n) { return n.nodeName; }; auto getCustomName = [](const auto& c) { return c.customName; };
			if (d.contains("Slots")) {
				if ((a_flags & (uint32_t)SerFlags::kSlotGlobal) && d["Slots"].contains("Global")) handleMap(d["Slots"]["Global"], _slots[ConfigScope::kGlobal], ParseJsonToSlotList, "/Configs/Global/", getSlotName);
				if ((a_flags & (uint32_t)SerFlags::kSlotActor) && d["Slots"].contains("Actor")) handleMap(d["Slots"]["Actor"], _slots[ConfigScope::kActor], ParseJsonToSlotList, "/Configs/Actor/", getSlotName);
				if ((a_flags & (uint32_t)SerFlags::kSlotNPC) && d["Slots"].contains("NPC")) handleMap(d["Slots"]["NPC"], _slots[ConfigScope::kNPC], ParseJsonToSlotList, "/Configs/NPC/", getSlotName);
				if ((a_flags & (uint32_t)SerFlags::kSlotRace) && d["Slots"].contains("Race")) handleMap(d["Slots"]["Race"], _slots[ConfigScope::kRace], ParseJsonToSlotList, "/Configs/Race/", getSlotName);
			}
			if (d.contains("Nodes")) {
				if ((a_flags & (uint32_t)SerFlags::kNodeGlobal) && d["Nodes"].contains("Global")) handleMap(d["Nodes"]["Global"], _nodes[ConfigScope::kGlobal], ParseJsonToNodeList, "/Configs/Global/", getNodeName);
				if ((a_flags & (uint32_t)SerFlags::kNodeActor) && d["Nodes"].contains("Actor")) handleMap(d["Nodes"]["Actor"], _nodes[ConfigScope::kActor], ParseJsonToNodeList, "/Configs/Actor/", getNodeName);
				if ((a_flags & (uint32_t)SerFlags::kNodeNPC) && d["Nodes"].contains("NPC")) handleMap(d["Nodes"]["NPC"], _nodes[ConfigScope::kNPC], ParseJsonToNodeList, "/Configs/NPC/", getNodeName);
				if ((a_flags & (uint32_t)SerFlags::kNodeRace) && d["Nodes"].contains("Race")) handleMap(d["Nodes"]["Race"], _nodes[ConfigScope::kRace], ParseJsonToNodeList, "/Configs/Race/", getNodeName);
			}
			if (d.contains("Customs")) {
				if ((a_flags & (uint32_t)SerFlags::kCustomGlobal) && d["Customs"].contains("Global")) handleMap(d["Customs"]["Global"], _customs[ConfigScope::kGlobal], ParseJsonToCustomList, "/Configs/Global/", getCustomName);
				if ((a_flags & (uint32_t)SerFlags::kCustomActor) && d["Customs"].contains("Actor")) handleMap(d["Customs"]["Actor"], _customs[ConfigScope::kActor], ParseJsonToCustomList, "/Configs/Actor/", getCustomName);
				if ((a_flags & (uint32_t)SerFlags::kCustomNPC) && d["Customs"].contains("NPC")) handleMap(d["Customs"]["NPC"], _customs[ConfigScope::kNPC], ParseJsonToCustomList, "/Configs/NPC/", getCustomName);
				if ((a_flags & (uint32_t)SerFlags::kCustomRace) && d["Customs"].contains("Race")) handleMap(d["Customs"]["Race"], _customs[ConfigScope::kRace], ParseJsonToCustomList, "/Configs/Race/", getCustomName);
			}
			if (d.contains("FormFilters") && (a_flags & (uint32_t)SerFlags::kFormFilters)) {
				auto& ffData = d["FormFilters"];
				if (ffData.is_object()) {
					std::string ffDir = _configDir + "/Profiles/FormFilters/";
					if (!fs::exists(ffDir)) fs::create_directories(ffDir);

					for (auto it = ffData.begin(); it != ffData.end(); ++it) {
						std::string savePath = ffDir + it.key() + ".json";
						std::ofstream fOut(savePath);
						if (fOut.is_open()) fOut << it.value().dump(4);
					}
				}
			}
			SaveConfig();
		}
		catch (...) {}
	}

	std::vector<std::string> ConfigManager::GetAvailableExports() {
		std::vector<std::string> results; std::string dir = _configDir + "/Exports"; if (!fs::exists(dir)) return results;
		for (const auto& entry : fs::directory_iterator(dir)) { if (entry.path().extension() == ".json") results.push_back(entry.path().stem().string()); } return results;
	}

	bool ConfigManager::RenameExport(const std::string& oldName, const std::string& newName) {
		if (!IsSafeConfigFileName(oldName) || !IsSafeConfigFileName(newName)) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		fs::path oldPath = fs::path(_configDir) / "Exports" / (oldName + ".json");
		fs::path newPath = fs::path(_configDir) / "Exports" / (newName + ".json");
		if (!fs::exists(oldPath) || fs::exists(newPath)) return false;
		try {
			fs::rename(oldPath, newPath);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool ConfigManager::DeleteExport(const std::string& profileName) {
		if (!IsSafeConfigFileName(profileName)) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		fs::path path = fs::path(_configDir) / "Exports" / (profileName + ".json");
		if (!fs::exists(path)) return false;
		try {
			return fs::remove(path);
		}
		catch (...) {
			return false;
		}
	}

	void ConfigManager::SaveSlotProfile(const std::string& profileName, const std::vector<SlotDefinition>& data) {
		std::string path = _configDir + "/Profiles/Slot/" + profileName + ".json"; json j; j["Entries"] = json::object();
		for (const auto& s : data) j["Entries"][s.slotName] = SerializeSlot(s);
		std::ofstream f(path); if (f.is_open()) f << j.dump(4);
	}
	bool ConfigManager::LoadSlotProfile(const std::string& profileName, std::vector<SlotDefinition>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/Slot/" + profileName + ".json"; if (!fs::exists(path)) return false;
		try { std::ifstream f(path); json j = json::parse(f); std::vector<SlotDefinition> temp; ParseJsonToSlotList(j, temp, ""); if (overwrite) outData.clear(); for (auto& newObj : temp) { auto it = std::find_if(outData.begin(), outData.end(), [&](const SlotDefinition& s) { return s.slotName == newObj.slotName; }); if (it != outData.end()) *it = newObj; else outData.push_back(newObj); } return true; }
		catch (...) {} return false;
	}

	void ConfigManager::SaveNodeProfile(const std::string& profileName, const std::vector<NodeDefinition>& data) {
		std::string path = _configDir + "/Profiles/NodeOverrides/" + profileName + ".json"; json j; j["Entries"] = json::object();
		for (const auto& n : data) j["Entries"][n.nodeName] = SerializeNode(n);
		std::ofstream f(path); if (f.is_open()) f << j.dump(4);
	}
	bool ConfigManager::LoadNodeProfile(const std::string& profileName, std::vector<NodeDefinition>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/NodeOverrides/" + profileName + ".json"; if (!fs::exists(path)) return false;
		try { std::ifstream f(path); json j = json::parse(f); std::vector<NodeDefinition> temp; ParseJsonToNodeList(j, temp, ""); if (overwrite) outData.clear(); for (auto& newObj : temp) { auto it = std::find_if(outData.begin(), outData.end(), [&](const NodeDefinition& n) { return n.nodeName == newObj.nodeName; }); if (it != outData.end()) *it = newObj; else outData.push_back(newObj); } return true; }
		catch (...) {} return false;
	}

	void ConfigManager::SaveNodeConversionProfile(const std::string& profileName, const std::vector<NodeDefinition>& data, bool version2) {
		std::string folder = version2 ? "/SkeletonExtensions/ConvertNodes2/" : "/SkeletonExtensions/ConvertNodes/";
		std::string path = _configDir + folder + profileName + ".json";
		json j;
		j["Type"] = version2 ? "IAD_ConvertNodes2" : "IAD_ConvertNodes";
		j["Entries"] = json::object();
		for (const auto& n : data) j["Entries"][n.nodeName] = SerializeNode(n);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadNodeConversionProfile(const std::string& profileName, std::vector<NodeDefinition>& outData, bool overwrite, bool version2) {
		std::string folder = version2 ? "/SkeletonExtensions/ConvertNodes2/" : "/SkeletonExtensions/ConvertNodes/";
		std::string path = _configDir + folder + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			std::vector<NodeDefinition> temp;

			if (j.contains("Entries")) {
				if (j["Entries"].is_object()) {
					ParseJsonToNodeList(j, temp, path);
				}
				else if (j["Entries"].is_array()) {
					json normalized;
					normalized["Entries"] = json::object();
					for (const auto& entry : j["Entries"]) {
						if (!entry.is_object()) continue;
						auto name = entry.value("Name", entry.value("NodeName", ""));
						if (name.empty()) continue;
						if (name.find("IAD_CME_") != 0) name = "IAD_CME_" + name;
						normalized["Entries"][name] = entry;
					}
					ParseJsonToNodeList(normalized, temp, path);
				}
			}
			else if (j.is_object()) {
				json normalized;
				normalized["Entries"] = json::object();
				for (auto it = j.begin(); it != j.end(); ++it) {
					std::string nodeName = it.key();
					if (nodeName == "Type" || nodeName == "Version") continue;
					if (nodeName.find("IAD_CME_") != 0) nodeName = "IAD_CME_" + nodeName;
					if (it.value().is_array()) {
						normalized["Entries"][nodeName]["FallbackHosts"] = it.value();
					}
					else {
						normalized["Entries"][nodeName] = it.value();
					}
				}
				ParseJsonToNodeList(normalized, temp, path);
			}

			if (overwrite) outData.clear();
			for (auto& newObj : temp) {
				auto it = std::find_if(outData.begin(), outData.end(), [&](const NodeDefinition& n) { return n.nodeName == newObj.nodeName; });
				if (it != outData.end()) *it = newObj;
				else outData.push_back(newObj);
			}
			return true;
		}
		catch (...) {}
		return false;
	}

	std::vector<std::string> ConfigManager::GetAvailableNodeConversionProfiles(bool version2) {
		std::vector<std::string> results;
		std::string folder = version2 ? "/SkeletonExtensions/ConvertNodes2" : "/SkeletonExtensions/ConvertNodes";
		std::string dir = _configDir + folder;
		if (!fs::exists(dir)) return results;
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (entry.path().extension() == ".json") results.push_back(entry.path().stem().string());
		}
		return results;
	}

	void ConfigManager::SaveCustomProfile(const std::string& profileName, const std::vector<CustomDefinition>& data) {
		std::string path = _configDir + "/Profiles/Custom/" + profileName + ".json"; json j; j["Entries"] = json::object();
		for (const auto& c : data) j["Entries"][c.customName] = SerializeCustom(c);
		std::ofstream f(path); if (f.is_open()) f << j.dump(4);
	}
	bool ConfigManager::LoadCustomProfile(const std::string& profileName, std::vector<CustomDefinition>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/Custom/" + profileName + ".json"; if (!fs::exists(path)) return false;
		try { std::ifstream f(path); json j = json::parse(f); std::vector<CustomDefinition> temp; ParseJsonToCustomList(j, temp, ""); if (overwrite) outData.clear(); for (auto& newObj : temp) { auto it = std::find_if(outData.begin(), outData.end(), [&](const CustomDefinition& c) { return c.customName == newObj.customName; }); if (it != outData.end()) *it = newObj; else outData.push_back(newObj); } return true; }
		catch (...) {} return false;
	}

	void ConfigManager::SaveModelGroupProfile(const std::string& profileName, const std::vector<ModelGroupEntry>& data) {
		std::string path = _configDir + "/Profiles/ModelGroups/" + profileName + ".json"; json j; j["Entries"] = json::array();
		for (const auto& group : data) j["Entries"].push_back(SerializeModelGroupEntry(group));
		std::ofstream f(path); if (f.is_open()) f << j.dump(4);
	}
	bool ConfigManager::LoadModelGroupProfile(const std::string& profileName, std::vector<ModelGroupEntry>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/ModelGroups/" + profileName + ".json"; if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path); json j = json::parse(f); std::vector<ModelGroupEntry> temp;
			if (j.contains("Entries")) {
				if (j["Entries"].is_array()) {
					for (const auto& gJ : j["Entries"]) {
						ModelGroupEntry group;
						ParseModelGroupEntry(gJ, group, "");
						if (!group.name.empty()) temp.push_back(group);
					}
				}
				else if (j["Entries"].is_object()) {
					for (auto it = j["Entries"].begin(); it != j["Entries"].end(); ++it) {
						ModelGroupEntry group;
						ParseModelGroupEntry(it.value(), group, it.key());
						if (!group.name.empty()) temp.push_back(group);
					}
				}
			}
			if (overwrite) outData.clear();
			for (auto& newObj : temp) {
				auto it = std::find_if(outData.begin(), outData.end(), [&](const ModelGroupEntry& group) { return group.name == newObj.name; });
				if (it != outData.end()) *it = newObj;
				else outData.push_back(newObj);
			}
			return true;
		}
		catch (...) {} return false;
	}

	void ConfigManager::SaveNodeMonitorProfile(const std::string& profileName, const std::vector<std::string>& data) {
		std::string path = _configDir + "/Profiles/NodeMonitors/" + profileName + ".json";
		json j;
		j["Entries"] = json::array();
		for (const auto& name : data) {
			if (!name.empty()) {
				j["Entries"].push_back(name);
			}
		}
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadNodeMonitorProfile(const std::string& profileName, std::vector<std::string>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/NodeMonitors/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			std::vector<std::string> temp;
			if (j.contains("Entries") && j["Entries"].is_array()) {
				for (const auto& nameEntry : j["Entries"]) {
					if (nameEntry.is_string()) {
						auto name = nameEntry.get<std::string>();
						if (!name.empty() && std::find(temp.begin(), temp.end(), name) == temp.end()) {
							temp.push_back(name);
						}
					}
				}
			}
			if (overwrite) outData.clear();
			for (const auto& name : temp) {
				if (std::find(outData.begin(), outData.end(), name) == outData.end()) {
					outData.push_back(name);
				}
			}
			return true;
		}
		catch (...) {} return false;
	}

	void ConfigManager::SaveConditionalVariableProfile(const std::string& profileName, const std::vector<ConditionalVariableDefinition>& data) {
		std::string path = _configDir + "/Profiles/ConditionalVariables/" + profileName + ".json";
		json j;
		j["ConditionalVariables"] = SerializeConditionalVariables(data);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadConditionalVariableProfile(const std::string& profileName, std::vector<ConditionalVariableDefinition>& outData, bool overwrite) {
		std::string path = _configDir + "/Profiles/ConditionalVariables/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			std::vector<ConditionalVariableDefinition> loaded;
			if (j.contains("ConditionalVariables")) ParseConditionalVariables(j["ConditionalVariables"], loaded);
			else if (j.is_array()) ParseConditionalVariables(j, loaded);
			else return false;

			if (overwrite) {
				outData = std::move(loaded);
				return true;
			}

			for (auto& variable : loaded) {
				auto existing = std::find_if(outData.begin(), outData.end(), [&](const auto& current) {
					return current.name == variable.name;
				});
				if (existing != outData.end()) *existing = std::move(variable);
				else outData.push_back(std::move(variable));
			}
			return true;
		}
		catch (...) {}
		return false;
	}

	void ConfigManager::SaveConditionProfile(const std::string& profileName, const ConditionNode& data) {
		std::string path = _configDir + "/Profiles/Conditions/" + profileName + ".json";
		json j;
		j["Root"] = SerializeConditionNode(data);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadConditionProfile(const std::string& profileName, ConditionNode& outData) {
		std::string path = _configDir + "/Profiles/Conditions/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			ConditionNode temp(true, true);
			if (j.contains("Root") && j["Root"].is_object()) {
				ParseConditionNode(j["Root"], temp);
			}
			else if (j.is_object()) {
				ParseConditionNode(j, temp);
			}
			outData = temp;
			return true;
		}
		catch (...) {} return false;
	}

	void ConfigManager::SaveTransformProfile(const std::string& profileName, const TransformData& data) {
		std::string path = _configDir + "/Profiles/Transforms/" + profileName + ".json";
		json j;
		j["Transform"] = SerializeTransform(data);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadTransformProfile(const std::string& profileName, TransformData& outData) {
		std::string path = _configDir + "/Profiles/Transforms/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			TransformData temp;
			if (j.contains("Transform") && j["Transform"].is_object()) {
				ParseTransform(j["Transform"], temp);
			}
			else if (j.is_object()) {
				ParseTransform(j, temp);
			}
			outData = temp;
			return true;
		}
		catch (...) {} return false;
	}

	void ConfigManager::SavePhysicsProfile(const std::string& profileName, const PhysicsValues& data) {
		std::string path = _configDir + "/Profiles/Physics/" + profileName + ".json";
		json j;
		j["Physics"] = SerializePhysics(data);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadPhysicsProfile(const std::string& profileName, PhysicsValues& outData) {
		std::string path = _configDir + "/Profiles/Physics/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path);
			json j = json::parse(f);
			PhysicsValues temp;
			if (j.contains("Physics") && j["Physics"].is_object()) {
				ParsePhysics(j["Physics"], temp);
			}
			else if (j.is_object()) {
				ParsePhysics(j, temp);
			}
			outData = temp;
			return true;
		}
		catch (...) {} return false;
	}

	void ConfigManager::SaveFormFilterProfile(const std::string& profileName, const FormFilter& data) {
		std::string path = _configDir + "/Profiles/FormFilters/" + profileName + ".json";
		json j = SerializeFormFilter(data);
		std::ofstream f(path);
		if (f.is_open()) f << j.dump(4);
	}

	bool ConfigManager::LoadFormFilterProfile(const std::string& profileName, FormFilter& outData) {
		std::string path = _configDir + "/Profiles/FormFilters/" + profileName + ".json";
		if (!fs::exists(path)) return false;
		try {
			std::ifstream f(path); json j = json::parse(f);
			ParseFormFilter(j, outData);
			return true;
		}
		catch (...) {} return false;
	}

	bool ConfigManager::RenameFormFilterReferences(const std::string& oldName, const std::string& newName) {
		if (oldName.empty() || newName.empty() || oldName == newName) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		bool changed = false;
		for (auto& [scope, targets] : _slots) {
			for (auto& [targetID, slots] : targets) {
				for (auto& slot : slots) {
					if (slot.itemFilter.useProfile && slot.itemFilter.profileName == oldName) {
						slot.itemFilter.profileName = newName;
						changed = true;
					}
				}
			}
		}
		return changed;
	}

	bool ConfigManager::ClearFormFilterReferences(const std::string& profileName) {
		if (profileName.empty()) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		bool changed = false;
		for (auto& [scope, targets] : _slots) {
			for (auto& [targetID, slots] : targets) {
				for (auto& slot : slots) {
					if (slot.itemFilter.useProfile && slot.itemFilter.profileName == profileName) {
						slot.itemFilter.useProfile = false;
						slot.itemFilter.profileName.clear();
						changed = true;
					}
				}
			}
		}
		return changed;
	}

	std::vector<std::string> ConfigManager::GetAvailableProfiles(const std::string& folderName) {
		std::vector<std::string> results; std::string dir = _configDir + "/Profiles/" + folderName; if (!fs::exists(dir)) return results;
		for (const auto& entry : fs::directory_iterator(dir)) { if (entry.path().extension() == ".json") results.push_back(entry.path().stem().string()); } return results;
	}

	bool ConfigManager::RenameProfile(const std::string& folderName, const std::string& oldName, const std::string& newName) {
		if (!IsSafeConfigFileName(folderName) || !IsSafeConfigFileName(oldName) || !IsSafeConfigFileName(newName)) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		fs::path oldPath = fs::path(_configDir) / "Profiles" / folderName / (oldName + ".json");
		fs::path newPath = fs::path(_configDir) / "Profiles" / folderName / (newName + ".json");
		if (!fs::exists(oldPath) || fs::exists(newPath)) return false;
		try {
			fs::rename(oldPath, newPath);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool ConfigManager::DeleteProfile(const std::string& folderName, const std::string& profileName) {
		if (!IsSafeConfigFileName(folderName) || !IsSafeConfigFileName(profileName)) return false;
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		fs::path path = fs::path(_configDir) / "Profiles" / folderName / (profileName + ".json");
		if (!fs::exists(path)) return false;
		try {
			return fs::remove(path);
		}
		catch (...) {
			return false;
		}
	}

	void ConfigManager::SetEditorHotkey(std::uint32_t a_key) {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		editorHotkey = a_key;
		SaveINISettings();
	}

	// 👇========== 🌟 进化版 INI 读写：接管 UI 布局记忆 ==========👇
	void ConfigManager::LoadINISettings() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		std::string iniPath = std::filesystem::current_path().string() + "\\Data\\F4SE\\Plugins\\ImmersiveArsenalDisplays\\ImmersiveArsenalDisplays.ini";

		editorHotkey = GetPrivateProfileIntA("GUI", "ToggleKey", 0x08, iniPath.c_str());
		editorModifier = GetPrivateProfileIntA("GUI", "ToggleModifier", 0, iniPath.c_str());
		playerBlockHotkey = GetPrivateProfileIntA("General", "PlayerBlockToggleKey", 0, iniPath.c_str());
		playerBlockModifier = GetPrivateProfileIntA("General", "PlayerBlockToggleModifier", 0, iniPath.c_str());
		displayFavoritesOnly = GetPrivateProfileIntA("General", "DisplayFavoritesOnly", 0, iniPath.c_str()) != 0;
		logLevel = std::clamp(static_cast<int>(GetPrivateProfileIntA("Debug", "LogLevel", 2, iniPath.c_str())), 0, 6);
		spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(logLevel));

		// 读取所有窗口的上次状态
		uiShowSlots = GetPrivateProfileIntA("UIState", "ShowSlots", 1, iniPath.c_str()) != 0;
		uiShowNodes = GetPrivateProfileIntA("UIState", "ShowNodes", 0, iniPath.c_str()) != 0;
		uiShowCustoms = GetPrivateProfileIntA("UIState", "ShowCustoms", 0, iniPath.c_str()) != 0;
		uiShowFilters = GetPrivateProfileIntA("UIState", "ShowFilters", 0, iniPath.c_str()) != 0;
		uiShowSettings = GetPrivateProfileIntA("UIState", "ShowSettings", 0, iniPath.c_str()) != 0;
		uiShowProfiles = GetPrivateProfileIntA("UIState", "ShowProfiles", 0, iniPath.c_str()) != 0;
		uiShowProfileSlots = GetPrivateProfileIntA("UIState", "ShowProfileSlots", 0, iniPath.c_str()) != 0;
		uiShowProfileCustoms = GetPrivateProfileIntA("UIState", "ShowProfileCustoms", 0, iniPath.c_str()) != 0;
		uiShowProfileNodes = GetPrivateProfileIntA("UIState", "ShowProfileNodes", 0, iniPath.c_str()) != 0;
		uiShowProfileFormFilters = GetPrivateProfileIntA("UIState", "ShowProfileFormFilters", 0, iniPath.c_str()) != 0;
		uiShowProfileModelGroups = GetPrivateProfileIntA("UIState", "ShowProfileModelGroups", 0, iniPath.c_str()) != 0;
		uiShowProfileNodeMonitors = GetPrivateProfileIntA("UIState", "ShowProfileNodeMonitors", 0, iniPath.c_str()) != 0;
		uiShowProfileConditions = GetPrivateProfileIntA("UIState", "ShowProfileConditions", 0, iniPath.c_str()) != 0;
		uiShowProfileTransforms = GetPrivateProfileIntA("UIState", "ShowProfileTransforms", 0, iniPath.c_str()) != 0;
		uiShowProfilePhysics = GetPrivateProfileIntA("UIState", "ShowProfilePhysics", 0, iniPath.c_str()) != 0;
		uiShowBoneScanner = GetPrivateProfileIntA("UIState", "ShowBoneScanner", 0, iniPath.c_str()) != 0;
		uiShowVisualizer = GetPrivateProfileIntA("UIState", "ShowVisualizer", 0, iniPath.c_str()) != 0;
		uiProfileManagedCategory = GetPrivateProfileIntA("UIState", "ProfileManagedCategory", 0, iniPath.c_str());
		uiLastClosedWindow = GetPrivateProfileIntA("UIState", "LastClosedWindow", 1, iniPath.c_str());

		REX::INFO("[IAD] INI 配置文件读取完毕。");
	}

	void ConfigManager::SaveINISettings() {
		std::lock_guard<std::recursive_mutex> lock(_configMutex);
		std::string dirPath = std::filesystem::current_path().string() + "\\Data\\F4SE\\Plugins\\ImmersiveArsenalDisplays";
		std::filesystem::create_directories(dirPath);
		std::string iniPath = dirPath + "\\ImmersiveArsenalDisplays.ini";

		WritePrivateProfileStringA("GUI", "ToggleKey", std::to_string(editorHotkey).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("GUI", "ToggleModifier", std::to_string(editorModifier).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("General", "PlayerBlockToggleKey", std::to_string(playerBlockHotkey).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("General", "PlayerBlockToggleModifier", std::to_string(playerBlockModifier).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("General", "DisplayFavoritesOnly", std::to_string(displayFavoritesOnly ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("Debug", "LogLevel", std::to_string(logLevel).c_str(), iniPath.c_str());

		// 写入所有窗口的当前状态
		WritePrivateProfileStringA("UIState", "ShowSlots", std::to_string(uiShowSlots ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowNodes", std::to_string(uiShowNodes ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowCustoms", std::to_string(uiShowCustoms ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowFilters", std::to_string(uiShowFilters ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowSettings", std::to_string(uiShowSettings ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfiles", std::to_string(uiShowProfiles ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileSlots", std::to_string(uiShowProfileSlots ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileCustoms", std::to_string(uiShowProfileCustoms ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileNodes", std::to_string(uiShowProfileNodes ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileFormFilters", std::to_string(uiShowProfileFormFilters ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileModelGroups", std::to_string(uiShowProfileModelGroups ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileNodeMonitors", std::to_string(uiShowProfileNodeMonitors ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileConditions", std::to_string(uiShowProfileConditions ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfileTransforms", std::to_string(uiShowProfileTransforms ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowProfilePhysics", std::to_string(uiShowProfilePhysics ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowBoneScanner", std::to_string(uiShowBoneScanner ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ShowVisualizer", std::to_string(uiShowVisualizer ? 1 : 0).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "ProfileManagedCategory", std::to_string(uiProfileManagedCategory).c_str(), iniPath.c_str());
		WritePrivateProfileStringA("UIState", "LastClosedWindow", std::to_string(uiLastClosedWindow).c_str(), iniPath.c_str());
	}
	// 👆================================================================👆
}
