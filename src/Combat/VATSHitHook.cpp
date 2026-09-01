#include "pch.h"
#include "VATSHitHook.h"
#include "WeaponHitSystem.h"
#include "DisarmHandler.h"
#include "System/HolsterManager.h"
#include "Engine/NodeManager.h"

#include <RE/T/TESHitEvent.h>
#include <RE/D/DamageImpactData.h>
#include <RE/A/Actor.h>
#include <RE/T/TESForm.h>
#include <RE/H/HitData.h>
#include <RE/M/MiddleHighProcessData.h>
#include <cstring>

namespace IAD::Combat
{
	void VATSHitHandler::Register()
	{
		auto hitEventSource = RE::TESHitEvent::GetEventSource();
		if (hitEventSource) {
			hitEventSource->RegisterSink(this);
			REX::INFO("[IAD Combat] VATSHitHandler 已注册到 TESHitEvent 事件源");
		}
		else {
			REX::WARN("[IAD Combat] 无法获取 TESHitEvent 事件源！注册失败！");
		}
	}

	RE::BSEventNotifyControl VATSHitHandler::ProcessEvent(
		const RE::TESHitEvent& a_event,
		[[maybe_unused]] RE::BSTEventSource<RE::TESHitEvent>* a_eventSource)
	{
		// 检查缴械功能是否启用
		if (!g_disarmEnabled)
			return RE::BSEventNotifyControl::kContinue;

		// 仅处理包含 HitData 的事件
		if (!a_event.usesHitData)
			return RE::BSEventNotifyControl::kContinue;

		const auto& hitData = a_event.hitData;

		// ==========================================
		// 步骤 1: 获取目标角色
		// ==========================================
		auto targetRef = a_event.target;
		if (!targetRef)
			return RE::BSEventNotifyControl::kContinue;

		auto targetActor = targetRef->As<RE::Actor>();
		if (!targetActor)
			return RE::BSEventNotifyControl::kContinue;

		// 跳过玩家（避免自我缴械）
		if (targetActor->IsPlayerRef())
			return RE::BSEventNotifyControl::kContinue;

		// ==========================================
		// 步骤 2: 检查目标是否装备了武器
		// ==========================================
		if (!HasEquippedWeapon(targetActor)) {
			REX::INFO("[IAD Combat] 目标 {} ({:X}) 未装备武器，跳过",
				targetActor->GetDisplayFullName(), targetActor->GetFormID());
			return RE::BSEventNotifyControl::kContinue;
		}

		// ==========================================
		// 步骤 3: VATS 武器部位检测（直接命中，无需射线）
		// ------------------------------------------------------
		// 若玩家在 VATS 中明确选中了 Weapon 部位，
		// 且命中事件为 VATS 触发（hitData.VATSCommand 非空），
		// 则直接判定命中武器 → 跳转到缴械。
		// ==========================================
		bool isVATSWeaponHit = false;
		if (hitData.VATSCommand) {
			auto vatsLimb = hitData.VATSCommand->limb;
			auto damageLimb = hitData.damageLimb;
			REX::INFO("[IAD Combat] VATS hit: VATSCommand.limb=0x{:X}, hitData.damageLimb=0x{:X}",
				static_cast<std::uint32_t>(vatsLimb.get()),
				static_cast<std::uint32_t>(damageLimb.get()));

			// Check 1: native kWeapon limb (if VATS ever supports it)
			if (vatsLimb == RE::BGSBodyPartDefs::LIMB_ENUM::kWeapon) {
				REX::INFO("[IAD Combat] VATS native weapon limb hit!");
				isVATSWeaponHit = true;
			}

			// Check 2: Head2 (0x5) hit on IAD actor
			// (Plan B: CK ESP body part at index 5, damageRootNode[5] -> Weapon)
			if (!isVATSWeaponHit &&
				(vatsLimb == RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm1)) {
			RE::TESFormID tid = targetActor->GetFormID();
			auto* hm = IAD::HolsterManager::GetSingleton();
			std::lock_guard<std::mutex> lock(IAD::NodeManager::_cacheMutex);
			auto it = hm->_actorDisplaySlots.find(tid);
			if (it != hm->_actorDisplaySlots.end()) {
				REX::INFO("[IAD Combat] VATS RightArm1 hit on IAD actor -> weapon disarm!");
				isVATSWeaponHit = true;
			}
		}

		if (!isVATSWeaponHit &&
				(vatsLimb == RE::BGSBodyPartDefs::LIMB_ENUM::kHead2)) {
				bool hasIAD = false;
				{
					RE::TESFormID tid = targetActor->GetFormID();
					auto* hm = IAD::HolsterManager::GetSingleton();
					std::lock_guard<std::mutex> lock(IAD::NodeManager::_cacheMutex);
					auto it = hm->_actorDisplaySlots.find(tid);
					hasIAD = (it != hm->_actorDisplaySlots.end());
				}
				if (hasIAD) {
					REX::INFO("[IAD Combat] VATS Head2 hit on IAD actor -> weapon disarm!");
					isVATSWeaponHit = true;
				}
			}

			// Check 3: RightArm2 (0x9) hit on IAD actor (alternate CK type)
			if (!isVATSWeaponHit &&
				(vatsLimb == RE::BGSBodyPartDefs::LIMB_ENUM::kRightArm2)) {
				bool hasIAD = false;
				{
					RE::TESFormID tid = targetActor->GetFormID();
					auto* hm = IAD::HolsterManager::GetSingleton();
					std::lock_guard<std::mutex> lock(IAD::NodeManager::_cacheMutex);
					auto it = hm->_actorDisplaySlots.find(tid);
					hasIAD = (it != hm->_actorDisplaySlots.end());
				}
				if (hasIAD) {
					REX::INFO("[IAD Combat] VATS RightArm2 hit on IAD actor -> weapon disarm!");
					isVATSWeaponHit = true;
				}
			}
		}

		if (!isVATSWeaponHit) {
			// ==========================================
			// 步骤 4: 获取命中点世界坐标
			// ==========================================
			RE::NiPoint3 hitPoint;
			std::memcpy(&hitPoint, &hitData.impactData, sizeof(RE::NiPoint3));

			REX::INFO("[IAD Combat] 命中事件! 目标: {} ({:X}), 命中点: ({:.1f}, {:.1f}, {:.1f}), usesHitData: {}, VATS: {}",
				targetActor->GetDisplayFullName(),
				targetActor->GetFormID(),
				hitPoint.x, hitPoint.y, hitPoint.z,
				a_event.usesHitData,
				(hitData.VATSCommand != nullptr));

			// ==========================================
			// 步骤 5: 射线-武器网格检测（非 VATS 后备路径）
			// ==========================================
			RE::Actor* attacker = nullptr;
			if (a_event.cause) {
				attacker = a_event.cause->As<RE::Actor>();
			}
			if (!attacker) {
				auto aggressorPtr = hitData.aggressor.get();
				attacker = aggressorPtr.get();
			}

			RayWeaponHit rayInfo;
			bool hitWeapon = DoesAttackHitWeaponFirst(attacker, targetActor, hitPoint, &rayInfo);

			if (!hitWeapon) {
				return RE::BSEventNotifyControl::kContinue;
			}

			REX::INFO("[IAD Combat] ★ 武器被击中! (来源={}, mesh={}) 触发缴械!",
				rayInfo.source, rayInfo.meshName);
		}

		// ==========================================
		// 步骤 6: 执行缴械
		// ==========================================
		RE::TESFormID targetID = targetActor->GetFormID();
		RE::TESFormID attackerID = 0;
		if (a_event.cause) {
			auto* attacker = a_event.cause->As<RE::Actor>();
			if (attacker) attackerID = attacker->GetFormID();
		}
		if (attackerID == 0) {
			auto aggressorPtr = hitData.aggressor.get();
			if (aggressorPtr) attackerID = aggressorPtr->GetFormID();
		}

		auto task = F4SE::GetTaskInterface();
		if (task) {
			task->AddTask([targetID, attackerID]() {
				auto target = RE::TESForm::GetFormByID<RE::Actor>(targetID);
				RE::Actor* attacker = nullptr;
				if (attackerID != 0) {
					attacker = RE::TESForm::GetFormByID<RE::Actor>(attackerID);
				}

				if (target) {
					DisarmActor(target, attacker);
				}
				});
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
