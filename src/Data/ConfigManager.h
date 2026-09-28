#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>

namespace IAD
{
	enum class ConfigScope : std::uint32_t {
		kGlobal = 0,
		kRace = 1,
		kNPC = 2,
		kActor = 3
	};

	template <class T>
	struct ScopedData {
		ConfigScope scope{ ConfigScope::kGlobal };
		T data;

		ScopedData() = default;
		ScopedData(ConfigScope a_scope, const T& a_data) : scope(a_scope), data(a_data) {}
	};

	template <class T>
	struct GenderedData {
		T m; // 男
		T f; // 女
	};

	struct TransformData {
		RE::NiPoint3 pos{ 0, 0, 0 };
		RE::NiPoint3 rot{ 0, 0, 0 };
		float scale{ 1.0f };
		RE::NiPoint3 pivot{ 0, 0, 0 };
	};

	using GenderedTransform = GenderedData<TransformData>;

	struct PhysicsConstraintParams {
		float velocityResponseScale = 0.1f;
		float penBiasDepthLimit = 20000.0f;
		float restitutionCoefficient = 0.0f;
		float penBiasFactor = 1.0f;
	};

	struct PhysicsValues {
		bool disabled = true;
		bool drawConstraints = false;
		bool drawPendulum = false;

		float stiffness = 2.0f;
		float stiffness2 = 1.0f;
		float springSlackOffset = 0.0f;
		float springSlackMag = 0.0f;
		float damping = 0.95f;
		float resistance = 0.0f;
		float mass = 1.0f;
		float maxVelocity = 20000.0f;
		float gravityBias = 1200.0f;

		RE::NiPoint3 linear{ 0, 0, 0 };
		RE::NiPoint3 rotational{ 1.0f, 1.0f, 1.0f };
		RE::NiPoint3 cogOffset{ 0, 0, 0 };

		bool enableAngularConstraint = false;
		float minPitch = -15.0f;
		float maxPitch = 60.0f;
		float minYaw = -45.0f;
		float maxYaw = 45.0f;
		float minRoll = -45.0f;
		float maxRoll = 45.0f;

		float visualProbeLength = 40.0f;

		bool enableBoxConstraint = false;
		RE::NiPoint3 maxOffsetN{ -20.0f, -20.0f, -20.0f };
		RE::NiPoint3 maxOffsetP{ 20.0f, 20.0f, 20.0f };
		float maxOffsetBoxFriction = 0.025f;
		PhysicsConstraintParams boxParams;

		bool enableSphereConstraint = false;
		RE::NiPoint3 maxOffsetSphereOffset{ 0, 0, 0 };
		float maxOffsetSphereRadius = 20.0f;
		float maxOffsetSphereFriction = 0.025f;
		PhysicsConstraintParams sphereParams;
	};

	struct ConditionNode {
		bool isGroup = true;
		bool isAnd = true;
		bool isNot = false;
		std::string type = "";
		std::string keyword = "";
		std::string keyword2 = "";
		bool expected = true;
		std::vector<ConditionNode> children;

		ConditionNode(bool group = true, bool andOp = true)
			: isGroup(group), isAnd(andOp), type(""), keyword(""), keyword2(""), isNot(false), expected(true) {
		}
	};

	// IED-compatible named keybind definition. numStates follows IED semantics:
	// state zero is the default, and each press advances through 1..numStates.
	struct KeyBindDefinition {
		std::uint32_t key = 0;
		std::uint32_t comboKey = 0;
		std::uint32_t numStates = 1;
	};

	struct ColorRGBA {
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
	};

	struct ModelAnimationConfig {
		bool playSequence = false;
		bool forwardAnimationEvents = false;
		// Legacy field retained so older JSON remains readable; the FO4 display
		// clone path does not expose or execute SubGraph attachment.
		bool attachSubGraphs = false;
		bool disableBehaviorGraphAnims = true;
		std::string sequenceName = "";
		std::string animationEvent = "";
	};

	struct ModelLightConfig {
		bool enabled = false;
		// Legacy fields retained for config compatibility. The active NiLight path
		// supports color, radius, dimmer, and transform only.
		bool targetSelf = false;
		bool dontLightWater = false;
		bool dontLightLandscape = false;
		bool castShadows = false;
		TransformData transform;
		ColorRGBA diffuse{ 1.0f, 0.92f, 0.75f, 1.0f };
		float radius = 128.0f;
		float dimmer = 1.0f;
		float fieldOfView = 60.0f;
		float shadowDepthBias = 0.0f;
	};

	struct ModelEffectShaderConfig {
		bool enabled = false;
		bool targetRoot = true;
		bool force = false;
		bool lighting = false;
		bool alpha = true;
		ColorRGBA fillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		ColorRGBA rimColor{ 0.0f, 0.0f, 0.0f, 0.0f };
		float baseFillScale = 1.0f;
		float baseFillAlpha = 1.0f;
		float baseRimAlpha = 1.0f;
		float edgeExponent = 1.0f;
		float alphaMultiplier = 1.0f;
		std::string baseTexturePath = "";
		std::string paletteTexturePath = "";
		std::string blockOutTexturePath = "";
	};

	struct ModelSwapVariableSource {
		bool enabled = false;
		std::string pathVariable = "";
		std::string formIDVariable = "";
	};

	struct StateOverride {
		std::string description = "新状态";
		ConditionNode conditionTree;
		bool continueAfterMatch = true;
		bool hideModel = false;

		bool overrideModelSwap = false;
		std::string modelSwapPath = "";
		std::uint32_t modelSwapFormID = 0;
		ModelSwapVariableSource modelSwapVariableSource;

		bool overrideDisplayFlags = false;
		bool extractMagazine = false;
		bool useProjectileForAmmo = false;
		bool useWorldModel = false;
		bool invisible = false;
		bool hideGeometry = false;
		bool hideLight = false;
		bool load1pWeaponModel = false;
		bool keepTorchFlame = false;
		bool removeScabbard = false;
		bool disableHavok = true;
		bool removeEditorMarker = true;
		bool removeProjectileTracers = true;

		bool overrideAnimation = false;
		ModelAnimationConfig animation;
		bool overrideEffectShader = false;
		ModelEffectShaderConfig effectShader;
		bool overrideLight = false;
		ModelLightConfig light;

		bool overrideTransform = false;
		bool useTransformPreset = false;
		std::string targetTransformPreset = "";
		TransformData independentTransform;

		bool overridePhysics = false;
		bool usePhysicsPreset = false;
		std::string targetPhysicsPreset = "";
		PhysicsValues independentPhysics;

		bool overrideTargetNode = false;
		std::string targetNode = "";

		bool absolutePosition = false;
		bool weaponAdjust = false;
		bool weightAdjust = false;

		bool overrideMeshTransform = false;
		TransformData independentMeshTransform;

		bool overrideGeometryTransform = false;
		TransformData independentGeometryTransform;
	};

	enum class ConditionalVariableType : std::uint8_t {
		kBoolean,
		kNumber,
		kForm,
		kModelPath
	};

	enum class ConditionalVariableFormSource : std::uint8_t {
		kStatic,
		kEquippedWeapon
	};

	struct ConditionalVariableRule {
		ConditionNode conditionTree;
		bool continueAfterMatch = true;
		bool booleanValue = false;
		float numberValue = 0.0f;
		std::uint32_t formIDValue = 0;
		ConditionalVariableFormSource formSource = ConditionalVariableFormSource::kStatic;
		std::string modelPathValue;
	};

	struct ConditionalVariableDefinition {
		std::string name;
		std::string description;
		ConditionalVariableType type = ConditionalVariableType::kBoolean;
		bool enabled = true;
		bool defaultBooleanValue = false;
		float defaultNumberValue = 0.0f;
		std::uint32_t defaultFormIDValue = 0;
		ConditionalVariableFormSource defaultFormSource = ConditionalVariableFormSource::kStatic;
		std::string defaultModelPathValue;
		std::vector<ConditionalVariableRule> rules;
	};

	struct ConfigBase {
		GenderedTransform transforms;
		PhysicsValues physics;
		ConditionNode displayConditionTree;
		std::vector<StateOverride> stateMachine;

		bool overrideTransform = false;
		bool overridePhysics = false;

		bool overrideMeshTransform = false;
		GenderedTransform meshTransforms;

		bool overrideGeometryTransform = false;
		GenderedTransform geometryTransforms;
	};

	struct AdvancedItemFilters {
		bool useBaseFilters = false;
		bool allowMelee = true;
		bool allowGun = true;
		bool allowThrown = true;
		bool allowOneHanded = true;
		bool allowTwoHanded = true;
		bool allowArmor = true;
		bool allowShield = true;
		bool allowAmmo = true;
		bool allowMedicine = true;
		bool allowFood = true;
		bool allowWater = true;
		bool allowKeys = true;
	};

	enum class KeywordFilterMode : int {
		kNone = 0,
		kWhitelist = 1,
		kBlacklist = 2
	};

	struct KeywordGroup {
		std::string groupName = "新关键字组";
		bool isAnd = false;
		std::vector<std::string> keywords;
	};

	struct FormFilter {
		bool denyAll = false;
		std::set<std::uint32_t> allowList;
		std::set<std::uint32_t> denyList;
		// IED-style named profile reference. When enabled, the slot resolves the
		// current profile content at evaluation time instead of copying it.
		bool useProfile = false;
		std::string profileName;
	};

	enum class AmmoDisplayMode : uint32_t {
		kSingle = 0,
		kDynamicArray = 1
	};

	// Mirrors IED's declared selection-mode shape. Preferred items remain an
	// unconditional override; this mode only selects among normal candidates.
	enum class SlotSelectionMode : uint32_t {
		kLastEquipped = 0,
		kStrongest = 1,
		kRandom = 2
	};

	struct SlotDefinition : public ConfigBase {
		std::string slotName;
		std::string originPath;
		int priority = 0;
		bool isEnabled = true;
		std::string targetNode;

		std::vector<std::uint32_t> preferredItems;
		FormFilter itemFilter;
		ConditionNode itemFilterConditionTree;
		bool overrideEquipmentMode = false;
		bool displayFavoritesOnly = true; // true = 仅限装备/收藏, false = 不限制(背包全显)
		SlotSelectionMode selectionMode = SlotSelectionMode::kLastEquipped;
		bool alwaysUnload = false;
		bool checkCannotWear = false;
		bool hideIfUsingFurniture = false;
		bool hideLayingDown = false;
		bool extractMagazine = false;
		bool useWorldModel = false;
		bool invisible = false;
		bool hideGeometry = false;
		bool hideLight = false;
		bool load1pWeaponModel = false;
		bool keepTorchFlame = false;
		bool removeScabbard = false;
		std::string modelSwapPath = "";
		std::uint32_t modelSwapFormID = 0;
		ModelSwapVariableSource modelSwapVariableSource;
		bool disableHavok = true;
		bool removeEditorMarker = true;
		bool removeProjectileTracers = true;
		ModelAnimationConfig animation;
		ModelLightConfig light;
		ModelEffectShaderConfig effectShader;

		struct AmmoRigConfig {
			bool isDedicatedAmmoSlot = false;
			AmmoDisplayMode displayMode = AmmoDisplayMode::kSingle;
			int maxMags = 3;
			float magSpacing = 5.0f;
			bool dynamicAmmoLogic = true;
			RE::NiPoint3 arrayDirection{ 0, 0, 1 };
		} ammoRig;

		std::vector<uint8_t> allowedFormTypes;
		std::vector<uint8_t> formTypePriority;
		int formTypePriorityLimit = 0;
		bool formTypePriorityAccountForEquipped = true;
		AdvancedItemFilters advancedFilters;
		KeywordFilterMode keywordMode = KeywordFilterMode::kNone;
		std::vector<KeywordGroup> keywordGroups;
		bool useProjectileForAmmo = false;

		// 👇========== 🌟 新增：独立枪套与刀鞘系统支持 ==========👇
		std::string holsterModelPath = "";
		bool keepHolsterWhenDrawn = true;
		GenderedTransform holsterTransforms;
		// 👆==========================================================👆
	};

	struct NodeDefinition : public ConfigBase {
		std::string nodeName;
		std::string originPath;
		bool isEnabled = true;

		bool absolutePosition = false;
		// Legacy fields retained for config compatibility; the active transform
		// path applies the configured transform directly.
		bool weaponAdjust = false;
		bool weightAdjust = false;

		std::vector<std::string> fallbackHosts;

		struct SkeletonMatchConfig {
			bool enabled = false;
			bool invert = false;
			std::string skeletonPathContains = "";
			std::uint32_t raceFormID = 0;
			std::uint32_t npcFormID = 0;
			std::vector<std::string> requiredNodes;
			std::vector<std::string> forbiddenNodes;
		} skeletonMatch;
	};

	struct ModelGroupEntry {
		std::string name = "ModelGroup";
		bool isEnabled = true;
		int sourceMode = 0; // 0 = NIF path, 1 = form model
		std::string modelPath = "";
		std::uint32_t sourceFormID = 0;
		bool extractMagazine = false;
		bool useProjectileForAmmo = false;
		std::string targetNode = "";
		bool hideWithWeapon = true;
		bool continueAfterMatch = true;
		bool loadOnlyWhenVisible = false;
		bool disableHavok = true;
		bool removeEditorMarker = true;
		bool removeProjectileTracers = true;
		bool invisible = false;
		bool hideGeometry = false;
		bool hideLight = false;
		bool load1pWeaponModel = false;
		bool keepTorchFlame = false;
		bool removeScabbard = false;
		ConditionNode displayConditionTree;
		GenderedTransform transforms;
		bool overrideGeometryTransform = false;
		GenderedTransform geometryTransforms;

		ModelLightConfig light;
		ModelEffectShaderConfig effectShader;
		ModelAnimationConfig animation;
	};

	struct CustomDefinition : public ConfigBase {
		std::string customName;
		uint32_t targetFormID = 0;
		std::string originPath;
		bool isEnabled = true;
		int priority = 0;
		std::string targetNode;
		std::string targetDisplaySlot;
		// FO4 equivalent of IED Custom variable mode. A non-zero runtime value
		// replaces targetFormID for this evaluation; targetFormID remains fallback.
		bool useRuntimeTargetForm = false;
		std::string runtimeTargetFormVariable;
		// Displays the resolved target Form's base model without requiring an
		// inventory instance. Instance OMODs are intentionally not applied.
		bool displayFormWithoutInventory = false;
		bool ignorePlayer = false;

		bool overrideEquipmentMode = false;
		bool displayFavoritesOnly = true;

		bool alwaysUnload = false;
		bool extractMagazine = false;
		bool hideIfUsingFurniture = false;
		bool hideLayingDown = false;
		bool useWorldModel = false;
		bool invisible = false;
		bool hideGeometry = false;
		bool hideLight = false;
		bool load1pWeaponModel = false;
		bool keepTorchFlame = false;
		bool removeScabbard = false;

		float spawnChance = 100.0f;
		bool lastEquippedMode = false;
		bool lastEquippedPrioritizeRecentBipedSlots = true;
		bool lastEquippedSkipOccupiedBipedSlots = false;
		bool lastEquippedDisableIfBipedSlotOccupied = true;
		bool lastEquippedPrioritizeRecentDisplaySlot = true;
		bool lastEquippedSkipOccupiedDisplaySlots = false;
		bool lastEquippedDisableIfDisplaySlotOccupied = false;
		bool lastEquippedFallbackToSlotted = false;
		bool lastEquippedFallbackToAnySlot = true;
		bool lastEquippedFallbackToRecentAcquired = false;
		bool lastEquippedPrioritizeRecentAcquiredTypes = true;
		std::vector<std::uint32_t> lastEquippedBipedSlots;
		std::vector<std::string> lastEquippedDisplaySlots;
		std::vector<std::uint8_t> lastEquippedRecentAcquiredFormTypes;
		ConditionNode lastEquippedFilterConditionTree;
		bool disableIfEquipped = false;
		bool selectInventoryRandom = false;
		// Explicit selection override for this Custom only.  The default keeps the
		// configured form order; this never changes ordinary slot selection.
		bool selectInventoryStrongest = false;
		ConditionNode inventoryConditionTree;

		int countMin = 1;
		int countMax = 0;
		std::vector<std::uint32_t> extraItems;
		std::string modelSwapPath = "";
		std::uint32_t modelSwapFormID = 0;
		ModelSwapVariableSource modelSwapVariableSource;
		bool useProjectileForAmmo = false;
		bool disableHavok = true;
		bool removeEditorMarker = true;
		bool removeProjectileTracers = true;
		ModelAnimationConfig animation;
		ModelLightConfig light;
		ModelEffectShaderConfig effectShader;
		bool groupMode = false;

		// 👇========== 🌟 新增：专属武器独立枪套系统支持 ==========👇
		std::string holsterModelPath = "";
		bool keepHolsterWhenDrawn = true;
		GenderedTransform holsterTransforms;
		// 👆==========================================================👆

		std::vector<ModelGroupEntry> modelGroups;
	};

	// Runtime consumers read one coherent copy per update/evaluation pass. The
	// editor may mutate the backing fields from the render thread, so callers
	// must not keep references into ConfigManager while evaluating an actor.
	struct RuntimeSettingsSnapshot {
		bool displayFavoritesOnly = true;
		bool prioritizeEquippedCandidates = true;
		bool useRecentDisplaySlotMemory = true;
		bool reserveEquippedForPositivePrioritySlots = true;
		bool prioritizeRecentAcquired = true;
		bool enableEquipmentPhysics = true;
		bool enableModelEffects = true;
		bool enableModelLights = true;
		bool enableNPCDisplays = true;
		bool blockPlayerDisplays = false;
		std::uint32_t npcEvaluationIntervalTicks = 4;
		bool nodeMonitorUseFilter = false;
	};

	struct InputSettingsSnapshot {
		std::uint32_t editorHotkey = 0x08;
		std::uint32_t editorModifier = 0;
		std::uint32_t playerBlockHotkey = 0;
		std::uint32_t playerBlockModifier = 0;
	};

	// Runtime evaluation consumes one coherent copy of all scope-resolved
	// configuration domains. The editor may mutate ConfigManager between frames,
	// so the runtime adapter must not resolve Slot, Node, and Custom data in
	// separate lock/read windows.
	struct RuntimeConfigSnapshot {
		std::vector<ScopedData<SlotDefinition>> scopedSlots;
		std::vector<ScopedData<NodeDefinition>> scopedNodes;
		std::vector<ScopedData<CustomDefinition>> scopedCustoms;
	};

	class ConfigManager
	{
	public:
		static ConfigManager* GetSingleton() {
			static ConfigManager instance;
			return &instance;
		}

		mutable std::recursive_mutex _configMutex;
		void LoadConfig();
		void SaveConfig();
		std::string GetConfigDir() const;

		enum class SerFlags : uint32_t {
			kNone = 0, kSlotGlobal = 1 << 0, kSlotActor = 1 << 1, kSlotNPC = 1 << 2, kSlotRace = 1 << 3,
			kNodeGlobal = 1 << 4, kNodeActor = 1 << 5, kNodeNPC = 1 << 6, kNodeRace = 1 << 7,
			kCustomGlobal = 1 << 8, kCustomActor = 1 << 9, kCustomNPC = 1 << 10, kCustomRace = 1 << 11, kFormFilters = 1 << 12,
			kAll = 0xFFFFFFFF
		};

		void ExportPreset(const std::string& a_presetName, uint32_t a_flags);
		void ImportPreset(const std::string& a_presetName, uint32_t a_flags, bool a_merge);
		std::vector<std::string> GetAvailableExports();
		bool RenameExport(const std::string& oldName, const std::string& newName);
		bool DeleteExport(const std::string& profileName);

		std::vector<SlotDefinition>& GetSlots(ConfigScope a_scope, std::uint32_t a_idOrFilter);
		std::vector<NodeDefinition>& GetNodes(ConfigScope a_scope, std::uint32_t a_idOrFilter);
		std::vector<CustomDefinition>& GetCustoms(ConfigScope a_scope, std::uint32_t a_idOrFilter);
		std::vector<std::uint32_t> GetConfiguredSlotTargetIDs(ConfigScope a_scope);
		std::vector<std::uint32_t> GetConfiguredNodeTargetIDs(ConfigScope a_scope);
		std::vector<std::uint32_t> GetConfiguredCustomTargetIDs(ConfigScope a_scope);

		std::vector<ScopedData<CustomDefinition>> ResolveCustomsWithScope(RE::Actor* a_actor);
		std::vector<ScopedData<SlotDefinition>> ResolveSlotsWithScope(RE::Actor* a_actor);
		std::vector<ScopedData<NodeDefinition>> ResolveNodesWithScope(RE::Actor* a_actor);
		RuntimeConfigSnapshot GetRuntimeConfigSnapshot(RE::Actor* a_actor);
		RuntimeSettingsSnapshot GetRuntimeSettingsSnapshot() const;
		InputSettingsSnapshot GetInputSettingsSnapshot() const;

		void SaveSlotProfile(const std::string& profileName, const std::vector<SlotDefinition>& data);
		bool LoadSlotProfile(const std::string& profileName, std::vector<SlotDefinition>& outData, bool overwrite = false);
		void SaveNodeProfile(const std::string& profileName, const std::vector<NodeDefinition>& data);
		bool LoadNodeProfile(const std::string& profileName, std::vector<NodeDefinition>& outData, bool overwrite = false);
		void SaveNodeConversionProfile(const std::string& profileName, const std::vector<NodeDefinition>& data, bool version2 = false);
		bool LoadNodeConversionProfile(const std::string& profileName, std::vector<NodeDefinition>& outData, bool overwrite = false, bool version2 = false);
		std::vector<std::string> GetAvailableNodeConversionProfiles(bool version2 = false);
		void SaveCustomProfile(const std::string& profileName, const std::vector<CustomDefinition>& data);
		bool LoadCustomProfile(const std::string& profileName, std::vector<CustomDefinition>& outData, bool overwrite = false);
		void SaveModelGroupProfile(const std::string& profileName, const std::vector<ModelGroupEntry>& data);
		bool LoadModelGroupProfile(const std::string& profileName, std::vector<ModelGroupEntry>& outData, bool overwrite = false);
		void SaveNodeMonitorProfile(const std::string& profileName, const std::vector<std::string>& data);
		bool LoadNodeMonitorProfile(const std::string& profileName, std::vector<std::string>& outData, bool overwrite = false);
		void SaveConditionalVariableProfile(const std::string& profileName, const std::vector<ConditionalVariableDefinition>& data);
		bool LoadConditionalVariableProfile(const std::string& profileName, std::vector<ConditionalVariableDefinition>& outData, bool overwrite = false);
		void SaveConditionProfile(const std::string& profileName, const ConditionNode& data);
		bool LoadConditionProfile(const std::string& profileName, ConditionNode& outData);
		void SaveTransformProfile(const std::string& profileName, const TransformData& data);
		bool LoadTransformProfile(const std::string& profileName, TransformData& outData);
		void SavePhysicsProfile(const std::string& profileName, const PhysicsValues& data);
		bool LoadPhysicsProfile(const std::string& profileName, PhysicsValues& outData);
		std::vector<std::string> GetAvailableProfiles(const std::string& folderName);
		bool RenameProfile(const std::string& folderName, const std::string& oldName, const std::string& newName);
		bool DeleteProfile(const std::string& folderName, const std::string& profileName);

		void SaveFormFilterProfile(const std::string& profileName, const FormFilter& data);
		bool LoadFormFilterProfile(const std::string& profileName, FormFilter& outData);
		bool RenameFormFilterReferences(const std::string& oldName, const std::string& newName);
		bool ClearFormFilterReferences(const std::string& profileName);

		static uint8_t StringToFormType(const std::string& typeStr);
		static std::string FormTypeToString(uint8_t type);
		bool IsRecentAcquiredFormTypeEnabled(std::uint8_t a_formType) const;
		void SetRecentAcquiredFormTypeEnabled(std::uint8_t a_formType, bool a_enabled);
		bool GetRuntimeVariable(const std::string& a_name);
		void SetRuntimeVariable(const std::string& a_name, bool a_value);
		void RemoveRuntimeVariable(const std::string& a_name);
		std::map<std::string, bool> GetRuntimeVariablesSnapshot();
		float GetRuntimeNumberVariable(const std::string& a_name);
		void SetRuntimeNumberVariable(const std::string& a_name, float a_value);
		void RemoveRuntimeNumberVariable(const std::string& a_name);
		std::map<std::string, float> GetRuntimeNumberVariablesSnapshot();
		std::string GetRuntimeModelPathVariable(const std::string& a_name);
		void SetRuntimeModelPathVariable(const std::string& a_name, const std::string& a_value);
		void RemoveRuntimeModelPathVariable(const std::string& a_name);
		std::map<std::string, std::string> GetRuntimeModelPathVariablesSnapshot();
		std::uint32_t GetRuntimeFormVariable(const std::string& a_name);
		void SetRuntimeFormVariable(const std::string& a_name, std::uint32_t a_value);
		void RemoveRuntimeFormVariable(const std::string& a_name);
		std::map<std::string, std::uint32_t> GetRuntimeFormVariablesSnapshot();
		bool IsActorDisplayBlocked(RE::Actor* a_actor);
		bool IsPlayerDisplaysBlocked();
		bool SetPlayerDisplaysBlocked(bool a_blocked);
		bool AddBlockedActorFormID(std::uint32_t a_formID);
		bool RemoveBlockedActorFormID(std::uint32_t a_formID);
		std::vector<std::uint32_t> GetBlockedActorFormIDsSnapshot();
		void AddNodeMonitorName(const std::string& a_name);
		void RemoveNodeMonitorName(std::size_t a_index);
		std::vector<std::string> GetNodeMonitorNamesSnapshot();
		std::vector<std::string> GetKeyBindConditionKeysSnapshot();
		std::map<std::string, KeyBindDefinition> GetKeyBindDefinitionsSnapshot();
		void SetKeyBindDefinitions(std::map<std::string, KeyBindDefinition> a_definitions);
		std::vector<std::uint32_t> GetQuestStageConditionFormIDsSnapshot();
		std::vector<std::uint32_t> GetActiveEffectConditionFormIDsSnapshot();
		std::vector<ConditionalVariableDefinition> GetConditionalVariablesSnapshot();
		void SetConditionalVariables(std::vector<ConditionalVariableDefinition> a_variables);

		void LoadINISettings();
		void SaveINISettings();

		std::uint32_t editorHotkey = 0x08;
		std::uint32_t editorModifier = 0;
		std::uint32_t playerBlockHotkey = 0;
		std::uint32_t playerBlockModifier = 0;
		int logLevel = 2;
		std::string uiLanguage = "zh_CN";
		void SetEditorHotkey(std::uint32_t a_key);
		bool displayFavoritesOnly = true;
		bool prioritizeEquippedCandidates = true;
		bool useRecentDisplaySlotMemory = true;
		bool reserveEquippedForPositivePrioritySlots = true;
		bool prioritizeRecentAcquired = true;
		bool enableEquipmentPhysics = true;
		bool enableModelEffects = true;
		bool enableModelLights = true;
		bool enableNPCDisplays = true;
		bool blockPlayerDisplays = false;
		std::set<std::uint32_t> blockedActorFormIDs;
		std::uint32_t npcEvaluationIntervalTicks = 4;
		bool nodeMonitorUseFilter = false;
		std::vector<std::string> nodeMonitorNames;
		std::vector<std::uint8_t> recentAcquiredFormTypes = {
			static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kWEAP),
			static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kARMO),
			static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kAMMO),
			static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kALCH),
			static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kMISC)
		};
		std::map<std::string, bool> runtimeVariables;
		std::map<std::string, float> runtimeNumberVariables;
		std::map<std::string, std::string> runtimeModelPathVariables;
		std::map<std::string, std::uint32_t> runtimeFormVariables;
		std::map<std::string, KeyBindDefinition> keyBindDefinitions;
		std::vector<ConditionalVariableDefinition> conditionalVariables;

		// 👇========== 🌟 新增：全局 UI 窗口状态记忆 ==========👇
		bool uiShowSlots = true;
		bool uiShowNodes = false;
		bool uiShowCustoms = false;
		bool uiShowFilters = false;
		bool uiShowSettings = false;
		bool uiShowProfiles = false;
		bool uiShowProfileSlots = false;
		bool uiShowProfileCustoms = false;
		bool uiShowProfileNodes = false;
		bool uiShowProfileFormFilters = false;
		bool uiShowProfileModelGroups = false;
		bool uiShowProfileNodeMonitors = false;
		bool uiShowProfileConditions = false;
		bool uiShowProfileTransforms = false;
		bool uiShowProfilePhysics = false;
		bool uiShowBoneScanner = false;
		bool uiShowVisualizer = false;
		int uiProfileManagedCategory = 0;
		int uiProfileRequestedTab = -1;
		int  uiLastClosedWindow = 1; // 记忆最后关闭的是哪个窗口

		struct UILayoutState {
			float slotLeftPaneWidth = 240.0f;
			float nodeLeftPaneWidth = 240.0f;
			float customLeftPaneWidth = 240.0f;
			float filterLeftPaneWidth = 240.0f;
			float profileSlotLeftPaneWidth = 220.0f;
			float profileNodeLeftPaneWidth = 220.0f;
			float profileCustomLeftPaneWidth = 220.0f;
			int slotInspectorTab = 0;
			int nodeInspectorTab = 0;
			int customPrimaryTab = 0;
			int customInspectorSection = 0;
		} uiLayout;
		// 👆===================================================👆

	private:
		ConfigManager() = default;
		std::string _configDir;

		std::map<ConfigScope, std::map<std::uint32_t, std::vector<SlotDefinition>>> _slots;
		std::map<ConfigScope, std::map<std::uint32_t, std::vector<NodeDefinition>>> _nodes;
		std::map<ConfigScope, std::map<std::uint32_t, std::vector<CustomDefinition>>> _customs;
	};
}
