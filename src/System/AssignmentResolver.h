#pragma once

#include "Engine/ConditionSystem.h"
#include "Data/ConfigManager.h"

#include <functional>
#include <map>
#include <optional>

namespace IAD {
	// IED-style assignment policy seam. The resolver owns deterministic ordering
	// and selection policy; HolsterManager supplies FO4 runtime state and keeps
	// all scene mutation on the game thread.
	enum class AssignmentSource {
		kDedicatedAmmo,
		kCustomConfiguredSlot,
		kCustomRecentSlot,
		kCustomPreferred,
		kCustomFallback,
		kPreferredItem,
		kLastEquipped,
		kStrongest,
		kRandom,
		kSlotPriority
	};

	class AssignmentResolver final {
	public:
		struct CandidateOrderingPolicy final {
			bool prioritizeEquipped = true;
			std::function<std::uint64_t(const ActiveItem&)> getRecentAcquiredScore;
		};

		static std::vector<SlotDefinition*> BuildSlotOrder(
			std::vector<ScopedData<SlotDefinition>>& a_scopedSlots);

		static void SortCandidates(
			std::vector<ActiveItem>& a_candidates,
			const CandidateOrderingPolicy& a_policy);

		static int GetSlotFormTypeRank(
			const SlotDefinition& a_slot,
			const ActiveItem* a_item);

		static std::vector<ActiveItem*> BuildSlotFallbackCandidates(
			const SlotDefinition& a_slot,
			std::vector<ActiveItem>& a_candidates);

		static std::vector<ActiveItem*> BuildSlotModeCandidates(
			const SlotDefinition& a_slot,
			const std::vector<ActiveItem*>& a_fallbackCandidates,
			std::uint32_t a_actorID);

		struct SlotEligibilityContext final {
			const SlotDefinition& slot;
			bool runtimeDisplayFavoritesOnly = true;
			bool equippedOnly = false;
			bool ignoreEquipmentEligibility = false;
			bool applyPriorityLimit = true;
			std::function<const FormFilter*(const SlotDefinition&)> resolveFormFilter;
			std::function<bool(ActiveItem*)> passesItemCondition;
			std::function<bool(ActiveItem*)> passesCannotWear;
			std::function<bool(ActiveItem*)> isBlackHole;
			std::function<bool(ActiveItem*)> isEquippedUnavailable;
		};

		static bool IsSlotCandidateEligible(
			const SlotEligibilityContext& a_context,
			ActiveItem* a_item,
			bool a_preferredOnly = false);

		struct AssignmentResult final {
			struct Entry final {
				ActiveItem* item = nullptr;
				CustomDefinition* custom = nullptr;
				bool isEquippedInstance = false;
				AssignmentSource source = AssignmentSource::kSlotPriority;
			};

			bool TryAdd(const std::string& a_slotName, Entry a_entry);
			bool Contains(const std::string& a_slotName) const;
			const Entry* Find(const std::string& a_slotName) const;
			std::size_t CountForCustom(const CustomDefinition* a_custom) const;
			const std::map<std::string, Entry>& Entries() const;

		private:
			std::map<std::string, Entry> _entries;
		};

		struct SlotAssignmentContext final {
			const SlotDefinition& slot;
			const std::vector<ActiveItem*>& fallbackCandidates;
			const std::vector<ActiveItem*>& modeCandidates;
			const SlotEligibilityContext& eligibility;
			std::function<std::uint64_t(ActiveItem*)> getRecentEquipScore;
		};

		struct SlotAssignmentDecision final {
			ActiveItem* item = nullptr;
			AssignmentSource source = AssignmentSource::kSlotPriority;
		};

		static std::optional<SlotAssignmentDecision> ResolveSlotAssignment(
			const SlotAssignmentContext& a_context);
	};
}
