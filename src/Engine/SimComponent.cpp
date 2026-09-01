#include "pch.h"
#include "SimComponent.h"
#include <algorithm>
#include <cmath>

#undef min
#undef max

namespace IAD
{
	namespace F4Math {
		// 🌟 正确的本地转世界 (行向量相乘 v * M，因为轴在行里)
		static RE::NiPoint3 LocalToWorld(const RE::NiMatrix3& m, const RE::NiPoint3& v) {
			return {
				v.x * m.entry[0][0] + v.y * m.entry[1][0] + v.z * m.entry[2][0],
				v.x * m.entry[0][1] + v.y * m.entry[1][1] + v.z * m.entry[2][1],
				v.x * m.entry[0][2] + v.y * m.entry[1][2] + v.z * m.entry[2][2]
			};
		}

		// 🌟 正确的世界转本地 (乘以逆矩阵，即提取列)
		static RE::NiPoint3 WorldToLocal(const RE::NiMatrix3& m, const RE::NiPoint3& v) {
			return {
				m.entry[0][0] * v.x + m.entry[0][1] * v.y + m.entry[0][2] * v.z,
				m.entry[1][0] * v.x + m.entry[1][1] * v.y + m.entry[1][2] * v.z,
				m.entry[2][0] * v.x + m.entry[2][1] * v.y + m.entry[2][2] * v.z
			};
		}

		static RE::NiMatrix3 MatMulMat(const RE::NiMatrix3& a, const RE::NiMatrix3& b) {
			RE::NiMatrix3 res;
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					res.entry[r][c] = a.entry[r][0] * b.entry[0][c] + a.entry[r][1] * b.entry[1][c] + a.entry[r][2] * b.entry[2][c];
				}
			}
			return res;
		}
	}

	inline RE::NiPoint3 SafeNormalize(const RE::NiPoint3& v) {
		float l = v.Length();
		return l > 0.000001f ? v / l : RE::NiPoint3(0, 0, 0);
	}

	static RE::NiPoint3 GetRestLocal(const PhysicsValues& conf, const RE::NiMatrix3& parentRot) {
		float a_stiff = conf.stiffness;
		float b_stiff = conf.stiffness2;
		float c_grav = conf.gravityBias * conf.mass;
		float sagDist = 0.0f;
		if (b_stiff > 0.0001f) {
			float delta = a_stiff * a_stiff + 4.0f * b_stiff * c_grav;
			if (delta > 0.0f) sagDist = (-a_stiff + std::sqrt(delta)) / (2.0f * b_stiff);
		}
		else if (a_stiff > 0.0001f) {
			sagDist = c_grav / a_stiff;
		}
		return F4Math::WorldToLocal(parentRot, RE::NiPoint3(0.0f, 0.0f, -sagDist));
	}

	SimComponent::SimComponent(const RE::NiTransform& a_initialTransform, const PhysicsValues& a_conf)
		: m_initialTransform(a_initialTransform), m_conf(a_conf)
	{
		m_objectLocalTransform = m_initialTransform;
		ProcessConfig();
	}

	void SimComponent::ProcessConfig() {
		m_conf.mass = std::max(0.001f, std::min(m_conf.mass, 10000.0f));
		m_conf.maxVelocity = std::max(1.0f, std::min(m_conf.maxVelocity, 50000.0f));
		m_maxVelocity2 = m_conf.maxVelocity * m_conf.maxVelocity;
		m_gravityForce = RE::NiPoint3(0, 0, -(m_conf.gravityBias * m_conf.mass));
		m_resistanceOn = m_conf.resistance > 0.0f;
		m_hasSpringSlack = m_conf.springSlackOffset > 0.0f || m_conf.springSlackMag > 0.0f;

		m_f4_maxOffsetP.x = std::max(m_conf.maxOffsetP.x, m_conf.maxOffsetN.x);
		m_f4_maxOffsetN.x = std::min(m_conf.maxOffsetP.x, m_conf.maxOffsetN.x);
		m_f4_maxOffsetP.y = std::max(m_conf.maxOffsetP.y, m_conf.maxOffsetN.y);
		m_f4_maxOffsetN.y = std::min(m_conf.maxOffsetP.y, m_conf.maxOffsetN.y);
		m_f4_maxOffsetP.z = std::max(m_conf.maxOffsetP.z, m_conf.maxOffsetN.z);
		m_f4_maxOffsetN.z = std::min(m_conf.maxOffsetP.z, m_conf.maxOffsetN.z);

		if (m_conf.minPitch > m_conf.maxPitch) std::swap(m_conf.minPitch, m_conf.maxPitch);
		if (m_conf.minYaw > m_conf.maxYaw) std::swap(m_conf.minYaw, m_conf.maxYaw);
		if (m_conf.minRoll > m_conf.maxRoll) std::swap(m_conf.minRoll, m_conf.maxRoll);

		m_conf.visualProbeLength = std::max(1.0f, m_conf.visualProbeLength);

		m_f4_sphereOffset = m_conf.maxOffsetSphereOffset;
	}

	void SimComponent::UpdateConfig(const PhysicsValues& a_conf) {
		m_conf = a_conf;
		ProcessConfig();
	}

	void SimComponent::Reset(const RE::NiTransform& a_parentWorld) {
		m_objectLocalTransform = m_initialTransform;
		m_parentWorldTransform = a_parentWorld;
		m_parentRot = a_parentWorld.rotate;
		m_oldParentPos = a_parentWorld.translate;
		m_oldWorldPos = CalculateTarget();
		m_virtld = { 0,0,0 };
		m_velocity = { 0,0,0 };
		m_parentVelocity = { 0,0,0 };
	}

	RE::NiPoint3 SimComponent::CalculateTarget() const {
		return m_parentWorldTransform.translate + F4Math::LocalToWorld(m_parentRot, m_conf.cogOffset);
	}

	void SimComponent::ReadTransforms(const RE::NiTransform& a_parentWorld, float a_step) {
		m_parentWorldTransform = a_parentWorld;
		m_parentRot = a_parentWorld.rotate;

		RE::NiPoint3 pos = a_parentWorld.translate;
		m_parentVelocity = (pos - m_oldParentPos) / a_step;
		m_oldParentPos = pos;
	}

	void SimComponent::LimitVelocity() {
		float l2 = m_velocity.SqrLength();
		if (l2 > m_maxVelocity2) {
			m_velocity = (m_velocity / std::sqrt(l2)) * m_conf.maxVelocity;
		}
	}

	void SimComponent::UpdateMotion(float a_step) {
		RE::NiPoint3 target = CalculateTarget();
		RE::NiPoint3 diff = target - m_oldWorldPos;

		if (diff.SqrLength() > 1024.0f * 1024.0f) {
			Reset(m_parentWorldTransform);
			return;
		}

		RE::NiPoint3 absDiff = { std::abs(diff.x), std::abs(diff.y), std::abs(diff.z) };
		RE::NiPoint3 force = diff * m_conf.stiffness + (diff * absDiff) * m_conf.stiffness2;

		if (m_hasSpringSlack) {
			float vLen = m_virtld.Length();
			float mult = 1.0f;
			float totalSlack = m_conf.springSlackOffset + m_conf.springSlackMag;
			if (vLen <= m_conf.springSlackOffset) mult = 0.0f;
			else if (vLen >= totalSlack) mult = 1.0f;
			else mult = (vLen - m_conf.springSlackOffset) / m_conf.springSlackMag;
			force *= (mult * mult);
		}

		force += m_gravityForce;

		float res = m_resistanceOn ? (1.0f - 1.0f / (m_velocity.Length() * 0.0075f + 1.0f)) * m_conf.resistance + 1.0f : 1.0f;
		m_velocity -= m_velocity * (m_conf.damping * res * a_step);
		m_velocity += (force / m_conf.mass) * a_step;

		LimitVelocity();

		RE::NiPoint3 worldDiff = (m_oldWorldPos + (m_velocity * a_step)) - target;
		m_virtld = F4Math::WorldToLocal(m_parentRot, worldDiff);

		if (m_conf.enableSphereConstraint) ConstrainMotionSphere(a_step);
		if (m_conf.enableBoxConstraint) ConstrainMotionBox(a_step);

		m_oldWorldPos = F4Math::LocalToWorld(m_parentRot, m_virtld) + target;

		RE::NiPoint3 restLocal = GetRestLocal(m_conf, m_parentRot);
		RE::NiPoint3 pureSway = m_virtld - restLocal;

		RE::NiPoint3 ld = { pureSway.x * m_conf.linear.x, pureSway.y * m_conf.linear.y, pureSway.z * m_conf.linear.z };
		m_objectLocalTransform.translate = m_initialTransform.translate + ld;

		RE::NiPoint3 rotVal;
		rotVal.x = -pureSway.y * m_conf.rotational.x;
		rotVal.y = -pureSway.x * m_conf.rotational.y;
		rotVal.z = pureSway.x * m_conf.rotational.z;

		if (m_conf.enableAngularConstraint) {
			auto SoftClamp = [](float val, float minV, float maxV) {
				const float maxDeform = 12.0f;
				const float stiffness = 10.0f;
				if (val > maxV) {
					float over = val - maxV;
					return maxV + maxDeform * (1.0f - std::exp(-over / stiffness));
				}
				else if (val < minV) {
					float over = minV - val;
					return minV - maxDeform * (1.0f - std::exp(-over / stiffness));
				}
				return val;
				};

			rotVal.x = SoftClamp(rotVal.x, m_conf.minPitch, m_conf.maxPitch);
			rotVal.y = SoftClamp(rotVal.y, m_conf.minYaw, m_conf.maxYaw);
			rotVal.z = SoftClamp(rotVal.z, m_conf.minRoll, m_conf.maxRoll);
		}

		RE::NiPoint3 mathAxis;
		mathAxis.x = rotVal.x;
		mathAxis.y = -rotVal.y;
		mathAxis.z = -rotVal.z;

		float l2 = mathAxis.SqrLength();
		if (l2 > 0.000001f) {
			float l = std::sqrt(l2);
			mathAxis = mathAxis / l;

			float angle = l * (3.14159265f / 180.0f);
			float s = -std::sin(angle);
			float c = std::cos(angle), t = 1.0f - c;
			float x = mathAxis.x, y = mathAxis.y, z = mathAxis.z;
			RE::NiMatrix3 rotMat;

			rotMat.entry[0][0] = t * x * x + c;      rotMat.entry[0][1] = t * x * y - z * s; rotMat.entry[0][2] = t * x * z + y * s;
			rotMat.entry[1][0] = t * x * y + z * s;  rotMat.entry[1][1] = t * y * y + c;     rotMat.entry[1][2] = t * y * z - x * s;
			rotMat.entry[2][0] = t * x * z - y * s;  rotMat.entry[2][1] = t * y * z + x * s; rotMat.entry[2][2] = t * z * z + c;

			m_objectLocalTransform.rotate = F4Math::MatMulMat(m_initialTransform.rotate, rotMat);
		}
		else {
			m_objectLocalTransform.rotate = m_initialTransform.rotate;
		}
	}

	void SimComponent::ConstrainMotionBox(float a_step) {
		RE::NiPoint3 restLocal = GetRestLocal(m_conf, m_parentRot);
		RE::NiPoint3 pureSway = m_virtld - restLocal;

		RE::NiPoint3 depth{ 0,0,0 };
		bool skip = true;

		if (pureSway.x > m_f4_maxOffsetP.x) { depth.x = pureSway.x - m_f4_maxOffsetP.x; skip = false; }
		else if (pureSway.x < m_f4_maxOffsetN.x) { depth.x = pureSway.x - m_f4_maxOffsetN.x; skip = false; }

		if (pureSway.y > m_f4_maxOffsetP.y) { depth.y = pureSway.y - m_f4_maxOffsetP.y; skip = false; }
		else if (pureSway.y < m_f4_maxOffsetN.y) { depth.y = pureSway.y - m_f4_maxOffsetN.y; skip = false; }

		if (pureSway.z > m_f4_maxOffsetP.z) { depth.z = pureSway.z - m_f4_maxOffsetP.z; skip = false; }
		else if (pureSway.z < m_f4_maxOffsetN.z) { depth.z = pureSway.z - m_f4_maxOffsetN.z; skip = false; }

		if (skip) return;

		RE::NiPoint3 n = SafeNormalize(F4Math::LocalToWorld(m_parentRot, depth));
		RE::NiPoint3 deltav = m_velocity - m_parentVelocity;
		float vdotn = deltav.Dot(n);
		float impulse = vdotn;
		float mag = depth.Length();

		float magThreshold = a_step * 60.0f;
		if (mag > magThreshold) {
			impulse += a_step * (m_conf.boxParams.penBiasFactor * 2880.0f) * std::max(0.0f, std::min(mag - magThreshold, m_conf.boxParams.penBiasDepthLimit));
		}
		if (impulse <= 0.0f) return;

		m_velocity -= (deltav - n * vdotn) * m_conf.maxOffsetBoxFriction;
		float J = (1.0f + m_conf.boxParams.restitutionCoefficient) * impulse;
		m_velocity -= n * (J * m_conf.boxParams.velocityResponseScale);

		RE::NiPoint3 newWorldDiff = (m_oldWorldPos + (m_velocity * a_step)) - CalculateTarget();
		m_virtld = F4Math::WorldToLocal(m_parentRot, newWorldDiff);
	}

	void SimComponent::ConstrainMotionSphere(float a_step) {
		RE::NiPoint3 restLocal = GetRestLocal(m_conf, m_parentRot);
		RE::NiPoint3 pureSway = m_virtld - restLocal;

		RE::NiPoint3 diff = pureSway - m_f4_sphereOffset;
		float difflen = diff.Length();
		if (difflen <= m_conf.maxOffsetSphereRadius) return;

		RE::NiPoint3 n = SafeNormalize(F4Math::LocalToWorld(m_parentRot, diff));
		RE::NiPoint3 deltav = m_velocity - m_parentVelocity;
		float vdotn = deltav.Dot(n);
		float impulse = vdotn;
		float mag = difflen - m_conf.maxOffsetSphereRadius;

		float magThreshold = a_step * 60.0f;
		if (mag > magThreshold) {
			impulse += a_step * (m_conf.sphereParams.penBiasFactor * 2880.0f) * std::max(0.0f, std::min(mag - magThreshold, m_conf.sphereParams.penBiasDepthLimit));
		}
		if (impulse <= 0.0f) return;

		m_velocity -= (deltav - n * vdotn) * m_conf.maxOffsetSphereFriction;
		float J = (1.0f + m_conf.sphereParams.restitutionCoefficient) * impulse;
		m_velocity -= n * (J * m_conf.sphereParams.velocityResponseScale);

		RE::NiPoint3 newWorldDiff = (m_oldWorldPos + (m_velocity * a_step)) - CalculateTarget();
		m_virtld = F4Math::WorldToLocal(m_parentRot, newWorldDiff);
	}

	bool SimComponent::GetDebugData(DebugBox& a_box) const {
		RE::NiPoint3 baseWorldPos = m_parentWorldTransform.translate + F4Math::LocalToWorld(m_parentRot, m_initialTransform.translate);
		a_box.center = baseWorldPos;

		a_box.drawAngular = m_conf.enableAngularConstraint;
		a_box.minPitch = m_conf.minPitch; a_box.maxPitch = m_conf.maxPitch;
		a_box.minYaw = m_conf.minYaw;     a_box.maxYaw = m_conf.maxYaw;
		a_box.minRoll = m_conf.minRoll;   a_box.maxRoll = m_conf.maxRoll;
		a_box.visualProbeLength = m_conf.visualProbeLength;

		a_box.axisX = F4Math::LocalToWorld(m_parentRot, { 1.0f, 0.0f, 0.0f });
		a_box.axisY = F4Math::LocalToWorld(m_parentRot, { 0.0f, 1.0f, 0.0f });
		a_box.axisZ = F4Math::LocalToWorld(m_parentRot, { 0.0f, 0.0f, 1.0f });

		a_box.virtPos = m_parentWorldTransform.translate + F4Math::LocalToWorld(m_parentRot, m_objectLocalTransform.translate);

		RE::NiPoint3 stick = { 0.0f, 0.0f, -m_conf.visualProbeLength };
		RE::NiPoint3 localRotatedStick = F4Math::LocalToWorld(m_objectLocalTransform.rotate, stick);
		a_box.weaponTip = m_parentWorldTransform.translate + F4Math::LocalToWorld(m_parentRot, m_objectLocalTransform.translate + localRotatedStick);

		if (a_box.drawSphere) {
			a_box.sphereCenter = baseWorldPos + F4Math::LocalToWorld(m_parentRot, m_f4_sphereOffset);
			a_box.sphereRadius = m_conf.maxOffsetSphereRadius;
		}

		if (a_box.drawBox) {
			RE::NiPoint3 vMaxP = { m_f4_maxOffsetP.x * m_conf.linear.x, m_f4_maxOffsetP.y * m_conf.linear.y, m_f4_maxOffsetP.z * m_conf.linear.z };
			RE::NiPoint3 vMaxN = { m_f4_maxOffsetN.x * m_conf.linear.x, m_f4_maxOffsetN.y * m_conf.linear.y, m_f4_maxOffsetN.z * m_conf.linear.z };

			RE::NiPoint3 offsets[8] = {
				{vMaxP.x, vMaxP.y, vMaxP.z},
				{vMaxN.x, vMaxP.y, vMaxP.z},
				{vMaxP.x, vMaxN.y, vMaxP.z},
				{vMaxN.x, vMaxN.y, vMaxP.z},
				{vMaxP.x, vMaxP.y, vMaxN.z},
				{vMaxN.x, vMaxP.y, vMaxN.z},
				{vMaxP.x, vMaxN.y, vMaxN.z},
				{vMaxN.x, vMaxN.y, vMaxN.z}
			};
			for (int i = 0; i < 8; ++i) {
				a_box.corners[i] = baseWorldPos + F4Math::LocalToWorld(m_parentRot, offsets[i]);
			}
		}
		return true;
	}
}