#pragma once

namespace IAD::UI {
    // 所有的 ImGui 独立面板，未来都必须继承这个基类
    class UIWindow {
    public:
        virtual ~UIWindow() = default;

        // Lifecycle hooks mirror the IED UIContext boundary. Existing
        // windows only need Draw(); future stateful windows can opt in without
        // moving lifecycle policy back into ImGuiManager.
        virtual void Initialize() {}
        virtual void Reset() {}
        virtual void OnOpen() {}
        virtual void OnClose() {}

        virtual void Draw() = 0;
    };
}
