#pragma once

#include "Data/ConfigManager.h"

namespace IAD::UI {

    class UIModelEditorControls final {
    public:
        static bool DrawModelCleanupSettings(bool& a_disableHavok, bool& a_removeEditorMarker, bool& a_removeProjectileTracers, const char* a_idSuffix);
        static bool DrawModelAnimationSettings(ModelAnimationConfig& a_animation, const char* a_idSuffix);
        static bool DrawModelEffectShaderSettings(ModelEffectShaderConfig& a_effect, const char* a_idSuffix);
        static bool DrawModelLightSettings(ModelLightConfig& a_light, const char* a_idSuffix);
        static bool DrawModelSwapVariableSource(ModelSwapVariableSource& a_source, const char* a_idSuffix);
    };

}
