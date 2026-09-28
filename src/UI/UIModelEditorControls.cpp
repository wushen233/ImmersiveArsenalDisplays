#include "pch.h"
#include "UILocalization.h"
#include "UIModelEditorControls.h"

#include "Data/ConfigManager.h"
#include "UITransformEditorControls.h"

namespace IAD::UI {

    bool UIModelEditorControls::DrawModelCleanupSettings(bool& a_disableHavok, bool& a_removeEditorMarker, bool& a_removeProjectileTracers, const char* a_idSuffix)
    {
        bool changed = false;
        (void)a_disableHavok;
        ImGui::PushID(a_idSuffix);
        ImGui::TextDisabled(TextLiteral("Havok/碰撞：展示模型始终为渲染副本"));
        if (ImGui::Checkbox("Remove editor markers", &a_removeEditorMarker)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Strip editor marker/helper nodes from the cloned model.");
        }
        if (ImGui::Checkbox("Remove projectile tracers/lights", &a_removeProjectileTracers)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Strip projectile tracer and related light nodes from the cloned model.");
        }
        ImGui::PopID();
        return changed;
    }

    bool UIModelEditorControls::DrawModelAnimationSettings(ModelAnimationConfig& a_animation, const char* a_idSuffix)
    {
        bool changed = false;
        ImGui::PushID(a_idSuffix);
        if (ImGui::TreeNodeEx(TextLiteral("主模型动画 / Sequence"), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox(TextLiteral("播放 NiController Sequence"), &a_animation.playSequence)) {
                changed = true;
            }
            if (ImGui::Checkbox(TextLiteral("转发动画事件"), &a_animation.forwardAnimationEvents)) {
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(TextLiteral("SubGraphs：FO4 展示副本不支持"));
            if (ImGui::Checkbox(TextLiteral("禁用行为图动画"), &a_animation.disableBehaviorGraphAnims)) {
                changed = true;
            }

            char sequenceBuffer[128];
            strcpy_s(sequenceBuffer, a_animation.sequenceName.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText(TextLiteral("Sequence 名称"), sequenceBuffer, 128)) {
                a_animation.sequenceName = sequenceBuffer;
                changed = true;
            }

            char eventBuffer[128];
            strcpy_s(eventBuffer, a_animation.animationEvent.c_str());
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputText(TextLiteral("动画事件"), eventBuffer, 128)) {
                a_animation.animationEvent = eventBuffer;
                changed = true;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    bool UIModelEditorControls::DrawModelEffectShaderSettings(ModelEffectShaderConfig& a_effect, const char* a_idSuffix)
    {
        bool changed = false;
        ImGui::PushID(a_idSuffix);
        if (ImGui::TreeNodeEx(TextLiteral("主模型 Effect Shader"), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox(TextLiteral("启用效果数据"), &a_effect.enabled)) {
                changed = true;
            }
            if (a_effect.enabled) {
                ImGui::Indent();
                if (ImGui::Checkbox("Target Root", &a_effect.targetRoot)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Force", &a_effect.force)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Lighting", &a_effect.lighting)) {
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Alpha", &a_effect.alpha)) {
                    changed = true;
                }
                changed |= UITransformEditorControls::DrawColorRGBA("Fill Color", a_effect.fillColor);
                changed |= UITransformEditorControls::DrawColorRGBA("Rim Color", a_effect.rimColor);
                if (ImGui::DragFloat(TextLiteral("透明度倍率"), &a_effect.alphaMultiplier, 0.01f, 0.0f, 1.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Fill Scale", &a_effect.baseFillScale, 0.01f, 0.0f, 20.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Fill Alpha", &a_effect.baseFillAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Rim Alpha", &a_effect.baseRimAlpha, 0.01f, 0.0f, 10.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Edge Exponent", &a_effect.edgeExponent, 0.01f, 0.0f, 20.0f, "%.2f")) {
                    changed = true;
                }

                char textureBuffer[256];
                strcpy_s(textureBuffer, a_effect.baseTexturePath.c_str());
                ImGui::SetNextItemWidth(420.0f);
                if (ImGui::InputText("Base Texture", textureBuffer, 256)) {
                    a_effect.baseTexturePath = textureBuffer;
                    changed = true;
                }
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    bool UIModelEditorControls::DrawModelLightSettings(ModelLightConfig& a_light, const char* a_idSuffix)
    {
        bool changed = false;
        ImGui::PushID(a_idSuffix);
        if (ImGui::TreeNodeEx(TextLiteral("主模型 Extra Light"), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox(TextLiteral("启用附加灯光数据"), &a_light.enabled)) {
                changed = true;
            }
            if (a_light.enabled) {
                ImGui::Indent();
                ImGui::TextDisabled(TextLiteral("Target/Water/Landscape/Shadows：当前渲染路径不支持"));
                changed |= UITransformEditorControls::DrawColorRGBA("Diffuse", a_light.diffuse);
                if (ImGui::DragFloat("Radius", &a_light.radius, 1.0f, 0.0f, 4096.0f, "%.1f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Dimmer", &a_light.dimmer, 0.01f, 0.0f, 20.0f, "%.2f")) {
                    changed = true;
                }
                ImGui::TextDisabled(TextLiteral("FOV/Shadow Bias：当前渲染路径不支持"));
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("灯光位置偏移"), a_light.transform.pos, { 0, 0, 0 }, 0.5f, false);
                changed |= UITransformEditorControls::DrawTransformWidget(TextLiteral("灯光旋转"), a_light.transform.rot, { 0, 0, 0 }, 1.0f, true);
                ImGui::Unindent();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    bool UIModelEditorControls::DrawModelSwapVariableSource(ModelSwapVariableSource& a_source, const char* a_idSuffix)
    {
        bool changed = false;
        ImGui::PushID(a_idSuffix);
        if (ImGui::Checkbox(TextLiteral("使用运行时变量源 (IAD Variable Source)"), &a_source.enabled)) {
            changed = true;
        }
        if (a_source.enabled) {
            ImGui::Indent();
            char pathVariable[64];
            strncpy_s(pathVariable, a_source.pathVariable.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText(TextLiteral("路径变量"), pathVariable, sizeof(pathVariable))) {
                a_source.pathVariable = pathVariable;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("##pathVariablePick", TextLiteral("选择"))) {
                const auto variables = ConfigManager::GetSingleton()->GetRuntimeModelPathVariablesSnapshot();
                for (const auto& [name, value] : variables) {
                    if (ImGui::Selectable(name.c_str(), a_source.pathVariable == name)) {
                        a_source.pathVariable = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            char formIDVariable[64];
            strncpy_s(formIDVariable, a_source.formIDVariable.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText(TextLiteral("FormID 变量"), formIDVariable, sizeof(formIDVariable))) {
                a_source.formIDVariable = formIDVariable;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("##formVariablePick", TextLiteral("选择"))) {
                const auto variables = ConfigManager::GetSingleton()->GetRuntimeFormVariablesSnapshot();
                for (const auto& [name, value] : variables) {
                    if (ImGui::Selectable(name.c_str(), a_source.formIDVariable == name)) {
                        a_source.formIDVariable = name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled(TextLiteral("路径变量优先；变量无有效值时保留手动模型路径/FormID。"));
            ImGui::Unindent();
        }
        ImGui::PopID();
        return changed;
    }

}
