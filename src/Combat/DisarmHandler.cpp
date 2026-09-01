#include "pch.h"
#include "DisarmHandler.h"

#include <RE/A/Actor.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESForm.h>
#include <RE/B/BGSObjectInstance.h>
#include <RE/B/BGSInventoryItem.h>

namespace IAD::Combat
{
	// 全局缴械冷却追踪（actor formID → 冷却到期时间）
	static std::unordered_map<RE::TESFormID, std::chrono::steady_clock::time_point> g_disarmCooldowns;
	static std::mutex g_cooldownMutex;

	RE::TESObjectWEAP* GetEquippedWeapon(RE::Actor* a_actor)
	{
		if (!a_actor || !a_actor->currentProcess || !a_actor->inventoryList)
			return nullptr;

		// 遍历库存，找到装备中的武器
		BSAutoReadLock lock(a_actor->inventoryList->rwLock);
		for (auto& item : a_actor->inventoryList->data) {
			if (!item.object)
				continue;
			auto* weap = item.object->As<RE::TESObjectWEAP>();
			if (!weap)
				continue;

			// 检查是否有装备的堆栈
			auto* stack = item.stackData.get();
			while (stack) {
				if (stack->IsEquipped()) {
					return weap;
				}
				stack = stack->nextStack.get();
			}
		}
		return nullptr;
	}

	bool GetEquippedWeaponInstance(RE::Actor* a_actor, RE::BGSObjectInstance& a_outInstance)
	{
		auto* weap = GetEquippedWeapon(a_actor);
		if (!weap)
			return false;

		// 直接构造 BGSObjectInstance（无实例数据 = 基础武器形态）
		a_outInstance = RE::BGSObjectInstance(weap, nullptr);
		return true;
	}

	bool HasEquippedWeapon(RE::Actor* a_actor)
	{
		return GetEquippedWeapon(a_actor) != nullptr;
	}

	bool IsDisarmOnCooldown(RE::Actor* a_actor)
	{
		if (!a_actor)
			return true;
		RE::TESFormID id = a_actor->GetFormID();
		std::lock_guard<std::mutex> lock(g_cooldownMutex);
		auto it = g_disarmCooldowns.find(id);
		if (it == g_disarmCooldowns.end())
			return false;
		if (std::chrono::steady_clock::now() >= it->second) {
			g_disarmCooldowns.erase(it);
			return false;
		}
		return true;
	}

	void ResetDisarmCooldown(RE::Actor* a_actor)
	{
		if (!a_actor)
			return;
		RE::TESFormID id = a_actor->GetFormID();
		std::lock_guard<std::mutex> lock(g_cooldownMutex);
		g_disarmCooldowns[id] = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_disarmCooldownMs);
	}

	bool DisarmActor(RE::Actor* a_target, RE::Actor* a_attacker)
	{
		if (!a_target || !g_disarmEnabled)
			return false;

		// 冷却检查
		if (IsDisarmOnCooldown(a_target))
			return false;

		// 直接获取武器形态，而不是 BGSObjectInstance
		auto* weap = GetEquippedWeapon(a_target);
		if (!weap)
			return false;

		RE::TESFormID targetID = a_target->GetFormID();
		RE::TESFormID attackerID = a_attacker ? a_attacker->GetFormID() : 0;

		REX::INFO("[IAD Combat] 缴械触发! 目标: {:X}, 武器: {:X}",
			targetID, weap->GetFormID());

		// 使用任务系统确保在主线程执行 DropObject
		auto task = F4SE::GetTaskInterface();
		if (!task) {
			REX::WARN("[IAD Combat] 无法获取任务接口，缴械失败");
			return false;
		}

		RE::NiPoint3 targetPos = a_target->GetPosition();
		RE::NiPoint3 dropPos{
			targetPos.x + 30.0f,  // 角色前方掉落
			targetPos.y,
			targetPos.z + 10.0f   // 略高于地面
		};

		// 捕获 weapon form ID 和 dropPos，在 Task 内构造 BGSObjectInstance
		RE::TESFormID weaponID = weap->GetFormID();
		task->AddTask([targetID, attackerID, weaponID, dropPos]() {
			auto target = RE::TESForm::GetFormByID<RE::Actor>(targetID);
			if (!target || target->IsDead(false))
				return;

			auto weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(weaponID);
			if (!weapon)
				return;

			// 步骤 1: 先卸下武器（防止 NPC 立即重新装备）
			RE::BGSObjectInstance inst(weapon, nullptr);
			auto* equipMgr = RE::ActorEquipManager::GetSingleton();
			if (equipMgr) {
				equipMgr->UnequipObject(
					target,           // a_actor
					&inst,            // a_object
					1,                // a_number
					nullptr,          // a_slot
					0,                // a_stackID
					true,             // a_queueEquip
					false,            // a_forceEquip
					false,            // a_playSounds (不播放卸装音效)
					true,             // a_applyNow
					nullptr);         // a_slotBeingReplaced
			}

			// 步骤 2: 调用引擎原生的 DropObject（武器从库存移除 → 掉落在地上）
			target->DropObject(inst, nullptr, 1, &dropPos, nullptr);

			REX::INFO("[IAD Combat] 缴械成功! 武器已掉落");

			if (attackerID != 0) {
				auto attacker = RE::TESForm::GetFormByID<RE::Actor>(attackerID);
				if (attacker) {
					REX::INFO("[IAD Combat] {} 缴械了 {} 的武器!",
						attacker->GetDisplayFullName(),
						target->GetDisplayFullName());
				}
			}
			});

		// 设置冷却
		ResetDisarmCooldown(a_target);

		return true;
	}
}
