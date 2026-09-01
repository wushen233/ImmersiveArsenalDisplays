#pragma once
#include "pch.h"
#include <vector>
#include <RE/B/BGSBodyPartDefs.h>

namespace IAD::Combat
{
	// ======================================
	// 模型位置检测 (射击武器模型→缴械)
	// ======================================

	/// 获取角色装备武器的原生骨骼节点 (weaponBone) 世界坐标
	bool GetNativeWeaponBonePosition(RE::Actor* a_actor, RE::NiPoint3& a_outPos);

	/// 一个 IAD 武器模型的世界坐标 + 估算包围盒半径信息
	struct WeaponModelInfo
	{
		RE::NiPoint3 worldPos;
		float        radius;
		std::string  slotName;
	};

	/// 获取目标角色身上所有 IAD 武器模型的世界坐标与估算半径
	std::vector<WeaponModelInfo> GetActorWeaponModelPositions(RE::Actor* a_actor);

	/// 原生武器骨骼节点子节点树检测
	bool IsHitNearNativeWeaponBone(RE::Actor* a_actor, const RE::NiPoint3& a_hitPoint, float a_tolerance);

	/// IAD 显示模型子节点树（包围球）检测
	bool IsHitNearIADModels(RE::Actor* a_actor, const RE::NiPoint3& a_hitPoint, float a_tolerance);

	/// 射线-点最近距离检测
	/// 计算从 a_rayOrigin 到 a_rayEnd 的射线段，到 a_targetPoint 的最短距离
	/// @return true 如果最短距离 <= a_tolerance
	bool IsAttackRayNearPoint(
		const RE::NiPoint3& a_rayOrigin,
		const RE::NiPoint3& a_rayEnd,
		const RE::NiPoint3& a_targetPoint,
		float a_tolerance);

	/// 综合检测：命中点是否在目标角色的武器模型附近
	/// 检测策略（二阶段）：
	///   阶段1: 直接距离检测 — 命中点到武器节点 + 子节点树遍历 (tolerance=100)
	///   阶段2: 攻击射线检测 — 攻击者→命中点的射线是否经过武器节点附近 (tolerance=120)
	/// @param a_actor     目标角色
	/// @param a_attacker  攻击者（可选，提供后可做射线检测）
	/// @param a_hitPoint  命中点世界坐标
	/// @param a_tolerance 检测容差（游戏单位，默认 100.0f）
	/// @return true 如果任一阶段判定命中武器
	bool IsHitNearEquippedWeapon(
		RE::Actor* a_actor,
		RE::Actor* a_attacker,
		const RE::NiPoint3& a_hitPoint,
		float a_tolerance = 100.0f);

	/// 估算一个 NiAVObject 的包围盒半径
	float EstimateObjectRadius(RE::NiAVObject* a_object);

	// ======================================
	// 射线-武器网格精确检测 (新主路径)
	// ======================================

	/// 射线武器命中信息
	struct RayWeaponHit
	{
		bool        hit = false;
		float       t = 1.0f;           ///< 归一化沿射线参数 [0,1]；t<1 表示在到达终点前已击中武器
		float       worldDistance = 0;  ///< 沿射线方向到命中点的世界距离（游戏单位）
		std::string source;             ///< "native_bone" 或 slot 名
		std::string meshName;           ///< 击中的网格名（诊断用）
	};

	/// 对角色身上所有 IAD 武器模型与原生 weaponBone 子树做线段-包围球求交。
	/// 取所有相交中"最靠近射线起点"的作为结果。
	/// @param a_actor    目标角色
	/// @param a_rayOrig  射线起点（世界坐标）
	/// @param a_rayEnd   射线终点（世界坐标）
	/// @return 是否击中，以及最近相交点信息
	RayWeaponHit RaycastActorWeapons(
		RE::Actor* a_actor,
		const RE::NiPoint3& a_rayOrig,
		const RE::NiPoint3& a_rayEnd);

	/// 判定：攻击者→身体命中点的射线，是否在到达身体之前就已经穿过武器模型。
	/// 这是"NV 风格 射线优先击中武器"的判定主入口。
	/// @param a_attacker  攻击者（用于计算射线起点）
	/// @param a_target    目标角色（被射击者）
	/// @param a_bodyHit   引擎给出的身体命中点（射线终点）
	/// @param a_outHit    可选输出：详细的射线命中信息
	/// @return true 表示射线在击中身体前先穿过了武器（应触发缴械）
	bool DoesAttackHitWeaponFirst(
		RE::Actor* a_attacker,
		RE::Actor* a_target,
		const RE::NiPoint3& a_bodyHit,
		RayWeaponHit* a_outHit = nullptr);
}
