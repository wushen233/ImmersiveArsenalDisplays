#pragma once

#include "Data/ConfigManager.h"
#include "UIInspectorNavigation.h"
#include "UIRecordSelectionState.h"

namespace IAD::UI {
    enum class MeshEditMode {
        kWeapon = 0,
        kHolster = 1,
        kMagazine = 2,
        kModelGroup = 3
    };

    // Editor-owned state for one record family. Runtime configuration remains
    // in ConfigManager; this context only owns navigation and transient UI
    // editing state.
    struct UIEditorContext {
        bool isOpen = false;
        std::uint32_t currentID = 0;
        ConfigScope scope = ConfigScope::kGlobal;
        bool scopeTabInitialized = false;
        std::uint32_t id = 0;
        int targetFilter = 0;
        int genderEdit = 0;
        bool syncGender = false;
        MeshEditMode meshMode = MeshEditMode::kWeapon;
        InspectorNavigationState inspectorNavigation;
        UIRecordSelectionState selection;
        bool lastSyncGender = false;
        bool wasDraggingTransform = false;
        bool wasDraggingMesh = false;
        bool wasDraggingPhysics = false;
        bool pendingConfigSave = false;
    };
}
