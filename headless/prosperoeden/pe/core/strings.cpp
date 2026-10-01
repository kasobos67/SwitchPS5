// ProsperoEden - The launcher's text in the player's language.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/core/strings.hpp"

namespace pe
{

namespace
{

// The text between the quotes of one .po line, with its escapes resolved; false when the line
// holds no quoted text.
bool quoted(std::string_view line, std::string *out)
{
    const std::size_t open = line.find('"');
    const std::size_t close = line.rfind('"');
    if (open == std::string_view::npos || close <= open)
        return false;
    for (std::size_t i = open + 1; i < close; ++i)
    {
        char c = line[i];
        if (c == '\\' && i + 1 < close)
        {
            c = line[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        out->push_back(c);
    }
    return true;
}

} // namespace

std::size_t Catalog::load(std::string_view po)
{
    entries_.clear();
    // A UTF-8 byte order mark is not text.
    if (po.substr(0, 3) == "\xef\xbb\xbf")
        po.remove_prefix(3);
    std::string id;
    std::string text;
    enum class Part
    {
        none,
        id,
        text,
    } part = Part::none;
    const auto finish = [&]
    {
        if (!id.empty() && !text.empty())
            entries_[id] = text;
        id.clear();
        text.clear();
    };
    while (!po.empty())
    {
        const std::size_t end = po.find('\n');
        std::string_view line = po.substr(0, end);
        po.remove_prefix(end == std::string_view::npos ? po.size() : end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        if (line.empty() || line.front() == '#')
            continue;
        if (line.substr(0, 6) == "msgid ")
        {
            finish();
            part = Part::id;
            quoted(line, &id);
        }
        else if (line.substr(0, 7) == "msgstr ")
        {
            part = Part::text;
            quoted(line, &text);
        }
        else if (line.front() == '"')
        {
            // A continued string.
            if (part == Part::id)
                quoted(line, &id);
            else if (part == Part::text)
                quoted(line, &text);
        }
    }
    finish();
    return entries_.size();
}

std::string_view Catalog::find(std::string_view english) const
{
    const auto found = entries_.find(english);
    return found != entries_.end() ? std::string_view{found->second} : english;
}

Catalog &catalog()
{
    static Catalog instance;
    return instance;
}

std::vector<std::string> catalog_candidates(std::string_view tag)
{
    // The same language as written in another place, when its own catalog is missing.
    static constexpr std::string_view kRelated[][2] = {
        {"fr-CA", "fr-FR"},  {"fr-FR", "fr-CA"}, {"es-419", "es-ES"},
        {"es-ES", "es-419"}, {"pt-PT", "pt-BR"}, {"pt-BR", "pt-PT"},
    };
    std::vector<std::string> result;
    if (tag.empty() || tag.substr(0, 2) == "en")
        return result;
    result.emplace_back(tag);
    for (const auto &pair : kRelated)
        if (pair[0] == tag)
            result.emplace_back(pair[1]);
    return result;
}

const char *tr(const char *english)
{
    const std::string_view text = catalog().find(english);
    // A translation is one of the catalog's strings: it ends in a zero like the English text.
    return text.data();
}

std::string tr(const std::string &english)
{
    return std::string{catalog().find(english)};
}

namespace
{

// Whether text holds a letter written right to left (Hebrew, Arabic).
bool right_to_left(std::string_view text)
{
    for (std::size_t i = 0; i + 1 < text.size(); ++i)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const unsigned char next = static_cast<unsigned char>(text[i + 1]);
        // U+0590-U+08FF are the two-byte sequences D6 90 to DF BF and the three-byte E0 A0-A3;
        // the Arabic presentation forms U+FB1D-U+FEFC start with EF AC-BB.
        if ((lead == 0xd6 && next >= 0x90) || (lead >= 0xd7 && lead <= 0xdf) ||
            (lead == 0xe0 && next >= 0xa0 && next <= 0xa3) || (lead == 0xef && next >= 0xac && next <= 0xbb))
            return true;
    }
    return false;
}

bool has_latin_letter(std::string_view text)
{
    for (const char c : text)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            return true;
    return false;
}

} // namespace

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values)
{
    // In a right-to-left sentence a left-to-right value (a path, a name, an English sentence)
    // keeps its own punctuation on its own side when it stands between left-to-right marks
    // (U+200E): "/data/logs" would otherwise show its first slash at the wrong end.
    static constexpr std::string_view kMark = "\xE2\x80\x8E";
    const bool mark = right_to_left(pattern);
    std::string out;
    out.reserve(pattern.size() + 16);
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
            pattern[i + 1] >= '0' && pattern[i + 1] <= '9')
        {
            const std::size_t index = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (index < values.size())
            {
                const std::string_view value = values.begin()[index];
                const bool wrap = mark && has_latin_letter(value) && !right_to_left(value);
                if (wrap)
                    out.append(kMark);
                out.append(value);
                if (wrap)
                    out.append(kMark);
            }
            i += 2;
            continue;
        }
        out.push_back(pattern[i]);
    }
    return out;
}

} // namespace pe
