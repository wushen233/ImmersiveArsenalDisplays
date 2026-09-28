#pragma once

#include "Data/ConfigManager.h"

#include <RE/N/NiPoint3.h>

namespace IAD::UI {

    class UITransformEditorControls final {
    public:
        static bool DrawTransformWidget(const char* a_label, RE::NiPoint3& a_value, const RE::NiPoint3& a_defaultValue, float a_baseSpeed, bool a_isRotation = false);
        static bool DrawColorRGBA(const char* a_label, ColorRGBA& a_color);
    };

}
