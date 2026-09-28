#pragma once

#include "pch.h"
#include <imgui.h>

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

namespace IAD::UI {
	class IPreviewSceneAdapter;

	class UIWorldPreviewSession final {
	public:
		static UIWorldPreviewSession& GetSingleton()
		{
			static UIWorldPreviewSession instance;
			return instance;
		}

		// Called once per ImGui frame after editor/gizmo input has been routed.
		void Update(bool a_enabled, bool a_yieldToGizmo);
		// Called from HolsterManager's existing F4SE game-thread update loop.
		void ProcessGameThread();
		void End(std::string_view a_reason);

		[[nodiscard]] bool IsActive() const noexcept { return m_active.load(std::memory_order_acquire); }
		void DrawStatus() const;

	private:
		enum class Gesture : std::uint8_t {
			kNone,
			kRotate,
			kPan
		};

		struct ActorSnapshot {
			RE::Actor* actor = nullptr;
			RE::TESFormID formID = 0;
			RE::NiNode* root3D = nullptr;
			RE::NiPoint3 position{ 0.0f, 0.0f, 0.0f };
			RE::NiPoint3 angle{ 0.0f, 0.0f, 0.0f };
			RE::NiPoint3 cameraPosition{ 0.0f, 0.0f, 0.0f };
			std::uint64_t sceneGeneration = 0;
		};

		struct PendingTransform {
			bool valid = false;
			bool writeCameraPosition = false;
			bool writeAngle = false;
			RE::TESFormID formID = 0;
			RE::NiNode* root3D = nullptr;
			std::uint64_t sceneGeneration = 0;
			RE::NiPoint3 cameraPosition{ 0.0f, 0.0f, 0.0f };
			RE::NiPoint3 angle{ 0.0f, 0.0f, 0.0f };
		};

		struct PendingRestore {
			bool valid = false;
			bool restoreCamera = false;
			IPreviewSceneAdapter* adapter = nullptr;
			ActorSnapshot snapshot;
			std::string reason;
		};

		UIWorldPreviewSession() = default;
		UIWorldPreviewSession(const UIWorldPreviewSession&) = delete;
		UIWorldPreviewSession(UIWorldPreviewSession&&) = delete;
		UIWorldPreviewSession& operator=(const UIWorldPreviewSession&) = delete;
		UIWorldPreviewSession& operator=(UIWorldPreviewSession&&) = delete;

		bool Begin();
		bool IsSceneInvalid() const;
		void RouteInput(bool a_yieldToGizmo);
		void QueueTransform(bool a_writeCameraPosition, bool a_writeAngle);
		void ProcessPendingCameraEnter();
		void ProcessPendingTransform(PendingTransform a_pending);
		void ProcessPendingRestore(PendingRestore a_restore);
		void QueueRestore(ActorSnapshot a_snapshot, std::string a_reason, bool a_restoreCamera);
		void SetStatus(std::string a_status);
		[[nodiscard]] IPreviewSceneAdapter& Adapter() const;

		ActorSnapshot m_snapshot;
		RE::NiPoint3 m_previewCameraPosition{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 m_previewAngle{ 0.0f, 0.0f, 0.0f };
		Gesture m_gesture = Gesture::kNone;
		std::string m_status;

		std::atomic_bool m_active{ false };
		std::atomic_bool m_abortRequested{ false };
		std::atomic_bool m_cameraOwned{ false };
		std::atomic_bool m_firstTransformPublished{ false };
		std::atomic_bool m_firstTransformApplied{ false };
		IPreviewSceneAdapter* m_adapter = nullptr;
		std::mutex m_pendingMutex;
		bool m_pendingCameraEnter = false;
		PendingTransform m_pending;
		PendingRestore m_pendingRestore;
	};
}
