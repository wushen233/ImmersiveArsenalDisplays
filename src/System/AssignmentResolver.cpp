#include "pch.h"

#include "AssignmentResolver.h"

#include <algorithm>

namespace IAD {
	namespace {
		bool IsPreferredItemForSlot(
			const SlotDefinition& a_slot,
			const ActiveItem* a_item)
		{
			if (!a_item || !a_item->object || a_slot.preferredItems.empty()) {
				return false;
			}

			const auto formID = a_item->object->GetFormID();
			return std::find(a_slot.preferredItems.begin(), a_slot.preferredItems.end(), formID) != a_slot.preferredItems.end();
		}

		bool PassesCommonSlotEligibility(
			const AssignmentResolver::SlotEligibilityContext& a_context,
			ActiveItem* a_item)
		{
			if (!a_item || !a_item->object ||
				(a_item->count == 0 && !a_context.slot.extractMagazine)) {
				return false;
			}

			if (a_context.equippedOnly &&
				(!a_item->isEquipped ||
					(a_context.isEquippedUnavailable && a_context.isEquippedUnavailable(a_item)))) {
				return false;
			}

			if (a_context.slot.checkCannotWear &&
				(!a_context.passesCannotWear || !a_context.passesCannotWear(a_item))) {
				return false;
			}

			bool requireFavoriteOrEquipped = a_context.runtimeDisplayFavoritesOnly;
			if (a_context.slot.overrideEquipmentMode) {
				requireFavoriteOrEquipped = a_context.slot.displayFavoritesOnly;
			}

			if (!a_context.ignoreEquipmentEligibility && requireFavoriteOrEquipped &&
				!a_item->isEquipped && !a_item->isFavorited) {
				return false;
			}

			return true;
		}

		bool PassesSlotEligibility(
			const AssignmentResolver::SlotEligibilityContext& a_context,
			ActiveItem* a_item,
			bool a_preferredOnly)
		{
			if (!PassesCommonSlotEligibility(a_context, a_item)) {
				return false;
			}

			if (!a_preferredOnly && a_context.applyPriorityLimit &&
				!a_context.slot.formTypePriority.empty() &&
				a_context.slot.formTypePriorityLimit > 0 &&
				!(a_context.slot.formTypePriorityAccountForEquipped && a_item->isEquipped) &&
				AssignmentResolver::GetSlotFormTypeRank(a_context.slot, a_item) >= a_context.slot.formTypePriorityLimit) {
				return false;
			}

			const auto* effectiveFormFilter = a_context.resolveFormFilter ?
				a_context.resolveFormFilter(a_context.slot) : nullptr;
			if (!effectiveFormFilter) {
				return false;
			}

			const bool hasBaseFilters = a_context.slot.advancedFilters.useBaseFilters;
			const bool hasKeywordFilters = a_context.slot.keywordMode != KeywordFilterMode::kNone;
			const bool hasFormList = !effectiveFormFilter->allowList.empty() ||
				!effectiveFormFilter->denyList.empty() || effectiveFormFilter->denyAll;
			const bool hasFormTypes = !a_context.slot.allowedFormTypes.empty();
			const bool hasPreferredItems = !a_context.slot.preferredItems.empty();
			const bool hasCandidateConditions = ConditionEvaluator::HasConditionRules(a_context.slot.itemFilterConditionTree);
			if (!hasBaseFilters && !hasKeywordFilters && !hasFormList && !hasFormTypes &&
				!hasPreferredItems && !hasCandidateConditions) {
				return false;
			}

			// Preferred items retain IED's explicit override semantics: they bypass
			// category, keyword, and condition filtering, but still respect the
			// form filter's deny policy and the common equipment checks above.
			if (a_preferredOnly || IsPreferredItemForSlot(a_context.slot, a_item)) {
				return !effectiveFormFilter->denyAll &&
					effectiveFormFilter->denyList.find(a_item->object->GetFormID()) == effectiveFormFilter->denyList.end();
			}

			if (!a_context.slot.allowedFormTypes.empty()) {
				const auto itemType = static_cast<std::uint8_t>(a_item->object->GetFormType());
				const auto typeIt = std::find(a_context.slot.allowedFormTypes.begin(), a_context.slot.allowedFormTypes.end(), itemType);
				if (typeIt == a_context.slot.allowedFormTypes.end()) {
					return false;
				}
			}

			if (!ConditionEvaluator::PassesFormFilter(a_item->object->GetFormID(), *effectiveFormFilter) ||
				!ConditionEvaluator::PassesLegacyFilters(
					a_item->object,
					a_context.slot.advancedFilters,
					a_context.slot.keywordMode,
					a_context.slot.keywordGroups)) {
				return false;
			}

			return !a_context.passesItemCondition || a_context.passesItemCondition(a_item);
		}
	}

	bool AssignmentResolver::IsSlotCandidateEligible(
		const SlotEligibilityContext& a_context,
		ActiveItem* a_item,
		bool a_preferredOnly)
	{
		return PassesSlotEligibility(a_context, a_item, a_preferredOnly);
	}

	bool AssignmentResolver::AssignmentResult::TryAdd(
		const std::string& a_slotName,
		Entry a_entry)
	{
		if (a_slotName.empty() || !a_entry.item || Contains(a_slotName)) {
			return false;
		}
		_entries.emplace(a_slotName, std::move(a_entry));
		return true;
	}

	bool AssignmentResolver::AssignmentResult::Contains(const std::string& a_slotName) const
	{
		return _entries.find(a_slotName) != _entries.end();
	}

	const AssignmentResolver::AssignmentResult::Entry* AssignmentResolver::AssignmentResult::Find(
		const std::string& a_slotName) const
	{
		if (const auto it = _entries.find(a_slotName); it != _entries.end()) {
			return &it->second;
		}
		return nullptr;
	}

	std::size_t AssignmentResolver::AssignmentResult::CountForCustom(
		const CustomDefinition* a_custom) const
	{
		std::size_t count = 0;
		for (const auto& [slotName, entry] : _entries) {
			if (entry.custom == a_custom) {
				++count;
			}
		}
		return count;
	}

	const std::map<std::string, AssignmentResolver::AssignmentResult::Entry>&
		AssignmentResolver::AssignmentResult::Entries() const
	{
		return _entries;
	}

	std::vector<SlotDefinition*> AssignmentResolver::BuildSlotOrder(
		std::vector<ScopedData<SlotDefinition>>& a_scopedSlots)
	{
		std::vector<SlotDefinition*> result;
		result.reserve(a_scopedSlots.size());
		for (auto& scopedSlot : a_scopedSlots) {
			result.push_back(&scopedSlot.data);
		}

		std::stable_sort(result.begin(), result.end(), [](const SlotDefinition* a_lhs, const SlotDefinition* a_rhs) {
			if (a_lhs->priority != a_rhs->priority) return a_lhs->priority > a_rhs->priority;
			return a_lhs->slotName < a_rhs->slotName;
		});
		return result;
	}

	void AssignmentResolver::SortCandidates(
		std::vector<ActiveItem>& a_candidates,
		const CandidateOrderingPolicy& a_policy)
	{
		std::stable_sort(a_candidates.begin(), a_candidates.end(), [&](const ActiveItem& a_lhs, const ActiveItem& a_rhs) {
			if (a_policy.prioritizeEquipped && a_lhs.isEquipped != a_rhs.isEquipped) {
				return a_lhs.isEquipped > a_rhs.isEquipped;
			}
			if (a_lhs.isFavorited != a_rhs.isFavorited) {
				return a_lhs.isFavorited > a_rhs.isFavorited;
			}

			const auto lhsAcquired = a_policy.getRecentAcquiredScore ?
				a_policy.getRecentAcquiredScore(a_lhs) : 0;
			const auto rhsAcquired = a_policy.getRecentAcquiredScore ?
				a_policy.getRecentAcquiredScore(a_rhs) : 0;
			if (lhsAcquired != rhsAcquired) {
				return lhsAcquired > rhsAcquired;
			}

			// Display history is resolved by the caller. UID is the deterministic
			// fallback when no history distinguishes the two instances.
			return a_lhs.uid > a_rhs.uid;
		});
	}

	int AssignmentResolver::GetSlotFormTypeRank(
		const SlotDefinition& a_slot,
		const ActiveItem* a_item)
	{
		if (a_slot.formTypePriority.empty() || !a_item || !a_item->object) {
			return 0;
		}

		const auto formType = static_cast<std::uint8_t>(a_item->object->GetFormType());
		auto it = std::find(a_slot.formTypePriority.begin(), a_slot.formTypePriority.end(), formType);
		if (it == a_slot.formTypePriority.end()) {
			return static_cast<int>(a_slot.formTypePriority.size());
		}
		return static_cast<int>(std::distance(a_slot.formTypePriority.begin(), it));
	}

	std::vector<ActiveItem*> AssignmentResolver::BuildSlotFallbackCandidates(
		const SlotDefinition& a_slot,
		std::vector<ActiveItem>& a_candidates)
	{
		std::vector<ActiveItem*> result;
		result.reserve(a_candidates.size());
		for (auto& candidate : a_candidates) {
			result.push_back(&candidate);
		}

		if (!a_slot.formTypePriority.empty()) {
			std::stable_sort(result.begin(), result.end(), [&](const ActiveItem* a_lhs, const ActiveItem* a_rhs) {
				if (!a_lhs || !a_rhs) {
					return a_lhs != nullptr;
				}
				if (a_slot.formTypePriorityAccountForEquipped && a_lhs->isEquipped != a_rhs->isEquipped) {
					return a_lhs->isEquipped > a_rhs->isEquipped;
				}
				return GetSlotFormTypeRank(a_slot, a_lhs) < GetSlotFormTypeRank(a_slot, a_rhs);
			});
		}
		return result;
	}

	std::vector<ActiveItem*> AssignmentResolver::BuildSlotModeCandidates(
		const SlotDefinition& a_slot,
		const std::vector<ActiveItem*>& a_fallbackCandidates,
		std::uint32_t a_actorID)
	{
		auto result = a_fallbackCandidates;
		if (a_slot.selectionMode == SlotSelectionMode::kStrongest) {
			std::stable_sort(result.begin(), result.end(), [](const ActiveItem* a_lhs, const ActiveItem* a_rhs) {
				if (!a_lhs || !a_rhs) {
					return a_lhs != nullptr;
				}
				return a_lhs->rating > a_rhs->rating;
			});
		}
		else if (a_slot.selectionMode == SlotSelectionMode::kRandom) {
			std::stable_sort(result.begin(), result.end(), [&](const ActiveItem* a_lhs, const ActiveItem* a_rhs) {
				if (!a_lhs || !a_rhs) {
					return a_lhs != nullptr;
				}
				auto hashItem = [&](const ActiveItem* a_item) {
					std::uint64_t seed = static_cast<std::uint64_t>(a_actorID) << 32;
					seed ^= static_cast<std::uint64_t>(std::hash<std::string>{}(a_slot.slotName));
					seed ^= static_cast<std::uint64_t>(a_item->object ? a_item->object->GetFormID() : 0);
					return seed ^ (a_item->uid * 0x9E3779B97F4A7C15ull);
				};
				return hashItem(a_lhs) < hashItem(a_rhs);
			});
		}
		return result;
	}

	std::optional<AssignmentResolver::SlotAssignmentDecision> AssignmentResolver::ResolveSlotAssignment(
		const SlotAssignmentContext& a_context)
	{
		if (!a_context.eligibility.resolveFormFilter || !a_context.eligibility.isBlackHole) {
			return std::nullopt;
		}

		// Preferred item order is explicit configuration order, not inventory
		// enumeration order or an incidental rating sort.
		for (const auto preferredFormID : a_context.slot.preferredItems) {
			auto it = std::find_if(a_context.fallbackCandidates.begin(), a_context.fallbackCandidates.end(), [&](ActiveItem* a_item) {
				return a_item && a_item->object && a_item->object->GetFormID() == preferredFormID &&
				IsSlotCandidateEligible(a_context.eligibility, a_item, true) &&
					!a_context.eligibility.isBlackHole(a_item);
			});
			if (it != a_context.fallbackCandidates.end()) {
				return SlotAssignmentDecision{ *it, AssignmentSource::kPreferredItem };
			}
		}

		if (a_context.slot.selectionMode == SlotSelectionMode::kStrongest) {
			for (auto* item : a_context.modeCandidates) {
				if (IsSlotCandidateEligible(a_context.eligibility, item) &&
					!a_context.eligibility.isBlackHole(item)) {
					return SlotAssignmentDecision{ item, AssignmentSource::kStrongest };
				}
			}
		}
		else if (a_context.slot.selectionMode == SlotSelectionMode::kRandom) {
			for (auto* item : a_context.modeCandidates) {
				if (IsSlotCandidateEligible(a_context.eligibility, item) &&
					!a_context.eligibility.isBlackHole(item)) {
					return SlotAssignmentDecision{ item, AssignmentSource::kRandom };
				}
			}
		}
		else {
			// The default IED pass is the latest eligible concrete inventory
			// instance from the equip-event cache.
			ActiveItem* lastEquippedMatch = nullptr;
			std::uint64_t lastEquippedScore = 0;
			for (auto* item : a_context.fallbackCandidates) {
				if (!IsSlotCandidateEligible(a_context.eligibility, item) ||
					a_context.eligibility.isBlackHole(item)) continue;
				const auto score = a_context.getRecentEquipScore ?
					a_context.getRecentEquipScore(item) : 0;
				if (score > lastEquippedScore) {
					lastEquippedMatch = item;
					lastEquippedScore = score;
				}
			}
			if (lastEquippedMatch) {
				return SlotAssignmentDecision{ lastEquippedMatch, AssignmentSource::kLastEquipped };
			}
		}

		// Final IED pass: take the first candidate in configured slot/type order
		// that satisfies the slot's filters.
		for (auto* item : a_context.fallbackCandidates) {
			if (IsSlotCandidateEligible(a_context.eligibility, item) &&
				!a_context.eligibility.isBlackHole(item)) {
				return SlotAssignmentDecision{ item, AssignmentSource::kSlotPriority };
			}
		}
		return std::nullopt;
	}
}
