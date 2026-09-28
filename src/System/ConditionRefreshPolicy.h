#pragma once

#include "Data/ConfigManager.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace IAD
{
	struct ConditionRefreshResult {
		std::vector<std::uint32_t> watchedActiveEffectFormIDs;
		bool pollActiveEffects = false;
		bool keyBindStateChanged = false;
		bool conditionalVariablesChanged = false;
		bool questStageChanged = false;
	};

	// Samples global condition inputs and reports refresh-worthy changes. It owns
	// cadence/history only; actor selection, task submission, and scene mutation
	// remain in HolsterManager.
	class ConditionRefreshPolicy final
	{
	public:
		ConditionRefreshResult Update(std::uint64_t a_currentTick, RE::Actor* a_player);

		static std::uint64_t GetActiveEffectSignature(
			RE::Actor* a_actor,
			const std::vector<std::uint32_t>& a_watchedFormIDs);

	private:
		bool UpdateConditionalVariables(RE::Actor* a_actor);
		bool UpdateKeyBindings(std::uint64_t a_currentTick);
		bool UpdateQuestStages(std::uint64_t a_currentTick);

		std::uint64_t _lastKeyBindConfigScanTick = 0;
		std::uint64_t _lastQuestConditionScanTick = 0;
		std::vector<std::string> _activeKeyBindConditions;
		std::unordered_map<std::string, bool> _keyBindStates;
		std::unordered_map<RE::TESFormID, std::uint16_t> _questConditionStages;
	};
}
