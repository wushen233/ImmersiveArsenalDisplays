#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace IAD::UI {
    struct UILanguage {
        std::string id;
        std::string name;
    };

    std::string_view GetLanguageID();
    void SetLanguage(std::string_view a_languageID);
    const char* Text(const char* a_key);
    // Resolve a user-facing source literal through the external locale files.
    // The literal itself remains the fallback so old/missing locale files stay usable.
    const char* TextLiteral(const char* a_fallbackText);
    const char* GetLanguageMenuLabel();
    const std::vector<UILanguage>& GetAvailableLanguages();
}
