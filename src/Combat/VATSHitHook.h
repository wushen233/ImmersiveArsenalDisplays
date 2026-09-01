#pragma once
#include "pch.h"

namespace IAD::Combat
{
	/// VATS 武器命中处理类
	/// 
	/// 监听 TESHitEvent，在 VATS 命中时检测命中点是否落在 IAD 武器模型上。
	/// 如果是，则触发缴械逻辑（DisarmHandler）。
	///
	/// 工作流程:
	/// 1. 注册为 TESHitEvent 的事件接收器
	/// 2. 收到命中事件时，检查是否有 VATSCommand（确认是 VATS 攻击）
	/// 3. 获取命中点的世界坐标 (HitData.impactData.location)
	/// 4. 使用 WeaponHitSystem 检查是否靠近武器模型
	/// 5. 如果是 → 调用 DisarmHandler::DisarmActor() 执行缴械
	class VATSHitHandler :
		public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		static VATSHitHandler* GetSingleton()
		{
			static VATSHitHandler singleton;
			return &singleton;
		}

		/// 注册到 TESHitEvent 的事件源
		void Register();

		/// 事件处理入口 - 由引擎在每次命中时调用
		virtual RE::BSEventNotifyControl ProcessEvent(
			const RE::TESHitEvent& a_event,
			RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

	private:
		VATSHitHandler() = default;
		VATSHitHandler(const VATSHitHandler&) = delete;
		VATSHitHandler& operator=(const VATSHitHandler&) = delete;
	};
}
