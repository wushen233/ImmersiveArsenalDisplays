#pragma once
#include "pch.h"
#include "Engine/ConditionSystem.h"
#include <vector>
#include <chrono>
#include <queue>
#include <functional>
#include <mutex>
// 👇 修正为新库的首字母路径 .h
#include <RE/B/BSTSmartPointer.h>

namespace IAD
{
	struct GCPendingItem {
		RE::TESObjectREFR* ref;
		RE::BSTSmartPointer<RE::ExtraDataList> weaponData;
		RE::BSTSmartPointer<RE::ExtraDataList> originalExtra;
		std::chrono::steady_clock::time_point expirationTime;
	};

	struct ModelCleanupPolicy {
		// Display clones are always render-only; retained for config compatibility.
		bool disableHavok = true;
		bool removeEditorMarker = true;
		bool removeProjectileTracers = true;
		bool removeScabbard = false;
		bool removeBlood = true;
		bool removeLights = true;
		bool removeUI = true;
		bool removeSounds = true;
		bool keepTorchFlame = false;
	};

	struct ModelEffectSettings {
		bool enabled = false;
		bool targetRoot = true;
		bool force = false;
		bool lighting = false;
		bool alpha = true;
		float fillR = 1.0f;
		float fillG = 1.0f;
		float fillB = 1.0f;
		float fillA = 1.0f;
		float rimR = 0.0f;
		float rimG = 0.0f;
		float rimB = 0.0f;
		float rimA = 0.0f;
		float baseFillScale = 1.0f;
		float baseFillAlpha = 1.0f;
		float baseRimAlpha = 1.0f;
		float edgeExponent = 1.0f;
		float alphaMultiplier = 1.0f;
	};

	struct ModelLightSettings {
		bool enabled = false;
		float posX = 0.0f;
		float posY = 0.0f;
		float posZ = 0.0f;
		float rotX = 0.0f;
		float rotY = 0.0f;
		float rotZ = 0.0f;
		float diffuseR = 1.0f;
		float diffuseG = 0.92f;
		float diffuseB = 0.75f;
		float diffuseA = 1.0f;
		float radius = 128.0f;
		float dimmer = 1.0f;
	};

	struct AsyncModelRequest {
		RE::TESObjectREFR* tempRef = nullptr;
		RE::BSTSmartPointer<RE::ExtraDataList> originalExtra;
		RE::BSTSmartPointer<RE::ExtraDataList> customExtra;
		bool isMagazineExtraction = false;
		bool loadFirstPersonModel = false;
		std::uint64_t sceneGeneration = 0;
		ModelCleanupPolicy cleanupPolicy;
		std::string validatePath;
		std::function<void(RE::NiAVObject*)> callback;
	};

	class ModelManager
	{
	public:
		static ModelManager* GetSingleton() {
			static ModelManager singleton;
			return &singleton;
		}

		void ProcessF4SEMessage(uint32_t a_msgType);
		RE::NiNode* GetOrCreateVirtualNode(RE::NiNode* a_parent, const char* a_nodeName);

		void RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestModelByPath(const std::string& a_path, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestModelByPath(const std::string& a_path, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestStaticFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback);
		void RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback);

		void SetModelAlpha(RE::NiAVObject* a_node, float a_alpha);
		void SetModelGeometryHidden(RE::NiAVObject* a_node, bool a_hidden);
		void ApplyModelEffect(RE::NiAVObject* a_node, const ModelEffectSettings& a_effect);
		void ApplyModelLight(RE::NiAVObject* a_node, const ModelLightSettings& a_light);
		RE::NiAVObject* FindNodeRecursive(RE::NiAVObject* a_root, const RE::BSFixedString& a_name);
		RE::NiAVObject* ExtractMagazineNode(RE::NiAVObject* a_root);

		void ProcessGarbageCollection();
		void ProcessAsyncQueue();
		void ForceClearAll();
		// MainMenu is the boundary between two independent actor scene graphs.
		// Only discard local IAD state here; never mutate temporary references while
		// the engine is tearing down or rebuilding a world.
		void BeginMainMenuTransition();

		// 💡 跨线程查询用：kPreLoadGame 触发自工作线程，下游清理逻辑需要知道当前是否在加载中以跳过 3D 操作
		static bool IsGameLoading();
		static bool IsGameSaving();
		static bool IsMainMenuTransition();
		static std::uint64_t GetSceneGeneration();

		std::vector<GCPendingItem> _gcQueue;
		std::vector<RE::NiAVObject*> _activeClones;

		std::queue<AsyncModelRequest> _asyncLoadQueue;
		std::mutex _queueMutex;

	private:
		ModelManager() = default;
		void FreezeToStaticStatue(RE::NiAVObject* a_node, bool a_isRoot, const ModelCleanupPolicy& a_cleanupPolicy);
		void CullBloodNodes(RE::NiAVObject* a_node, const ModelCleanupPolicy& a_cleanupPolicy);
		void RenameClonedNodes(RE::NiAVObject* a_node);
	};
}
