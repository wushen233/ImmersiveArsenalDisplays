#pragma once

#include "PreviewScene.h"

#include <RE/I/Interface3D.h>

#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace IAD::UI {
	// Fallout 4-specific detached-scene Adapter. All Interface3D and Ni scene
	// ownership changes are game-thread operations. ImGui only copies the camera
	// frame after the engine has rendered the offscreen target.
	class DetachedPreviewScene final : public IPreviewSceneAdapter {
	public:
		static DetachedPreviewScene& GetSingleton() noexcept
		{
			static DetachedPreviewScene instance;
			return instance;
		}

		void ProcessGameThread(bool a_enabled, const PreviewSceneSnapshot& a_snapshot);
		void RetireGameThread(const char* a_reason);

		[[nodiscard]] bool IsReady() const noexcept { return m_ready.load(std::memory_order_acquire); }
		[[nodiscard]] bool IsOwned() const noexcept { return m_rendererOwned.load(std::memory_order_acquire); }

		bool CaptureFrame(
			const PreviewSceneSnapshot& a_snapshot,
			const ImVec2& a_viewportMin,
			const ImVec2& a_viewportMax) override;
		RE::NiPoint3 CaptureCurrentWorldPosition(RE::Actor* a_actor) const override;
		bool EnterPreview(const RE::NiPoint3& a_cameraPosition, bool& a_cameraOwned) const override;
		bool ApplyPreview(const RE::NiPoint3& a_cameraPosition) const override;
		void RestorePreview(bool a_cameraOwned) const override;
		bool WorldToScreen(const RE::NiPoint3& a_worldPosition, ImVec2& a_screenPosition) const noexcept override;
		bool GetBasis(PreviewCameraBasis& a_basis) const noexcept override;
		[[nodiscard]] const char* GetName() const noexcept override { return "detached-interface3d"; }

	private:
		using Matrix = std::array<std::array<float, 4>, 4>;

		struct Frame {
			Matrix worldToCamera{};
			RE::NiTransform previewTransform = RE::NiTransform::IDENTITY;
			RE::NiPoint3 sourcePosition{ 0.0f, 0.0f, 0.0f };
			PreviewCameraBasis basis;
			ActorDisplayIdentity identity;
			ImVec2 viewportMin{ 0.0f, 0.0f };
			ImVec2 viewportMax{ 0.0f, 0.0f };
		};

		DetachedPreviewScene() = default;
		DetachedPreviewScene(const DetachedPreviewScene&) = delete;
		DetachedPreviewScene(DetachedPreviewScene&&) = delete;
		DetachedPreviewScene& operator=(const DetachedPreviewScene&) = delete;
		DetachedPreviewScene& operator=(DetachedPreviewScene&&) = delete;

		bool EnsureRendererGameThread();
		bool EnsureDisplayRootGameThread();
		bool EnsureCloneGameThread(RE::Actor& a_actor, RE::NiAVObject& a_source, const PreviewSceneSnapshot& a_snapshot);
		void SynchronizeClonePoseGameThread();
		void UpdateCloneFrameGameThread(RE::Actor& a_actor);
		void ReleaseRendererGameThread();

		mutable std::mutex m_frameMutex;
		mutable std::shared_mutex m_sceneMutex;
		Frame m_frame;
		RE::Interface3D::Renderer* m_renderer{ nullptr };
		RE::NiPointer<RE::NiAVObject> m_previewRoot;
		RE::NiPointer<RE::NiAVObject> m_displayRoot;
		RE::NiAVObject* m_sourceRoot{ nullptr };
		ActorDisplayIdentity m_identity;
		RE::NiPoint3 m_sourceActorPosition{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 m_cameraPosition{ 0.0f, 0.0f, 0.0f };
		float m_sourceYaw{ 0.0f };
		std::uint32_t m_targetWidth{ 0 };
		std::uint32_t m_targetHeight{ 0 };
		std::atomic_bool m_ready{ false };
		std::atomic_bool m_rendererOwned{ false };
	};
}
