#include "pch.h"
#include "ModelManager.h"
#include <RE/B/BGSModelMaterialSwap.h>
#include <RE/T/TESAmmo.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectSTAT.h>
#include <RE/T/TESObjectWEAP.h>
#include "System/HolsterManager.h"
#include "Engine/NodeManager.h"

#include <RE/C/ConcreteFormFactory.h>
#include <RE/M/Main.h>
#include <RE/T/TESForm.h>
#include <RE/B/BGSMod.h>
#include <RE/U/UI.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESAmmo.h>
#include <RE/E/ExtraDataList.h>
#include <RE/B/BSExtraData.h>
#include <RE/E/EXTRA_DATA_TYPE.h>
#include <RE/M/MemoryManager.h>
#include <RE/E/ENUM_FORM_ID.h>         
#include <RE/N/NiCloningProcess.h>
#include <RE/N/NiLight.h>
#include <RE/N/NiUpdateData.h>
#include <RE/B/BSModelDB.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/P/ProcessLists.h>

// 👇 引入你刚刚查到的 VTABLE 定义
#include <RE/IDs_VTABLE.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <unordered_map>

namespace IAD {

	// 👇========== 🌟 完美的虚表替换魔法 ==========👇
	// 利用你找到的 VTABLE 特征码，完美复刻 stl::emplace_vtable，
	// 让引擎 100% 相信这就是原装的 ExtraCount，从而渲染出弹药盒！
	class DummyExtraCount : public RE::BSExtraData {
	public:
		F4_HEAP_REDEFINE_NEW(DummyExtraCount);

		std::int32_t count;
		std::uint32_t pad;

		DummyExtraCount(std::int32_t a_count) {
			// 强行把当前类的虚函数表指针，指向引擎原生的 ExtraCount 虚表
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::ExtraCount[0] };
			*reinterpret_cast<std::uintptr_t*>(this) = vtbl.address();

			this->type = RE::EXTRA_DATA_TYPE::kCount;
			this->count = a_count;
		}

		virtual ~DummyExtraCount() = default;
	};
	// 👆==================================================================👆

	static std::atomic<bool> g_isGameSaving{ false };
	static std::atomic<bool> g_isGameLoading{ false };
	static std::atomic<bool> g_isMainMenuTransition{ false };
	static std::atomic<bool> g_pendingLoadRefresh{ false };
	static std::atomic<std::uint64_t> g_sceneGeneration{ 1 };

	bool ModelManager::IsGameLoading() { return g_isGameLoading.load(std::memory_order_acquire); }
	bool ModelManager::IsGameSaving() { return g_isGameSaving.load(std::memory_order_acquire); }
	bool ModelManager::IsMainMenuTransition() { return g_isMainMenuTransition.load(std::memory_order_acquire); }
	std::uint64_t ModelManager::GetSceneGeneration() { return g_sceneGeneration.load(std::memory_order_acquire); }

	static std::string LowerCopy(std::string a_value) {
		std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return a_value;
	}

	static bool ContainsAny(const std::string& a_value, std::initializer_list<const char*> a_needles) {
		for (auto needle : a_needles) {
			if (a_value.find(needle) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	static bool ShouldRemoveNodeForCleanup(RE::NiAVObject* a_node, const ModelCleanupPolicy& a_cleanupPolicy) {
		if (!a_node) {
			return false;
		}

		const std::string lowerRtti = LowerCopy(a_node->GetRTTI() ? a_node->GetRTTI()->name : "");
		const std::string name = LowerCopy(a_node->name.c_str() ? a_node->name.c_str() : "");

		const bool bloodNode = ContainsAny(name, { "blood", "stain" }) || ContainsAny(lowerRtti, { "blood", "stain" });
		const bool editorMarkerNode =
			ContainsAny(lowerRtti, { "decal", "editor" }) ||
			ContainsAny(name, { "editormarker", "editor_marker", "editor marker", "marker", "decal" });
		const bool scabbardNode =
			name == "scb" ||
			name == "scbleft" ||
			ContainsAny(name, { "scabbard", "sheath", "weapon_sheath", "sheathe", "holster" });
		const bool projectileFxNode =
			ContainsAny(lowerRtti, { "particle" }) ||
			ContainsAny(name, { "fx", "tracer", "projectile", "beam", "muzzleflash", "muzzle_flash" });
		const bool lightNode = ContainsAny(lowerRtti, { "light" });
		const bool torchFlameNode =
			ContainsAny(name, { "torchfire", "torch_fire", "torch flame", "torch_flame", "flame", "fire", "burn", "pilotlight", "pilot_light" }) ||
			ContainsAny(lowerRtti, { "flame", "fire" });
		const bool uiNode =
			ContainsAny(lowerRtti, { "bsui", "scaleform" }) ||
			ContainsAny(name, { "screen", "reticle", "movie", "flash", "scaleform" }) ||
			name == "ui" ||
			name.find("_ui") != std::string::npos ||
			name.find(" ui") != std::string::npos;
		// A displayed item is a render clone, never a game-world object. Collision
		// and audio helpers can otherwise emit impacts when the actor changes pose.
		const bool runtimePhysicsOrAudioNode =
			ContainsAny(lowerRtti, { "bhk", "havok", "constraint", "cloth", "collision", "sound", "audio", "impact" }) ||
			ContainsAny(name, { "bhk", "havok", "constraint", "cloth", "collision", "sound", "audio", "impact" });

		if (a_cleanupPolicy.keepTorchFlame && torchFlameNode) {
			return false;
		}

		return runtimePhysicsOrAudioNode ||
			(a_cleanupPolicy.removeBlood && bloodNode) ||
			(a_cleanupPolicy.removeEditorMarker && editorMarkerNode) ||
			(a_cleanupPolicy.removeScabbard && scabbardNode) ||
			(a_cleanupPolicy.removeProjectileTracers && projectileFxNode) ||
			(a_cleanupPolicy.removeLights && lightNode) ||
			(a_cleanupPolicy.removeUI && uiNode);
	}

	static bool ShouldRemoveExtraForCleanup(const std::string& a_lowerExtraName, const std::string& a_lowerExtraRTTI, const ModelCleanupPolicy& a_cleanupPolicy) {
		const bool havokOrBehavior =
			ContainsAny(a_lowerExtraName, { "behavior", "bhk", "havok", "constraint", "cloth", "collision", "physics" }) ||
			ContainsAny(a_lowerExtraRTTI, { "behavior", "bhk", "havok", "constraint", "cloth", "collision", "physics" });
		const bool sound =
			ContainsAny(a_lowerExtraName, { "sound", "audio", "impact" }) ||
			ContainsAny(a_lowerExtraRTTI, { "sound", "audio", "impact" });
		const bool ui =
			ContainsAny(a_lowerExtraName, { "ui", "movie", "flash", "scaleform" }) ||
			ContainsAny(a_lowerExtraRTTI, { "ui", "movie", "flash", "scaleform" });

		return havokOrBehavior ||
			sound ||
			(a_cleanupPolicy.removeUI && ui);
	}

	static void ApplyModelAlphaToGeometry(RE::BSGeometry* a_geometry, float a_alpha) {
		if (!a_geometry) {
			return;
		}

		const float alpha = std::clamp(a_alpha, 0.0f, 1.0f);
		for (auto& propertyPtr : a_geometry->properties) {
			auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(propertyPtr.get());
			if (!shaderProperty) {
				continue;
			}

			shaderProperty->alpha = alpha;
			shaderProperty->SetMaterialAlpha(alpha);
			shaderProperty->DoClearRenderPasses();
		}
	}

	static void ApplyModelEffectToGeometry(RE::BSGeometry* a_geometry, const ModelEffectSettings& a_effect) {
		if (!a_geometry) {
			return;
		}

		const float alpha = a_effect.enabled && a_effect.alpha ?
			std::clamp(a_effect.alphaMultiplier * a_effect.fillA, 0.0f, 1.0f) :
			1.0f;

		ApplyModelAlphaToGeometry(a_geometry, alpha);

		if (!a_effect.enabled || !a_effect.lighting) {
			return;
		}

		const float fillAlpha = std::clamp(a_effect.fillA * a_effect.baseFillAlpha, 0.0f, 10.0f);
		for (auto& propertyPtr : a_geometry->properties) {
			auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(propertyPtr.get());
			if (!lightingProperty) {
				continue;
			}

			lightingProperty->projectedUVColor = { a_effect.fillR, a_effect.fillG, a_effect.fillB, fillAlpha };
			lightingProperty->projectedUVParams.a = std::clamp(a_effect.edgeExponent, 0.0f, 20.0f);
			if (lightingProperty->emitColor) {
				lightingProperty->emitColor->r = a_effect.fillR;
				lightingProperty->emitColor->g = a_effect.fillG;
				lightingProperty->emitColor->b = a_effect.fillB;
				lightingProperty->emitColorScale = std::clamp(a_effect.baseFillScale * fillAlpha, 0.0f, 20.0f);
			}
			lightingProperty->DoClearRenderPasses();
		}
	}

	static void ApplyModelLightToNode(RE::NiAVObject* a_node, const ModelLightSettings& a_light) {
		if (!a_node || !a_light.enabled) {
			return;
		}

		auto* niLight = netimmerse_cast<RE::NiLight*>(a_node);
		if (!niLight) {
			return;
		}

		const float dimmer = std::clamp(a_light.dimmer, 0.0f, 20.0f) * std::clamp(a_light.diffuseA, 0.0f, 1.0f);
		const float radius = std::clamp(a_light.radius, 0.0f, 4096.0f);
		const float r = std::clamp(a_light.diffuseR, 0.0f, 1.0f);
		const float g = std::clamp(a_light.diffuseG, 0.0f, 1.0f);
		const float b = std::clamp(a_light.diffuseB, 0.0f, 1.0f);

		niLight->SetAppCulled(false);
		niLight->amb = { 0.0f, 0.0f, 0.0f };
		niLight->diff = { r, g, b };
		niLight->spec = { r, g, b };
		niLight->dimmer = dimmer;
		niLight->modelBound.center = { 0.0f, 0.0f, 0.0f };
		niLight->modelBound.fRadius = radius;
		niLight->local.translate = { a_light.posX, a_light.posY, a_light.posZ };
		niLight->local.rotate = TransformMath::EulerToMatrix({ a_light.rotX, a_light.rotY, a_light.rotZ });
		niLight->local.scale = 1.0f;
	}

	RE::NiAVObject* ModelManager::FindNodeRecursive(RE::NiAVObject* a_root, const RE::BSFixedString& a_name) {
		if (!a_root) return nullptr;
		if (a_root->name == a_name) return a_root;
		auto niNode = a_root->IsNode();
		if (niNode) for (auto& child : niNode->children) if (child) {
			auto result = FindNodeRecursive(child.get(), a_name);
			if (result) return result;
		}
		return nullptr;
	}

	RE::NiNode* ModelManager::GetOrCreateVirtualNode(RE::NiNode* a_parent, const char* a_nodeName) {
		if (!a_parent) return nullptr;
		RE::BSFixedString nodeNameStr(a_nodeName);
		auto existing = a_parent->GetObjectByName(nodeNameStr);
		if (existing) return static_cast<RE::NiNode*>(existing);
		RE::NiCloningProcess cloning;
		cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
		cloning.appendChar = '\0';
		cloning.scale = { 1.0f, 1.0f, 1.0f };
		auto* newNode = a_parent->CreateClone(cloning);
		auto* node = newNode ? newNode->IsNode() : nullptr;
		if (node) {
			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			for (auto& child : node->children) if (child) children.push_back(child);
			for (auto& child : children) node->DetachChild(child.get());
			node->controllers.reset();
			node->extra = nullptr;
			node->collisionObject.reset();
			node->name = a_nodeName;
			node->local.translate = { 0, 0, 0 };
			node->local.rotate.MakeIdentity();
			node->local.scale = 1.0f;
			a_parent->AttachChild(node, true);
			return node;
		}
		return nullptr;
	}

	void ModelManager::SetModelAlpha(RE::NiAVObject* a_node, float a_alpha) {
		if (!a_node) return;
		const float alpha = std::clamp(a_alpha, 0.0f, 1.0f);
		a_node->fadeAmount = alpha;
		if (auto geometry = a_node->IsGeometry()) {
			ApplyModelAlphaToGeometry(geometry, alpha);
		}
		auto niNode = a_node->IsNode();
		if (niNode) for (auto& child : niNode->children) if (child) SetModelAlpha(child.get(), a_alpha);
	}

	void ModelManager::SetModelGeometryHidden(RE::NiAVObject* a_node, bool a_hidden) {
		if (!a_node) return;
		if (auto geometry = a_node->IsGeometry()) {
			geometry->SetAppCulled(a_hidden);
		}
		auto niNode = a_node->IsNode();
		if (niNode) for (auto& child : niNode->children) if (child) SetModelGeometryHidden(child.get(), a_hidden);
	}

	void ModelManager::ApplyModelEffect(RE::NiAVObject* a_node, const ModelEffectSettings& a_effect) {
		if (!a_node) return;

		const float alpha = a_effect.enabled && a_effect.alpha ?
			std::clamp(a_effect.alphaMultiplier * a_effect.fillA, 0.0f, 1.0f) :
			1.0f;

		a_node->fadeAmount = alpha;
		if (auto geometry = a_node->IsGeometry()) {
			ApplyModelEffectToGeometry(geometry, a_effect);
		}

		auto niNode = a_node->IsNode();
		if (niNode) {
			for (auto& child : niNode->children) {
				if (child) {
					ApplyModelEffect(child.get(), a_effect);
				}
			}
		}
	}

	void ModelManager::ApplyModelLight(RE::NiAVObject* a_node, const ModelLightSettings& a_light) {
		if (!a_node || !a_light.enabled) return;

		ApplyModelLightToNode(a_node, a_light);

		if (auto niNode = a_node->IsNode()) {
			for (auto& child : niNode->children) {
				if (child) {
					ApplyModelLight(child.get(), a_light);
				}
			}
		}
	}

	void ModelManager::RenameClonedNodes(RE::NiAVObject* a_node) {
		if (!a_node) return;
		if (a_node->name.c_str()) {
			std::string name(a_node->name.c_str());
			if (name.find("IAD_") != 0) {
				a_node->name = "IAD_Clone_" + name;
			}
		}
		auto niNode = a_node->IsNode();
		if (niNode) {
			for (auto& child : niNode->children) {
				if (child) RenameClonedNodes(child.get());
			}
		}
	}

	void ModelManager::FreezeToStaticStatue(RE::NiAVObject* a_node, bool a_isRoot, const ModelCleanupPolicy& a_cleanupPolicy) {
		if (!a_node) return;

		// Display clones are render-only. Runtime controllers can enqueue behavior
		// work after the temporary weapon reference has been retired.
		if (auto* objectNet = static_cast<RE::NiObjectNET*>(a_node)) {
			objectNet->controllers.reset();
		}

		if (a_isRoot) {
			a_node->SetAppCulled(false);
		}
		a_node->fadeAmount = 1.0f;

		// IAD sway is transform-only. A display clone must never enter Havok,
		// regardless of slot cleanup options.
		if (a_node->collisionObject) {
			a_node->collisionObject.reset();
		}

		if (auto niNode = a_node->IsNode()) {
			std::vector<RE::NiAVObject*> toRemove;
			for (auto& child : niNode->children) {
				if (!child) continue;

				if (ShouldRemoveNodeForCleanup(child.get(), a_cleanupPolicy)) {
					toRemove.push_back(child.get());
				}
				else {
					FreezeToStaticStatue(child.get(), false, a_cleanupPolicy);
				}
			}
			for (auto* trash : toRemove) {
				niNode->DetachChild(trash);
			}
		}
		else if (ShouldRemoveNodeForCleanup(a_node, a_cleanupPolicy)) {
			a_node->local.scale = 0.0f;
			a_node->SetAppCulled(true);
		}

		if (a_node->extra) {
			auto& extraArray = a_node->extra->extra;
			for (int i = static_cast<int>(extraArray.size()) - 1; i >= 0; --i) {
				auto extraData = extraArray[i].get();
				if (extraData) {
					const std::string lowerExName = LowerCopy(extraData->name.c_str() ? extraData->name.c_str() : "");
					const std::string lowerExRTTI = LowerCopy(extraData->GetRTTI() ? extraData->GetRTTI()->name : "");

					if (ShouldRemoveExtraForCleanup(lowerExName, lowerExRTTI, a_cleanupPolicy)) {
						if (lowerExRTTI.find("cloth") != std::string::npos) {
							REX::INFO("[DEBUG-b7c1] stripped runtime cloth extra '{}' from display clone", extraData->GetRTTI()->name);
						}
						extraArray.erase(extraArray.begin() + i);
					}
				}
			}
		}

		// Safe_AttachNode updates the stripped clone after it joins the actor tree.
	}

	void ModelManager::CullBloodNodes(RE::NiAVObject* a_node, const ModelCleanupPolicy& a_cleanupPolicy) {
		if (!a_cleanupPolicy.removeBlood) return;
		if (!a_node) return;
		if (a_node->name.c_str()) {
			std::string nodeName(a_node->name.c_str());
			std::transform(nodeName.begin(), nodeName.end(), nodeName.begin(), ::tolower);
			if (nodeName.find("blood") != std::string::npos || nodeName.find("stain") != std::string::npos) {
				a_node->local.scale = 0.0f; a_node->SetAppCulled(true);
			}
		}
		auto niNode = a_node->IsNode();
		if (niNode) for (auto& child : niNode->children) if (child) CullBloodNodes(child.get(), a_cleanupPolicy);
	}

	void ModelManager::ProcessF4SEMessage(uint32_t a_msgType) {
		if (a_msgType == F4SE::MessagingInterface::kPreSaveGame) {
			// 💡 只设标志，不做任何清理。kPreSaveGame 在 exitsave（退游戏自动存档）时
			// 从 BSJobs::JobThread 派发，跨线程操作表单 / 3D 节点必然触发 access violation。
			// 引擎随后要么终止（exitsave）要么正常继续（手动存档），无需我们介入。
			g_isGameSaving = true;
		}
		else if (a_msgType == F4SE::MessagingInterface::kPostSaveGame) {
			g_isGameSaving = false;
		}
		else if (a_msgType == F4SE::MessagingInterface::kPreLoadGame) {
			// This callback runs inside the save-load worker. Keep it strictly
			// lock-free and side-effect free: no logging, allocation, form lookup,
			// or scene access is permitted here.
			g_isGameLoading = true;
			g_sceneGeneration.fetch_add(1, std::memory_order_acq_rel);
			g_pendingLoadRefresh.store(false, std::memory_order_release);
		}
		else if (a_msgType == F4SE::MessagingInterface::kPostLoadGame || a_msgType == F4SE::MessagingInterface::kGameLoaded || a_msgType == F4SE::MessagingInterface::kNewGame) {
			g_isGameLoading = false;
			g_pendingLoadRefresh.store(true, std::memory_order_release);
		}
	}

	void ModelManager::ForceClearAll() {
		// 💡 加载/保存中跨线程：跳过 DetachChild，只清空引用。引擎随后销毁/重建场景树，节点自然回收
		const bool skipDetach = g_isGameLoading.load(std::memory_order_acquire) || g_isGameSaving.load(std::memory_order_acquire);
		if (!skipDetach) {
			for (auto* clone : _activeClones) if (clone && clone->parent) clone->parent->DetachChild(clone);
		}
		_activeClones.clear();

		std::lock_guard<std::mutex> lock(_queueMutex);
		while (!_asyncLoadQueue.empty()) {
			auto req = _asyncLoadQueue.front();
			_asyncLoadQueue.pop_front();
			if (req.tempRef) {
				if (req.originalExtra) req.tempRef->extraList = req.originalExtra;
				else req.tempRef->extraList = nullptr;

				auto form = RE::TESForm::GetFormByID(0x3B);
				if (form) req.tempRef->SetObjectReference(static_cast<RE::TESBoundObject*>(form));

				req.tempRef->formFlags |= 0x20;
				req.tempRef->SetWantsDelete(true);
			}
		}
	}

	void ModelManager::BeginMainMenuTransition() {
		if (g_isMainMenuTransition.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		g_pendingLoadRefresh.store(false, std::memory_order_release);

		// MainMenu's open event is already inside the engine's scene-reset sequence.
		// Do not mutate the actor tree here: DetachChild can race Havok behavior
		// graph teardown. The next completed load clears only IAD's non-owning state.
		_activeClones.clear();
		_gcQueue.clear();

		std::lock_guard<std::mutex> lock(_queueMutex);
		const auto discardedRequests = _asyncLoadQueue.size();
		std::deque<AsyncModelRequest> empty;
		_asyncLoadQueue.swap(empty);
		REX::INFO("[IAD Lifecycle] MainMenu teardown completed on game thread; discarded {} pending model requests", discardedRequests);
	}

	void ModelManager::RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestItemModel(a_actor, a_item, a_extractProjectile, ModelCleanupPolicy{}, a_callback);
	}

	void ModelManager::RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestItemModel(a_actor, a_item, a_extractProjectile, a_cleanupPolicy, false, a_callback);
	}

	void ModelManager::RequestItemModel(RE::Actor* a_actor, ActiveItem& a_item, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback) {
		if (!a_actor || !a_item.object || !a_item.stack) { a_callback(nullptr); return; }

		auto factory = RE::ConcreteFormFactory<RE::TESObjectREFR>::GetFormFactory();
		auto tempRef = factory ? factory->Create() : nullptr;
		if (!tempRef) { a_callback(nullptr); return; }

		tempRef->parentCell = a_actor->parentCell;
		tempRef->data.location = a_actor->data.location;
		tempRef->data.angle = a_actor->data.angle;
		tempRef->SetScale(1.0f);

		tempRef->formFlags |= (0x10 | 0x1000 | 0x2000);

		RE::BSTSmartPointer<RE::ExtraDataList> originalExtra = tempRef->extraList;
		tempRef->SetObjectReference(a_item.object);

		RE::BSTSmartPointer<RE::ExtraDataList> payloadExtra = nullptr;

		if (a_item.object->GetFormType() == RE::ENUM_FORM_ID::kAMMO) {
			auto rawExtra = new RE::ExtraDataList();
			if (!a_extractProjectile) {
				// 注入我们的完美替身，彻底解决弹药盒显示问题
				auto xCount = new DummyExtraCount(2);
				rawExtra->AddExtra(xCount);
			}
			payloadExtra.reset(rawExtra);
		}
		else {
			// The copied list preserves the selected OMODs while keeping the temporary
			// display reference separate from the inventory stack container itself.
			auto rawExtra = new RE::ExtraDataList();
			if (a_item.stack->extra) {
				rawExtra->CopyList(a_item.stack->extra.get());
			}
			payloadExtra.reset(rawExtra);
		}

		AsyncModelRequest req;
		req.tempRef = tempRef;
		req.originalExtra = originalExtra;
		req.customExtra = payloadExtra;
		req.actorFormID = a_actor->GetFormID();
		req.isMagazineExtraction = false;
		req.loadFirstPersonModel = a_loadFirstPersonModel && a_item.object->As<RE::TESObjectWEAP>() != nullptr;
		req.cleanupPolicy = a_cleanupPolicy;
		req.sceneGeneration = GetSceneGeneration();
		req.validatePath = "";
		req.callback = a_callback;

		std::lock_guard<std::mutex> lock(_queueMutex);
		_asyncLoadQueue.push_back(std::move(req));
	}

	void ModelManager::RequestModelByPath(const std::string& a_path, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestModelByPath(a_path, ModelCleanupPolicy{}, a_callback);
	}

	void ModelManager::RequestModelByPath(const std::string& a_path, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestModelByPath(0, a_path, a_cleanupPolicy, std::move(a_callback));
	}

	void ModelManager::RequestModelByPath(RE::TESFormID a_actorFormID, const std::string& a_path, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		if (a_path.empty()) { a_callback(nullptr); return; }

		AsyncModelRequest req;
		req.tempRef = nullptr;
		req.originalExtra = nullptr;
		req.customExtra = nullptr;
		req.actorFormID = a_actorFormID;
		req.isMagazineExtraction = false;
		req.cleanupPolicy = a_cleanupPolicy;
		req.sceneGeneration = GetSceneGeneration();
		req.validatePath = a_path;
		req.callback = a_callback;

		std::lock_guard<std::mutex> lock(_queueMutex);
		_asyncLoadQueue.push_back(std::move(req));
	}

	void ModelManager::RequestStaticFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		if (!a_form) {
			a_callback(nullptr);
			return;
		}

		if (auto* weapon = a_form->As<RE::TESObjectWEAP>()) {
			if (!a_actor) {
				a_callback(nullptr);
				return;
			}

			auto factory = RE::ConcreteFormFactory<RE::TESObjectREFR>::GetFormFactory();
			auto tempRef = factory ? factory->Create() : nullptr;
			if (!tempRef) {
				a_callback(nullptr);
				return;
			}

			tempRef->parentCell = a_actor->parentCell;
			tempRef->data.location = a_actor->data.location;
			tempRef->data.angle = a_actor->data.angle;
			tempRef->SetScale(1.0f);
			tempRef->formFlags |= (0x10 | 0x1000 | 0x2000);

			RE::BSTSmartPointer<RE::ExtraDataList> originalExtra = tempRef->extraList;
			auto rawExtra = new RE::ExtraDataList();
			BGSMod::Template::Items::CreateInstanceDataForObjectAndExtra(*weapon, *rawExtra, nullptr, true);
			RE::BSTSmartPointer<RE::ExtraDataList> payloadExtra(rawExtra);
			tempRef->SetObjectReference(weapon);

			AsyncModelRequest req;
			req.tempRef = tempRef;
			req.originalExtra = originalExtra;
			req.customExtra = payloadExtra;
			req.actorFormID = a_actor->GetFormID();
			req.isMagazineExtraction = false;
			req.loadFirstPersonModel = false;
			req.cleanupPolicy = a_cleanupPolicy;
			req.sceneGeneration = GetSceneGeneration();
			req.callback = std::move(a_callback);

			REX::INFO("[IAD StaticForm] player={:08X} assembling default OMOD model for {:08X}",
				a_actor->GetFormID(), weapon->GetFormID());
			std::lock_guard<std::mutex> lock(_queueMutex);
			_asyncLoadQueue.push_back(std::move(req));
			return;
		}

		const RE::TESModel* model = nullptr;
		if (auto* ammo = a_form->As<RE::TESAmmo>()) {
			model = static_cast<RE::BGSModelMaterialSwap*>(ammo);
		}
		else if (auto* misc = a_form->As<RE::TESObjectMISC>()) {
			model = static_cast<RE::BGSModelMaterialSwap*>(misc);
		}
		else if (auto* stat = a_form->As<RE::TESObjectSTAT>()) {
			model = static_cast<RE::BGSModelMaterialSwap*>(stat);
		}

		const auto path = model ? model->GetModel() : nullptr;
		if (!path || !*path) {
			REX::WARN("[IAD StaticForm] no supported base model for {:08X}", a_form->GetFormID());
			a_callback(nullptr);
			return;
		}

		REX::INFO("[IAD StaticForm] requesting direct model {:08X}: {}", a_form->GetFormID(), path);
		RequestModelByPath(a_actor ? a_actor->GetFormID() : 0, path, a_cleanupPolicy, std::move(a_callback));
	}

	void ModelManager::RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestFormModel(a_actor, a_form, a_extractMagazine, a_extractProjectile, ModelCleanupPolicy{}, a_callback);
	}

	void ModelManager::RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestFormModel(a_actor, a_form, a_extractMagazine, a_extractProjectile, a_cleanupPolicy, false, a_callback);
	}

	void ModelManager::RequestFormModel(RE::Actor* a_actor, RE::TESBoundObject* a_form, bool a_extractMagazine, bool a_extractProjectile, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback) {
		if (!a_actor || !a_form) { a_callback(nullptr); return; }

		auto factory = RE::ConcreteFormFactory<RE::TESObjectREFR>::GetFormFactory();
		auto tempRef = factory ? factory->Create() : nullptr;
		if (!tempRef) { a_callback(nullptr); return; }

		tempRef->parentCell = a_actor->parentCell;
		tempRef->data.location = a_actor->data.location;
		tempRef->data.angle = a_actor->data.angle;
		tempRef->SetScale(1.0f);
		tempRef->formFlags |= (0x10 | 0x1000 | 0x2000);

		RE::BSTSmartPointer<RE::ExtraDataList> originalExtra = tempRef->extraList;
		tempRef->SetObjectReference(a_form);
		RE::BSTSmartPointer<RE::ExtraDataList> payloadExtra = nullptr;

		if (a_form->GetFormType() == RE::ENUM_FORM_ID::kAMMO && !a_extractProjectile) {
			auto rawExtra = new RE::ExtraDataList();
			auto xCount = new DummyExtraCount(2);
			rawExtra->AddExtra(xCount);
			payloadExtra.reset(rawExtra);
		}

		AsyncModelRequest req;
		req.tempRef = tempRef;
		req.originalExtra = originalExtra;
		req.customExtra = payloadExtra;
		req.actorFormID = a_actor->GetFormID();
		req.isMagazineExtraction = a_extractMagazine;
		req.loadFirstPersonModel = a_loadFirstPersonModel && a_form->As<RE::TESObjectWEAP>() != nullptr;
		req.cleanupPolicy = a_cleanupPolicy;
		req.sceneGeneration = GetSceneGeneration();
		req.validatePath = "";
		req.callback = a_callback;

		std::lock_guard<std::mutex> lock(_queueMutex);
		_asyncLoadQueue.push_back(std::move(req));
	}

	RE::NiAVObject* ModelManager::ExtractMagazineNode(RE::NiAVObject* a_root) {
		if (!a_root) return nullptr;
		RE::NiAVObject* targetMag = nullptr;

		std::function<void(RE::NiAVObject*)> FindMag = [&](RE::NiAVObject* node) {
			if (targetMag || !node) return;
			if (node->name.c_str()) {
				std::string name = node->name.c_str();
				std::string lowerName = name;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });

				if (lowerName.find("mag") != std::string::npos || lowerName.find("clip") != std::string::npos || lowerName.find("drum") != std::string::npos) {
					if (lowerName.find("ap_") != std::string::npos || lowerName.find("attach") != std::string::npos) {
						auto niNode = node->IsNode();
						if (niNode && !niNode->children.empty() && niNode->children[0]) {
							targetMag = niNode->children[0].get(); return;
						}
					}
					else {
						targetMag = node; return;
					}
				}
			}
			auto niNode = node->IsNode();
			if (niNode) {
				for (auto& child : niNode->children) { if (child) FindMag(child.get()); }
			}
			};

		FindMag(a_root);
		return targetMag;
	}

	void ModelManager::RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestMagazineModel(a_actor, a_weapon, ModelCleanupPolicy{}, a_callback);
	}

	void ModelManager::RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, const ModelCleanupPolicy& a_cleanupPolicy, std::function<void(RE::NiAVObject*)> a_callback) {
		RequestMagazineModel(a_actor, a_weapon, a_cleanupPolicy, false, a_callback);
	}

	void ModelManager::RequestMagazineModel(RE::Actor* a_actor, ActiveItem& a_weapon, const ModelCleanupPolicy& a_cleanupPolicy, bool a_loadFirstPersonModel, std::function<void(RE::NiAVObject*)> a_callback) {
		if (a_weapon.object && a_weapon.object->As<RE::TESAmmo>()) {
			a_callback(nullptr); return;
		}

		auto factory = RE::ConcreteFormFactory<RE::TESObjectREFR>::GetFormFactory();
		auto tempRef = factory ? factory->Create() : nullptr;
		if (!tempRef) { a_callback(nullptr); return; }

		tempRef->parentCell = a_actor->parentCell;
		tempRef->data.location = a_actor->data.location;
		tempRef->data.angle = a_actor->data.angle;
		tempRef->SetScale(1.0f);

		tempRef->formFlags |= (0x10 | 0x1000 | 0x2000);

		RE::BSTSmartPointer<RE::ExtraDataList> originalExtra = tempRef->extraList;
		tempRef->SetObjectReference(a_weapon.object);

		AsyncModelRequest req;
		req.tempRef = tempRef;
		req.originalExtra = originalExtra;
		// Magazine extraction needs the same current OMOD selection as the weapon.
		auto rawExtra = new RE::ExtraDataList();
		if (a_weapon.stack && a_weapon.stack->extra) {
			rawExtra->CopyList(a_weapon.stack->extra.get());
		}
		req.customExtra.reset(rawExtra);
		req.actorFormID = a_actor->GetFormID();
		req.isMagazineExtraction = true;
		req.loadFirstPersonModel = a_loadFirstPersonModel && a_weapon.object && a_weapon.object->As<RE::TESObjectWEAP>() != nullptr;
		req.cleanupPolicy = a_cleanupPolicy;
		req.sceneGeneration = GetSceneGeneration();
		req.validatePath = "";
		req.callback = a_callback;

		std::lock_guard<std::mutex> lock(_queueMutex);
		_asyncLoadQueue.push_back(std::move(req));
	}

	void ModelManager::ProcessAsyncQueue() {
		if (g_isGameSaving || g_isGameLoading || g_isMainMenuTransition) return;

		AsyncModelRequest req;
		{
			std::lock_guard<std::mutex> lock(_queueMutex);
			if (_asyncLoadQueue.empty()) return;

			// A player weapon switch can enqueue several evaluations before the
			// main-thread model pass reaches them. Prefer the newest player-owned
			// request so stale player entries cannot delay the visible result.
			// NPC and unowned requests retain FIFO order when no player request is pending.
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto playerFormID = player ? player->GetFormID() : 0;
			auto selected = _asyncLoadQueue.begin();
			if (playerFormID != 0) {
				for (auto it = _asyncLoadQueue.end(); it != _asyncLoadQueue.begin();) {
					--it;
					if (it->actorFormID == playerFormID) {
						selected = it;
						break;
					}
				}
			}
			req = std::move(*selected);
			_asyncLoadQueue.erase(selected);
		}

		if (req.sceneGeneration != GetSceneGeneration()) {
			if (req.tempRef) {
				req.tempRef->Release3DRelatedData();
				if (req.originalExtra) req.tempRef->extraList = req.originalExtra;
				else req.tempRef->extraList = nullptr;
				if (auto* form = RE::TESForm::GetFormByID(0x3B)) {
					req.tempRef->SetObjectReference(static_cast<RE::TESBoundObject*>(form));
				}
				req.tempRef->formFlags |= 0x20;
				req.tempRef->SetWantsDelete(true);
			}
			return;
		}

		if (!req.tempRef && !req.validatePath.empty()) {
			RE::NiPointer<RE::NiNode> model;
			RE::BSModelDB::DBTraits::ArgsType args;
			std::memset(&args, 0, sizeof(args));

			args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
			args.loadTextures = 1;
			args.prepareAfterLoad = 1;
			args.performProcess = 1;
			args.createFadeNode = 1;

			auto err = RE::BSModelDB::Demand(req.validatePath.c_str(), &model, args);
			if (!model || err != RE::BSResource::ErrorCode::kNone) {
				REX::WARN("[IAD 追踪] 模型异步加载失败，路径: {}", req.validatePath);
				if (req.callback) req.callback(nullptr);
				return;
			}

			RE::NiCloningProcess cp;
			cp.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cp.appendChar = '\0';
			cp.scale = { 1.0f, 1.0f, 1.0f };

			auto clone = static_cast<RE::NiAVObject*>(model->CreateClone(cp));
			if (clone) {
				clone->local.scale = 1.0f;
				FreezeToStaticStatue(clone, true, req.cleanupPolicy);
				CullBloodNodes(clone, req.cleanupPolicy);
				if (req.callback) req.callback(clone);
			}
			else {
				if (req.callback) req.callback(nullptr);
			}
			return;
		}

		if (!req.tempRef) {
			if (req.callback) req.callback(nullptr);
			return;
		}

		req.tempRef->extraList = req.customExtra;
		auto assembledModel = req.tempRef->Load3D(req.loadFirstPersonModel);
		req.tempRef->extraList = req.originalExtra;

		RE::NiAVObject* finalClone = nullptr;

		if (assembledModel) {
			assembledModel->SetAppCulled(true);
			assembledModel->local.scale = 1.0f;
			assembledModel->local.translate = { 0, 0, 0 };
			assembledModel->local.rotate.MakeIdentity();

			RE::NiCloningProcess cp;
			cp.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cp.appendChar = '\0';
			cp.scale = { 1.0f, 1.0f, 1.0f };

			finalClone = static_cast<RE::NiAVObject*>(assembledModel->CreateClone(cp));

			if (finalClone) {
				RenameClonedNodes(finalClone);
				finalClone->local.scale = 1.0f;

				FreezeToStaticStatue(finalClone, true, req.cleanupPolicy);
				CullBloodNodes(finalClone, req.cleanupPolicy);

				if (req.isMagazineExtraction) {
					auto magNode = ExtractMagazineNode(finalClone);
					if (magNode) {
						RE::NiCloningProcess cp2;
						cp2.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
						cp2.appendChar = '\0';
						cp2.scale = { 1.0f, 1.0f, 1.0f };

						auto clonedMag = static_cast<RE::NiAVObject*>(magNode->CreateClone(cp2));
						if (clonedMag) {
							RenameClonedNodes(clonedMag);
							FreezeToStaticStatue(clonedMag, true, req.cleanupPolicy);
							clonedMag->local.translate = { 0, 0, 0 };
							clonedMag->local.rotate.MakeIdentity();
							clonedMag->local.scale = 1.0f;
							clonedMag->SetAppCulled(false);

							RE::NiPointer<RE::NiAVObject> trash(finalClone);
							finalClone = clonedMag;
						}
						else {
							RE::NiPointer<RE::NiAVObject> trash(finalClone);
							finalClone = nullptr;
						}
					}
					else {
						RE::NiPointer<RE::NiAVObject> trash(finalClone);
						finalClone = nullptr;
					}
				}
			}
		}

		req.tempRef->Release3DRelatedData();
		if (req.originalExtra) req.tempRef->extraList = req.originalExtra;
		else req.tempRef->extraList = nullptr;

		auto form = RE::TESForm::GetFormByID(0x3B);
		if (form) req.tempRef->SetObjectReference(static_cast<RE::TESBoundObject*>(form));

		req.tempRef->formFlags |= 0x20;
		req.tempRef->SetWantsDelete(true);

		if (req.callback) {
			req.callback(finalClone);
		}
	}

	void ModelManager::ProcessGarbageCollection() {
		if (g_isGameSaving || g_isGameLoading) return;
		if (g_isMainMenuTransition && !g_pendingLoadRefresh.load(std::memory_order_acquire)) return;

		if (g_pendingLoadRefresh.load(std::memory_order_acquire)) {
			auto ui = RE::UI::GetSingleton();
			auto player = RE::PlayerCharacter::GetSingleton();

			if (ui && !ui->GetMenuOpen("LoadingMenu") && player && player->GetFullyLoaded3D() != nullptr) {
				REX::INFO("[IAD 追踪] 加载黑屏结束，3D数据已就绪，正在触发刷新");
				g_pendingLoadRefresh.store(false, std::memory_order_release);
				const bool wasMainMenuTransition = g_isMainMenuTransition.exchange(false, std::memory_order_acq_rel);
				if (wasMainMenuTransition) {
					// MainMenu destroys the old actor scene before this point. Reset only
					// non-owning IAD state now, on the game thread, without DetachChild.
					IAD::HolsterManager::GetSingleton()->ClearAllActorSlots(true);
					REX::INFO("[IAD Lifecycle] MainMenu scene-state reset completed on game thread");
				} else {
					// An in-game load keeps the actor scene tree alive. Detach every old
					// display clone before the restored equipment is evaluated, otherwise
					// the new models stack on stale clones from the dead save state.
					IAD::HolsterManager::GetSingleton()->ClearAllActorSlots(false);
					REX::INFO("[IAD Lifecycle] in-game load scene-state reset completed on game thread");
				}
				IAD::NodeManager::ClearAllCaches();

				auto hm = IAD::HolsterManager::GetSingleton();
				hm->ForceRefreshAll();

				auto taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([]() {
						auto p = RE::PlayerCharacter::GetSingleton();
						if (p) {
							REX::INFO("[IAD 追踪] 正在评估玩家装备状态");
							IAD::HolsterManager::GetSingleton()->RequestEvaluate(p->GetFormID());
						}
						});
				}
			}
		}

		auto now = std::chrono::steady_clock::now();
		for (auto it = _gcQueue.begin(); it != _gcQueue.end(); ) {
			if (now >= it->expirationTime) {
				if (it->ref) {
					it->ref->formFlags |= 0x20;
					it->ref->SetWantsDelete(true);
				}
				it = _gcQueue.erase(it);
			}
			else ++it;
		}
	}
}
