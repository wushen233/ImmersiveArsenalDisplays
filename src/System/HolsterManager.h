#pragma once
#include "pch.h"
#include "Engine/ConditionSystem.h"
#include "Data/ConfigManager.h"
#include "ModelManager.h"
#include "Engine/SimComponent.h"
#include "System/ActorRefreshScheduler.h"
#include "System/ConditionRefreshPolicy.h"
#include <RE/A/ActorEquipManager.h>
#include <RE/A/ActorEquipManagerEvent.h>
#include <RE/T/TESContainerChangedEvent.h>
#include <RE/T/TESSwitchRaceCompleteEvent.h>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <atomic>
#include <deque>

namespace IAD
{
	enum class DebugNodeType {
		kVanilla,
		kCME,
		kMOV
	};

	struct DebugNode {
		RE::NiPoint3 pos;
		RE::NiPoint3 parentPos;
		bool hasParent;
		std::string name;
		DebugNodeType type;
		RE::NiPoint3 axisX, axisY, axisZ;
		// Snapshot the spaces needed by the world preview editor. Keeping these
		// with the render snapshot avoids reading scene nodes from the UI thread.
		RE::NiMatrix3 parentWorldRotate;
		RE::NiMatrix3 localRotate;
		RE::NiMatrix3 rootWorldRotate;
		bool isAbsolute = false;
	};

	struct DebugBoundSphere {
		RE::NiPoint3 worldCenter{ 0.0f, 0.0f, 0.0f };
		float worldRadius = 0.0f;
		std::string meshName;
		// Sampled world-space mesh vertices used by the world preview editor to
		// draw and hit-test the actual weapon silhouette instead of a debug box.
		// Shared ownership keeps render snapshots cheap to copy while the game
		// thread publishes a replacement bound snapshot.
		std::shared_ptr<std::vector<RE::NiPoint3>> worldVertices;
		bool hasGeometryContour = false;
		// The bound is a render-thread-safe snapshot, so retain the logical MOV
		// slot that owns the scene model for hit testing and selection.
		std::string slotName;
		bool isHolster = false;
	};

	enum class ActiveAxis {
		kNone,
		kX,
		kY,
		kZ
	};

	struct NodeState {
		bool isHidden = false;
		bool isAbsolute = false;
		TransformData finalTransform;
		PhysicsValues activePhys;
		std::vector<std::string> targetBones;
	};

	struct HolsterSlot {
		std::vector<RE::NiPointer<RE::NiAVObject>> currentModels;
		std::vector<RE::NiPointer<RE::NiAVObject>> currentHolsters;
		std::vector<RE::NiPointer<RE::NiAVObject>> currentModelGroups;

		std::vector<RE::NiPointer<RE::NiAVObject>> oldModels;
		std::vector<RE::NiPointer<RE::NiAVObject>> oldHolsters;
		std::vector<RE::NiPointer<RE::NiAVObject>> oldModelGroups;
		// Scene traversal add-ons can retain a node briefly after a weapon switch.
		// Keep replaced display models culled until that traversal window has passed.
		uint64_t oldModelsRetireAfterTick = 0;

		std::string lastTargetNode = "";
		std::string lastHolsterPath = "";
		std::string lastModelRequestSignature = "";
		std::string lastModelGroupSignature = "";
		RE::TESBoundObject* lastItem = nullptr;

		std::uint64_t currentUID = 0;
		// Separate epochs keep a change to one resource from invalidating unrelated
		// in-flight model, holster, or model-group loads for the same slot.
		std::uint64_t modelRequestGeneration = 0;
		std::uint64_t holsterRequestGeneration = 0;
		std::uint64_t modelGroupRequestGeneration = 0;

		bool isEquipped = false;
		bool isSlotHidden = false;
		bool isWeaponHidden = false;
		bool isHolsterHidden = false;
		// Effective AlwaysUnload policy used by live visibility checks between
		// inventory evaluations and while an async model callback is pending.
		bool hideWeaponWhenDrawn = false;

		std::shared_ptr<IAD::SimComponent> physicsSim;
		uint64_t lastUpdateTime = 0;

		TransformData slotBaseTransform;
		PhysicsValues activePhys;
		bool hasActivePhys = false;
		// World-preview editing temporarily owns the live base transform. This
		// prevents the physics integrator from immediately replacing the value
		// while the user is dragging a slot gizmo.
		bool previewTransformActive = false;

		TransformData meshTransform;
		TransformData geometryTransform;
		bool overrideGeometryTransform = false;
		TransformData holsterMeshTransform;
		std::vector<TransformData> modelGroupTransforms;
		std::vector<TransformData> modelGroupGeometryTransforms;
		std::vector<bool> modelGroupOverrideGeometryTransforms;
		std::vector<bool> modelGroupHideWithWeapon;
		std::vector<bool> modelGroupConditionVisible;
		std::vector<bool> modelGroupInvisible;
		std::vector<bool> modelGroupHideGeometry;
		std::vector<ModelEffectSettings> modelGroupEffects;
		std::vector<ModelLightSettings> modelGroupLights;
		std::vector<std::string> modelGroupMovNames;
		std::vector<bool> modelGroupPlaySequence;
		std::vector<bool> modelGroupForwardAnimationEvents;
		std::vector<bool> modelGroupDisableBehaviorGraphAnims;
		std::vector<std::string> modelGroupSequenceNames;
		std::vector<std::string> modelGroupAnimationEvents;
		std::vector<bool> modelGroupAnimationPlayed;
		bool keepHolsterWhenDrawn = true;

		int numModelsToSpawn = 1;
		RE::NiPoint3 arrayDir{ 0, 0, 0 };
		float arraySpacing = 0.0f;
	};

	struct ActorDisplayRuntime {
		std::unordered_map<std::string, NodeState> nodeStates;
		std::unordered_map<std::string, HolsterSlot> displaySlots;
	};

	struct DebugSettings {
		bool showVanilla = false;  bool showVanillaNames = false;  bool showVanillaAxes = false;
		bool showCME = false;      bool showCMENames = true;       bool showCMEAaxes = true;
		bool showMOV = false;      bool showMOVNames = true;       bool showMOVAxes = true;

		bool useLocalAxesSpace = true;
		bool activeUIIsRotation = false;
		bool worldPreviewEdit = true;
		bool worldPreviewAutoWindow = true;
		// Opt-in while the detached Interface3D renderer is being validated on
		// the selected Fallout 4 runtime. The live camera remains the default.
		bool worldPreviewDetached = false;
	};

	class HolsterManager :
		public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>,
		public RE::BSTEventSink<RE::TESContainerChangedEvent>,
		public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>
	{
	public:
		static HolsterManager* GetSingleton() {
			static HolsterManager singleton;
			return &singleton;
		}

		std::unordered_map<RE::TESFormID, ActorDisplayRuntime> _actorDisplayRuntime;

		// Protects the settings edited by ImGui while the game thread snapshots
		// them for ActorDisplayContext publication.
		std::mutex debugSettingsMutex;
		DebugSettings debugSettings;
		ActiveAxis activeUIItemAxis = ActiveAxis::kNone;

		void Update();
		void StartUpdateLoop();

		void RequestEvaluate(RE::TESFormID a_formID);
		void RequestTransformUpdate(RE::TESFormID a_formID);
		// UI edits are published from the render thread and consumed/coalesced on
		// the game thread before actor transforms are updated.
		void RequestPreviewTransform(RE::TESFormID a_formID, const std::string& a_name, bool a_isMOV, const TransformData& a_transform);
		void RequestPreviewTransformReset(RE::TESFormID a_formID);
		void RequestEvaluateAll();
		void ForceRefreshAll() { _refreshScheduler.RequestGlobalRefresh(); }
		void SetNPCDisplaysEnabled(bool a_enabled);

		void ClearActorSlots(RE::TESFormID a_formID, bool a_skipSceneDetach = false);
		// Must run on the game thread. During a load transition callers pass true
		// because the old scene tree may already be owned by the engine.
		void ClearAllActorSlots(bool a_skipSceneDetach = false);
		// Game-thread lifecycle teardown. Detaches display clones while their actor
		// scene is still valid, before the engine tears down the actor skeleton.
		void DetachAllActorSlotsForSceneTeardown();

		virtual RE::BSEventNotifyControl ProcessEvent(const RE::ActorEquipManagerEvent::Event& a_event, RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>* a_eventSource) override;
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent& a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESSwitchRaceCompleteEvent& a_event, RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>* a_eventSource) override;
	private:
		uint64_t _currentUpdateTick = 0;
		uint64_t _lastLFSweepTime = 0;
		ActorRefreshScheduler _refreshScheduler;
		ConditionRefreshPolicy _conditionRefreshPolicy;

		std::atomic<bool> _isUpdatingLoop{ false }; // 防止 16ms 轮询积压洪水
		std::mutex _evalMutex;

		struct PendingPreviewTransform {
			bool valid = false;
			bool reset = false;
			RE::TESFormID formID = 0;
			bool isMOV = false;
			std::string name;
			TransformData transform;
		};
		std::mutex _previewTransformMutex;
		PendingPreviewTransform _pendingPreviewTransform;

		HolsterManager() = default;

		void EvaluateActor(RE::Actor* a_actor);
		void ProcessPreviewTransformRequest();
		void UpdateActorTransforms(RE::Actor* a_actor, std::vector<DebugBox>& newBoxes, std::vector<DebugNode>& newNodes);
		void CollectModelBoundSpheres(RE::Actor* a_actor, std::vector<DebugBoundSphere>& outSpheres);
		void ClearActorSlots_Internal(RE::TESFormID a_formID, bool a_skipSceneDetach = false, bool a_forceSceneDetach = false) noexcept;

		void ExecuteForceRefresh();
		bool BeginEvaluation(RE::TESFormID a_formID, bool a_force);
		void CompleteEvaluation(RE::TESFormID a_formID);
		void RunLowFrequencyMaintenance(RE::TESFormID a_playerID);
		void ClearActorHistory(RE::TESFormID a_formID);
		void RecordRecentEquip(RE::TESFormID a_actorID, std::uint64_t a_itemUID);
		bool IsRecentlyEquipped(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const;
		std::uint64_t GetRecentEquipScore(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const;
		void RecordRecentBipedSlots(RE::TESFormID a_actorID, RE::TESBoundObject* a_item);
		std::uint64_t GetRecentBipedSlotScore(RE::TESFormID a_actorID, std::uint32_t a_slot) const;
		void RecordRecentDisplaySlot(RE::TESFormID a_actorID, std::uint64_t a_itemUID, const std::string& a_slotName);
		bool GetRecentDisplaySlot(RE::TESFormID a_actorID, std::uint64_t a_itemUID, std::string& a_outSlotName) const;
		std::uint64_t GetRecentDisplayItemScore(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const;
		std::uint64_t GetRecentDisplaySlotScore(RE::TESFormID a_actorID, const std::string& a_slotName) const;
		void RecordRecentAcquired(RE::TESFormID a_actorID, RE::TESFormID a_formID);
		struct PendingEquipEvent {
			RE::TESFormID formID = 0;
			std::uint32_t stackID = 0;
		};

		mutable std::mutex _recentEquipMutex;
		std::unordered_map<RE::TESFormID, std::deque<std::uint64_t>> _recentEquippedItems;
		std::mutex _pendingEquipMutex;
		std::unordered_map<RE::TESFormID, std::deque<PendingEquipEvent>> _pendingEquippedForms;
		mutable std::mutex _recentBipedSlotMutex;
		std::uint64_t _recentBipedSlotSequence = 0;
		std::unordered_map<RE::TESFormID, std::unordered_map<std::uint32_t, std::uint64_t>> _recentBipedSlots;
		mutable std::mutex _recentDisplaySlotMutex;
		std::uint64_t _recentDisplaySlotSequence = 0;
		std::unordered_map<RE::TESFormID, std::unordered_map<std::uint64_t, std::string>> _recentDisplaySlots;
		std::unordered_map<RE::TESFormID, std::unordered_map<std::uint64_t, std::uint64_t>> _recentDisplayItemScores;
		std::unordered_map<RE::TESFormID, std::unordered_map<std::string, std::uint64_t>> _recentDisplaySlotScores;
		mutable std::mutex _recentAcquiredMutex;
		std::uint64_t _recentAcquiredSequence = 0;
		std::unordered_map<RE::TESFormID, std::unordered_map<RE::TESFormID, std::uint64_t>> _recentAcquiredForms;
	};
}
