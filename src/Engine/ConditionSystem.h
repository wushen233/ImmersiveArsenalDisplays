#pragma once
#include "pch.h"
#include <vector>
#include <string>
#include "Data/ConfigManager.h" 

// 👇========== 🌟 修正为新库的首字母分类路径 ==========👇
#include <RE/B/BGSInventoryItem.h>
// 👆===================================================👆

namespace RE {
	class Actor;
	class TESBoundObject;
}

namespace IAD
{
	struct ActiveItem {
		RE::TESBoundObject* object;
		RE::BGSInventoryItem::Stack* stack;
		bool isEquipped;
		bool isFavorited;
		bool ratingUsesInstanceData = false;
		float rating = 0.0f;
		std::uint32_t count = 0;
		std::uint32_t stackID = 0;
		std::uint64_t uid = 0; // 🌟 新增：IED级别的终极防伪标识符
	};

	class Scanner
	{
	public:
		static std::vector<ActiveItem> GetActiveItems(RE::Actor* a_actor, bool a_favoritesOnly = false);
		static std::uint32_t GetItemCount(RE::Actor* a_actor, std::uint32_t a_formID);
	};

	class ConditionEvaluator
	{
	public:
		// 1. 动作与物理状态 
		static bool IsSneaking(RE::Actor* a_actor);
		static bool IsSprinting(RE::Actor* a_actor);
		static bool IsSitting(RE::Actor* a_actor);
		static bool IsSleeping(RE::Actor* a_actor);
		static bool IsInCombat(RE::Actor* a_actor);

		// 👇========== 🌟 辐射4 终极特化：6阶武器状态全收录 ==========👇
		static bool IsWeaponDrawn(RE::Actor* a_actor);       // 广义的“手里有武器” (包括正在拔和正在收)
		static bool IsWeaponDrawing(RE::Actor* a_actor);     // 仅仅是“正在拔出”的那零点几秒
		static bool IsWeaponDrawnStrict(RE::Actor* a_actor); // 仅仅是“完全拔出并持握”的状态
		static bool IsWeaponSheathing(RE::Actor* a_actor);   // 仅仅是“正在插回枪套”的那零点几秒
		static bool IsWeaponSheathed(RE::Actor* a_actor);    // 完全收起状态
		static bool IsDrawnPistol(RE::Actor* a_actor);
		static bool IsDrawnRifle(RE::Actor* a_actor);
		static bool IsDrawnMelee(RE::Actor* a_actor);
		// 👆==========================================================👆

		static bool IsBleedingOut(RE::Actor* a_actor);
		static bool IsFirstPerson(RE::Actor* a_actor);
		static bool IsInPowerArmor(RE::Actor* a_actor);
		static bool IsInInterior(RE::Actor* a_actor);
		static bool IsInPublicCell(RE::Actor* a_actor);
		static bool IsInOwnedCell(RE::Actor* a_actor);
		static bool IsCellOwner(RE::Actor* a_actor);
		static bool IsNPCCellOwner(RE::Actor* a_actor);
		static bool IsInWater(RE::Actor* a_actor);
		static bool IsUnderwater(RE::Actor* a_actor);
		static bool IsSwimming(RE::Actor* a_actor);
		static bool IsInvisible(RE::Actor* a_actor);
		static bool IsInAir(RE::Actor* a_actor);
		static bool IsAiming(RE::Actor* a_actor);
		static bool IsUsingPipboy(RE::Actor* a_actor);
		static bool IsTimeOfDay(float a_startTime, float a_endTime);
		static bool IsCrafting(RE::Actor* a_actor);
		static bool IsInDialogue(RE::Actor* a_actor);
		static bool IsInVertibird(RE::Actor* a_actor);
		static bool IsVisible(RE::Actor* a_actor);
		static bool IsFollowing(RE::Actor* a_actor);
		static bool IsPathing(RE::Actor* a_actor);
		static bool IsPathingComplete(RE::Actor* a_actor);
		static bool IsQuadruped(RE::Actor* a_actor);
		static bool IsFlying(RE::Actor* a_actor);
		static bool IsFlightBlocked(RE::Actor* a_actor);
		static bool CanFly(RE::Actor* a_actor);
		static bool IsForceRun(RE::Actor* a_actor);
		static bool IsForceSneak(RE::Actor* a_actor);
		static bool IsHeadTracking(RE::Actor* a_actor);
		static bool WantsBlocking(RE::Actor* a_actor);
		static bool IsStaggered(RE::Actor* a_actor);
		static bool IsInSyncAnim(RE::Actor* a_actor);
		static bool IsReanimating(RE::Actor* a_actor);
		static bool IsScenePackage(RE::Actor* a_actor);
		static bool IsInRandomScene(RE::Actor* a_actor);
		static bool CanSpeak(RE::Actor* a_actor);
		static bool CanDoFavor(RE::Actor* a_actor);
		static bool CanSpeakToEssentialDown(RE::Actor* a_actor);
		static bool IsAttackOnSight(RE::Actor* a_actor);
		static bool IsAttackingDisabled(RE::Actor* a_actor);
		static bool IsCastingDisabled(RE::Actor* a_actor);
		static bool IsMovementBlocked(RE::Actor* a_actor);
		static bool DoNotShowOnStealthMeter(RE::Actor* a_actor);
		static bool IsInBleedoutAnimation(RE::Actor* a_actor);
		static bool IsGuard(RE::Actor* a_actor);
		static bool IsInMerchantFaction(RE::Actor* a_actor);
		static bool IsPlayerTeammate(RE::Actor* a_actor);
		static bool IsPlayerEnemy(RE::Actor* a_actor);
		static bool KeyBindStateMatches(const std::string& a_key, const std::string& a_expression);
		static bool PlayerEnemiesNearby(RE::Actor* a_actor, const std::string& a_radius);
		static bool InPlayerEnemyFaction(RE::Actor* a_actor);
		static bool IsLayingDown(RE::Actor* a_actor);
		static bool HasHumanoidSkeleton(RE::Actor* a_actor);
		static bool SkeletonPathContains(RE::Actor* a_actor, const std::string& a_text);
		static bool IsPlayer(RE::Actor* a_actor);
		static bool IsFemale(RE::Actor* a_actor);
		static bool IsDead(RE::Actor* a_actor);
		static bool IsChild(RE::Actor* a_actor);
		static bool IsUnconscious(RE::Actor* a_actor);
		static bool IsRestrained(RE::Actor* a_actor);
		static bool IsTrespassing(RE::Actor* a_actor);
		static bool IsInKillmove(RE::Actor* a_actor);
		static bool IsBribedByPlayer(RE::Actor* a_actor);
		static bool IsAngryWithPlayer(RE::Actor* a_actor);
		static bool IsEssential(RE::Actor* a_actor);
		static bool IsProtected(RE::Actor* a_actor);
		static bool IsUnique(RE::Actor* a_actor);
		static bool IsSummonable(RE::Actor* a_actor);
		static bool IsInvulnerable(RE::Actor* a_actor);
		static bool IsCommanded(RE::Actor* a_actor);
		static bool IsParalyzed(RE::Actor* a_actor);
		static bool IsWaitingForPlayer(RE::Actor* a_actor);
		static bool ActorMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorBaseMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorRaceMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorCombatStyleMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorClassMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorInFaction(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorInCell(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool ActorInLocation(RE::Actor* a_actor, std::uint32_t a_formID, bool a_includeChildLocations = false);
		static bool ActorInWorldspace(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool GlobalValueMatches(std::uint32_t a_formID, const std::string& a_expression);
		static bool ActorLevelMatches(RE::Actor* a_actor, const std::string& a_expression);
		static bool ActorValueMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression);
		static bool ActorPerkRankMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression);
		static bool RandomPercentMatches(RE::Actor* a_actor, const ActiveItem* a_candidateItem, float a_percent, const std::string& a_seed);
		static bool DayOfWeekMatches(std::uint32_t a_day);
		static bool ActorLifeStateMatches(RE::Actor* a_actor, const std::string& a_state);
		static bool NodeMonitor(RE::Actor* a_actor, const std::string& a_nodeName, const std::string& a_mode = "");
		static bool IsSunAboveHorizon();
		static bool TimeOfDayPhaseMatches(const std::string& a_phase);
		static bool CurrentWeatherMatchesFormID(std::uint32_t a_formID);
		static bool WeatherClassMatches(const std::string& a_weatherClass);
		static bool LightingTemplateMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool QuestStageMatches(std::uint32_t a_formID, const std::string& a_expression);
		static bool HasActiveEffect(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool HasMagicEffect(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool HasSpell(RE::Actor* a_actor, std::uint32_t a_formID);
		static bool IsInDarkArea(RE::Actor* a_actor);
		static bool IsInDarkness(RE::Actor* a_actor);
		static bool InteriorAmbientLightLevelMatches(RE::Actor* a_actor, const std::string& a_expression);
		static bool SunAngleMatches(const std::string& a_expression, const std::string& a_flags);

		// 2. 装备与物品状态 
		static bool IsBipedSlotOccupied(RE::Actor* a_actor, std::uint32_t a_bipedIndex);
		static bool InventoryItemCountMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression);
		static bool CandidateStackCountMatches(const ActiveItem* a_candidateItem, const std::string& a_expression);
		static bool CandidateInventoryCountMatches(RE::Actor* a_actor, const ActiveItem* a_candidateItem, const std::string& a_expression);
		static bool HasKeywordEquipped(RE::Actor* a_actor, const char* a_keywordEditorID);
		static bool ActorHasKeyword(RE::Actor* a_actor, const char* a_keywordEditorID);
		static bool HasEquippedFormID(RE::Actor* a_actor, std::uint32_t a_formID, std::uint32_t a_omodID = 0);
		static bool PassesFormFilter(std::uint32_t a_formID, const FormFilter& a_filter);
		static bool PassesLegacyFilters(RE::TESBoundObject* a_obj, const AdvancedItemFilters& a_adv, KeywordFilterMode a_kwMode, const std::vector<KeywordGroup>& a_kwGroups);
		static bool CheckCannotWear(RE::Actor* a_actor, RE::TESBoundObject* a_item);
		static bool RollSpawnChance(RE::Actor* a_actor, std::uint32_t a_formID, float a_chance);
		static bool HasConditionRules(const ConditionNode& a_node);

		// 3. 状态机树状评估
		static bool EvaluateConditionTree(RE::Actor* a_actor, const ConditionNode& a_node);
		static bool EvaluateConditionTree(RE::Actor* a_actor, const ConditionNode& a_node, const ActiveItem* a_candidateItem);
	};
}
