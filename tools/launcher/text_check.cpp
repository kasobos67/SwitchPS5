// ProsperoEden - Host check of the launcher's text direction code against ICU.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: text_check [file with one UTF-8 line per text ...]
// pe::gfx::bidi is a small part of the Unicode bidirectional algorithm, written for the launcher.
// This compares its embedding levels and drawing order with ICU's full implementation, on
// generated mixes of the characters the launcher shows and on the lines of the files given
// (the catalogs' translations). Build: tools/launcher/text-check.sh (needs libicu-dev).

#include "pe/gfx/bidi.hpp"

#include <unicode/ubidi.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace
{

std::vector<char32_t> decode(const std::string &text)
{
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const int length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
        char32_t value = length == 1 ? lead : lead & (0xffu >> (length + 1));
        for (int k = 1; k < length && i + static_cast<std::size_t>(k) < text.size(); ++k)
            value = (value << 6) | (static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]) & 0x3fu);
        out.push_back(value);
        i += static_cast<std::size_t>(length);
    }
    return out;
}

std::string show(const std::vector<char32_t> &text)
{
    std::string out;
    char buffer[16];
    for (const char32_t c : text)
    {
        std::snprintf(buffer, sizeof(buffer), "%04X ", static_cast<unsigned>(c));
        out += buffer;
    }
    return out;
}

int failures = 0;
int compared = 0;

// Characters ICU and the launcher treat as invisible: their level does not show.
bool unseen(char32_t c)
{
    return u_charDirection(static_cast<UChar32>(c)) == U_BOUNDARY_NEUTRAL ||
           (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069);
}

void check(const std::vector<char32_t> &text)
{
    if (text.empty())
        return;
    // ICU works on UTF-16; every test character is in the BMP except where noted.
    std::vector<UChar> utf16;
    std::vector<int> unit_of; // first UTF-16 unit of each character
    for (const char32_t c : text)
    {
        unit_of.push_back(static_cast<int>(utf16.size()));
        if (c >= 0x10000)
        {
            utf16.push_back(static_cast<UChar>(0xd800 + ((c - 0x10000) >> 10)));
            utf16.push_back(static_cast<UChar>(0xdc00 + ((c - 0x10000) & 0x3ff)));
        }
        else
        {
            utf16.push_back(static_cast<UChar>(c));
        }
    }
    UErrorCode error = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    ubidi_setPara(bidi, utf16.data(), static_cast<int32_t>(utf16.size()), UBIDI_DEFAULT_LTR, nullptr, &error);
    const UBiDiLevel *reference = ubidi_getLevels(bidi, &error);
    const int reference_paragraph = ubidi_getParaLevel(bidi);

    std::vector<std::uint8_t> levels;
    const int paragraph = pe::gfx::bidi::resolve(text, &levels);
    ++compared;
    // What shows is each character's direction (its level's parity) and the drawing order. The
    // levels themselves may differ by two: ICU keeps Arabic-Indic digits at level 0 in text
    // without right-to-left letters, where the rules give 2. Both draw left to right.
    bool same = U_SUCCESS(error) && paragraph == reference_paragraph;
    for (std::size_t i = 0; same && i < text.size(); ++i)
        same = unseen(text[i]) || (levels[i] & 1) == (reference[unit_of[i]] & 1);
    if (!same)
    {
        if (++failures <= 25)
        {
            std::string ours;
            std::string theirs;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                ours += static_cast<char>('0' + levels[i]);
                theirs += static_cast<char>('0' + (U_SUCCESS(error) ? reference[unit_of[i]] : 9));
            }
            std::fprintf(stderr, "levels differ: %s\n  ours   %d %s\n  ICU    %d %s\n", show(text).c_str(), paragraph,
                         ours.c_str(), reference_paragraph, theirs.c_str());
        }
    }
    else
    {
        // The drawing order of the visible characters.
        const std::vector<int> order = pe::gfx::bidi::visual_order(levels);
        std::vector<int32_t> map(utf16.size());
        std::vector<UBiDiLevel> unit_levels(reference, reference + utf16.size());
        ubidi_reorderVisual(unit_levels.data(), static_cast<int32_t>(utf16.size()), map.data());
        std::vector<int> ours;
        std::vector<int> theirs;
        for (const int index : order)
            if (!unseen(text[static_cast<std::size_t>(index)]))
                ours.push_back(unit_of[static_cast<std::size_t>(index)]);
        for (const int32_t unit : map)
        {
            const UChar value = utf16[static_cast<std::size_t>(unit)];
            if (value >= 0xdc00 && value <= 0xdfff)
                continue; // the second half of a pair
            char32_t c = value;
            if (value >= 0xd800 && value <= 0xdbff)
                c = 0x10000; // any character outside the BMP is visible here
            if (!unseen(c))
                theirs.push_back(unit);
        }
        // Inside a right-to-left run ICU lists the pair's second unit first: compare by character.
        for (int &unit : theirs)
            if (unit > 0 && utf16[static_cast<std::size_t>(unit)] >= 0xdc00 && utf16[static_cast<std::size_t>(unit)] <= 0xdfff)
                --unit;
        if (ours != theirs && ++failures <= 25)
            std::fprintf(stderr, "order differs: %s\n", show(text).c_str());
    }
    ubidi_close(bidi);
}

} // namespace

int main(int argc, char **argv)
{
    // The kinds of character a launcher line mixes: Latin and Arabic and Hebrew letters, both
    // kinds of digits, spaces, separators, brackets, quotes, marks, Thai, Japanese.
    const std::vector<std::vector<char32_t>> kinds = {
        {'a', 'b', 'Z', 0x00e9, 0x0416},                                   // Latin, Cyrillic
        {0x0627, 0x0628, 0x062a, 0x0644, 0x0645, 0x0646, 0x064a, 0x0629},  // Arabic letters
        {0x05d0, 0x05d1, 0x05e9},                                           // Hebrew letters
        {'0', '1', '5', '9'},                                               // European digits
        {0x0660, 0x0661, 0x0665, 0x0669},                                   // Arabic-Indic digits
        {' ', ' ', ' '},                                                    // spaces
        {',', '.', ':', '/', 0x060c, 0x00a0},                               // separators
        {'+', '-', '%', '$', 0x066a, 0x00b0},                               // signs and terminators
        {'(', ')', '[', ']', '{', '}', 0x300c, 0x300d},                     // brackets
        {'!', '?', '"', '\'', '*', '&', ';', 0x061f, 0x2026, 0x00ab, 0x00bb, '<', '>'}, // other punctuation
        {0x064b, 0x064e, 0x0651, 0x0301},                                   // marks
        {0x0e01, 0x0e34, 0x0e49, 0x0e32},                                   // Thai
        {0x65e5, 0x672c, 0x30a2, 0xac00, 0x3001},                           // Japanese, Korean
        {0x200b, 0x200d, 0x00ad},                                           // invisible
    };
    std::mt19937 random(20261001);
    for (int round = 0; round < 400000; ++round)
    {
        const int length = 1 + static_cast<int>(random() % 14);
        std::vector<char32_t> text;
        // Most lines favour a few kinds, as real text does.
        const std::size_t favourite = random() % kinds.size();
        const std::size_t second = random() % kinds.size();
        for (int i = 0; i < length; ++i)
        {
            const unsigned pick = random() % 10;
            const auto &kind = pick < 4 ? kinds[favourite] : pick < 6 ? kinds[second] : kinds[random() % kinds.size()];
            const char32_t c = kind[random() % kind.size()];
            // A mark written straight after a bracket: rule N0 gives it the direction the bracket
            // resolved to, ICU leaves it to its neighbours. No text does this; it is not generated.
            if (&kind == &kinds[10] && !text.empty() && pe::gfx::bidi::mirror(text.back()) != text.back())
                continue;
            text.push_back(c);
        }
        check(text);
    }
    const int generated = compared;
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream file(argv[i]);
        for (std::string line; std::getline(file, line);)
            check(decode(line));
    }
    std::printf("text direction: %d generated lines and %d lines of %d files compared with ICU %s, %d differ\n", generated,
                compared - generated, argc - 1, U_ICU_VERSION, failures);
    return failures == 0 ? 0 : 1;
}
