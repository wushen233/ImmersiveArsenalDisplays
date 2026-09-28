#pragma once

#include "System/ActorDisplayContext.h"

#include <imgui.h>

#include <atomic>

namespace IAD::UI {
	class DetachedPreviewScene;

    struct PreviewCameraBasis {
        RE::NiPoint3 forward{ 0.0f, 1.0f, 0.0f };
        RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
        RE::NiPoint3 right{ 1.0f, 0.0f, 0.0f };
    };

    struct PreviewSceneSnapshot {
        ActorDisplayIdentity identity;
        RE::NiPoint3 actorPosition{ 0.0f, 0.0f, 0.0f };
    };

    class IPreviewSceneAdapter {
    public:
        virtual ~IPreviewSceneAdapter() = default;

        virtual bool CaptureFrame(
            const PreviewSceneSnapshot& a_snapshot,
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax) = 0;
        virtual RE::NiPoint3 CaptureCurrentWorldPosition(RE::Actor* a_actor) const = 0;
        virtual bool EnterPreview(const RE::NiPoint3& a_cameraPosition, bool& a_cameraOwned) const = 0;
        virtual bool ApplyPreview(const RE::NiPoint3& a_cameraPosition) const = 0;
        virtual void RestorePreview(bool a_cameraOwned) const = 0;
        virtual bool WorldToScreen(const RE::NiPoint3& a_worldPosition, ImVec2& a_screenPosition) const noexcept = 0;
        virtual bool GetBasis(PreviewCameraBasis& a_basis) const noexcept = 0;
        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
    };

    class PreviewScene final {
    public:
        static PreviewScene& GetSingleton() noexcept;

        [[nodiscard]] IPreviewSceneAdapter& GetActive() noexcept;
        [[nodiscard]] const IPreviewSceneAdapter& GetActive() const noexcept;
		[[nodiscard]] DetachedPreviewScene& GetDetached() noexcept;
		[[nodiscard]] const DetachedPreviewScene& GetDetached() const noexcept;
		void ProcessGameThread(bool a_enabled, const PreviewSceneSnapshot& a_snapshot);
		void SetDetachedRequested(bool a_requested) noexcept;
		[[nodiscard]] bool IsDetachedRequested() const noexcept;

    private:
        PreviewScene() = default;
        PreviewScene(const PreviewScene&) = delete;
        PreviewScene(PreviewScene&&) = delete;
        PreviewScene& operator=(const PreviewScene&) = delete;
        PreviewScene& operator=(PreviewScene&&) = delete;

		std::atomic_bool m_detachedRequested{ false };
	};
}
