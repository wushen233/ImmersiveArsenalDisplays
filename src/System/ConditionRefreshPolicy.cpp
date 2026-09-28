#include "pch.h"

#include "ConditionRefreshPolicy.h"

#include "Engine/ConditionSystem.h"
#include "System/KeyBindStateManager.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/ActiveEffectList.h>
#include <RE/E/EffectItem.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESQuest.h>

#include <algorithm>
#include <unordered_set>

namespace IAD
{
	ConditionRefreshResult ConditionRefreshPolicy::Update(
		std::uint64_t a_currentTick,
		RE::Actor* a_player)
	{
		ConditionRefreshResult result;

		constexpr std::uint64_t kActiveEffectPollIntervalTicks = 16;
		if ((a_currentTick % kActiveEffectPollIntervalTicks) == 0) {
			result.watchedActiveEffectFormIDs =
				ConfigManager::GetSingleton()->GetActiveEffectConditionFormIDsSnapshot();
		}
		result.pollActiveEffects = !result.watchedActiveEffectFormIDs.empty();

		result.keyBindStateChanged = UpdateKeyBindings(a_currentTick);

		constexpr std::uint64_t kConditionalVariableUpdateIntervalTicks = 16;
		if (a_player && (a_currentTick % kConditionalVariableUpdateIntervalTicks) == 0) {
			result.conditionalVariablesChanged = UpdateConditionalVariables(a_player);
		}

		result.questStageChanged = UpdateQuestStages(a_currentTick);
		return result;
	}

	std::uint64_t ConditionRefreshPolicy::GetActiveEffectSignature(
		RE::Actor* a_actor,
		const std::vector<std::uint32_t>& a_watchedFormIDs)
	{
		if (!a_actor || a_watchedFormIDs.empty()) return 0;
		auto* effects = a_actor->GetActiveEffectList();
		if (!effects) return 0;

		struct ActiveEffectKey {
			std::uint32_t spellFormID;
			std::uint32_t effectFormID;
			std::uint32_t sourceFormID;

			auto Tie() const
			{
				return std::tie(spellFormID, effectFormID, sourceFormID);
			}
		};

		std::vector<ActiveEffectKey> activeEffects;
		activeEffects.reserve(effects->data.size());
		for (const auto& effectPtr : effects->data) {
			auto* active = effectPtr.get();
			if (!active ||
				active->flags.any(RE::ActiveEffect::Flags::kInactive) ||
				active->flags.any(RE::ActiveEffect::Flags::kRemovedEffects) ||
				active->flags.any(RE::ActiveEffect::Flags::kDispelled) ||
				active->flags.any(RE::ActiveEffect::Flags::kWornOff)) {
				continue;
			}

			const auto spellFormID = active->spell ? active->spell->GetFormID() : 0;
			const auto effectFormID = active->effect && active->effect->effectSetting ?
				active->effect->effectSetting->GetFormID() : 0;
			const auto sourceFormID = active->source ? active->source->GetFormID() : 0;
			const auto isWatched = [&](std::uint32_t a_formID) {
				return a_formID != 0 && std::binary_search(a_watchedFormIDs.begin(), a_watchedFormIDs.end(), a_formID);
			};
			if (isWatched(spellFormID) || isWatched(effectFormID) || isWatched(sourceFormID)) {
				activeEffects.push_back({ spellFormID, effectFormID, sourceFormID });
			}
		}

		std::sort(activeEffects.begin(), activeEffects.end(), [](const auto& a_lhs, const auto& a_rhs) {
			return a_lhs.Tie() < a_rhs.Tie();
		});

		std::uint64_t signature = 1469598103934665603ULL;
		auto mix = [&signature](std::uint64_t a_value) {
			signature ^= a_value;
			signature *= 1099511628211ULL;
		};

		for (const auto& effect : activeEffects) {
			mix(effect.spellFormID);
			mix(effect.effectFormID);
			mix(effect.sourceFormID);
		}
		mix(activeEffects.size());
		return signature;
	}

	bool ConditionRefreshPolicy::UpdateKeyBindings(std::uint64_t a_currentTick)
	{
		auto* config = ConfigManager::GetSingleton();
		constexpr std::uint64_t kKeyBindConfigScanIntervalTicks = 16;
		if (a_currentTick - _lastKeyBindConfigScanTick >= kKeyBindConfigScanIntervalTicks) {
			_activeKeyBindConditions = config->GetKeyBindConditionKeysSnapshot();
			_lastKeyBindConfigScanTick = a_currentTick;
			for (auto it = _keyBindStates.begin(); it != _keyBindStates.end();) {
				if (std::find(_activeKeyBindConditions.begin(), _activeKeyBindConditions.end(), it->first) == _activeKeyBindConditions.end()) {
					it = _keyBindStates.erase(it);
				}
				else {
					++it;
				}
			}
		}

		const auto keyBindDefinitions = config->GetKeyBindDefinitionsSnapshot();
		bool keyBindStateChanged = KeyBindStateManager::GetSingleton()->Update(keyBindDefinitions);
		for (const auto& key : _activeKeyBindConditions) {
			if (keyBindDefinitions.contains(key)) continue;

			const auto isDown = ConditionEvaluator::KeyBindStateMatches(key, ">0");
			auto [it, inserted] = _keyBindStates.emplace(key, isDown);
			if (!inserted && it->second != isDown) {
				it->second = isDown;
				keyBindStateChanged = true;
				REX::INFO("[IAD Condition] legacy KeyBindState '{}' changed to {}; queued refresh", key, isDown ? "down" : "up");
			}
		}
		return keyBindStateChanged;
	}

	bool ConditionRefreshPolicy::UpdateConditionalVariables(RE::Actor* a_actor)
	{
		if (!a_actor) return false;
		auto* config = ConfigManager::GetSingleton();
		const auto definitions = config->GetConditionalVariablesSnapshot();
		bool changed = false;
		bool equippedWeaponResolved = false;
		std::uint32_t equippedWeaponFormID = 0;
		auto resolveFormValue = [&](ConditionalVariableFormSource a_source, std::uint32_t a_staticValue) {
			if (a_source != ConditionalVariableFormSource::kEquippedWeapon) return a_staticValue;
			if (!equippedWeaponResolved) {
				equippedWeaponResolved = true;
				const auto items = Scanner::GetActiveItems(a_actor);
				for (const auto& item : items) {
					if (item.isEquipped && item.object && item.object->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
						equippedWeaponFormID = item.object->GetFormID();
						break;
					}
				}
			}
			return equippedWeaponFormID;
		};

		for (const auto& definition : definitions) {
			if (!definition.enabled || definition.name.empty()) continue;
			bool definitionChanged = false;

			bool booleanValue = definition.defaultBooleanValue;
			float numberValue = definition.defaultNumberValue;
			std::uint32_t formIDValue = resolveFormValue(definition.defaultFormSource, definition.defaultFormIDValue);
			std::string modelPathValue = definition.defaultModelPathValue;
			for (const auto& rule : definition.rules) {
				if (!ConditionEvaluator::EvaluateConditionTree(a_actor, rule.conditionTree)) continue;
				booleanValue = rule.booleanValue;
				numberValue = rule.numberValue;
				formIDValue = resolveFormValue(rule.formSource, rule.formIDValue);
				modelPathValue = rule.modelPathValue;
				if (!rule.continueAfterMatch) break;
			}

			switch (definition.type) {
			case ConditionalVariableType::kBoolean:
				if (config->GetRuntimeVariable(definition.name) != booleanValue) {
					config->SetRuntimeVariable(definition.name, booleanValue);
					definitionChanged = true;
					changed = true;
				}
				break;
			case ConditionalVariableType::kNumber:
				if (config->GetRuntimeNumberVariable(definition.name) != numberValue) {
					config->SetRuntimeNumberVariable(definition.name, numberValue);
					definitionChanged = true;
					changed = true;
				}
				break;
			case ConditionalVariableType::kForm:
				if (config->GetRuntimeFormVariable(definition.name) != formIDValue) {
					config->SetRuntimeFormVariable(definition.name, formIDValue);
					definitionChanged = true;
					changed = true;
				}
				break;
			case ConditionalVariableType::kModelPath:
				if (config->GetRuntimeModelPathVariable(definition.name) != modelPathValue) {
					config->SetRuntimeModelPathVariable(definition.name, modelPathValue);
					definitionChanged = true;
					changed = true;
				}
				break;
			}
			if (definitionChanged) {
				REX::INFO("[IAD ConditionalVariable] '{}' updated", definition.name);
			}
		}
		return changed;
	}

	bool ConditionRefreshPolicy::UpdateQuestStages(std::uint64_t a_currentTick)
	{
		constexpr std::uint64_t kQuestConditionScanIntervalTicks = 60;
		if (a_currentTick - _lastQuestConditionScanTick < kQuestConditionScanIntervalTicks) return false;

		auto* config = ConfigManager::GetSingleton();
		const auto questFormIDs = config->GetQuestStageConditionFormIDsSnapshot();
		bool questStageChanged = false;
		std::unordered_set<RE::TESFormID> activeQuestForms(questFormIDs.begin(), questFormIDs.end());
		for (const auto formID : questFormIDs) {
			auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(formID);
			const auto stage = quest ? quest->currentStage : static_cast<std::uint16_t>(0);
			auto [it, inserted] = _questConditionStages.emplace(formID, stage);
			if (!inserted && it->second != stage) {
				it->second = stage;
				questStageChanged = true;
				REX::INFO("[IAD Condition] quest {:08X} stage changed to {}; queued refresh", formID, stage);
			}
		}
		for (auto it = _questConditionStages.begin(); it != _questConditionStages.end();) {
			if (!activeQuestForms.contains(it->first)) it = _questConditionStages.erase(it);
			else ++it;
		}
		_lastQuestConditionScanTick = a_currentTick;
		return questStageChanged;
	}
}
