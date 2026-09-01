#pragma once
#include "pch.h"
#include "Data/ConfigManager.h"

// 👇========== 🌟 只更新头文件路径，绝对不碰逻辑 ==========👇
#include <RE/N/NiPoint3.h>
#include <RE/N/NiMatrix3.h>
#include <RE/N/NiTransform.h>
// 👆=======================================================👆

namespace IAD
{
	// 🌟 结构升级：彻底分离平移锚点与旋转探针
	struct DebugBox {
		bool drawBox = false;
		RE::NiPoint3 corners[8];

		bool drawSphere = false;
		RE::NiPoint3 sphereCenter;
		float sphereRadius = 0.0f;

		bool drawPendulum = false;
		RE::NiPoint3 center;      // CME 挂载点基座
		RE::NiPoint3 virtPos;     // 物理锚点 (线性位移后的位置)
		RE::NiPoint3 weaponTip;   // 探针末端 (旋转摇摆后的位置)
		RE::NiPoint3 axisX, axisY, axisZ;

		// 新增：非对称角度约束可视化参数
		bool drawAngular = false;
		float minPitch = 0.0f; float maxPitch = 0.0f;
		float minYaw = 0.0f; float maxYaw = 0.0f;
		float minRoll = 0.0f; float maxRoll = 0.0f; // 扭转极限
		float visualProbeLength = 40.0f;
	};

	class SimComponent
	{
	public:
		SimComponent(const RE::NiTransform& a_initialTransform, const PhysicsValues& a_conf);
		~SimComponent() = default;

		void ReadTransforms(const RE::NiTransform& a_parentWorld, float a_step);
		void UpdateMotion(float a_step);
		void UpdateConfig(const PhysicsValues& a_conf);
		void Reset(const RE::NiTransform& a_parentWorld);

		const RE::NiTransform& GetObjectLocalTransform() const { return m_objectLocalTransform; }

		void SetInitialTransform(const RE::NiTransform& a_initialTransform) { m_initialTransform = a_initialTransform; }
		bool GetDebugData(DebugBox& a_box) const;

	private:
		RE::NiPoint3 CalculateTarget() const;
		void ProcessConfig();
		void LimitVelocity();
		void ConstrainMotionBox(float a_step);
		void ConstrainMotionSphere(float a_step);

		PhysicsValues m_conf;

		RE::NiTransform m_initialTransform;
		RE::NiTransform m_objectLocalTransform;
		RE::NiTransform m_parentWorldTransform;

		RE::NiMatrix3 m_parentRot;

		RE::NiPoint3 m_oldParentPos{ 0,0,0 };
		RE::NiPoint3 m_oldWorldPos{ 0,0,0 };
		RE::NiPoint3 m_virtld{ 0,0,0 };
		RE::NiPoint3 m_velocity{ 0,0,0 };
		RE::NiPoint3 m_parentVelocity{ 0,0,0 };

		RE::NiPoint3 m_f4_maxOffsetP{ 0,0,0 };
		RE::NiPoint3 m_f4_maxOffsetN{ 0,0,0 };
		RE::NiPoint3 m_f4_sphereOffset{ 0,0,0 };

		float m_maxVelocity2;
		RE::NiPoint3 m_gravityForce;
		bool m_resistanceOn;
		bool m_hasSpringSlack;
	};
}