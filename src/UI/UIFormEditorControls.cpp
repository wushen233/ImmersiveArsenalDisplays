#include "pch.h"
#include "UILocalization.h"
#include "UIFormEditorControls.h"

#include "Data/ConfigManager.h"

#include <RE/E/ENUM_FORM_ID.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <cstring>

namespace IAD::UI {

    void UIFormEditorControls::DrawFormSetUI(std::set<std::uint32_t>& a_set, const char* a_id)
    {
        ImGui::PushID(a_id);
        if (ImGui::BeginPopupContextItem("Context_AddForm")) {
            static char hexInput[16] = "";
            ImGui::Text(TextLiteral("输入 FormID (16进制, 例如 14):"));
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputText("##HexInput", hexInput, 16, ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::Button(TextLiteral("添加 (Add)"), { -1, 0 })) {
                if (strlen(hexInput) > 0) {
                    try {
                        const auto formID = std::stoul(hexInput, nullptr, 16);
                        a_set.insert(formID);
                        hexInput[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                    catch (...) {
                    }
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(TextLiteral("⚙️ 操作 (右键此处添加物品)"))) {
            ImGui::OpenPopup("Context_AddForm");
        }
        if (ImGui::BeginTable("FormTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn(TextLiteral("操作"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(TextLiteral("物品名称"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            std::uint32_t toRemove = 0;
            bool doRemove = false;
            for (const auto formID : a_set) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(formID);
                if (ImGui::Button("X")) {
                    toRemove = formID;
                    doRemove = true;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%08X", formID);
                ImGui::TableNextColumn();
                auto form = RE::TESForm::GetFormByID(formID);
                if (form) {
                    auto fullName = form->As<RE::TESFullName>();
                    if (fullName && fullName->GetFullName()) {
                        ImGui::Text("%s", fullName->GetFullName());
                    }
                    else {
                        ImGui::TextDisabled(TextLiteral("[未知名称]"));
                    }
                }
                else {
                    ImGui::TextColored({ 1, 0, 0, 1 }, TextLiteral("[未加载]"));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (doRemove) {
                a_set.erase(toRemove);
            }
        }
        ImGui::PopID();
    }

    bool UIFormEditorControls::DrawFormVectorUI(std::vector<std::uint32_t>& a_vec, const char* a_id)
    {
        bool changed = false;
        ImGui::PushID(a_id);
        if (ImGui::BeginPopupContextItem("Context_AddFormVec")) {
            static char hexInput[16] = "";
            ImGui::Text(TextLiteral("输入 FormID (16进制):"));
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputText("##HexInputVec", hexInput, 16, ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::Button(TextLiteral("添加 (Add)"), { -1, 0 })) {
                if (strlen(hexInput) > 0) {
                    try {
                        const auto formID = std::stoul(hexInput, nullptr, 16);
                        if (std::find(a_vec.begin(), a_vec.end(), formID) == a_vec.end()) {
                            a_vec.push_back(formID);
                            changed = true;
                        }
                        hexInput[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                    catch (...) {
                    }
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(TextLiteral("⚙️ 操作 (右键此处添加物品)"))) {
            ImGui::OpenPopup("Context_AddFormVec");
        }
        if (ImGui::BeginTable("FormVecTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn(TextLiteral("操作"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(TextLiteral("排序"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(TextLiteral("物品名称"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < a_vec.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button("X")) {
                    a_vec.erase(a_vec.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::TableNextColumn();
                if (ImGui::Button("^") && i > 0) {
                    std::swap(a_vec[i], a_vec[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("v") && i < a_vec.size() - 1) {
                    std::swap(a_vec[i], a_vec[i + 1]);
                    changed = true;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%08X", a_vec[i]);
                ImGui::TableNextColumn();
                auto form = RE::TESForm::GetFormByID(a_vec[i]);
                if (form) {
                    auto fullName = form->As<RE::TESFullName>();
                    if (fullName && fullName->GetFullName()) {
                        ImGui::Text("%s", fullName->GetFullName());
                    }
                    else {
                        ImGui::TextDisabled(TextLiteral("[未知名称]"));
                    }
                }
                else {
                    ImGui::TextColored({ 1, 0, 0, 1 }, TextLiteral("[未加载]"));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        return changed;
    }

    bool UIFormEditorControls::DrawFormIDField(const char* a_label, std::uint32_t& a_formID)
    {
        bool changed = false;
        char formBuf[16];
        sprintf_s(formBuf, "%08X", a_formID);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText(a_label, formBuf, 16, ImGuiInputTextFlags_CharsHexadecimal)) {
            try {
                a_formID = std::stoul(formBuf, nullptr, 16);
                changed = true;
            }
            catch (...) {
                a_formID = 0;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (auto form = RE::TESForm::GetFormByID(a_formID)) {
            if (auto fullName = form->As<RE::TESFullName>(); fullName && fullName->GetFullName()) {
                ImGui::TextDisabled("%s", fullName->GetFullName());
            }
            else if (auto editorID = form->GetFormEditorID(); editorID && editorID[0] != '\0') {
                ImGui::TextDisabled("%s", editorID);
            }
            else {
                ImGui::TextDisabled(TextLiteral("[已加载]"));
            }
        }
        else {
            ImGui::TextDisabled(TextLiteral("[未加载]"));
        }
        return changed;
    }

    bool UIFormEditorControls::DrawStringVectorEditor(const char* a_label, std::vector<std::string>& a_values, const char* a_defaultValue)
    {
        bool changed = false;
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                ImGui::PushID(i);
                char buf[128];
                strcpy_s(buf, a_values[i].c_str());
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::InputText("##nodeName", buf, 128)) {
                    a_values[i] = buf;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(TextLiteral("上移")) && i > 0) {
                    std::swap(a_values[i], a_values[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0) {
                a_values.erase(a_values.begin() + removeIndex);
                changed = true;
            }
            if (ImGui::Button(TextLiteral("添加"))) {
                a_values.push_back(a_defaultValue ? a_defaultValue : "");
                changed = true;
            }
            ImGui::TreePop();
        }
        return changed;
    }

    bool UIFormEditorControls::DrawFormTypeVectorEditor(const char* a_label, std::vector<std::uint8_t>& a_values)
    {
        bool changed = false;
        const char* labels[] = { "WEAP", "ARMO", "AMMO", "ALCH", "MISC" };
        const std::uint8_t types[] = {
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kWEAP),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kARMO),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kAMMO),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kALCH),
            static_cast<std::uint8_t>(RE::ENUM_FORM_ID::kMISC)
        };

        ImGui::PushID(a_label);
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < static_cast<int>(std::size(types)); ++i) {
                bool enabled = std::find(a_values.begin(), a_values.end(), types[i]) != a_values.end();
                if (ImGui::Checkbox(labels[i], &enabled)) {
                    if (enabled) {
                        if (std::find(a_values.begin(), a_values.end(), types[i]) == a_values.end()) {
                            a_values.push_back(types[i]);
                        }
                    }
                    else {
                        a_values.erase(std::remove(a_values.begin(), a_values.end(), types[i]), a_values.end());
                    }
                    changed = true;
                }
                if (i + 1 < static_cast<int>(std::size(types))) {
                    ImGui::SameLine();
                }
            }
            ImGui::NewLine();

            if (!a_values.empty()) {
                ImGui::TextDisabled("Evaluation order:");
                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                    ImGui::PushID(i);
                    ImGui::Text("%d. %s", i + 1, ConfigManager::FormTypeToString(a_values[i]).c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Up") && i > 0) {
                        std::swap(a_values[i], a_values[i - 1]);
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Down") && i + 1 < static_cast<int>(a_values.size())) {
                        std::swap(a_values[i], a_values[i + 1]);
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        removeIndex = i;
                    }
                    ImGui::PopID();
                }
                if (removeIndex >= 0) {
                    a_values.erase(a_values.begin() + removeIndex);
                    changed = true;
                }
            }
            else {
                ImGui::TextDisabled("Empty list uses global recent-acquired form types.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

    static std::uint32_t NormalizeUIBipedSlot(int a_value)
    {
        if (a_value < 0) {
            a_value = 0;
        }
        if (a_value < 32) {
            return static_cast<std::uint32_t>(a_value + 30);
        }
        if (a_value < 30) {
            a_value = 30;
        }
        if (a_value > 61) {
            a_value = 61;
        }
        return static_cast<std::uint32_t>(a_value);
    }

    bool UIFormEditorControls::DrawBipedSlotVectorEditor(const char* a_label, std::vector<std::uint32_t>& a_values)
    {
        bool changed = false;
        ImGui::PushID(a_label);
        if (ImGui::TreeNodeEx(a_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(a_values.size()); ++i) {
                ImGui::PushID(i);
                int slot = static_cast<int>(a_values[i]);
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputInt("Slot", &slot, 1, 1)) {
                    a_values[i] = NormalizeUIBipedSlot(slot);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("index %u", a_values[i] >= 30 ? a_values[i] - 30 : a_values[i]);
                ImGui::SameLine();
                if (ImGui::Button("Up") && i > 0) {
                    std::swap(a_values[i], a_values[i - 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && i + 1 < static_cast<int>(a_values.size())) {
                    std::swap(a_values[i], a_values[i + 1]);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0) {
                a_values.erase(a_values.begin() + removeIndex);
                changed = true;
            }
            if (ImGui::Button("Add biped slot")) {
                if (std::find(a_values.begin(), a_values.end(), 33u) == a_values.end()) {
                    a_values.push_back(33);
                }
                else {
                    a_values.push_back(30);
                }
                changed = true;
            }
            ImGui::TextDisabled("FO4 biped slots use 30-61. Manual 0-31 input is normalized to 30-61.");
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }

}
