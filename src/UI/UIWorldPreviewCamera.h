#pragma once

#include "PreviewScene.h"

#include <array>

namespace IAD::UI {
	// Render-thread camera seam. The UI consumes a copied frame instead of
	// retaining a live PlayerCamera/NiCamera pointer while drawing markers.
	class UIWorldPreviewCamera final : public IPreviewSceneAdapter {
	public:
		static UIWorldPreviewCamera& GetSingleton()
		{
			static UIWorldPreviewCamera instance;
			return instance;
		}

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
		[[nodiscard]] const char* GetName() const noexcept override { return "live-camera"; }
		bool IsValid() const noexcept { return m_valid; }

	private:
		using Matrix = std::array<std::array<float, 4>, 4>;

		struct Frame {
			Matrix worldToCamera{};
			PreviewCameraBasis basis;
			ActorDisplayIdentity identity;
			ImVec2 viewportMin{ 0.0f, 0.0f };
			ImVec2 viewportMax{ 0.0f, 0.0f };
		};

		static RE::NiCamera* FindCamera(RE::NiAVObject* a_object);

		Frame m_frame;
		bool m_valid = false;
	};
}
