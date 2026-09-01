#pragma once

namespace IAD::UI {
    // 所有的 ImGui 独立面板，未来都必须继承这个基类
    class UIWindow {
    public:
        virtual ~UIWindow() = default;
        
        // 纯虚函数，强制每个子窗口必须实现自己的 Draw() 绘制逻辑
        virtual void Draw() = 0;
    };
}