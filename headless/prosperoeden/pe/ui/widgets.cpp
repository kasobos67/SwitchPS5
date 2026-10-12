// ProsperoEden - Launcher building blocks: backdrop, panels, plates, lists and hints.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/widgets.hpp"
#include "pe/ui/stock_motion.hpp"
#include "pe/ui/stock_assets.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pe::ui
{

namespace
{

FitReport fit_report = nullptr;

// A text colour as shown: with high contrast, greys go most of the way to white and accents a
// little, and faded text is less faded.
Color text_ink(Color color)
{
    if (!look().high_contrast)
        return color;
    if (look().white_theme) return {color.r * .25f, color.g * .25f, color.b * .25f, 1.0f};
    const float high = std::max({color.r, color.g, color.b});
    const float low = std::min({color.r, color.g, color.b});
    const float lift = high - low > 0.24f ? 0.25f : 0.70f;
    return {color.r + (1.0f - color.r) * lift, color.g + (1.0f - color.g) * lift,
            color.b + (1.0f - color.b) * lift, 1.0f - (1.0f - color.a) * 0.5f};
}

// Draws at exactly `drawn` on the line laid out for text of `size`.
float draw_text(Canvas &c, std::string_view value, float x, float baseline, float size, float drawn,
                Color color, Align align, float tracking)
{
    gfx::TextStyle style;
    style.size = drawn;
    style.color = text_ink(color);
    style.align = align;
    style.tracking = tracking;
    return c.list.text(*c.fonts.font, c.fonts.texture, value, x, baseline + (drawn - size) * 0.35f,
                       style);
}
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
const Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};

} // namespace

// ---------------------------------------------------------------- backdrop

void Backdrop::update(float dt)
{
    // With reduced motion the art stands still.
    time_ += dt * motion();
}

Rect Backdrop::art() const
{
return {0.0f, 0.0f, 1920.0f, 1080.0f};
}

Rect Backdrop::uv(const Rect &r) const
{
    const Rect a = art();
    return {(r.x - a.x) / a.w, (r.y - a.y) / a.h, r.w / a.w, r.h / a.h};
}

void Backdrop::draw(gfx::DrawList &list, const Textures &textures, float dim) const
{
(void)textures; (void)dim;
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f,
                      look().high_contrast && !look().white_theme ? Color::rgb(0x181818) : theme::kBase);
}

// ---------------------------------------------------------------- text

float text(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
           Align align, float tracking)
{
    return draw_text(c, value, x, baseline, size, text_size(size), color, align, tracking);
}

float text_width(Canvas &c, std::string_view value, float size, float tracking)
{
    return c.fonts.font->measure(value, text_size(size), tracking);
}

float text_fit(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
               float max_width, Align align)
{
    const float drawn = text_size(size);
    return draw_text(c, c.fonts.font->fit(value, drawn, max_width), x, baseline, size, drawn, color,
                     align, 0.0f);
}

void text_marquee(Canvas &c, std::string_view value, const Rect &r, float size, Color color,
                  float elapsed, Align align)
{
    const float width = text_width(c, value, size);
    if (width <= r.w) { text(c,value,align == Align::center ? r.x+r.w*.5f : r.x,baseline(r.y,r.h,size),size,color,align); return; }
    if (look().reduce_motion) {
        text_fit(c,value,align == Align::center ? r.x+r.w*.5f : r.x,baseline(r.y,r.h,size),size,color,r.w,align);
        return;
    }
    const float distance = width-r.w;
    const float travel = distance/72.0f;
    const float cycle = 2.4f+travel+1.0f;
    const float phase = std::fmod(std::max(0.0f,elapsed),cycle);
    const float offset = look().reduce_motion ? 0.0f : std::clamp((phase-2.4f)*72.0f,0.0f,distance);
    c.list.push_clip(r);
    text(c,value,r.x-offset,baseline(r.y,r.h,size),size,color);
    c.list.pop_clip();
}

void set_fit_report(FitReport report)
{
    fit_report = report;
}

float text_shrink(Canvas &c, std::string_view value, float x, float baseline, float size,
                  Color color, float max_width, Align align, float tracking, float least)
{
    const gfx::Font &font = *c.fonts.font;
    const float drawn = text_size(size);
    const float width = font.measure(value, drawn, tracking);
    if (width <= max_width)
        return draw_text(c, value, x, baseline, size, drawn, color, align, tracking);
    // Larger text gives its extra size back before anything is cut.
    const float scale = std::max(least * size / drawn, max_width / width);
    if (fit_report != nullptr)
        fit_report(value, scale, width * scale > max_width + 0.5f);
    return draw_text(c, font.fit(value, drawn * scale, max_width + 0.5f, tracking * scale), x,
                     baseline, size, drawn * scale, color, align, tracking * scale);
}

void text_block(Canvas &c, std::string_view value, float x, float first_baseline, float size,
                float line_height, Color color, float max_width, int max_lines, float least)
{
    const gfx::Font &font = *c.fonts.font;
    const float full = text_size(size);
    float drawn = full;
    std::vector<std::string> lines = font.wrap(value, drawn, max_width);
    // A step smaller at a time, until the text fits its lines or the letters are as small as
    // allowed.
    // (Larger text gives its extra size back before anything is cut.)
    const float smallest = size * least;
    while (static_cast<int>(lines.size()) > max_lines && drawn > smallest + 0.01f)
    {
        drawn = std::max(smallest, drawn * 0.94f);
        lines = font.wrap(value, drawn, max_width);
    }
    if (fit_report != nullptr && drawn < full)
        fit_report(value, drawn / full, static_cast<int>(lines.size()) > max_lines);
    const int count = std::min(static_cast<int>(lines.size()), max_lines);
    for (int line = 0; line < count; ++line)
    {
        const bool cut = line == count - 1 && static_cast<int>(lines.size()) > max_lines;
        // The last line that fits swallows the next one, so its ellipsis shows more follows.
        const std::string content =
            !cut ? lines[line] :
            lines[line] + (gfx::joins_without_space(lines[line], lines[line + 1]) ? "" : " ") + lines[line + 1];
        draw_text(c, font.fit(content, drawn, max_width), x,
                  first_baseline + line_height * static_cast<float>(line), size, drawn, color,
                  Align::left, 0.0f);
    }
}

namespace
{

// The warning mark with its left edge at x, centred on the line of text of `size` at `baseline`.
// Returns the room it takes, the gap to the text included.
float warning_mark(Canvas &c, float x, float baseline, float size, Color color, bool draw = true)
{
    const float drawn = text_size(size);
    const float radius = drawn * 0.46f;
    if (draw)
    {
        const Color ink = text_ink(color);
        const float cx = x + radius;
        const float cy = baseline - size * 0.35f;
        c.list.ring(cx, cy, radius - 0.5f, drawn * 0.085f, ink);
        c.list.line(cx, cy - radius * 0.50f, cx, cy + radius * 0.10f, drawn * 0.11f, ink);
        c.list.circle(cx, cy + radius * 0.48f, drawn * 0.065f, ink);
    }
    return radius * 2.0f + drawn * 0.42f;
}

} // namespace

float notice(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
             float max_width, bool warning, Align align)
{
    if (!warning || value.empty())
        return text_shrink(c, value, x, baseline, size, color, max_width, align);
    const float mark = warning_mark(c, x, baseline, size, color, false);
    if (align == Align::right)
    {
        const float width = text_shrink(c, value, x, baseline, size, color, max_width - mark, align);
        warning_mark(c, x - width - mark, baseline, size, color);
        return width + mark;
    }
    warning_mark(c, x, baseline, size, color);
    return mark + text_shrink(c, value, x + mark, baseline, size, color, max_width - mark, align);
}

void notice_block(Canvas &c, std::string_view value, float x, float first_baseline, float size,
                  float line_height, Color color, float max_width, int max_lines, bool warning)
{
    const float mark = warning && !value.empty() ? warning_mark(c, x, first_baseline, size, color) : 0.0f;
    text_block(c, value, x + mark, first_baseline, size, line_height, color, max_width - mark,
               max_lines, kShrink);
}

float value_column(Canvas &c, std::initializer_list<std::string_view> labels, float left,
                   float column, float size, float tracking)
{
    float widest = 0.0f;
    for (const std::string_view label : labels)
        widest = std::max(widest, text_width(c, label, size, tracking));
    return std::max(column, left + widest + 24.0f);
}

// ---------------------------------------------------------------- surfaces

void glass(Canvas &c, const Rect &r, float radius, Color tint, Color edge, float shadow)
{
    const float curvature=radius == 0.0f ? 0.0f : std::min(radius,7.5f);
    if (shadow > 0) c.list.shadow({r.x,r.y+1.5f,r.w,r.h},curvature,6.0f,Color::rgb(0,c.dialog_shadow*std::min(1.0f,shadow)));
    c.list.bordered_rect(r,curvature,tint,edge.a > .01f ? 1.0f : 0.0f,edge);
}

const Plate kRowPlate{2.0f, theme::kRow, theme::kRowEdge.with_alpha(0.0f),
    theme::kRowFocus, {}, {}, theme::kLime};
const Plate kListPlate = kRowPlate;
const Plate kButtonPlate = kRowPlate;
const Plate kTilePlate = kRowPlate;
const Plate kNavPlate{2.0f, Color::rgb(0x000000, 0.0f), Color::rgb(0x000000, 0.0f),
    theme::kRowFocus, {}, {}, theme::kLime};

void plate_rest(Canvas &c, const Plate &style, const Rect &r)
{
if (style.fill.a > 0.0f) c.list.rounded_rect(r, style.radius, theme::kRow);
    c.list.line(r.x + 1.0f, r.y + r.h - 1.0f, r.x + r.w - 1.0f, r.y + r.h - 1.0f,
                1.0f, theme::kRule.with_alpha(0.45f));
}

void plate_focus(Canvas &c, const Plate &style, const Rect &r, float amount)
{
if (amount <= 0.001f) return;
    const Color edge = look().high_contrast ? theme::kTitle : theme::kLime;
    c.list.bordered_rect(r, style.radius, theme::kRowFocus.with_alpha(amount),
                        look().high_contrast ? 4.0f : 3.0f, edge.with_alpha(amount));
}

void plate(Canvas &c, const Plate &style, const Rect &r, float focus)
{
    plate_rest(c, style, r);
    plate_focus(c, style, r, focus);
}

void cover(Canvas &c, const std::string &path, const Rect &r, float radius, float shadow)
{
    if (shadow > 0.0f)
        c.list.shadow({r.x, r.y + 1.5f, r.w, r.h}, radius,
                      7.5f, kBlack.with_alpha(0.4f * shadow));
    const Cover image = c.textures.cover(path, r.w);
    const float fade = image.texture != 0 ? 1.0f : 0.0f;
    if (fade < 1.0f)
    {
        // A neutral stock loading pane. Permanently missing artwork stays blank;
        // it must not advertise the app's brand or pretend loading is ongoing.
        c.list.rounded_rect(r,radius,theme::kRowFocus);
        if (!image.missing && c.textures.stock_ui())
        {
            const float size=60.0f;
            c.list.image(c.textures.stock_ui(),{r.x+(r.w-size)*.5f,r.y+(r.h-size)*.5f,size,size},
                         stock_assets::wait_uv(c.time*60.0f*motion()),theme::kMeta);
        }
    }
    if (image.texture != 0)
        c.list.rounded_image(image.texture, r, {0.0f, 0.0f, 1.0f, 1.0f}, radius,
                             kWhite.with_alpha(fade));
    c.list.bordered_rect(r, radius, kWhite.with_alpha(0.0f), 1.0f, kWhite.with_alpha(0.10f));
}

void stock_focus(Canvas &c, const Rect &r, float amount, float elapsed, bool circular)
{
    if (amount <= 0.001f) return;
    const float frame=look().reduce_motion ? 0.0f : c.time*60.0f;
    const float strength=stock_motion::cursor_glow_effect(frame);
    const Color edge=look().high_contrast ? theme::kTitle : theme::kLime;
    const Rect box{r.x-7.5f,r.y-7.5f,r.w+15,r.h+15};
    const float radius=circular ? box.w*.5f : 3.0f;
    // The renderer's shadow primitive includes its interior. Clip the glow to
    // the area outside the card so selecting artwork never darkens its pixels.
    const Rect glow_clips[]={{box.x-18,box.y-18,box.w+36,r.y-box.y+18},
        {box.x-18,r.y+r.h,box.w+36,box.y+box.h+18-r.y-r.h},
        {box.x-18,r.y,r.x-box.x+18,r.h},
        {r.x+r.w,r.y,box.x+box.w+18-r.x-r.w,r.h}};
    for(const auto &clip:glow_clips) {
        c.list.push_clip(clip);
        c.list.shadow({box.x,box.y+4.5f,box.w,box.h},radius,6,
                      Color::rgb(0x15516e,(80.0f/255.0f)*amount*(.5f+strength)));
        c.list.pop_clip();
    }
    c.list.bordered_rect(box,radius,Color::rgb(0,0),7.5f,edge.with_alpha(amount));
    if(!circular && c.textures.stock_ui()) {
        const Rect edges[]={{box.x,box.y,box.w,7.5f},{box.x,box.y+box.h-7.5f,box.w,7.5f},
            {box.x,box.y+7.5f,7.5f,box.h-15},{box.x+box.w-7.5f,box.y+7.5f,7.5f,box.h-15}};
        for(const auto &strip:edges) {
            c.list.push_clip(strip);
            c.list.image(c.textures.stock_ui(),box,stock_assets::cursor_texture_uv(frame),
                         Color::rgb(0xffffff,amount*.55f*stock_motion::cursor_main_effect(frame)));
            c.list.pop_clip();
        }
        const float alphas[]={stock_motion::cursor_tl_alpha(frame),stock_motion::cursor_tr_alpha(frame),stock_motion::cursor_bl_alpha(frame),stock_motion::cursor_br_alpha(frame)};
        const float x[]={box.x,box.x+box.w-24,box.x,box.x+box.w-24};
        const float y[]={box.y,box.y,box.y+box.h-7.5f,box.y+box.h-7.5f};
        const auto corner=[&](int i) { return Color::rgb(0xffffff,alphas[i]/255.0f*amount*.42f); };
        c.list.hgradient_rect({box.x,box.y,box.w,7.5f},1.5f,corner(0),corner(1));
        c.list.hgradient_rect({box.x,box.y+box.h-7.5f,box.w,7.5f},1.5f,corner(2),corner(3));
        c.list.gradient_rect({box.x,box.y,7.5f,box.h},1.5f,corner(0),corner(2));
        c.list.gradient_rect({box.x+box.w-7.5f,box.y,7.5f,box.h},1.5f,corner(1),corner(3));
        (void)x; (void)y;
    }
    (void)elapsed;
}

void applet_symbol(Canvas &c, int symbol, float x, float y, Color ink)
{
    if (!c.textures.stock_ui())
        return;
    // About, Game Files, Multiplayer and Controllers use the user's artwork,
    // including the original Joy-Con icon. Their transparent
    // canvases match the HOME circle, which is drawn by the caller.
    constexpr stock_assets::Icon icons[] = {
        stock_assets::Icon::news, stock_assets::Icon::shop,
        stock_assets::Icon::multiplayer, stock_assets::Icon::joycon,
        stock_assets::Icon::settings, stock_assets::Icon::power,
    };
    const int index = std::clamp(symbol, 0, 5);
    const float size = index < 4 ? 132.0f : 96.0f;
    c.list.image(c.textures.stock_ui(), {x - size * .5f, y - size * .5f, size, size},
                 stock_assets::icon_uv(icons[index]), ink);
}

void controller_icon(Canvas &c, const Rect &r, float lit)
{
    const std::uint32_t picture = c.textures.controller();
    if (picture == 0)
        return;
    c.list.image(picture, r, {0.0f, 0.0f, 1.0f, 1.0f},
                 theme::kTitle.with_alpha(.16f+.84f*lit));
}

void toggle(Canvas &c, float right, float cy, float position)
{
    text(c,position >= .5f ? tr("On") : tr("Off"),right,cy+28*.35f,28,position >= .5f ? theme::kLime : theme::kMeta,Align::right);
}

void level_bar(Canvas &c, float right, float cy, float width, float level, float focus)
{
    constexpr float kHeight = 8.0f;
    const Rect track{right - width, cy - kHeight * 0.5f, width, kHeight};
    c.list.rounded_rect(track, kHeight * 0.5f, Color::rgb(0x555555, 0.95f));
    const float filled = std::max(kHeight, width * std::clamp(level, 0.0f, 1.0f));
    if (level > 0.0f)
        c.list.hgradient_rect({track.x, track.y, filled, kHeight}, kHeight * 0.5f, theme::kLimeDeep,
                              theme::kLime);
    const float knob = 7.0f + 3.0f * focus;
    c.list.shadow({track.x + filled - knob, cy - knob + 2.0f, knob * 2.0f, knob * 2.0f}, knob, 5.0f,
                  kBlack.with_alpha(0.35f));
    c.list.circle(track.x + (level > 0.0f ? filled : 0.0f), cy, knob, theme::kTitle);
}

float chooser(Canvas &c, std::string_view value, float right, float baseline, float focus,
              Color color)
{
    const float size = theme::kText24;
    const float arrow = 26.0f * focus; // room the chevrons take when focused
    const float width = text(c, value, right - arrow, baseline, size, color, Align::right);
    if (focus <= 0.01f)
        return width;
    const float cy = baseline - size * 0.35f;
    const Color ink = theme::kLimePale.with_alpha(focus);
    const float rx = right - 4.0f;
    c.list.line(rx - 7.0f, cy - 8.0f, rx, cy, 2.2f, ink);
    c.list.line(rx, cy, rx - 7.0f, cy + 8.0f, 2.2f, ink);
    const float lx = right - arrow - width - 16.0f;
    c.list.line(lx + 7.0f, cy - 8.0f, lx, cy, 2.2f, ink);
    c.list.line(lx, cy, lx + 7.0f, cy + 8.0f, 2.2f, ink);
    return width + arrow + 16.0f;
}

// ---------------------------------------------------------------- controller hints

float pad_width(Pad button, float size)
{
    switch (button)
    {
    case Pad::none:
        return 0.0f;
    case Pad::l1:
    case Pad::r1:
        return size * 1.45f;
    default:
        return size;
    }
}

void draw_pad(Canvas &c, Pad button, float x, float cy, float size, float alpha)
{
    const float width = pad_width(button, size);
    const float cx = x + width * 0.5f;
    const float half = size * 0.5f;
    const bool bold = look().high_contrast;
    const bool face=button==Pad::cross || button==Pad::circle || button==Pad::square || button==Pad::triangle;
    const bool stock_face=face && c.textures.stock_icons()!=0;
    if (stock_face)
        c.list.image(c.textures.stock_icons(),{x,cy-half,size,size},
                     {192.0f/384.0f,64.0f/128.0f,32.0f/384.0f,32.0f/128.0f},theme::kText.with_alpha(alpha));
    const Color ring = theme::kText.with_alpha(stock_face ? 0.0f : (bold ? 0.85f : 0.40f) * alpha);
    const Color ink = (stock_face ? theme::kBase : theme::kText).with_alpha((bold ? 1.0f : 0.92f) * alpha);
    const Color dim = theme::kText.with_alpha((bold ? 0.45f : 0.30f) * alpha);
    const float stroke = size * 0.085f;
    switch (button)
    {
    case Pad::none:
        return;
    case Pad::options:
        c.list.ring(cx,cy,half-.5f,1.6f,theme::kText.with_alpha(alpha));
        for(int i=-1;i<=1;++i) c.list.line(cx-size*.2f,cy+i*size*.14f,cx+size*.2f,cy+i*size*.14f,stroke,theme::kText.with_alpha(alpha));
        return;
    case Pad::cross:
    {
        const float d = size * 0.17f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.line(cx - d, cy - d, cx + d, cy + d, stroke, ink);
        c.list.line(cx - d, cy + d, cx + d, cy - d, stroke, ink);
        return;
    }
    case Pad::circle:
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.ring(cx, cy, size * 0.21f, stroke, ink);
        return;
    case Pad::square:
    {
        const float side = size * 0.36f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.bordered_rect({cx - side * 0.5f, cy - side * 0.5f, side, side}, 1.5f,
                             ink.with_alpha(0.0f), stroke, ink);
        return;
    }
    case Pad::triangle:
    {
        const float w = size * 0.46f;
        const float h = size * 0.40f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.triangle({cx - w * 0.5f, cy - h * 0.60f, w, h}, ink, stroke);
        return;
    }
    case Pad::dpad:
    case Pad::updown:
    case Pad::leftright:
    {
        if(c.textures.stock_icons()!=0) {
            const float u=button==Pad::dpad ? 0.0f : button==Pad::updown ? 32.0f : 64.0f;
            c.list.image(c.textures.stock_icons(),{x,cy-half,size,size},
                         {u/384.0f,64.0f/128.0f,32.0f/384.0f,32.0f/128.0f},ink);
            return;
        }
        const float arm = size * 0.36f;
        const float thick = size * 0.26f;
        const Color vertical = button == Pad::leftright ? dim : ink;
        const Color horizontal = button == Pad::updown ? dim : ink;
        c.list.rounded_rect({cx - arm, cy - thick * 0.5f, arm * 2.0f, thick}, thick * 0.28f,
                            horizontal);
        c.list.rounded_rect({cx - thick * 0.5f, cy - arm, thick, arm * 2.0f}, thick * 0.28f,
                            vertical);
        if (button != Pad::dpad)
        {
            // Redraw the bright arm over the crossing so it reads as one piece.
            if (button == Pad::updown)
                c.list.rounded_rect({cx - thick * 0.5f, cy - arm, thick, arm * 2.0f},
                                    thick * 0.28f, ink);
            else
                c.list.rounded_rect({cx - arm, cy - thick * 0.5f, arm * 2.0f, thick},
                                    thick * 0.28f, ink);
        }
        return;
    }
    case Pad::l1:
    case Pad::r1:
    {
        const float height = size * 0.80f;
        c.list.bordered_rect({x, cy - height * 0.5f, width, height}, height * 0.30f,
                             ink.with_alpha(0.0f), 1.6f, ring);
        draw_text(c, button == Pad::l1 ? "L1" : "R1", cx, cy + size * 0.17f, size * 0.48f,
                  size * 0.48f, ink, Align::center, 0.0f);
        return;
    }
    }
}

namespace
{
constexpr float kHintIcon=36.0f;
constexpr float kHintText=30.0f;
constexpr float kHintGap=10.0f;
constexpr float kHintItemGap=34.0f;
}
float hints_width(Canvas &c, const Hint *hints, int count)
{
    float width=0;
    for(int i=0;i<count;++i) {
        width+=pad_width(hints[i].button,kHintIcon)+kHintGap+text_width(c,tr(hints[i].label),kHintText);
        if(hints[i].second!=Pad::none) width+=6+pad_width(hints[i].second,kHintIcon);
        if(i+1<count) width+=kHintItemGap;
    }
    return width;
}
float draw_hints(Canvas &c, const Hint *hints, int count, float x, float cy, Color color, float max_width)
{
    const float width=hints_width(c,hints,count);
    float cursor=x;
    for(int i=0;i<count;++i) {
        draw_pad(c,hints[i].button,cursor,cy,kHintIcon);
        cursor+=pad_width(hints[i].button,kHintIcon);
        if(hints[i].second!=Pad::none) { cursor+=6; draw_pad(c,hints[i].second,cursor,cy,kHintIcon); cursor+=pad_width(hints[i].second,kHintIcon); }
        cursor+=kHintGap;
        cursor+=text(c,tr(hints[i].label),cursor,cy+kHintText*.35f,kHintText,color);
        if(i+1<count) cursor+=kHintItemGap;
    }
    (void)max_width; // Stock prompts retain their size; callers allocate an appropriate guide region.
    return width;
}
void draw_hints_right(Canvas &c, const Hint *hints, int count, float right, float cy, Color color)
{
    draw_hints(c,hints,count,right-hints_width(c,hints,count),cy,color);
}

// ---------------------------------------------------------------- lists

std::string list_position(int selected, int count)
{
    return fill(tr("{0} OF {1}"), {std::to_string(selected), std::to_string(count)});
}

void ListView::follow()
{
    const int max_top = std::max(0, count - visible);
    if (selected < top_)
        top_ = selected;
    if (selected > top_ + visible - 1)
        top_ = selected - (visible - 1);
    top_ = std::clamp(top_, 0, max_top);
    scroll_.target = static_cast<float>(top_) * pitch;
    cursor_.target = static_cast<float>(selected) * pitch;
}

void ListView::reset(int new_count, int new_selected)
{
    count = std::max(0, new_count);
    selected = std::clamp(new_selected, 0, std::max(0, count - 1));
    // Open with the selection a few rows down when the list is long enough.
    top_ = std::max(0, selected - visible / 2);
    follow();
    scroll_.snap(scroll_.target);
    cursor_.snap(cursor_.target);
}

bool ListView::move(int delta)
{
    if (count <= 0)
        return false;
    int next = selected + delta;
    if (wrap)
        next = (next % count + count) % count;
    else
        next = std::clamp(next, 0, count - 1);
    if (next == selected)
        return false;
    selected = next;
    follow();
    return true;
}

bool ListView::page(int delta)
{
    if (count <= 0)
        return false;
    const int next = std::clamp(selected + delta * visible, 0, count - 1);
    if (next == selected)
        return false;
    selected = next;
    follow();
    return true;
}

void ListView::update(float dt)
{
    scroll_.update(dt, theme::kScrollSpring);
    cursor_.update(dt, theme::kCursorSpring);
}

int ListView::first_row() const
{
    return std::max(0, static_cast<int>(std::floor(scroll_.value / pitch)));
}

int ListView::last_row() const
{
    return std::min(count - 1, static_cast<int>(std::floor(scroll_.value / pitch)) + visible);
}

float ListView::row_alpha(int row, float row_height) const
{
    const float y = static_cast<float>(row) * pitch - scroll_.value;
    const float window = static_cast<float>(visible - 1) * pitch + row_height;
    const float above = tween::clamp01((y + row_height) / row_height);
    const float below = tween::clamp01((window - y) / row_height);
    return std::min(above, below);
}

float ListView::thumb() const
{
    const float range = static_cast<float>(std::max(1, count - visible)) * pitch;
    return tween::clamp01(scroll_.value / range);
}

void scrollbar(Canvas &c, const ListView &view, float x, float y, float height)
{
    if (view.count <= view.visible)
        return;
    c.list.rounded_rect({x, y, 10.0f, height}, 5.0f, Color::rgb(0x444444));
    const float thumb = std::max(
        64.0f, height * static_cast<float>(view.visible) / static_cast<float>(view.count));
    c.list.rounded_rect({x, y + (height - thumb) * view.thumb(), 10.0f, thumb}, 5.0f,
                        Color::rgb(0xaaaaaa));
}


// Stock-like HOME shortcut symbols; each remains wired to a real launcher action.
void home_symbol(Canvas &c, int symbol, float x, float y, Color ink)
{
    auto &l = c.list;
    const std::uint32_t stock = c.textures.stock_icons();
    if (stock != 0 && (symbol == 0 || symbol == 1))
    {
        // The next atlas row begins at y=64. Keep linear sampling inside this
        // sprite so its button glyphs cannot bleed beneath All Software.
        const Rect uv = symbol == 0 ? Rect{0, 0, 64.0f/384.0f, 63.0f/128.0f} :
                                     Rect{128.0f/384.0f, 0, 40.0f/384.0f, 40.0f/128.0f};
        l.image(stock, {x-30, y-30, 60, 60}, uv, ink);
        return;
    }
    if(stock!=0 && symbol==2) {
        l.image(stock,{x-30,y-30,60,60},{160.0f/384.0f,64.0f/128.0f,32.0f/384.0f,32.0f/128.0f},ink);
        return;
    }
    if (symbol == 0) { // all software
        for (int i=0; i<4; ++i) l.bordered_rect({x-22.0f+(i%2)*26.0f,y-22.0f+(i/2)*26.0f,18.0f,18.0f},
                                                2.0f, ink.with_alpha(0.0f), 2.5f, ink);
    } else if (symbol == 1) { // settings
        l.ring(x,y,17.0f,3.0f,ink); l.ring(x,y,6.0f,3.0f,ink);
        for (int i=0; i<8; ++i) { float a=6.2831853f*i/8;
            l.line(x+19*std::cos(a),y+19*std::sin(a),x+25*std::cos(a),y+25*std::sin(a),5.0f,ink); }
    } else if (symbol == 2) { // information
        l.ring(x,y,23.0f,2.5f,ink); l.circle(x,y-11,2.8f,ink); l.line(x,y-2,x,y+12,3.0f,ink);
    } else { // options
        for (int i=0; i<3; ++i) { float yy=y-16+i*16;
            l.line(x-22,yy,x+22,yy,2.5f,ink); l.circle(x+(i==1?-8:8),yy,5,ink); }
    }
}

void arc(gfx::DrawList &list, float cx, float cy, float radius, float width, float start, float sweep, Color color)
{
    if (sweep <= 0.001f)
        return;
    const int steps = std::max(2, static_cast<int>(sweep / (3.14159265f / 60.0f)));
    float x = cx + radius * std::cos(start);
    float y = cy + radius * std::sin(start);
    for (int i = 1; i <= steps; ++i)
    {
        const float a = start + sweep * static_cast<float>(i) / static_cast<float>(steps);
        const float nx = cx + radius * std::cos(a);
        const float ny = cy + radius * std::sin(a);
        list.line(x, y, nx, ny, width, color);
        x = nx;
        y = ny;
    }
}

} // namespace pe::ui
