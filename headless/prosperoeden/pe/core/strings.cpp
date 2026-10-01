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

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values)
{
    std::string out;
    out.reserve(pattern.size() + 16);
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
            pattern[i + 1] >= '0' && pattern[i + 1] <= '9')
        {
            const std::size_t index = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (index < values.size())
                out.append(values.begin()[index]);
            i += 2;
            continue;
        }
        out.push_back(pattern[i]);
    }
    return out;
}

} // namespace pe
