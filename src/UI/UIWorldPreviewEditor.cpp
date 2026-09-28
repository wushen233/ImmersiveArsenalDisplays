#include "pch.h"
#include "UILocalization.h"
#include "UIWorldPreviewEditor.h"

#include "Data/ConfigManager.h"
#include "Engine/NodeManager.h"
#include "ImGuiManager.h"
#include "UIEditCoordinator.h"
#include "UIEditorContextStore.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESNPC.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace IAD::UI {
	namespace {
		constexpr float kNodeHitRadius = 14.0f;
		constexpr float kAxisHitRadius = 9.0f;
		constexpr float kGizmoLength = 18.0f;
		constexpr float kEpsilon = 0.0001f;

		UIEditorContext& GetSlotEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Slot(); }
		UIEditorContext& GetNodeEditorContext() noexcept { return UIEditorContextStore::GetSingleton().Node(); }

		bool NearlyEqual(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
		{
			return std::abs(a_lhs.x - a_rhs.x) < kEpsilon &&
				std::abs(a_lhs.y - a_rhs.y) < kEpsilon &&
				std::abs(a_lhs.z - a_rhs.z) < kEpsilon;
		}

		uint32_t GetQueryID(const ImGuiManager::WindowState& a_state)
		{
			return (a_state.scope == ConfigScope::kGlobal) ? a_state.targetFilter : a_state.id;
		}

		RE::NiPoint3 Normalize(const RE::NiPoint3& a_value)
		{
			const float length = std::sqrt(a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z);
			if (length <= kEpsilon) return { 0.0f, 0.0f, 0.0f };
			return { a_value.x / length, a_value.y / length, a_value.z / length };
		}

		bool IsMouseOverUI()
		{
			return ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
		}

		float Cross2D(const ImVec2& a_origin, const ImVec2& a_lhs, const ImVec2& a_rhs)
		{
			return (a_lhs.x - a_origin.x) * (a_rhs.y - a_origin.y) -
				(a_lhs.y - a_origin.y) * (a_rhs.x - a_origin.x);
		}

		std::vector<ImVec2> BuildConvexHull(std::vector<ImVec2> a_points)
		{
			if (a_points.size() < 3) return a_points;
			std::sort(a_points.begin(), a_points.end(), [](const ImVec2& a_lhs, const ImVec2& a_rhs) {
				return a_lhs.x < a_rhs.x || (a_lhs.x == a_rhs.x && a_lhs.y < a_rhs.y);
			});
			a_points.erase(std::unique(a_points.begin(), a_points.end(), [](const ImVec2& a_lhs, const ImVec2& a_rhs) {
				return std::abs(a_lhs.x - a_rhs.x) < 0.25f && std::abs(a_lhs.y - a_rhs.y) < 0.25f;
			}), a_points.end());
			if (a_points.size() < 3) return a_points;

			std::vector<ImVec2> hull;
			hull.reserve(a_points.size() * 2);
			for (const auto& point : a_points) {
				while (hull.size() >= 2 && Cross2D(hull[hull.size() - 2], hull.back(), point) <= 0.0f) hull.pop_back();
				hull.push_back(point);
			}
			const std::size_t lowerSize = hull.size();
			for (auto it = a_points.rbegin(); it != a_points.rend(); ++it) {
				while (hull.size() > lowerSize && Cross2D(hull[hull.size() - 2], hull.back(), *it) <= 0.0f) hull.pop_back();
				hull.push_back(*it);
			}
			if (!hull.empty()) hull.pop_back();
			return hull;
		}
	}

	std::string UIWorldPreviewEditor::StripManagedPrefix(const std::string& a_name)
	{
		if (a_name.rfind("IAD_CME_", 0) == 0 || a_name.rfind("IAD_MOV_", 0) == 0) {
			return a_name.substr(8);
		}
		return a_name;
	}

	void UIWorldPreviewEditor::BeginViewportFrame(const ImVec2& a_displaySize)
	{
		m_screenCacheValid = false;
		m_cachedNodes = nullptr;
		m_cachedModelBounds = nullptr;
		m_viewport.displaySize = a_displaySize;
		m_viewport.valid = a_displaySize.x > 1.0f && a_displaySize.y > 1.0f;
	}

	void UIWorldPreviewEditor::SetDebugSettings(const DebugSettings& a_settings) noexcept
	{
		m_debugSettings = a_settings;
	}

	bool UIWorldPreviewEditor::IsInPreviewViewport(const ImVec2& a_point) const noexcept
	{
		return m_viewport.valid && a_point.x >= 0.0f && a_point.x <= m_viewport.displaySize.x &&
			a_point.y >= 0.0f && a_point.y <= m_viewport.displaySize.y;
	}

	bool UIWorldPreviewEditor::IsSameNode(const DebugNode& a_node, const std::string& a_selectedName)
	{
		if (a_selectedName.empty()) return false;
		return a_selectedName == a_node.name || a_selectedName == StripManagedPrefix(a_node.name);
	}

	float UIWorldPreviewEditor::DistanceToSegmentSq(const ImVec2& a_point, const ImVec2& a_start, const ImVec2& a_end, float* a_outT)
	{
		const ImVec2 delta{ a_end.x - a_start.x, a_end.y - a_start.y };
		const ImVec2 offset{ a_point.x - a_start.x, a_point.y - a_start.y };
		const float lengthSq = delta.x * delta.x + delta.y * delta.y;
		float t = lengthSq > 0.0f ? (offset.x * delta.x + offset.y * delta.y) / lengthSq : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		if (a_outT) *a_outT = t;
		const ImVec2 closest{ a_start.x + delta.x * t, a_start.y + delta.y * t };
		const float dx = a_point.x - closest.x;
		const float dy = a_point.y - closest.y;
		return dx * dx + dy * dy;
	}

	bool UIWorldPreviewEditor::IsPointInPolygon(const std::vector<ImVec2>& a_polygon, const ImVec2& a_point)
	{
		if (a_polygon.size() < 3) return false;
		bool inside = false;
		for (std::size_t i = 0, j = a_polygon.size() - 1; i < a_polygon.size(); j = i++) {
			const auto& current = a_polygon[i];
			const auto& previous = a_polygon[j];
			const bool crosses = ((current.y > a_point.y) != (previous.y > a_point.y)) &&
				a_point.x < (previous.x - current.x) * (a_point.y - current.y) /
					(previous.y - current.y) + current.x;
			if (crosses) inside = !inside;
		}
		return inside;
	}

	float UIWorldPreviewEditor::DistanceToPolylineSq(const std::vector<ImVec2>& a_polyline, const ImVec2& a_point)
	{
		if (a_polyline.size() < 2) return std::numeric_limits<float>::max();
		float closest = std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < a_polyline.size(); ++i) {
			const auto& start = a_polyline[i];
			const auto& end = a_polyline[(i + 1) % a_polyline.size()];
			closest = std::min(closest, DistanceToSegmentSq(a_point, start, end));
		}
		return closest;
	}

	RE::NiPoint3 UIWorldPreviewEditor::AxisFor(const DebugNode& a_node, ActiveAxis a_axis, bool a_localSpace)
	{
		if (!a_localSpace) {
			if (a_axis == ActiveAxis::kX) return { 1.0f, 0.0f, 0.0f };
			if (a_axis == ActiveAxis::kY) return { 0.0f, 1.0f, 0.0f };
			return { 0.0f, 0.0f, 1.0f };
		}
		if (a_axis == ActiveAxis::kX) return Normalize(a_node.axisX);
		if (a_axis == ActiveAxis::kY) return Normalize(a_node.axisY);
		return Normalize(a_node.axisZ);
	}

	TransformData& UIWorldPreviewEditor::SelectGenderTransform(ConfigBase& a_config, int a_gender)
	{
		return a_gender == 1 ? a_config.transforms.f : a_config.transforms.m;
	}

	int UIWorldPreviewEditor::GetPlayerGender()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) return 0;
		auto* npc = player->data.objectReference ? player->data.objectReference->As<RE::TESNPC>() : nullptr;
		return npc && npc->GetSex() == RE::SEX::kFemale ? 1 : 0;
	}

	ConfigBase* UIWorldPreviewEditor::FindLocalConfig(const DebugNode& a_node) const
	{
		auto* config = ConfigManager::GetSingleton();
		if (!config) return nullptr;
		const std::string name = StripManagedPrefix(a_node.name);

		if (a_node.type == DebugNodeType::kCME) {
			auto& nodes = config->GetNodes(GetNodeEditorContext().scope, GetQueryID(GetNodeEditorContext()));
			auto it = std::find_if(nodes.begin(), nodes.end(), [&](const NodeDefinition& a_candidate) {
				return a_candidate.nodeName == name || a_candidate.nodeName == a_node.name;
			});
			return it != nodes.end() ? static_cast<ConfigBase*>(&*it) : nullptr;
		}
		if (a_node.type == DebugNodeType::kMOV) {
			auto& slots = config->GetSlots(GetSlotEditorContext().scope, GetQueryID(GetSlotEditorContext()));
			auto it = std::find_if(slots.begin(), slots.end(), [&](const SlotDefinition& a_candidate) {
				return a_candidate.slotName == name || a_candidate.slotName == a_node.name;
			});
			return it != slots.end() ? static_cast<ConfigBase*>(&*it) : nullptr;
		}
		return nullptr;
	}

	ConfigBase* UIWorldPreviewEditor::FindTransactionConfig(const EditTransaction& a_transaction) const
	{
		auto* config = ConfigManager::GetSingleton();
		if (!config || !a_transaction.valid) return nullptr;

		if (a_transaction.type == DebugNodeType::kCME) {
			auto& nodes = config->GetNodes(a_transaction.scope, a_transaction.queryID);
			auto it = std::find_if(nodes.begin(), nodes.end(), [&](const NodeDefinition& a_candidate) {
				return a_candidate.nodeName == a_transaction.name ||
					a_candidate.nodeName == ("IAD_CME_" + a_transaction.name);
			});
			return it != nodes.end() ? static_cast<ConfigBase*>(&*it) : nullptr;
		}
		if (a_transaction.type == DebugNodeType::kMOV) {
			auto& slots = config->GetSlots(a_transaction.scope, a_transaction.queryID);
			auto it = std::find_if(slots.begin(), slots.end(), [&](const SlotDefinition& a_candidate) {
				return a_candidate.slotName == a_transaction.name ||
					a_candidate.slotName == ("IAD_MOV_" + a_transaction.name);
			});
			return it != slots.end() ? static_cast<ConfigBase*>(&*it) : nullptr;
		}
		return nullptr;
	}

	SlotDefinition* UIWorldPreviewEditor::EnsureLocalSlotOverride(const DebugNode& a_node, bool& a_created)
	{
		a_created = false;
		if (a_node.type != DebugNodeType::kMOV) return nullptr;

		auto* config = ConfigManager::GetSingleton();
		if (!config) return nullptr;
		const std::string name = StripManagedPrefix(a_node.name);
		const auto scope = GetSlotEditorContext().scope;
		const auto queryID = GetQueryID(GetSlotEditorContext());
		auto& localSlots = config->GetSlots(scope, queryID);
		auto localIt = std::find_if(localSlots.begin(), localSlots.end(), [&](const SlotDefinition& a_candidate) {
			return StripManagedPrefix(a_candidate.slotName) == name;
		});
		if (localIt != localSlots.end()) return &*localIt;

		// The live scene is resolved with the same fallback order as the runtime.
		// Copying that effective definition into the editor's current scope makes a
		// model-bound click immediately editable without changing another scope.
		SlotDefinition source;
		bool found = false;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			for (const auto& resolved : config->ResolveSlotsWithScope(player)) {
				if (StripManagedPrefix(resolved.data.slotName) == name) {
					source = resolved.data;
					found = true;
					break;
				}
			}
		}
		if (!found) return nullptr;

		localSlots.push_back(std::move(source));
		a_created = true;
		return &localSlots.back();
	}

	bool UIWorldPreviewEditor::RestoreTransaction(const EditTransaction& a_transaction)
	{
		if (!a_transaction.valid) return false;
		auto* config = ConfigManager::GetSingleton();
		if (!config) return false;

		if (a_transaction.createdConfig && a_transaction.type == DebugNodeType::kMOV) {
			auto& slots = config->GetSlots(a_transaction.scope, a_transaction.queryID);
			auto it = std::find_if(slots.begin(), slots.end(), [&](const SlotDefinition& a_candidate) {
				return StripManagedPrefix(a_candidate.slotName) == a_transaction.name;
			});
			if (it == slots.end()) return false;
			slots.erase(it);
			return true;
		}

		if (auto* target = FindTransactionConfig(a_transaction)) {
			target->overrideTransform = a_transaction.overrideTransform;
			SelectGenderTransform(*target, a_transaction.gender) = a_transaction.transform;
			return true;
		}
		return false;
	}

	void UIWorldPreviewEditor::SelectTarget(const DebugNode& a_node)
	{
		if (a_node.type == DebugNodeType::kVanilla) {
			SetStatus(TextLiteral("Vanilla 骨骼是只读预览，不能直接写回 IAD 配置。"));
			return;
		}

		const std::string name = StripManagedPrefix(a_node.name);
		const int playerGender = GetPlayerGender();
		if (a_node.type == DebugNodeType::kCME) {
			SetActiveEditorWindow(EditorWindow::kNodes);
			GetNodeEditorContext().selection.Select(a_node.name);
			GetNodeEditorContext().genderEdit = playerGender;
			ConfigManager::GetSingleton()->uiShowNodes = true;
		}
		else if (a_node.type == DebugNodeType::kMOV) {
			SetActiveEditorWindow(EditorWindow::kSlots);
			GetSlotEditorContext().selection.Select(a_node.name);
			GetSlotEditorContext().genderEdit = playerGender;
			ConfigManager::GetSingleton()->uiShowSlots = true;
		}
		REX::INFO("[IAD Preview] selected {} '{}'; editor windows remain open",
			a_node.type == DebugNodeType::kCME ? "CME" : "MOV", a_node.name);

		if (FindLocalConfig(a_node)) {
			SetStatus(TextLiteral("已选中 ") + name + TextLiteral("，拖动对应的红/绿/蓝轴可编辑位置。"));
		}
		else {
			SetStatus(TextLiteral("已选中 ") + name + TextLiteral("；当前范围没有本地配置，请先在编辑窗口添加 Override。"));
		}
	}

	void UIWorldPreviewEditor::SelectSlotTarget(const std::string& a_slotName)
	{
		const std::string name = StripManagedPrefix(a_slotName);
		if (name.empty()) return;
		const std::string managedName = a_slotName.rfind("IAD_MOV_", 0) == 0 ? a_slotName : "IAD_MOV_" + name;
		SetActiveEditorWindow(EditorWindow::kSlots);
		GetSlotEditorContext().selection.Select(managedName);
		GetSlotEditorContext().genderEdit = GetPlayerGender();
		ConfigManager::GetSingleton()->uiShowSlots = true;
		REX::INFO("[IAD Preview] selected MOV model outline for slot '{}'", managedName);

		DebugNode slotNode;
		slotNode.name = managedName;
		slotNode.type = DebugNodeType::kMOV;
		if (FindLocalConfig(slotNode)) {
			SetStatus(TextLiteral("已选中 ") + name + TextLiteral("，拖动对应的红/绿/蓝轴可编辑位置。"));
		}
		else {
			SetStatus(TextLiteral("已选中 ") + name + TextLiteral("；拖动插槽轴时会在当前范围自动创建 Override。"));
		}
	}

	bool UIWorldPreviewEditor::BeginDrag(const DebugNode& a_node, ActiveAxis a_axis, const ImVec2& a_mouse)
	{
		if (a_node.type == DebugNodeType::kVanilla) {
			SetStatus(TextLiteral("Vanilla 骨骼是只读预览，不能直接写回 IAD 配置。"));
			return false;
		}

		SelectTarget(a_node);
		bool createdConfig = false;
		auto* target = FindLocalConfig(a_node);
		if (!target && a_node.type == DebugNodeType::kMOV) {
			target = EnsureLocalSlotOverride(a_node, createdConfig);
		}
		if (!target) return false;

		const int gender = a_node.type == DebugNodeType::kCME ? GetNodeEditorContext().genderEdit : GetSlotEditorContext().genderEdit;
		const auto scope = a_node.type == DebugNodeType::kCME ? GetNodeEditorContext().scope : GetSlotEditorContext().scope;
		const auto queryID = a_node.type == DebugNodeType::kCME ? GetQueryID(GetNodeEditorContext()) : GetQueryID(GetSlotEditorContext());
		const bool startOverrideTransform = target->overrideTransform;
		const TransformData startTransform = SelectGenderTransform(*target, gender);
		m_drag = {};
		m_drag.active = true;
		m_drag.type = a_node.type;
		m_drag.axis = a_axis;
		m_drag.name = a_node.name;
		m_drag.axisWorld = AxisFor(a_node, a_axis, m_debugSettings.useLocalAxesSpace);
		m_drag.startWorld = a_node.pos;
		m_drag.startMouse = a_mouse;
		m_drag.startTransform = startTransform;
		m_drag.before.valid = true;
		m_drag.before.type = a_node.type;
		m_drag.before.name = StripManagedPrefix(a_node.name);
		m_drag.before.scope = scope;
		m_drag.before.queryID = queryID;
		m_drag.before.gender = gender;
		m_drag.before.overrideTransform = startOverrideTransform;
		m_drag.before.createdConfig = createdConfig;
		m_drag.before.transform = startTransform;

		// A slot with Override Transform disabled is currently using the identity
		// base transform. Seed the override from that identity to avoid a jump to
		// stale, ignored values on the first gizmo drag.
		if (a_node.type == DebugNodeType::kMOV && !target->overrideTransform) {
			target->overrideTransform = true;
			SelectGenderTransform(*target, gender) = TransformData{};
			m_drag.dirty = true;
			SetStatus(TextLiteral("已为插槽启用 Override Transform，正在编辑当前性别的位置。"));
		}
		else if (!target->stateMachine.empty()) {
			SetStatus(TextLiteral("正在编辑本地位置；注意：状态机中的变换可能在匹配时覆盖它。"));
		}
		// Keep the pre-edit state for cancel/undo, but use the effective state
		// after any MOV seed as the drag baseline.
		m_drag.startTransform = SelectGenderTransform(*target, gender);

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			HolsterManager::GetSingleton()->RequestPreviewTransform(
				player->GetFormID(),
				m_drag.before.name,
				a_node.type == DebugNodeType::kMOV,
				m_drag.startTransform);
		}
		if (m_drag.dirty) UIEditCoordinator::RequestConfigSave();
		return true;
	}

	RE::NiPoint3 UIWorldPreviewEditor::WorldDeltaToConfigDelta(const DebugNode& a_node, const RE::NiPoint3& a_worldDelta) const
	{
		if (a_node.type == DebugNodeType::kMOV) {
			return TransformMath::Multiply(TransformMath::Inverse(a_node.parentWorldRotate), a_worldDelta);
		}
		if (a_node.isAbsolute) {
			return TransformMath::Multiply(TransformMath::Inverse(a_node.rootWorldRotate), a_worldDelta);
		}

		const auto parentSpaceDelta = TransformMath::Multiply(TransformMath::Inverse(a_node.parentWorldRotate), a_worldDelta);
		return TransformMath::Multiply(TransformMath::Inverse(a_node.localRotate), parentSpaceDelta);
	}

	void UIWorldPreviewEditor::UpdateDrag(const DebugNode& a_node, const ImVec2& a_axisStart, const ImVec2& a_axisEnd)
	{
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const ImVec2 axisScreen{ a_axisEnd.x - a_axisStart.x, a_axisEnd.y - a_axisStart.y };
		const float axisLengthSq = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
		if (axisLengthSq <= 1.0f) return;

		const ImVec2 mouseDelta{ mouse.x - m_drag.startMouse.x, mouse.y - m_drag.startMouse.y };
		const float signedPixels = (mouseDelta.x * axisScreen.x + mouseDelta.y * axisScreen.y) / std::sqrt(axisLengthSq);
		float worldDistance = signedPixels * (kGizmoLength / std::sqrt(axisLengthSq));
		const auto& io = ImGui::GetIO();
		// Match common DCC editor behavior: Shift is fine, Ctrl is coarse, and
		// Ctrl+Shift is an extra-fine override for final alignment.
		if (io.KeyCtrl && io.KeyShift) worldDistance *= 0.025f;
		else if (io.KeyCtrl) worldDistance *= 4.0f;
		else if (io.KeyShift) worldDistance *= 0.1f;

		const RE::NiPoint3 worldDelta = m_drag.axisWorld * worldDistance;
		const RE::NiPoint3 configDelta = WorldDeltaToConfigDelta(a_node, worldDelta);
		if (m_drag.hasLastConfigDelta && NearlyEqual(configDelta, m_drag.lastConfigDelta)) return;

		auto* target = FindTransactionConfig(m_drag.before);
		if (!target) {
			SetStatus(TextLiteral("当前目标配置已改变，请重新选择场景节点。"));
			FinishDrag();
			return;
		}

		auto& transform = SelectGenderTransform(*target, m_drag.before.gender);
		transform = m_drag.startTransform;
		transform.pos = m_drag.startTransform.pos + configDelta;
		m_drag.lastConfigDelta = configDelta;
		m_drag.hasLastConfigDelta = true;
		m_drag.dirty = true;

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			HolsterManager::GetSingleton()->RequestPreviewTransform(
				player->GetFormID(),
				m_drag.before.name,
				a_node.type == DebugNodeType::kMOV,
				transform);
		}
	}

	void UIWorldPreviewEditor::FinishDrag()
	{
		if (m_drag.dirty && m_drag.before.valid) {
			// Keep completed drags as independent transactions so Ctrl+Z can walk
			// backwards through the whole editing session instead of only the last
			// drag. Bound the stack for long preview sessions.
			static constexpr std::size_t kMaxEditHistory = 64;
			if (m_editHistory.size() >= kMaxEditHistory) m_editHistory.erase(m_editHistory.begin());
			m_editHistory.push_back(m_drag.before);
			UIEditCoordinator::RequestConfigSave();
			SetStatus(TextLiteral("本次节点拖动已完成，可撤销。当前可撤销 ") + std::to_string(m_editHistory.size()) + TextLiteral(" 次。"));
		}
		m_drag = {};
		HolsterManager::GetSingleton()->activeUIItemAxis = ActiveAxis::kNone;
	}

	void UIWorldPreviewEditor::CancelCurrentEdit()
	{
		if (!m_drag.active) {
			SetStatus(TextLiteral("当前没有进行中的节点编辑。"));
			return;
		}

		const EditTransaction transaction = m_drag.before;
		if (RestoreTransaction(transaction)) {
			if (!transaction.createdConfig) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					HolsterManager::GetSingleton()->RequestPreviewTransform(
						player->GetFormID(),
						transaction.name,
						transaction.type == DebugNodeType::kMOV,
						transaction.transform);
				}
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
				HolsterManager::GetSingleton()->RequestTransformUpdate(player->GetFormID());
			}
			UIEditCoordinator::RequestConfigChange();
			SetStatus(transaction.createdConfig ?
				TextLiteral("已取消当前插槽编辑，自动创建的 Override 已移除。") :
				TextLiteral("已取消当前节点编辑，配置已恢复到拖动前状态。"));
		}
		else {
			SetStatus(TextLiteral("当前目标配置已不存在，无法恢复当前节点编辑。"));
		}

		m_drag = {};
		HolsterManager::GetSingleton()->activeUIItemAxis = ActiveAxis::kNone;
	}

	void UIWorldPreviewEditor::UndoLastEdit()
	{
		if (m_drag.active) {
			SetStatus(TextLiteral("当前仍在拖动节点，请先松开鼠标或取消当前拖动。"));
			return;
		}
		if (m_editHistory.empty()) {
			SetStatus(TextLiteral("没有可撤销的节点编辑。"));
			return;
		}

		const EditTransaction transaction = m_editHistory.back();
		if (RestoreTransaction(transaction)) {
			if (!transaction.createdConfig) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					HolsterManager::GetSingleton()->RequestPreviewTransform(
						player->GetFormID(),
						transaction.name,
						transaction.type == DebugNodeType::kMOV,
						transaction.transform);
				}
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				HolsterManager::GetSingleton()->RequestEvaluate(player->GetFormID());
				HolsterManager::GetSingleton()->RequestTransformUpdate(player->GetFormID());
			}
			UIEditCoordinator::RequestConfigChange();
			m_editHistory.pop_back();
			SetStatus(transaction.createdConfig ?
				TextLiteral("已撤销插槽编辑，自动创建的 Override 已移除。") :
				TextLiteral("已撤销节点编辑，配置已恢复。"));
			if (!m_editHistory.empty()) {
				SetStatus(m_status + TextLiteral("剩余可撤销 ") + std::to_string(m_editHistory.size()) + TextLiteral(" 次。"));
			}
		}
		else {
			SetStatus(TextLiteral("上次编辑对应的配置已不存在，无法撤销。"));
		}
	}

	void UIWorldPreviewEditor::InvalidateSceneSnapshot()
	{
		m_drag = {};
		m_editHistory.clear();
		m_status = TextLiteral("角色 3D 场景已替换，已清除旧的可视化节点选择。");
		m_activeEditorWindow = EditorWindow::kNone;
		m_viewport = {};
		m_hoveredNodeName.clear();
		m_hoveredSlotName.clear();
		m_hoveredAxis = ActiveAxis::kNone;
		m_screenCacheValid = false;
		m_cachedNodes = nullptr;
		m_cachedModelBounds = nullptr;
		HolsterManager::GetSingleton()->activeUIItemAxis = ActiveAxis::kNone;
	}

	void UIWorldPreviewEditor::SetStatus(std::string a_status)
	{
		m_status = std::move(a_status);
	}

	std::vector<UIWorldPreviewEditor::ScreenNode> UIWorldPreviewEditor::BuildScreenNodes(
		const std::vector<DebugNode>& a_nodes,
		const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen) const
	{
		std::vector<ScreenNode> screenNodes;
		screenNodes.reserve(a_nodes.size());
		for (const auto& node : a_nodes) {
			if (node.type == DebugNodeType::kVanilla || !ShouldRenderNode(node.type)) continue;
			ScreenNode screenNode;
			screenNode.node = &node;
			if (!a_worldToScreen(node.pos, screenNode.screenPos)) continue;
			for (int i = 0; i < 3; ++i) {
				const ActiveAxis axis = static_cast<ActiveAxis>(i + 1);
				const RE::NiPoint3 axisWorld = AxisFor(node, axis, m_debugSettings.useLocalAxesSpace);
				const RE::NiPoint3 endpoint = node.pos + axisWorld * kGizmoLength;
				screenNode.axisValid[i] = a_worldToScreen(endpoint, screenNode.axisEnd[i]);
			}
			screenNodes.push_back(screenNode);
		}
		return screenNodes;
	}

	std::vector<UIWorldPreviewEditor::ScreenModel> UIWorldPreviewEditor::BuildScreenModels(
		const std::vector<DebugBoundSphere>& a_modelBounds,
		const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen) const
	{
		std::vector<ScreenModel> screenModels;
		if (!ShouldRenderNode(DebugNodeType::kMOV)) return screenModels;
		screenModels.reserve(a_modelBounds.size());

		for (const auto& bound : a_modelBounds) {
			if (bound.slotName.empty() || bound.worldRadius <= 0.01f) continue;
			ScreenModel screenModel;
			screenModel.bound = &bound;
			std::vector<ImVec2> projectedVertices;
			if (!bound.worldVertices) continue;
			projectedVertices.reserve(bound.worldVertices->size());
			for (const auto& worldPoint : *bound.worldVertices) {
				ImVec2 projected;
				if (a_worldToScreen(worldPoint, projected)) projectedVertices.push_back(projected);
			}
			if (projectedVertices.size() >= 3) {
				screenModel.outline = BuildConvexHull(std::move(projectedVertices));
				screenModel.geometryOutline = screenModel.outline.size() >= 3;
			}
			if (!screenModel.geometryOutline) {
				if (!a_worldToScreen(bound.worldCenter, screenModel.center)) continue;
				screenModel.min = screenModel.center;
				screenModel.max = screenModel.center;
				const RE::NiPoint3 extents[3] = {
					{ bound.worldRadius, 0.0f, 0.0f },
					{ 0.0f, bound.worldRadius, 0.0f },
					{ 0.0f, 0.0f, bound.worldRadius }
				};
				for (const auto& extent : extents) {
					for (const float sign : { -1.0f, 1.0f }) {
						const RE::NiPoint3 point{
							bound.worldCenter.x + extent.x * sign,
							bound.worldCenter.y + extent.y * sign,
							bound.worldCenter.z + extent.z * sign
						};
						ImVec2 projected;
						if (!a_worldToScreen(point, projected)) continue;
						screenModel.min.x = std::min(screenModel.min.x, projected.x);
						screenModel.min.y = std::min(screenModel.min.y, projected.y);
						screenModel.max.x = std::max(screenModel.max.x, projected.x);
						screenModel.max.y = std::max(screenModel.max.y, projected.y);
					}
				}
				if (std::abs(screenModel.min.x - screenModel.max.x) <= 0.001f &&
					std::abs(screenModel.min.y - screenModel.max.y) <= 0.001f) {
					screenModel.min = { screenModel.center.x - 10.0f, screenModel.center.y - 10.0f };
					screenModel.max = { screenModel.center.x + 10.0f, screenModel.center.y + 10.0f };
				}
			}
			else {
				screenModel.min = screenModel.outline.front();
				screenModel.max = screenModel.outline.front();
				for (const auto& point : screenModel.outline) {
					screenModel.min.x = std::min(screenModel.min.x, point.x);
					screenModel.min.y = std::min(screenModel.min.y, point.y);
					screenModel.max.x = std::max(screenModel.max.x, point.x);
					screenModel.max.y = std::max(screenModel.max.y, point.y);
				}
				screenModel.center = { (screenModel.min.x + screenModel.max.x) * 0.5f, (screenModel.min.y + screenModel.max.y) * 0.5f };
			}
			screenModel.area = std::max(1.0f,
				(screenModel.max.x - screenModel.min.x) * (screenModel.max.y - screenModel.min.y));
			screenModels.push_back(screenModel);
		}
		return screenModels;
	}

	void UIWorldPreviewEditor::PrepareScreenCache(
		const std::vector<DebugNode>& a_nodes,
		const std::vector<DebugBoundSphere>& a_modelBounds,
		const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen)
	{
		if (m_screenCacheValid && m_cachedNodes == &a_nodes && m_cachedModelBounds == &a_modelBounds) return;
		m_screenNodes = BuildScreenNodes(a_nodes, a_worldToScreen);
		m_screenModels = BuildScreenModels(a_modelBounds, a_worldToScreen);
		m_cachedNodes = &a_nodes;
		m_cachedModelBounds = &a_modelBounds;
		m_screenCacheValid = true;
	}

	const UIWorldPreviewEditor::ScreenModel* UIWorldPreviewEditor::FindModelAt(
		const std::vector<ScreenModel>& a_models,
		const ImVec2& a_point) const
	{
		const ScreenModel* result = nullptr;
		float bestArea = std::numeric_limits<float>::max();
		for (const auto& model : a_models) {
			if (a_point.x < model.min.x || a_point.x > model.max.x ||
				a_point.y < model.min.y || a_point.y > model.max.y) {
				continue;
			}
			const bool hit = model.geometryOutline ?
				(IsPointInPolygon(model.outline, a_point) || DistanceToPolylineSq(model.outline, a_point) <= 10.0f * 10.0f) :
				true;
			if (!hit) continue;
			if (model.area < bestArea) {
				bestArea = model.area;
				result = &model;
			}
		}
		return result;
	}

	void UIWorldPreviewEditor::RenderOverlay(
		ImDrawList* a_drawList,
		const std::vector<DebugNode>& a_nodes,
		const std::vector<DebugBoundSphere>& a_modelBounds,
		const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen)
	{
		if (!a_drawList || !m_debugSettings.worldPreviewEdit) return;

		PrepareScreenCache(a_nodes, a_modelBounds, a_worldToScreen);
		const auto& screenNodes = m_screenNodes;
		const auto& screenModels = m_screenModels;
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const bool mouseInViewport = IsInPreviewViewport(mouse);
		m_hoveredNodeName.clear();
		m_hoveredSlotName.clear();
		m_hoveredAxis = ActiveAxis::kNone;

		if (mouseInViewport && !IsMouseOverUI()) {
			float closestAxisDistance = std::numeric_limits<float>::max();
			for (const auto& screenNode : screenNodes) {
				const auto* node = screenNode.node;
				const std::string& selectedName = node->type == DebugNodeType::kCME ? GetNodeEditorContext().selection.Selected() : GetSlotEditorContext().selection.Selected();
				if (!IsSameNode(*node, selectedName)) continue;
				for (int i = 0; i < 3; ++i) {
					if (!screenNode.axisValid[i]) continue;
					float segmentT = 0.0f;
					const float distance = DistanceToSegmentSq(mouse, screenNode.screenPos, screenNode.axisEnd[i], &segmentT);
					if (segmentT < 0.2f || distance > kAxisHitRadius * kAxisHitRadius || distance >= closestAxisDistance) continue;
					closestAxisDistance = distance;
					m_hoveredNodeName = node->name;
					m_hoveredAxis = static_cast<ActiveAxis>(i + 1);
				}
			}
			if (m_hoveredAxis == ActiveAxis::kNone) {
				if (const auto* model = FindModelAt(screenModels, mouse)) {
					m_hoveredSlotName = model->bound->slotName;
				}
				else {
					float closestNodeDistance = kNodeHitRadius * kNodeHitRadius;
					for (const auto& screenNode : screenNodes) {
						const float dx = mouse.x - screenNode.screenPos.x;
						const float dy = mouse.y - screenNode.screenPos.y;
						const float distance = dx * dx + dy * dy;
						if (distance >= closestNodeDistance) continue;
						closestNodeDistance = distance;
						m_hoveredNodeName = screenNode.node->name;
					}
				}
			}
		}
		if (m_hoveredAxis != ActiveAxis::kNone || !m_hoveredNodeName.empty()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}
		else if (!m_hoveredSlotName.empty()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}

		auto drawAxisArrow = [&](const ImVec2& a_start, const ImVec2& a_end, ImU32 a_color, float a_thickness) {
			const ImVec2 delta{ a_end.x - a_start.x, a_end.y - a_start.y };
			const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
			if (length <= 1.0f) return;
			const ImVec2 direction{ delta.x / length, delta.y / length };
			const ImVec2 normal{ -direction.y, direction.x };
			const ImVec2 base{ a_end.x - direction.x * 7.0f, a_end.y - direction.y * 7.0f };
			const ImVec2 left{ base.x + normal.x * 4.0f, base.y + normal.y * 4.0f };
			const ImVec2 right{ base.x - normal.x * 4.0f, base.y - normal.y * 4.0f };
			a_drawList->AddLine(a_start, base, a_color, a_thickness);
			a_drawList->AddTriangleFilled(a_end, left, right, a_color);
		};

		const ImU32 cmeColor = IM_COL32(255, 205, 80, 230);
		const ImU32 movColor = IM_COL32(80, 220, 235, 230);
		for (const auto& screenModel : screenModels) {
			const auto* bound = screenModel.bound;
			const bool selected = GetSlotEditorContext().selection.IsSelected(bound->slotName);
			const bool hovered = m_hoveredSlotName == bound->slotName;
			const ImU32 outlineColor = selected ? IM_COL32(75, 235, 245, 245) :
				hovered ? IM_COL32(245, 255, 255, 230) : IM_COL32(75, 220, 235, 75);
			const float thickness = selected ? 3.0f : hovered ? 2.5f : 1.0f;
			if (screenModel.geometryOutline) {
				a_drawList->AddPolyline(screenModel.outline.data(), static_cast<int>(screenModel.outline.size()), outlineColor, ImDrawFlags_Closed, thickness);
				if (selected || hovered) {
					const ImU32 fillColor = selected ? IM_COL32(75, 235, 245, 24) : IM_COL32(245, 255, 255, 18);
					a_drawList->AddConvexPolyFilled(screenModel.outline.data(), static_cast<int>(screenModel.outline.size()), fillColor);
				}
			}
			else {
				// Renderer vertex data is unavailable for a few dynamic/GPU-only
				// shapes. Keep a visibly different fallback so it is not mistaken for
				// the requested mesh contour.
				a_drawList->AddRect(screenModel.min, screenModel.max, IM_COL32(210, 170, 80, 135), 3.0f, 0, 1.0f);
			}
			if (selected || hovered) {
				const std::string label = "MOV: " + StripManagedPrefix(bound->slotName);
				a_drawList->AddText({ screenModel.min.x + 4.0f, screenModel.min.y - 17.0f }, IM_COL32(235, 255, 255, 245), label.c_str());
			}
		}
		for (const auto& screenNode : screenNodes) {
			const auto* node = screenNode.node;
			const bool selected = IsSameNode(*node, node->type == DebugNodeType::kCME ? GetNodeEditorContext().selection.Selected() : GetSlotEditorContext().selection.Selected());
			const bool hovered = m_hoveredNodeName == node->name;
			const ImU32 accent = node->type == DebugNodeType::kCME ? cmeColor : movColor;

			if (!selected) {
				a_drawList->AddCircleFilled(screenNode.screenPos, hovered ? 4.5f : 3.0f, IM_COL32(220, 230, 235, hovered ? 190 : 110));
				if (hovered) a_drawList->AddCircle(screenNode.screenPos, 8.0f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
				continue;
			}

			a_drawList->AddCircleFilled(screenNode.screenPos, 5.0f, IM_COL32(24, 28, 36, 230));
			a_drawList->AddCircle(screenNode.screenPos, 8.0f, accent, 0, 2.0f);
			a_drawList->AddLine({ screenNode.screenPos.x - 11.0f, screenNode.screenPos.y }, { screenNode.screenPos.x + 11.0f, screenNode.screenPos.y }, IM_COL32(255, 255, 255, 150), 1.0f);
			a_drawList->AddLine({ screenNode.screenPos.x, screenNode.screenPos.y - 11.0f }, { screenNode.screenPos.x, screenNode.screenPos.y + 11.0f }, IM_COL32(255, 255, 255, 150), 1.0f);

			const std::string label = (node->type == DebugNodeType::kCME ? "CME: " : "MOV: ") + StripManagedPrefix(node->name);
			a_drawList->AddText({ screenNode.screenPos.x + 12.0f, screenNode.screenPos.y - 19.0f }, IM_COL32(255, 255, 255, 245), label.c_str());
			for (int i = 0; i < 3; ++i) {
				if (!screenNode.axisValid[i]) continue;
				const ActiveAxis axis = static_cast<ActiveAxis>(i + 1);
				const bool axisHovered = m_hoveredNodeName == node->name && m_hoveredAxis == axis;
				const ImU32 axisColor = axis == ActiveAxis::kX ? IM_COL32(255, axisHovered ? 220 : 80, axisHovered ? 80 : 80, 255) :
					axis == ActiveAxis::kY ? IM_COL32(axisHovered ? 100 : 60, 255, axisHovered ? 100 : 80, 255) :
					IM_COL32(axisHovered ? 130 : 80, axisHovered ? 190 : 130, 255, 255);
				drawAxisArrow(screenNode.screenPos, screenNode.axisEnd[i], axisColor, axisHovered ? 5.0f : 3.0f);
				const char axisLabel = axis == ActiveAxis::kX ? 'X' : axis == ActiveAxis::kY ? 'Y' : 'Z';
				char axisText[2]{ axisLabel, '\0' };
				a_drawList->AddText({ screenNode.axisEnd[i].x + 3.0f, screenNode.axisEnd[i].y - 7.0f }, axisColor, axisText);
			}
		}
	}

	void UIWorldPreviewEditor::NotifyEditorWindow(EditorWindow a_window, bool a_focused)
	{
		if (a_focused) m_activeEditorWindow = a_window;
	}

	void UIWorldPreviewEditor::SetActiveEditorWindow(EditorWindow a_window)
	{
		m_activeEditorWindow = a_window;
	}

	bool UIWorldPreviewEditor::ShouldRenderNode(DebugNodeType a_type) const
	{
		if (!m_debugSettings.worldPreviewEdit || !m_debugSettings.worldPreviewAutoWindow || a_type == DebugNodeType::kVanilla) {
			return true;
		}

		if (m_activeEditorWindow == EditorWindow::kSlots) return a_type == DebugNodeType::kMOV;
		if (m_activeEditorWindow == EditorWindow::kNodes) return a_type == DebugNodeType::kCME;
		if (!GetSlotEditorContext().selection.Empty() && GetNodeEditorContext().selection.Empty()) return a_type == DebugNodeType::kMOV;
		if (!GetNodeEditorContext().selection.Empty() && GetSlotEditorContext().selection.Empty()) return a_type == DebugNodeType::kCME;
		return true;
	}

	void UIWorldPreviewEditor::HandleInput(
		const std::vector<DebugNode>& a_nodes,
		const std::vector<DebugBoundSphere>& a_modelBounds,
		const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen)
	{
		std::unique_lock<std::recursive_mutex> configLock;
		if (auto* config = ConfigManager::GetSingleton()) {
			configLock = std::unique_lock<std::recursive_mutex>(config->_configMutex);
		}
		auto* hm = HolsterManager::GetSingleton();
		if (!m_debugSettings.worldPreviewEdit) {
			if (m_drag.active) FinishDrag();
			m_hoveredNodeName.clear();
			m_hoveredSlotName.clear();
			m_hoveredAxis = ActiveAxis::kNone;
			return;
		}

		PrepareScreenCache(a_nodes, a_modelBounds, a_worldToScreen);
		const auto& screenNodes = m_screenNodes;
		const auto& screenModels = m_screenModels;
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) &&
			!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
			if (m_drag.active) CancelCurrentEdit();
			else UndoLastEdit();
			return;
		}

		if (m_drag.active) {
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				FinishDrag();
				return;
			}

			for (const auto& screenNode : screenNodes) {
				if (screenNode.node->name != m_drag.name) continue;
				const int axisIndex = static_cast<int>(m_drag.axis) - 1;
				if (axisIndex >= 0 && axisIndex < 3 && screenNode.axisValid[axisIndex]) {
					hm->activeUIItemAxis = m_drag.axis;
					UpdateDrag(*screenNode.node, screenNode.screenPos, screenNode.axisEnd[axisIndex]);
				}
				break;
			}
			return;
		}

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		if (!IsInPreviewViewport(mouse) || IsMouseOverUI()) return;
		if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

		const ScreenNode* bestAxisNode = nullptr;
		ActiveAxis bestAxis = ActiveAxis::kNone;
		float bestAxisDistance = std::numeric_limits<float>::max();
		for (const auto& screenNode : screenNodes) {
			if (screenNode.node->type == DebugNodeType::kVanilla || !IsSameNode(*screenNode.node,
				screenNode.node->type == DebugNodeType::kCME ? GetNodeEditorContext().selection.Selected() : GetSlotEditorContext().selection.Selected())) {
				continue;
			}
			for (int i = 0; i < 3; ++i) {
				if (!screenNode.axisValid[i]) continue;
				float segmentT = 0.0f;
				const float distance = DistanceToSegmentSq(mouse, screenNode.screenPos, screenNode.axisEnd[i], &segmentT);
				if (segmentT < 0.2f || distance > kAxisHitRadius * kAxisHitRadius || distance >= bestAxisDistance) continue;
				bestAxisDistance = distance;
				bestAxisNode = &screenNode;
				bestAxis = static_cast<ActiveAxis>(i + 1);
			}
		}
		if (bestAxisNode && BeginDrag(*bestAxisNode->node, bestAxis, mouse)) return;

		if (const auto* model = FindModelAt(screenModels, mouse)) {
			SelectSlotTarget(model->bound->slotName);
			return;
		}

		const ScreenNode* bestNode = nullptr;
		float bestNodeDistance = kNodeHitRadius * kNodeHitRadius;
		for (const auto& screenNode : screenNodes) {
			const float dx = mouse.x - screenNode.screenPos.x;
			const float dy = mouse.y - screenNode.screenPos.y;
			const float distance = dx * dx + dy * dy;
			if (distance <= bestNodeDistance) {
				bestNodeDistance = distance;
				bestNode = &screenNode;
			}
		}
		if (bestNode) SelectTarget(*bestNode->node);
	}

	void UIWorldPreviewEditor::DrawStatus() const
	{
		if (!m_status.empty()) {
			ImGui::TextWrapped(TextLiteral("预览编辑状态: %s"), m_status.c_str());
		}
		if (m_activeEditorWindow == EditorWindow::kSlots) {
			ImGui::TextDisabled(TextLiteral("当前自动显示: Slots / MOV 插槽"));
		}
		else if (m_activeEditorWindow == EditorWindow::kNodes) {
			ImGui::TextDisabled(TextLiteral("当前自动显示: Nodes / CME 节点"));
		}
		else {
			ImGui::TextDisabled(TextLiteral("当前自动显示: 根据已选目标决定"));
		}
		ImGui::TextDisabled(TextLiteral("左键点击节点或装备轮廓选择；拖动红/绿/蓝轴移动。右键拖动旋转角色，中键平移，滚轮缩放。Shift 精细(0.1x)，Ctrl 粗略(4x)，Ctrl+Shift 超精细(0.025x)。Ctrl+Z 可连续撤回拖动。"));
	}

	void UIWorldPreviewEditor::DrawTransactionControls()
	{
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.65f, 0.9f, 1.0f, 1.0f), TextLiteral("编辑操作"));
		if (m_drag.active) {
			ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), TextLiteral("编辑事务：正在拖动节点或插槽"));
			if (ImGui::Button(TextLiteral("取消当前拖动##worldPreviewCancel"))) {
				CancelCurrentEdit();
			}
		}
		else {
			ImGui::TextDisabled(TextLiteral("编辑事务：当前没有进行中的拖动"));
		}

		if (!m_editHistory.empty()) {
			if (ImGui::Button(TextLiteral("撤销上次编辑##worldPreviewUndo"))) {
				UndoLastEdit();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(TextLiteral("快捷键：Ctrl+Z（剩余 %zu 次）"), m_editHistory.size());
		}
		else {
			ImGui::TextDisabled(TextLiteral("没有可撤销的编辑（快捷键：Ctrl+Z）"));
		}
	}

	void UIWorldPreviewEditor::Reset()
	{
		if (m_drag.active) {
			CancelCurrentEdit();
		}
		m_drag = {};
		m_editHistory.clear();
		m_status.clear();
		m_activeEditorWindow = EditorWindow::kNone;
		m_viewport = {};
		m_hoveredNodeName.clear();
		m_hoveredSlotName.clear();
		m_hoveredAxis = ActiveAxis::kNone;
		m_screenCacheValid = false;
		m_cachedNodes = nullptr;
		m_cachedModelBounds = nullptr;
		HolsterManager::GetSingleton()->activeUIItemAxis = ActiveAxis::kNone;
	}
}
