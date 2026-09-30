// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(push_constant) uniform Text { uint glyphs[24]; uint width; uint x; uint y; uint loading; } text;
layout(location = 0) out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) - ivec2(text.x, text.y);
    // Vulkan push-constant arrays require dynamically uniform indices. Load
    // each word at a constant index before the per-pixel local-array lookup.
    uint glyphs[24] = uint[24](
        text.glyphs[0], text.glyphs[1], text.glyphs[2], text.glyphs[3],
        text.glyphs[4], text.glyphs[5], text.glyphs[6], text.glyphs[7],
        text.glyphs[8], text.glyphs[9], text.glyphs[10], text.glyphs[11],
        text.glyphs[12], text.glyphs[13], text.glyphs[14], text.glyphs[15],
        text.glyphs[16], text.glyphs[17], text.glyphs[18], text.glyphs[19],
        text.glyphs[20], text.glyphs[21], text.glyphs[22], text.glyphs[23]);
    bool ink = false;
    if (p.x >= 0 && p.y >= 0) {
        ivec2 cell = p / 4;
        int i = cell.x / 4;
        int x = cell.x % 4;
        if (i < 24 && x < 3 && cell.y < 5)
            ink = ((glyphs[i] >> uint((4 - cell.y) * 3 + 2 - x)) & 1u) != 0u;
    }
    color = text.loading != 0u ?
        (ink ? vec4(0.7216, 0.9490, 0.0471, 1.0) : vec4(0.0196, 0.0392, 0.0039, 1.0)) :
        (ink ? vec4(0.9, 0.95, 1.0, 1.0) : vec4(0.025, 0.04, 0.075, 0.5));
}
