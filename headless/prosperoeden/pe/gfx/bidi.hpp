// ProsperoEden - Direction of text: which way each character of a line is drawn.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace pe::gfx::bidi
{

// The Unicode bidirectional algorithm (UAX #9) for a launcher's text: one paragraph, without the
// explicit embedding, override and isolate controls (they are skipped like other invisible
// format characters). Arabic and Hebrew letters run right to left; numbers and Latin words
// inside them run left to right.
//
// resolve() gives every character its embedding level (even: left to right, odd: right to left)
// and returns the paragraph's level: that of its first strong letter, 0 when it has none.
int resolve(const std::vector<char32_t> &text, std::vector<std::uint8_t> *levels);

// The paragraph's level alone: 1 when its first strong letter is Arabic or Hebrew.
int paragraph_level(const std::vector<char32_t> &text);

// The characters' logical indices in the order they are drawn, left to right (rule L2).
std::vector<int> visual_order(const std::vector<std::uint8_t> &levels);

// The bracket drawn for c in right-to-left text: "(" for ")" (rule L4); c itself otherwise.
char32_t mirror(char32_t c);

// A character that attaches to the one before it (an accent, a Thai vowel or tone mark, an
// Arabic vowel sign, a joiner): a line is never broken or cut before it.
bool attaches(char32_t c);

} // namespace pe::gfx::bidi
