#include "pch.h"
#include "UIConditionCatalog.h"

namespace IAD::UI {
    namespace {
        constexpr std::array<const char*, 132> kConditionTypes = {
            "IsSneaking", "IsSprinting", "IsInCombat", "IsWeaponDrawn", "IsWeaponDrawing", "IsWeaponDrawnStrict",
            "IsWeaponSheathing", "IsWeaponSheathed", "IsDrawn_Pistol", "IsDrawn_Rifle", "IsDrawn_Melee",
            "IsInPowerArmor", "IsInInterior", "InPublicCell", "InOwnedCell", "IsCellOwner", "IsNPCCellOwner",
            "IsInWater", "IsUnderwater", "IsUsingPipboy", "IsSitting", "IsSleeping", "IsLayingDown", "IsSwimming",
            "IsInvisible", "IsVisible", "IsCrafting", "IsInDialogue", "IsInVertibird", "IsPlayer", "IsPlayerTeammate",
            "IsPlayerEnemy", "InPlayerEnemyFaction", "PlayerEnemiesNearby", "IsFemale", "IsDead", "IsChild",
            "IsUnconscious", "IsRestrained", "IsBleedingOut", "IsInBleedoutAnimation", "IsTrespassing", "IsInKillmove",
            "IsBribedByPlayer", "IsAngryWithPlayer", "IsEssential", "IsProtected", "IsUnique", "IsSummonable",
            "IsInvulnerable", "IsCommanded", "IsParalyzed", "IsWaitingForPlayer", "IsGuard", "InMerchantFaction",
            "IsFollowing", "IsPathing", "IsPathingComplete", "IsQuadruped", "IsFlying", "IsFlightBlocked", "CanFly",
            "IsForceRun", "IsForceSneak", "IsHeadTracking", "WantsBlocking", "IsStaggered", "IsInSyncAnim",
            "IsReanimating", "IsScenePackage", "IsInRandomScene", "CanSpeak", "CanDoFavor", "CanSpeakToEssentialDown",
            "IsAttackOnSight", "IsAttackingDisabled", "IsCastingDisabled", "IsMovementBlocked", "DoNotShowOnStealthMeter",
            "ActorFormID", "ActorBaseFormID", "ActorRaceFormID", "ActorCombatStyle", "ActorClass", "ActorInFaction",
            "ActorInCell", "ActorInLocation", "ActorInLocationOrChild", "ActorInWorldspace", "GlobalValue", "ActorLevel",
            "ActorValue", "ActorPerkRank", "QuestStage", "DayOfWeek", "ActorLifeState", "HumanoidSkeleton",
            "SkeletonPathContains", "NodeMonitor", "RandomPercent", "KeyBindState", "IsFirstPerson", "IsInAir",
            "IsAiming", "IsTimeOfDay", "IsSunAboveHorizon", "SunAngle", "TimeOfDayPhase", "CurrentWeather", "WeatherClass",
            "LightingTemplate", "HasActiveEffect", "HasMagicEffect", "HasSpell", "InDarkArea", "InDarkness",
            "InteriorAmbientLightLevel", "RuntimeVariable", "RuntimeNumberVariable", "HasKeywordEquipped", "ActorHasKeyword",
            "HasEquippedBipedSlot", "HasEquippedFormID", "InventoryItemCount", "CandidateHasKeyword", "CandidateFormID",
            "CandidateFormType", "CandidateHasOMOD", "CandidateCount", "CandidateInventoryCount", "CandidateIsEquipped",
            "CandidateIsFavorited"
        };
    }

    const std::array<const char*, 132>& UIConditionCatalog::Types() noexcept
    {
        return kConditionTypes;
    }
}
