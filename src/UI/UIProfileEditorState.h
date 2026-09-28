#pragma once

#include <array>
#include <string>
#include <string_view>

namespace IAD::UI {
    struct UIProfileEditorState {
        std::string selected;
        std::string status;
        std::array<char, 64> name{};
        std::array<char, 64> filter{};
        std::array<char, 512> description{};
        std::string lastSelected;
    };

    class UIProfileEditorStateStore final {
    public:
        static UIProfileEditorState& Get(std::string_view a_id);
    };
}
