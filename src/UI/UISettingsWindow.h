#pragma once
#include "UIWindow.h"

namespace IAD::UI {
    class UISettingsWindow : public UIWindow {
    public:
        // 实现基类的绘制接口
        void Draw() override;
    };
}