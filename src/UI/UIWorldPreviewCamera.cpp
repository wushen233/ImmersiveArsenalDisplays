#include "pch.h"

#include "UIWorldPreviewCamera.h"

#include <RE/N/NiCamera.h>
#include <RE/N/NiNode.h>
#include <RE/F/FreeCameraState.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>

#include <cmath>
#include <cstring>

namespace IAD::UI {
	namespace {
		float Length(const RE::NiPoint3& a_value)
		{
			return std::sqrt(a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z);
		}

		RE::NiPoint3 Normalize(const RE::NiPoint3& a_value, const RE::NiPoint3& a_fallback)
		{
			const float length = Length(a_value);
			if (length <= 0.0001f) return a_fallback;
			return { a_value.x / length, a_value.y / length, a_value.z / length };
		}
	}

	RE::NiCamera* UIWorldPreviewCamera::FindCamera(RE::NiAVObject* a_object)
	{
		if (!a_object) return nullptr;
		const auto* rtti = a_object->GetRTTI();
		if (rtti && rtti->name && std::strstr(rtti->name, "NiCamera")) {
			return reinterpret_cast<RE::NiCamera*>(a_object);
		}

		auto* node = a_object->IsNode();
		if (!node) return nullptr;
		for (auto& child : node->children) {
			if (auto* camera = FindCamera(child.get())) return camera;
		}
		return nullptr;
	}

	bool UIWorldPreviewCamera::CaptureFrame(
		const PreviewSceneSnapshot& a_snapshot,
		const ImVec2& a_viewportMin,
		const ImVec2& a_viewportMax)
	{
		m_valid = false;
		const float width = a_viewportMax.x - a_viewportMin.x;
		const float height = a_viewportMax.y - a_viewportMin.y;
		if (width <= 1.0f || height <= 1.0f) return false;

		auto* playerCamera = RE::PlayerCamera::GetSingleton();
		auto* niCamera = playerCamera && playerCamera->cameraRoot ? FindCamera(playerCamera->cameraRoot.get()) : nullptr;
		if (!niCamera) return false;

		for (std::size_t row = 0; row < 4; ++row) {
			for (std::size_t column = 0; column < 4; ++column) {
				m_frame.worldToCamera[row][column] = niCamera->worldToCam[row][column];
			}
		}

		const auto& rotation = niCamera->world.rotate;
		m_frame.basis.forward = Normalize({ rotation.entry[0][0], rotation.entry[0][1], rotation.entry[0][2] }, { 0.0f, 1.0f, 0.0f });
		m_frame.basis.up = Normalize({ rotation.entry[1][0], rotation.entry[1][1], rotation.entry[1][2] }, { 0.0f, 0.0f, 1.0f });
		m_frame.basis.right = Normalize({ rotation.entry[2][0], rotation.entry[2][1], rotation.entry[2][2] }, { 1.0f, 0.0f, 0.0f });
		m_frame.identity = a_snapshot.identity;
		m_frame.viewportMin = a_viewportMin;
		m_frame.viewportMax = a_viewportMax;
		m_valid = true;
		return true;
	}

	RE::NiPoint3 UIWorldPreviewCamera::CaptureCurrentWorldPosition(RE::Actor* a_actor) const
	{
		auto* playerCamera = RE::PlayerCamera::GetSingleton();
		if (playerCamera && playerCamera->cameraRoot) {
			if (auto* camera = FindCamera(playerCamera->cameraRoot.get())) {
				return camera->world.translate;
			}
		}

		// Keep the game API as a fallback for frames where the camera scene graph
		// has not been published yet. Start from zero because the API may add the
		// player position to a camera-relative result.
		RE::NiPoint3 fallback{};
		if (playerCamera && playerCamera->GetCameraPosition(fallback, true)) return fallback;
		return a_actor ? a_actor->GetPosition() : RE::NiPoint3{};
	}

	bool UIWorldPreviewCamera::EnterPreview(const RE::NiPoint3& a_cameraPosition, bool& a_cameraOwned) const
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera) return false;

		auto freeState = camera->GetState<RE::FreeCameraState>();
		auto currentState = camera->GetCameraCurrentState();
		const bool alreadyFree = freeState && currentState &&
			currentState.get() == static_cast<RE::TESCameraState*>(freeState.get());
		if (!alreadyFree) {
			camera->ToggleFreeCameraMode(false);
			freeState = camera->GetState<RE::FreeCameraState>();
		}
		if (!freeState) return false;

		freeState->translation = a_cameraPosition;
		camera->bufferedCameraPos = a_cameraPosition;
		a_cameraOwned = !alreadyFree || a_cameraOwned;
		REX::INFO("[IAD Preview] camera adapter entered live preview ({})", alreadyFree ? "existing free camera" : "IAD free camera");
		return true;
	}

	bool UIWorldPreviewCamera::ApplyPreview(const RE::NiPoint3& a_cameraPosition) const
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto freeState = camera ? camera->GetState<RE::FreeCameraState>() : nullptr;
		auto currentState = camera ? camera->GetCameraCurrentState() : nullptr;
		if (!camera || !freeState || !currentState ||
			currentState.get() != static_cast<RE::TESCameraState*>(freeState.get())) {
			return false;
		}

		freeState->translation = a_cameraPosition;
		camera->bufferedCameraPos = a_cameraPosition;
		return true;
	}

	void UIWorldPreviewCamera::RestorePreview(bool a_cameraOwned) const
	{
		if (!a_cameraOwned) return;
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera) return;

		auto freeState = camera->GetState<RE::FreeCameraState>();
		auto currentState = camera->GetCameraCurrentState();
		if (freeState && currentState &&
			currentState.get() == static_cast<RE::TESCameraState*>(freeState.get())) {
			camera->ToggleFreeCameraMode(false);
		}
	}

	bool UIWorldPreviewCamera::WorldToScreen(const RE::NiPoint3& a_worldPosition, ImVec2& a_screenPosition) const noexcept
	{
		if (!m_valid) return false;

		const auto& matrix = m_frame.worldToCamera;
		const float w = matrix[3][0] * a_worldPosition.x +
			matrix[3][1] * a_worldPosition.y +
			matrix[3][2] * a_worldPosition.z + matrix[3][3];
		if (w < 0.001f || !std::isfinite(w)) return false;

		const float inverseW = 1.0f / w;
		const float x = (matrix[0][0] * a_worldPosition.x +
			matrix[0][1] * a_worldPosition.y +
			matrix[0][2] * a_worldPosition.z + matrix[0][3]) * inverseW;
		const float y = (matrix[1][0] * a_worldPosition.x +
			matrix[1][1] * a_worldPosition.y +
			matrix[1][2] * a_worldPosition.z + matrix[1][3]) * inverseW;
		if (!std::isfinite(x) || !std::isfinite(y)) return false;

		const float width = m_frame.viewportMax.x - m_frame.viewportMin.x;
		const float height = m_frame.viewportMax.y - m_frame.viewportMin.y;
		a_screenPosition.x = m_frame.viewportMin.x + ((x + 1.0f) * 0.5f) * width;
		a_screenPosition.y = m_frame.viewportMin.y + ((1.0f - y) * 0.5f) * height;
		return true;
	}

	bool UIWorldPreviewCamera::GetBasis(PreviewCameraBasis& a_basis) const noexcept
	{
		if (!m_valid) return false;
		a_basis = m_frame.basis;
		return true;
	}
}
