#include "pch.h"
#include "HolsterManager.h"
#include "Data/ConfigManager.h"
#include "Engine/NodeManager.h"
#include "ModelManager.h"
#include "Engine/ConditionSystem.h"
#include "Profile/GlobalProfileManager.h"
#include "System/ActorDisplayContext.h"
#include "System/ActorDisplayLifecycle.h"
#include "System/ActorRuntimeContext.h"
#include "System/AssignmentResolver.h"
#include "System/ConditionRefreshPolicy.h"
#include "UI/UIWorldPreviewSession.h"
// [DISABLED] #include "Combat/VATSWeaponPart.h"

// 替换为新库的路径
#include <RE/P/ProcessLists.h>
#include <RE/T/TESForm.h>
#include <RE/U/UI.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/A/Actor.h>
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiCloningProcess.h>
#include <RE/T/TESNPC.h>
#include <RE/E/ENUM_FORM_ID.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESAmmo.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESRace.h>
#include <RE/B/BGSBipedObjectForm.h>
#include <RE/B/BSTEvent.h>
#include <RE/B/BSTriShape.h>
#include <cstring>

	namespace IAD
	{
	static std::atomic<bool> g_playerEquipChanged{ false };

	namespace {
		// Fallout 4 stores the position stream of its regular render meshes as
		// three IEEE-754 half floats. Keep contour sampling self-contained: the
		// engine UnpackVertexData relocation is not a safe dependency for this
		// diagnostic snapshot path, especially while a save is rebuilding clones.
		float DecodePackedHalf(std::uint16_t a_value) noexcept {
			const auto sign = static_cast<std::uint32_t>(a_value & 0x8000u) << 16;
			const auto exponent = static_cast<std::uint32_t>((a_value >> 10) & 0x1Fu);
			const auto fraction = static_cast<std::uint32_t>(a_value & 0x03FFu);

			if (exponent == 0) {
				if (fraction == 0) return (sign != 0) ? -0.0f : 0.0f;
				const auto magnitude = std::ldexp(static_cast<float>(fraction), -24);
				return (sign != 0) ? -magnitude : magnitude;
			}

			std::uint32_t bits = sign;
			if (exponent == 0x1Fu) {
				bits |= 0x7F800000u | (fraction << 13);
			}
			else {
				bits |= ((exponent + 112u) << 23) | (fraction << 13);
			}

			float result = 0.0f;
			std::memcpy(std::addressof(result), std::addressof(bits), sizeof(result));
			return result;
		}

		bool ReadPackedPosition(
			const std::uint8_t* a_vertices,
			std::size_t a_dataSize,
			std::size_t a_stride,
			std::size_t a_positionOffset,
			std::size_t a_index,
			RE::NiPoint3& a_result) noexcept {
			if (!a_vertices || a_dataSize == 0 || a_stride < 6 ||
				a_positionOffset > a_stride - 6 || a_index > (a_dataSize - a_positionOffset - 6) / a_stride) {
				return false;
			}

			const auto offset = a_index * a_stride + a_positionOffset;
			std::uint16_t packed[3]{};
			std::memcpy(packed, a_vertices + offset, sizeof(packed));
			a_result = {
				DecodePackedHalf(packed[0]),
				DecodePackedHalf(packed[1]),
				DecodePackedHalf(packed[2])
			};
			return std::isfinite(a_result.x) && std::isfinite(a_result.y) && std::isfinite(a_result.z);
		}

		std::uint64_t GetInventorySignature(RE::Actor* a_actor) {
			if (!a_actor || !a_actor->inventoryList) return 0;
			BSAutoReadLock inventoryLock(a_actor->inventoryList->rwLock);

			std::uint64_t signature = 1469598103934665603ULL;
			auto mix = [&signature](std::uint64_t a_value) {
				signature ^= a_value;
				signature *= 1099511628211ULL;
			};

			for (const auto& inventoryItem : a_actor->inventoryList->data) {
				if (!inventoryItem.object) continue;
				mix(inventoryItem.object->GetFormID());

				std::uint64_t stackCount = 0;
				for (auto* stack = inventoryItem.stackData.get(); stack; stack = stack->nextStack.get()) {
					++stackCount;
					mix(stack->count);
					mix(stack->IsEquipped() ? 1 : 0);
					mix(stack->extra && stack->extra->IsFavorite() ? 1 : 0);
				}
				mix(stackCount);
			}

			mix(a_actor->inventoryList->data.size());
			return signature;
		}

		bool ShouldHideWeaponDisplay(RE::Actor* a_actor, const HolsterSlot& a_state) noexcept {
			if (!a_actor) return a_state.isWeaponHidden;
			return a_state.isSlotHidden ||
				(a_state.hideWeaponWhenDrawn && a_state.isEquipped &&
					a_actor->weaponState != RE::WEAPON_STATE::kSheathed);
		}

		bool ShouldHideHolsterDisplay(RE::Actor* a_actor, const HolsterSlot& a_state) noexcept {
			if (!a_actor) return a_state.isHolsterHidden;
			const bool hideForDrawnWeapon = a_state.hideWeaponWhenDrawn && a_state.isEquipped &&
				a_actor->weaponState != RE::WEAPON_STATE::kSheathed;
			return a_state.isSlotHidden || (hideForDrawnWeapon && !a_state.keepHolsterWhenDrawn);
		}

		std::string FormatCMEName(const std::string& name) {
			return (name.find("IAD_CME_") == 0) ? name : "IAD_CME_" + name;
		}
		std::string FormatMOVName(const std::string& name) {
			return (name.find("IAD_MOV_") == 0) ? name : "IAD_MOV_" + name;
		}
		std::string StripManagedNodePrefix(const std::string& name) {
			if (name.find("IAD_CME_") == 0) return name.substr(8);
			if (name.find("IAD_MOV_") == 0) return name.substr(8);
			return name;
		}

		bool NormalizeBipedSlot(std::uint32_t a_slot, std::uint32_t& a_index) {
			if (a_slot < 32) {
				a_index = a_slot;
				return true;
			}
			if (a_slot >= 30 && a_slot <= 61) {
				a_index = a_slot - 30;
				return true;
			}
			return false;
		}

		std::uint32_t GetItemBipedMask(RE::TESBoundObject* a_item) {
			if (!a_item) return 0;
			auto* bipedForm = a_item->As<RE::BGSBipedObjectForm>();
			return bipedForm ? bipedForm->bipedModelData.bipedObjectSlots : 0;
		}

		bool ItemUsesBipedSlot(RE::TESBoundObject* a_item, std::uint32_t a_slot) {
			std::uint32_t index = 0;
			if (!NormalizeBipedSlot(a_slot, index)) return false;
			const auto mask = GetItemBipedMask(a_item);
			return (mask & (1u << index)) != 0;
		}

		bool BipedSlotOccupied(RE::Actor* a_actor, std::uint32_t a_slot) {
			if (!a_actor) return false;
			std::uint32_t index = 0;
			if (!NormalizeBipedSlot(a_slot, index)) return false;
			auto biped = a_actor->GetBiped(false);
			return biped && biped->object[index].parent.object != nullptr;
		}

		struct RuntimeModelGroupEntry {
			std::string name;
			int sourceMode = 0;
			std::string modelPath;
			RE::TESFormID sourceFormID = 0;
			bool extractMagazine = false;
			bool useProjectileForAmmo = false;
			bool removeEditorMarker = true;
			bool removeProjectileTracers = true;
			bool invisible = false;
			bool hideGeometry = false;
			bool hideLight = false;
			bool load1pWeaponModel = false;
			bool keepTorchFlame = false;
		bool removeScabbard = false;
		std::string targetNode;
		std::string movName;
		TransformData transform;
		bool overrideGeometryTransform = false;
		TransformData geometryTransform;
		bool hideWithWeapon = true;
		bool conditionVisible = true;
		ModelEffectSettings effect;
			ModelLightSettings light;
			bool playSequence = false;
			bool forwardAnimationEvents = false;
			bool disableBehaviorGraphAnims = true;
			std::string sequenceName;
			std::string animationEvent;
		};

		TransformData ResolveStateTransform(const StateOverride& a_state) {
			if (a_state.useTransformPreset && !a_state.targetTransformPreset.empty()) {
				if (const auto resolved = Profile::GlobalProfileManager::GetSingleton().ResolveRuntimeTransform(a_state.targetTransformPreset)) {
					return *resolved;
				}
			}
			return a_state.independentTransform;
		}

		constexpr const char* kGeometryTransformRootName = "IAD_GeometryTransformRoot";

		RE::NiNode* FindGeometryTransformRoot(RE::NiNode* a_root)
		{
			if (!a_root) {
				return nullptr;
			}

			const RE::BSFixedString rootName(kGeometryTransformRootName);
			for (auto& child : a_root->children) {
				if (child && child->name == rootName) {
					return child->IsNode();
				}
			}
			return nullptr;
		}

		RE::NiNode* EnsureGeometryTransformRoot(RE::NiAVObject* a_model)
		{
			auto* rootNode = a_model ? a_model->IsNode() : nullptr;
			if (!rootNode) {
				return nullptr;
			}

			if (auto* existing = FindGeometryTransformRoot(rootNode)) {
				return existing;
			}

			RE::NiCloningProcess cloning;
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '\0';
			cloning.scale = { 1.0f, 1.0f, 1.0f };
			auto* geometryRootObject = rootNode->CreateClone(cloning);
			auto* geometryRoot = geometryRootObject ? geometryRootObject->IsNode() : nullptr;
			if (!geometryRoot) {
				return nullptr;
			}
			std::vector<RE::NiPointer<RE::NiAVObject>> clonedChildren;
			for (auto& child : geometryRoot->children) if (child) clonedChildren.push_back(child);
			for (auto& child : clonedChildren) geometryRoot->DetachChild(child.get());
			geometryRoot->controllers.reset();
			geometryRoot->extra = nullptr;
			geometryRoot->collisionObject.reset();

			geometryRoot->name = kGeometryTransformRootName;
			geometryRoot->local.translate = { 0, 0, 0 };
			geometryRoot->local.rotate.MakeIdentity();
			geometryRoot->local.scale = 1.0f;

			std::vector<RE::NiPointer<RE::NiAVObject>> childrenToMove;
			for (auto& child : rootNode->children) {
				if (child) {
					childrenToMove.push_back(child);
				}
			}

			for (auto& child : childrenToMove) {
				rootNode->DetachChild(child.get());
				geometryRoot->AttachChild(child.get(), true);
			}

			rootNode->AttachChild(geometryRoot, true);
			return geometryRoot;
		}

		void ApplyGeometryTransform(RE::NiAVObject* a_model, bool a_enabled, const TransformData& a_transform)
		{
			auto* target = a_enabled ?
				EnsureGeometryTransformRoot(a_model) :
				(a_model && a_model->IsNode() ? FindGeometryTransformRoot(a_model->IsNode()) : nullptr);

			if (!target) {
				return;
			}

			target->local.MakeIdentity();

			if (!a_enabled) {
				target->local.scale = 1.0f;
				return;
			}

			const RE::NiMatrix3 rotation = TransformMath::EulerToMatrix(a_transform.rot);
			const RE::NiPoint3 pivot = a_transform.pivot;
			const RE::NiPoint3 rotatedPivot = rotation * pivot;

			target->local.translate.x = a_transform.pos.x + (pivot.x - rotatedPivot.x);
			target->local.translate.y = a_transform.pos.y + (pivot.y - rotatedPivot.y);
			target->local.translate.z = a_transform.pos.z + (pivot.z - rotatedPivot.z);
			target->local.rotate = rotation;
			target->local.scale = a_transform.scale;
		}

		PhysicsValues ResolveStatePhysics(const StateOverride& a_state) {
			if (a_state.usePhysicsPreset && !a_state.targetPhysicsPreset.empty()) {
				if (const auto resolved = Profile::GlobalProfileManager::GetSingleton().ResolveRuntimePhysics(a_state.targetPhysicsPreset)) {
					return *resolved;
				}
			}
			return a_state.independentPhysics;
		}

		ModelEffectSettings BuildModelEffectSettings(const ModelEffectShaderConfig& a_effect) {
			ModelEffectSettings out;
			out.enabled = a_effect.enabled;
			out.targetRoot = a_effect.targetRoot;
			out.force = a_effect.force;
			out.lighting = a_effect.lighting;
			out.alpha = a_effect.alpha;
			out.fillR = std::clamp(a_effect.fillColor.r, 0.0f, 1.0f);
			out.fillG = std::clamp(a_effect.fillColor.g, 0.0f, 1.0f);
			out.fillB = std::clamp(a_effect.fillColor.b, 0.0f, 1.0f);
			out.fillA = std::clamp(a_effect.fillColor.a, 0.0f, 1.0f);
			out.rimR = std::clamp(a_effect.rimColor.r, 0.0f, 1.0f);
			out.rimG = std::clamp(a_effect.rimColor.g, 0.0f, 1.0f);
			out.rimB = std::clamp(a_effect.rimColor.b, 0.0f, 1.0f);
			out.rimA = std::clamp(a_effect.rimColor.a, 0.0f, 1.0f);
			out.baseFillScale = a_effect.baseFillScale;
			out.baseFillAlpha = a_effect.baseFillAlpha;
			out.baseRimAlpha = a_effect.baseRimAlpha;
			out.edgeExponent = a_effect.edgeExponent;
			out.alphaMultiplier = std::clamp(a_effect.alphaMultiplier, 0.0f, 1.0f);
			return out;
		}

		ModelLightSettings BuildModelLightSettings(const ModelLightConfig& a_light) {
			ModelLightSettings out;
			out.enabled = a_light.enabled;
			out.posX = a_light.transform.pos.x;
			out.posY = a_light.transform.pos.y;
			out.posZ = a_light.transform.pos.z;
			out.rotX = a_light.transform.rot.x;
			out.rotY = a_light.transform.rot.y;
			out.rotZ = a_light.transform.rot.z;
			out.diffuseR = std::clamp(a_light.diffuse.r, 0.0f, 1.0f);
			out.diffuseG = std::clamp(a_light.diffuse.g, 0.0f, 1.0f);
			out.diffuseB = std::clamp(a_light.diffuse.b, 0.0f, 1.0f);
			out.diffuseA = std::clamp(a_light.diffuse.a, 0.0f, 1.0f);
			out.radius = std::clamp(a_light.radius, 0.0f, 4096.0f);
			out.dimmer = std::clamp(a_light.dimmer, 0.0f, 20.0f);
			return out;
		}

		void AppendModelEffectSignature(std::string& a_signature, const ModelEffectSettings& a_effect) {
			a_signature += a_effect.enabled ? '1' : '0';
			a_signature += '|';
			a_signature += a_effect.targetRoot ? '1' : '0';
			a_signature += '|';
			a_signature += a_effect.force ? '1' : '0';
			a_signature += '|';
			a_signature += a_effect.lighting ? '1' : '0';
			a_signature += '|';
			a_signature += a_effect.alpha ? '1' : '0';
			a_signature += '|';
			a_signature += std::to_string(a_effect.fillR);
			a_signature += '|';
			a_signature += std::to_string(a_effect.fillG);
			a_signature += '|';
			a_signature += std::to_string(a_effect.fillB);
			a_signature += '|';
			a_signature += std::to_string(a_effect.fillA);
			a_signature += '|';
			a_signature += std::to_string(a_effect.rimR);
			a_signature += '|';
			a_signature += std::to_string(a_effect.rimG);
			a_signature += '|';
			a_signature += std::to_string(a_effect.rimB);
			a_signature += '|';
			a_signature += std::to_string(a_effect.rimA);
			a_signature += '|';
			a_signature += std::to_string(a_effect.baseFillScale);
			a_signature += '|';
			a_signature += std::to_string(a_effect.baseFillAlpha);
			a_signature += '|';
			a_signature += std::to_string(a_effect.baseRimAlpha);
			a_signature += '|';
			a_signature += std::to_string(a_effect.edgeExponent);
			a_signature += '|';
			a_signature += std::to_string(a_effect.alphaMultiplier);
			a_signature += '|';
		}

		void AppendModelLightSignature(std::string& a_signature, const ModelLightSettings& a_light) {
			a_signature += a_light.enabled ? '1' : '0';
			a_signature += '|';
			a_signature += std::to_string(a_light.posX);
			a_signature += '|';
			a_signature += std::to_string(a_light.posY);
			a_signature += '|';
			a_signature += std::to_string(a_light.posZ);
			a_signature += '|';
			a_signature += std::to_string(a_light.rotX);
			a_signature += '|';
			a_signature += std::to_string(a_light.rotY);
			a_signature += '|';
			a_signature += std::to_string(a_light.rotZ);
			a_signature += '|';
			a_signature += std::to_string(a_light.diffuseR);
			a_signature += '|';
			a_signature += std::to_string(a_light.diffuseG);
			a_signature += '|';
			a_signature += std::to_string(a_light.diffuseB);
			a_signature += '|';
			a_signature += std::to_string(a_light.diffuseA);
			a_signature += '|';
			a_signature += std::to_string(a_light.radius);
			a_signature += '|';
			a_signature += std::to_string(a_light.dimmer);
			a_signature += '|';
		}

		void AppendModelCleanupSignature(std::string& a_signature, const ModelCleanupPolicy& a_cleanupPolicy) {
			a_signature += a_cleanupPolicy.removeEditorMarker ? '1' : '0';
			a_signature += '|';
			a_signature += a_cleanupPolicy.removeProjectileTracers ? '1' : '0';
			a_signature += '|';
			a_signature += a_cleanupPolicy.removeScabbard ? '1' : '0';
			a_signature += '|';
			a_signature += a_cleanupPolicy.removeLights ? '1' : '0';
			a_signature += '|';
			a_signature += a_cleanupPolicy.keepTorchFlame ? '1' : '0';
			a_signature += '|';
		}

		bool HasModelAnimationConfig(const ModelAnimationConfig& a_animation) {
			return a_animation.playSequence ||
				a_animation.forwardAnimationEvents ||
				!a_animation.disableBehaviorGraphAnims ||
				!a_animation.sequenceName.empty() ||
				!a_animation.animationEvent.empty();
		}

		bool IsActorUsingFurnitureForDisplayHide(RE::Actor* a_actor) {
			return ConditionEvaluator::IsSitting(a_actor) ||
				ConditionEvaluator::IsSleeping(a_actor) ||
				ConditionEvaluator::IsLayingDown(a_actor) ||
				ConditionEvaluator::IsCrafting(a_actor);
		}

		void AppendModelAnimationSignature(std::string& a_signature, const ModelAnimationConfig& a_animation) {
			a_signature += a_animation.playSequence ? '1' : '0';
			a_signature += '|';
			a_signature += a_animation.forwardAnimationEvents ? '1' : '0';
			a_signature += '|';
			a_signature += a_animation.disableBehaviorGraphAnims ? '1' : '0';
			a_signature += '|';
			a_signature += a_animation.sequenceName;
			a_signature += '|';
			a_signature += a_animation.animationEvent;
			a_signature += '|';
		}

		void TryPlayModelAnimationOnce(
			RE::Actor* a_actor,
			RE::NiAVObject* a_model,
			const ModelAnimationConfig& a_animation)
		{
			if (!a_actor || !a_model) {
				return;
			}

			auto* objectNet = static_cast<RE::NiObjectNET*>(a_model);
			auto* controller = objectNet ? RE::NiControllerManager::GetNiControllerManager(objectNet) : nullptr;

			if (a_animation.playSequence && controller && !a_animation.sequenceName.empty()) {
				if (auto* sequence = controller->GetSequenceByName(a_animation.sequenceName.c_str())) {
					sequence->Activate(0, true, 1.0f, 0.0f, nullptr, false);
				}
			}

			if (!a_animation.playSequence &&
				!a_animation.disableBehaviorGraphAnims &&
				!a_animation.animationEvent.empty() &&
				a_animation.forwardAnimationEvents) {
				a_actor->NotifyAnimationGraphImpl(a_animation.animationEvent.c_str());
			}

		}

		std::string BuildModelGroupSignature(const std::vector<RuntimeModelGroupEntry>& a_groups) {
			std::string signature;
			for (const auto& group : a_groups) {
				signature += group.name;
				signature += '|';
				signature += std::to_string(group.sourceMode);
				signature += '|';
				signature += group.modelPath;
				signature += '|';
				signature += std::to_string(group.sourceFormID);
				signature += '|';
				signature += group.extractMagazine ? '1' : '0';
				signature += '|';
				signature += group.useProjectileForAmmo ? '1' : '0';
				signature += '|';
				signature += group.removeEditorMarker ? '1' : '0';
				signature += '|';
				signature += group.removeProjectileTracers ? '1' : '0';
				signature += '|';
				signature += group.invisible ? '1' : '0';
				signature += '|';
				signature += group.hideGeometry ? '1' : '0';
				signature += '|';
				signature += group.hideLight ? '1' : '0';
				signature += '|';
				signature += group.load1pWeaponModel ? '1' : '0';
				signature += '|';
				signature += group.keepTorchFlame ? '1' : '0';
				signature += '|';
				signature += group.removeScabbard ? '1' : '0';
				signature += '|';
				signature += group.targetNode;
				signature += '|';
				signature += group.movName;
				signature += '|';
				signature += group.hideWithWeapon ? '1' : '0';
				signature += '|';
				AppendModelEffectSignature(signature, group.effect);
				AppendModelLightSignature(signature, group.light);
				signature += group.playSequence ? '1' : '0';
				signature += '|';
				signature += group.forwardAnimationEvents ? '1' : '0';
				signature += '|';
				signature += group.disableBehaviorGraphAnims ? '1' : '0';
				signature += '|';
				signature += group.sequenceName;
				signature += '|';
				signature += group.animationEvent;
				signature += ';';
			}
			return signature;
		}

		void TryPlayModelGroupAnimation(
			RE::Actor* a_actor,
			RE::NiAVObject* a_model,
			size_t a_groupIndex,
			HolsterSlot& a_slot)
		{
			if (!a_actor || !a_model) {
				return;
			}
			if (a_slot.modelGroupAnimationPlayed.size() <= a_groupIndex) {
				return;
			}
			if (a_slot.modelGroupAnimationPlayed[a_groupIndex]) {
				return;
			}

			auto playSequence = a_groupIndex < a_slot.modelGroupPlaySequence.size() ? a_slot.modelGroupPlaySequence[a_groupIndex] : false;
			auto sequenceName = a_groupIndex < a_slot.modelGroupSequenceNames.size() ? a_slot.modelGroupSequenceNames[a_groupIndex] : "";
			auto animationEvent = a_groupIndex < a_slot.modelGroupAnimationEvents.size() ? a_slot.modelGroupAnimationEvents[a_groupIndex] : "";
			auto forwardAnimationEvents = a_groupIndex < a_slot.modelGroupForwardAnimationEvents.size() ? a_slot.modelGroupForwardAnimationEvents[a_groupIndex] : false;
			auto disableBehaviorGraphAnims = a_groupIndex < a_slot.modelGroupDisableBehaviorGraphAnims.size() ? a_slot.modelGroupDisableBehaviorGraphAnims[a_groupIndex] : true;
			auto* objectNet = static_cast<RE::NiObjectNET*>(a_model);
			auto* controller = objectNet ? RE::NiControllerManager::GetNiControllerManager(objectNet) : nullptr;

			bool consumed = false;
			if (playSequence && controller && !sequenceName.empty()) {
				if (auto* sequence = controller->GetSequenceByName(sequenceName.c_str())) {
					sequence->Activate(0, true, 1.0f, 0.0f, nullptr, false);
					consumed = true;
				}
			}

			if (!playSequence && !disableBehaviorGraphAnims && !animationEvent.empty() && forwardAnimationEvents && a_actor) {
				a_actor->NotifyAnimationGraphImpl(animationEvent.c_str());
				consumed = true;
			}

			a_slot.modelGroupAnimationPlayed[a_groupIndex] = consumed;
		}

		void TryReplayModelGroupAnimationsIfNeeded(RE::Actor* a_actor, HolsterSlot& a_slot)
		{
			if (!a_actor) {
				return;
			}

			const auto modelCount = (a_slot.currentModelGroups.size() < a_slot.modelGroupAnimationPlayed.size()) ?
				a_slot.currentModelGroups.size() :
				a_slot.modelGroupAnimationPlayed.size();
			for (size_t i = 0; i < modelCount; ++i) {
				if (a_slot.modelGroupAnimationPlayed[i]) {
					continue;
				}
				auto* groupModel = a_slot.currentModelGroups[i].get();
				if (!groupModel) {
					continue;
				}
				TryPlayModelGroupAnimation(a_actor, groupModel, i, a_slot);
			}
		}

		std::string GetActorSkeletonPath(RE::Actor* a_actor) {
			if (!a_actor || !a_actor->race) return "";
			auto base = a_actor->data.objectReference ? a_actor->data.objectReference->As<RE::TESNPC>() : nullptr;
			int sex = (base && base->GetSex() == RE::SEX::kFemale) ? 1 : 0;
			return a_actor->race->skeletonModel[sex].model.c_str() ? a_actor->race->skeletonModel[sex].model.c_str() : "";
		}

		bool ActorHas3DNode(RE::Actor* a_actor, const std::string& a_nodeName) {
			if (!a_actor || a_nodeName.empty()) return false;
			auto root = static_cast<RE::NiNode*>(a_actor->Get3D(false));
			return root && NodeManager::GetNodeByName(root, a_nodeName) != nullptr;
		}

		bool MatchesSkeletonConfig(RE::Actor* a_actor, const NodeDefinition::SkeletonMatchConfig& a_match) {
			if (!a_match.enabled) return true;
			if (!a_actor) return false;

			bool matched = true;
			auto base = a_actor->data.objectReference ? a_actor->data.objectReference->As<RE::TESNPC>() : nullptr;
			if (a_match.raceFormID != 0 && (!a_actor->race || a_actor->race->GetFormID() != a_match.raceFormID)) matched = false;
			if (a_match.npcFormID != 0 && (!base || base->GetFormID() != a_match.npcFormID)) matched = false;

			if (!a_match.skeletonPathContains.empty()) {
				auto skeletonPath = GetActorSkeletonPath(a_actor);
				std::string lowerPath = skeletonPath;
				std::string lowerNeedle = a_match.skeletonPathContains;
				std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lowerPath.find(lowerNeedle) == std::string::npos) matched = false;
			}

			for (const auto& nodeName : a_match.requiredNodes) {
				if (!ActorHas3DNode(a_actor, nodeName)) {
					matched = false;
					break;
				}
			}
			for (const auto& nodeName : a_match.forbiddenNodes) {
				if (ActorHas3DNode(a_actor, nodeName)) {
					matched = false;
					break;
				}
			}

			return a_match.invert ? !matched : matched;
		}
	}

	void HolsterManager::StartUpdateLoop() {
		auto taskInterface = F4SE::GetTaskInterface(); if (!taskInterface) return;
		std::thread([this]() {
			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				// 🌟 防洪堤：如果上一个 Update 还没执行完，绝不塞入新的！防止加载界面无限积压！
				if (!_isUpdatingLoop.exchange(true)) {
					auto localTask = F4SE::GetTaskInterface();
					if (localTask) {
						localTask->AddTask([this]() {
							this->Update();
							this->_isUpdatingLoop = false; // 任务执行完，放下闸门
							});
					}
					else {
						this->_isUpdatingLoop = false;
					}
				}
			}
			}).detach();

		if (auto equipMgr = RE::ActorEquipManager::GetSingleton()) {
			equipMgr->RegisterSink(this);
			REX::INFO("[IAD 追踪] 成功注册装备管理事件监听器");
		}

		if (auto containerSource = RE::TESContainerChangedEvent::GetEventSource()) {
			containerSource->RegisterSink(this);
			REX::INFO("[IAD 追踪] 成功注册容器变化事件监听器");
		}

		if (auto raceSwitchSource = RE::TESSwitchRaceCompleteEvent::GetEventSource()) {
			raceSwitchSource->RegisterSink(this);
			REX::INFO("[IAD Lifecycle] registered race-switch listener");
		}
	}

	RE::BSEventNotifyControl HolsterManager::ProcessEvent(const RE::ActorEquipManagerEvent::Event& a_event, RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) {
		if (ModelManager::IsMainMenuTransition()) return RE::BSEventNotifyControl::kContinue;
		if (a_event.actorAffected) {
			const auto actorID = a_event.actorAffected->GetFormID();
			RequestEvaluate(actorID);

			if (a_event.changeType == RE::ActorEquipManagerEvent::Type::kEquip &&
				a_event.itemAffected && a_event.itemAffected->object) {
				auto* object = a_event.itemAffected->object->As<RE::TESBoundObject>();
				if (object) {
					std::lock_guard<std::mutex> lock(_pendingEquipMutex);
					auto& pending = _pendingEquippedForms[actorID];
					pending.push_back({ object->GetFormID(), a_event.stackID });
					while (pending.size() > 8) {
						pending.pop_front();
					}
					RecordRecentBipedSlots(actorID, object);
				}
			}

			if (a_event.actorAffected->IsPlayerRef()) {
				g_playerEquipChanged.store(true);
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl HolsterManager::ProcessEvent(const RE::TESContainerChangedEvent& a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
		if (ModelManager::IsMainMenuTransition()) return RE::BSEventNotifyControl::kContinue;
		if (a_event.itemCount == 0 || a_event.baseObjectFormID == 0) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (a_event.itemCount > 0 && a_event.newContainerFormID != 0) {
			if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_event.newContainerFormID)) {
				RecordRecentAcquired(actor->GetFormID(), a_event.baseObjectFormID);
			}
		}

		const auto requestActorRefresh = [](std::uint32_t a_formID) {
			if (a_formID == 0) return;
			if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(a_formID)) {
				HolsterManager::GetSingleton()->RequestEvaluate(actor->GetFormID());
				if (actor->IsPlayerRef()) {
					g_playerEquipChanged.store(true);
				}
			}
		};

		// Container events describe both sides of a transfer. Refresh the old
		// actor for removals/drops and the new actor for pickups/transfers.
		requestActorRefresh(a_event.oldContainerFormID);
		if (a_event.newContainerFormID != a_event.oldContainerFormID) {
			requestActorRefresh(a_event.newContainerFormID);
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl HolsterManager::ProcessEvent(const RE::TESSwitchRaceCompleteEvent& a_event, RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>*) {
		if (ModelManager::IsMainMenuTransition()) return RE::BSEventNotifyControl::kContinue;
		auto* actor = a_event.actor ? a_event.actor->As<RE::Actor>() : nullptr;
		if (!actor) return RE::BSEventNotifyControl::kContinue;

		const auto actorID = actor->GetFormID();
		if (auto taskInterface = F4SE::GetTaskInterface()) {
			taskInterface->AddTask([actorID]() {
				auto* manager = HolsterManager::GetSingleton();
				NodeManager::ForgetCache(actorID);
				manager->ClearActorSlots(actorID, true);
				manager->RequestEvaluate(actorID);
				REX::INFO("[IAD Lifecycle] rebuilt actor {:08X} after race switch", actorID);
				});
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	void HolsterManager::RecordRecentEquip(RE::TESFormID a_actorID, std::uint64_t a_itemUID) {
		if (a_actorID == 0 || a_itemUID == 0) return;

		std::lock_guard<std::mutex> lock(_recentEquipMutex);
		auto& recent = _recentEquippedItems[a_actorID];
		recent.erase(std::remove(recent.begin(), recent.end(), a_itemUID), recent.end());
		recent.push_front(a_itemUID);

		constexpr std::size_t kMaxRecentEquips = 16;
		while (recent.size() > kMaxRecentEquips) {
			recent.pop_back();
		}
	}

	bool HolsterManager::IsRecentlyEquipped(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const {
		if (a_actorID == 0 || a_itemUID == 0) return false;

		std::lock_guard<std::mutex> lock(_recentEquipMutex);
		auto it = _recentEquippedItems.find(a_actorID);
		if (it == _recentEquippedItems.end()) return false;

		const auto& recent = it->second;
		return std::find(recent.begin(), recent.end(), a_itemUID) != recent.end();
	}

	std::uint64_t HolsterManager::GetRecentEquipScore(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const {
		if (a_actorID == 0 || a_itemUID == 0) return 0;

		std::lock_guard<std::mutex> lock(_recentEquipMutex);
		auto actorIt = _recentEquippedItems.find(a_actorID);
		if (actorIt == _recentEquippedItems.end()) return 0;

		const auto& recent = actorIt->second;
		auto itemIt = std::find(recent.begin(), recent.end(), a_itemUID);
		if (itemIt == recent.end()) return 0;
		return static_cast<std::uint64_t>(std::distance(itemIt, recent.end()));
	}

	void HolsterManager::RecordRecentBipedSlots(RE::TESFormID a_actorID, RE::TESBoundObject* a_item) {
		if (a_actorID == 0 || !a_item) return;
		const auto mask = GetItemBipedMask(a_item);
		if (mask == 0) return;

		std::lock_guard<std::mutex> lock(_recentBipedSlotMutex);
		auto& recentSlots = _recentBipedSlots[a_actorID];
		for (std::uint32_t index = 0; index < 32; ++index) {
			if ((mask & (1u << index)) != 0) {
				recentSlots[index + 30] = ++_recentBipedSlotSequence;
			}
		}
	}

	std::uint64_t HolsterManager::GetRecentBipedSlotScore(RE::TESFormID a_actorID, std::uint32_t a_slot) const {
		if (a_actorID == 0) return 0;

		std::uint32_t index = 0;
		if (!NormalizeBipedSlot(a_slot, index)) return 0;
		const auto normalizedSlot = index + 30;

		std::lock_guard<std::mutex> lock(_recentBipedSlotMutex);
		auto actorIt = _recentBipedSlots.find(a_actorID);
		if (actorIt == _recentBipedSlots.end()) return 0;
		auto slotIt = actorIt->second.find(normalizedSlot);
		return slotIt != actorIt->second.end() ? slotIt->second : 0;
	}

	void HolsterManager::RecordRecentDisplaySlot(RE::TESFormID a_actorID, std::uint64_t a_itemUID, const std::string& a_slotName) {
		if (a_actorID == 0 || a_itemUID == 0 || a_slotName.empty()) return;

		std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
		const auto sequence = ++_recentDisplaySlotSequence;
		_recentDisplaySlots[a_actorID][a_itemUID] = a_slotName;
		_recentDisplayItemScores[a_actorID][a_itemUID] = sequence;
		_recentDisplaySlotScores[a_actorID][a_slotName] = sequence;
	}

	bool HolsterManager::GetRecentDisplaySlot(RE::TESFormID a_actorID, std::uint64_t a_itemUID, std::string& a_outSlotName) const {
		if (a_actorID == 0 || a_itemUID == 0) return false;

		std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
		auto actorIt = _recentDisplaySlots.find(a_actorID);
		if (actorIt == _recentDisplaySlots.end()) return false;

		auto itemIt = actorIt->second.find(a_itemUID);
		if (itemIt == actorIt->second.end() || itemIt->second.empty()) return false;

		a_outSlotName = itemIt->second;
		return true;
	}

	std::uint64_t HolsterManager::GetRecentDisplayItemScore(RE::TESFormID a_actorID, std::uint64_t a_itemUID) const {
		if (a_actorID == 0 || a_itemUID == 0) return 0;

		std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
		auto actorIt = _recentDisplayItemScores.find(a_actorID);
		if (actorIt == _recentDisplayItemScores.end()) return 0;
		auto itemIt = actorIt->second.find(a_itemUID);
		return itemIt != actorIt->second.end() ? itemIt->second : 0;
	}

	std::uint64_t HolsterManager::GetRecentDisplaySlotScore(RE::TESFormID a_actorID, const std::string& a_slotName) const {
		if (a_actorID == 0 || a_slotName.empty()) return 0;

		std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
		auto actorIt = _recentDisplaySlotScores.find(a_actorID);
		if (actorIt == _recentDisplaySlotScores.end()) return 0;
		auto slotIt = actorIt->second.find(a_slotName);
		return slotIt != actorIt->second.end() ? slotIt->second : 0;
	}

	void HolsterManager::RecordRecentAcquired(RE::TESFormID a_actorID, RE::TESFormID a_formID) {
		if (a_actorID == 0 || a_formID == 0) return;

		std::lock_guard<std::mutex> lock(_recentAcquiredMutex);
		auto& actorForms = _recentAcquiredForms[a_actorID];
		actorForms[a_formID] = ++_recentAcquiredSequence;

		static constexpr std::size_t kMaxRecentAcquiredForms = 64;
		if (actorForms.size() <= kMaxRecentAcquiredForms) return;

		auto oldestIt = std::min_element(actorForms.begin(), actorForms.end(), [](const auto& a_lhs, const auto& a_rhs) {
			return a_lhs.second < a_rhs.second;
			});
		if (oldestIt != actorForms.end()) {
			actorForms.erase(oldestIt);
		}
	}

	void HolsterManager::RequestEvaluate(RE::TESFormID a_formID) {
		if (a_formID == 0 || ModelManager::IsMainMenuTransition()) return;
		_refreshScheduler.RequestEvaluate(a_formID);
	}

	void HolsterManager::RequestTransformUpdate(RE::TESFormID a_formID) {
		if (a_formID == 0 || ModelManager::IsMainMenuTransition()) return;
		_refreshScheduler.RequestTransformUpdate(a_formID);
	}

	void HolsterManager::RequestPreviewTransform(
		RE::TESFormID a_formID,
		const std::string& a_name,
		bool a_isMOV,
		const TransformData& a_transform)
	{
		if (a_formID == 0 || a_name.empty() || ModelManager::IsMainMenuTransition()) return;
		std::lock_guard<std::mutex> lock(_previewTransformMutex);
		_pendingPreviewTransform.valid = true;
		_pendingPreviewTransform.reset = false;
		_pendingPreviewTransform.formID = a_formID;
		_pendingPreviewTransform.isMOV = a_isMOV;
		_pendingPreviewTransform.name = a_name;
		_pendingPreviewTransform.transform = a_transform;
	}

	void HolsterManager::RequestPreviewTransformReset(RE::TESFormID a_formID)
	{
		if (a_formID == 0) return;
		std::lock_guard<std::mutex> lock(_previewTransformMutex);
		_pendingPreviewTransform = {};
		_pendingPreviewTransform.valid = true;
		_pendingPreviewTransform.reset = true;
		_pendingPreviewTransform.formID = a_formID;
	}

	void HolsterManager::ProcessPreviewTransformRequest()
	{
		PendingPreviewTransform request;
		{
			std::lock_guard<std::mutex> lock(_previewTransformMutex);
			if (!_pendingPreviewTransform.valid) return;
			request = std::move(_pendingPreviewTransform);
			_pendingPreviewTransform = {};
		}

		std::lock_guard<std::mutex> evalLock(_evalMutex);
		auto slotsIt = _actorDisplaySlots.find(request.formID);
		if (request.reset) {
			if (slotsIt != _actorDisplaySlots.end()) {
				for (auto& [slotName, state] : slotsIt->second) {
					state.previewTransformActive = false;
					state.physicsSim.reset();
				}
			}
			return;
		}

		if (request.isMOV) {
			if (slotsIt == _actorDisplaySlots.end()) {
				REX::WARN("[IAD Preview] MOV preview state is missing for actor {:08X}, name='{}'", request.formID, request.name);
				return;
			}

			// Config slot keys and managed scene-node names are allowed to use
			// different forms. The editor publishes the user-facing name, while
			// _actorDisplaySlots is keyed by the exact JSON slot key. Resolve all
			// supported forms before giving up so a prefixed IAD_MOV_* slot still
			// receives the live preview transform.
			const std::string strippedName = StripManagedNodePrefix(request.name);
			auto slotIt = slotsIt->second.find(request.name);
			if (slotIt == slotsIt->second.end()) slotIt = slotsIt->second.find(strippedName);
			if (slotIt == slotsIt->second.end()) slotIt = slotsIt->second.find(FormatMOVName(strippedName));
			if (slotIt == slotsIt->second.end()) {
				for (auto it = slotsIt->second.begin(); it != slotsIt->second.end(); ++it) {
					if (StripManagedNodePrefix(it->first) == strippedName) {
						slotIt = it;
						break;
					}
				}
			}
			if (slotIt == slotsIt->second.end()) {
				REX::WARN("[IAD Preview] MOV preview target not found for actor {:08X}, name='{}', normalized='{}'", request.formID, request.name, strippedName);
				return;
			}

			slotIt->second.slotBaseTransform = request.transform;
			slotIt->second.previewTransformActive = true;
			slotIt->second.physicsSim.reset();
			return;
		}

		auto nodesIt = _actorNodeStates.find(request.formID);
		if (nodesIt == _actorNodeStates.end()) {
			REX::WARN("[IAD Preview] CME preview state is missing for actor {:08X}, name='{}'", request.formID, request.name);
			return;
		}
		const std::string strippedName = StripManagedNodePrefix(request.name);
		auto nodeIt = nodesIt->second.find(request.name);
		if (nodeIt == nodesIt->second.end()) nodeIt = nodesIt->second.find(strippedName);
		if (nodeIt == nodesIt->second.end()) nodeIt = nodesIt->second.find(FormatCMEName(strippedName));
		if (nodeIt == nodesIt->second.end()) {
			for (auto it = nodesIt->second.begin(); it != nodesIt->second.end(); ++it) {
				if (StripManagedNodePrefix(it->first) == strippedName) {
					nodeIt = it;
					break;
				}
			}
		}
		if (nodeIt == nodesIt->second.end()) {
			REX::WARN("[IAD Preview] CME preview target not found for actor {:08X}, name='{}', normalized='{}'", request.formID, request.name, strippedName);
			return;
		}
		nodeIt->second.finalTransform = request.transform;
	}

	void HolsterManager::RequestEvaluateAll() {
		if (ModelManager::IsMainMenuTransition()) return;
		auto queueActorEvaluation = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor->IsDead(false) || a_actor->IsDeleted() || a_actor->IsDisabled()) {
				return;
			}
			_refreshScheduler.RequestEvaluate(a_actor->GetFormID());
		};

		auto player = RE::PlayerCharacter::GetSingleton();
		queueActorEvaluation(player);
		if (auto pl = RE::ProcessLists::GetSingleton()) {
			for (auto& pHandle : pl->highActorHandles) {
				if (auto actorPtr = pHandle.get()) {
					queueActorEvaluation(actorPtr.get());
				}
			}
		}
	}

	bool HolsterManager::BeginEvaluation(RE::TESFormID a_formID, bool a_force) {
		return _refreshScheduler.BeginEvaluation(a_formID, a_force);
	}

	void HolsterManager::CompleteEvaluation(RE::TESFormID a_formID) {
		_refreshScheduler.CompleteEvaluation(a_formID, _currentUpdateTick);
	}

	void HolsterManager::ClearActorHistory(RE::TESFormID a_formID) {
		{
			std::lock_guard<std::mutex> lock(_recentEquipMutex);
			_recentEquippedItems.erase(a_formID);
		}
		{
			std::lock_guard<std::mutex> lock(_recentBipedSlotMutex);
			_recentBipedSlots.erase(a_formID);
		}
		{
			std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
			_recentDisplaySlots.erase(a_formID);
			_recentDisplayItemScores.erase(a_formID);
			_recentDisplaySlotScores.erase(a_formID);
		}
		{
			std::lock_guard<std::mutex> lock(_pendingEquipMutex);
			_pendingEquippedForms.erase(a_formID);
		}
		{
			std::lock_guard<std::mutex> lock(_recentAcquiredMutex);
			_recentAcquiredForms.erase(a_formID);
		}
	}

	void HolsterManager::RunLowFrequencyMaintenance(RE::TESFormID a_playerID) {
		constexpr uint64_t kSweepIntervalTicks = 600;
		constexpr uint64_t kStaleActorTicks = 1800;
		if (_currentUpdateTick - _lastLFSweepTime < kSweepIntervalTicks) return;

		_lastLFSweepTime = _currentUpdateTick;
		const auto retiredActors = _refreshScheduler.CollectStaleActors(
			a_playerID,
			_currentUpdateTick,
			kStaleActorTicks);

		for (const auto actorID : retiredActors) {
			ClearActorSlots(actorID, true);
			NodeManager::ForgetCache(actorID);
			ClearActorHistory(actorID);
			REX::INFO("[IAD Lifecycle] retired inactive actor {:08X}", actorID);
		}
	}

	void HolsterManager::ExecuteForceRefresh() {
		RequestEvaluateAll();
	}

	void HolsterManager::ClearActorSlots(RE::TESFormID a_formID, bool a_skipSceneDetach) {
		std::lock_guard<std::mutex> evalLock(_evalMutex); // 🌟 多线程防爆锁
		ClearActorSlots_Internal(a_formID, a_skipSceneDetach);
	}

	void HolsterManager::ClearActorSlots_Internal(RE::TESFormID a_formID, bool a_skipSceneDetach, bool a_forceSceneDetach) noexcept {
		auto it = _actorDisplaySlots.find(a_formID);
		if (it != _actorDisplaySlots.end()) {
			for (auto& [slotName, sState] : it->second) {
				// 永远无条件执行安全释放，绝不制造野指针！
				ActorDisplayLifecycle::ClearSlotModels(sState, a_skipSceneDetach, a_forceSceneDetach);

				sState.lastTargetNode = ""; sState.lastHolsterPath = ""; sState.lastModelRequestSignature = ""; sState.lastModelGroupSignature = "";
				sState.lastItem = nullptr;
				sState.currentUID = 0;
				sState.isEquipped = false;
				sState.hideWeaponWhenDrawn = false;
				sState.isSlotHidden = false; sState.isWeaponHidden = false; sState.isHolsterHidden = false;
				sState.modelGroupTransforms.clear();
				sState.modelGroupGeometryTransforms.clear();
				sState.modelGroupOverrideGeometryTransforms.clear();
				sState.modelGroupHideWithWeapon.clear();
				sState.modelGroupConditionVisible.clear();
				sState.modelGroupInvisible.clear();
				sState.modelGroupHideGeometry.clear();
				sState.modelGroupEffects.clear();
				sState.modelGroupLights.clear();
				sState.modelGroupMovNames.clear();
				sState.modelGroupPlaySequence.clear();
				sState.modelGroupForwardAnimationEvents.clear();
				sState.modelGroupDisableBehaviorGraphAnims.clear();
				sState.modelGroupSequenceNames.clear();
				sState.modelGroupAnimationEvents.clear();
				sState.modelGroupAnimationPlayed.clear();
				sState.physicsSim.reset();
			}
			_actorDisplaySlots.erase(it);
		}
		auto nodeIt = _actorNodeStates.find(a_formID);
		if (nodeIt != _actorNodeStates.end()) {
			_actorNodeStates.erase(nodeIt);
		}
	}

	void HolsterManager::SetNPCDisplaysEnabled(bool a_enabled) {
		std::lock_guard<std::mutex> evalLock(_evalMutex);
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto playerID = player ? player->GetFormID() : 0;

		if (!a_enabled) {
			for (auto& [actorID, actorSlots] : _actorDisplaySlots) {
				if (actorID == playerID) continue;
				for (auto& [slotName, state] : actorSlots) {
					auto cull = [](auto& models) {
						for (auto& model : models) {
							if (model) model->SetAppCulled(true);
						}
					};
					cull(state.currentModels);
					cull(state.currentHolsters);
					cull(state.currentModelGroups);
					cull(state.oldModels);
					cull(state.oldHolsters);
					cull(state.oldModelGroups);
					state.physicsSim.reset();
				}
			}
		}

		_refreshScheduler.RequestGlobalRefresh();
	}

	void HolsterManager::ClearAllActorSlots(bool a_skipSceneDetach) {
		std::lock_guard<std::mutex> evalLock(_evalMutex);
		for (auto& [formID, actorSlots] : _actorDisplaySlots) {
			for (auto& [slotName, sState] : actorSlots) {
				ActorDisplayLifecycle::ClearSlotModels(sState, a_skipSceneDetach);
			}
		}
		_actorDisplaySlots.clear();
		_actorNodeStates.clear();
		{
			std::lock_guard<std::mutex> lock(_recentEquipMutex);
			_recentEquippedItems.clear();
		}
		{
			std::lock_guard<std::mutex> lock(_pendingEquipMutex);
			_pendingEquippedForms.clear();
		}
		{
			std::lock_guard<std::mutex> lock(_recentBipedSlotMutex);
			_recentBipedSlots.clear();
			_recentBipedSlotSequence = 0;
		}
		{
			std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
			_recentDisplaySlots.clear();
			_recentDisplayItemScores.clear();
			_recentDisplaySlotScores.clear();
			_recentDisplaySlotSequence = 0;
		}
		{
			std::lock_guard<std::mutex> lock(_recentAcquiredMutex);
			_recentAcquiredForms.clear();
			_recentAcquiredSequence = 0;
		}
		_refreshScheduler.ClearAll();
	}

	void HolsterManager::DetachAllActorSlotsForSceneTeardown() {
		std::lock_guard<std::mutex> evalLock(_evalMutex);
		std::size_t actorCount = _actorDisplaySlots.size();
		for (auto& [formID, actorSlots] : _actorDisplaySlots) {
			for (auto& [slotName, sState] : actorSlots) {
				ActorDisplayLifecycle::ClearSlotModels(sState, false, true);
			}
		}
		_actorDisplaySlots.clear();
		_actorNodeStates.clear();
		{
			std::lock_guard<std::mutex> lock(_recentEquipMutex);
			_recentEquippedItems.clear();
		}
		{
			std::lock_guard<std::mutex> lock(_pendingEquipMutex);
			_pendingEquippedForms.clear();
		}
		{
			std::lock_guard<std::mutex> lock(_recentBipedSlotMutex);
			_recentBipedSlots.clear();
			_recentBipedSlotSequence = 0;
		}
		{
			std::lock_guard<std::mutex> lock(_recentDisplaySlotMutex);
			_recentDisplaySlots.clear();
			_recentDisplayItemScores.clear();
			_recentDisplaySlotScores.clear();
			_recentDisplaySlotSequence = 0;
		}
		{
			std::lock_guard<std::mutex> lock(_recentAcquiredMutex);
			_recentAcquiredForms.clear();
			_recentAcquiredSequence = 0;
		}
		_refreshScheduler.ClearAll();
		REX::INFO("[IAD Lifecycle] scene teardown detached display models for {} actor(s)", actorCount);
	}

	void HolsterManager::Update() {
		_currentUpdateTick++;
		UI::UIWorldPreviewSession::GetSingleton().ProcessGameThread();
		DebugSettings debugSettingsSnapshot;
		{
			std::lock_guard<std::mutex> lock(debugSettingsMutex);
			debugSettingsSnapshot = debugSettings;
		}
		auto* config = ConfigManager::GetSingleton();
		const auto runtimeSettings = config->GetRuntimeSettingsSnapshot();
		auto* modelManager = ModelManager::GetSingleton();
		modelManager->ProcessGarbageCollection();
		if (ModelManager::IsMainMenuTransition()) return;
		modelManager->ProcessAsyncQueue();
		// Apply the latest editor value once per game update. The render/UI thread
		// may publish many mouse samples between updates; only the newest one is
		// relevant and this keeps scene writes on the game thread.
		ProcessPreviewTransformRequest();
		// Keep scene attachment and physics responsive, but schedule NPC inventory
		// resolution as a distinct medium-frequency phase. Pending actor flags remain
		// latched until this phase consumes them.
		const auto npcEvaluationIntervalTicks = std::clamp<std::uint64_t>(
			runtimeSettings.npcEvaluationIntervalTicks,
			1,
			60);
		const bool runMediumPhase = (_currentUpdateTick % npcEvaluationIntervalTicks) == 0;

		static int s_loadDelayFrames = 0;
		static std::uint64_t s_previewContourStableSince = 0;
		static RE::NiNode* s_previewContourRoot = nullptr;

		if (_refreshScheduler.ConsumeGlobalRefreshRequest()) {
			if (_refreshScheduler.TryQueueGlobalRefreshTask()) {
				REX::INFO("[IAD Refresh] queued consolidated global refresh");
				auto taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([]() {
						auto* manager = IAD::HolsterManager::GetSingleton();
						manager->ExecuteForceRefresh();
						manager->_refreshScheduler.CompleteGlobalRefreshTask();
						});
					s_loadDelayFrames = 1;
				}
				else {
					_refreshScheduler.RequeueGlobalRefresh();
				}
			}
			else {
				// A task is already pending. Preserve the request for the next
				// task completion instead of losing a global condition change.
				_refreshScheduler.RequestGlobalRefresh();
			}
		}

		auto player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			// VATS weapon body part diagnostic (runs once, when player process is ready)
			static bool s_vatsDiagDone = false;
			if (!s_vatsDiagDone) {
				auto* proc = player->currentProcess;
				if (proc && proc->middleHigh && proc->middleHigh->weaponBone) {
					auto* wb = proc->middleHigh->weaponBone;
					REX::INFO("[IAD VATS DIAG] Player weaponBone: valid (name='{}')",
						wb->name.c_str());
					REX::INFO("[IAD VATS DIAG] Player damageRootNode dump:");
					int count = 0;
					for (int i = 0; i < 26; i++) {
						auto* n = proc->middleHigh->damageRootNode[i];
						if (n) {
							REX::INFO("[IAD VATS DIAG]   [{}] '{}'", i, n->name.c_str());
							count++;
						}
					}
					REX::INFO("[IAD VATS DIAG] Total {} non-null damageRootNode entries", count);
					s_vatsDiagDone = true;
				}
			}
			static bool s_lastFirstPerson = false;
			static RE::WEAPON_STATE s_lastWeaponState = RE::WEAPON_STATE::kSheathed;
			static bool s_wasPipboyOpen = false;

			if (s_loadDelayFrames > 0) {
				auto ui = RE::UI::GetSingleton();
				bool isLoading = ui && (ui->GetMenuOpen("LoadingMenu") || ui->GetMenuOpen("FaderMenu"));

				// 采用 Get3D(false) 替换 Is3DLoaded
				if (!isLoading && player->Get3D(false) != nullptr && NodeManager::Is3DSafeAndCacheReady(player, _currentUpdateTick)) {
					s_loadDelayFrames++;
					if (s_loadDelayFrames > 60) {
						REX::INFO("[IAD 追踪] ===> 引擎已彻底稳定！下发终极强制算命指令！");

						RequestEvaluate(player->GetFormID());

						s_lastFirstPerson = ConditionEvaluator::IsFirstPerson(player);
						s_lastWeaponState = player->weaponState;
						s_wasPipboyOpen = ui && ui->GetMenuOpen("PipboyMenu");

						s_loadDelayFrames = 0;
					}
				}
			}

			bool currentFP = ConditionEvaluator::IsFirstPerson(player);
			if (currentFP != s_lastFirstPerson) { RequestEvaluate(player->GetFormID()); s_lastFirstPerson = currentFP; }

			if (player->weaponState != s_lastWeaponState) {
				RequestEvaluate(player->GetFormID());
				s_lastWeaponState = player->weaponState;
			}

			// 换枪监控
			if (g_playerEquipChanged.exchange(false)) {
				RequestEvaluate(player->GetFormID());
			}

			// Keep a fallback poll for script-driven inventory changes. Include stack
			// counts and per-stack equip/favorite state so splitting or consuming a
			// stack is visible even when the list length stays constant.
			static std::uint64_t s_lastInventorySignature = 0;
			const auto currentInventorySignature = GetInventorySignature(player);
			if (currentInventorySignature != s_lastInventorySignature) {
				RequestEvaluate(player->GetFormID());
				s_lastInventorySignature = currentInventorySignature;
			}

			// Pip-boy 监控
			auto ui = RE::UI::GetSingleton();
			bool isPipboyOpen = ui && ui->GetMenuOpen("PipboyMenu");
			if (s_wasPipboyOpen && !isPipboyOpen) {
				RequestEvaluate(player->GetFormID());
			}
			s_wasPipboyOpen = isPipboyOpen;
		}

		const auto conditionRefresh = _conditionRefreshPolicy.Update(_currentUpdateTick, player);
		if (conditionRefresh.keyBindStateChanged) {
			RequestEvaluateAll();
		}
		if (conditionRefresh.conditionalVariablesChanged) {
			REX::INFO("[IAD ConditionalVariable] player condition values changed; queued refresh");
			RequestEvaluateAll();
		}
		if (conditionRefresh.questStageChanged) {
			RequestEvaluateAll();
		}
		const auto& watchedActiveEffectFormIDs = conditionRefresh.watchedActiveEffectFormIDs;
		const bool pollActiveEffects = conditionRefresh.pollActiveEffects;

		std::vector<RE::Actor*> activeActors;
		std::unordered_set<RE::TESFormID> activeActorIDs;
		auto addActiveActor = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor->GetFormID() == 0) return;
			if (activeActorIDs.emplace(a_actor->GetFormID()).second) {
				activeActors.push_back(a_actor);
			}
		};
		addActiveActor(player);
		if (auto pl = RE::ProcessLists::GetSingleton()) {
			for (auto& pHandle : pl->highActorHandles) {
				if (auto actorPtr = pHandle.get()) addActiveActor(actorPtr.get());
			}
		}

		std::vector<DebugBox> newBoxes;
		std::vector<DebugNode> newNodes;
		std::vector<DebugBoundSphere> newBoundSpheres;

		RE::NiPoint3 playerPos{ 0.0f, 0.0f, 0.0f };
		if (player) {
			auto pos = player->GetPosition();
			playerPos.x = pos.x; playerPos.y = pos.y; playerPos.z = pos.z;
		}

		std::vector<RE::Actor*> validNPCsForEval;
		const bool processNPCDisplays = runtimeSettings.enableNPCDisplays;

		for (auto* actor : activeActors) {
			if (!actor) continue;
			if (actor != player && !processNPCDisplays) continue;

			if (actor->IsDead(false) || actor->IsDeleted() || actor->IsDisabled()) {
				if (_refreshScheduler.MarkRetired(actor->GetFormID())) {
					auto formID = actor->GetFormID();
					auto taskInterface = F4SE::GetTaskInterface();
					if (taskInterface) {
						taskInterface->AddTask([formID]() {
							// A death task can run after an in-game load has already restored
							// the same actor FormID. Revalidate before touching the restored
							// scene so a stale deferred task cannot clear new display models.
							if (auto* actor = RE::TESForm::GetFormByID<RE::Actor>(formID);
								actor && (actor->IsDead(false) || actor->IsDeleted() || actor->IsDisabled())) {
								auto* manager = IAD::HolsterManager::GetSingleton();
								manager->ClearActorSlots(formID, false);
								manager->ClearActorHistory(formID);
								REX::INFO("[IAD Lifecycle] cleared dead actor display state {:08X}", formID);
							}
						});
					}
				}
				continue;
			}
			else {
				const auto activeEffectSignature = pollActiveEffects ? ConditionRefreshPolicy::GetActiveEffectSignature(actor, watchedActiveEffectFormIDs) : 0;
				const auto observation = _refreshScheduler.ObserveActor(
					actor->GetFormID(),
					_currentUpdateTick,
					pollActiveEffects,
					activeEffectSignature);
				const bool newlyTracked = observation.newlyTracked;
				const bool needsCacheClear = observation.needsCacheClear;
				if (observation.activeEffectChanged && actor == player) {
					REX::INFO("[IAD Condition] player active effects changed; queued refresh");
				}
				if (needsCacheClear) {
					NodeManager::ClearCache(actor->GetFormID());
				}
				if (newlyTracked && actor != player) {
					REX::INFO("[IAD Lifecycle] tracking actor {:08X}", actor->GetFormID());
				}
			}

			if (actor != player) {
				auto pos = actor->GetPosition();
				float distSq = (playerPos.x - pos.x) * (playerPos.x - pos.x) +
					(playerPos.y - pos.y) * (playerPos.y - pos.y) +
					(playerPos.z - pos.z) * (playerPos.z - pos.z);
				if (distSq > 9000000.0f) continue;
			}

			if (!NodeManager::Is3DSafeAndCacheReady(actor, _currentUpdateTick)) continue;

			if (actor != player && runMediumPhase) {
				validNPCsForEval.push_back(actor);
			}

			UpdateActorTransforms(actor, newBoxes, newNodes);
				// [DISABLED] VATS combat override
				// {
				//     bool hasIAD = false;
				//     {
				//         std::lock_guard<std::mutex> cacheLock(IAD::NodeManager::_cacheMutex);
				//         auto slotIt = _actorDisplaySlots.find(actor->GetFormID());
				//         hasIAD = (slotIt != _actorDisplaySlots.end());
				//     }
				//     IAD::Combat::OverrideWeaponBodyPart(actor, hasIAD);
				// }
		}

		RunLowFrequencyMaintenance(player ? player->GetFormID() : 0);

		// A post-load 3D root can be visible before the display clones and their
		// renderer buffers have finished settling. The normal evaluation delay is
		// intentionally separate from contour sampling, so keep a root-specific
		// grace window here as well. This also catches the same-tick second
		// Is3DSafeAndCacheReady call after it observes a root replacement.
		bool previewContourReady = false;
		if (player && debugSettingsSnapshot.worldPreviewEdit &&
			!player->IsDead(false) && !player->IsDeleted() && !player->IsDisabled()) {
			auto* ui = RE::UI::GetSingleton();
			const bool uiLoading = ui && (ui->GetMenuOpen("LoadingMenu") || ui->GetMenuOpen("FaderMenu"));
			auto* currentRoot = player->Get3D(false) ? player->Get3D(false)->IsNode() : nullptr;
			const bool sceneReady = !ModelManager::IsGameLoading() && !ModelManager::IsMainMenuTransition() &&
				!uiLoading && player->GetFullyLoaded3D() != nullptr && currentRoot != nullptr &&
				NodeManager::Is3DSafeAndCacheReady(player, _currentUpdateTick);

			constexpr std::uint64_t kPreviewContourSettleTicks = 120;
			if (!sceneReady || currentRoot != s_previewContourRoot) {
				s_previewContourRoot = currentRoot;
				s_previewContourStableSince = 0;
			}
			else {
				if (s_previewContourStableSince == 0) s_previewContourStableSince = _currentUpdateTick;
				previewContourReady = _currentUpdateTick - s_previewContourStableSince >= kPreviewContourSettleTicks;
			}
		}
		else {
			s_previewContourRoot = nullptr;
			s_previewContourStableSince = 0;
		}

		if (previewContourReady) {
			CollectModelBoundSpheres(player, newBoundSpheres);
		}

		if (player && !player->IsDead(false) && !player->IsDeleted() && !player->IsDisabled() && NodeManager::Is3DSafeAndCacheReady(player, _currentUpdateTick)) {
			// Fallout 4 can emit several inventory and equip events for one player
			// action. Keep the request latched, but merge the resulting scans.
			constexpr std::uint64_t kPlayerEvaluationMinIntervalTicks = 8;
			bool playerEvaluationDue = false;
			const auto playerRefresh = _refreshScheduler.Snapshot(player->GetFormID());
			playerEvaluationDue = !playerRefresh.retired &&
				!playerRefresh.evaluationQueued &&
				playerRefresh.HasPending(ActorRefreshFlag::kEvaluateEquip) &&
				(playerRefresh.lastEvaluationTick == 0 ||
					_currentUpdateTick - playerRefresh.lastEvaluationTick >= kPlayerEvaluationMinIntervalTicks);

			if (playerEvaluationDue && BeginEvaluation(player->GetFormID(), false)) {
				auto formID = player->GetFormID();
				auto taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([formID]() {
						auto* manager = IAD::HolsterManager::GetSingleton();
						if (auto p = RE::TESForm::GetFormByID<RE::Actor>(formID)) {
							manager->EvaluateActor(p);
						}
						manager->CompleteEvaluation(formID);
						});
				}
				else {
					CompleteEvaluation(formID);
				}
			}
		}

		if (!validNPCsForEval.empty()) {
			static size_t s_rrIndex = 0;

			if (s_rrIndex >= validNPCsForEval.size()) s_rrIndex = 0;
			RE::Actor* targetNPC = validNPCsForEval[s_rrIndex];
			s_rrIndex++;

			bool needsEval = false;
			const auto npcRefresh = _refreshScheduler.Snapshot(targetNPC->GetFormID());
			needsEval = !npcRefresh.retired &&
				!npcRefresh.evaluationQueued &&
				(npcRefresh.HasPending(ActorRefreshFlag::kEvaluateEquip) ||
					(_currentUpdateTick - npcRefresh.lastEvaluationTick > 120));

			if (needsEval && BeginEvaluation(targetNPC->GetFormID(), true)) {
				auto formID = targetNPC->GetFormID();
				auto taskInterface = F4SE::GetTaskInterface();
				if (taskInterface) {
					taskInterface->AddTask([formID]() {
						auto* manager = IAD::HolsterManager::GetSingleton();
						if (auto npc = RE::TESForm::GetFormByID<RE::Actor>(formID)) {
							manager->EvaluateActor(npc);
						}
						manager->CompleteEvaluation(formID);
						});
				}
				else {
					CompleteEvaluation(formID);
				}
			}
		}

		ActorDisplayIdentity displayIdentity;
		if (player) {
			displayIdentity.actorFormID = player->GetFormID();
			displayIdentity.actor3DGeneration = NodeManager::GetActor3DGeneration(player->GetFormID());
		}
		displayIdentity.sceneGeneration = ModelManager::GetSceneGeneration();
		ActorDisplaySnapshot displaySnapshot;
		displaySnapshot.identity = displayIdentity;
		displaySnapshot.settings = debugSettingsSnapshot;
		displaySnapshot.boxes = std::move(newBoxes);
		displaySnapshot.nodes = std::move(newNodes);
		displaySnapshot.modelBounds = std::move(newBoundSpheres);
		ActorDisplayContext::GetSingleton().Publish(std::move(displaySnapshot));
	}

	void HolsterManager::EvaluateActor(RE::Actor* a_actor) {
		std::lock_guard<std::mutex> evalLock(_evalMutex); // 🌟 多线程防爆锁：阻止并发疯狂读写！
		if (!a_actor || ModelManager::IsMainMenuTransition()) return;

		auto* config = ConfigManager::GetSingleton();
		auto* modelManager = ModelManager::GetSingleton();
		const auto runtimeSettings = config->GetRuntimeSettingsSnapshot();
		RE::TESFormID actorID = a_actor->GetFormID();
		if (config->IsActorDisplayBlocked(a_actor)) {
			ActorRuntimeContext::GetSingleton().Invalidate(actorID);
			auto slotStatesIt = _actorDisplaySlots.find(actorID);
			if (slotStatesIt != _actorDisplaySlots.end()) {
				auto cull = [](auto& models) {
					for (auto& model : models) {
						if (model) model->SetAppCulled(true);
					}
				};
				for (auto& [slotName, state] : slotStatesIt->second) {
					cull(state.currentModels);
					cull(state.currentHolsters);
					cull(state.currentModelGroups);
					cull(state.oldModels);
					cull(state.oldHolsters);
					cull(state.oldModelGroups);
					state.physicsSim.reset();
				}
			}
			return;
		}
		auto& nodeStates = _actorNodeStates[actorID];
		auto& sStates = _actorDisplaySlots[actorID];

		bool isPlayer = a_actor->IsPlayerRef();
		if (!isPlayer && !runtimeSettings.enableNPCDisplays) {
			ActorRuntimeContext::GetSingleton().Invalidate(actorID);
			// FO4 may still traverse cloth data attached beneath an IAD node while a
			// nearby actor's 3D is being torn down. Cull only; lifecycle cleanup owns
			// detachment at the established safe boundaries.
			for (auto& [slotName, state] : sStates) {
				auto cull = [](auto& models) {
					for (auto& model : models) {
						if (model) model->SetAppCulled(true);
					}
				};
				cull(state.currentModels);
				cull(state.currentHolsters);
				cull(state.currentModelGroups);
			}
			return;
		}
		if (a_actor->IsDead(false)) {
			ActorRuntimeContext::GetSingleton().Invalidate(actorID);
			ClearActorSlots_Internal(actorID, true);
			return;
		}

		// 替换 GetActorBase
		auto base = a_actor->data.objectReference ? a_actor->data.objectReference->As<RE::TESNPC>() : nullptr;
		bool isFemale = (base && base->GetSex() == RE::SEX::kFemale);

		struct ConditionEvalCacheKey {
			const ConditionNode* node = nullptr;
			std::uint64_t itemUID = 0;

			bool operator==(const ConditionEvalCacheKey& a_rhs) const noexcept {
				return node == a_rhs.node && itemUID == a_rhs.itemUID;
			}
		};
		struct ConditionEvalCacheHash {
			std::size_t operator()(const ConditionEvalCacheKey& a_key) const noexcept {
				auto seed = std::hash<const ConditionNode*>{}(a_key.node);
				seed ^= std::hash<std::uint64_t>{}(a_key.itemUID) + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
				return seed;
			}
		};
		std::unordered_map<ConditionEvalCacheKey, bool, ConditionEvalCacheHash> conditionEvalCache;
		auto EvaluateConditionCached = [&](const ConditionNode& node, const ActiveItem* item = nullptr) -> bool {
			ConditionEvalCacheKey key{ &node, item ? item->uid : 0 };
			if (auto it = conditionEvalCache.find(key); it != conditionEvalCache.end()) {
				return it->second;
			}
			const bool result = ConditionEvaluator::EvaluateConditionTree(a_actor, node, const_cast<ActiveItem*>(item));
			conditionEvalCache.emplace(key, result);
			return result;
			};

		auto runtimeConfigSnapshot = config->GetRuntimeConfigSnapshot(a_actor);
		auto& scopedNodes = runtimeConfigSnapshot.scopedNodes;
		for (auto& scoped : scopedNodes) {
			auto& nDef = scoped.data;
			auto& nState = nodeStates[nDef.nodeName];

			if (!nDef.isEnabled || !MatchesSkeletonConfig(a_actor, nDef.skeletonMatch)) {
				nState.isHidden = true;
				nState.targetBones.clear();
				continue;
			}

			nState.finalTransform = isFemale ? nDef.transforms.f : nDef.transforms.m;
			nState.targetBones = nDef.fallbackHosts;
			nState.isAbsolute = nDef.absolutePosition;
			nState.isHidden = !EvaluateConditionCached(nDef.displayConditionTree);
			nState.activePhys = nDef.physics;

			for (const auto& state : nDef.stateMachine) {
				if (EvaluateConditionCached(state.conditionTree)) {
					if (state.hideModel) nState.isHidden = true;
					if (state.overrideTargetNode && !state.targetNode.empty()) nState.targetBones = { state.targetNode };
					if (state.overrideTransform) {
						nState.finalTransform = ResolveStateTransform(state);
						nState.isAbsolute = state.absolutePosition;
					}
					if (state.overridePhysics) nState.activePhys = ResolveStatePhysics(state);
					if (!state.continueAfterMatch) break;
				}
			}
		}

		auto candidateItems = Scanner::GetActiveItems(a_actor);
		if (isPlayer) REX::INFO("[IAD 追踪] 玩家背包扫描完成，符合大类的物品数量: {}", candidateItems.size());

		// IED keeps last-equipped state from equip events. Fallout 4 can leave more
		// than one stack marked equipped, so do not rewrite history on every scan.
		std::vector<std::uint64_t> newlyEquippedUIDs;
		{
			std::lock_guard<std::mutex> lock(_pendingEquipMutex);
			auto pendingIt = _pendingEquippedForms.find(actorID);
			if (pendingIt != _pendingEquippedForms.end()) {
				auto& pending = pendingIt->second;
				for (auto it = pending.begin(); it != pending.end();) {
					auto equippedIt = std::find_if(candidateItems.begin(), candidateItems.end(), [&](const ActiveItem& item) {
						return item.isEquipped && item.object &&
							item.object->GetFormID() == it->formID && item.stackID == it->stackID;
						});
					if (equippedIt == candidateItems.end()) {
						equippedIt = std::find_if(candidateItems.begin(), candidateItems.end(), [&](const ActiveItem& item) {
							return item.isEquipped && item.object && item.object->GetFormID() == it->formID;
							});
					}
					if (equippedIt != candidateItems.end()) {
						newlyEquippedUIDs.push_back(equippedIt->uid);
						it = pending.erase(it);
					}
					else {
						++it;
					}
				}
				if (pending.empty()) {
					_pendingEquippedForms.erase(pendingIt);
				}
			}
		}
		for (const auto uid : newlyEquippedUIDs) {
			RecordRecentEquip(actorID, uid);
		}

		for (const auto& item : candidateItems) {
			if (item.isEquipped) {
				// Seed state after a load without disturbing an established event order.
				if (!IsRecentlyEquipped(actorID, item.uid)) {
					RecordRecentEquip(actorID, item.uid);
				}
			}
		}

		std::unordered_map<RE::TESFormID, std::uint64_t> acquiredScores;
		{
			std::lock_guard<std::mutex> lock(_recentAcquiredMutex);
			if (auto it = _recentAcquiredForms.find(actorID); it != _recentAcquiredForms.end()) {
				acquiredScores = it->second;
			}
		}

		auto GetAcquiredScore = [&](const ActiveItem& item) -> std::uint64_t {
			if (!item.object) return 0;
			if (!config->IsRecentAcquiredFormTypeEnabled(static_cast<std::uint8_t>(item.object->GetFormType()))) return 0;
			auto it = acquiredScores.find(item.object->GetFormID());
			return it != acquiredScores.end() ? it->second : 0;
			};

		auto HasFormType = [](const std::vector<std::uint8_t>& types, std::uint8_t formType) {
			return std::find(types.begin(), types.end(), formType) != types.end();
			};

		AssignmentResolver::SortCandidates(candidateItems, {
			runtimeSettings.prioritizeEquippedCandidates,
			GetAcquiredScore
		});

		auto& scopedSlots = runtimeConfigSnapshot.scopedSlots;
		auto& scopedCustoms = runtimeConfigSnapshot.scopedCustoms;

		ActorRuntimeSnapshot runtimeSnapshot;
		runtimeSnapshot.identity.actorFormID = actorID;
		runtimeSnapshot.identity.sceneGeneration = ModelManager::GetSceneGeneration();
		runtimeSnapshot.identity.actor3DGeneration = NodeManager::GetActor3DGeneration(actorID);
		runtimeSnapshot.runtimeSettings = runtimeSettings;
		runtimeSnapshot.isPlayer = isPlayer;
		runtimeSnapshot.isFemale = isFemale;
		runtimeSnapshot.actor3DReady = a_actor->Get3D(false) != nullptr;
	runtimeSnapshot.candidateItems.reserve(candidateItems.size());
		for (const auto& item : candidateItems) {
			ActorRuntimeItemSnapshot itemSnapshot;
			itemSnapshot.formID = item.object ? item.object->GetFormID() : 0;
			itemSnapshot.stackID = item.stackID;
			itemSnapshot.uid = item.uid;
			itemSnapshot.count = item.count;
			itemSnapshot.isEquipped = item.isEquipped;
			itemSnapshot.isFavorited = item.isFavorited;
			itemSnapshot.ratingUsesInstanceData = item.ratingUsesInstanceData;
			itemSnapshot.rating = item.rating;
			runtimeSnapshot.candidateItems.push_back(itemSnapshot);
		}
		runtimeSnapshot.scopedSlots = scopedSlots;
		runtimeSnapshot.scopedCustoms = scopedCustoms;
		runtimeSnapshot.nodeStates.reserve(nodeStates.size());
		for (const auto& [nodeName, nodeState] : nodeStates) {
			ActorRuntimeNodeSnapshot nodeSnapshot;
			nodeSnapshot.nodeName = nodeName;
			nodeSnapshot.isHidden = nodeState.isHidden;
			nodeSnapshot.isAbsolute = nodeState.isAbsolute;
			nodeSnapshot.finalTransform = nodeState.finalTransform;
			nodeSnapshot.activePhys = nodeState.activePhys;
			nodeSnapshot.targetBones = nodeState.targetBones;
			runtimeSnapshot.nodeStates.push_back(std::move(nodeSnapshot));
		}
		ActorRuntimeContext::GetSingleton().Publish(std::move(runtimeSnapshot));

		const auto sortedSlots = AssignmentResolver::BuildSlotOrder(scopedSlots);

		auto AssignmentSourceName = [](AssignmentSource a_source) -> const char* {
			switch (a_source) {
			case AssignmentSource::kDedicatedAmmo: return "dedicated-ammo";
			case AssignmentSource::kCustomConfiguredSlot: return "custom-configured-slot";
			case AssignmentSource::kCustomRecentSlot: return "custom-recent-slot";
			case AssignmentSource::kCustomPreferred: return "custom-preferred";
			case AssignmentSource::kCustomFallback: return "custom-fallback";
			case AssignmentSource::kPreferredItem: return "preferred-item";
			case AssignmentSource::kLastEquipped: return "last-equipped";
			case AssignmentSource::kStrongest: return "strongest";
			case AssignmentSource::kRandom: return "random";
			case AssignmentSource::kSlotPriority: return "slot-priority";
			}
			return "unknown";
		};
		AssignmentResolver::AssignmentResult finalAssignments;
		std::unordered_map<ActiveItem*, bool> equippedConsumed;

		auto ResolveEffectiveTargetNode = [&](const SlotDefinition& slotDef, const CustomDefinition* customMatch, ActiveItem* item) {
			std::string targetNodeName = customMatch && !customMatch->targetNode.empty() ? customMatch->targetNode : slotDef.targetNode;
			auto applyStateTargetOverrides = [&](const std::vector<StateOverride>& stateMachine) {
				for (const auto& state : stateMachine) {
					if (EvaluateConditionCached(state.conditionTree, item)) {
						if (state.overrideTargetNode && !state.targetNode.empty()) {
							targetNodeName = state.targetNode;
						}
						if (!state.continueAfterMatch) break;
					}
				}
				};
			applyStateTargetOverrides(slotDef.stateMachine);
			if (customMatch) applyStateTargetOverrides(customMatch->stateMachine);
			return targetNodeName;
			};

		auto HasNodeStateForName = [&](const std::string& rawNodeName) {
			if (nodeStates.count(rawNodeName)) return true;
			std::string stripped = rawNodeName;
			if (stripped.find("IAD_CME_") == 0) stripped = stripped.substr(8);
			if (nodeStates.count(stripped)) return true;
			return nodeStates.count(FormatCMEName(rawNodeName)) != 0;
			};

		auto IsNodeStateHiddenForName = [&](const std::string& rawNodeName) {
			bool hidden = false;
			auto applyHidden = [&](const std::string& key) {
				if (auto it = nodeStates.find(key); it != nodeStates.end()) {
					hidden = hidden || it->second.isHidden;
				}
				};
			applyHidden(rawNodeName);
			std::string stripped = rawNodeName;
			if (stripped.find("IAD_CME_") == 0) stripped = stripped.substr(8);
			applyHidden(stripped);
			applyHidden(FormatCMEName(rawNodeName));
			return hidden;
			};

		auto CheckBlackHole = [&](SlotDefinition& slotDef, ActiveItem* item, CustomDefinition* tempCustomMatch) -> bool {
			bool willBeHidden = false;
			if (!EvaluateConditionCached(slotDef.displayConditionTree, item)) willBeHidden = true;
			if (!willBeHidden) {
				for (const auto& state : slotDef.stateMachine) {
					if (EvaluateConditionCached(state.conditionTree, item)) {
						if (state.hideModel) {
							willBeHidden = true;
							break;
						}
						if (!state.continueAfterMatch) break;
					}
				}
			}
			if (tempCustomMatch && !willBeHidden) {
				if (!EvaluateConditionCached(tempCustomMatch->displayConditionTree, item)) willBeHidden = true;
				if (!willBeHidden) {
					for (const auto& state : tempCustomMatch->stateMachine) {
						if (EvaluateConditionCached(state.conditionTree, item)) {
							if (state.hideModel) {
								willBeHidden = true;
								break;
							}
							if (!state.continueAfterMatch) break;
						}
					}
				}
			}
			if (!willBeHidden) {
				std::string targetNodeName = ResolveEffectiveTargetNode(slotDef, tempCustomMatch, item);
				if (!targetNodeName.empty()) {
					willBeHidden = IsNodeStateHiddenForName(targetNodeName);
				}
			}
			return willBeHidden;
			};

		auto IsPreferredItemForSlot = [](const SlotDefinition& slotDef, const ActiveItem* item) -> bool {
			if (!item || !item->object || slotDef.preferredItems.empty()) {
				return false;
			}
			const auto formID = item->object->GetFormID();
			return std::find(slotDef.preferredItems.begin(), slotDef.preferredItems.end(), formID) != slotDef.preferredItems.end();
			};

		auto IsDisplaySlotAllowedForCustom = [](const CustomDefinition* customMatch, const std::string& slotName) -> bool {
			if (!customMatch || !customMatch->lastEquippedMode || customMatch->lastEquippedDisplaySlots.empty()) {
				return true;
			}
			return std::find(customMatch->lastEquippedDisplaySlots.begin(), customMatch->lastEquippedDisplaySlots.end(), slotName) != customMatch->lastEquippedDisplaySlots.end();
			};

			std::map<std::string, FormFilter> runtimeFormFilters;
			auto ResolveSlotFormFilter = [&](const SlotDefinition& slotDef) -> const FormFilter* {
			if (!slotDef.itemFilter.useProfile) {
				return &slotDef.itemFilter;
			}

			if (slotDef.itemFilter.profileName.empty()) {
				return nullptr;
			}

			const auto cached = runtimeFormFilters.find(slotDef.itemFilter.profileName);
			if (cached != runtimeFormFilters.end()) {
				return &cached->second;
			}

			const auto resolved = Profile::GlobalProfileManager::GetSingleton().ResolveRuntimeFormFilter(slotDef.itemFilter.profileName);
			if (!resolved) {
				return nullptr;
			}
			const auto result = runtimeFormFilters.emplace(slotDef.itemFilter.profileName, *resolved);
			return &result.first->second;
		};

		auto MakeSlotEligibilityContext = [&](SlotDefinition& slotDef, bool equippedOnly, bool ignoreEquipmentEligibility, bool applyPriorityLimit) {
			return AssignmentResolver::SlotEligibilityContext{
				slotDef,
				runtimeSettings.displayFavoritesOnly,
				equippedOnly,
				ignoreEquipmentEligibility,
				applyPriorityLimit,
				ResolveSlotFormFilter,
				[&](ActiveItem* item) { return EvaluateConditionCached(slotDef.itemFilterConditionTree, item); },
				[&](ActiveItem* item) {
					return item && item->object && !ConditionEvaluator::CheckCannotWear(a_actor, item->object);
				},
				[&](ActiveItem* item) { return CheckBlackHole(slotDef, item, nullptr); },
				[&](ActiveItem* item) { return equippedConsumed[item]; }
			};
		};

		auto TryAssignItemToSlot = [&](SlotDefinition& slotDef, ActiveItem* item, CustomDefinition* customMatch, AssignmentSource source) -> bool {
			if (item->count <= 0) return false;
			if (finalAssignments.Contains(slotDef.slotName)) return false;
			if (!slotDef.isEnabled) return false;

			const bool isStaticCustom = customMatch && customMatch->displayFormWithoutInventory && item->stack == nullptr;
			const auto eligibilityContext = MakeSlotEligibilityContext(slotDef, false, isStaticCustom, false);
			if (AssignmentResolver::IsSlotCandidateEligible(eligibilityContext, item)) {
				if (!CheckBlackHole(slotDef, item, customMatch)) {
					bool isEqInst = false;
					if (item->isEquipped && !equippedConsumed[item]) {
						isEqInst = true;
						equippedConsumed[item] = true;
					}
					if (!finalAssignments.TryAdd(slotDef.slotName, { item, customMatch, isEqInst, source })) return false;
					item->count--;
					return true;
				}
			}
			return false;
			};

		auto TryAssignItemToBestSlot = [&](ActiveItem* item, CustomDefinition* customMatch, bool allowConfiguredSlotFallback = false) -> bool {
			if (customMatch && !customMatch->targetDisplaySlot.empty()) {
				auto configuredSlot = std::find_if(sortedSlots.begin(), sortedSlots.end(), [&](SlotDefinition* slotDef) {
					return slotDef && slotDef->slotName == customMatch->targetDisplaySlot;
				});
				return configuredSlot != sortedSlots.end() &&
					TryAssignItemToSlot(**configuredSlot, item, customMatch, AssignmentSource::kCustomConfiguredSlot);
			}

			const bool isLastEquippedCustom = customMatch && customMatch->lastEquippedMode;
			const bool useRecentSlot = runtimeSettings.useRecentDisplaySlotMemory &&
				(!isLastEquippedCustom || customMatch->lastEquippedPrioritizeRecentDisplaySlot);

			if (useRecentSlot) {
				std::string recentSlotName;
				if (GetRecentDisplaySlot(actorID, item->uid, recentSlotName)) {
					auto recentSlotIt = std::find_if(sortedSlots.begin(), sortedSlots.end(), [&](SlotDefinition* slotDef) {
						return slotDef && slotDef->slotName == recentSlotName;
						});
					if (recentSlotIt != sortedSlots.end() && IsDisplaySlotAllowedForCustom(customMatch, recentSlotName)) {
						const bool recentSlotOccupied = finalAssignments.Contains(recentSlotName);
						if (isLastEquippedCustom && customMatch->lastEquippedDisableIfDisplaySlotOccupied && recentSlotOccupied) {
							return false;
						}
						if (!(isLastEquippedCustom && customMatch->lastEquippedSkipOccupiedDisplaySlots && recentSlotOccupied)) {
							if (TryAssignItemToSlot(**recentSlotIt, item, customMatch, AssignmentSource::kCustomRecentSlot)) {
								return true;
							}
						}
						if (isLastEquippedCustom && !customMatch->lastEquippedFallbackToAnySlot) {
							return false;
						}
					}
				}
			}
			if (isLastEquippedCustom && !customMatch->lastEquippedFallbackToAnySlot && !allowConfiguredSlotFallback) {
				return false;
			}

			for (auto* slotDefPtr : sortedSlots) {
				if (slotDefPtr &&
					IsDisplaySlotAllowedForCustom(customMatch, slotDefPtr->slotName) &&
					IsPreferredItemForSlot(*slotDefPtr, item) &&
					TryAssignItemToSlot(*slotDefPtr, item, customMatch, AssignmentSource::kCustomPreferred)) return true;
			}

			for (auto* slotDefPtr : sortedSlots) {
				if (slotDefPtr &&
					IsDisplaySlotAllowedForCustom(customMatch, slotDefPtr->slotName) &&
					TryAssignItemToSlot(*slotDefPtr, item, customMatch, AssignmentSource::kCustomFallback)) return true;
			}
			return false;
			};

		// Keep configured slot priority intact. IED does not remap ordinary slots
		// from a previous display assignment.
		auto baseSortedSlots = sortedSlots;

		for (auto* slotDefPtr : baseSortedSlots) {
			auto& slotDef = *slotDefPtr;
			if (!slotDef.isEnabled) continue;

			if (slotDef.ammoRig.isDedicatedAmmoSlot) {
				ActiveItem* chosenAmmoSource = nullptr;
				for (auto& ai : candidateItems) {
					// 替换 kWEAP 枚举
					if (ai.count > 0 && ai.isEquipped && ai.object->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
						auto weap = ai.object->As<RE::TESObjectWEAP>();
						if (weap && weap->weaponData.ammo) {
							chosenAmmoSource = &ai;
							break;
						}
					}
				}
				if (!chosenAmmoSource) {
					auto& sState = sStates[slotDef.slotName];
					if (sState.lastItem && sState.lastItem->GetFormType() == RE::ENUM_FORM_ID::kWEAP) {
						for (auto& ai : candidateItems) {
							if (ai.object == sState.lastItem) {
								chosenAmmoSource = &ai;
								break;
							}
						}
					}
				}
				if (chosenAmmoSource) {
					finalAssignments.TryAdd(slotDef.slotName, { chosenAmmoSource, nullptr, false, AssignmentSource::kDedicatedAmmo });
				}
			}
		}

		std::vector<CustomDefinition*> sortedCustoms;
		if (!scopedCustoms.empty()) {
			for (auto& cd : scopedCustoms) {
				if (!cd.data.isEnabled) continue;
				if (cd.data.ignorePlayer && a_actor->IsPlayerRef()) continue;
				sortedCustoms.push_back(&cd.data);
			}
			std::stable_sort(sortedCustoms.begin(), sortedCustoms.end(), [](CustomDefinition* a, CustomDefinition* b) { return a->priority > b->priority; });
		}
		std::vector<ActiveItem> staticCustomItems;
		staticCustomItems.reserve(sortedCustoms.size());

		auto GetResolvedCustomTargetFormID = [&](const CustomDefinition& cd) -> RE::TESFormID {
			if (cd.useRuntimeTargetForm) {
				const auto runtimeFormID = config->GetRuntimeFormVariable(cd.runtimeTargetFormVariable);
				if (runtimeFormID != 0) {
					return runtimeFormID;
				}
			}
			return cd.targetFormID;
			};

		auto GetCustomFormIDs = [&](const CustomDefinition& cd) {
			std::vector<RE::TESFormID> forms;
			const auto targetFormID = GetResolvedCustomTargetFormID(cd);
			if (targetFormID != 0) {
				forms.push_back(targetFormID);
			}
			for (auto formID : cd.extraItems) {
				if (formID != 0 && std::find(forms.begin(), forms.end(), formID) == forms.end()) {
					forms.push_back(formID);
				}
			}
			if (cd.groupMode) {
				for (const auto& group : cd.modelGroups) {
					if (group.sourceMode == 1 && group.sourceFormID != 0 &&
						std::find(forms.begin(), forms.end(), group.sourceFormID) == forms.end()) {
						forms.push_back(group.sourceFormID);
					}
				}
			}
			return forms;
			};

		auto CustomMatchesFormID = [&](const CustomDefinition& cd, RE::TESFormID formID) -> bool {
			if (formID == 0) return false;
			auto forms = GetCustomFormIDs(cd);
			return std::find(forms.begin(), forms.end(), formID) != forms.end();
			};

		auto CustomMatchesItem = [&](const CustomDefinition& cd, const ActiveItem& item) -> bool {
			return item.object && CustomMatchesFormID(cd, item.object->GetFormID());
			};

		auto GetCustomAcquiredScore = [&](const CustomDefinition& cd, const ActiveItem& item) -> std::uint64_t {
			if (!item.object) return 0;
			const auto formType = static_cast<std::uint8_t>(item.object->GetFormType());
			if (!cd.lastEquippedRecentAcquiredFormTypes.empty()) {
				if (!HasFormType(cd.lastEquippedRecentAcquiredFormTypes, formType)) return 0;
			}
			else if (!config->IsRecentAcquiredFormTypeEnabled(formType)) {
				return 0;
			}

			auto it = acquiredScores.find(item.object->GetFormID());
			return it != acquiredScores.end() ? it->second : 0;
			};

		auto GetCustomFormOrder = [&](const CustomDefinition& cd, RE::TESFormID formID) -> std::size_t {
			auto forms = GetCustomFormIDs(cd);
			auto it = std::find(forms.begin(), forms.end(), formID);
			if (it != forms.end()) return static_cast<std::size_t>(std::distance(forms.begin(), it));
			return static_cast<std::size_t>(-1);
			};

		auto GetCurrentDisplayedCustomFormID = [&](const CustomDefinition& cd) -> RE::TESFormID {
			for (const auto& [slotName, slotState] : sStates) {
				if (slotState.lastItem && CustomMatchesFormID(cd, slotState.lastItem->GetFormID())) {
					return slotState.lastItem->GetFormID();
				}
			}
			return 0;
			};

		auto GetCustomInventoryCount = [&](const CustomDefinition& cd) -> std::uint32_t {
			std::uint32_t totalCount = 0;
			for (auto formID : GetCustomFormIDs(cd)) {
				totalCount += Scanner::GetItemCount(a_actor, formID);
			}
			return totalCount;
			};

		auto HasBlockedLastEquippedDisplaySlot = [&](const CustomDefinition& cd) -> bool {
			if (!cd.lastEquippedMode || !cd.lastEquippedDisableIfDisplaySlotOccupied || cd.lastEquippedDisplaySlots.empty()) {
				return false;
			}
			for (const auto& slotName : cd.lastEquippedDisplaySlots) {
				if (finalAssignments.Contains(slotName)) {
					return true;
				}
			}
			return false;
			};

		auto HasBlockedLastEquippedBipedSlot = [&](const CustomDefinition& cd) -> bool {
			if (!cd.lastEquippedMode || !cd.lastEquippedDisableIfBipedSlotOccupied || cd.lastEquippedBipedSlots.empty()) {
				return false;
			}
			for (auto slot : cd.lastEquippedBipedSlots) {
				if (BipedSlotOccupied(a_actor, slot)) {
					return true;
				}
			}
			return false;
			};

		auto ItemMatchesLastEquippedBipedSlots = [&](const CustomDefinition& cd, const ActiveItem& ai) -> bool {
			if (cd.lastEquippedBipedSlots.empty()) return true;
			if (!ai.object || GetItemBipedMask(ai.object) == 0) return false;
			for (auto slot : cd.lastEquippedBipedSlots) {
				if (ItemUsesBipedSlot(ai.object, slot)) {
					return true;
				}
			}
			return false;
			};

		auto ItemOverlapsOccupiedLastEquippedBipedSlot = [&](const CustomDefinition& cd, const ActiveItem& ai) -> bool {
			if (cd.lastEquippedBipedSlots.empty() || !ai.object) return false;
			for (auto slot : cd.lastEquippedBipedSlots) {
				if (ItemUsesBipedSlot(ai.object, slot) && BipedSlotOccupied(a_actor, slot)) {
					return true;
				}
			}
			return false;
			};

		auto PassesCustomInventoryRules = [&](const CustomDefinition& cd, const ActiveItem& ai) -> bool {
			if (!ai.object || ai.count <= 0) return false;
			if (cd.disableIfEquipped && ai.isEquipped) return false;

			const auto totalCount = GetCustomInventoryCount(cd);
			if (cd.countMin > 0 && totalCount < static_cast<std::uint32_t>(cd.countMin)) return false;
			if (cd.countMax > 0 && totalCount > static_cast<std::uint32_t>(cd.countMax)) return false;
			if (!ConditionEvaluator::RollSpawnChance(a_actor, ai.object->GetFormID(), cd.spawnChance)) return false;

			bool requireFavOrEquipped = runtimeSettings.displayFavoritesOnly;
			if (cd.overrideEquipmentMode) requireFavOrEquipped = cd.displayFavoritesOnly;
			if (requireFavOrEquipped && !ai.isEquipped && !ai.isFavorited) return false;
			if (!EvaluateConditionCached(cd.inventoryConditionTree, &ai)) return false;

			return true;
			};

		auto GetCustomSelectionHash = [&](const CustomDefinition& cd, const ActiveItem& ai) -> std::uint64_t {
			std::uint64_t seed = static_cast<std::uint64_t>(actorID) << 32;
			seed ^= static_cast<std::uint64_t>(ai.object ? ai.object->GetFormID() : 0);
			seed ^= static_cast<std::uint64_t>(std::hash<std::string>{}(cd.customName));
			return seed ^ (ai.uid * 0x9E3779B97F4A7C15ull);
			};

		auto GetCustomBipedSlotScore = [&](const CustomDefinition& cd, const ActiveItem& item) -> std::uint64_t {
			if (!cd.lastEquippedPrioritizeRecentBipedSlots || cd.lastEquippedBipedSlots.empty() || !item.object) {
				return 0;
			}

			std::uint64_t bestScore = 0;
			for (auto slot : cd.lastEquippedBipedSlots) {
				if (!ItemUsesBipedSlot(item.object, slot)) continue;
				bestScore = std::max(bestScore, GetRecentBipedSlotScore(actorID, slot));
			}
			return bestScore;
			};

		auto SortCustomCandidates = [&](const CustomDefinition& cd, std::vector<ActiveItem*>& candidates, bool prioritizeAcquired, bool prioritizeBipedSlots, bool allowStrongest) {
			const auto currentFormID = GetCurrentDisplayedCustomFormID(cd);
			std::stable_sort(candidates.begin(), candidates.end(), [&](const ActiveItem* lhs, const ActiveItem* rhs) {
				if (!lhs || !rhs || !lhs->object || !rhs->object) return lhs != nullptr;

				// IAD extension: strongest selection is opt-in. It intentionally precedes
				// display-memory stabilization so a stronger eligible item can replace
				// the current one.
				if (allowStrongest && cd.selectInventoryStrongest && lhs->rating != rhs->rating) {
					return lhs->rating > rhs->rating;
				}

				const auto lhsForm = lhs->object->GetFormID();
				const auto rhsForm = rhs->object->GetFormID();
				const bool lhsCurrent = currentFormID != 0 && lhsForm == currentFormID;
				const bool rhsCurrent = currentFormID != 0 && rhsForm == currentFormID;
				if (lhsCurrent != rhsCurrent) return lhsCurrent;

				if (prioritizeBipedSlots) {
					const auto lhsBipedScore = GetCustomBipedSlotScore(cd, *lhs);
					const auto rhsBipedScore = GetCustomBipedSlotScore(cd, *rhs);
					if (lhsBipedScore != rhsBipedScore) return lhsBipedScore > rhsBipedScore;
				}

				if (prioritizeAcquired) {
					const auto lhsAcquired = GetCustomAcquiredScore(cd, *lhs);
					const auto rhsAcquired = GetCustomAcquiredScore(cd, *rhs);
					if (lhsAcquired != rhsAcquired) return lhsAcquired > rhsAcquired;
				}

				if (cd.selectInventoryRandom) {
					const auto lhsHash = GetCustomSelectionHash(cd, *lhs);
					const auto rhsHash = GetCustomSelectionHash(cd, *rhs);
					if (lhsHash != rhsHash) return lhsHash < rhsHash;
				}
				else {
					const auto lhsOrder = GetCustomFormOrder(cd, lhsForm);
					const auto rhsOrder = GetCustomFormOrder(cd, rhsForm);
					if (lhsOrder != rhsOrder) return lhsOrder < rhsOrder;
				}

				if (lhs->isEquipped != rhs->isEquipped) return lhs->isEquipped > rhs->isEquipped;
				if (lhs->isFavorited != rhs->isFavorited) return lhs->isFavorited > rhs->isFavorited;
				return lhs->uid > rhs->uid;
				});
			};

		auto BuildCustomCandidates = [&](const CustomDefinition& cd, bool lastEquippedOnly, bool acquiredFallback) {
			std::vector<ActiveItem*> candidates;
			for (auto& ai : candidateItems) {
				if (!CustomMatchesItem(cd, ai)) continue;
				if (!PassesCustomInventoryRules(cd, ai)) continue;

				if (lastEquippedOnly) {
					// IED's last-equipped selection reads the recent equipment cache
					// without excluding the item while it remains equipped.
					if (!acquiredFallback) {
						if (!ItemMatchesLastEquippedBipedSlots(cd, ai)) continue;
						if (cd.lastEquippedSkipOccupiedBipedSlots && ItemOverlapsOccupiedLastEquippedBipedSlot(cd, ai)) continue;
					}
					if (acquiredFallback) {
						if (GetCustomAcquiredScore(cd, ai) == 0) continue;
					}
					else if (!IsRecentlyEquipped(actorID, ai.uid)) {
						continue;
					}
					if (!EvaluateConditionCached(cd.lastEquippedFilterConditionTree, &ai)) continue;
				}

				candidates.push_back(&ai);
			}

			// FO4 weapon assembly requires a synthetic reference with generated OMOD
			// instance data.  Restrict that path to the player until NPC lifetime and
			// teardown behavior can be made equally safe.
			if (a_actor->IsPlayerRef() && candidates.empty() && !lastEquippedOnly && !acquiredFallback &&
				cd.displayFormWithoutInventory && !cd.targetDisplaySlot.empty()) {
				const auto targetFormID = GetResolvedCustomTargetFormID(cd);
				if (auto* form = RE::TESForm::GetFormByID<RE::TESBoundObject>(targetFormID)) {
					ActiveItem staticItem{};
					staticItem.object = form;
					staticItem.count = 1;
					staticItem.uid = 0xD1AD000000000000ull ^
						(static_cast<std::uint64_t>(targetFormID) << 16) ^
						static_cast<std::uint64_t>(std::hash<std::string>{}(cd.customName));
					if (EvaluateConditionCached(cd.inventoryConditionTree, &staticItem)) {
						staticCustomItems.push_back(staticItem);
						candidates.push_back(&staticCustomItems.back());
					}
				}
			}

			SortCustomCandidates(
				cd,
				candidates,
				acquiredFallback && cd.lastEquippedPrioritizeRecentAcquiredTypes,
				lastEquippedOnly && !acquiredFallback && cd.lastEquippedPrioritizeRecentBipedSlots,
				!lastEquippedOnly && !acquiredFallback);
			return candidates;
			};

		auto BuildSlottedFallbackCandidates = [&](const CustomDefinition& cd) {
			std::vector<std::string> slotNames;
			if (!cd.lastEquippedDisplaySlots.empty()) {
				slotNames = cd.lastEquippedDisplaySlots;
			}
			else {
				for (const auto& [slotName, slotState] : sStates) {
					if (slotState.lastItem) {
						slotNames.push_back(slotName);
					}
				}
			}

			if (cd.lastEquippedPrioritizeRecentDisplaySlot && slotNames.size() > 1) {
				std::stable_sort(slotNames.begin(), slotNames.end(), [&](const std::string& lhs, const std::string& rhs) {
					return GetRecentDisplaySlotScore(actorID, lhs) > GetRecentDisplaySlotScore(actorID, rhs);
					});
			}

			std::vector<ActiveItem*> candidates;
			for (const auto& slotName : slotNames) {
				if (cd.lastEquippedSkipOccupiedDisplaySlots && finalAssignments.Contains(slotName)) {
					continue;
				}
				auto stateIt = sStates.find(slotName);
				if (stateIt == sStates.end() || !stateIt->second.lastItem) {
					continue;
				}

				const auto lastFormID = stateIt->second.lastItem->GetFormID();
				for (auto& ai : candidateItems) {
					if (!ai.object || ai.object->GetFormID() != lastFormID) continue;
					if (ai.isEquipped) continue;
					if (!CustomMatchesItem(cd, ai)) continue;
					if (!PassesCustomInventoryRules(cd, ai)) continue;
					if (!EvaluateConditionCached(cd.lastEquippedFilterConditionTree, &ai)) continue;
					if (std::find(candidates.begin(), candidates.end(), &ai) == candidates.end()) {
						candidates.push_back(&ai);
					}
				}
			}
			return candidates;
			};

		auto AssignCustomCandidates = [&](CustomDefinition& cd, std::vector<ActiveItem*>& candidates, bool allowConfiguredSlotFallback = false) {
			for (auto* ai : candidates) {
				if (!ai) continue;
				while (ai->count > 0) {
					if (!TryAssignItemToBestSlot(ai, &cd, allowConfiguredSlotFallback)) break;
				}
			}
			};

		auto AssignCustomsByMode = [&](bool a_lastEquippedMode) {
			std::vector<CustomDefinition*> modeCustoms;
			for (auto* cdPtr : sortedCustoms) {
				if (cdPtr && cdPtr->lastEquippedMode == a_lastEquippedMode) {
					modeCustoms.push_back(cdPtr);
				}
			}

			if (a_lastEquippedMode) {
				auto GetLastEquippedRuleScore = [&](const CustomDefinition& cd) {
					std::uint64_t score = 0;
					for (const auto& item : candidateItems) {
						if (!CustomMatchesItem(cd, item)) continue;
						score = std::max(score, GetRecentEquipScore(actorID, item.uid));
					}
					return score;
				};

				std::stable_sort(modeCustoms.begin(), modeCustoms.end(), [&](const CustomDefinition* lhs, const CustomDefinition* rhs) {
					if (lhs->priority != rhs->priority) return lhs->priority > rhs->priority;
					const auto lhsScore = GetLastEquippedRuleScore(*lhs);
					const auto rhsScore = GetLastEquippedRuleScore(*rhs);
					if (lhsScore != rhsScore) return lhsScore > rhsScore;
					return lhs->customName < rhs->customName;
				});
			}

			for (auto* cdPtr : modeCustoms) {
				auto& cd = *cdPtr;
				if (HasBlockedLastEquippedDisplaySlot(cd)) continue;
				if (HasBlockedLastEquippedBipedSlot(cd)) continue;

				auto candidates = BuildCustomCandidates(cd, a_lastEquippedMode, false);
				const auto before = finalAssignments.CountForCustom(&cd);
				AssignCustomCandidates(cd, candidates);

				if (a_lastEquippedMode &&
					cd.lastEquippedFallbackToSlotted &&
					finalAssignments.CountForCustom(&cd) == before) {
					auto slottedCandidates = BuildSlottedFallbackCandidates(cd);
					AssignCustomCandidates(cd, slottedCandidates, true);
				}

				if (a_lastEquippedMode &&
					cd.lastEquippedFallbackToRecentAcquired &&
					finalAssignments.CountForCustom(&cd) == before) {
					auto acquiredCandidates = BuildCustomCandidates(cd, true, true);
					AssignCustomCandidates(cd, acquiredCandidates);
				}

				if (a_lastEquippedMode && finalAssignments.CountForCustom(&cd) == before) {
					// Match IED's Last Equipped behavior: when no runtime history exists
					// (for example, immediately after loading a save), fall back to the
					// Custom's normal target selection instead of yielding to a generic slot.
					auto defaultCandidates = BuildCustomCandidates(cd, false, false);
					AssignCustomCandidates(cd, defaultCandidates);
				}
			}
			};

			AssignCustomsByMode(false);

		auto TryAssignBestCandidateToSlot = [&](SlotDefinition& slotDef, bool equippedOnly) -> bool {
			if (finalAssignments.Contains(slotDef.slotName)) return false;
			if (!slotDef.isEnabled) return false;

			auto fallbackCandidates = AssignmentResolver::BuildSlotFallbackCandidates(slotDef, candidateItems);
			auto modeCandidates = AssignmentResolver::BuildSlotModeCandidates(slotDef, fallbackCandidates, actorID);
			auto CommitAssignment = [&](ActiveItem* item, AssignmentSource source) {
				bool isEqInst = false;
				if (!slotDef.extractMagazine && item->isEquipped && !equippedConsumed[item]) {
					isEqInst = true;
					equippedConsumed[item] = true;
				}
				finalAssignments.TryAdd(slotDef.slotName, { item, nullptr, isEqInst, source });
				if (!slotDef.extractMagazine) item->count--;
			};
		const auto eligibilityContext = MakeSlotEligibilityContext(slotDef, equippedOnly, false, true);

		const AssignmentResolver::SlotAssignmentContext resolutionContext{
			slotDef,
			fallbackCandidates,
			modeCandidates,
			eligibilityContext,
			[&](ActiveItem* item) { return GetRecentEquipScore(actorID, item ? item->uid : 0); }
		};
		if (const auto decision = AssignmentResolver::ResolveSlotAssignment(resolutionContext)) {
			CommitAssignment(decision->item, decision->source);
			return true;
		}
		return false;

		};

			// A Last Equipped custom is an explicit display rule.  Resolve it before
			// ordinary slots reserve the currently equipped instance, otherwise a
			// generic slot can consume it and bypass the custom transform.
			AssignCustomsByMode(true);

			if (runtimeSettings.reserveEquippedForPositivePrioritySlots) {
				for (auto* slotDefPtr : baseSortedSlots) {
					if (slotDefPtr && slotDefPtr->priority > 0) TryAssignBestCandidateToSlot(*slotDefPtr, true);
				}
			}

			for (auto* slotDefPtr : baseSortedSlots) {
				if (slotDefPtr) TryAssignBestCandidateToSlot(*slotDefPtr, false);
			}

		for (auto& scoped : scopedSlots) {
			auto& sDef = scoped.data;
			auto& sState = sStates[sDef.slotName];

			if (const auto* assignment = finalAssignments.Find(sDef.slotName)) {
				ActiveItem* winner = assignment->item;
				CustomDefinition* winnerCustom = assignment->custom;
				const auto previousFormID = sState.lastItem ? sState.lastItem->GetFormID() : 0;
				const auto winnerFormID = winner && winner->object ? winner->object->GetFormID() : 0;
				const auto previousEquipped = sState.isEquipped;

				sState.lastItem = winner->object;
				sState.isEquipped = assignment->isEquippedInstance;

				if (isPlayer && (previousFormID != winnerFormID || previousEquipped != sState.isEquipped)) {
					REX::INFO(
						"[IAD Resolve] player slot='{}' item {:08X}->{:08X} equipped {}->{} uid={} source={}",
						sDef.slotName,
						previousFormID,
						winnerFormID,
						previousEquipped,
						sState.isEquipped,
						winner ? winner->uid : 0,
						AssignmentSourceName(assignment->source));
					if (winnerCustom) {
						const char* selectionMode = winnerCustom->lastEquippedMode ? "last-equipped" :
							winnerCustom->selectInventoryStrongest ? "strongest" :
							winnerCustom->selectInventoryRandom ? "random" : "configured-order";
						REX::INFO(
							"[IAD Custom] player slot='{}' rule='{}' selection={} item={:08X} rating={:.1f} ratingSource={}",
							sDef.slotName,
							winnerCustom->customName,
							selectionMode,
							winnerFormID,
							winner ? winner->rating : 0.0f,
							winner && winner->ratingUsesInstanceData ? "instance" : "base");
						if (winnerCustom->useRuntimeTargetForm) {
							const auto runtimeFormID = config->GetRuntimeFormVariable(winnerCustom->runtimeTargetFormVariable);
							REX::INFO(
								"[IAD Custom] runtime-target variable='{}' value={:08X} effective={:08X}",
								winnerCustom->runtimeTargetFormVariable,
								runtimeFormID,
								GetResolvedCustomTargetFormID(*winnerCustom));
						}
					}
				}

				std::string rawFinalCME = ResolveEffectiveTargetNode(sDef, winnerCustom, winner);

				bool isCustomNode = (rawFinalCME.find("Node_") != std::string::npos || rawFinalCME.find("IAD_") != std::string::npos);
				if (isCustomNode && !HasNodeStateForName(rawFinalCME)) {
					ActorDisplayLifecycle::ClearSlotModels(sState);
					sState.lastItem = nullptr;
					sState.currentUID = 0;
					sState.lastModelRequestSignature.clear();
					sState.lastModelGroupSignature.clear();
					sState.modelGroupTransforms.clear();
					sState.modelGroupGeometryTransforms.clear();
					sState.modelGroupOverrideGeometryTransforms.clear();
					sState.modelGroupHideWithWeapon.clear();
					sState.modelGroupConditionVisible.clear();
					sState.modelGroupInvisible.clear();
					sState.modelGroupHideGeometry.clear();
					sState.modelGroupEffects.clear();
					sState.modelGroupLights.clear();
					sState.modelGroupMovNames.clear();
					sState.modelGroupPlaySequence.clear();
					sState.modelGroupForwardAnimationEvents.clear();
					sState.modelGroupDisableBehaviorGraphAnims.clear();
					sState.modelGroupSequenceNames.clear();
					sState.modelGroupAnimationEvents.clear();
					sState.modelGroupAnimationPlayed.clear();
					sState.isSlotHidden = true; sState.isWeaponHidden = true; sState.isHolsterHidden = true;
					continue;
				}

				std::string cleanCME = FormatCMEName(rawFinalCME);

				auto cmeNode = NodeManager::GetManagedNode(a_actor, cleanCME);
				if (!cmeNode) {
					if (isPlayer) REX::INFO("[IAD 追踪] 插槽 {} 映射成功, 注入挂载点 {} ...", sDef.slotName, cleanCME);

					std::vector<std::string> targetBonesToUse;
					std::string baseNodeName = rawFinalCME;
					if (baseNodeName.find("IAD_CME_") == 0) baseNodeName = baseNodeName.substr(8);

					if (nodeStates.count(baseNodeName)) {
						targetBonesToUse = nodeStates[baseNodeName].targetBones;
					}
					else if (nodeStates.count(rawFinalCME)) {
						targetBonesToUse = nodeStates[rawFinalCME].targetBones;
					}
					else {
						targetBonesToUse = { baseNodeName, rawFinalCME, "Weapon" };
					}

					NodeManager::InjectCMENode(a_actor, cleanCME, targetBonesToUse);
					cmeNode = NodeManager::GetManagedNode(a_actor, cleanCME);
				}
				if (!cmeNode) {
					if (isPlayer) REX::WARN("[IAD 追踪] 警告: 玩家骨骼未就绪，CME挂载点注入失败。正在重试...");
					RequestEvaluate(actorID);
					continue;
				}

				NodeManager::GetOrCreateMOVNode(a_actor, cmeNode->node, FormatMOVName(sDef.slotName));

				RecordRecentDisplaySlot(actorID, winner->uid, sDef.slotName);

				std::uint64_t newUID = winner->uid;
				const bool groupOnlyMode = winnerCustom && winnerCustom->groupMode;
				std::string currentHolsterPath = !groupOnlyMode && winnerCustom && !winnerCustom->holsterModelPath.empty() ? winnerCustom->holsterModelPath : sDef.holsterModelPath;
				if (groupOnlyMode) {
					currentHolsterPath.clear();
				}
				std::string effectiveModelSwapPath = sDef.modelSwapPath;
				std::uint32_t effectiveModelSwapFormID = sDef.modelSwapFormID;
				auto applyModelSwapVariableSource = [&](const ModelSwapVariableSource& source) {
					if (!source.enabled) {
						return;
					}

					auto* configManager = ConfigManager::GetSingleton();
					const auto path = configManager->GetRuntimeModelPathVariable(source.pathVariable);
					if (!path.empty()) {
						effectiveModelSwapPath = path;
						effectiveModelSwapFormID = 0;
						return;
					}

					const auto formID = configManager->GetRuntimeFormVariable(source.formIDVariable);
					if (formID != 0) {
						effectiveModelSwapPath.clear();
						effectiveModelSwapFormID = formID;
					}
					};
				applyModelSwapVariableSource(sDef.modelSwapVariableSource);
				if (winnerCustom) {
					if (!winnerCustom->modelSwapPath.empty()) {
						effectiveModelSwapPath = winnerCustom->modelSwapPath;
						effectiveModelSwapFormID = winnerCustom->modelSwapFormID;
					}
					else if (winnerCustom->modelSwapFormID != 0) {
						effectiveModelSwapPath.clear();
						effectiveModelSwapFormID = winnerCustom->modelSwapFormID;
					}
					applyModelSwapVariableSource(winnerCustom->modelSwapVariableSource);
				}
				bool effectiveExtractMagazine = sDef.extractMagazine || (winnerCustom && winnerCustom->extractMagazine);
				bool effectiveUseProjectileForAmmo = winnerCustom ? winnerCustom->useProjectileForAmmo : sDef.useProjectileForAmmo;
				bool effectiveUseWorldModel = sDef.useWorldModel || (winnerCustom && winnerCustom->useWorldModel);
				bool effectiveInvisible = sDef.invisible || (winnerCustom && winnerCustom->invisible);
				bool effectiveHideGeometry = sDef.hideGeometry || (winnerCustom && winnerCustom->hideGeometry);
				bool effectiveHideLight = sDef.hideLight || (winnerCustom && winnerCustom->hideLight);
				bool effectiveLoad1pWeaponModel = sDef.load1pWeaponModel || (winnerCustom && winnerCustom->load1pWeaponModel);
				bool effectiveKeepTorchFlame = sDef.keepTorchFlame || (winnerCustom && winnerCustom->keepTorchFlame);
				bool effectiveRemoveScabbard = sDef.removeScabbard || (winnerCustom && winnerCustom->removeScabbard);
				bool effectiveRemoveEditorMarker = winnerCustom ? winnerCustom->removeEditorMarker : sDef.removeEditorMarker;
				bool effectiveRemoveProjectileTracers = winnerCustom ? winnerCustom->removeProjectileTracers : sDef.removeProjectileTracers;
				ModelAnimationConfig effectiveAnimation = (winnerCustom && HasModelAnimationConfig(winnerCustom->animation)) ? winnerCustom->animation : sDef.animation;
				ModelEffectSettings effectiveEffect = runtimeSettings.enableModelEffects && (winnerCustom && winnerCustom->effectShader.enabled) ? BuildModelEffectSettings(winnerCustom->effectShader) : BuildModelEffectSettings(sDef.effectShader);
				if (!runtimeSettings.enableModelEffects) effectiveEffect.enabled = false;
				ModelLightSettings effectiveLight = (winnerCustom && winnerCustom->light.enabled) ? BuildModelLightSettings(winnerCustom->light) : BuildModelLightSettings(sDef.light);
				auto applyModelStateOverrides = [&](const std::vector<StateOverride>& stateMachine) {
					for (const auto& state : stateMachine) {
						if (EvaluateConditionCached(state.conditionTree, winner)) {
							if (state.overrideModelSwap) {
								effectiveModelSwapPath = state.modelSwapPath;
								effectiveModelSwapFormID = state.modelSwapFormID;
								applyModelSwapVariableSource(state.modelSwapVariableSource);
							}
							if (state.overrideDisplayFlags) {
								effectiveExtractMagazine = state.extractMagazine;
								effectiveUseProjectileForAmmo = state.useProjectileForAmmo;
								effectiveUseWorldModel = state.useWorldModel;
								effectiveInvisible = state.invisible;
								effectiveHideGeometry = state.hideGeometry;
								effectiveHideLight = state.hideLight;
								effectiveLoad1pWeaponModel = state.load1pWeaponModel;
								effectiveKeepTorchFlame = state.keepTorchFlame;
								effectiveRemoveScabbard = state.removeScabbard;
								effectiveRemoveEditorMarker = state.removeEditorMarker;
								effectiveRemoveProjectileTracers = state.removeProjectileTracers;
							}
							if (state.overrideAnimation) {
								effectiveAnimation = state.animation;
							}
							if (state.overrideEffectShader) {
								effectiveEffect = BuildModelEffectSettings(state.effectShader);
							}
							if (state.overrideLight) {
								effectiveLight = BuildModelLightSettings(state.light);
							}
							if (!state.continueAfterMatch) break;
						}
					}
					};
				applyModelStateOverrides(sDef.stateMachine);
				if (winnerCustom) applyModelStateOverrides(winnerCustom->stateMachine);
				if (!runtimeSettings.enableModelEffects) effectiveEffect.enabled = false;
				if (!runtimeSettings.enableModelLights) effectiveLight.enabled = false;
				ModelCleanupPolicy mainCleanupPolicy;
				mainCleanupPolicy.removeEditorMarker = effectiveRemoveEditorMarker;
				mainCleanupPolicy.removeProjectileTracers = effectiveRemoveProjectileTracers;
				mainCleanupPolicy.keepTorchFlame = effectiveKeepTorchFlame;
				mainCleanupPolicy.removeScabbard = effectiveRemoveScabbard;
				if (effectiveHideLight) {
					effectiveLight.enabled = false;
				}
				mainCleanupPolicy.removeLights = !effectiveLight.enabled;
				std::string currentModelRequestSignature;
				if (groupOnlyMode) {
					currentModelRequestSignature = "group-only";
				}
				else if (!effectiveModelSwapPath.empty()) {
					currentModelRequestSignature = "path:" + effectiveModelSwapPath;
				}
				else if (effectiveModelSwapFormID != 0) {
					currentModelRequestSignature = "form:" + std::to_string(effectiveModelSwapFormID) + ":proj:" + (effectiveUseProjectileForAmmo ? "1" : "0");
				}
				else if (winnerCustom && winnerCustom->displayFormWithoutInventory && winner->stack == nullptr) {
					currentModelRequestSignature = "static:" + std::to_string(winner->object->GetFormID());
				}
				else if (effectiveExtractMagazine) {
					currentModelRequestSignature = "mag:" + std::to_string(winner->uid);
				}
				else if (effectiveUseWorldModel && winner->object) {
					currentModelRequestSignature = "world:" + std::to_string(winner->object->GetFormID()) + ":proj:" + (effectiveUseProjectileForAmmo ? "1" : "0");
				}
				else {
					currentModelRequestSignature = "item:" + std::to_string(winner->uid) + ":proj:" + (effectiveUseProjectileForAmmo ? "1" : "0");
				}
				currentModelRequestSignature += ":world:";
				currentModelRequestSignature += effectiveUseWorldModel ? "1" : "0";
				currentModelRequestSignature += ":invisible:";
				currentModelRequestSignature += effectiveInvisible ? "1" : "0";
				currentModelRequestSignature += ":hideGeometry:";
				currentModelRequestSignature += effectiveHideGeometry ? "1" : "0";
				currentModelRequestSignature += ":hideLight:";
				currentModelRequestSignature += effectiveHideLight ? "1" : "0";
				currentModelRequestSignature += ":load1p:";
				currentModelRequestSignature += effectiveLoad1pWeaponModel ? "1" : "0";
				currentModelRequestSignature += ":clean:";
				AppendModelCleanupSignature(currentModelRequestSignature, mainCleanupPolicy);
				currentModelRequestSignature += ":anim:";
				AppendModelAnimationSignature(currentModelRequestSignature, effectiveAnimation);
				currentModelRequestSignature += ":effect:";
				AppendModelEffectSignature(currentModelRequestSignature, effectiveEffect);
				currentModelRequestSignature += ":light:";
				AppendModelLightSignature(currentModelRequestSignature, effectiveLight);
				std::vector<RuntimeModelGroupEntry> activeModelGroups;
				auto ensureCMEForRawNode = [&](const std::string& rawNodeName) -> RE::NiNode* {
					std::string groupCleanCME = FormatCMEName(rawNodeName);
					auto groupCmeNode = NodeManager::GetManagedNode(a_actor, groupCleanCME);
					if (!groupCmeNode) {
						std::vector<std::string> targetBonesToUse;
						std::string baseNodeName = rawNodeName;
						if (baseNodeName.find("IAD_CME_") == 0) baseNodeName = baseNodeName.substr(8);

						if (nodeStates.count(baseNodeName)) {
							targetBonesToUse = nodeStates[baseNodeName].targetBones;
						}
						else if (nodeStates.count(rawNodeName)) {
							targetBonesToUse = nodeStates[rawNodeName].targetBones;
						}
						else {
							targetBonesToUse = { baseNodeName, rawNodeName, "Weapon" };
						}

						NodeManager::InjectCMENode(a_actor, groupCleanCME, targetBonesToUse);
						groupCmeNode = NodeManager::GetManagedNode(a_actor, groupCleanCME);
					}
					return groupCmeNode && groupCmeNode->node ? groupCmeNode->node : nullptr;
					};
				if (winnerCustom) {
					for (const auto& group : winnerCustom->modelGroups) {
						if (!group.isEnabled) continue;
						const bool useFormModel = group.sourceMode == 1;
						if (!useFormModel && group.modelPath.empty()) continue;
						if (useFormModel && group.sourceFormID == 0) continue;
						std::string groupRawNode = group.targetNode.empty() ? rawFinalCME : group.targetNode;
						bool groupCustomNode = (groupRawNode.find("Node_") != std::string::npos || groupRawNode.find("IAD_") != std::string::npos);
						bool hasGroupNodeState = HasNodeStateForName(groupRawNode);
						if (!group.targetNode.empty() && groupCustomNode && !hasGroupNodeState) continue;

						bool groupNodeHidden = !group.targetNode.empty() && IsNodeStateHiddenForName(groupRawNode);
						bool groupConditionVisible = !groupNodeHidden && EvaluateConditionCached(group.displayConditionTree, winner);
						if (group.loadOnlyWhenVisible && !groupConditionVisible) continue;

					auto* groupCME = group.targetNode.empty() ? cmeNode->node : ensureCMEForRawNode(groupRawNode);
						if (!groupCME) {
							RequestEvaluate(actorID);
							continue;
						}

						std::string groupMovName = group.targetNode.empty() ? FormatMOVName(sDef.slotName) : FormatMOVName(sDef.slotName + "_MG_" + std::to_string(activeModelGroups.size()));
						NodeManager::GetOrCreateMOVNode(a_actor, groupCME, groupMovName);

						RuntimeModelGroupEntry runtimeGroup;
						runtimeGroup.name = group.name;
						runtimeGroup.sourceMode = group.sourceMode;
						runtimeGroup.modelPath = group.modelPath;
						runtimeGroup.sourceFormID = group.sourceFormID;
						runtimeGroup.extractMagazine = group.extractMagazine;
						runtimeGroup.useProjectileForAmmo = group.useProjectileForAmmo;
						runtimeGroup.removeEditorMarker = group.removeEditorMarker;
						runtimeGroup.removeProjectileTracers = group.removeProjectileTracers;
						runtimeGroup.invisible = group.invisible;
						runtimeGroup.hideGeometry = group.hideGeometry;
						runtimeGroup.hideLight = group.hideLight;
						runtimeGroup.load1pWeaponModel = group.load1pWeaponModel;
						runtimeGroup.keepTorchFlame = group.keepTorchFlame;
						runtimeGroup.removeScabbard = group.removeScabbard;
						runtimeGroup.targetNode = group.targetNode;
						runtimeGroup.movName = groupMovName;
						runtimeGroup.transform = isFemale ? group.transforms.f : group.transforms.m;
						runtimeGroup.overrideGeometryTransform = group.overrideGeometryTransform;
						runtimeGroup.geometryTransform = isFemale ? group.geometryTransforms.f : group.geometryTransforms.m;
						runtimeGroup.hideWithWeapon = group.hideWithWeapon;
						runtimeGroup.conditionVisible = groupConditionVisible;
						runtimeGroup.effect = BuildModelEffectSettings(group.effectShader);
						if (!runtimeSettings.enableModelEffects) {
							runtimeGroup.effect.enabled = false;
						}
						runtimeGroup.light = BuildModelLightSettings(group.light);
						if (!runtimeSettings.enableModelLights || runtimeGroup.hideLight) {
							runtimeGroup.light.enabled = false;
						}
						runtimeGroup.playSequence = group.animation.playSequence;
						runtimeGroup.forwardAnimationEvents = group.animation.forwardAnimationEvents;
						runtimeGroup.disableBehaviorGraphAnims = group.animation.disableBehaviorGraphAnims;
						runtimeGroup.sequenceName = group.animation.sequenceName;
						runtimeGroup.animationEvent = group.animation.animationEvent;
						activeModelGroups.push_back(std::move(runtimeGroup));
						if (groupConditionVisible && !group.continueAfterMatch) break;
					}
				}
				std::string currentModelGroupSignature = BuildModelGroupSignature(activeModelGroups);

				if (cleanCME != sState.lastTargetNode) {
					ActorDisplayLifecycle::ClearSlotModels(sState);
					sState.lastTargetNode = cleanCME;
					sState.lastModelRequestSignature.clear();
					sState.lastModelGroupSignature.clear();
					sState.modelGroupTransforms.clear();
					sState.modelGroupGeometryTransforms.clear();
					sState.modelGroupOverrideGeometryTransforms.clear();
					sState.modelGroupHideWithWeapon.clear();
					sState.modelGroupConditionVisible.clear();
					sState.modelGroupInvisible.clear();
					sState.modelGroupHideGeometry.clear();
					sState.modelGroupEffects.clear();
					sState.modelGroupLights.clear();
					sState.modelGroupMovNames.clear();
					sState.modelGroupPlaySequence.clear();
					sState.modelGroupForwardAnimationEvents.clear();
					sState.modelGroupDisableBehaviorGraphAnims.clear();
					sState.modelGroupSequenceNames.clear();
					sState.modelGroupAnimationEvents.clear();
					sState.modelGroupAnimationPlayed.clear();
					sState.physicsSim.reset();
				}

				const bool uidChanged = sState.currentUID != newUID;
				const bool modelRequestChanged = sState.lastModelRequestSignature != currentModelRequestSignature;
				const bool holsterPathChanged = sState.lastHolsterPath != currentHolsterPath;
				const bool modelGroupChanged = sState.lastModelGroupSignature != currentModelGroupSignature;
				if (uidChanged || modelRequestChanged) {
					++sState.modelRequestGeneration;
					// Do not detach a just-rendered node in the same weapon-switch window.
					// Motion Vector Fixes (and similar scene visitors) can still be walking
					// the old subtree on a render job.  Cull it now so it cannot ghost, then
					// retire it from the main update after the traversal grace period.
					ActorDisplayLifecycle::BeginModelReplacement(sState, _currentUpdateTick);
					sState.currentUID = newUID;
					sState.lastModelRequestSignature = currentModelRequestSignature;
				}

				if (holsterPathChanged) {
					++sState.holsterRequestGeneration;
					ActorDisplayLifecycle::BeginHolsterReplacement(sState, _currentUpdateTick);
					sState.lastHolsterPath = currentHolsterPath;
				}

				if (uidChanged || modelGroupChanged) {
					++sState.modelGroupRequestGeneration;
					ActorDisplayLifecycle::BeginModelGroupReplacement(sState, _currentUpdateTick);
					sState.lastModelGroupSignature = currentModelGroupSignature;
				}
				sState.modelGroupTransforms.clear();
				sState.modelGroupGeometryTransforms.clear();
				sState.modelGroupOverrideGeometryTransforms.clear();
				sState.modelGroupHideWithWeapon.clear();
				sState.modelGroupConditionVisible.clear();
				sState.modelGroupInvisible.clear();
				sState.modelGroupHideGeometry.clear();
				sState.modelGroupEffects.clear();
				sState.modelGroupLights.clear();
				sState.modelGroupMovNames.clear();
				sState.modelGroupPlaySequence.clear();
				sState.modelGroupForwardAnimationEvents.clear();
				sState.modelGroupDisableBehaviorGraphAnims.clear();
				sState.modelGroupSequenceNames.clear();
				sState.modelGroupAnimationEvents.clear();
				sState.modelGroupAnimationPlayed.clear();
				for (const auto& group : activeModelGroups) {
					sState.modelGroupTransforms.push_back(group.transform);
					sState.modelGroupGeometryTransforms.push_back(group.geometryTransform);
					sState.modelGroupOverrideGeometryTransforms.push_back(group.overrideGeometryTransform);
					sState.modelGroupHideWithWeapon.push_back(group.hideWithWeapon);
					sState.modelGroupConditionVisible.push_back(group.conditionVisible);
					sState.modelGroupInvisible.push_back(group.invisible);
					sState.modelGroupHideGeometry.push_back(group.hideGeometry);
					sState.modelGroupEffects.push_back(group.effect);
					sState.modelGroupLights.push_back(group.light);
					sState.modelGroupMovNames.push_back(group.movName);
					sState.modelGroupPlaySequence.push_back(group.playSequence);
					sState.modelGroupForwardAnimationEvents.push_back(group.forwardAnimationEvents);
					sState.modelGroupDisableBehaviorGraphAnims.push_back(group.disableBehaviorGraphAnims);
					sState.modelGroupSequenceNames.push_back(group.sequenceName);
					sState.modelGroupAnimationEvents.push_back(group.animationEvent);
					sState.modelGroupAnimationPlayed.push_back(false);
				}
				if (uidChanged) {
					sState.modelGroupAnimationPlayed.assign(sState.modelGroupTransforms.size(), false);
				}
				if (uidChanged) {
					TryReplayModelGroupAnimationsIfNeeded(a_actor, sState);
				}

				int numToSpawn = groupOnlyMode ? 0 : 1;
				if (!groupOnlyMode && sDef.ammoRig.isDedicatedAmmoSlot && sDef.ammoRig.displayMode == AmmoDisplayMode::kDynamicArray) {
					if (auto weap = winner->object->As<RE::TESObjectWEAP>()) {
						int ammo = static_cast<int>(Scanner::GetItemCount(a_actor, weap->weaponData.ammo ? weap->weaponData.ammo->GetFormID() : 0));
						int capacity = static_cast<int>(weap->weaponData.ammoCapacity);
						if (capacity <= 0) capacity = 1;
						numToSpawn = std::min<int>(ammo / capacity, sDef.ammoRig.maxMags);
					}
				}
				sState.numModelsToSpawn = numToSpawn;
				sState.arrayDir = sDef.ammoRig.arrayDirection; sState.arraySpacing = sDef.ammoRig.magSpacing;

				for (size_t i = sState.currentModels.size(); i < (size_t)numToSpawn; ++i) {
					sState.currentModels.push_back(nullptr);
					auto callback = [actorID, exactSlotKey = sDef.slotName, cleanMovName = FormatMOVName(sDef.slotName), index = i, reqUID = newUID, reqGeneration = sState.modelRequestGeneration, reqModelSignature = currentModelRequestSignature, reqAnimation = effectiveAnimation, reqEffect = effectiveEffect, reqLight = effectiveLight, reqInvisible = effectiveInvisible, reqHideGeometry = effectiveHideGeometry, sceneGeneration = ModelManager::GetSceneGeneration()](RE::NiAVObject* loaded) {
						if (!loaded) {
							REX::WARN("[IAD 追踪] 模型回调收到 nullptr，停止无限重试。插槽: {}", exactSlotKey);
							return;
						}
						RE::NiPointer<RE::NiAVObject> safeLoaded(loaded);

						auto task = F4SE::GetTaskInterface();
						if (task) {
							task->AddTask([actorID, exactSlotKey, cleanMovName, index, safeLoaded, reqUID, reqGeneration, reqModelSignature, reqAnimation, reqEffect, reqLight, reqInvisible, reqHideGeometry, sceneGeneration]() {
							if (ModelManager::GetSceneGeneration() != sceneGeneration || ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition()) return;
							auto hm = HolsterManager::GetSingleton();
							std::lock_guard<std::mutex> evalLock(hm->_evalMutex);
							auto it = hm->_actorDisplaySlots.find(actorID);
								if (it != hm->_actorDisplaySlots.end() && it->second.count(exactSlotKey)) {
									auto& state = it->second[exactSlotKey];

									if (state.modelRequestGeneration != reqGeneration) return;
									if (state.currentUID != reqUID) return;
									if (state.lastModelRequestSignature != reqModelSignature) return;

									if (index < state.currentModels.size()) {
										auto act = RE::TESForm::GetFormByID<RE::Actor>(actorID);
										safeLoaded->name = "IAD_Mesh_Wrapper";
										RE::NiMatrix3 mRot = TransformMath::EulerToMatrix(state.meshTransform.rot);
										RE::NiPoint3 p = state.meshTransform.pivot;
										RE::NiPoint3 pRot = mRot * p;
										RE::NiPoint3 arrayOffset = state.arrayDir * (state.arraySpacing * static_cast<float>(index));

										safeLoaded->local.translate.x = state.meshTransform.pos.x + (p.x - pRot.x) + arrayOffset.x;
										safeLoaded->local.translate.y = state.meshTransform.pos.y + (p.y - pRot.y) + arrayOffset.y;
										safeLoaded->local.translate.z = state.meshTransform.pos.z + (p.z - pRot.z) + arrayOffset.z;
										safeLoaded->local.rotate = mRot;
										safeLoaded->local.scale = state.meshTransform.scale;
						ApplyGeometryTransform(safeLoaded.get(), state.overrideGeometryTransform, state.geometryTransform);

										// Visibility belongs to UpdateActorTransforms. Keeping a freshly
										// loaded clone culled prevents a callback that lands between
										// weapon-state samples from producing a one-frame flash.
										safeLoaded->SetAppCulled(true);

										state.currentModels[index] = safeLoaded;

										if (act) {
											auto mov = NodeManager::GetManagedNode(act, cleanMovName);
										if (mov && mov->node) NodeManager::Safe_AttachNode(mov->node, safeLoaded.get());
											if (reqEffect.enabled) {
												ModelManager::GetSingleton()->ApplyModelEffect(safeLoaded.get(), reqEffect);
											}
											if (reqLight.enabled) {
												ModelManager::GetSingleton()->ApplyModelLight(safeLoaded.get(), reqLight);
											}
											if (reqInvisible) {
												ModelManager::GetSingleton()->SetModelAlpha(safeLoaded.get(), 0.0f);
											}
											if (reqHideGeometry) {
												ModelManager::GetSingleton()->SetModelGeometryHidden(safeLoaded.get(), true);
											}
											TryPlayModelAnimationOnce(act, safeLoaded.get(), reqAnimation);
										}
									}
								}
								});
						}
						};
					if (!effectiveModelSwapPath.empty()) modelManager->RequestModelByPath(actorID, effectiveModelSwapPath, mainCleanupPolicy, callback);
					else if (effectiveModelSwapFormID != 0) {
						auto form = RE::TESForm::GetFormByID<RE::TESBoundObject>(effectiveModelSwapFormID);
						if (form) {
							modelManager->RequestFormModel(a_actor, form, false, effectiveUseProjectileForAmmo, mainCleanupPolicy, effectiveLoad1pWeaponModel, callback);
						}
						else {
							REX::WARN("[IAD 追踪] Custom 模型替换 FormID 无法加载: {:08X}", effectiveModelSwapFormID);
							callback(nullptr);
						}
					}
					else if (winnerCustom && winnerCustom->displayFormWithoutInventory && winner->stack == nullptr) modelManager->RequestStaticFormModel(a_actor, winner->object, mainCleanupPolicy, callback);
					else if (effectiveExtractMagazine && effectiveUseWorldModel) modelManager->RequestFormModel(a_actor, winner->object, true, effectiveUseProjectileForAmmo, mainCleanupPolicy, effectiveLoad1pWeaponModel, callback);
					else if (effectiveExtractMagazine) modelManager->RequestMagazineModel(a_actor, *winner, mainCleanupPolicy, effectiveLoad1pWeaponModel, callback);
					else if (effectiveUseWorldModel && winner->object) modelManager->RequestFormModel(a_actor, winner->object, false, effectiveUseProjectileForAmmo, mainCleanupPolicy, effectiveLoad1pWeaponModel, callback);
					else modelManager->RequestItemModel(a_actor, *winner, effectiveUseProjectileForAmmo, mainCleanupPolicy, effectiveLoad1pWeaponModel, callback);
				}

				for (size_t i = sState.currentHolsters.size(); i < (size_t)numToSpawn; ++i) {
					sState.currentHolsters.push_back(nullptr);
					if (!currentHolsterPath.empty()) {
						auto hCallback = [actorID, exactSlotKey = sDef.slotName, cleanMovName = FormatMOVName(sDef.slotName), index = i, reqPath = currentHolsterPath, reqGeneration = sState.holsterRequestGeneration, sceneGeneration = ModelManager::GetSceneGeneration()](RE::NiAVObject* loaded) {
							if (!loaded) {
								REX::WARN("[IAD 追踪] 枪套回调收到 nullptr。插槽: {}", exactSlotKey);
								return;
							}
							RE::NiPointer<RE::NiAVObject> safeLoaded(loaded);

							auto task = F4SE::GetTaskInterface();
							if (task) {
								task->AddTask([actorID, exactSlotKey, cleanMovName, index, safeLoaded, reqPath, reqGeneration, sceneGeneration]() {
								if (ModelManager::GetSceneGeneration() != sceneGeneration || ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition()) return;
								auto hm = HolsterManager::GetSingleton();
								std::lock_guard<std::mutex> evalLock(hm->_evalMutex);
								auto it = hm->_actorDisplaySlots.find(actorID);
								if (it != hm->_actorDisplaySlots.end() && it->second.count(exactSlotKey)) {
									auto& state = it->second[exactSlotKey];

									if (state.holsterRequestGeneration != reqGeneration) return;
									if (state.lastHolsterPath != reqPath) return;

									if (index < state.currentHolsters.size()) {
										auto act = RE::TESForm::GetFormByID<RE::Actor>(actorID);
										safeLoaded->name = "IAD_Holster_Wrapper";
										RE::NiMatrix3 mRot = TransformMath::EulerToMatrix(state.holsterMeshTransform.rot);
										RE::NiPoint3 p = state.holsterMeshTransform.pivot;
										RE::NiPoint3 pRot = mRot * p;
										RE::NiPoint3 arrayOffset = state.arrayDir * (state.arraySpacing * static_cast<float>(index));

										safeLoaded->local.translate.x = state.holsterMeshTransform.pos.x + (p.x - pRot.x) + arrayOffset.x;
										safeLoaded->local.translate.y = state.holsterMeshTransform.pos.y + (p.y - pRot.y) + arrayOffset.y;
										safeLoaded->local.translate.z = state.holsterMeshTransform.pos.z + (p.z - pRot.z) + arrayOffset.z;
										safeLoaded->local.rotate = mRot;
										safeLoaded->local.scale = state.holsterMeshTransform.scale;

										safeLoaded->SetAppCulled(true);

										state.currentHolsters[index] = safeLoaded;

										if (act) {
											auto mov = NodeManager::GetManagedNode(act, cleanMovName);
										if (mov && mov->node) NodeManager::Safe_AttachNode(mov->node, safeLoaded.get());
										}
									}
								}
								});
							}
							};
						modelManager->RequestModelByPath(actorID, currentHolsterPath, ModelCleanupPolicy{}, hCallback);
					}
				}

				for (size_t i = sState.currentModelGroups.size(); i < activeModelGroups.size(); ++i) {
					sState.currentModelGroups.push_back(nullptr);
					const auto groupPath = activeModelGroups[i].modelPath;
					const auto groupSourceMode = activeModelGroups[i].sourceMode;
					const auto groupSourceFormID = activeModelGroups[i].sourceFormID;
					const auto groupExtractMagazine = activeModelGroups[i].extractMagazine;
					const auto groupUseProjectileForAmmo = activeModelGroups[i].useProjectileForAmmo;
					const auto groupLoad1pWeaponModel = activeModelGroups[i].load1pWeaponModel;
					ModelCleanupPolicy groupCleanupPolicy;
					groupCleanupPolicy.removeEditorMarker = activeModelGroups[i].removeEditorMarker;
					groupCleanupPolicy.removeProjectileTracers = activeModelGroups[i].removeProjectileTracers;
					groupCleanupPolicy.removeScabbard = activeModelGroups[i].removeScabbard;
					groupCleanupPolicy.removeLights = !activeModelGroups[i].light.enabled;
					groupCleanupPolicy.keepTorchFlame = activeModelGroups[i].keepTorchFlame;
					const auto groupMovName = activeModelGroups[i].movName;
					auto gCallback = [actorID, exactSlotKey = sDef.slotName, groupMovName, index = i, reqSignature = currentModelGroupSignature, reqUID = newUID, reqGeneration = sState.modelGroupRequestGeneration, sceneGeneration = ModelManager::GetSceneGeneration()](RE::NiAVObject* loaded) {
						if (!loaded) {
							REX::WARN("[IAD 追踪] 模型组回调收到 nullptr。插槽: {}", exactSlotKey);
							return;
						}
						RE::NiPointer<RE::NiAVObject> safeLoaded(loaded);

						auto task = F4SE::GetTaskInterface();
						if (task) {
							task->AddTask([actorID, exactSlotKey, groupMovName, index, safeLoaded, reqSignature, reqUID, reqGeneration, sceneGeneration]() {
							if (ModelManager::GetSceneGeneration() != sceneGeneration || ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition()) return;
							auto hm = HolsterManager::GetSingleton();
								std::lock_guard<std::mutex> evalLock(hm->_evalMutex);
								auto it = hm->_actorDisplaySlots.find(actorID);
								if (it != hm->_actorDisplaySlots.end() && it->second.count(exactSlotKey)) {
									auto& state = it->second[exactSlotKey];

									if (state.modelGroupRequestGeneration != reqGeneration) return;
									if (state.lastModelGroupSignature != reqSignature || state.currentUID != reqUID) return;

									if (index < state.currentModelGroups.size() && index < state.modelGroupTransforms.size()) {
										safeLoaded->name = "IAD_ModelGroup_Wrapper";
										auto& groupTransform = state.modelGroupTransforms[index];
										RE::NiMatrix3 mRot = TransformMath::EulerToMatrix(groupTransform.rot);
										RE::NiPoint3 p = groupTransform.pivot;
										RE::NiPoint3 pRot = mRot * p;

										safeLoaded->local.translate.x = groupTransform.pos.x + (p.x - pRot.x);
										safeLoaded->local.translate.y = groupTransform.pos.y + (p.y - pRot.y);
										safeLoaded->local.translate.z = groupTransform.pos.z + (p.z - pRot.z);
										safeLoaded->local.rotate = mRot;
										safeLoaded->local.scale = groupTransform.scale;
										if (index < state.modelGroupGeometryTransforms.size()) {
											const bool overrideGeometry = index < state.modelGroupOverrideGeometryTransforms.size() ? state.modelGroupOverrideGeometryTransforms[index] : false;
											ApplyGeometryTransform(safeLoaded.get(), overrideGeometry, state.modelGroupGeometryTransforms[index]);
										}

										auto act = RE::TESForm::GetFormByID<RE::Actor>(actorID);
										if (index < state.modelGroupEffects.size()) {
											ModelManager::GetSingleton()->ApplyModelEffect(safeLoaded.get(), state.modelGroupEffects[index]);
										}
										if (index < state.modelGroupLights.size()) {
											ModelManager::GetSingleton()->ApplyModelLight(safeLoaded.get(), state.modelGroupLights[index]);
										}
										if (index < state.modelGroupInvisible.size() && state.modelGroupInvisible[index]) {
											ModelManager::GetSingleton()->SetModelAlpha(safeLoaded.get(), 0.0f);
										}
										if (index < state.modelGroupHideGeometry.size() && state.modelGroupHideGeometry[index]) {
											ModelManager::GetSingleton()->SetModelGeometryHidden(safeLoaded.get(), true);
										}
										safeLoaded->SetAppCulled(true);

										state.currentModelGroups[index] = safeLoaded;

										if (act) {
											auto mov = NodeManager::GetManagedNode(act, groupMovName);
										if (mov && mov->node) NodeManager::Safe_AttachNode(mov->node, safeLoaded.get());
										}

										TryPlayModelGroupAnimation(act, safeLoaded.get(), index, state);
									}
								}
							});
						}
						};
					if (groupSourceMode == 1) {
						auto form = RE::TESForm::GetFormByID<RE::TESBoundObject>(groupSourceFormID);
						if (form) {
							modelManager->RequestFormModel(a_actor, form, groupExtractMagazine, groupUseProjectileForAmmo, groupCleanupPolicy, groupLoad1pWeaponModel, gCallback);
						}
						else {
							REX::WARN("[IAD 追踪] 模型组 FormID 无法加载: {:08X}", groupSourceFormID);
						}
					}
					else {
						modelManager->RequestModelByPath(actorID, groupPath, groupCleanupPolicy, gCallback);
					}
				}

				sState.slotBaseTransform.pos = { 0,0,0 }; sState.slotBaseTransform.rot = { 0,0,0 }; sState.slotBaseTransform.scale = 1.0f;
				sState.meshTransform.pos = { 0,0,0 }; sState.meshTransform.rot = { 0,0,0 }; sState.meshTransform.scale = 1.0f; sState.meshTransform.pivot = { 0,0,0 };
				sState.geometryTransform.pos = { 0,0,0 }; sState.geometryTransform.rot = { 0,0,0 }; sState.geometryTransform.scale = 1.0f; sState.geometryTransform.pivot = { 0,0,0 };
				sState.overrideGeometryTransform = false;
				sState.holsterMeshTransform.pos = { 0,0,0 }; sState.holsterMeshTransform.rot = { 0,0,0 }; sState.holsterMeshTransform.scale = 1.0f; sState.holsterMeshTransform.pivot = { 0,0,0 };

				if (sDef.overrideTransform) sState.slotBaseTransform = isFemale ? sDef.transforms.f : sDef.transforms.m;
				if (sDef.overrideMeshTransform) sState.meshTransform = isFemale ? sDef.meshTransforms.f : sDef.meshTransforms.m;
				if (sDef.overrideGeometryTransform) {
					sState.overrideGeometryTransform = true;
					sState.geometryTransform = isFemale ? sDef.geometryTransforms.f : sDef.geometryTransforms.m;
				}

				sState.holsterMeshTransform = isFemale ? sDef.holsterTransforms.f : sDef.holsterTransforms.m;
				sState.keepHolsterWhenDrawn = sDef.keepHolsterWhenDrawn;

				if (winnerCustom && winnerCustom->overrideTransform) sState.slotBaseTransform = isFemale ? winnerCustom->transforms.f : winnerCustom->transforms.m;
				if (winnerCustom && winnerCustom->overrideMeshTransform) sState.meshTransform = isFemale ? winnerCustom->meshTransforms.f : winnerCustom->meshTransforms.m;
				if (winnerCustom && winnerCustom->overrideGeometryTransform) {
					sState.overrideGeometryTransform = true;
					sState.geometryTransform = isFemale ? winnerCustom->geometryTransforms.f : winnerCustom->geometryTransforms.m;
				}

				if (!groupOnlyMode && winnerCustom && !winnerCustom->holsterModelPath.empty()) {
					sState.holsterMeshTransform = isFemale ? winnerCustom->holsterTransforms.f : winnerCustom->holsterTransforms.m;
					sState.keepHolsterWhenDrawn = winnerCustom->keepHolsterWhenDrawn;
				}

				sState.hasActivePhys = sDef.overridePhysics || (winnerCustom && winnerCustom->overridePhysics);
				if (winnerCustom && winnerCustom->overridePhysics) sState.activePhys = winnerCustom->physics;
				else if (sDef.overridePhysics) sState.activePhys = sDef.physics;

				bool condHidden = !EvaluateConditionCached(sDef.displayConditionTree, winner);
				const bool hideIfUsingFurniture = sDef.hideIfUsingFurniture || (winnerCustom && winnerCustom->hideIfUsingFurniture);
				const bool hideIfLayingDown = sDef.hideLayingDown || (winnerCustom && winnerCustom->hideLayingDown);
				if (hideIfUsingFurniture && IsActorUsingFurnitureForDisplayHide(a_actor)) {
					condHidden = true;
				}
				if (hideIfLayingDown && ConditionEvaluator::IsLayingDown(a_actor)) {
					condHidden = true;
				}
				for (const auto& state : sDef.stateMachine) {
					if (EvaluateConditionCached(state.conditionTree, winner)) {
						if (state.hideModel) condHidden = true;
						if (state.overrideTransform) sState.slotBaseTransform = ResolveStateTransform(state);
						if (state.overrideMeshTransform) sState.meshTransform = state.independentMeshTransform;
						if (state.overrideGeometryTransform) { sState.overrideGeometryTransform = true; sState.geometryTransform = state.independentGeometryTransform; }
						if (state.overridePhysics) { sState.hasActivePhys = true; sState.activePhys = ResolveStatePhysics(state); }
						if (!state.continueAfterMatch) break;
					}
				}

				if (winnerCustom) {
					condHidden |= !EvaluateConditionCached(winnerCustom->displayConditionTree, winner);
					for (const auto& state : winnerCustom->stateMachine) {
						if (EvaluateConditionCached(state.conditionTree, winner)) {
							if (state.hideModel) condHidden = true;
							if (state.overrideTransform) sState.slotBaseTransform = ResolveStateTransform(state);
							if (state.overrideMeshTransform) sState.meshTransform = state.independentMeshTransform;
							if (state.overrideGeometryTransform) { sState.overrideGeometryTransform = true; sState.geometryTransform = state.independentGeometryTransform; }
							if (state.overridePhysics) { sState.hasActivePhys = true; sState.activePhys = ResolveStatePhysics(state); }
							if (!state.continueAfterMatch) break;
						}
					}
				}

				std::string rawNodeName = ResolveEffectiveTargetNode(sDef, winnerCustom, winner);
				bool nodeHidden = IsNodeStateHiddenForName(rawNodeName);

				bool checkUnload = winnerCustom ? winnerCustom->alwaysUnload : sDef.alwaysUnload;
				sState.hideWeaponWhenDrawn = checkUnload;

				// 👇========== 🌟 修复 2：完美同步收枪动画的显示时机 ==========👇
				// 直接读取底层 weaponState: 只要没彻底收回裤裆 (kSheathed=0)，就依然强制隐藏背部模型！
				bool isWeaponStateDrawn = (a_actor->weaponState != RE::WEAPON_STATE::kSheathed);
				
				bool isBaseHidden = ConditionEvaluator::IsFirstPerson(a_actor) || condHidden || nodeHidden;
				bool isWeaponDrawnHide = checkUnload && sState.isEquipped && isWeaponStateDrawn;
				// 👆==============================================================👆
				sState.isSlotHidden = isBaseHidden;
				sState.isWeaponHidden = isBaseHidden || isWeaponDrawnHide;
				sState.isHolsterHidden = isBaseHidden || (isWeaponDrawnHide && !sState.keepHolsterWhenDrawn);
			}
			else {
				ActorDisplayLifecycle::ClearSlotModels(sState);
				sState.lastItem = nullptr;
				sState.currentUID = 0;
				sState.hideWeaponWhenDrawn = false;
				sState.lastModelRequestSignature.clear();
				sState.lastModelGroupSignature.clear();
				sState.overrideGeometryTransform = false;
				sState.geometryTransform.pos = { 0,0,0 }; sState.geometryTransform.rot = { 0,0,0 }; sState.geometryTransform.scale = 1.0f; sState.geometryTransform.pivot = { 0,0,0 };
				sState.modelGroupTransforms.clear();
				sState.modelGroupGeometryTransforms.clear();
				sState.modelGroupOverrideGeometryTransforms.clear();
				sState.modelGroupHideWithWeapon.clear();
				sState.modelGroupConditionVisible.clear();
				sState.modelGroupInvisible.clear();
				sState.modelGroupHideGeometry.clear();
				sState.modelGroupEffects.clear();
				sState.modelGroupLights.clear();
				sState.modelGroupMovNames.clear();
				sState.isSlotHidden = true;
				sState.isWeaponHidden = true;
				sState.isHolsterHidden = true;
			}
		}

		{
			std::lock_guard<std::mutex> lock(NodeManager::_cacheMutex);
			auto itCache = NodeManager::_nodeCache.find(actorID);
			if (itCache != NodeManager::_nodeCache.end()) {
				auto& activeNodes = itCache->second.activeNodes;
				for (auto itCacheNode = activeNodes.begin(); itCacheNode != activeNodes.end(); ) {
					const std::string& cachedName = itCacheNode->first;
					bool existsInConfig = false;

					if (cachedName.find("IAD_CME_") == 0) {
						for (auto& nodeDef : scopedNodes) {
							if (FormatCMEName(nodeDef.data.nodeName) == cachedName) {
								existsInConfig = true;
								break;
							}
						}
						if (!existsInConfig) {
							itCacheNode = activeNodes.erase(itCacheNode);
							continue;
						}
					}
					++itCacheNode;
				}
			}
		}
	}

	void HolsterManager::CollectModelBoundSpheres(RE::Actor* a_actor, std::vector<DebugBoundSphere>& outSpheres)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> evalLock(_evalMutex);
		auto slotsIt = _actorDisplaySlots.find(a_actor->GetFormID());
		if (slotsIt == _actorDisplaySlots.end()) return;

		// This is intentionally a read-only scene snapshot. Calling
		// UpdateWorldBound() here can mutate the scene graph while the UI is
		// consuming the previous snapshot and was also the source of detached
		// debug bounds on some model wrappers.
		constexpr std::size_t kMaxContourVertices = 384;
		std::function<void(RE::NiAVObject*, DebugBoundSphere&, bool&, RE::NiPoint3&, float&)> collectModelData;
		collectModelData = [&](RE::NiAVObject* a_object, DebugBoundSphere& a_snapshot, bool& a_hasFallback, RE::NiPoint3& a_fallbackCenter, float& a_fallbackRadius) {
			if (!a_object || a_object->GetAppCulled()) return;

			const auto& bound = a_object->worldBound;
			if (!a_hasFallback && std::isfinite(bound.fRadius) && bound.fRadius > 0.01f &&
				std::isfinite(bound.center.x) && std::isfinite(bound.center.y) && std::isfinite(bound.center.z)) {
				a_hasFallback = true;
				a_fallbackCenter = bound.center;
				a_fallbackRadius = bound.fRadius;
			}

			if (auto* shape = a_object->IsTriShape(); shape && a_snapshot.worldVertices && a_snapshot.worldVertices->size() < kMaxContourVertices) {
				auto* renderShape = static_cast<RE::BSGraphics::TriShape*>(shape->rendererData);
				if (renderShape && renderShape->vertexBuffer && renderShape->vertexBuffer->data) {
					// FO4 stores position attributes as packed half floats for the
					// regular game vertex formats. Decode the six-byte position locally
					// so contour sampling never calls an engine relocation.
					const std::size_t stride = shape->vertexDesc.GetSize();
					const std::size_t vertexCount = shape->numVertices;
					const std::size_t dataSize = renderShape->vertexBuffer->dataSize;
					const std::size_t positionOffset = shape->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
					const bool validLayout = stride >= 6 && positionOffset <= stride - 6 && vertexCount > 0 &&
						vertexCount <= 65535 && dataSize >= vertexCount * stride;
					if (validLayout) {
						const auto* bytes = static_cast<const std::uint8_t*>(renderShape->vertexBuffer->data);
						const std::size_t remaining = kMaxContourVertices - a_snapshot.worldVertices->size();
						const std::size_t step = std::max<std::size_t>(1, (vertexCount + remaining - 1) / remaining);
						for (std::size_t index = 0; index < vertexCount && a_snapshot.worldVertices->size() < kMaxContourVertices; index += step) {
							RE::NiPoint3 localPoint{ 0.0f, 0.0f, 0.0f };
							if (!ReadPackedPosition(bytes, dataSize, stride, positionOffset, index, localPoint)) continue;

							const RE::NiPoint3 scaledPoint{
								localPoint.x * a_object->world.scale,
								localPoint.y * a_object->world.scale,
								localPoint.z * a_object->world.scale
							};
							RE::NiPoint3 worldPoint = TransformMath::Multiply(a_object->world.rotate, scaledPoint);
							worldPoint = TransformMath::Add(worldPoint, a_object->world.translate);
							if (std::isfinite(worldPoint.x) && std::isfinite(worldPoint.y) && std::isfinite(worldPoint.z)) {
								a_snapshot.worldVertices->push_back(worldPoint);
							}
						}
					}
				}
			}

			if (auto* node = a_object->IsNode()) {
				for (const auto& child : node->children) {
					collectModelData(child.get(), a_snapshot, a_hasFallback, a_fallbackCenter, a_fallbackRadius);
				}
			}
		};

		std::function<void(RE::NiAVObject*, const std::string&, bool)> appendModel;
		appendModel = [&](RE::NiAVObject* a_model, const std::string& a_slotName, bool a_isHolster) {
			if (!a_model || a_model->GetAppCulled()) return;
			DebugBoundSphere snapshot;
			snapshot.worldVertices = std::make_shared<std::vector<RE::NiPoint3>>();
			snapshot.meshName = a_model->name.c_str();
			snapshot.slotName = a_slotName;
			snapshot.isHolster = a_isHolster;
			bool hasFallback = false;
			RE::NiPoint3 fallbackCenter{ 0.0f, 0.0f, 0.0f };
			float fallbackRadius = 0.0f;
			collectModelData(a_model, snapshot, hasFallback, fallbackCenter, fallbackRadius);

			if (snapshot.worldVertices && !snapshot.worldVertices->empty()) {
				RE::NiPoint3 minPoint = snapshot.worldVertices->front();
				RE::NiPoint3 maxPoint = minPoint;
				for (const auto& point : *snapshot.worldVertices) {
					minPoint.x = std::min(minPoint.x, point.x);
					minPoint.y = std::min(minPoint.y, point.y);
					minPoint.z = std::min(minPoint.z, point.z);
					maxPoint.x = std::max(maxPoint.x, point.x);
					maxPoint.y = std::max(maxPoint.y, point.y);
					maxPoint.z = std::max(maxPoint.z, point.z);
					}
				snapshot.worldCenter = (minPoint + maxPoint) * 0.5f;
				snapshot.worldRadius = 0.0f;
				for (const auto& point : *snapshot.worldVertices) {
					const auto delta = point - snapshot.worldCenter;
					snapshot.worldRadius = std::max(snapshot.worldRadius, std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
				}
				snapshot.hasGeometryContour = true;
			}
			else if (hasFallback) {
				snapshot.worldCenter = fallbackCenter;
				snapshot.worldRadius = fallbackRadius;
			}

			if (snapshot.worldRadius > 0.01f && std::isfinite(snapshot.worldRadius)) {
				outSpheres.push_back(std::move(snapshot));
			}
		};

		for (const auto& [slotName, state] : slotsIt->second) {
			for (const auto& model : state.currentModels) appendModel(model.get(), slotName, false);
			for (const auto& model : state.currentHolsters) appendModel(model.get(), slotName, true);
			for (const auto& model : state.currentModelGroups) appendModel(model.get(), slotName, false);
		}
	}

	void HolsterManager::UpdateActorTransforms(RE::Actor* a_actor, std::vector<DebugBox>& newBoxes, std::vector<DebugNode>& newNodes) {
		std::lock_guard<std::mutex> evalLock(_evalMutex); // 🌟 多线程防爆锁：确保读取时没人正在修改数据！
		if (!a_actor || ModelManager::IsMainMenuTransition()) return;
		DebugSettings debugSettingsSnapshot;
		{
			std::lock_guard<std::mutex> settingsLock(debugSettingsMutex);
			debugSettingsSnapshot = debugSettings;
		}

		RE::TESFormID actorID = a_actor->GetFormID();
		auto nodeStatesIt = _actorNodeStates.find(actorID);
		auto slotStatesIt = _actorDisplaySlots.find(actorID);
		if (nodeStatesIt == _actorNodeStates.end() && slotStatesIt == _actorDisplaySlots.end()) return;
		uint64_t nowTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		auto actor3D = a_actor->Get3D(false);
		if (!actor3D) return;
		auto* config = ConfigManager::GetSingleton();
		const auto runtimeSettings = config->GetRuntimeSettingsSnapshot();
		// This must be enforced in the transform pass as well as evaluation. The
		// transform pass runs independently and otherwise restores culled NPC models.
		const bool suppressDisplays =
			(!a_actor->IsPlayerRef() && !runtimeSettings.enableNPCDisplays) ||
			config->IsActorDisplayBlocked(a_actor);

		if (nodeStatesIt != _actorNodeStates.end()) {
			for (auto& [name, nState] : nodeStatesIt->second) {
				auto cmeManaged = NodeManager::GetManagedNode(a_actor, FormatCMEName(name));
				if (cmeManaged && cmeManaged->node) {
					auto* cmeNode = cmeManaged->node;
					TransformMath::ApplyAdvancedTransform(a_actor, cmeNode, cmeManaged->orig, nState.finalTransform, nState.isAbsolute);
					RE::NiUpdateData ctxForce; ctxForce.flags = 0x1;
					cmeNode->UpdateTransforms(ctxForce);
				}
			}
		}
		if (slotStatesIt == _actorDisplaySlots.end()) return;
		auto& sStates = slotStatesIt->second;

		for (auto& [slotName, sState] : sStates) {
			if ((sState.currentModels.empty() && sState.currentHolsters.empty() && sState.currentModelGroups.empty() &&
				sState.oldModels.empty() && sState.oldHolsters.empty() && sState.oldModelGroups.empty()) || sState.lastTargetNode.empty()) continue;

			ActorDisplayLifecycle::RetireDeferredModels(sState, _currentUpdateTick);

			auto movManaged = NodeManager::GetManagedNode(a_actor, FormatMOVName(slotName));
			if (!movManaged || !movManaged->node) continue;
			auto* movNode = movManaged->node;

			RE::NiTransform baseLocal;
			baseLocal.MakeIdentity();
			baseLocal.translate = sState.slotBaseTransform.pos;
			baseLocal.rotate = TransformMath::EulerToMatrix(sState.slotBaseTransform.rot);
			baseLocal.scale = sState.slotBaseTransform.scale;

			std::string rawNodeName = sState.lastTargetNode;

			PhysicsValues* activePhysPtr = nullptr;
			if (sState.hasActivePhys) {
				activePhysPtr = &sState.activePhys;
			}
			else if (nodeStatesIt != _actorNodeStates.end()) {
				auto nodeStateIt = nodeStatesIt->second.find(rawNodeName);
				if (nodeStateIt != nodeStatesIt->second.end()) {
					activePhysPtr = &nodeStateIt->second.activePhys;
				}
				else {
					std::string stripped = rawNodeName;
					if (stripped.find("IAD_CME_") == 0) stripped = stripped.substr(8);
					nodeStateIt = nodeStatesIt->second.find(stripped);
					if (nodeStateIt != nodeStatesIt->second.end()) activePhysPtr = &nodeStateIt->second.activePhys;
				}
			}

			// Furniture animations can temporarily change or replace the skeleton
			// parent transform. Do not integrate display sway against that transient
			// space; keep the MOV at its configured local transform until it ends.
			const bool suspendPhysicsForFurniture = IsActorUsingFurnitureForDisplayHide(a_actor);
			if (!sState.previewTransformActive && !suppressDisplays && runtimeSettings.enableEquipmentPhysics && !suspendPhysicsForFurniture && activePhysPtr && !activePhysPtr->disabled && movNode->parent) {
				float dt = (sState.lastUpdateTime != 0) ? (nowTime - sState.lastUpdateTime) / 1000000.0f : 0.016f;
				dt = std::clamp(dt, 0.001f, 0.1f);
				if (!sState.physicsSim) {
					sState.physicsSim = std::make_shared<IAD::SimComponent>(baseLocal, *activePhysPtr);
					sState.physicsSim->Reset(movNode->parent->world);
				}
				else sState.physicsSim->UpdateConfig(*activePhysPtr);

				sState.physicsSim->ReadTransforms(movNode->parent->world, dt);
				sState.physicsSim->UpdateMotion(dt);
				movNode->local = sState.physicsSim->GetObjectLocalTransform();

				if (activePhysPtr->drawConstraints || activePhysPtr->drawPendulum) {
					DebugBox dbox;
					dbox.drawBox = activePhysPtr->enableBoxConstraint && activePhysPtr->drawConstraints;
					dbox.drawSphere = activePhysPtr->enableSphereConstraint && activePhysPtr->drawConstraints;
					dbox.drawPendulum = activePhysPtr->drawPendulum;

					if (sState.physicsSim->GetDebugData(dbox)) {
						if (!activePhysPtr->drawConstraints) {
							dbox.drawAngular = false;
						}
						newBoxes.push_back(dbox);
					}
				}
			}
			else {
				if (sState.physicsSim) sState.physicsSim.reset();
				movNode->local = baseLocal;
			}
			sState.lastUpdateTime = nowTime;

			auto ApplyTransforms = [&](std::vector<RE::NiPointer<RE::NiAVObject>>& modelArray, TransformData& tData, bool isHidden, std::size_t activeCount) {
				for (size_t i = 0; i < modelArray.size(); ++i) {
					auto& model = modelArray[i];
					if (!model) continue;

					// 恢复原本完美的逻辑
					bool shouldShow = !suppressDisplays && !isHidden && i < activeCount;
					ApplyGeometryTransform(model.get(), sState.overrideGeometryTransform, sState.geometryTransform);

					if (shouldShow) {
						RE::NiPoint3 arrayOffset = sState.arrayDir * (sState.arraySpacing * static_cast<float>(i));
						RE::NiMatrix3 mRot = TransformMath::EulerToMatrix(tData.rot);
						RE::NiPoint3 p = tData.pivot;
						RE::NiPoint3 pRot = mRot * p;
						model->local.MakeIdentity();
						model->local.translate.x = tData.pos.x + (p.x - pRot.x) + arrayOffset.x;
						model->local.translate.y = tData.pos.y + (p.y - pRot.y) + arrayOffset.y;
						model->local.translate.z = tData.pos.z + (p.z - pRot.z) + arrayOffset.z;
						model->local.rotate = mRot;
						model->local.scale = tData.scale;
						if (model->GetAppCulled()) model->SetAppCulled(false);
					}
					else if (!model->GetAppCulled()) model->SetAppCulled(true);
				}
				};

			// Recheck weaponState here as well as in EvaluateActor. Equip events and
			// async model callbacks can straddle the engine's draw/sheath transition.
			const bool weaponHiddenNow = ShouldHideWeaponDisplay(a_actor, sState);
			const bool holsterHiddenNow = ShouldHideHolsterDisplay(a_actor, sState);
			const auto activeModelCount = sState.numModelsToSpawn > 0 ? static_cast<std::size_t>(sState.numModelsToSpawn) : 0;
			ApplyTransforms(sState.currentModels, sState.meshTransform, weaponHiddenNow, activeModelCount);
			ApplyTransforms(sState.oldModels, sState.meshTransform, true, 0);
			ApplyTransforms(sState.currentHolsters, sState.holsterMeshTransform, holsterHiddenNow, activeModelCount);
			ApplyTransforms(sState.oldHolsters, sState.holsterMeshTransform, true, 0);

			auto ApplyModelGroupTransforms = [&](std::vector<RE::NiPointer<RE::NiAVObject>>& modelArray, bool allowVisible) {
				for (size_t i = 0; i < modelArray.size(); ++i) {
					auto& model = modelArray[i];
					if (!model) continue;

					TransformData tData;
					if (i < sState.modelGroupTransforms.size()) tData = sState.modelGroupTransforms[i];
					bool hideWithWeapon = i < sState.modelGroupHideWithWeapon.size() ? sState.modelGroupHideWithWeapon[i] : true;
					bool conditionVisible = i < sState.modelGroupConditionVisible.size() ? sState.modelGroupConditionVisible[i] : true;
					bool shouldShow = allowVisible && !suppressDisplays && conditionVisible &&
						(hideWithWeapon ? !ShouldHideWeaponDisplay(a_actor, sState) : !sState.isSlotHidden);
					if (i < sState.modelGroupEffects.size()) {
						ModelManager::GetSingleton()->ApplyModelEffect(model.get(), sState.modelGroupEffects[i]);
					}
					if (i < sState.modelGroupLights.size()) {
						ModelManager::GetSingleton()->ApplyModelLight(model.get(), sState.modelGroupLights[i]);
					}
					if (i < sState.modelGroupInvisible.size() && sState.modelGroupInvisible[i]) {
						ModelManager::GetSingleton()->SetModelAlpha(model.get(), 0.0f);
					}
					if (i < sState.modelGroupHideGeometry.size() && sState.modelGroupHideGeometry[i]) {
						ModelManager::GetSingleton()->SetModelGeometryHidden(model.get(), true);
					}
					if (i < sState.modelGroupGeometryTransforms.size()) {
						const bool overrideGeometry = i < sState.modelGroupOverrideGeometryTransforms.size() ? sState.modelGroupOverrideGeometryTransforms[i] : false;
						ApplyGeometryTransform(model.get(), overrideGeometry, sState.modelGroupGeometryTransforms[i]);
					}

					if (shouldShow) {
						RE::NiMatrix3 mRot = TransformMath::EulerToMatrix(tData.rot);
						RE::NiPoint3 p = tData.pivot;
						RE::NiPoint3 pRot = mRot * p;
						model->local.MakeIdentity();
						model->local.translate.x = tData.pos.x + (p.x - pRot.x);
						model->local.translate.y = tData.pos.y + (p.y - pRot.y);
						model->local.translate.z = tData.pos.z + (p.z - pRot.z);
						model->local.rotate = mRot;
						model->local.scale = tData.scale;
						if (model->GetAppCulled()) model->SetAppCulled(false);
					}
					else if (!model->GetAppCulled()) model->SetAppCulled(true);
				}
				};

			ApplyModelGroupTransforms(sState.currentModelGroups, true);

			RE::NiUpdateData ctx; ctx.flags = 0x1;
			movNode->UpdateTransforms(ctx);
			std::vector<std::string> updatedGroupMovs;
			for (const auto& groupMovName : sState.modelGroupMovNames) {
				if (groupMovName.empty() || groupMovName == FormatMOVName(slotName)) continue;
				if (std::find(updatedGroupMovs.begin(), updatedGroupMovs.end(), groupMovName) != updatedGroupMovs.end()) continue;
				updatedGroupMovs.push_back(groupMovName);
				auto groupMovManaged = NodeManager::GetManagedNode(a_actor, groupMovName);
				if (groupMovManaged && groupMovManaged->node) {
					groupMovManaged->node->UpdateTransforms(ctx);
				}
			}
		}

		if (a_actor->IsPlayerRef() && (debugSettingsSnapshot.showVanilla || debugSettingsSnapshot.showCME || debugSettingsSnapshot.showMOV || debugSettingsSnapshot.worldPreviewEdit)) {
			auto monitorNames = config->GetNodeMonitorNamesSnapshot();
			bool useMonitorFilter = runtimeSettings.nodeMonitorUseFilter && !monitorNames.empty();
			auto passesMonitorFilter = [&](const std::string& nodeName) -> bool {
				if (!useMonitorFilter) return true;
				auto stripped = StripManagedNodePrefix(nodeName);
				for (const auto& monitorName : monitorNames) {
					if (monitorName.empty()) continue;
					if (nodeName == monitorName || stripped == monitorName) return true;
					if (FormatCMEName(monitorName) == nodeName || FormatMOVName(monitorName) == nodeName) return true;
				}
				return false;
				};

			std::lock_guard<std::mutex> lock(NodeManager::_cacheMutex);
			auto itCache = NodeManager::_nodeCache.find(actorID);
			if (itCache != NodeManager::_nodeCache.end()) {
				for (auto& [nodeName, managedNode] : itCache->second.activeNodes) {
					if (managedNode.node && managedNode.node->local.scale != 0.0f) {
						DebugNodeType nType = DebugNodeType::kVanilla;
						if (nodeName.find("IAD_CME_") == 0) nType = DebugNodeType::kCME;
						else if (nodeName.find("IAD_MOV_") == 0) nType = DebugNodeType::kMOV;

						// Editing must be able to reach a node even when the optional
						// monitor filter is hiding it from the diagnostics view.
						if (!debugSettingsSnapshot.worldPreviewEdit && !passesMonitorFilter(nodeName)) continue;

						if ((nType == DebugNodeType::kCME && (debugSettingsSnapshot.showCME || debugSettingsSnapshot.worldPreviewEdit)) ||
							(nType == DebugNodeType::kMOV && (debugSettingsSnapshot.showMOV || debugSettingsSnapshot.worldPreviewEdit)))
						{
							DebugNode dn;
							dn.pos = managedNode.node->world.translate;
							dn.hasParent = (managedNode.node->parent != nullptr);
							if (dn.hasParent) dn.parentPos = managedNode.node->parent->world.translate;
							dn.parentWorldRotate.MakeIdentity();
							dn.localRotate.MakeIdentity();
							dn.rootWorldRotate = actor3D->world.rotate;
							if (dn.hasParent) dn.parentWorldRotate = managedNode.node->parent->world.rotate;
							dn.localRotate = managedNode.node->local.rotate;

							dn.name = nodeName;
							dn.type = nType;
							auto& R = managedNode.node->world.rotate;
							dn.axisX = { R.entry[0][0], R.entry[0][1], R.entry[0][2] };
							dn.axisY = { R.entry[1][0], R.entry[1][1], R.entry[1][2] };
							dn.axisZ = { R.entry[2][0], R.entry[2][1], R.entry[2][2] };
							if (nType == DebugNodeType::kCME && nodeStatesIt != _actorNodeStates.end()) {
								auto nodeStateIt = nodeStatesIt->second.find(StripManagedNodePrefix(nodeName));
								if (nodeStateIt == nodeStatesIt->second.end()) nodeStateIt = nodeStatesIt->second.find(nodeName);
								if (nodeStateIt != nodeStatesIt->second.end()) dn.isAbsolute = nodeStateIt->second.isAbsolute;
							}
							newNodes.push_back(dn);
						}
					}
				}
			}
		}
	}
}
