#include "pch.h"

#include "DetachedPreviewScene.h"

#include "ModelManager.h"
#include "Engine/NodeManager.h"

#include <RE/A/Actor.h>
#include <RE/B/BipedAnim.h>
#include <RE/B/BSFadeNode.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSModelDB.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/B/bhkWorld.h>
#include <RE/I/Interface3D.h>
#include <RE/N/NiCloningProcess.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiUpdateData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESRace.h>

#include "RE_BSSkin.h"

#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace IAD::UI {
	namespace {
		constexpr auto kRendererName = "IAD_DetachedPreview";
		constexpr auto kDisplayMeshPath = "Interface/GunModMenu/ModMenuRenderMesh.nif";
		constexpr auto kDisplayMeshGeometry = "ModMenuRenderMesh:0";
		constexpr float kFrameDistance = 220.0f;
		constexpr float kPreviewScale = 0.45f;

		void ForEachAVObject(RE::NiAVObject* a_object, const std::function<void(RE::NiAVObject&)>& a_fn)
		{
			if (!a_object) return;
			a_fn(*a_object);
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					if (child) ForEachAVObject(child.get(), a_fn);
				}
			}
		}

		RE::NiPointer<RE::NiAVObject> CloneExact(RE::NiAVObject& a_source)
		{
			RE::NiCloningProcess cloning;
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '$';
			cloning.scale = { 1.0f, 1.0f, 1.0f };

			std::vector<std::pair<RE::NiAVObject*, RE::NiPointer<RE::NiTimeController>>> detached;
			ForEachAVObject(std::addressof(a_source), [&](RE::NiAVObject& a_object) {
				if (a_object.controllers) {
					detached.emplace_back(std::addressof(a_object), a_object.controllers);
					a_object.controllers.reset();
				}
			});

			RE::NiObject* clone = a_source.CreateClone(cloning);
			a_source.ProcessClone(cloning);
			for (auto& [object, controller] : detached) {
				if (object) object->controllers = controller;
			}
			return clone ? RE::NiPointer<RE::NiAVObject>(static_cast<RE::NiAVObject*>(clone)) : nullptr;
		}

		RE::BSFlattenedBoneTree* FindFlattenedBoneTree(RE::NiAVObject* a_object);
		void SanitizeClone(RE::NiAVObject& a_root, RE::NiAVObject* a_source);

		// A whole live-player-root clone is not a valid detached-preview source in Fallout 4. Its runtime
		// cloth/ragdoll extras own engine-private state that is only valid while attached to the actor. The
		// IED/PIP-OS architecture avoids that lifetime boundary by loading a clean race skeleton and cloning each
		// already-built Biped part into it with skin mappings seeded before NiCloningProcess runs.
		using PreviewNodeMap = std::unordered_map<std::string, RE::NiAVObject*>;

		void CollectPreviewNodes(RE::NiAVObject& a_root, PreviewNodeMap& a_nodes)
		{
			a_nodes.clear();
			// Use only the ordinary cloned node hierarchy. A BSFlattenedBoneTree copied from the resource cache can
			// retain non-owning node pointers until the engine rebuilds its private map; that rebuild is not valid for
			// this detached resource-only tree on the current Fallout 4 runtime.
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				const char* name = a_object.GetName().c_str();
				if (name && name[0] != '\0') { a_nodes.try_emplace(name, std::addressof(a_object)); }
			});
		}

		RE::NiNode* FindPreviewNode(const PreviewNodeMap& a_nodes, std::string_view a_name)
		{
			const auto it = a_nodes.find(std::string(a_name));
			return it == a_nodes.end() || !it->second ? nullptr : it->second->IsNode();
		}

		RE::NiStringExtraData* FindStringExtraDataInTree(
			RE::NiAVObject& a_root, const RE::BSFixedString& a_name)
		{
			RE::NiStringExtraData* result = nullptr;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (!result) { result = netimmerse_cast<RE::NiStringExtraData*>(a_object.GetExtraData(a_name)); }
			});
			return result;
		}

		void AttachChildLikeEngine(RE::NiAVObject& a_child, RE::NiNode& a_parent)
		{
			if (a_child.parent == std::addressof(a_parent)) { return; }
			RE::NiPointer<RE::NiAVObject> keepAlive(std::addressof(a_child));
			if (auto* oldParent = a_child.parent) { oldParent->DetachChild(std::addressof(a_child)); }
			a_parent.AttachChild(keepAlive.get(), true);
		}

		[[nodiscard]] bool IsEngineWeaponAttachSlot(const RE::BIPED_OBJECT a_slot)
		{
			const auto slot = std::to_underlying(a_slot);
			return slot >= 32 && (slot <= 39 || (slot >= 41 && slot <= 43));
		}

		RE::NiPointer<RE::NiAVObject> CloneBipedPartToFreshSkeleton(
			RE::NiAVObject& a_source, RE::NiAVObject& a_previewRoot, const PreviewNodeMap& a_previewNodes)
		{
			RE::NiCloningProcess cloning;
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '$';
			cloning.scale = { 1.0f, 1.0f, 1.0f };

			// BSSkin stores raw bone and world-transform pointers. Seed both the skin root and every named bone
			// before cloning, so no source-player palette pointer is ever published in the detached part.
			ForEachAVObject(std::addressof(a_source), [&](RE::NiAVObject& a_object) {
				auto* geometry = netimmerse_cast<RE::BSGeometry*>(std::addressof(a_object));
				auto* skin = geometry ? geometry->skinInstance.get() : nullptr;
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) { return; }
				if (skin->rootNode) { cloning.cloneMap.emplace(skin->rootNode, std::addressof(a_previewRoot)); }
				for (auto* sourceBone : skin->bones) {
					if (!sourceBone) { continue; }
					const char* name = sourceBone->GetName().c_str();
					if (!name || name[0] == '\0') { continue; }
					if (const auto it = a_previewNodes.find(name); it != a_previewNodes.end() && it->second) {
						cloning.cloneMap.emplace(sourceBone, it->second);
					}
				}
			});

			std::vector<std::pair<RE::NiAVObject*, RE::NiPointer<RE::NiTimeController>>> detached;
			ForEachAVObject(std::addressof(a_source), [&](RE::NiAVObject& a_object) {
				if (a_object.controllers) {
					detached.emplace_back(std::addressof(a_object), a_object.controllers);
					a_object.controllers.reset();
				}
			});
			RE::NiObject* clone = a_source.CreateClone(cloning);
			a_source.ProcessClone(cloning);
			for (auto& [object, controller] : detached) {
				if (object) { object->controllers = controller; }
			}
			return clone ? RE::NiPointer<RE::NiAVObject>(static_cast<RE::NiAVObject*>(clone)) : nullptr;
		}

		RE::NiPointer<RE::NiNode> LoadFreshRaceSkeleton(RE::PlayerCharacter& a_player)
		{
			auto* race = a_player.GetVisualsRace();
			if (!race) {
				REX::WARN("[IAD Preview] detached fresh skeleton: player race unavailable");
				return nullptr;
			}
			const auto sex = std::min<std::uint32_t>(static_cast<std::uint32_t>(a_player.GetSex()), 1);
			const char* skeletonPath = race->skeletonModel[sex].GetModel();
			if (!skeletonPath || skeletonPath[0] == '\0') { skeletonPath = race->skeletonModel[0].GetModel(); }
			if (!skeletonPath || skeletonPath[0] == '\0') { skeletonPath = race->skeletonModel[1].GetModel(); }
			if (!skeletonPath || skeletonPath[0] == '\0') {
				REX::WARN("[IAD Preview] detached fresh skeleton: race skeleton path unavailable");
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> loadedRoot;
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.loadLevel = 3;
			args.prepareAfterLoad = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			const auto result = RE::BSModelDB::Demand(skeletonPath, std::addressof(loadedRoot), args);
			if (result != RE::BSResource::ErrorCode::kNone || !loadedRoot) {
				REX::WARN("[IAD Preview] detached fresh skeleton load failed: '{}' ({})",
					skeletonPath, std::to_underlying(result));
				return nullptr;
			}

			auto cloned = CloneExact(*loadedRoot);
			auto* previewRoot = cloned ? cloned->IsNode() : nullptr;
			if (!previewRoot) {
				REX::WARN("[IAD Preview] detached fresh skeleton clone is not a NiNode");
				return nullptr;
			}

			REX::INFO("[IAD Preview] detached fresh race skeleton loaded: '{}' (resource-only node mapping)", skeletonPath);
			return RE::NiPointer<RE::NiNode>(previewRoot);
		}

		struct FreshBipedBuildResult {
			std::uint32_t expected{ 0 };
			std::uint32_t attached{ 0 };
		};

		FreshBipedBuildResult PopulateFreshSkeletonFromBiped(
			RE::NiNode& a_previewRoot, const RE::BipedAnim& a_biped)
		{
			PreviewNodeMap previewNodes;
			CollectPreviewNodes(a_previewRoot, previewNodes);
			FreshBipedBuildResult result;
			std::unordered_set<RE::NiAVObject*> seenSources;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto slot = static_cast<RE::BIPED_OBJECT>(i);
				const auto& sourceObject = a_biped.object[i];
				auto* sourcePart = sourceObject.partClone.get();
				if (!sourcePart || !seenSources.insert(sourcePart).second) { continue; }
				++result.expected;
				auto clone = CloneBipedPartToFreshSkeleton(*sourcePart, a_previewRoot, previewNodes);
				if (!clone) { continue; }

				const auto* sourceForm = sourceObject.parent.object;
				const bool weaponAttach = sourceForm && sourceForm->Is(RE::ENUM_FORM_ID::kWEAP) &&
					(IsEngineWeaponAttachSlot(slot) || slot == RE::BIPED_OBJECT::kShield);
				RE::NiNode* parent = nullptr;
				std::string parentName;
				if (auto* prn = FindStringExtraDataInTree(*sourcePart, RE::BSFixedString("Prn"));
					prn && !prn->GetValue().empty()) {
					parentName = prn->GetValue().c_str();
					parent = FindPreviewNode(previewNodes, parentName);
				}
				if (!parent) {
					for (auto* sourceParent = sourcePart->parent; sourceParent && !parent;
						sourceParent = sourceParent->parent) {
						const char* name = sourceParent->GetName().c_str();
						if (name && name[0] != '\0') { parent = FindPreviewNode(previewNodes, name); }
					}
				}
				if (!parent && weaponAttach) {
					parent = slot == RE::BIPED_OBJECT::kShield ?
						FindPreviewNode(previewNodes, "WeaponLeft") : FindPreviewNode(previewNodes, "Weapon");
					if (!parent) { parent = FindPreviewNode(previewNodes, "RArm_Hand"); }
				}
				if (!parent) { parent = &a_previewRoot; }

				// If the race skeleton has no authored Weapon node, promote the cloned assembled weapon root under the
				// animated hand and preserve the source Weapon ancestor's local grip transform.
				if (weaponAttach && parent->GetName() == RE::BSFixedString("RArm_Hand") &&
					!FindPreviewNode(previewNodes, "Weapon")) {
					for (auto* sourceParent = sourcePart->parent; sourceParent; sourceParent = sourceParent->parent) {
						if (sourceParent->GetName() == RE::BSFixedString("Weapon")) {
							clone->SetLocalTransform(sourceParent->GetLocalTransform());
							break;
						}
					}
				}
				clone->fadeAmount = 1.0f;
				parent->AttachChild(clone.get(), false);
				try { RE::bhkWorld::RemoveObjects(clone.get(), true, true); } catch (...) {}
				ForEachAVObject(clone.get(), [](RE::NiAVObject& a_object) { a_object.controllers.reset(); });
				ForEachAVObject(clone.get(), [&](RE::NiAVObject& a_object) {
					const char* name = a_object.GetName().c_str();
					if (name && name[0] != '\0') { previewNodes.try_emplace(name, std::addressof(a_object)); }
				});
				++result.attached;
			}

			REX::INFO("[IAD Preview] detached biped parts attached: {}/{}", result.attached, result.expected);
			return result;
		}

		RE::NiPointer<RE::NiAVObject> BuildFreshPlayerPreview(
			RE::PlayerCharacter& a_player, RE::NiAVObject* a_liveSource)
		{
			const auto& biped = a_player.GetBiped(false);
			if (!biped) {
				REX::WARN("[IAD Preview] detached fresh character: third-person biped unavailable");
				return nullptr;
			}
			auto previewRoot = LoadFreshRaceSkeleton(a_player);
			if (!previewRoot) { return nullptr; }
			const auto population = PopulateFreshSkeletonFromBiped(*previewRoot, *biped);
			if (population.expected == 0 || population.attached != population.expected) {
				REX::WARN("[IAD Preview] detached fresh character incomplete: {}/{} biped parts",
					population.attached, population.expected);
				return nullptr;
			}
			SanitizeClone(*previewRoot, a_liveSource);
			return previewRoot;
		}

		RE::BSFlattenedBoneTree* FindFlattenedBoneTree(RE::NiAVObject* a_object)
		{
			if (!a_object) return nullptr;
			if (auto* tree = netimmerse_cast<RE::BSFlattenedBoneTree*>(a_object)) return tree;
			if (auto* node = a_object->IsNode()) {
				for (auto& child : node->children) {
					if (child) {
						if (auto* tree = FindFlattenedBoneTree(child.get())) return tree;
					}
				}
			}
			return nullptr;
		}

		RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByName(
			RE::BSFlattenedBoneTree& a_tree, std::string_view a_name)
		{
			if (!a_tree.bone || a_tree.boneCount <= 0 || a_tree.boneCount > 4096) return nullptr;
			for (std::int32_t i = 0; i < a_tree.boneCount; ++i) {
				const char* name = a_tree.bone[i].name.c_str();
				if (name && a_name == name) return std::addressof(a_tree.bone[i]);
			}
			return nullptr;
		}

		RE::NiAVObject* FindNodeByNameRecursive(RE::NiAVObject* a_root, const RE::BSFixedString& a_name)
		{
			if (!a_root) return nullptr;
			if (a_root->name == a_name) return a_root;
			if (auto* node = a_root->IsNode()) {
				for (auto& child : node->children) {
					if (child) {
						if (auto* found = FindNodeByNameRecursive(child.get(), a_name)) return found;
					}
				}
			}
			return nullptr;
		}

		void RepairShaderFadeNodes(RE::NiAVObject& a_object, RE::BSFadeNode* a_currentFade)
		{
			if (auto* fade = netimmerse_cast<RE::BSFadeNode*>(std::addressof(a_object))) a_currentFade = fade;
			if (a_currentFade) {
				if (auto* geometry = netimmerse_cast<RE::BSGeometry*>(std::addressof(a_object))) {
					for (auto& property : geometry->properties) {
						if (auto* shader = netimmerse_cast<RE::BSShaderProperty*>(property.get())) shader->fadeNode = a_currentFade;
					}
				}
			}
			if (auto* node = a_object.IsNode()) {
				for (auto& child : node->children) {
					if (child) RepairShaderFadeNodes(*child, a_currentFade);
				}
			}
		}

		void RebindClonedSkins(RE::NiAVObject& a_root, RE::NiAVObject* a_source)
		{
			// Keep this operation entirely on the ordinary cloned node hierarchy. Rebuilding the private flattened
			// map requires an engine body-part conversion context and was the second path that entered the invalid
			// ragdoll destructor on the current Fallout 4 runtime.
			(void)a_source;

			std::unordered_set<const void*> sourceSkins;


			std::uint32_t reboundGeometry = 0;
			std::uint32_t reboundBones = 0;
			std::uint32_t recoveredNullSlots = 0;
			std::uint32_t droppedNullSlots = 0;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				auto* geometry = netimmerse_cast<RE::BSGeometry*>(std::addressof(a_object));
				if (!geometry || !geometry->skinInstance) return;
				auto* skin = geometry->skinInstance.get();
				if (sourceSkins.contains(skin) || skin->bones.empty() || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) return;
				if (!skin->worldTransforms.empty() && skin->worldTransforms.size() != skin->bones.size()) return;

				for (std::size_t i = 0; i < skin->bones.size(); ++i) {
					auto* sourceBone = skin->bones[i];
					if (!sourceBone) {
						if (!skin->worldTransforms.empty()) skin->worldTransforms[i] = nullptr;
						++droppedNullSlots;
						continue;
					}
					const auto name = sourceBone->GetName();
					if (!name.c_str() || name.c_str()[0] == '\0') continue;

					RE::NiAVObject* destinationBone = FindNodeByNameRecursive(std::addressof(a_root), name);
					RE::NiTransform* destinationWorld = destinationBone ?
						std::addressof(destinationBone->world) : nullptr;
					if (destinationBone) {
						skin->bones[i] = destinationBone;
						++reboundBones;
					}
					if (!skin->worldTransforms.empty()) skin->worldTransforms[i] = destinationWorld;
				}
				skin->rootNode = std::addressof(a_root);
				skin->paletteStamp = 0;
				++reboundGeometry;
			});
			REX::INFO("[IAD Preview] detached clone skin rebind: {} geometries, {} bones ({} null recovered, {} dropped)",
				reboundGeometry, reboundBones, recoveredNullSlots, droppedNullSlots);
		}

		int CopyOrdinaryNodePose(RE::NiAVObject& a_root, RE::NiAVObject* a_source)
		{
			if (!a_source) return 0;
			int copied = 0;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (!a_object.IsNode()) return;
				const char* name = a_object.GetName().c_str();
				if (!name || name[0] == '\0') return;
				if (auto* sourceObject = FindNodeByNameRecursive(a_source, RE::BSFixedString(name))) {
					a_object.SetLocalTransform(sourceObject->GetLocalTransform());
					++copied;
				}
			});
			return copied;
		}

		int CopyFlattenedPose(RE::NiAVObject& a_root, RE::NiAVObject* a_source)
		{
			if (!a_source) return 0;
			const int ordinaryCopied = CopyOrdinaryNodePose(a_root, a_source);
			auto* destination = FindFlattenedBoneTree(std::addressof(a_root));
			auto* source = FindFlattenedBoneTree(a_source);
			if (!destination || !source || !destination->bone || !source->bone ||
				destination->boneCount <= 0 || destination->boneCount > 4096) return ordinaryCopied;

			int copied = 0;
			for (std::int32_t i = 0; i < destination->boneCount; ++i) {
				auto& destinationBone = destination->bone[i];
				const char* name = destinationBone.name.c_str();
				if (!name || name[0] == '\0' || std::string_view(name) == "Weapon" || std::string_view(name) == "WeaponLeft") continue;
				if (auto* sourceBone = FindFlattenedBoneByName(*source, name)) {
					destinationBone.local = sourceBone->local;
					destinationBone.world = sourceBone->world;
					++copied;
				}
			}
			return copied + ordinaryCopied;
		}

		void SanitizeClone(RE::NiAVObject& a_root, RE::NiAVObject* a_source)
		{
			ForEachAVObject(std::addressof(a_root), [](RE::NiAVObject& a_object) {
				a_object.controllers.reset();
				a_object.fadeAmount = 1.0f;
				if (auto* fade = netimmerse_cast<RE::BSFadeNode*>(std::addressof(a_object))) {
					fade->currentFade = 1.0f;
					fade->currentDecalFade = 1.0f;
					fade->previousMaxA = 1.0f;
				}
			});
			RepairShaderFadeNodes(a_root, nullptr);
			const auto copiedPose = CopyFlattenedPose(a_root, a_source);
			RebindClonedSkins(a_root, a_source);
			RE::NiUpdateData updateData{};
			a_root.Update(updateData);
			REX::INFO("[IAD Preview] detached clone pose copied: {} flattened bones", copiedPose);
		}
	}

	void DetachedPreviewScene::ProcessGameThread(bool a_enabled, const PreviewSceneSnapshot& a_snapshot)
	{
		std::unique_lock sceneLock(m_sceneMutex);
		if (!a_enabled || ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition() ||
			a_snapshot.identity.actorFormID == 0 || a_snapshot.identity.sceneGeneration == 0) {
			if (m_rendererOwned.load(std::memory_order_acquire)) {
				REX::INFO("[IAD Preview] retiring detached scene: inactive or invalid snapshot");
				ReleaseRendererGameThread();
			}
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* source = player && player->Get3D(false) ? static_cast<RE::NiAVObject*>(player->Get3D(false)) : nullptr;
		if (!player || !source || player->GetFormID() != a_snapshot.identity.actorFormID ||
			player->IsDead(false) || player->IsDeleted() || player->IsDisabled()) {
			ReleaseRendererGameThread();
			return;
		}

		if (!EnsureDisplayRootGameThread() || !EnsureRendererGameThread() ||
			!EnsureCloneGameThread(*player, *source, a_snapshot)) {
			ReleaseRendererGameThread();
			return;
		}
		UpdateCloneFrameGameThread(*player);
		if (m_renderer && m_previewRoot && m_renderer->offscreenElement.get() != m_previewRoot.get()) {
			m_renderer->Offscreen_Set3D(m_previewRoot.get());
		}
		if (m_renderer && !m_renderer->enabled) m_renderer->Enable(false);
		m_ready.store(m_renderer && m_previewRoot && m_renderer->offscreenElement.get() == m_previewRoot.get(), std::memory_order_release);
	}

	void DetachedPreviewScene::RetireGameThread(const char* a_reason)
	{
		std::unique_lock sceneLock(m_sceneMutex);
		if (m_rendererOwned.load(std::memory_order_acquire)) {
			REX::INFO("[IAD Preview] detached scene retired: {}", a_reason ? a_reason : "unspecified");
			ReleaseRendererGameThread();
		}
	}

	bool DetachedPreviewScene::EnsureDisplayRootGameThread()
	{
		if (m_displayRoot) return true;
		RE::NiPointer<RE::NiNode> loaded;
		RE::BSModelDB::DBTraits::ArgsType args{};
		args.prepareAfterLoad = true;
		args.useErrorMarker = true;
		args.performProcess = true;
		args.createFadeNode = true;
		args.loadTextures = true;
		if (RE::BSModelDB::Demand(kDisplayMeshPath, std::addressof(loaded), args) != RE::BSResource::ErrorCode::kNone || !loaded) {
			REX::WARN("[IAD Preview] detached display mesh load failed: {}", kDisplayMeshPath);
			return false;
		}
		m_displayRoot = CloneExact(*loaded);
		if (!m_displayRoot) return false;
		m_displayRoot->local.translate = { 0.0f, 375.0f, 0.0f };
		RE::NiUpdateData updateData{};
		m_displayRoot->Update(updateData);
		return true;
	}

	bool DetachedPreviewScene::EnsureRendererGameThread()
	{
		if (m_renderer) return true;
		const RE::BSFixedString name(kRendererName);
		if (auto* existing = RE::Interface3D::Renderer::GetByName(name)) {
			// The name is IAD-owned. Clear the scene before releasing a stale
			// instance left by a previous menu session or plugin reload.
			existing->Offscreen_Set3D(nullptr);
			existing->MainScreen_SetScreenAttached3D(nullptr);
			if (existing->enabled) existing->Disable();
			existing->Release();
		}

		m_renderer = RE::Interface3D::Renderer::Create(name, RE::UI_DEPTH_PRIORITY::kMessage, 70.0f, true);
		if (!m_renderer) {
			REX::WARN("[IAD Preview] detached Interface3D renderer creation failed");
			return false;
		}
		m_renderer->alwaysRenderWhenEnabled = true;
		m_renderer->hideScreenWhenDisabled = true;
		m_renderer->MainScreen_SetBackgroundMode(RE::Interface3D::BackgroundMode::kLive);
		m_renderer->MainScreen_SetPostAA(true);
		m_renderer->Offscreen_Enable3D(true);
		m_renderer->Offscreen_SetUseLongRangeCamera(true);
		m_renderer->Offscreen_SetRenderTargetSize(RE::Interface3D::OffscreenMenuSize::kFullFrame);
		m_renderer->Offscreen_SetDisplayMode(RE::Interface3D::ScreenMode::kScreenAttached, kDisplayMeshGeometry, nullptr);
		// Match the proven Pip-Boy screen-attached path. Without the HUD shadow mask, the
		// offscreen character pass can leak as an unmasked silhouette into the main scene
		// while the screen-attached composite is being updated.
		m_renderer->MainScreen_EnableScreenAttached3DMasking(
			"HUDShadowFlat:0", "Materials\\Interface\\ModMenuShadow.BGEM");
		m_renderer->Offscreen_SetPostEffect(RE::Interface3D::PostEffect::kHUDGlass);
		m_renderer->Offscreen_SetClearRenderTarget(true);
		m_renderer->Offscreen_SetBackgroundColor(RE::NiColorA{ 0.0f, 0.0f, 0.0f, 0.0f });
		m_renderer->customRenderTarget = -1;
		m_renderer->customSwapTarget = -1;
		m_renderer->MainScreen_SetScreenAttached3D(m_displayRoot.get());
		m_renderer->Offscreen_Set3D(nullptr);
		m_targetWidth = m_renderer->Offscreen_GetRenderTargetWidth();
		m_targetHeight = m_renderer->Offscreen_GetRenderTargetHeight();
		m_rendererOwned.store(true, std::memory_order_release);
		REX::INFO("[IAD Preview] detached Interface3D renderer configured: {}x{}", m_targetWidth, m_targetHeight);
		return true;
	}

	bool DetachedPreviewScene::EnsureCloneGameThread(
		RE::Actor& a_actor, RE::NiAVObject& a_source, const PreviewSceneSnapshot& a_snapshot)
	{
		if (m_previewRoot && m_sourceRoot == std::addressof(a_source) && m_identity.SameScene(a_snapshot.identity)) return true;
		m_ready.store(false, std::memory_order_release);
		m_previewRoot.reset();
		// Do not call ModelManager::CloneRenderOnly here. That path clones the live player root, including
		// actor-owned cloth/ragdoll extras, and Fallout 4 may release those extras through a stale physics driver.
		// Build the detached tree from a clean race skeleton plus individually mapped Biped parts instead.
		auto clone = BuildFreshPlayerPreview(*RE::PlayerCharacter::GetSingleton(), std::addressof(a_source));
		if (!clone) {
			REX::WARN("[IAD Preview] detached fresh player preview failed for {:08X}", a_actor.GetFormID());
			return false;
		}
		m_sourceRoot = std::addressof(a_source);
		m_identity = a_snapshot.identity;
		m_sourceActorPosition = a_actor.GetPosition();
		m_sourceYaw = a_actor.data.angle.z;
		{
			std::lock_guard frameLock(m_frameMutex);
			m_cameraPosition = m_sourceActorPosition;
		}
		m_previewRoot = std::move(clone);
		REX::INFO("[IAD Preview] detached fresh player preview built for {:08X}, scene generation {}, 3D generation {}",
			a_actor.GetFormID(), m_identity.sceneGeneration, m_identity.actor3DGeneration);
		return true;
	}

	void DetachedPreviewScene::SynchronizeClonePoseGameThread()
	{
		if (!m_previewRoot || !m_sourceRoot) return;

		// The clone intentionally has no controllers, so its flattened bone locals
		// must be refreshed from the live actor before the root is framed. Keep this
		// on the game thread with the rest of the scene-graph work; ImGui only sees
		// the copied projection frame.
		CopyFlattenedPose(*m_previewRoot, m_sourceRoot);
	}

	void DetachedPreviewScene::UpdateCloneFrameGameThread(RE::Actor& a_actor)
	{
		if (!m_previewRoot) return;
		SynchronizeClonePoseGameThread();
		RE::NiPoint3 cameraPosition;
		{
			std::lock_guard frameLock(m_frameMutex);
			cameraPosition = m_cameraPosition;
		}

		// First evaluate the clone without a translation so its local bounds can
		// be centered. This keeps framing independent of the actor's world cell.
		auto transform = RE::NiTransform::IDENTITY;
		transform.scale = kPreviewScale;
		transform.rotate = TransformMath::EulerToMatrix({ 0.0f, 0.0f, (a_actor.data.angle.z - m_sourceYaw) * 180.0f / 3.14159265f });
		m_previewRoot->local = transform;
		RE::NiUpdateData updateData{};
		m_previewRoot->Update(updateData);

		const auto target = m_previewRoot->worldBound.center;
		const RE::NiPoint3 cameraDelta{
			cameraPosition.x - m_sourceActorPosition.x,
			cameraPosition.y - m_sourceActorPosition.y,
			cameraPosition.z - m_sourceActorPosition.z
		};
		transform.translate = {
			-target.x - cameraDelta.x,
			kFrameDistance - target.y - cameraDelta.y,
			-target.z - cameraDelta.z
		};
		m_previewRoot->local = transform;
		m_previewRoot->Update(updateData);
	}

	void DetachedPreviewScene::ReleaseRendererGameThread()
	{
		m_ready.store(false, std::memory_order_release);
		if (m_renderer) {
			m_renderer->Offscreen_Set3D(nullptr);
			m_renderer->MainScreen_SetScreenAttached3D(nullptr);
			if (m_renderer->enabled) m_renderer->Disable();
			m_renderer->Release();
			m_renderer = nullptr;
		}
		m_previewRoot.reset();
		m_displayRoot.reset();
		m_sourceRoot = nullptr;
		m_identity = {};
		m_targetWidth = 0;
		m_targetHeight = 0;
		m_rendererOwned.store(false, std::memory_order_release);
	}

	bool DetachedPreviewScene::CaptureFrame(
		const PreviewSceneSnapshot& a_snapshot,
		const ImVec2& a_viewportMin,
		const ImVec2& a_viewportMax)
	{
		std::shared_lock sceneLock(m_sceneMutex);
		if (!m_ready.load(std::memory_order_acquire) || !m_renderer || !m_renderer->nativeAspect) return false;
		if (!m_identity.SameScene(a_snapshot.identity)) return false;
		if (a_viewportMax.x - a_viewportMin.x <= 1.0f || a_viewportMax.y - a_viewportMin.y <= 1.0f) return false;

		Frame next;
		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				next.worldToCamera[row][column] = m_renderer->nativeAspect->worldToCam[row][column];
			}
		}
		const auto& rotation = m_renderer->nativeAspect->world.rotate;
		next.basis.forward = { rotation.entry[0][0], rotation.entry[0][1], rotation.entry[0][2] };
		next.basis.up = { rotation.entry[1][0], rotation.entry[1][1], rotation.entry[1][2] };
		next.basis.right = { rotation.entry[2][0], rotation.entry[2][1], rotation.entry[2][2] };
		next.previewTransform = m_previewRoot ? m_previewRoot->local : RE::NiTransform::IDENTITY;
		next.sourcePosition = m_sourceActorPosition;
		next.identity = m_identity;
		next.viewportMin = a_viewportMin;
		next.viewportMax = a_viewportMax;
		{
			std::lock_guard frameLock(m_frameMutex);
			m_frame = next;
		}
		return true;
	}

	RE::NiPoint3 DetachedPreviewScene::CaptureCurrentWorldPosition(RE::Actor* a_actor) const
	{
		std::lock_guard frameLock(m_frameMutex);
		return m_cameraPosition.x != 0.0f || m_cameraPosition.y != 0.0f || m_cameraPosition.z != 0.0f ?
			m_cameraPosition : (a_actor ? a_actor->GetPosition() : RE::NiPoint3{});
	}

	bool DetachedPreviewScene::EnterPreview(const RE::NiPoint3& a_cameraPosition, bool& a_cameraOwned) const
	{
		if (!IsReady()) return false;
		a_cameraOwned = false;
		return ApplyPreview(a_cameraPosition);
	}

	bool DetachedPreviewScene::ApplyPreview(const RE::NiPoint3& a_cameraPosition) const
	{
		// Interface3D owns the detached camera. The current adapter stage records
		// the requested anchor; camera controls are enabled in the next stage when
		// the renderer's camera setter has been validated on AE.
		auto& self = const_cast<DetachedPreviewScene&>(*this);
		std::lock_guard frameLock(self.m_frameMutex);
		self.m_cameraPosition = a_cameraPosition;
		return self.IsReady();
	}

	void DetachedPreviewScene::RestorePreview(bool) const
	{
		// No live PlayerCamera state is owned by the detached Adapter.
	}

	bool DetachedPreviewScene::WorldToScreen(const RE::NiPoint3& a_worldPosition, ImVec2& a_screenPosition) const noexcept
	{
		Frame frame;
		{
			std::lock_guard frameLock(m_frameMutex);
			frame = m_frame;
		}
		const auto relative = a_worldPosition - frame.sourcePosition;
		const auto previewPosition = frame.previewTransform.rotate * (relative * frame.previewTransform.scale) + frame.previewTransform.translate;
		const auto& matrix = frame.worldToCamera;
		const float w = matrix[3][0] * previewPosition.x + matrix[3][1] * previewPosition.y + matrix[3][2] * previewPosition.z + matrix[3][3];
		if (w < 0.001f || !std::isfinite(w)) return false;
		const float inverseW = 1.0f / w;
		const float x = (matrix[0][0] * previewPosition.x + matrix[0][1] * previewPosition.y + matrix[0][2] * previewPosition.z + matrix[0][3]) * inverseW;
		const float y = (matrix[1][0] * previewPosition.x + matrix[1][1] * previewPosition.y + matrix[1][2] * previewPosition.z + matrix[1][3]) * inverseW;
		if (!std::isfinite(x) || !std::isfinite(y)) return false;
		const float width = frame.viewportMax.x - frame.viewportMin.x;
		const float height = frame.viewportMax.y - frame.viewportMin.y;
		a_screenPosition.x = frame.viewportMin.x + ((x + 1.0f) * 0.5f) * width;
		a_screenPosition.y = frame.viewportMin.y + ((1.0f - y) * 0.5f) * height;
		return true;
	}

	bool DetachedPreviewScene::GetBasis(PreviewCameraBasis& a_basis) const noexcept
	{
		std::lock_guard frameLock(m_frameMutex);
		if (!m_ready.load(std::memory_order_acquire)) return false;
		a_basis = m_frame.basis;
		return true;
	}
}
