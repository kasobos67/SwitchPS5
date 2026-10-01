// ProsperoEden - Direction of text: which way each character of a line is drawn.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/gfx/bidi.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace pe::gfx::bidi
{

namespace
{

// The bidirectional character types of UAX #9 that are left without explicit controls.
enum Class : std::uint8_t
{
    L,   // left-to-right letter
    R,   // right-to-left letter (Hebrew)
    AL,  // Arabic letter
    EN,  // European digit
    ES,  // plus, minus
    ET,  // percent, currency, degree
    AN,  // Arabic-Indic digit
    CS,  // comma, full stop, colon, slash
    NSM, // mark that attaches to the character before it
    BN,  // invisible format character
    B,   // paragraph separator
    S,   // tab
    WS,  // space
    ON,  // other punctuation and symbols
};

struct Range
{
    char32_t first;
    char32_t last;
    Class type;
};

// Everything that is not a left-to-right letter, from U+0080 up, in ascending order. Blocks the
// launcher cannot show at all (it has no font for them) are left as letters.
constexpr Range kRanges[] = {
    {0x0080, 0x0084, BN},   {0x0085, 0x0085, B},    {0x0086, 0x009f, BN},   {0x00a0, 0x00a0, CS},
    {0x00a1, 0x00a1, ON},   {0x00a2, 0x00a5, ET},   {0x00a6, 0x00a9, ON},   {0x00ab, 0x00ac, ON},
    {0x00ad, 0x00ad, BN},   {0x00ae, 0x00af, ON},   {0x00b0, 0x00b1, ET},   {0x00b2, 0x00b3, EN},
    {0x00b4, 0x00b4, ON},   {0x00b6, 0x00b8, ON},   {0x00b9, 0x00b9, EN},   {0x00bb, 0x00bf, ON},
    {0x00d7, 0x00d7, ON},   {0x00f7, 0x00f7, ON},   {0x02b9, 0x02ba, ON},   {0x02c2, 0x02cf, ON},
    {0x02d2, 0x02df, ON},   {0x02e5, 0x02ed, ON},   {0x02ef, 0x02ff, ON},   {0x0300, 0x036f, NSM},
    {0x0374, 0x0375, ON},   {0x037e, 0x037e, ON},   {0x0384, 0x0385, ON},   {0x0387, 0x0387, ON},
    {0x03f6, 0x03f6, ON},   {0x0483, 0x0489, NSM},  {0x058a, 0x058a, ON},   {0x058d, 0x058e, ON},
    {0x058f, 0x058f, ET},   {0x0590, 0x0590, R},    {0x0591, 0x05bd, NSM},  {0x05be, 0x05be, R},
    {0x05bf, 0x05bf, NSM},  {0x05c0, 0x05c0, R},    {0x05c1, 0x05c2, NSM},  {0x05c3, 0x05c3, R},
    {0x05c4, 0x05c5, NSM},  {0x05c6, 0x05c6, R},    {0x05c7, 0x05c7, NSM},  {0x05c8, 0x05ff, R},
    {0x0600, 0x0605, AN},   {0x0606, 0x0607, ON},   {0x0608, 0x0608, AL},   {0x0609, 0x060a, ET},
    {0x060b, 0x060b, AL},   {0x060c, 0x060c, CS},   {0x060d, 0x060d, AL},   {0x060e, 0x060f, ON},
    {0x0610, 0x061a, NSM},  {0x061b, 0x061b, AL},   {0x061c, 0x061c, BN},   {0x061d, 0x064a, AL},
    {0x064b, 0x065f, NSM},  {0x0660, 0x0669, AN},   {0x066a, 0x066a, ET},   {0x066b, 0x066c, AN},
    {0x066d, 0x066f, AL},   {0x0670, 0x0670, NSM},  {0x0671, 0x06d5, AL},   {0x06d6, 0x06dc, NSM},
    {0x06dd, 0x06dd, AN},   {0x06de, 0x06de, ON},   {0x06df, 0x06e4, NSM},  {0x06e5, 0x06e6, AL},
    {0x06e7, 0x06e8, NSM},  {0x06e9, 0x06e9, ON},   {0x06ea, 0x06ed, NSM},  {0x06ee, 0x06ef, AL},
    {0x06f0, 0x06f9, EN},   {0x06fa, 0x07bf, AL},   {0x07c0, 0x085f, R},    {0x0860, 0x08d2, AL},
    {0x08d3, 0x08e1, NSM},  {0x08e2, 0x08e2, AN},   {0x08e3, 0x08ff, NSM},  {0x0e31, 0x0e31, NSM},
    {0x0e34, 0x0e3a, NSM},  {0x0e3f, 0x0e3f, ET},   {0x0e47, 0x0e4e, NSM},  {0x0eb1, 0x0eb1, NSM},
    {0x0eb4, 0x0ebc, NSM},  {0x0ec8, 0x0ece, NSM},  {0x1ab0, 0x1aff, NSM},  {0x1dc0, 0x1dff, NSM},
    {0x2000, 0x200a, WS},   {0x200b, 0x200d, BN},   {0x200f, 0x200f, R},    {0x2010, 0x2027, ON},
    {0x2028, 0x2028, WS},   {0x2029, 0x2029, B},    {0x202a, 0x202e, BN},   {0x202f, 0x202f, CS},
    {0x2030, 0x2034, ET},   {0x2035, 0x2043, ON},   {0x2044, 0x2044, CS},   {0x2045, 0x205e, ON},
    {0x205f, 0x205f, WS},   {0x2060, 0x206f, BN},   {0x2070, 0x2070, EN},   {0x2074, 0x2079, EN},
    {0x207a, 0x207b, ES},   {0x207c, 0x207e, ON},   {0x2080, 0x2089, EN},   {0x208a, 0x208b, ES},
    {0x208c, 0x208e, ON},   {0x20a0, 0x20cf, ET},   {0x20d0, 0x20ff, NSM},  {0x2100, 0x2101, ON},
    {0x2103, 0x2106, ON},   {0x2108, 0x2109, ON},   {0x2114, 0x2114, ON},   {0x2116, 0x2118, ON},
    {0x211e, 0x2123, ON},   {0x2125, 0x2125, ON},   {0x2127, 0x2127, ON},   {0x2129, 0x2129, ON},
    {0x212e, 0x212e, ET},   {0x213a, 0x213b, ON},   {0x2140, 0x2144, ON},   {0x214a, 0x214d, ON},
    {0x2150, 0x215f, ON},   {0x2189, 0x218b, ON},   {0x2190, 0x2211, ON},   {0x2212, 0x2212, ES},
    {0x2213, 0x2213, ET},   {0x2214, 0x2335, ON},   {0x237b, 0x2394, ON},   {0x2396, 0x2487, ON},
    {0x2488, 0x249b, EN},   {0x24ea, 0x26ab, ON},   {0x26ad, 0x27ff, ON},   {0x2900, 0x2bff, ON},
    {0x2e00, 0x2e7f, ON},   {0x2e80, 0x2fdf, ON},   {0x2ff0, 0x2fff, ON},   {0x3000, 0x3000, WS},
    {0x3001, 0x3004, ON},   {0x3008, 0x3020, ON},   {0x302a, 0x302d, NSM},  {0x3030, 0x3030, ON},
    {0x3036, 0x3037, ON},   {0x303d, 0x303f, ON},   {0x3099, 0x309a, NSM},  {0x309b, 0x309c, ON},
    {0x30a0, 0x30a0, ON},   {0x30fb, 0x30fb, ON},   {0x31c0, 0x31e3, ON},   {0x321d, 0x321e, ON},
    {0x3250, 0x325f, ON},   {0x327c, 0x327e, ON},   {0x32b1, 0x32bf, ON},   {0x32cc, 0x32cf, ON},
    {0x3377, 0x337a, ON},   {0x33de, 0x33df, ON},   {0x33ff, 0x33ff, ON},   {0x4dc0, 0x4dff, ON},
    {0xfb1d, 0xfb1d, R},    {0xfb1e, 0xfb1e, NSM},  {0xfb1f, 0xfb28, R},    {0xfb29, 0xfb29, ES},
    {0xfb2a, 0xfb4f, R},    {0xfb50, 0xfd3d, AL},   {0xfd3e, 0xfd3f, ON},   {0xfd40, 0xfdff, AL},
    {0xfe00, 0xfe0f, NSM},  {0xfe10, 0xfe19, ON},   {0xfe20, 0xfe2f, NSM},  {0xfe30, 0xfe4f, ON},
    {0xfe50, 0xfe50, CS},   {0xfe51, 0xfe51, ON},   {0xfe52, 0xfe52, CS},   {0xfe54, 0xfe54, ON},
    {0xfe55, 0xfe55, CS},   {0xfe56, 0xfe5e, ON},   {0xfe5f, 0xfe5f, ET},   {0xfe60, 0xfe61, ON},
    {0xfe62, 0xfe63, ES},   {0xfe64, 0xfe66, ON},   {0xfe68, 0xfe68, ON},   {0xfe69, 0xfe6a, ET},
    {0xfe6b, 0xfe6b, ON},   {0xfe70, 0xfefe, AL},   {0xfeff, 0xfeff, BN},   {0xff01, 0xff02, ON},
    {0xff03, 0xff05, ET},   {0xff06, 0xff0a, ON},   {0xff0b, 0xff0b, ES},   {0xff0c, 0xff0c, CS},
    {0xff0d, 0xff0d, ES},   {0xff0e, 0xff0f, CS},   {0xff10, 0xff19, EN},   {0xff1a, 0xff1a, CS},
    {0xff1b, 0xff20, ON},   {0xff3b, 0xff40, ON},   {0xff5b, 0xff65, ON},   {0xffe0, 0xffe1, ET},
    {0xffe2, 0xffe4, ON},   {0xffe5, 0xffe6, ET},   {0xffe8, 0xffee, ON},   {0xfff9, 0xfffb, ON},
    {0xfffc, 0xfffd, ON},   {0x1f000, 0x1faff, ON}, {0xe0100, 0xe01ef, NSM},
};

Class classify(char32_t c)
{
    if (c < 0x80)
    {
        if (c >= '0' && c <= '9')
            return EN;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            return L;
        switch (c)
        {
        case '\n':
        case '\r':
        case 0x1c:
        case 0x1d:
        case 0x1e:
            return B;
        case '\t':
        case 0x0b:
        case 0x1f:
            return S;
        case ' ':
        case 0x0c:
            return WS;
        case '+':
        case '-':
            return ES;
        case '#':
        case '$':
        case '%':
            return ET;
        case ',':
        case '.':
        case '/':
        case ':':
            return CS;
        default:
            return c < 0x20 || c == 0x7f ? BN : ON;
        }
    }
    const Range *found = std::upper_bound(std::begin(kRanges), std::end(kRanges), c,
                                          [](char32_t value, const Range &range) { return value < range.first; });
    if (found != std::begin(kRanges) && c <= (found - 1)->last)
        return (found - 1)->type;
    return L;
}

// Paired brackets (rule N0) and the other characters with a mirror image (rule L4).
constexpr char32_t kBrackets[][2] = {
    {'(', ')'},       {'[', ']'},       {'{', '}'},       {0x2045, 0x2046}, {0x207d, 0x207e},
    {0x208d, 0x208e}, {0x2308, 0x2309}, {0x230a, 0x230b}, {0x2329, 0x232a}, {0x2768, 0x2769},
    {0x276a, 0x276b}, {0x276c, 0x276d}, {0x276e, 0x276f}, {0x2770, 0x2771}, {0x2772, 0x2773},
    {0x2774, 0x2775}, {0x27e6, 0x27e7}, {0x27e8, 0x27e9}, {0x27ea, 0x27eb}, {0x2983, 0x2984},
    {0x2985, 0x2986}, {0x3008, 0x3009}, {0x300a, 0x300b}, {0x300c, 0x300d}, {0x300e, 0x300f},
    {0x3010, 0x3011}, {0x3014, 0x3015}, {0x3016, 0x3017}, {0x3018, 0x3019}, {0x301a, 0x301b},
    {0xfe59, 0xfe5a}, {0xfe5b, 0xfe5c}, {0xfe5d, 0xfe5e}, {0xff08, 0xff09}, {0xff3b, 0xff3d},
    {0xff5b, 0xff5d}, {0xff5f, 0xff60}, {0xff62, 0xff63},
};
constexpr char32_t kMirrored[][2] = {
    {'<', '>'}, {0x00ab, 0x00bb}, {0x2039, 0x203a}, {0x2264, 0x2265}, {0xff1c, 0xff1e},
};

// 0: no bracket; 1: opening (its closing one in *other); 2: closing (its opening one in *other).
int bracket(char32_t c, char32_t *other)
{
    for (const auto &pair : kBrackets)
    {
        if (pair[0] == c)
        {
            *other = pair[1];
            return 1;
        }
        if (pair[1] == c)
        {
            *other = pair[0];
            return 2;
        }
    }
    return 0;
}

bool neutral(Class type)
{
    return type == B || type == S || type == WS || type == ON;
}

// The direction a resolved type counts as around neutrals: numbers side with right-to-left.
Class strong(Class type)
{
    return type == L ? L : R;
}

bool is_strong(Class type)
{
    return type == L || type == R || type == EN || type == AN;
}

} // namespace

int paragraph_level(const std::vector<char32_t> &text)
{
    // P2, P3
    for (const char32_t c : text)
    {
        const Class type = classify(c);
        if (type == L)
            return 0;
        if (type == R || type == AL)
            return 1;
    }
    return 0;
}

int resolve(const std::vector<char32_t> &text, std::vector<std::uint8_t> *levels)
{
    const std::size_t count = text.size();
    std::vector<Class> original(count);
    bool right_to_left = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        original[i] = classify(text[i]);
        right_to_left = right_to_left || original[i] == R || original[i] == AL || original[i] == AN;
    }
    // P2, P3: the paragraph runs the way of its first strong letter.
    int paragraph = 0;
    for (const Class type : original)
    {
        if (type == L)
            break;
        if (type == R || type == AL)
        {
            paragraph = 1;
            break;
        }
    }
    levels->assign(count, static_cast<std::uint8_t>(paragraph));
    if (!right_to_left)
        return paragraph;

    const Class embedding = paragraph != 0 ? R : L; // also the type before and after the text
    // X9: invisible format characters take no part; `at` lists the others.
    std::vector<std::size_t> at;
    at.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        if (original[i] != BN)
            at.push_back(i);
    const std::size_t length = at.size();
    std::vector<Class> type(length);
    for (std::size_t k = 0; k < length; ++k)
        type[k] = original[at[k]];

    // W1: a mark takes the type of the character it sits on.
    for (std::size_t k = 0; k < length; ++k)
        if (type[k] == NSM)
            type[k] = k == 0 ? embedding : type[k - 1];
    // W2: European digits after Arabic letters are Arabic numbers.
    {
        Class last = embedding;
        for (std::size_t k = 0; k < length; ++k)
        {
            if (type[k] == L || type[k] == R || type[k] == AL)
                last = type[k];
            else if (type[k] == EN && last == AL)
                type[k] = AN;
        }
    }
    // W3
    for (Class &value : type)
        if (value == AL)
            value = R;
    // W4: one separator between two numbers of a kind belongs to them.
    for (std::size_t k = 1; k + 1 < length; ++k)
    {
        if (type[k] == ES && type[k - 1] == EN && type[k + 1] == EN)
            type[k] = EN;
        else if (type[k] == CS && type[k - 1] == type[k + 1] && (type[k - 1] == EN || type[k - 1] == AN))
            type[k] = type[k - 1];
    }
    // W5: terminators next to European digits belong to them.
    for (std::size_t k = 0; k < length;)
    {
        if (type[k] != ET)
        {
            ++k;
            continue;
        }
        std::size_t end = k;
        while (end < length && type[end] == ET)
            ++end;
        if ((k > 0 && type[k - 1] == EN) || (end < length && type[end] == EN))
            std::fill(type.begin() + static_cast<std::ptrdiff_t>(k), type.begin() + static_cast<std::ptrdiff_t>(end), EN);
        k = end;
    }
    // W6
    for (Class &value : type)
        if (value == ES || value == ET || value == CS)
            value = ON;
    // W7: European digits after a left-to-right letter are part of that text.
    {
        Class last = embedding;
        for (std::size_t k = 0; k < length; ++k)
        {
            if (type[k] == L || type[k] == R)
                last = type[k];
            else if (type[k] == EN && last == L)
                type[k] = L;
        }
    }

    // N0: a pair of brackets takes one direction, from what it encloses or what precedes it.
    {
        struct Open
        {
            char32_t closing;
            std::size_t position;
        };
        std::vector<Open> stack;
        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        bool overflow = false;
        for (std::size_t k = 0; k < length && !overflow; ++k)
        {
            if (type[k] != ON)
                continue;
            char32_t other = 0;
            const int kind = bracket(text[at[k]], &other);
            if (kind == 1)
            {
                if (stack.size() == 63)
                    overflow = true;
                else
                    stack.push_back({other, k});
            }
            else if (kind == 2)
            {
                for (std::size_t depth = stack.size(); depth-- > 0;)
                {
                    if (stack[depth].closing == text[at[k]])
                    {
                        pairs.emplace_back(stack[depth].position, k);
                        stack.resize(depth);
                        break;
                    }
                }
            }
        }
        if (overflow)
            pairs.clear();
        std::sort(pairs.begin(), pairs.end());
        const Class opposite = embedding == L ? R : L;
        for (const auto &[open, close] : pairs)
        {
            bool with = false;
            bool against = false;
            for (std::size_t k = open + 1; k < close; ++k)
            {
                if (!is_strong(type[k]))
                    continue;
                if (strong(type[k]) == embedding)
                    with = true;
                else
                    against = true;
            }
            Class decided = ON;
            if (with)
                decided = embedding;
            else if (against)
            {
                Class before = embedding;
                for (std::size_t k = open; k-- > 0;)
                {
                    if (is_strong(type[k]))
                    {
                        before = strong(type[k]);
                        break;
                    }
                }
                decided = before == opposite ? opposite : embedding;
            }
            if (decided == ON)
                continue;
            for (const std::size_t position : {open, close})
            {
                type[position] = decided;
                // Marks on the bracket follow it.
                for (std::size_t k = position + 1; k < length && original[at[k]] == NSM; ++k)
                    type[k] = decided;
            }
        }
    }
    // N1, N2: other neutrals take the direction both neighbours share, else the paragraph's.
    for (std::size_t k = 0; k < length;)
    {
        if (!neutral(type[k]))
        {
            ++k;
            continue;
        }
        std::size_t end = k;
        while (end < length && neutral(type[end]))
            ++end;
        const Class before = k == 0 ? embedding : strong(type[k - 1]);
        const Class after = end == length ? embedding : strong(type[end]);
        std::fill(type.begin() + static_cast<std::ptrdiff_t>(k), type.begin() + static_cast<std::ptrdiff_t>(end),
                  before == after ? before : embedding);
        k = end;
    }
    // I1, I2: levels from the resolved types.
    for (std::size_t k = 0; k < length; ++k)
    {
        int level = paragraph;
        if (paragraph == 0)
            level += type[k] == R ? 1 : type[k] == AN || type[k] == EN ? 2 : 0;
        else
            level += type[k] == L || type[k] == EN || type[k] == AN ? 1 : 0;
        (*levels)[at[k]] = static_cast<std::uint8_t>(level);
    }
    // Invisible characters go with the character before them.
    for (std::size_t i = 0; i < count; ++i)
        if (original[i] == BN)
            (*levels)[i] = i == 0 ? static_cast<std::uint8_t>(paragraph) : (*levels)[i - 1];
    // L1: separators, and spaces before them or at the end, sit at the paragraph's level.
    bool trailing = true;
    for (std::size_t i = count; i-- > 0;)
    {
        const Class value = original[i];
        if (value == B || value == S)
        {
            (*levels)[i] = static_cast<std::uint8_t>(paragraph);
            trailing = true;
        }
        else if (value == WS || value == BN)
        {
            if (trailing)
                (*levels)[i] = static_cast<std::uint8_t>(paragraph);
        }
        else
        {
            trailing = false;
        }
    }
    return paragraph;
}

std::vector<int> visual_order(const std::vector<std::uint8_t> &levels)
{
    const std::size_t count = levels.size();
    std::vector<int> order(count);
    std::uint8_t highest = 0;
    std::uint8_t lowest_odd = 255;
    for (std::size_t i = 0; i < count; ++i)
    {
        order[i] = static_cast<int>(i);
        highest = std::max(highest, levels[i]);
        if ((levels[i] & 1) != 0)
            lowest_odd = std::min(lowest_odd, levels[i]);
    }
    if (lowest_odd == 255)
        return order;
    // L2: from the highest level down to the lowest odd one, reverse every run at that level or above.
    for (int level = highest; level >= lowest_odd; --level)
    {
        for (std::size_t i = 0; i < count;)
        {
            if (levels[static_cast<std::size_t>(order[i])] < level)
            {
                ++i;
                continue;
            }
            std::size_t end = i;
            while (end < count && levels[static_cast<std::size_t>(order[end])] >= level)
                ++end;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(end));
            i = end;
        }
    }
    return order;
}

char32_t mirror(char32_t c)
{
    char32_t other = 0;
    if (bracket(c, &other) != 0)
        return other;
    for (const auto &pair : kMirrored)
    {
        if (pair[0] == c)
            return pair[1];
        if (pair[1] == c)
            return pair[0];
    }
    return c;
}

bool attaches(char32_t c)
{
    if (c < 0x300)
        return false;
    // Thai and Lao SARA AM: a spacing vowel written as part of the syllable before it.
    if (c == 0x0e33 || c == 0x0eb3 || c == 0x200d)
        return true;
    return classify(c) == NSM;
}

} // namespace pe::gfx::bidi
