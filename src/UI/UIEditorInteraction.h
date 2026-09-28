#pragma once

namespace IAD::UI {
    class UIEditorInteraction final {
    public:
        static bool DrawPaneSplitter(const char* a_id, float& a_width, float a_minWidth, float a_maxWidth);
        static void ClampPaneWidth(float& a_width, float a_minWidth, float a_maxWidth);
        static void TrackLiveConfigEdits(bool& a_pendingConfigSave);
    };
}
