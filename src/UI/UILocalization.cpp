#include "pch.h"
#include "UILocalization.h"
#include "Data/ConfigManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace {
    using json = nlohmann::json;
    using TranslationTable = std::unordered_map<std::string, std::string>;

    constexpr std::string_view kFallbackLanguageID = "en_US";

    TranslationTable s_currentTranslations;
    TranslationTable s_fallbackTranslations;
    std::vector<IAD::UI::UILanguage> s_availableLanguages;
    std::string s_loadedLanguageID;
    std::string s_currentLanguageName;
    std::string s_languageMenuLabel;
    bool s_languagesDiscovered = false;

    std::filesystem::path GetLocalizationDirectory()
    {
        return std::filesystem::path("Data") / "F4SE" / "Plugins" / "ImmersiveArsenalDisplays" / "Localization";
    }

    bool LoadTranslationFile(const std::filesystem::path& a_path, TranslationTable& a_translations, std::string& a_languageName)
    {
        std::ifstream file(a_path);
        if (!file.is_open()) {
            return false;
        }

        try {
            const auto document = json::parse(file);
            if (!document.is_object() || !document.contains("strings") || !document["strings"].is_object()) {
                return false;
            }

            if (document.contains("name") && document["name"].is_string()) {
                a_languageName = document["name"].get<std::string>();
            }

            for (const auto& [key, value] : document["strings"].items()) {
                if (value.is_string()) {
                    a_translations[key] = value.get<std::string>();
                }
            }
            return true;
        }
        catch (const std::exception& e) {
            REX::WARN("[IAD] Failed to load localization file '{}': {}", a_path.string(), e.what());
            return false;
        }
    }

    const std::string* FindTranslation(const TranslationTable& a_translations, const char* a_key)
    {
        const auto it = a_translations.find(a_key);
        return it != a_translations.end() ? &it->second : nullptr;
    }

    void EnsureLanguagesDiscovered()
    {
        if (s_languagesDiscovered) {
            return;
        }
        s_languagesDiscovered = true;

        const auto directory = GetLocalizationDirectory();
        if (!std::filesystem::exists(directory)) {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            const auto languageID = entry.path().stem().string();
            std::string languageName;
            TranslationTable ignoredTranslations;
            if (!LoadTranslationFile(entry.path(), ignoredTranslations, languageName)) {
                continue;
            }
            if (languageName.empty()) {
                languageName = languageID;
            }
            s_availableLanguages.push_back({ languageID, std::move(languageName) });
        }

        std::sort(s_availableLanguages.begin(), s_availableLanguages.end(), [](const auto& a_left, const auto& a_right) {
            return a_left.id < a_right.id;
        });
    }

    void EnsureCurrentLanguageLoaded()
    {
        const auto languageID = std::string(IAD::UI::GetLanguageID());
        if (s_loadedLanguageID == languageID) {
            return;
        }

        s_loadedLanguageID = languageID;
        s_currentTranslations.clear();
        s_fallbackTranslations.clear();
        s_currentLanguageName.clear();
        s_languageMenuLabel.clear();

        const auto directory = GetLocalizationDirectory();
        const auto currentPath = directory / (languageID + ".json");
        LoadTranslationFile(currentPath, s_currentTranslations, s_currentLanguageName);

        if (languageID != kFallbackLanguageID) {
            std::string ignoredName;
            LoadTranslationFile(directory / (std::string(kFallbackLanguageID) + ".json"), s_fallbackTranslations, ignoredName);
        }

        if (s_currentLanguageName.empty()) {
            if (const auto* name = FindTranslation(s_currentTranslations, "language.name")) {
                s_currentLanguageName = *name;
            }
        }
        if (s_currentLanguageName.empty()) {
            if (const auto* name = FindTranslation(s_fallbackTranslations, "language.name")) {
                s_currentLanguageName = *name;
            }
        }
        if (s_currentLanguageName.empty()) {
            s_currentLanguageName = languageID;
        }
    }
}

namespace IAD::UI {
    std::string_view GetLanguageID()
    {
        return ConfigManager::GetSingleton()->uiLanguage;
    }

    void SetLanguage(std::string_view a_languageID)
    {
        ConfigManager::GetSingleton()->uiLanguage = a_languageID;
    }

    const char* Text(const char* a_key)
    {
        EnsureCurrentLanguageLoaded();
        if (const auto* translation = FindTranslation(s_currentTranslations, a_key)) {
            return translation->c_str();
        }
        if (const auto* translation = FindTranslation(s_fallbackTranslations, a_key)) {
            return translation->c_str();
        }
        return a_key;
    }

    const char* TextLiteral(const char* a_fallbackText)
    {
        EnsureCurrentLanguageLoaded();

        std::string key = "literal.";
        key += a_fallbackText ? a_fallbackText : "";
        if (const auto* translation = FindTranslation(s_currentTranslations, key.c_str())) {
            return translation->c_str();
        }
        if (const auto* translation = FindTranslation(s_fallbackTranslations, key.c_str())) {
            return translation->c_str();
        }
        return a_fallbackText ? a_fallbackText : "";
    }

    const char* GetLanguageMenuLabel()
    {
        EnsureCurrentLanguageLoaded();
        if (const auto* format = FindTranslation(s_currentTranslations, "language.menu_label")) {
            s_languageMenuLabel = *format;
        }
        else if (const auto* format = FindTranslation(s_fallbackTranslations, "language.menu_label")) {
            s_languageMenuLabel = *format;
        }
        else {
            s_languageMenuLabel = "language.menu_label";
        }

        const std::string placeholder = "{language}";
        const auto position = s_languageMenuLabel.find(placeholder);
        if (position != std::string::npos) {
            s_languageMenuLabel.replace(position, placeholder.size(), s_currentLanguageName);
        }
        return s_languageMenuLabel.c_str();
    }

    const std::vector<UILanguage>& GetAvailableLanguages()
    {
        EnsureLanguagesDiscovered();
        return s_availableLanguages;
    }
}
