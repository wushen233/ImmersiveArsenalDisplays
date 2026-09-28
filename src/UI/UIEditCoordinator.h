#pragma once

namespace IAD::UI {
    class UIEditCoordinator final {
    public:
        static void RequestConfigChange();
        static void RequestConfigSave();
        static void RequestINIChange(bool a_refreshRuntime = false);
        static void RequestINISettingsSave();
        static void RequestRuntimeRefresh();
        static void CommitNow();
    };
}
