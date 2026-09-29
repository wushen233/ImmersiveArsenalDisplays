#include "pch.h"
#include "NodeManager.h"
#include "Data/ConfigManager.h"
#include "Engine/ConditionSystem.h"
#include "System/HolsterManager.h"
#include "ModelManager.h"
#include <cmath>
#include <algorithm>

// 👇========== 🌟 只更新头文件路径 ==========👇
#include <RE/N/NiNode.h>
#include <RE/N/NiCloningProcess.h>
#include <RE/B/BSModelDB.h>
#include <RE/A/Actor.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESBoundAnimObject.h>
#include <RE/T/TESNPC.h> 

#undef ERROR // 防止宏冲突
// 👆========================================👆

namespace IAD
{
		namespace {
		RE::NiAVObject* Safe_GetObjectByName(RE::NiNode* a_root, const RE::BSFixedString* a_name) {
			__try { return a_root->GetObjectByName(*a_name); }
			__except (1) { return nullptr; }
		}

		void Safe_DetachManagedNode(NodeManager::ManagedNode& a_managed) {
			auto* node = a_managed.node;
			if (!node) return;
			__try {
				if (node->parent) node->parent->DetachChild(node);
				node->SetAppCulled(true);
				node->local.scale = 0.0f;
			}
			__except (1) {}
		}

		RE::NiNode* CreateEngineOwnedEmptyNode(RE::NiNode* a_prototype) {
			if (!a_prototype) return nullptr;

			RE::NiCloningProcess cloning;
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '\0';
			cloning.scale = { 1.0f, 1.0f, 1.0f };

			auto* clone = a_prototype->CreateClone(cloning);
			auto* node = clone ? clone->IsNode() : nullptr;
			if (!node) return nullptr;

		// A clone has engine-owned allocation, but must not inherit the source
		// bone's children or animation controllers.
		std::vector<RE::NiPointer<RE::NiAVObject>> children;
		for (auto& child : node->children) {
			if (child) children.push_back(child);
		}
		for (auto& child : children) {
			node->DetachChild(child.get());
		}
		node->controllers.reset();
		node->extra = nullptr;
		node->collisionObject.reset();
			node->parent = nullptr;
			return node;
		}

	}

	std::unordered_map<RE::TESFormID, NodeManager::ActorNodeCache> NodeManager::_nodeCache;
	std::unordered_map<RE::TESFormID, std::uint64_t> NodeManager::_actor3DGenerations;
	std::mutex NodeManager::_cacheMutex;
	std::unordered_map<std::string, std::unordered_map<std::string, RE::NiMatrix3>> NodeManager::_pathBasedBoneDicts;
	std::mutex NodeManager::_dictMutex;

	bool NodeManager::Safe_AttachNode(RE::NiNode* a_parent, RE::NiAVObject* a_child) {
		if (!a_parent || !a_child) return false;
		__try {
			a_parent->AttachChild(a_child, false);

			RE::NiUpdateData ctx;
			ctx.flags = 0;
			a_child->Update(ctx);

			return true;
		}
		__except (1) { return false; }
	}

	void NodeManager::ClearCache(RE::TESFormID a_formID) {
		std::lock_guard<std::mutex> lock(_cacheMutex);
		// This cache observes actor-owned IAD nodes. The next injection pass looks
		// them up by name and reuses them, so cache invalidation must never detach
		// a live scene node during UI edits, 3D replacement, or load transitions.
		_nodeCache.erase(a_formID);
	}

	void NodeManager::ForgetCache(RE::TESFormID a_formID) {
		std::lock_guard<std::mutex> lock(_cacheMutex);
		_nodeCache.erase(a_formID);
	}

	void NodeManager::ClearAllCaches() {
		std::lock_guard<std::mutex> lock(_cacheMutex);
		_nodeCache.clear();
	}

	void NodeManager::InvalidateForConfigRefresh() {
		std::lock_guard<std::mutex> lock(_cacheMutex);
		for (auto& [formID, cache] : _nodeCache) {
			(void)formID;
			// Configuration edits need the next evaluation to resolve new target
			// bones and names, but they do not replace the actor scene graph.
			cache.activeNodes.clear();
			cache.hasInjectedMounts = false;
		}
	}

	std::uint64_t NodeManager::GetActor3DGeneration(RE::TESFormID a_formID) {
		if (a_formID == 0) return 0;
		std::lock_guard<std::mutex> lock(_cacheMutex);
		auto it = _actor3DGenerations.find(a_formID);
		return it != _actor3DGenerations.end() ? it->second : 0;
	}

	void NodeManager::DetachAllManagedNodesForSceneTeardown() {
		std::lock_guard<std::mutex> lock(_cacheMutex);
		for (auto& [formID, cache] : _nodeCache) {
			// A MOV may own cloned weapon geometry. Remove it before its CME mount so
			// actor skeleton teardown never has to traverse IAD nodes.
			for (auto& [name, managed] : cache.activeNodes) {
				if (name.starts_with("IAD_MOV_")) {
					Safe_DetachManagedNode(managed);
				}
			}
			for (auto& [name, managed] : cache.activeNodes) {
				if (!name.starts_with("IAD_MOV_")) {
					Safe_DetachManagedNode(managed);
				}
			}
		}
	}

	void NodeManager::RemoveManagedNode(RE::Actor* a_actor, const std::string& a_name) {
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(_cacheMutex);
		auto it = _nodeCache.find(a_actor->GetFormID());
		if (it != _nodeCache.end()) {
			auto& activeNodes = it->second.activeNodes;
			auto nodeIt = activeNodes.find(a_name);
			if (nodeIt != activeNodes.end()) {
				Safe_DetachManagedNode(nodeIt->second);
				activeNodes.erase(nodeIt);
			}
		}
	}

	void NodeManager::EnsureBoneDictionaryForPath(const std::string& a_nifPath) {
		if (a_nifPath.empty()) return;
		static std::set<std::string> failedPaths;
		{
			std::lock_guard<std::mutex> lock(_dictMutex);
			if (_pathBasedBoneDicts.count(a_nifPath) || failedPaths.count(a_nifPath)) return;
		}

		RE::NiPointer<RE::NiNode> rootNode;
		RE::BSModelDB::DBTraits::ArgsType args;
		std::memset(&args, 0, sizeof(args));
		args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
		args.loadLevel = 0;
		args.loadTextures = 0;

		auto err = RE::BSModelDB::Demand(a_nifPath.c_str(), &rootNode, args);
		if (err != RE::BSResource::ErrorCode::kNone || !rootNode) {
			REX::ERROR("[IAD] BSModelDB 无法加载骨骼模型: {}", a_nifPath);
			std::lock_guard<std::mutex> lock(_dictMutex);
			failedPaths.insert(a_nifPath);
			return;
		}

		RE::NiUpdateData ctx;
		ctx.flags = 0x1;
		rootNode->UpdateTransforms(ctx);

		std::unordered_map<std::string, RE::NiMatrix3> newDict;

		std::function<void(RE::NiAVObject*)> ExtractPurePose = [&](RE::NiAVObject* obj) {
			if (!obj) return;
			if (obj->name.c_str() && strlen(obj->name.c_str()) > 0) {
				std::string bName = obj->name.c_str();
				if (bName.find("IAD_") == std::string::npos) {
					newDict[bName] = TransformMath::Inverse(obj->world.rotate);
				}
			}
			if (auto niNode = obj->IsNode()) {
				for (std::uint16_t i = 0; i < niNode->children.size(); ++i) {
					auto& child = niNode->children[i];
					if (child) ExtractPurePose(child.get());
				}
			}
			};

		ExtractPurePose(rootNode.get());

		std::lock_guard<std::mutex> lock(_dictMutex);
		_pathBasedBoneDicts[a_nifPath] = std::move(newDict);
		REX::INFO("[IAD] 种族骨骼字典已就绪：{} (记录 {} 根骨骼)", a_nifPath, _pathBasedBoneDicts[a_nifPath].size());
	}

	bool NodeManager::Is3DSafeAndCacheReady(RE::Actor* a_actor, uint64_t a_currentTick) {
		(void)a_currentTick;
		if (!a_actor) return false;
		RE::TESFormID actorID = a_actor->GetFormID();

		const bool actorUnavailable = a_actor->IsDead(false) || a_actor->IsDeleted() || a_actor->IsDisabled();
		if (actorUnavailable) {
			bool needsCleanup = false;
			{
				std::lock_guard<std::mutex> lock(_cacheMutex);
				auto it = _nodeCache.find(actorID);
				if (it != _nodeCache.end()) {
					it->second.activeNodes.clear();
					it->second.root3D = nullptr;
					it->second.coreBone = nullptr;
					it->second.wasDead = true;
					it->second.hasInjectedMounts = false;
					needsCleanup = true;
				}
			}
			if (needsCleanup) {
				HolsterManager::GetSingleton()->ClearActorSlots(actorID, true);
			}
			return false;
		}

		auto root = static_cast<RE::NiNode*>(a_actor->Get3D(false));
		if (!root) return false;

		bool needsCleanup = false;
		bool ready = false;

		{
			std::lock_guard<std::mutex> lock(_cacheMutex);
			auto& cache = _nodeCache[actorID];

			bool stateChanged = cache.wasDead;
			cache.wasDead = false;

			if (stateChanged || cache.root3D != root) {
				needsCleanup = true;
				cache.root3D = root;
				cache.actor3DGeneration = ++_actor3DGenerations[actorID];
				RE::BSFixedString spineName("SPINE2");
				cache.coreBone = static_cast<RE::NiNode*>(Safe_GetObjectByName(root, &spineName));
				cache.hasInjectedMounts = false;
				cache.activeNodes.clear();
			}
			else {
				ready = (cache.coreBone != nullptr);
			}
		}

		// 🌟 修复：在 _cacheMutex 作用域之外调用 ClearActorSlots，消除锁顺序反转
		if (needsCleanup) {
			HolsterManager::GetSingleton()->ClearActorSlots(actorID, true);
			HolsterManager::GetSingleton()->RequestEvaluate(actorID);
			REX::INFO("[IAD Lifecycle] actor {:08X} 3D root replaced; queued display rebuild", actorID);
			return false;
		}

		return ready;
	}

	std::optional<NodeManager::ManagedNode> NodeManager::GetManagedNode(RE::Actor* a_actor, const std::string& a_name) {
		if (!a_actor) return std::nullopt;
		std::lock_guard<std::mutex> lock(_cacheMutex);
		auto it = _nodeCache.find(a_actor->GetFormID());
		if (it != _nodeCache.end()) {
			auto nodeIt = it->second.activeNodes.find(a_name);
			if (nodeIt != it->second.activeNodes.end() && nodeIt->second.node) {
				return nodeIt->second;
			}
		}
		return std::nullopt;
	}

	namespace TransformMath {
		RE::NiMatrix3 EulerToMatrix(const RE::NiPoint3& a_angles) {
			float x = a_angles.x * (3.14159265f / 180.0f); float y = a_angles.y * (3.14159265f / 180.0f); float z = a_angles.z * (3.14159265f / 180.0f);
			float sx = sinf(x), cx = cosf(x); float sy = sinf(y), cy = cosf(y); float sz = sinf(z), cz = cosf(z);
			RE::NiMatrix3 mX, mY, mZ;
			mX.entry[0][0] = 1; mX.entry[0][1] = 0;   mX.entry[0][2] = 0;
			mX.entry[1][0] = 0; mX.entry[1][1] = cx;  mX.entry[1][2] = sx;
			mX.entry[2][0] = 0; mX.entry[2][1] = -sx; mX.entry[2][2] = cx;
			mY.entry[0][0] = cy;  mY.entry[0][1] = 0; mY.entry[0][2] = -sy;
			mY.entry[1][0] = 0;   mY.entry[1][1] = 1; mY.entry[1][2] = 0;
			mY.entry[2][0] = sy;  mY.entry[2][1] = 0; mY.entry[2][2] = cy;
			mZ.entry[0][0] = cz;  mZ.entry[0][1] = sz;  mZ.entry[0][2] = 0;
			mZ.entry[1][0] = -sz; mZ.entry[1][1] = cz;  mZ.entry[1][2] = 0;
			mZ.entry[2][0] = 0;   mZ.entry[2][1] = 0;   mZ.entry[2][2] = 1;
			return Multiply(mZ, Multiply(mX, mY));
		}

		RE::NiMatrix3 Multiply(const RE::NiMatrix3& a, const RE::NiMatrix3& b) {
			RE::NiMatrix3 res;
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					res.entry[r][c] = a.entry[r][0] * b.entry[0][c] + a.entry[r][1] * b.entry[1][c] + a.entry[r][2] * b.entry[2][c];
				}
			}
			return res;
		}

		RE::NiPoint3 Multiply(const RE::NiMatrix3& m, const RE::NiPoint3& v) {
			return {
				v.x * m.entry[0][0] + v.y * m.entry[1][0] + v.z * m.entry[2][0],
				v.x * m.entry[0][1] + v.y * m.entry[1][1] + v.z * m.entry[2][1],
				v.x * m.entry[0][2] + v.y * m.entry[1][2] + v.z * m.entry[2][2]
			};
		}

		RE::NiMatrix3 Inverse(const RE::NiMatrix3& m) {
			RE::NiMatrix3 res;
			res.entry[0][0] = m.entry[0][0]; res.entry[0][1] = m.entry[1][0]; res.entry[0][2] = m.entry[2][0];
			res.entry[1][0] = m.entry[0][1]; res.entry[1][1] = m.entry[1][1]; res.entry[1][2] = m.entry[2][1];
			res.entry[2][0] = m.entry[0][2]; res.entry[2][1] = m.entry[1][2]; res.entry[2][2] = m.entry[2][2];
			return res;
		}

		RE::NiPoint3 Add(const RE::NiPoint3& a, const RE::NiPoint3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		RE::NiPoint3 Subtract(const RE::NiPoint3& a, const RE::NiPoint3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

		void ApplyAdvancedTransform(RE::Actor* a_actor, RE::NiNode* a_node, const RE::NiTransform& a_orig, const TransformData& a_gui, bool a_absolute) {
			if (!a_actor || !a_node || !a_node->parent) return;
			RE::NiTransform finalXfrm = a_orig;
			finalXfrm.scale = std::clamp(finalXfrm.scale * a_gui.scale, 0.01f, 10.0f);
			RE::NiMatrix3 guiRotMat = EulerToMatrix(a_gui.rot);
			finalXfrm.rotate = Multiply(finalXfrm.rotate, guiRotMat);
			RE::NiPoint3 offsetPos = a_gui.pos;

			if (a_absolute) {
				auto root = a_actor->Get3D(false);
				if (root) {
					RE::NiMatrix3 invParentRot = Inverse(a_node->parent->world.rotate);
					RE::NiMatrix3 rootRot = root->world.rotate;
					RE::NiMatrix3 desiredWorldRot = Multiply(rootRot, guiRotMat);
					finalXfrm.rotate = Multiply(desiredWorldRot, invParentRot);
					RE::NiPoint3 desiredWorldPos = Add(root->world.translate, Multiply(rootRot, offsetPos));
					RE::NiPoint3 diff = Subtract(desiredWorldPos, a_node->parent->world.translate);
					finalXfrm.translate = Multiply(invParentRot, diff);
					a_node->local = finalXfrm;
					return;
				}
			}
			finalXfrm.translate = Add(finalXfrm.translate, Multiply(finalXfrm.rotate, offsetPos));
			a_node->local = finalXfrm;
		}
	}

	RE::NiNode* NodeManager::GetNodeByName(RE::NiNode* a_root, const std::string& a_name)
	{
		if (!a_root) return nullptr;
		RE::BSFixedString searchName(a_name);
		auto obj = Safe_GetObjectByName(a_root, &searchName);
		return obj ? static_cast<RE::NiNode*>(obj) : nullptr;
	}

	RE::NiNode* NodeManager::InjectCMENode(RE::Actor* a_actor, const std::string& a_cmeName, const std::vector<std::string>& a_fallbackTargetNodes)
	{
		if (!a_actor) return nullptr;
		auto race = a_actor->race;

		// 🌟 修复：使用 data.objectReference 替代老版的 GetActorBase
		auto base = a_actor->data.objectReference ? a_actor->data.objectReference->As<RE::TESNPC>() : nullptr;
		if (!race || !base) return nullptr;

		int sex = (base->GetSex() == RE::SEX::kFemale) ? 1 : 0;
		std::string nifPath = race->skeletonModel[sex].model.c_str();

		EnsureBoneDictionaryForPath(nifPath);

		std::string finalName = a_cmeName;
		if (finalName.find("IAD_CME_") != 0) finalName = "IAD_CME_" + finalName;

		auto rootNode = static_cast<RE::NiNode*>(a_actor->Get3D(false));
		if (!rootNode) return nullptr;

		std::lock_guard<std::mutex> lock(_cacheMutex);
		auto& cache = _nodeCache[a_actor->GetFormID()];

		RE::NiNode* targetHost = nullptr;
		for (const auto& targetName : a_fallbackTargetNodes) {
			targetHost = GetNodeByName(rootNode, targetName);
			if (targetHost) break;
		}
		if (!targetHost) return nullptr;

		RE::NiMatrix3 perfectRot;
		perfectRot.MakeIdentity();
		{
			std::lock_guard<std::mutex> dictLock(_dictMutex);
			auto itDict = _pathBasedBoneDicts.find(nifPath);
			if (itDict != _pathBasedBoneDicts.end()) {
				std::string hostName = targetHost->name.c_str() ? targetHost->name.c_str() : "";
				if (itDict->second.count(hostName)) {
					perfectRot = itDict->second[hostName];
				}
			}
		}

		if (auto existingCME = GetNodeByName(rootNode, finalName)) {
			if (existingCME->parent != targetHost) Safe_AttachNode(targetHost, existingCME);
			existingCME->local.translate = { 0, 0, 0 };
			existingCME->local.rotate = perfectRot;
			existingCME->local.scale = 1.0f;
			RE::NiTransform pureOrig; pureOrig.translate = { 0,0,0 }; pureOrig.rotate = perfectRot; pureOrig.scale = 1.0f;
			cache.activeNodes[finalName] = { existingCME, pureOrig };
			return existingCME;
		}

		auto newCME = CreateEngineOwnedEmptyNode(targetHost);
		if (newCME) {
			newCME->name = RE::BSFixedString(finalName);
			newCME->local.translate = { 0, 0, 0 };
			newCME->local.rotate = perfectRot;
			newCME->local.scale = 1.0f;
			if (Safe_AttachNode(targetHost, newCME)) {
				RE::NiTransform pureOrig; pureOrig.translate = { 0,0,0 }; pureOrig.rotate = perfectRot; pureOrig.scale = 1.0f;
				cache.activeNodes[finalName] = { newCME, pureOrig };
				return newCME;
			}
			newCME->SetAppCulled(true);
		}
		return nullptr;
	}

	RE::NiNode* NodeManager::GetOrCreateMOVNode(RE::Actor* a_actor, RE::NiNode* a_cmeNode, const std::string& a_movName)
	{
		if (!a_cmeNode || !a_actor) return nullptr;
		std::string finalName = a_movName;
		if (finalName.find("IAD_MOV_") != 0) finalName = "IAD_MOV_" + finalName;

		std::lock_guard<std::mutex> lock(_cacheMutex);
		auto& cache = _nodeCache[a_actor->GetFormID()];

		if (cache.activeNodes.count(finalName) && cache.activeNodes[finalName].node) {
			auto* existingNode = cache.activeNodes[finalName].node;
			if (existingNode->parent != a_cmeNode) {
				Safe_AttachNode(a_cmeNode, existingNode);
			}
			return existingNode;
		}

		if (auto existingMOV = GetNodeByName(a_cmeNode, finalName)) {
			if (existingMOV->parent != a_cmeNode) Safe_AttachNode(a_cmeNode, existingMOV);
			RE::NiTransform identityOrig; identityOrig.translate = { 0,0,0 }; identityOrig.rotate.MakeIdentity(); identityOrig.scale = 1.0f;
			cache.activeNodes[finalName] = { existingMOV, identityOrig };
			return existingMOV;
		}

		auto newMOV = CreateEngineOwnedEmptyNode(a_cmeNode);
		if (newMOV) {
			newMOV->name = RE::BSFixedString(finalName);
			newMOV->local.translate = { 0, 0, 0 };
			newMOV->local.rotate.MakeIdentity();
			newMOV->local.scale = 1.0f;
			if (Safe_AttachNode(a_cmeNode, newMOV)) {
				RE::NiTransform identityOrig; identityOrig.translate = { 0,0,0 }; identityOrig.rotate.MakeIdentity(); identityOrig.scale = 1.0f;
				cache.activeNodes[finalName] = { newMOV, identityOrig };
				return newMOV;
			}
			newMOV->SetAppCulled(true);
		}
		return nullptr;
	}
}
