#pragma once

#include <array>
#include <cstdint>
#include <string_view>

extern "C" int sceSystemServiceParamGetInt(int parameter_id, int* value);

namespace ps5::i18n {

enum class SystemLanguage : std::int32_t {
    Japanese = 0,
    EnglishUs = 1,
    French = 2,
    Spanish = 3,
    German = 4,
    Italian = 5,
    Dutch = 6,
    PortuguesePortugal = 7,
    Russian = 8,
    Korean = 9,
    ChineseTraditional = 10,
    ChineseSimplified = 11,
    Finnish = 12,
    Swedish = 13,
    Danish = 14,
    Norwegian = 15,
    Polish = 16,
    PortugueseBrazil = 17,
    EnglishUk = 18,
    Turkish = 19,
    SpanishLatinAmerica = 20,
    Arabic = 21,
    FrenchCanada = 22,
    Czech = 23,
    Hungarian = 24,
    Greek = 25,
    Romanian = 26,
    Thai = 27,
    Vietnamese = 28,
    Indonesian = 29,
    Ukrainian = 30,
};

inline constexpr int kSystemLanguageParameter = 1;
inline constexpr std::string_view kFallbackLanguage = "en-US";

inline constexpr std::array<std::string_view, 31> kLanguageTags = {
    "ja-JP", "en-US", "fr-FR", "es-ES", "de-DE", "it-IT", "nl-NL", "pt-PT",
    "ru-RU", "ko-KR", "zh-Hant", "zh-Hans", "fi-FI", "sv-SE", "da-DK", "nb-NO",
    "pl-PL", "pt-BR", "en-GB", "tr-TR", "es-419", "ar", "fr-CA", "cs-CZ",
    "hu-HU", "el-GR", "ro-RO", "th-TH", "vi-VN", "id-ID", "uk-UA",
};

constexpr std::string_view language_tag(
    std::int32_t language,
    std::string_view fallback = kFallbackLanguage) noexcept
{
    const auto index = static_cast<std::uint32_t>(language);
    return index < kLanguageTags.size() ? kLanguageTags[index] : fallback;
}

inline std::string_view current_language_tag(
    std::string_view fallback = kFallbackLanguage) noexcept
{
    int language = 0;
    return sceSystemServiceParamGetInt(kSystemLanguageParameter, &language) == 0
        ? language_tag(language, fallback)
        : fallback;
}

} // namespace ps5::i18n

