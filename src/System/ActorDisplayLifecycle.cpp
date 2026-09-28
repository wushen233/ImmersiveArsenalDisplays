#include "pch.h"

#include "ActorDisplayLifecycle.h"

#include "HolsterManager.h"
#include "ModelManager.h"

namespace IAD
{
	namespace
	{
		using ModelArray = std::vector<RE::NiPointer<RE::NiAVObject>>;

		void QueueDeferredModels(ModelArray& a_current, ModelArray& a_deferred)
		{
			for (auto& model : a_current) {
				if (model) {
					model->SetAppCulled(true);
					a_deferred.push_back(model);
				}
			}
			a_current.clear();
		}

		void ScheduleDeferredRetirement(HolsterSlot& a_slot, std::uint64_t a_currentTick)
		{
			if (!a_slot.oldModels.empty() || !a_slot.oldHolsters.empty() || !a_slot.oldModelGroups.empty()) {
				a_slot.oldModelsRetireAfterTick = a_currentTick + ActorDisplayLifecycle::kModelRetirementGraceTicks;
			}
		}
	}

	void ActorDisplayLifecycle::AbandonModelArray(
		std::vector<RE::NiPointer<RE::NiAVObject>>& a_models,
		bool a_skipSceneDetach,
		bool a_forceSceneDetach) noexcept
	{
		// kPreLoadGame can run from a save-loading worker. Detaching a clone from
		// that thread races the engine's scene-tree teardown, so release only the
		// NiPointer while the engine owns the old tree. Normal runtime teardown
		// still detaches and culls the clone before releasing it.
		const bool skipSceneDetach = a_skipSceneDetach || ModelManager::IsGameSaving() ||
			(!a_forceSceneDetach && ModelManager::IsGameLoading());
		for (auto& model : a_models) {
			if (model && !skipSceneDetach) {
				if (model->parent) {
					model->parent->DetachChild(model.get());
				}
				model->SetAppCulled(true);
				model->local.scale = 0.0f;
			}
		}
		a_models.clear();
	}

	void ActorDisplayLifecycle::ClearSlotModels(
		HolsterSlot& a_slot,
		bool a_skipSceneDetach,
		bool a_forceSceneDetach) noexcept
	{
		AbandonModelArray(a_slot.currentModels, a_skipSceneDetach, a_forceSceneDetach);
		AbandonModelArray(a_slot.currentHolsters, a_skipSceneDetach, a_forceSceneDetach);
		AbandonModelArray(a_slot.currentModelGroups, a_skipSceneDetach, a_forceSceneDetach);
		AbandonModelArray(a_slot.oldModels, a_skipSceneDetach, a_forceSceneDetach);
		AbandonModelArray(a_slot.oldHolsters, a_skipSceneDetach, a_forceSceneDetach);
		AbandonModelArray(a_slot.oldModelGroups, a_skipSceneDetach, a_forceSceneDetach);
		a_slot.oldModelsRetireAfterTick = 0;
	}

	void ActorDisplayLifecycle::BeginModelReplacement(
		HolsterSlot& a_slot,
		std::uint64_t a_currentTick)
	{
		// Keep the previous weapon culled but attached for a short traversal
		// window. Motion-vector and scene visitors may still hold the old subtree
		// during the same weapon-switch frame; the update pass retires it later.
		for (auto& model : a_slot.currentModels) {
			if (model) {
				model->SetAppCulled(true);
				a_slot.oldModels.push_back(model);
			}
		}
		a_slot.currentModels.clear();
		ScheduleDeferredRetirement(a_slot, a_currentTick);
	}

	void ActorDisplayLifecycle::BeginHolsterReplacement(
		HolsterSlot& a_slot,
		std::uint64_t a_currentTick)
	{
		QueueDeferredModels(a_slot.currentHolsters, a_slot.oldHolsters);
		ScheduleDeferredRetirement(a_slot, a_currentTick);
	}

	void ActorDisplayLifecycle::BeginModelGroupReplacement(
		HolsterSlot& a_slot,
		std::uint64_t a_currentTick)
	{
		QueueDeferredModels(a_slot.currentModelGroups, a_slot.oldModelGroups);
		ScheduleDeferredRetirement(a_slot, a_currentTick);
	}

	void ActorDisplayLifecycle::RetireDeferredModels(
		HolsterSlot& a_slot,
		std::uint64_t a_currentTick) noexcept
	{
		const bool hasDeferredModels = !a_slot.oldModels.empty() || !a_slot.oldHolsters.empty() || !a_slot.oldModelGroups.empty();
		if (!hasDeferredModels) {
			a_slot.oldModelsRetireAfterTick = 0;
			return;
		}
		if (a_slot.oldModelsRetireAfterTick == 0 ||
			a_currentTick < a_slot.oldModelsRetireAfterTick) {
			return;
		}

		REX::TRACE(
			"[IAD Lifecycle] retiring {} deferred display clone(s)",
			a_slot.oldModels.size() + a_slot.oldHolsters.size() + a_slot.oldModelGroups.size());
		AbandonModelArray(a_slot.oldModels);
		AbandonModelArray(a_slot.oldHolsters);
		AbandonModelArray(a_slot.oldModelGroups);
		a_slot.oldModelsRetireAfterTick = 0;
	}
}
