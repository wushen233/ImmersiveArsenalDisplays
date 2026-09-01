#pragma once
#include "UIWindow.h"
#include "Data/ConfigManager.h"
#include <string>

namespace IAD::UI {
	bool DrawConditionTreeEditor(IAD::ConditionNode& rootNode, bool showProfileControls = true);

    class UISlotsWindow : public UIWindow { public: void Draw() override; };
    class UINodesWindow : public UIWindow { public: void Draw() override; };
    class UICustomsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileEditorWindow : public UIWindow { public: void Draw() override; };
    class UIProfileSlotsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileCustomsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileNodesWindow : public UIWindow { public: void Draw() override; };
    class UIProfileFormFiltersWindow : public UIWindow { public: void Draw() override; };
    class UIProfileModelGroupsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileNodeMonitorsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileConditionsWindow : public UIWindow { public: void Draw() override; };
    class UIProfileTransformsWindow : public UIWindow { public: void Draw() override; };
    class UIProfilePhysicsWindow : public UIWindow { public: void Draw() override; };

    class UIFiltersWindow : public UIWindow {
    private:
        std::string m_selectedProf = "";
        IAD::FormFilter m_filterData;
    public:
        void Draw() override;
    };

    class UIBoneScannerWindow : public UIWindow { public: void Draw() override; };
    class UIVisualizerWindow : public UIWindow { public: void Draw() override; };
}
