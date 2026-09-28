#include "pch.h"
#include "UILocalization.h"

#include "UIWorldPreviewSession.h"

#include "ModelManager.h"
#include "Engine/NodeManager.h"
#include "System/HolsterManager.h"
#include "PreviewScene.h"
#include "DetachedPreviewScene.h"

#include <RE/A/Actor.h>
#include <RE/P/PlayerCharacter.h>

#include <algorithm>
#include <cmath>

namespace IAD::UI {
	namespace {
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kRotateSensitivity = 0.005f;
		constexpr float kPanSensitivity = 0.08f;
		constexpr float kZoomSensitivity = 12.0f;
		constexpr float kMinZoomDistance = 32.0f;
		constexpr float kMaxZoomDistance = 768.0f;

		RE::NiPoint3 Add(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
		{
			return { a_lhs.x + a_rhs.x, a_lhs.y + a_rhs.y, a_lhs.z + a_rhs.z };
		}

		RE::NiPoint3 Scale(const RE::NiPoint3& a_value, float a_scale)
		{
			return { a_value.x * a_scale, a_value.y * a_scale, a_value.z * a_scale };
		}

		float Length(const RE::NiPoint3& a_value)
		{
			return std::sqrt(a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z);
		}

		RE::NiPoint3 Normalize(const RE::NiPoint3& a_value)
		{
			const float length = Length(a_value);
			if (length <= 0.001f) return { 0.0f, 0.0f, 0.0f };
			return Scale(a_value, 1.0f / length);
		}

		bool IsMouseOverImGuiWindow()
		{
			return ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
		}

		float WrapAngle(float a_angle)
		{
			while (a_angle > kPi) a_angle -= 2.0f * kPi;
			while (a_angle < -kPi) a_angle += 2.0f * kPi;
			return a_angle;
		}
	}

	void UIWorldPreviewSession::Update(bool a_enabled, bool a_yieldToGizmo)
	{
		if (!a_enabled) {
			if (IsActive()) End("preview disabled");
			return;
		}

		if (!IsActive()) {
			if (PreviewScene::GetSingleton().IsDetachedRequested() &&
				!PreviewScene::GetSingleton().GetDetached().IsReady()) {
				SetStatus(TextLiteral("正在创建独立角色预览场景..."));
				return;
			}
			Begin();
			return;
		}

		if (m_adapter && m_adapter != std::addressof(PreviewScene::GetSingleton().GetActive())) {
			End(TextLiteral("预览场景适配器已切换"));
			return;
		}

		if (m_abortRequested.exchange(false, std::memory_order_acq_rel) || IsSceneInvalid()) {
			End("scene invalidated");
			return;
		}

		RouteInput(a_yieldToGizmo);
	}

	bool UIWorldPreviewSession::Begin()
	{
		if (ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition()) {
			SetStatus(TextLiteral("等待游戏场景完成加载后启动角色预览。"));
			return false;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || player->IsDead(false) || player->IsDeleted() || player->IsDisabled()) {
			SetStatus(TextLiteral("当前没有可用的玩家角色场景。"));
			return false;
		}

		auto* root3D = player->Get3D(false) ? player->Get3D(false)->IsNode() : nullptr;
		if (!root3D) {
			SetStatus(TextLiteral("等待玩家 3D 骨骼就绪后启动角色预览。"));
			return false;
		}

		if (PreviewScene::GetSingleton().IsDetachedRequested() && !PreviewScene::GetSingleton().GetDetached().IsReady()) {
			SetStatus(TextLiteral("正在创建独立角色预览场景..."));
			return false;
		}

		m_adapter = std::addressof(PreviewScene::GetSingleton().GetActive());
		const RE::NiPoint3 cameraPosition = Adapter().CaptureCurrentWorldPosition(player);

		m_snapshot.actor = player;
		m_snapshot.formID = player->GetFormID();
		m_snapshot.root3D = root3D;
		m_snapshot.position = player->GetPosition();
		m_snapshot.angle = player->data.angle;
		m_snapshot.cameraPosition = cameraPosition;
		m_snapshot.sceneGeneration = ModelManager::GetSceneGeneration();
		m_previewCameraPosition = m_snapshot.cameraPosition;
		m_previewAngle = m_snapshot.angle;
		m_gesture = Gesture::kNone;
		m_abortRequested.store(false, std::memory_order_release);
		m_firstTransformPublished.store(false, std::memory_order_release);
		m_firstTransformApplied.store(false, std::memory_order_release);
		m_active.store(true, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			// A new session supersedes an unconsumed restore from the previous
			// session. The snapshot is refreshed below before any new command is
			// published.
			m_pending.valid = false;
			m_pendingRestore.valid = false;
			m_pendingRestore.reason.clear();
			m_pendingCameraEnter = true;
		}
		SetStatus(TextLiteral("实时角色预览已启动；右键旋转，中键平移，滚轮缩放。"));
		REX::INFO("[IAD Preview] session started for player {:08X}, scene generation {}", m_snapshot.formID, m_snapshot.sceneGeneration);
		REX::INFO("[IAD Preview] camera snapshot at ({:.1f}, {:.1f}, {:.1f}); player at ({:.1f}, {:.1f}, {:.1f})",
			m_snapshot.cameraPosition.x, m_snapshot.cameraPosition.y, m_snapshot.cameraPosition.z,
			m_snapshot.position.x, m_snapshot.position.y, m_snapshot.position.z);
		return true;
	}

	bool UIWorldPreviewSession::IsSceneInvalid() const
	{
		if (ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition()) return true;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || player != m_snapshot.actor || player->GetFormID() != m_snapshot.formID) return true;
		if (player->IsDead(false) || player->IsDeleted() || player->IsDisabled()) return true;
		if (ModelManager::GetSceneGeneration() != m_snapshot.sceneGeneration) return true;

		auto* currentRoot = player->Get3D(false) ? player->Get3D(false)->IsNode() : nullptr;
		return currentRoot == nullptr || currentRoot != m_snapshot.root3D;
	}

	void UIWorldPreviewSession::RouteInput(bool a_yieldToGizmo)
	{
		if (!m_active.load(std::memory_order_acquire)) return;

		ImGuiIO& io = ImGui::GetIO();
		const bool overWindow = IsMouseOverImGuiWindow();
		const bool fine = io.KeyShift;
		const float precision = fine ? 0.2f : 1.0f;
		RE::NiPoint3 right{ 1.0f, 0.0f, 0.0f };
		RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
		RE::NiPoint3 forward{ 0.0f, 1.0f, 0.0f };
		PreviewCameraBasis cameraBasis;
		if (Adapter().GetBasis(cameraBasis)) {
			// Fallout's camera basis is stored as forward, up, right in these
			// rows. The UI consumes the copied frame from the camera seam.
			forward = cameraBasis.forward;
			up = cameraBasis.up;
			right = cameraBasis.right;
		}

		if (m_gesture == Gesture::kRotate) {
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				m_gesture = Gesture::kNone;
				return;
			}
			m_previewAngle.z = WrapAngle(m_previewAngle.z + io.MouseDelta.x * kRotateSensitivity * precision);
			if (std::abs(io.MouseDelta.x) > 0.01f) QueueTransform(false, true);
		}
		else if (m_gesture == Gesture::kPan) {
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
				m_gesture = Gesture::kNone;
				return;
			}

			const float sensitivity = kPanSensitivity * precision;
			m_previewCameraPosition = Add(m_previewCameraPosition, Scale(right, io.MouseDelta.x * sensitivity));
			m_previewCameraPosition = Add(m_previewCameraPosition, Scale(up, -io.MouseDelta.y * sensitivity));
			if (std::sqrt(io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y) > 0.01f) QueueTransform(true, false);
		}

		if (m_gesture == Gesture::kNone && !overWindow && !a_yieldToGizmo) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				m_gesture = Gesture::kRotate;
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
				m_gesture = Gesture::kPan;
			}

			if (std::abs(io.MouseWheel) > 0.01f) {
				const auto offset = Add(m_previewCameraPosition, Scale(m_snapshot.position, -1.0f));
				const float distance = std::clamp(Length(offset), kMinZoomDistance, kMaxZoomDistance);
				const float nextDistance = std::clamp(distance - io.MouseWheel * kZoomSensitivity * precision, kMinZoomDistance, kMaxZoomDistance);
				m_previewCameraPosition = Add(m_previewCameraPosition, Scale(forward, distance - nextDistance));
				QueueTransform(true, false);
			}
		}
	}

	void UIWorldPreviewSession::QueueTransform(bool a_writeCameraPosition, bool a_writeAngle)
	{
		if (!m_active.load(std::memory_order_acquire)) return;
		if (!a_writeCameraPosition && !a_writeAngle) return;

		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			m_pending.valid = true;
			m_pending.writeCameraPosition = m_pending.writeCameraPosition || a_writeCameraPosition;
			m_pending.writeAngle = m_pending.writeAngle || a_writeAngle;
			m_pending.formID = m_snapshot.formID;
			m_pending.root3D = m_snapshot.root3D;
			m_pending.sceneGeneration = m_snapshot.sceneGeneration;
			m_pending.cameraPosition = m_previewCameraPosition;
			m_pending.angle = m_previewAngle;
		}
		if (!m_firstTransformPublished.exchange(true, std::memory_order_acq_rel)) {
			REX::INFO("[IAD Preview] first transform published from ImGui thread");
		}
	}

	void UIWorldPreviewSession::ProcessGameThread()
	{
		PreviewSceneSnapshot previewSnapshot;
		if (m_active.load(std::memory_order_acquire)) {
			previewSnapshot.identity.actorFormID = m_snapshot.formID;
			previewSnapshot.identity.sceneGeneration = m_snapshot.sceneGeneration;
			previewSnapshot.identity.actor3DGeneration = NodeManager::GetActor3DGeneration(m_snapshot.formID);
			previewSnapshot.actorPosition = m_snapshot.position;
		}
		PreviewScene::GetSingleton().ProcessGameThread(
			m_active.load(std::memory_order_acquire), previewSnapshot);

		bool enterCamera = false;
		PendingRestore restore;
		PendingTransform transform;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			if (m_pendingCameraEnter) {
				enterCamera = true;
				m_pendingCameraEnter = false;
			}
			// Restore has priority so closing the menu cannot leave a pending
			// interactive sample applied after the snapshot is restored.
			if (m_pendingRestore.valid) {
				restore = std::move(m_pendingRestore);
				m_pendingRestore = {};
			}
			else if (m_pending.valid) {
				transform = m_pending;
				m_pending = {};
			}
		}

		if (enterCamera) {
			ProcessPendingCameraEnter();
		}
		if (restore.valid) {
			ProcessPendingRestore(std::move(restore));
		}
		else if (transform.valid) {
			ProcessPendingTransform(transform);
		}
	}

	void UIWorldPreviewSession::ProcessPendingCameraEnter()
	{
		if (!m_active.load(std::memory_order_acquire) || ModelManager::IsGameLoading() ||
			ModelManager::IsMainMenuTransition() || IsSceneInvalid()) {
			m_abortRequested.store(true, std::memory_order_release);
			return;
		}

		bool cameraOwned = m_cameraOwned.load(std::memory_order_acquire);
		if (!Adapter().EnterPreview(m_snapshot.cameraPosition, cameraOwned)) {
			m_abortRequested.store(true, std::memory_order_release);
			return;
		}
		m_cameraOwned.store(cameraOwned, std::memory_order_release);
	}

	void UIWorldPreviewSession::ProcessPendingTransform(PendingTransform a_pending)
	{
		if (!a_pending.valid || !m_active.load(std::memory_order_acquire) ||
			ModelManager::IsGameLoading() || ModelManager::IsMainMenuTransition() ||
			ModelManager::GetSceneGeneration() != a_pending.sceneGeneration) {
			if (a_pending.valid) m_abortRequested.store(true, std::memory_order_release);
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* root3D = player && player->Get3D(false) ? player->Get3D(false)->IsNode() : nullptr;
		if (!player || player->GetFormID() != a_pending.formID || root3D != a_pending.root3D ||
			player->IsDead(false) || player->IsDeleted() || player->IsDisabled()) {
			m_abortRequested.store(true, std::memory_order_release);
			return;
		}

		if (a_pending.writeCameraPosition) {
			if (!Adapter().ApplyPreview(a_pending.cameraPosition)) {
				m_abortRequested.store(true, std::memory_order_release);
				return;
			}
		}
		if (a_pending.writeAngle) {
			player->data.angle.x = a_pending.angle.x;
			player->data.angle.y = a_pending.angle.y;
			player->data.angle.z = a_pending.angle.z;
		}
		if (!m_firstTransformApplied.exchange(true, std::memory_order_acq_rel)) {
			REX::INFO("[IAD Preview] first transform applied on game thread");
		}
	}

	void UIWorldPreviewSession::ProcessPendingRestore(PendingRestore a_restore)
	{
		if (!a_restore.valid || IsActive() || ModelManager::IsGameLoading() ||
			ModelManager::IsMainMenuTransition() ||
			ModelManager::GetSceneGeneration() != a_restore.snapshot.sceneGeneration) {
			if (a_restore.valid) {
				REX::INFO("[IAD Preview] restore skipped after session/scene transition: {}", a_restore.reason);
			}
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* root3D = player && player->Get3D(false) ? player->Get3D(false)->IsNode() : nullptr;
		if (!player || player->GetFormID() != a_restore.snapshot.formID || root3D != a_restore.snapshot.root3D) {
			REX::INFO("[IAD Preview] restore skipped because player 3D changed: {}", a_restore.reason);
			return;
		}

		player->data.angle.x = a_restore.snapshot.angle.x;
		player->data.angle.y = a_restore.snapshot.angle.y;
		player->data.angle.z = a_restore.snapshot.angle.z;
		if (a_restore.restoreCamera) {
			if (a_restore.adapter) a_restore.adapter->RestorePreview(true);
			m_cameraOwned.store(false, std::memory_order_release);
			REX::INFO("[IAD Preview] camera preview exited");
		}
		HolsterManager::GetSingleton()->ForceRefreshAll();
		REX::INFO("[IAD Preview] player transform restored after {}", a_restore.reason);
	}

	void UIWorldPreviewSession::End(std::string_view a_reason)
	{
		if (!m_active.exchange(false, std::memory_order_acq_rel)) return;

		m_gesture = Gesture::kNone;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			m_pendingCameraEnter = false;
			m_pending.valid = false;
			m_pendingRestore.valid = false;
		}
		m_abortRequested.store(false, std::memory_order_release);
		SetStatus(std::string(TextLiteral("实时角色预览已结束：")) + std::string(a_reason));
		REX::INFO("[IAD Preview] session ended: {}", a_reason);
		HolsterManager::GetSingleton()->RequestPreviewTransformReset(m_snapshot.formID);
		QueueRestore(m_snapshot, std::string(a_reason), m_cameraOwned.load(std::memory_order_acquire));
	}

	void UIWorldPreviewSession::QueueRestore(ActorSnapshot a_snapshot, std::string a_reason, bool a_restoreCamera)
	{
		std::lock_guard<std::mutex> lock(m_pendingMutex);
		m_pending.valid = false;
		m_pendingRestore.valid = true;
		m_pendingRestore.restoreCamera = a_restoreCamera;
		m_pendingRestore.adapter = m_adapter;
		m_pendingRestore.snapshot = a_snapshot;
		m_pendingRestore.reason = std::move(a_reason);
		m_adapter = nullptr;
	}

	IPreviewSceneAdapter& UIWorldPreviewSession::Adapter() const
	{
		return m_adapter ? *m_adapter : PreviewScene::GetSingleton().GetActive();
	}

	void UIWorldPreviewSession::SetStatus(std::string a_status)
	{
		m_status = std::move(a_status);
	}

	void UIWorldPreviewSession::DrawStatus() const
	{
		if (IsActive()) {
			ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.75f, 1.0f), TextLiteral("实时角色会话: Active"));
		}
		else {
			ImGui::TextDisabled(TextLiteral("实时角色会话: Inactive"));
		}
		if (!m_status.empty()) ImGui::TextWrapped(TextLiteral("会话状态: %s"), m_status.c_str());
	}
}
