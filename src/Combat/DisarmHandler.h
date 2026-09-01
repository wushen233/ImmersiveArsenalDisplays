#pragma once
#include "pch.h"

namespace IAD::Combat
{
	/// 配置：是否启用武器命中 → 缴械功能（可在 ini 中扩展）
	inline bool g_disarmEnabled = true;

	/// 配置：缴械时掉落武器的数量（0 = 卸下但不掉落，1 = 正常掉落）
	inline int  g_disarmDropCount = 1;

	/// 配置：缴械触发冷却时间（毫秒，防止同一目标被多次缴械）
	inline int  g_disarmCooldownMs = 3000;

	// ======================================
	// 缴械核心 API
	// ======================================

	/// 获取目标角色当前装备的主手武器
	RE::TESObjectWEAP* GetEquippedWeapon(RE::Actor* a_actor);

	/// 获取目标角色当前装备的武器物品实例（用于 DropObject）
	bool GetEquippedWeaponInstance(RE::Actor* a_actor, RE::BGSObjectInstance& a_outInstance);

	/// 执行缴械：卸下目标当前武器并掉落在地上
	bool DisarmActor(RE::Actor* a_target, RE::Actor* a_attacker = nullptr);

	/// 检查目标是否已装备武器
	bool HasEquippedWeapon(RE::Actor* a_actor);

	/// 检查目标是否处于缴械冷却中
	bool IsDisarmOnCooldown(RE::Actor* a_actor);

	/// 重置目标的缴械冷却
	void ResetDisarmCooldown(RE::Actor* a_actor);
}
