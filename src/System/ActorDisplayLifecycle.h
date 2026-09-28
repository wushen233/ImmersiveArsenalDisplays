#pragma once

#include <cstdint>
#include <vector>

#include <RE/N/NiPointer.h>

namespace RE
{
	class NiAVObject;
}

namespace IAD
{
	struct HolsterSlot;

	// Owns the lifecycle policy for render-only display clones. Evaluation and
	// configuration decide what should exist; this module decides how an
	// existing clone is retired without racing Fallout 4 scene teardown.
	class ActorDisplayLifecycle final
	{
	public:
		static constexpr std::uint64_t kModelRetirementGraceTicks = 60;

		static void AbandonModelArray(
			std::vector<RE::NiPointer<RE::NiAVObject>>& a_models,
			bool a_skipSceneDetach = false,
			bool a_forceSceneDetach = false) noexcept;

		static void ClearSlotModels(
			HolsterSlot& a_slot,
			bool a_skipSceneDetach = false,
			bool a_forceSceneDetach = false) noexcept;

		static void BeginModelReplacement(
			HolsterSlot& a_slot,
			std::uint64_t a_currentTick);

		static void BeginHolsterReplacement(
			HolsterSlot& a_slot,
			std::uint64_t a_currentTick);

		static void BeginModelGroupReplacement(
			HolsterSlot& a_slot,
			std::uint64_t a_currentTick);

		static void RetireDeferredModels(
			HolsterSlot& a_slot,
			std::uint64_t a_currentTick) noexcept;
	};
}
