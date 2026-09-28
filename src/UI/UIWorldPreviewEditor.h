#pragma once

#include "System/HolsterManager.h"
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

namespace IAD::UI {
	class UIWorldPreviewEditor final {
	public:
		enum class EditorWindow {
			kNone,
			kSlots,
			kNodes,
			kVisualizer
		};

		static UIWorldPreviewEditor& GetSingleton()
		{
			static UIWorldPreviewEditor instance;
			return instance;
		}

		void HandleInput(
			const std::vector<DebugNode>& a_nodes,
			const std::vector<DebugBoundSphere>& a_modelBounds,
			const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen);
		void RenderOverlay(
			ImDrawList* a_drawList,
			const std::vector<DebugNode>& a_nodes,
			const std::vector<DebugBoundSphere>& a_modelBounds,
			const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen);
		void BeginViewportFrame(const ImVec2& a_displaySize);
		void SetDebugSettings(const DebugSettings& a_settings) noexcept;
		bool IsInPreviewViewport(const ImVec2& a_point) const noexcept;
		void NotifyEditorWindow(EditorWindow a_window, bool a_focused);
		void SetActiveEditorWindow(EditorWindow a_window);
		bool ShouldRenderNode(DebugNodeType a_type) const;
		bool IsGizmoInteractionActive() const noexcept
		{
			return m_drag.active || m_hoveredAxis != ActiveAxis::kNone ||
				!m_hoveredNodeName.empty() || !m_hoveredSlotName.empty();
		}
		void DrawStatus() const;
		void DrawTransactionControls();
		void CancelCurrentEdit();
		void UndoLastEdit();
		// Drop pointers and edit transactions that refer to a replaced actor 3D
		// tree without issuing scene writes against the old tree.
		void InvalidateSceneSnapshot();
		void Reset();

	private:
		struct EditTransaction {
			bool valid = false;
			DebugNodeType type = DebugNodeType::kVanilla;
			std::string name;
			ConfigScope scope = ConfigScope::kGlobal;
			std::uint32_t queryID = 0;
			int gender = 0;
			bool overrideTransform = false;
			bool createdConfig = false;
			TransformData transform;
		};

		struct DragState {
			bool active = false;
			bool dirty = false;
			DebugNodeType type = DebugNodeType::kVanilla;
			ActiveAxis axis = ActiveAxis::kNone;
			std::string name;
			RE::NiPoint3 axisWorld{ 0.0f, 0.0f, 0.0f };
			RE::NiPoint3 startWorld{ 0.0f, 0.0f, 0.0f };
			ImVec2 startMouse{ 0.0f, 0.0f };
			TransformData startTransform;
			EditTransaction before;
			RE::NiPoint3 lastConfigDelta{ 0.0f, 0.0f, 0.0f };
			bool hasLastConfigDelta = false;
		};

		struct ScreenNode {
			const DebugNode* node = nullptr;
			ImVec2 screenPos{ 0.0f, 0.0f };
			ImVec2 axisEnd[3]{};
			bool axisValid[3]{ false, false, false };
		};

		struct ScreenModel {
			const DebugBoundSphere* bound = nullptr;
			std::vector<ImVec2> outline;
			ImVec2 min{ 0.0f, 0.0f };
			ImVec2 max{ 0.0f, 0.0f };
			ImVec2 center{ 0.0f, 0.0f };
			float area = 0.0f;
			bool geometryOutline = false;
		};

		struct ViewportState {
			ImVec2 displaySize{ 0.0f, 0.0f };
			bool valid = false;
		};

		static std::string StripManagedPrefix(const std::string& a_name);
		static bool IsSameNode(const DebugNode& a_node, const std::string& a_selectedName);
		static float DistanceToSegmentSq(const ImVec2& a_point, const ImVec2& a_start, const ImVec2& a_end, float* a_outT = nullptr);
		static bool IsPointInPolygon(const std::vector<ImVec2>& a_polygon, const ImVec2& a_point);
		static float DistanceToPolylineSq(const std::vector<ImVec2>& a_polyline, const ImVec2& a_point);
		static RE::NiPoint3 AxisFor(const DebugNode& a_node, ActiveAxis a_axis, bool a_localSpace);
		static TransformData& SelectGenderTransform(ConfigBase& a_config, int a_gender);
		static int GetPlayerGender();

		ConfigBase* FindLocalConfig(const DebugNode& a_node) const;
		ConfigBase* FindTransactionConfig(const EditTransaction& a_transaction) const;
		SlotDefinition* EnsureLocalSlotOverride(const DebugNode& a_node, bool& a_created);
		bool RestoreTransaction(const EditTransaction& a_transaction);
		std::vector<ScreenNode> BuildScreenNodes(
			const std::vector<DebugNode>& a_nodes,
			const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen) const;
		std::vector<ScreenModel> BuildScreenModels(
			const std::vector<DebugBoundSphere>& a_modelBounds,
			const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen) const;
		void PrepareScreenCache(
			const std::vector<DebugNode>& a_nodes,
			const std::vector<DebugBoundSphere>& a_modelBounds,
			const std::function<bool(const RE::NiPoint3&, ImVec2&)>& a_worldToScreen);
		void SelectTarget(const DebugNode& a_node);
		void SelectSlotTarget(const std::string& a_slotName);
		const ScreenModel* FindModelAt(const std::vector<ScreenModel>& a_models, const ImVec2& a_point) const;
		bool BeginDrag(const DebugNode& a_node, ActiveAxis a_axis, const ImVec2& a_mouse);
		void UpdateDrag(const DebugNode& a_node, const ImVec2& a_axisStart, const ImVec2& a_axisEnd);
		void FinishDrag();
		RE::NiPoint3 WorldDeltaToConfigDelta(const DebugNode& a_node, const RE::NiPoint3& a_worldDelta) const;
		void SetStatus(std::string a_status);

		DragState m_drag;
		std::vector<EditTransaction> m_editHistory;
		ViewportState m_viewport;
		std::string m_status;
		EditorWindow m_activeEditorWindow = EditorWindow::kNone;
		std::string m_hoveredNodeName;
		ActiveAxis m_hoveredAxis = ActiveAxis::kNone;
		std::string m_hoveredSlotName;
		bool m_screenCacheValid = false;
		DebugSettings m_debugSettings{};
		const std::vector<DebugNode>* m_cachedNodes = nullptr;
		const std::vector<DebugBoundSphere>* m_cachedModelBounds = nullptr;
		std::vector<ScreenNode> m_screenNodes;
		std::vector<ScreenModel> m_screenModels;
	};
}
