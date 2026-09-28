#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace IAD::UI {

    class UIFormEditorControls final {
    public:
        static void DrawFormSetUI(std::set<std::uint32_t>& a_set, const char* a_id);
        static bool DrawFormVectorUI(std::vector<std::uint32_t>& a_vec, const char* a_id);
        static bool DrawFormIDField(const char* a_label, std::uint32_t& a_formID);
        static bool DrawStringVectorEditor(const char* a_label, std::vector<std::string>& a_values, const char* a_defaultValue);
        static bool DrawFormTypeVectorEditor(const char* a_label, std::vector<std::uint8_t>& a_values);
        static bool DrawBipedSlotVectorEditor(const char* a_label, std::vector<std::uint32_t>& a_values);
    };

}
