// ProsperoEden - The launcher's text in the player's language.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Marks English text that is translated where it is used (in tables that cannot call tr()).
// tools/launcher/strings.py collects it for the catalogs.
#define TR(text) text

namespace pe
{

// The code holds the English text. A catalog (ui/lang/<code>.po, gettext's msgid/msgstr pairs)
// gives another language; text a catalog does not have stays English.
class Catalog
{
  public:
    // Replaces the entries with those of a .po file; returns how many it holds.
    std::size_t load(std::string_view po);
    void clear()
    {
        entries_.clear();
    }
    // The translation, or the English text itself.
    std::string_view find(std::string_view english) const;
    std::size_t size() const
    {
        return entries_.size();
    }

  private:
    struct Hash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const
        {
            return std::hash<std::string_view>{}(text);
        }
    };
    std::unordered_map<std::string, std::string, Hash, std::equal_to<>> entries_;
};

// The catalog every tr() reads. Load it before the first screen is built.
Catalog &catalog();

// The catalogs to try for a system language tag, the best first: the tag itself, then the same
// language as written elsewhere ("fr-CA": fr-CA, fr-FR). English ("en-US", "en-GB") needs none.
std::vector<std::string> catalog_candidates(std::string_view tag);

// The text in the player's language. The pointer is the catalog's (or english itself): it stays
// valid until the catalog is loaded again.
const char *tr(const char *english);
std::string tr(const std::string &english);

// Puts values where the pattern says {0}, {1}...: fill(tr("{0} OF {1}"), {"3", "12"}).
std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values);

} // namespace pe
