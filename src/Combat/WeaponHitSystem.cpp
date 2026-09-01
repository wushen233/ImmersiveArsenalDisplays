#include "pch.h"
#include "WeaponHitSystem.h"
#include "System/HolsterManager.h"
#include "Engine/NodeManager.h"

#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSVisit.h>
#include <RE/A/Actor.h>
#include <RE/A/AIProcess.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/T/TESForm.h>
#include <RE/H/HitData.h>
#include <functional>

namespace IAD::Combat
{
	// ============================================================
	// 辅助：点到线段的最短距离
	// ============================================================

	static float DistancePointToSegment(
		const RE::NiPoint3& a_point,
		const RE::NiPoint3& a_segStart,
		const RE::NiPoint3& a_segEnd)
	{
		RE::NiPoint3 segDir = a_segEnd - a_segStart;
		float segLenSq = segDir.x * segDir.x + segDir.y * segDir.y + segDir.z * segDir.z;
		if (segLenSq < 0.0001f) {
			float dx = a_point.x - a_segStart.x;
			float dy = a_point.y - a_segStart.y;
			float dz = a_point.z - a_segStart.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		float segLen = std::sqrt(segLenSq);
		segDir.x /= segLen;
		segDir.y /= segLen;
		segDir.z /= segLen;

		float t = (a_point.x - a_segStart.x) * segDir.x +
				  (a_point.y - a_segStart.y) * segDir.y +
				  (a_point.z - a_segStart.z) * segDir.z;
		t = (std::max)(0.0f, (std::min)(segLen, t));

		RE::NiPoint3 closest{
			a_segStart.x + segDir.x * t,
			a_segStart.y + segDir.y * t,
			a_segStart.z + segDir.z * t
		};

		float dx = a_point.x - closest.x;
		float dy = a_point.y - closest.y;
		float dz = a_point.z - closest.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	// ============================================================
	// 攻击射线检测
	// ============================================================

	bool IsAttackRayNearPoint(
		const RE::NiPoint3& a_rayOrigin,
		const RE::NiPoint3& a_rayEnd,
		const RE::NiPoint3& a_targetPoint,
		float a_tolerance)
	{
		float dist = DistancePointToSegment(a_targetPoint, a_rayOrigin, a_rayEnd);
		return dist <= a_tolerance;
	}

	// ============================================================
	// 核心检测：使用 BSGeometry::modelBound（局部空间网格包围球）
	// modelBound 是网格本身的几何数据，克隆时总是准确的。
	// 我们手动将其变换到世界空间进行命中判定。
	// ============================================================

	static bool IsHitPointInModelBounds(
		RE::NiAVObject* a_root,
		const RE::NiPoint3& a_hitPoint)
	{
		if (!a_root)
			return false;

		bool hit = false;

		RE::BSVisit::TraverseScenegraphGeometries(a_root,
			[&](RE::BSGeometry* a_geom) -> RE::BSVisitControl
		{
			if (!a_geom || hit)
				return RE::BSVisitControl::kContinue;

			const auto& modelBound = a_geom->modelBound;
			if (modelBound.fRadius <= 0.0f)
				return RE::BSVisitControl::kContinue;

			// 将局部包围球中心变换到世界空间
			// NiTransform::operator* 做: rotate * (point * scale) + translate
			RE::NiPoint3 worldCenter = a_geom->world * modelBound.center;
			float worldRadius = modelBound.fRadius * a_geom->world.scale;

			float dx = a_hitPoint.x - worldCenter.x;
			float dy = a_hitPoint.y - worldCenter.y;
			float dz = a_hitPoint.z - worldCenter.z;

			if (dx * dx + dy * dy + dz * dz <= worldRadius * worldRadius) {
				REX::INFO("[IAD Combat]   ✓ 命中网格 '{}' (模型包围球半径={:.1f} 世界半径={:.1f})",
					a_geom->name.c_str(), modelBound.fRadius, worldRadius);
				hit = true;
				return RE::BSVisitControl::kStop;
			}

			return RE::BSVisitControl::kContinue;
		});

		return hit;
	}

	// ============================================================
	// 诊断：打印完整的模型包围球树
	// ============================================================

	static void LogModelBoundTree(
		RE::NiAVObject* a_node,
		const RE::NiPoint3& a_hitPoint,
		int a_depth = 0)
	{
		if (!a_node || a_depth > 8)
			return;

		std::string indent(static_cast<std::size_t>(a_depth) * 2, ' ');
		const auto& pos = a_node->world.translate;

		if (auto geom = a_node->IsGeometry()) {
			const auto& mb = geom->modelBound;
			RE::NiPoint3 wc = a_node->world * mb.center;
			float wr = mb.fRadius * a_node->world.scale;

			float dx = a_hitPoint.x - wc.x;
			float dy = a_hitPoint.y - wc.y;
			float dz = a_hitPoint.z - wc.z;
			float distToBound = std::sqrt(dx * dx + dy * dy + dz * dz) - wr;

			REX::INFO("[IAD Combat]   {}[MESH] {} 局部半径={:.1f} → 世界半径={:.1f} 距包围球={:.1f}{}",
				indent, a_node->name.c_str(),
				mb.fRadius, wr, distToBound,
				(distToBound <= 0.0f) ? " ★命中★" : "");
		}
		else if (auto niNode = a_node->IsNode()) {
			if (a_depth > 0) {
				REX::INFO("[IAD Combat]   {}[NODE] {} pos=({:.0f},{:.0f},{:.0f})",
					indent, a_node->name.c_str(), pos.x, pos.y, pos.z);
			}

			for (auto& child : niNode->children) {
				if (child)
					LogModelBoundTree(child.get(), a_hitPoint, a_depth + 1);
			}
		}
		else {
			REX::INFO("[IAD Combat]   {}[AVOBJ] {} pos=({:.0f},{:.0f},{:.0f})",
				indent, a_node->name.c_str(), pos.x, pos.y, pos.z);
		}
	}

	// ============================================================
	// 原生武器骨骼节点 — modelBound 检测
	// ============================================================

	bool GetNativeWeaponBonePosition(RE::Actor* a_actor, RE::NiPoint3& a_outPos)
	{
		if (!a_actor || !a_actor->currentProcess || !a_actor->currentProcess->middleHigh)
			return false;

		auto* weaponNode = a_actor->currentProcess->middleHigh->weaponBone;
		if (!weaponNode)
			return false;

		a_outPos = weaponNode->world.translate;
		return true;
	}

	bool IsHitNearNativeWeaponBone(RE::Actor* a_actor, const RE::NiPoint3& a_hitPoint, float a_tolerance)
	{
		if (!a_actor || !a_actor->currentProcess || !a_actor->currentProcess->middleHigh)
			return false;

		auto* weaponNode = a_actor->currentProcess->middleHigh->weaponBone;
		if (!weaponNode)
			return false;

		// 使用模型包围球检测（覆盖所有子网格几何体）
		if (IsHitPointInModelBounds(weaponNode, a_hitPoint))
			return true;

		// 后备：world.translate 距离检测
		float dx = a_hitPoint.x - weaponNode->world.translate.x;
		float dy = a_hitPoint.y - weaponNode->world.translate.y;
		float dz = a_hitPoint.z - weaponNode->world.translate.z;
		if (dx * dx + dy * dy + dz * dz <= a_tolerance * a_tolerance)
			return true;

		return false;
	}

	// ============================================================
	// IAD 模型 — modelBound 检测
	// ============================================================

	float EstimateObjectRadius(RE::NiAVObject* a_object)
	{
		if (!a_object)
			return 0.0f;

		const auto& origin = a_object->world.translate;
		float maxDistSq = 0.0f;

		std::function<void(RE::NiAVObject*)> traverse;
		traverse = [&](RE::NiAVObject* node) {
			if (!node)
				return;

			// 使用 modelBound 的半径（更准确）
			if (auto geom = node->IsGeometry()) {
				RE::NiPoint3 worldCenter = node->world * geom->modelBound.center;
				float worldRadius = geom->modelBound.fRadius * node->world.scale;
				float dx = worldCenter.x - origin.x;
				float dy = worldCenter.y - origin.y;
				float dz = worldCenter.z - origin.z;
				float distSq = dx * dx + dy * dy + dz * dz;
				// 包围球最远点距离 = 中心距离 + 半径
				float farDist = std::sqrt(distSq) + worldRadius;
				float farDistSq = farDist * farDist;
				if (farDistSq > maxDistSq)
					maxDistSq = farDistSq;
			}
			else {
				const auto& pos = node->world.translate;
				float dx = pos.x - origin.x;
				float dy = pos.y - origin.y;
				float dz = pos.z - origin.z;
				float distSq = dx * dx + dy * dy + dz * dz;
				if (distSq > maxDistSq)
					maxDistSq = distSq;
			}

			if (auto niNode = node->IsNode()) {
				for (auto& child : niNode->children) {
					if (child)
						traverse(child.get());
				}
			}
		};

		traverse(a_object);

		float radius = std::sqrt(maxDistSq);
		return (radius < 5.0f) ? 5.0f : radius;
	}

	std::vector<WeaponModelInfo> GetActorWeaponModelPositions(RE::Actor* a_actor)
	{
		std::vector<WeaponModelInfo> results;
		if (!a_actor)
			return results;

		RE::TESFormID actorID = a_actor->GetFormID();
		auto* hm = IAD::HolsterManager::GetSingleton();

		std::lock_guard<std::mutex> cacheLock(IAD::NodeManager::_cacheMutex);

		auto slotIt = hm->_actorDisplaySlots.find(actorID);
		if (slotIt == hm->_actorDisplaySlots.end())
			return results;

		for (const auto& [slotName, slotState] : slotIt->second) {
			if (slotState.isWeaponHidden || slotState.currentModels.empty())
				continue;

			auto& activeNodes = IAD::NodeManager::_nodeCache[actorID].activeNodes;
			std::string movName = "IAD_MOV_" + slotName;
			auto movIt = activeNodes.find(movName);

			RE::NiPoint3 worldPos{ 0.0f, 0.0f, 0.0f };
			float radius = 15.0f;

			if (movIt != activeNodes.end() && movIt->second.node) {
				worldPos = movIt->second.node->world.translate;

				// 用 modelBound 计算更准确的半径
				float maxRadius = 0.0f;
				RE::BSVisit::TraverseScenegraphGeometries(movIt->second.node,
					[&](RE::BSGeometry* a_geom) -> RE::BSVisitControl
				{
					if (!a_geom)
						return RE::BSVisitControl::kContinue;
					float worldRadius = a_geom->modelBound.fRadius * a_geom->world.scale;
					if (worldRadius > maxRadius)
						maxRadius = worldRadius;
					return RE::BSVisitControl::kContinue;
				});

				if (maxRadius > 0.0f)
					radius = maxRadius;
			}
			else if (!slotState.currentModels.empty() && slotState.currentModels[0]) {
				worldPos = slotState.currentModels[0]->world.translate;
				radius = EstimateObjectRadius(slotState.currentModels[0].get());
			}

			results.push_back({ worldPos, radius, slotName });
		}

		return results;
	}

	bool IsHitNearIADModels(RE::Actor* a_actor, const RE::NiPoint3& a_hitPoint, float a_tolerance)
	{
		if (!a_actor)
			return false;

		RE::TESFormID actorID = a_actor->GetFormID();
		auto* hm = IAD::HolsterManager::GetSingleton();

		std::lock_guard<std::mutex> cacheLock(IAD::NodeManager::_cacheMutex);

		auto slotIt = hm->_actorDisplaySlots.find(actorID);
		if (slotIt == hm->_actorDisplaySlots.end())
			return false;

		for (const auto& [slotName, slotState] : slotIt->second) {
			if (slotState.isWeaponHidden || slotState.currentModels.empty())
				continue;

			for (const auto& model : slotState.currentModels) {
				if (!model)
					continue;

				REX::INFO("[IAD Combat]   IAD模型 '{}' ({}):", slotName, model->name.c_str());

				// 遍历所有网格子节点，用 modelBound 检测
				if (IsHitPointInModelBounds(model.get(), a_hitPoint)) {
					REX::INFO("[IAD Combat]   ✓ 命中IAD模型 '{}'!", slotName);
					return true;
				}

				// 诊断打印（仅未命中时）
				if (auto modelNode = model->IsNode()) {
					REX::INFO("[IAD Combat]   ↓ '{}' 网格包围球诊断:", model->name.c_str());
					LogModelBoundTree(modelNode, a_hitPoint, 1);
				}

				// 后备：world.translate 距离检测
				float dx = a_hitPoint.x - model->world.translate.x;
				float dy = a_hitPoint.y - model->world.translate.y;
				float dz = a_hitPoint.z - model->world.translate.z;
				float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (dist <= a_tolerance) {
					REX::INFO("[IAD Combat]   ✓ 命中IAD模型 '{}' (距离={:.1f} <= 容差={:.0f})",
						slotName, dist, a_tolerance);
					return true;
				}
			}
		}

		return false;
	}

	// ============================================================
	// 综合检测：是否命中武器模型（modelBound 检测 + 攻击射线后备）
	// ============================================================

	bool IsHitNearEquippedWeapon(
		RE::Actor* a_actor,
		RE::Actor* a_attacker,
		const RE::NiPoint3& a_hitPoint,
		float a_tolerance)
	{
		if (!a_actor)
			return false;

		REX::INFO("[IAD Combat] 检测武器模型命中 (modelBound 模式)...");

		// ========================================================
		// 阶段 1: modelBound 检测 — 使用网格几何体包围球
		// ========================================================
		bool pointHit = false;

		if (IsHitNearNativeWeaponBone(a_actor, a_hitPoint, a_tolerance)) {
			REX::INFO("[IAD Combat]   ✓ 命中原生武器骨骼");
			pointHit = true;
		}
		else {
			REX::INFO("[IAD Combat]   原生武器骨骼: 未命中或不可用");
		}

		if (!pointHit && IsHitNearIADModels(a_actor, a_hitPoint, a_tolerance)) {
			REX::INFO("[IAD Combat]   ✓ 命中IAD显示模型");
			pointHit = true;
		}

		if (pointHit) {
			REX::INFO("[IAD Combat] ★ modelBound 检测命中!");
			return true;
		}

		// ========================================================
		// 阶段 2: 攻击射线检测 — 后备方案
		// ========================================================
		if (a_attacker) {
			RE::NiPoint3 attackerPos = a_attacker->GetPosition();
			const float rayTolerance = 250.0f;

			REX::INFO("[IAD Combat]   攻击者位置: ({:.1f},{:.1f},{:.1f}), 射线容差={:.0f}",
				attackerPos.x, attackerPos.y, attackerPos.z, rayTolerance);

			RE::NiPoint3 weaponBonePos;
			if (GetNativeWeaponBonePosition(a_actor, weaponBonePos)) {
				float dist = DistancePointToSegment(weaponBonePos, attackerPos, a_hitPoint);
				if (dist <= rayTolerance) {
					REX::INFO("[IAD Combat]   ✓ 攻击射线经过原生武器骨骼 (距离={:.1f})", dist);
					return true;
				}
				else {
					REX::INFO("[IAD Combat]   攻击射线距原生武器骨骼 {:.1f} (容差={:.0f})",
						dist, rayTolerance);
				}
			}

			auto models = GetActorWeaponModelPositions(a_actor);
			for (const auto& modelInfo : models) {
				float dist = DistancePointToSegment(modelInfo.worldPos, attackerPos, a_hitPoint);
				float effectiveTol = (std::max)(rayTolerance, modelInfo.radius * 2.5f);
				if (dist <= effectiveTol) {
					REX::INFO("[IAD Combat]   ✓ 攻击射线经过IAD模型 '{}' (距离={:.1f})",
						modelInfo.slotName, dist);
					return true;
				}
				else {
					REX::INFO("[IAD Combat]   攻击射线距IAD模型 '{}' {:.1f} (容差={:.0f})",
						modelInfo.slotName, dist, effectiveTol);
				}
			}
		}
		else {
			REX::INFO("[IAD Combat]   无攻击者信息，跳过射线检测");
		}

		REX::INFO("[IAD Combat]   ✗ 未命中任何武器模型");
		return false;
	}

	// ============================================================
	// 射线-武器网格精确检测 (NV 风格 主路径)
	// ------------------------------------------------------------
	// 原理：
	//   引擎给出的 hitPoint 是子弹击中"身体表面"的点 (武器模型无碰撞，
	//   子弹永远先打到身体)。要判断子弹"是否穿过了武器"，必须用射线段
	//   (攻击者眼睛 → 身体命中点) 与 IAD 武器模型的网格包围球做求交。
	//   若任意网格相交，且相交参数 t ∈ [0,1] (在到达身体之前) → 命中武器。
	// ============================================================

	/// 线段 vs 球：标准二次求解
	/// 返回沿线段方向第一次进入球的归一化参数 t；线段范围 [0,1]
	static bool RaySegmentSphereIntersect(
		const RE::NiPoint3& a_O, const RE::NiPoint3& a_E,
		const RE::NiPoint3& a_C, float a_r,
		float& a_outT)
	{
		RE::NiPoint3 d{ a_E.x - a_O.x, a_E.y - a_O.y, a_E.z - a_O.z };
		RE::NiPoint3 f{ a_O.x - a_C.x, a_O.y - a_C.y, a_O.z - a_C.z };

		float A = d.x * d.x + d.y * d.y + d.z * d.z;
		if (A < 1e-6f)
			return false;  // 线段长度 0，退化

		float B = 2.0f * (f.x * d.x + f.y * d.y + f.z * d.z);
		float C = f.x * f.x + f.y * f.y + f.z * f.z - a_r * a_r;
		float disc = B * B - 4.0f * A * C;
		if (disc < 0.0f)
			return false;

		float sqrtD = std::sqrt(disc);
		float t1 = (-B - sqrtD) / (2.0f * A);
		float t2 = (-B + sqrtD) / (2.0f * A);

		// 线段范围 [0, 1]
		if (t2 < 0.0f || t1 > 1.0f)
			return false;

		// 若 origin 在球内 → 进入点 t = 0
		a_outT = (t1 > 0.0f) ? t1 : 0.0f;
		return true;
	}

	/// 对单个 NiAVObject 子树的所有 BSGeometry 做线段-包围球求交
	/// 返回最近相交点 (t 最小者)
	static bool RaycastNodeGeometries(
		RE::NiAVObject* a_root,
		const RE::NiPoint3& a_O, const RE::NiPoint3& a_E,
		float& a_outT, std::string& a_outMeshName)
	{
		if (!a_root)
			return false;

		bool found = false;
		float bestT = 2.0f;  // > 1.0 表示尚未命中

		RE::BSVisit::TraverseScenegraphGeometries(a_root,
			[&](RE::BSGeometry* a_geom) -> RE::BSVisitControl
		{
			if (!a_geom)
				return RE::BSVisitControl::kContinue;

			const auto& mb = a_geom->modelBound;
			if (mb.fRadius <= 0.0f)
				return RE::BSVisitControl::kContinue;

			RE::NiPoint3 worldCenter = a_geom->world * mb.center;
			float worldRadius = mb.fRadius * a_geom->world.scale;

			float t;
			if (RaySegmentSphereIntersect(a_O, a_E, worldCenter, worldRadius, t)) {
				if (t < bestT) {
					bestT = t;
					found = true;
					a_outMeshName = a_geom->name.c_str() ? a_geom->name.c_str() : "";
				}
			}
			return RE::BSVisitControl::kContinue;
		});

		if (found)
			a_outT = bestT;
		return found;
	}

	RayWeaponHit RaycastActorWeapons(
		RE::Actor* a_actor,
		const RE::NiPoint3& a_rayOrig,
		const RE::NiPoint3& a_rayEnd)
	{
		RayWeaponHit result;
		if (!a_actor)
			return result;

		// 计算射线长度（用于 t→世界距离换算）
		float rayDx = a_rayEnd.x - a_rayOrig.x;
		float rayDy = a_rayEnd.y - a_rayOrig.y;
		float rayDz = a_rayEnd.z - a_rayOrig.z;
		float rayLen = std::sqrt(rayDx * rayDx + rayDy * rayDy + rayDz * rayDz);

		float bestT = 1.0f;
		std::string bestSource;
		std::string bestMesh;

		// ---- 1. 原生 weaponBone 子树 ----
		if (a_actor->currentProcess && a_actor->currentProcess->middleHigh) {
			auto* weaponNode = a_actor->currentProcess->middleHigh->weaponBone;
			if (weaponNode) {
				float t;
				std::string meshName;
				if (RaycastNodeGeometries(weaponNode, a_rayOrig, a_rayEnd, t, meshName)) {
					if (t < bestT) {
						bestT = t;
						bestSource = "native_bone";
						bestMesh = meshName;
					}
				}
			}
		}

		// ---- 2. IAD 显示模型 ----
		RE::TESFormID actorID = a_actor->GetFormID();
		auto* hm = IAD::HolsterManager::GetSingleton();

		{
			std::lock_guard<std::mutex> cacheLock(IAD::NodeManager::_cacheMutex);
			auto slotIt = hm->_actorDisplaySlots.find(actorID);
			if (slotIt != hm->_actorDisplaySlots.end()) {
				for (const auto& [slotName, slotState] : slotIt->second) {
					if (slotState.isWeaponHidden || slotState.currentModels.empty())
						continue;

					for (const auto& model : slotState.currentModels) {
						if (!model)
							continue;

						float t;
						std::string meshName;
						if (RaycastNodeGeometries(model.get(), a_rayOrig, a_rayEnd, t, meshName)) {
							if (t < bestT) {
								bestT = t;
								bestSource = slotName;
								bestMesh = meshName;
							}
						}
					}
				}
			}
		}

		if (bestT < 1.0f) {
			result.hit = true;
			result.t = bestT;
			result.worldDistance = bestT * rayLen;
			result.source = bestSource;
			result.meshName = bestMesh;
		}
		return result;
	}

	bool DoesAttackHitWeaponFirst(
		RE::Actor* a_attacker,
		RE::Actor* a_target,
		const RE::NiPoint3& a_bodyHit,
		RayWeaponHit* a_outHit)
	{
		if (!a_target)
			return false;

		// 计算射线起点 - 用攻击者 eye vector (引擎自己用来确定攻击方向的)
		RE::NiPoint3 origin{ 0, 0, 0 };
		bool hasOrigin = false;

		if (a_attacker) {
			RE::NiPoint3 dir;
			a_attacker->GetEyeVector(origin, dir, true);
			hasOrigin = true;
		}

		if (!hasOrigin) {
			// 没有攻击者信息 → 无法构造射线，跳过武器命中判定
			REX::INFO("[IAD Combat] 无攻击者，跳过射线-武器检测");
			return false;
		}

		// 防御：射线段过短（攻击者与命中点重叠）
		float dx = a_bodyHit.x - origin.x;
		float dy = a_bodyHit.y - origin.y;
		float dz = a_bodyHit.z - origin.z;
		float segLen = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (segLen < 1.0f) {
			REX::INFO("[IAD Combat] 射线段过短 ({:.2f})，跳过", segLen);
			return false;
		}

		// 略微延长射线（5%）以覆盖武器恰好在身体表面外侧的边界情况
		RE::NiPoint3 end{
			a_bodyHit.x + dx * 0.05f,
			a_bodyHit.y + dy * 0.05f,
			a_bodyHit.z + dz * 0.05f
		};

		auto rayHit = RaycastActorWeapons(a_target, origin, end);

		if (rayHit.hit) {
			REX::INFO("[IAD Combat] ★ 射线击中武器 '{}' (mesh='{}', t={:.3f}, 距离={:.1f}/{:.1f})",
				rayHit.source, rayHit.meshName, rayHit.t,
				rayHit.worldDistance, segLen);
		}
		else {
			REX::INFO("[IAD Combat] 射线未击中武器 (射线长度={:.1f})", segLen);
		}

		if (a_outHit)
			*a_outHit = rayHit;
		return rayHit.hit;
	}
}
