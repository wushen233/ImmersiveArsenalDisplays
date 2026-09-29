#pragma once
#include "pch.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
// 👇========== 🌟 只更新头文件路径 ==========👇
#include <RE/N/NiPointer.h>
#include <RE/N/NiTransform.h>
// 👆========================================👆
#include "Data/ConfigManager.h"

namespace RE {
	class Actor;
	class NiNode;
	class NiAVObject;
	class TESBoundObject;
}

namespace IAD
{
	namespace TransformMath {
		RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& a_angles);
		RE::NiMatrix3 Inverse(const RE::NiMatrix3& m);
		RE::NiMatrix3 Multiply(const RE::NiMatrix3& a, const RE::NiMatrix3& b);
		RE::NiPoint3 Multiply(const RE::NiMatrix3& m, const RE::NiPoint3& v);
		RE::NiPoint3 Add(const RE::NiPoint3& a, const RE::NiPoint3& b);
		RE::NiPoint3 Subtract(const RE::NiPoint3& a, const RE::NiPoint3& b);

		void ApplyAdvancedTransform(
			RE::Actor* a_actor,
			RE::NiNode* a_node,
			const RE::NiTransform& a_origTransform,
			const TransformData& a_guiData,
			bool a_absolute
		);
	}

	class NodeManager
	{
	public:
		struct ManagedNode {
			// The actor skeleton owns attached IAD nodes. This cache only observes them;
			// cache invalidation must not alter engine scene ownership.
			RE::NiNode* node = nullptr;
			RE::NiTransform orig;
		};

		struct ActorNodeCache {
			RE::NiNode* root3D = nullptr;
			RE::NiNode* coreBone = nullptr;
			std::uint64_t actor3DGeneration = 0;
			bool wasDead = false;
			bool hasInjectedMounts = false;
			std::unordered_map<std::string, ManagedNode> activeNodes;
		};

		static std::unordered_map<RE::TESFormID, ActorNodeCache> _nodeCache;
		// Monotonic per-actor scene identity. It survives cache eviction so a
		// replacement 3D tree cannot reuse the identity of a stale UI snapshot.
		static std::unordered_map<RE::TESFormID, std::uint64_t> _actor3DGenerations;
		static std::mutex _cacheMutex;
		static std::unordered_map<std::string, std::unordered_map<std::string, RE::NiMatrix3>> _pathBasedBoneDicts;
		static std::mutex _dictMutex;

		static RE::NiNode* GetNodeByName(RE::NiNode* a_root, const std::string& a_name);
		static RE::NiNode* InjectCMENode(RE::Actor* a_actor, const std::string& a_cmeName, const std::vector<std::string>& a_fallbackTargetNodes);
		static RE::NiNode* GetOrCreateMOVNode(RE::Actor* a_actor, RE::NiNode* a_cmeNode, const std::string& a_movName);
		static void RemoveManagedNode(RE::Actor* a_actor, const std::string& a_name);
		// Must be invoked from a game-thread lifecycle boundary before an actor
		// scene is destroyed. MOV nodes are detached before their CME parents.
		static void DetachAllManagedNodesForSceneTeardown();
		static void ClearCache(RE::TESFormID a_formID);
		static void ForgetCache(RE::TESFormID a_formID);
		static void ClearAllCaches();
		// Rebuild configuration-owned CME/MOV bindings without making the current
		// actor 3D tree look like a newly-created scene to the UI identity tracker.
		static void InvalidateForConfigRefresh();
		static std::uint64_t GetActor3DGeneration(RE::TESFormID a_formID);

		static void EnsureBoneDictionaryForPath(const std::string& a_nifPath);
		static bool Is3DSafeAndCacheReady(RE::Actor* a_actor, uint64_t a_currentTick);

		// Returns cache metadata by value; callers do not retain activeNodes storage after the cache lock is released.
		static std::optional<ManagedNode> GetManagedNode(RE::Actor* a_actor, const std::string& a_name);
		static bool Safe_AttachNode(RE::NiNode* a_parent, RE::NiAVObject* a_child);
	};
}
