#include "pch.h"
#include "UIProfileEditorState.h"

#include <map>

namespace IAD::UI {
    UIProfileEditorState& UIProfileEditorStateStore::Get(std::string_view a_id)
    {
        static std::map<std::string, UIProfileEditorState> states;
        return states[std::string(a_id)];
    }
}
