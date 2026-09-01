#pragma once
#include "pch.h"

namespace IAD::Combat
{
	/// 为所有 Race 注入 Weapon (kWeapon=0x11) 身体部位 BGSBodyPart。
	/// 应在 kGameDataReady 和 kPostLoadGame 时调用。
	void InjectWeaponBodyParts();

	/// Plan B: 将 damageRootNode[9] (RightArm2) 重定向到骨架 Weapon 节点。
	///
	/// Fallout 4 的 VATS 硬编码了显示的 body part 列表，kWeapon (index 17)
	/// 不在其中。因此我们复用 VATS 肯定会显示的 RightArm2 槽位，
	/// 将其 skeleton node 指向武器位置。
	///
	/// 在 VATSHitHook 中检测 limb == RightArm2 + 目标有 IAD 模型 → 缴械。
	///
	/// @param a_actor   目标角色
	/// @param a_hasIAD  该角色是否有 IAD 武器显示模型
	void OverrideWeaponBodyPart(RE::Actor* a_actor, bool a_hasIAD);
}
