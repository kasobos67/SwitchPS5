// ProsperoEden - Launcher preview on a PC: draws the real screens to PNG files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: pe_preview <ui assets dir> <output dir> [width height]
//        pe_preview <ui assets dir> <output dir> --tour [width height]   (raw RGBA frames on stdout)
// Renders with the launcher's own draw list, shaders and font through Mesa's
// surfaceless EGL (llvmpipe), with sample data in place of the console.

#include "fake_services.hpp"
#include "pe/core/file.hpp"
#include "pe/gfx/gl_batch.hpp"
#include "pe/gfx/gl_program.hpp"
#include "pe/gfx/system_fonts.hpp"
#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_assets.hpp"

#include <array>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using pe::ui::Key;

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr ?
            get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) :
            eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

struct Stage
{
    pe::host::FakeServices &services;
    pe::ui::Textures &textures;
    pe::ui::Fonts fonts;
    pe::gfx::GlBatch &batch;
    int width;
    int height;
    std::string output;
    std::unique_ptr<pe::ui::Launcher> launcher;
    pe::gfx::DrawList list;
    std::vector<unsigned char> pixels;
    bool tour = false;
    bool ok = true;
    std::vector<pe::audio::Cue> heard;
    // A host-only widened library fixture can exercise real continuous scrolling
    // without changing FakeServices or the console's service interface.
    pe::ui::Services *fixture_services = nullptr;
    std::string presentation_file;
    int presentation_generation = 0;

    void restart(bool first_start = false)
    {
        // Each unrelated regression starts with a clean presentation preference
        // file; explicit persistence cases can provide a shared path instead.
        const std::string state_file = presentation_file.empty() ?
            output + "/fixture-ui-" + std::to_string(++presentation_generation) + ".cfg" :
            presentation_file;
        if (presentation_file.empty()) std::remove(state_file.c_str());
        launcher = std::make_unique<pe::ui::Launcher>(fixture_services ? *fixture_services : services,
                                                   textures, fonts, first_start, state_file);
    }
    void frame()
    {
        launcher->update(1.0f / 60.0f);
        for (pe::audio::Cue cue : launcher->take_cues())
            heard.push_back(cue);
        if (tour)
            emit();
    }
    void wait(float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 60.0f + 0.5f); ++i)
            frame();
    }
    void press(std::initializer_list<Key> keys, float pause = 0.0f)
    {
        for (Key key : keys)
        {
            launcher->press(key);
            frame();
            if (pause > 0.0f)
                wait(pause);
        }
    }
    void render()
    {
        list.clear();
        launcher->draw(list);
        batch.sync_font_texture(fonts.texture, *fonts.font);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        batch.draw(list, pe::gfx::fit_viewport(width, height), width, height);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    // One frame of the tour, top row first, to stdout.
    void emit()
    {
        render();
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        for (int y = height - 1; y >= 0; --y)
            std::fwrite(pixels.data() + static_cast<std::size_t>(y) * stride, 1, stride, stdout);
    }
    void shoot(const char *name)
    {
        if (tour)
            return;
        render();
        const std::string path = output + "/" + name + ".png";
        const bool written =
            stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
        const GLenum error = glGetError();
        std::fprintf(stderr, "%s: %zu instances, %zu draw calls, GL error 0x%x%s\n", name,
                     list.instances().size(), batch.last_draw_calls(), error,
                     written ? "" : " (not written)");
        ok = ok && written && error == GL_NO_ERROR;
    }
};


// Observe the real draw list rather than counting placeholder tiles or private focus IDs.
struct CoverQuad
{
    std::uint32_t texture;
    pe::gfx::Rect rect;
    pe::gfx::Rect clip;
    float alpha;
};

std::vector<CoverQuad> cover_quads(const Stage &s)
{
    std::vector<CoverQuad> result;
    for (const auto &run : s.list.runs())
        for (std::uint32_t i = run.first; i < run.first + run.count; ++i)
        {
            const auto &quad = s.list.instances()[i];
            const int shape = static_cast<int>(quad.params[3]);
            if (!run.texture || (shape != static_cast<int>(pe::gfx::Shape::image) &&
                                 shape != static_cast<int>(pe::gfx::Shape::rounded_image)) ||
                run.texture == s.textures.brand() || run.texture == s.textures.stock_icons() ||
                run.texture == s.textures.stock_ui() || run.texture == s.textures.stock_profiles() ||
                run.texture == s.textures.controller() || run.texture == s.textures.backdrop() ||
                run.texture == s.textures.backdrop_blur() ||
                quad.rect[2] < 128.0f || std::fabs(quad.rect[2] - quad.rect[3]) > 0.5f)
                continue;
            result.push_back({run.texture,
                              {quad.rect[0], quad.rect[1], quad.rect[2], quad.rect[3]},
                              run.clipped ? run.clip : pe::gfx::Rect{0, 0, 1920, 1080},
                              std::min(quad.color_top[3], quad.color_bottom[3])});
        }
    return result;
}

bool visible_cover(const CoverQuad &q)
{
    const float left = std::max(0.0f, q.clip.x);
    const float top = std::max(0.0f, q.clip.y);
    const float right = std::min(1920.0f, q.clip.x + q.clip.w);
    const float bottom = std::min(1080.0f, q.clip.y + q.clip.h);
    return q.alpha > 0.001f && q.rect.x < right && q.rect.y < bottom &&
           q.rect.x + q.rect.w > left && q.rect.y + q.rect.h > top;
}

// Twenty-five invented entries expose four complete rows and an incomplete last
// row. Covers and real saved settings continue to use the existing host fixture.
// This adapter is excluded from every native app build.
class WideLibraryServices final : public pe::ui::Services
{
  public:
    explicit WideLibraryServices(pe::host::FakeServices &base, const std::string &output) : base_(base)
    {
        const auto samples = base.games();
        std::vector<pe::ui::Game> with_art;
        for (const auto &game : samples)
            if (!game.cover.empty()) with_art.push_back(game);
        for (int i = 0; i < 25; ++i) {
            auto game = with_art[static_cast<std::size_t>(i) % with_art.size()];
            char prefix[16]; std::snprintf(prefix, sizeof(prefix), "%02d - ", i);
            game.name = prefix + game.name;
            game.file = prefix + game.file;
            game.title_id += static_cast<std::uint64_t>(i + 100) * 0x10000u;
            // Unique texture identities allow continuity and selected-card
            // placement to be observed without private launcher state.
            std::string pixels;
            if (pe::read_file(game.cover,&pixels)) {
                const std::string copy=output+"/wide-cover-"+std::to_string(i)+".tga";
                if (std::FILE *file=std::fopen(copy.c_str(),"wb")) {
                    const bool copied=std::fwrite(pixels.data(),1,pixels.size(),file)==pixels.size();
                    const bool closed=std::fclose(file)==0;
                    if(copied && closed)game.cover=copy;
                }
            }
            entries.push_back(std::move(game));
        }
    }
    pe::ui::Home home() override { pe::ui::Home h; h.setup_ready=true; return h; }
    std::string clock() override { return base_.clock(); }
    unsigned controllers() override { return base_.controllers(); }
    std::string version() override { return base_.version(); }
    std::vector<pe::ui::Game> games() override { return entries; }
    std::string game_path(const std::string &file) override { return base_.game_path(file); }
    bool game_exists(const std::string &file) override { return base_.game_exists(file); }
    bool docked(std::uint64_t title) override { return base_.docked(title); }
    bool set_docked(std::uint64_t title,bool value) override { return base_.set_docked(title,value); }
    pe::ui::GameSettings game_settings(std::uint64_t title) override { return base_.game_settings(title); }
    bool set_game_settings(std::uint64_t title,const pe::ui::GameSettings &value) override { return base_.set_game_settings(title,value); }
    pe::ui::Preferences preferences() override { return base_.preferences(); }
    bool set_preferences(const pe::ui::Preferences &value) override { return base_.set_preferences(value); }
    const std::vector<std::string> &resolution_labels() override { return base_.resolution_labels(); }
    const std::vector<std::string> &resolution_keys() override { return base_.resolution_keys(); }
    const std::vector<std::string> &filter_labels() override { return base_.filter_labels(); }
    const std::vector<std::string> &astc_labels() override { return base_.astc_labels(); }
    int frame_gen_state() override { return base_.frame_gen_state(); }
    const std::vector<std::string> &language_labels() override { return base_.language_labels(); }
    std::string language_region(int n) override { return base_.language_region(n); }
    std::string setup_details() override { return base_.setup_details(); }
    bool folders(const std::string &folder,std::vector<std::string> *names) override { return base_.folders(folder,names); }
    pe::ui::FolderInfo folder_info(const std::string &folder) override { return base_.folder_info(folder); }
    std::string files_folder() override { return base_.files_folder(); }
    std::string saved_files_folder() override { return base_.saved_files_folder(); }
    std::string default_files_folder() override { return base_.default_files_folder(); }
    bool set_files_folder(const std::string &folder) override { return base_.set_files_folder(folder); }
    int filesystem_access() override { return base_.filesystem_access(); }
    std::vector<pe::ui::Profile> profiles() override { return base_.profiles(); }
    bool choose_profile(int n) override { return base_.choose_profile(n); }
    bool load_image(const std::string &path, pe::gfx::Image *image) override { return base_.load_image(path,image); }
    std::vector<pe::ui::Game> entries;
  private:
    pe::host::FakeServices &base_;
};

// All Software is the circular final entry on HOME's strip. Repeated Right
// clamps there even before the asynchronous scan has populated all seven games.
void home_library(Stage &s, float settle = 0.6f)
{
    for (int i=0;i<7;++i) s.press({Key::right});
    s.press({Key::cross});
    s.wait(settle);
}

void home_settings(Stage &s, float settle = 0.6f)
{
    s.press({Key::down, Key::right, Key::right, Key::right, Key::right, Key::cross});
    s.wait(settle);
}

void home_shortcut(Stage &s, int position, float settle = 0.8f)
{
    s.press({Key::down});
    for (int i = 1; i < position; ++i) s.press({Key::right});
    s.press({Key::cross});
    s.wait(settle);
}

void switch_library_fidelity_checks(Stage &s)
{
    int checks = 0;
    const auto check = [&](bool passed, const std::string &message) {
        ++checks;
        if (!passed) { std::fprintf(stderr, "error: Switch library fidelity: %s\n", message.c_str()); s.ok=false; }
    };
    s.launcher.reset();
    WideLibraryServices fixture{s.services,s.output};
    s.fixture_services = &fixture;
    const auto ready = [&]() {
        s.restart(); s.wait(1.0f); home_library(s);
        for (int i=0; i<3; ++i) { s.render(); s.wait(0.4f); }
        s.render();
    };
    const auto artwork = [&]() {
        std::vector<CoverQuad> result;
        for (const auto &q : cover_quads(s))
            if (visible_cover(q) && q.alpha > 0.99f) result.push_back(q);
        return result;
    };
    const auto check_grid = [&](const char *where, bool first) {
        const auto covers = artwork();
        check(!covers.empty(), std::string{where}+" has no library artwork");
        std::map<int,std::vector<pe::gfx::Rect>> rows;
        int full=0, partial=0;
        for (const auto &q : covers) {
            check(std::fabs(q.rect.w-261.0f)<0.5f && std::fabs(q.rect.h-261.0f)<0.5f,
                  std::string{where}+" changed firmware 261px library covers");
            check(q.rect.x >= q.clip.x-0.5f && q.rect.x+q.rect.w <= q.clip.x+q.clip.w+0.5f,
                  std::string{where}+" clipped a library column horizontally");
            check(q.clip.y+q.clip.h<=952.0f && q.clip.x+q.clip.w<=1850.0f,
                  std::string{where}+" artwork can overlap the footer or scrollbar");
            const bool whole=q.rect.y>=q.clip.y-0.5f && q.rect.y+q.rect.h<=q.clip.y+q.clip.h+0.5f;
            full+=whole; partial+=!whole;
            rows[static_cast<int>(std::lround(q.rect.y))].push_back(q.rect);
        }
        if (first) {
            check(covers.size()==18 && rows.size()==3,
                  "first view must contain six columns across three row extents");
            check(full==12 && partial==6,
                  "first library view must contain two complete rows and a clipped third row");
        }
        int previous=0; bool have_previous=false;
        for (auto &[y,row] : rows) {
            std::sort(row.begin(),row.end(),[](const auto &a,const auto &b){return a.x<b.x;});
            check(row.size()<=6,std::string{where}+" has more than six columns");
            for (std::size_t i=1;i<row.size();++i)
                check(std::fabs(row[i].x-row[i-1].x-276.0f)<0.5f,
                      std::string{where}+" lost stock 276px horizontal pitch");
            if (have_previous)
                check(std::abs(y-previous-276)<=1,std::string{where}+" lost stock row pitch");
            previous=y; have_previous=true;
        }
        return covers;
    };
    ready();
    const auto initial=check_grid("initial library",true);
    const auto check_top_return = [&](std::initializer_list<Key> down,
                                      std::initializer_list<Key> up, const char *where) {
        ready(); s.press(down); s.wait(0.6f);
        s.press(up); s.wait(0.8f); s.render();
        const auto returned = check_grid(where,true);
        check(!initial.empty() && !returned.empty() &&
                  std::fabs(initial.front().rect.y-returned.front().rect.y)<0.5f,
              std::string{where}+" did not restore the opening scroll position");
        check(!returned.empty() && returned.front().rect.y-84.0f>=returned.front().clip.y,
              std::string{where}+" clips the selected title bubble");
    };
    check_top_return({Key::down,Key::down},{Key::up,Key::up},"arrow return to top");
    check_top_return({Key::r1,Key::r1},{Key::l1,Key::l1},"shoulder return to top");
    s.shoot("switch-library-returned-top");
    ready();
    s.shoot("switch-library-six-by-three");
    s.press({Key::options});s.wait(.3f);s.shoot("switch-library-sort");
    s.press({Key::circle});s.wait(.3f);
    s.press({Key::down});s.wait(0.6f);s.render();
    const auto second=check_grid("second-row focus",true);
    check(!initial.empty() && !second.empty() && std::fabs(initial.front().rect.y-second.front().rect.y)<0.5f,
          "moving one row hard-replaced or scrolled the first library view");
    s.press({Key::down});s.wait(0.6f);s.render();
    const auto scrolled=check_grid("third-row focus",false);
    bool moved=false;
    for(const auto &before:initial)
        for(const auto &after:scrolled)
            if(before.texture==after.texture && std::fabs(before.rect.y-after.rect.y)>100.0f)moved=true;
    check(moved,"third-row selection did not move shared artwork in the continuous clipped grid");
    s.shoot("switch-library-continuous-scroll");

    // Idle focus has its own firmware effect; cover geometry remains stationary.
    if (!s.services.preferences().reduce_motion) {
        const auto before=s.pixels;
        s.wait(0.25f);s.render();
        check(s.pixels!=before,"library idle focus effect remained a static outline");
    }
    const auto launch_at = [&](std::initializer_list<Key> keys, int index,const char *where) {
        ready();s.press(keys);s.wait(0.7f);s.render();
        check_grid(where,false);
        s.press({Key::cross});
        check(s.launcher->selected_game()==fixture.game_path(fixture.entries[static_cast<std::size_t>(index)].file),
              std::string{where}+" launched the wrong game");
        check(!s.launcher->done(),std::string{where}+" bypassed the normal launch transition");
        s.wait(1.1f);check(s.launcher->done(),std::string{where}+" did not complete launch handoff");
    };
    launch_at({Key::down},6,"six-column Down");
    launch_at({Key::down,Key::right,Key::up},1,"six-column Up");
    launch_at({Key::r1},18,"eighteen-entry shoulder scroll");
    launch_at({Key::r1,Key::l1},0,"reverse shoulder scroll");
    launch_at({Key::r1,Key::r1,Key::down,Key::right},24,"incomplete final row");
    ready();s.press({Key::r1,Key::r1});s.wait(0.7f);s.shoot("switch-library-final-row");
    s.launcher.reset();s.fixture_services=nullptr;
    std::fprintf(stderr,"Switch reference library geometry and scroll checks completed: %d checks\n",checks);
}

// Seven HOME games are a presentation pool: last/recent, then the existing scan.
// The fixture expectations are explicit so duplicate or reordered targets cannot
// pass by reconstructing the implementation's own selection algorithm.
void switch_home_checks(Stage &s)
{
    int checks = 0;
    const auto check = [&](bool passed, const std::string &message) {
        ++checks;
        if (!passed) {
            std::fprintf(stderr, "error: Switch HOME: %s\n", message.c_str());
            s.ok = false;
        }
    };
    const auto games = s.services.games();
    const auto saved_removed = s.services.removed;
    const bool saved_history = s.services.has_history;
    const auto saved_preferences = s.services.preferences();
    const auto saved_people = s.services.people;
    const auto saved_launch_error = s.services.launch_error;
    const auto saved_game_settings = s.services.game_settings(0);
    const auto saved_settings_title = s.services.last_game_settings_title;
    std::vector<bool> modes;
    for (const auto &game : games) modes.push_back(s.services.docked(game.title_id));
    std::vector<pe::ui::Game> expected;
    for (const char *name : {"Echoes of the Valley", "Kart Carnival Deluxe", "Starfall Odyssey",
                             "Caf\xC3\xA9 Nocturne", "Cloudline", "Ember Knights II", "Moss & Lantern"}) {
        const auto game = std::find_if(games.begin(), games.end(),
                                      [&](const auto &item) { return item.name == name; });
        check(game != games.end(), "seven-game fixture lost " + std::string{name});
        if (game == games.end()) return;
        expected.push_back(*game);
    }
    const auto right = [&](int count) {
        for (int i = 0; i < count; ++i) s.press({Key::right});
    };
    const auto warm = [&]() {
        // Drawing queues covers; simulated time alone does not queue them.
        for (int i = 0; i < 3; ++i) { s.render(); s.wait(0.4f); }
        s.render();
    };
    const auto assert_covers = [&](const std::vector<pe::ui::Game> &wanted, const char *where,
                                   int selected_slot = 0, bool at_start = false) {
        const auto quads = cover_quads(s);
        std::set<std::uint32_t> textures;
        std::vector<pe::gfx::Rect> rectangles;
        int actual_visible = 0, fully_visible = 0, partial = 0;
        bool selected_visible = wanted.empty();
        for (const auto &q : quads) {
            if (!visible_cover(q)) continue;
            ++actual_visible;
            const auto game = std::find_if(wanted.begin(), wanted.end(), [&](const auto &item) {
                return s.textures.cover(item.cover, q.rect.w).texture == q.texture;
            });
            check(game != wanted.end(), std::string{where} + " drew a game outside the seven-game HOME pool");
            if (game == wanted.end()) continue;
            check(textures.insert(q.texture).second, std::string{where} + " reused a game cover");
            check(std::fabs(q.rect.w - 384.0f) < 0.5f && std::fabs(q.rect.h - 384.0f) < 0.5f,
                  std::string{where} + " changed stock 384px tile dimensions");
            const float left = std::max(0.0f, q.clip.x), top = std::max(0.0f, q.clip.y);
            const float right_edge = std::min(1920.0f, q.clip.x + q.clip.w);
            const float bottom = std::min(1080.0f, q.clip.y + q.clip.h);
            const bool full = q.rect.x >= left - 0.5f && q.rect.x + q.rect.w <= right_edge + 0.5f;
            fully_visible += full;
            partial += !full;
            check(q.alpha >= 0.99f && q.rect.y >= top - 0.5f && q.rect.y + q.rect.h <= bottom + 0.5f,
                  std::string{where} + " cover faded or clipped vertically: " + game->name);
            if (game->file == wanted[static_cast<std::size_t>(selected_slot)].file)
                selected_visible = full;
            for (const auto &other : rectangles)
                check(q.rect.x + q.rect.w <= other.x + 0.5f || other.x + other.w <= q.rect.x + 0.5f ||
                      q.rect.y + q.rect.h <= other.y + 0.5f || other.y + other.h <= q.rect.y + 0.5f,
                      std::string{where} + " covers overlap");
            rectangles.push_back(q.rect);
        }
        check(selected_visible, std::string{where} + " focused game is missing or clipped");
        check(actual_visible <= 5 && actual_visible <= static_cast<int>(wanted.size()),
              std::string{where} + " showed too many HOME games simultaneously");
        if (wanted.size() >= 5 && at_start)
            check(fully_visible == 4 && partial == 1,
                  std::string{where} + " must show four full stock-size cards and part of the fifth");
        else if (wanted.size() < 5)
            check(actual_visible == static_cast<int>(wanted.size()),
                  std::string{where} + " did not show the available HOME games");
        std::sort(rectangles.begin(), rectangles.end(), [](const auto &a,const auto &b) { return a.x < b.x; });
        for (std::size_t i = 1; i < rectangles.size(); ++i)
            check(std::fabs(rectangles[i].x - rectangles[i-1].x - 405.0f) < 0.5f,
                  std::string{where} + " lost stock 405px artwork pitch");
    };
    const auto selected = [&](const pe::ui::Game &game, const char *where) {
        check(s.launcher->selected_game() == s.services.game_path(game.file),
              std::string{where} + " selected the wrong game path");
        check(!s.launcher->done(), std::string{where} + " skipped the launch transition");
        s.wait(1.1f);
        check(s.launcher->done(), std::string{where} + " failed to finish the launch");
    };
    const auto ready_home = [&]() {
        // Stage.wait advances simulation time, not the std::async worker's wall
        // clock. Entering the library uses its public blocking initial scan path;
        // return and Left give these navigation cases a known leading-game focus.
        s.restart(); s.wait(1.0f);
        home_library(s,0.4f);
        s.press({Key::circle});
        for(int i=0;i<7;++i) s.press({Key::left});
        s.wait(0.4f); warm();
    };

    ready_home();
    assert_covers(expected, "default HOME", 0, true);
    s.shoot("switch-home-stock-size-strip");
    right(6); s.wait(0.7f); s.render();
    assert_covers(expected, "seventh-focused HOME", 6);
    s.shoot("switch-home-seventh-focus");
    s.press({Key::cross}); selected(expected[6], "seventh HOME launch");

    ready_home(); right(6); s.press({Key::triangle}); s.wait(0.6f);
    s.shoot("switch-home-seventh-options");
    s.press({Key::right});
    for (std::size_t i = 0; i < games.size(); ++i)
        check(s.services.docked(games[i].title_id) ==
                  (games[i].file == expected[6].file ? !modes[i] : modes[i]),
              "seventh HOME options changed the wrong game's console mode");
    s.press({Key::left});
    for (std::size_t i = 0; i < games.size(); ++i)
        check(s.services.docked(games[i].title_id) == modes[i], "seventh HOME options did not restore mode");

    // The third HOME shortcut opens Multiplayer without touching game settings.
    ready_home(); s.press({Key::down, Key::right, Key::right, Key::cross}); s.wait(0.4f);
    for (std::size_t i = 0; i < games.size(); ++i)
        check(s.services.docked(games[i].title_id) == modes[i],
              "HOME Multiplayer shortcut changed a game's console mode");
    check(!s.launcher->done() && s.launcher->selected_game().empty(),
          "HOME Multiplayer shortcut launched a game");
    s.press({Key::circle});

    ready_home(); right(7); s.wait(0.7f);
    s.press({Key::cross}); s.wait(0.7f); s.render();
    check(s.launcher->selected_game().empty() && !s.launcher->done(), "right of seventh did not open All Software");
    const auto library_quads = cover_quads(s);
    check(std::any_of(library_quads.begin(), library_quads.end(), [](const auto &q) {
        return visible_cover(q) && std::fabs(q.rect.w - 261.0f) < 0.5f;
    }), "All Software did not draw its library grid");
    s.press({Key::circle}); s.wait(0.4f); right(3); s.press({Key::cross});s.wait(0.4f);
    check(s.launcher->selected_game().empty() && !s.launcher->done(), "HOME end boundary did not clamp at All Software");
    ready_home();s.press({Key::left,Key::left,Key::cross});
    selected(expected.front(), "HOME leading boundary clamp");

    // Sample every actual HOME/library fade in both directions. Covers from the
    // two screen geometries must never be visible together, even at the midpoint.
    ready_home();right(7);s.wait(0.7f);
    const auto transition = [&](Key key, bool to_library) {
        s.launcher->press(key);
        // Observe a complete second including separate header/content phases.
        const int frames = 60;
        bool saw_outgoing = false, saw_incoming = false;
        for (int frame = 0; frame <= frames; ++frame) {
            s.render();
            int home = 0, library = 0;
            for (const auto &q : cover_quads(s)) if (visible_cover(q)) {
                home += std::fabs(q.rect.w - 384.0f) < 0.5f;
                library += std::fabs(q.rect.w - 261.0f) < 0.5f;
            }
            check(!(home && library), "HOME/library transition drew overlapping screens");
            const int outgoing = to_library ? home : library;
            const int incoming = to_library ? library : home;
            // Firmware header/content/footer phases need not occupy equal halves.
            // The safety contract is that once incoming covers become visible,
            // outgoing game geometry can never return or coexist with it.
            check(!saw_incoming || outgoing == 0, "outgoing covers reappeared after incoming covers");
            saw_outgoing = saw_outgoing || outgoing > 0;
            saw_incoming = saw_incoming || incoming > 0;
            check(glGetError() == GL_NO_ERROR, "transition render reported a GL error");
            if (frame == frames / 2)
                s.shoot(to_library ? "switch-home-to-library-mid" : "switch-library-to-home-mid");
            s.frame();
        }
        check(saw_outgoing && saw_incoming, "transition omitted one screen or stayed blank");
        check(s.launcher->selected_game().empty() && !s.launcher->done(), "transition launched a game");
    };
    transition(Key::cross, true); s.wait(0.4f);
    transition(Key::circle, false); s.wait(0.4f);

    // Without history, the scan supplies seven distinct games rather than a blank tile.
    s.launcher.reset(); // Finish any old worker before changing its fake service data.
    s.services.has_history = false;ready_home();
    const std::vector<pe::ui::Game> scanned(games.begin(), games.begin() + 7);
    assert_covers(scanned, "HOME without history", 0, true);
    s.shoot("switch-home-no-history-strip");
    right(6); s.press({Key::cross}); selected(games[6], "no-history seventh launch");
    for (int count : {0, 1}) {
        s.launcher.reset();
        s.services.removed = saved_removed;
        for (std::size_t i = static_cast<std::size_t>(count); i < games.size(); ++i)
            s.services.removed.push_back(games[i].file);
        ready_home(); s.wait(0.4f); s.render();
        assert_covers(count == 0 ? std::vector<pe::ui::Game>{} : std::vector<pe::ui::Game>{games[0]},
                      count == 0 ? "empty HOME" : "single-game HOME");
        s.shoot(count == 0 ? "switch-home-empty" : "switch-home-single");
        if (count == 0) {
            s.press({Key::right, Key::cross}); s.wait(1.1f);
            check(s.launcher->selected_game().empty() && !s.launcher->done(), "empty HOME launched a file");
        } else {
            s.press({Key::right, Key::cross}); s.wait(0.4f);
            check(s.launcher->selected_game().empty() && !s.launcher->done(), "single HOME All Software launched directly");
            s.press({Key::circle}); s.wait(0.4f); s.press({Key::left, Key::left, Key::cross});
            selected(games[0], "single-game HOME leading clamp");
        }
    }
    // A game removed while HOME is active must repair focus to All Software.
    s.launcher.reset(); s.services.removed = saved_removed;
    for (std::size_t i = 1; i < games.size(); ++i) s.services.removed.push_back(games[i].file);
    ready_home();
    s.services.removed.push_back(games.front().file); s.wait(2.2f);
    s.press({Key::cross}); s.wait(.7f); s.render();
    check(!s.launcher->done() && s.launcher->selected_game().empty(),
          "removing the final HOME game left a launchable missing tile");
    const auto empty_library = cover_quads(s);
    check(std::none_of(empty_library.begin(), empty_library.end(), visible_cover),
          "removing the final HOME game retained stale library covers");
    s.shoot("switch-home-final-game-removed");
    s.launcher.reset();
    s.services.removed = saved_removed; s.services.has_history = saved_history;
    ready_home(); right(6);
    s.services.removed.push_back(expected[6].file); s.wait(2.2f);
    s.press({Key::cross});
    const auto replacement = std::find_if(games.begin(), games.end(),
                                          [](const auto &g) { return g.name == "Neon Drifters"; });
    check(replacement != games.end(), "removed-game fixture lost replacement");
    if (replacement != games.end()) selected(*replacement, "removed seventh HOME replacement");

    // A presence refresh can replace the recent row with a diagnostic message.
    // Keep slot10 focused: navigating to Profiles would instead select shortcut2
    // before the refresh and would not test repair of a hidden seventh-game focus.
    s.launcher.reset();
    s.services.removed = saved_removed;
    ready_home(); right(6);
    s.services.launch_error = "Host HOME diagnostic regression";
    s.services.removed.push_back(expected[1].file);
    s.wait(2.2f); warm();
    assert_covers({expected.front()}, "HOME diagnostic hides extra tiles");
    s.shoot("switch-home-diagnostic-focus-repaired");
    s.press({Key::cross}); selected(expected.front(), "diagnostic HOME focus repair");

    s.launcher.reset();
    s.services.removed = saved_removed; s.services.has_history = saved_history;
    s.services.people = saved_people; s.services.launch_error = saved_launch_error;
    s.services.set_preferences(saved_preferences); s.services.set_game_settings(0, saved_game_settings);
    s.services.last_game_settings_title = saved_settings_title;
    for (std::size_t i = 0; i < games.size(); ++i) s.services.set_docked(games[i].title_id, modes[i]);
    std::fprintf(stderr, "Switch seven-game HOME and transition checks completed: %d checks\n", checks);
}

// Verify the new grid using the real launcher and host services. No emulator is involved.

void switch_loading_picture(Stage &s,const std::string &assets)
{
    std::string word,scene;
    if (!pe::read_file(assets+"/../../loading_text.glsl",&word) ||
        !pe::read_file(assets+"/../../loading_scene.glsl",&scene)) { s.ok=false;return; }
    const std::string fragment = word+scene+
        "\nuniform vec2 size;uniform float seconds;out vec4 color;"
        "void main(){color=vec4(loading_scene(gl_FragCoord.xy,size,seconds),1.0);}\n";
    const char *vertex="void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
    pe::gfx::set_glsl_prefix("#version 330 core\n");
    GLuint program=pe::gfx::build_program("Switch game loading",vertex,fragment.c_str());
    pe::gfx::set_glsl_prefix("#version 450 core\n");
    if(!program) {s.ok=false;return;}
    GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glUseProgram(program);
    glUniform2f(glGetUniformLocation(program,"size"),s.width,s.height);
    glViewport(0,0,s.width,s.height);
    const GLint clock_uniform=glGetUniformLocation(program,"seconds");
    const auto draw=[&](float seconds) {
        glUniform1f(clock_uniform,seconds);glDrawArrays(GL_TRIANGLES,0,3);
        glReadPixels(0,0,s.width,s.height,GL_RGBA,GL_UNSIGNED_BYTE,s.pixels.data());
    };
    std::vector<unsigned char> normal,calm_pixels;
    for(int calm=0;calm<2;++calm) {
        draw(0.6f+calm*1000.0f);
        const std::string path=s.output+(calm ? "/switch-game-loading-calm.png" : "/switch-game-loading.png");
        s.ok = s.ok && stbi_write_png(path.c_str(),s.width,s.height,4,s.pixels.data(),s.width*4)!=0;
        const GLenum error=glGetError();
        std::fprintf(stderr,"Switch loading%s: GL error 0x%x\n",calm ? " calm" : "",error);
        s.ok=s.ok && error==GL_NO_ERROR;
        if(calm) calm_pixels=s.pixels;else normal=s.pixels;
    }
    draw(1001.4f);
    const bool stationary=s.pixels==calm_pixels;
    draw(0.20f);
    const bool animated=s.pixels!=normal;
    const std::size_t middle=(static_cast<std::size_t>(s.height/2)*s.width+s.width/2)*4;
    const bool black=s.pixels[middle]==0 && s.pixels[middle+1]==0 && s.pixels[middle+2]==0;
    if(!stationary || !animated || !black) {
        std::fprintf(stderr,"error: Switch boot shader did not honor animation, calm mode or black background\n");s.ok=false;
    }
    for(int frame=0;frame<54;++frame) {
        draw(frame/30.0f);
        char name[64];std::snprintf(name,sizeof(name),"/switch-boot-frame-%02d.png",frame);
        s.ok = s.ok && stbi_write_png((s.output+name).c_str(),s.width,s.height,4,s.pixels.data(),s.width*4)!=0;
    }
    const GLenum error=glGetError();s.ok=s.ok && error==GL_NO_ERROR;
    std::fprintf(stderr,"Switch boot animation: GL error 0x%x; stationary calm mode %s; normal motion %s; black background %s\n",
                 error,stationary ? "PASS" : "FAIL",animated ? "PASS" : "FAIL",black ? "PASS" : "FAIL");
    glUseProgram(0);glBindVertexArray(0);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
}

void switch_opening_pictures(Stage &s)
{
    s.restart();s.wait(1.0f);s.render();s.wait(0.8f);s.render();
    for(int frame=0;frame<15;++frame) {
        char name[64];std::snprintf(name,sizeof(name),"/switch-opening-frame-%02d.png",frame);
        s.render();s.ok=s.ok && stbi_write_png((s.output+name).c_str(),s.width,s.height,4,s.pixels.data(),s.width*4)!=0;
    }
    s.press({Key::cross});
    for(int frame=15;frame<44;++frame) {
        char name[64];std::snprintf(name,sizeof(name),"/switch-opening-frame-%02d.png",frame);
        s.render();s.ok=s.ok && stbi_write_png((s.output+name).c_str(),s.width,s.height,4,s.pixels.data(),s.width*4)!=0;
        if(frame==16) s.shoot("switch-game-selected");
        if(frame==24) s.shoot("switch-game-fade");
        if(frame==37) s.shoot("switch-game-handoff");
        s.frame();s.frame();
    }
    const GLenum error=glGetError();s.ok=s.ok && error==GL_NO_ERROR;
    std::fprintf(stderr,"Switch opening animation: GL error 0x%x\n",error);
}

void switch_navigation_checks(Stage &s)
{
    const auto games = s.services.games();
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "error: Switch navigation: %s\n",message); s.ok=false; }
    };
    const auto launch_at = [&](std::initializer_list<Key> keys,int index) {
        s.restart(); s.wait(1.0f); home_library(s);
        const bool docked = s.services.docked(games[static_cast<std::size_t>(index)].title_id);
        s.press(keys); s.press({Key::cross});
        check(s.launcher->selected_game()==s.services.game_path(games[static_cast<std::size_t>(index)].file),
              "grid navigation selected the wrong game");
        check(s.services.docked(games[static_cast<std::size_t>(index)].title_id)==docked,
              "navigation changed console mode");
        check(!s.launcher->done(),"launch completed before its closing transition");
        s.wait(1.1f);check(s.launcher->done(),"launch did not finish");
    };
    launch_at({Key::right},1);
    launch_at({Key::down},6);
    launch_at({Key::down,Key::right,Key::up},1);
    launch_at({Key::left,Key::up},0);
    launch_at({Key::r1},static_cast<int>(games.size())-1);
    launch_at({Key::r1,Key::r1,Key::down,Key::right},static_cast<int>(games.size())-1);
    launch_at({Key::r1,Key::l1},0);
    // The Docked/Handheld action is still available on the existing game-settings row.
    s.restart();s.wait(1.0f);home_library(s);
    const bool old=s.services.docked(games[0].title_id);
    s.press({Key::triangle,Key::right});
    check(s.services.docked(games[0].title_id)!=old,"game settings lost the console-mode action");
    s.press({Key::left});check(s.services.docked(games[0].title_id)==old,"console mode did not restore");
    s.restart();s.wait(1.0f);home_settings(s);s.shoot("switch-system-settings-route");
    // HOME options must act on the focused tile, even when it differs from last played.
    const auto home = s.services.home();
    const auto home_options = [&](std::initializer_list<Key> keys, const std::string &file,
                                  const char *picture) {
        const auto game = std::find_if(games.begin(),games.end(),
            [&](const auto &item) { return item.file == file; });
        check(game != games.end(),"HOME fixture does not contain its game");
        if (game == games.end()) return;
        const bool old_mode = s.services.docked(game->title_id);
        s.restart();s.wait(1.0f);s.press(keys);s.wait(0.6f);s.shoot(picture);
        s.press({Key::right});
        check(s.services.docked(game->title_id)!=old_mode,"HOME options targeted the wrong game");
        s.press({Key::left});
        check(s.services.docked(game->title_id)==old_mode,"HOME options did not restore console mode");
        s.press({Key::circle});s.wait(0.7f);s.render();
        const auto returned = cover_quads(s);
        check(std::any_of(returned.begin(),returned.end(),[](const auto &q) {
            return visible_cover(q) && std::fabs(q.rect.w-384.0f)<0.5f;
        }),"closing HOME game settings did not return to HOME");
    };
    home_options({Key::triangle},home.last_file,"switch-home-game-options");
    const auto recent = std::find_if(home.recents.begin(),home.recents.end(),
        [&](const auto &item) { return item.file != home.last_file; });
    if (recent != home.recents.end())
        home_options({Key::right,Key::triangle},recent->file,"switch-home-recent-options");
    // HOME opens the actual multiplayer workflow. Its service calls remain the
    // same ones used by System Settings; closing it retains HOME shortcut focus.
    const auto prior_room = s.services.room;
    const auto prior_room_connection = s.services.last_room_connection;
    const auto prior_saved_room = s.services.saved_room;
    const auto signal_pixels=[&]() {
        std::uint64_t hash=1469598103934665603ull;
        const int left = static_cast<int>(1830.0f * s.width / 1920.0f);
        const int right = static_cast<int>(std::ceil(1882.0f * s.width / 1920.0f));
        const int top = static_cast<int>(82.0f * s.height / 1080.0f);
        const int bottom = static_cast<int>(std::ceil(128.0f * s.height / 1080.0f));
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const std::size_t p=(static_cast<std::size_t>(s.height-1-y)*s.width+x)*4;
            for(int channel=0;channel<3;++channel)
                hash=(hash^s.pixels[p+channel])*1099511628211ull;
        }
        return hash;
    };
    s.services.room = {};
    s.restart();s.wait(1.1f);s.shoot("switch-home-server-offline");
    const auto offline_signal=signal_pixels();
    s.services.room={Eden::Multiplayer::Phase::Connecting,{},{},{}};
    s.wait(1.1f);s.shoot("switch-home-server-connecting");
    const auto connecting_signal=signal_pixels();
    s.services.room={Eden::Multiplayer::Phase::Connected,{},"Preview room",{"Eden"}};
    s.wait(1.1f);s.shoot("switch-home-server-connected");
    const auto connected_signal=signal_pixels();
    check(offline_signal!=connecting_signal && connecting_signal!=connected_signal &&
          offline_signal!=connected_signal,
          "HOME wireless indicator did not follow the live multiplayer phase");
    s.services.room = {};
    s.restart();s.wait(1.0f);s.press({Key::down,Key::right,Key::right});s.wait(.6f);
    s.shoot("switch-shortcut-multiplayer");
    s.press({Key::cross});s.wait(.6f);s.shoot("switch-home-multiplayer");
    s.press({Key::cross});
    for(int i=0;i<100;++i) {s.frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    s.shoot("switch-home-multiplayer-public-rooms");
    s.press({Key::down,Key::cross});s.wait(.3f);
    check(s.services.room.phase==Eden::Multiplayer::Phase::Connecting &&
          s.services.last_room_connection.host==s.services.directory.rooms.front().address,
          "HOME Multiplayer did not use the existing public-room connection service");
    s.press({Key::circle,Key::circle});s.wait(.7f);s.render();
    const auto multiplayer_returned=cover_quads(s);
    check(std::any_of(multiplayer_returned.begin(),multiplayer_returned.end(),[](const auto &q) {
        return visible_cover(q) && std::fabs(q.rect.w-384.0f)<0.5f;
    }),"closing HOME Multiplayer did not return to HOME");
    s.shoot("switch-shortcut-multiplayer-returned-home");
    s.services.room={};
    s.press({Key::cross,Key::cross});
    for(int i=0;i<100;++i) {s.frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    s.press({Key::down,Key::cross});
    check(s.services.room.phase==Eden::Multiplayer::Phase::Connecting,
          "closing HOME Multiplayer lost the Multiplayer shortcut focus");
    s.press({Key::circle,Key::circle});s.services.room=prior_room;
    s.services.last_room_connection=prior_room_connection;
    s.services.saved_room=prior_saved_room;
    // HOME shortcut exit routes preserve the HOME strip and its focus.
    for (int shortcut : {0,1}) {
        s.restart();s.wait(1.0f);s.press({Key::down});
        if(shortcut==1)s.press({Key::right});
        s.press({Key::cross});s.wait(0.7f);
        s.press({Key::circle});s.wait(0.7f);s.render();
        const auto returned=cover_quads(s);
        check(std::any_of(returned.begin(),returned.end(),[](const auto &q) {
            return visible_cover(q) && std::fabs(q.rect.w-384.0f)<0.5f;
        }),"HOME shortcut Back opened a different screen");
        s.shoot(shortcut==0 ? "switch-shortcut-about-returned-home" :
                             "switch-shortcut-files-returned-home");
    }
    s.restart();s.wait(1.0f);home_settings(s);
    for(int i=0;i<4;++i)s.press({Key::down});
    s.press({Key::cross});
    const auto accessibility=s.services.preferences();
    s.press({Key::cross});
    check(s.services.preferences().large_text!=accessibility.large_text &&
          s.services.preferences().vibration==accessibility.vibration,
          "the removed Controls category still occupies its System Settings row");
    s.press({Key::cross});
    // HOME scroll remains presentation-only. The circular end button still
    // opens the same library; returning and scrolling left reaches the first game.
    s.restart();s.wait(1.0f);for(int i=0;i<7;++i)s.press({Key::right});s.render();s.wait(0.7f);
    s.shoot("switch-home-all-software");
    s.press({Key::cross});s.wait(0.4f);s.shoot("switch-home-strip-library");
    check(!s.launcher->done(),"HOME All Software tile launched a game");
    s.press({Key::circle});s.wait(0.4f);for(int i=0;i<7;++i)s.press({Key::left});s.wait(0.7f);
    s.shoot("switch-home-strip-returned");
    s.press({Key::cross});s.wait(1.1f);
    check(s.launcher->selected_game()==s.services.game_path(home.last_file),"HOME strip return changed the launch target");
    // Empty, one-game and incomplete final rows remain safe and cannot launch a removed file.
    s.services.has_history=false;
    for (int count : {0,1,5}) {
        s.services.removed.clear();
        for (std::size_t i=count;i<games.size();++i) s.services.removed.push_back(games[i].file);
        s.restart();s.wait(1.0f);home_library(s,0.4f);
        s.press({Key::r1,Key::down,Key::right});s.wait(0.4f);
        s.shoot(count==0 ? "switch-library-empty" : count==1 ? "switch-library-single" : "switch-library-partial-row");
        s.press({Key::cross});
        check(s.launcher->selected_game()==(count==0 ? std::string{} : s.services.game_path(games[count-1].file)),
            "empty or partial grid launched the wrong file");
        s.wait(1.1f);
        check(s.launcher->done()==(count!=0),"empty or partial grid finished incorrectly");
    }
    s.services.removed.clear();s.services.has_history=true;
    std::fprintf(stderr,"Switch grid and launch contract checks completed\n");
}

// Exercise actual input routes and observable service effects, including rejected writes.
// This fixture cannot establish PS5 runtime behavior; the real service implementation is unchanged.
void switch_menu_checks(Stage &s)
{
    int checks=0;
    const auto check=[&](bool passed,const char *what) {
        ++checks;
        if(!passed) { std::fprintf(stderr,"error: Switch menu: %s\n",what);s.ok=false; }
    };
    const auto prefs=s.services.preferences();
    const auto game_prefs=s.services.game_settings(0);
    const auto people=s.services.people;
    const auto saved=s.services.saved_files_folder();
    const auto category=[&](int n) {
        s.restart();s.wait(0.8f);home_settings(s,0.3f);
        for(int i=0;i<n;++i) s.press({Key::down});
        s.press({Key::cross});s.wait(0.3f);
    };
    const auto choice=[&](int index) {
        s.press({Key::cross});
        for(int i=0;i<20;++i)s.press({Key::up});
        for(int i=0;i<index;++i)s.press({Key::down});
        s.press({Key::cross});s.wait(0.2f);
    };
    category(1);
    const auto video=s.services.preferences();
    choice(1-video.renderer);check(s.services.preferences().renderer!=video.renderer,"renderer");
    s.press({Key::down});choice((video.output+1)%3);check(s.launcher->output()==(video.output+1)%3,"output resolution");
    s.press({Key::down});choice((video.resolution+1)%static_cast<int>(s.services.resolution_labels().size()));check(s.services.preferences().resolution==(video.resolution+1)%static_cast<int>(s.services.resolution_labels().size()),"render resolution");
    s.press({Key::down});choice((video.filter+1)%static_cast<int>(s.services.filter_labels().size()));check(s.services.preferences().filter==(video.filter+1)%static_cast<int>(s.services.filter_labels().size()),"upscaling filter");
    s.press({Key::down});choice(1-video.astc);check(s.services.preferences().astc!=video.astc,"ASTC texture quality");
    s.press({Key::down});choice(1-video.refresh);check(s.services.preferences().refresh!=video.refresh,"refresh rate");
    s.press({Key::down,Key::cross});check(s.services.preferences().hud!=video.hud,"FPS overlay");
    category(3);
    choice(9);check(s.services.preferences().volume==90,"game volume");
    choice(0);
    check(s.services.preferences().volume==0,"volume lower bound");
    choice(10);
    check(s.services.preferences().volume==100,"volume upper bound");
    s.services.fail_settings=true;choice(9);
    check(s.services.preferences().volume==100,"failed write changed saved volume");
    s.wait(0.3f);s.shoot("switch-settings-save-failed");
    s.services.fail_settings=false;s.press({Key::circle});choice(9);
    check(s.services.preferences().volume==90,"failed write did not roll back menu volume");
    s.press({Key::down,Key::cross});check(s.services.preferences().mute!=prefs.mute,"mute");
    s.press({Key::down});choice(prefs.menu_volume/10-1);check(s.launcher->menu_volume()==prefs.menu_volume-10,"immediate menu sound level");
    s.wait(0.3f);s.shoot("switch-audio-changed");
    s.restart();s.wait(.8f);home_shortcut(s,4);
    s.render();check(cover_quads(s).empty(),"Controllers page leaves HOME artwork visible");
    s.shoot("switch-home-controller-settings");
    s.press({Key::cross});check(s.services.preferences().vibration!=prefs.vibration,"vibration");
    category(4);s.press({Key::cross});check(s.services.preferences().large_text!=prefs.large_text,"larger text");
    s.press({Key::down,Key::cross});check(s.services.preferences().high_contrast!=prefs.high_contrast,"high contrast");
    s.press({Key::down,Key::cross});check(s.services.preferences().reduce_motion!=prefs.reduce_motion,"reduce motion");
    s.services.set_preferences(prefs);
    category(5);s.press({Key::cross});check(s.services.preferences().detailed_logging!=prefs.detailed_logging,"detailed logging");
    category(6);s.press({Key::cross,Key::down,Key::cross});check(s.services.preferences().language!=prefs.language,"game language");
    s.restart();s.wait(.8f);home_shortcut(s,2);
    check(s.services.saved_files_folder()==saved,"browsing saved a folder without selecting it");
    s.press({Key::triangle});check(s.services.saved_files_folder()==s.services.files_folder(),"Use this folder");
    s.press({Key::square});check(s.services.saved_files_folder()=="/data/prosperoeden","Default folder");
    s.services.fail_folder=true;s.press({Key::cross,Key::triangle});
    check(s.services.saved_files_folder()=="/data/prosperoeden","failed folder write");
    s.services.fail_folder=false;
    s.services.set_files_folder(saved);
    s.restart();s.wait(.8f);home_shortcut(s,2);s.services.unreadable_folder="/mnt/ext1/eden/firmware";
    s.press({Key::down,Key::cross});s.wait(0.3f);s.shoot("switch-folder-open-failed");
    s.press({Key::triangle});check(s.services.saved_files_folder()==s.services.files_folder(),"failed folder open changed current directory");
    s.services.unreadable_folder.clear();
    s.services.set_files_folder(saved);
    s.press({Key::circle});s.wait(.8f);s.render();
    const auto file_return=cover_quads(s);
    check(std::any_of(file_return.begin(),file_return.end(),[](const auto &q) {
        return visible_cover(q) && std::fabs(q.rect.w-384.0f)<.5f;
    }),"Game Files Back did not return to HOME");
    category(0);s.press({Key::cross,Key::triangle});check(s.services.profiles()[0].name!=people[0].name,"profile naming");
    s.services.people=people;

    const auto games=s.services.games();
    const auto open_game=[&](int row) {
        s.restart();s.wait(0.8f);home_library(s,0.4f);s.press({Key::triangle});
        for(int i=0;i<row;++i) s.press({Key::down});
    };
    open_game(3);s.press({Key::cross,Key::right});
    check(s.services.game_settings(0).volume==0 && s.services.last_game_settings_title==games[0].title_id,"per-game audio target");
    s.press({Key::down,Key::right});check(s.services.game_settings(0).mute==0,"per-game mute");
    const auto global=s.services.preferences();
    check(global.volume==prefs.volume && global.mute==prefs.mute,"per-game audio changed globals");
    s.services.fail_settings=true;s.press({Key::right});
    check(s.services.game_settings(0).mute==0,"per-game rejected write");
    s.wait(0.3f);s.shoot("switch-game-save-failed");
    s.services.fail_settings=false;s.press({Key::right});
    check(s.services.game_settings(0).mute==1,"per-game rollback");
    open_game(6);s.press({Key::cross,Key::right});check(s.services.game_settings(0).language==0,"per-game language");
    s.press({Key::left});check(s.services.game_settings(0).language==-1,"per-game follows Settings");
    s.services.set_game_settings(0,game_prefs);

    open_game(8);
    const int imports=s.services.imports,exports=s.services.exports;
    s.press({Key::cross});check(s.services.imports==imports,"save import skipped confirmation");
    s.press({Key::cross});check(s.services.imports==imports+1 && s.services.last_save_title==games[0].title_id,"confirmed save import target");
    s.wait(0.3f);s.shoot("switch-save-imported");
    s.press({Key::cross,Key::up,Key::down,Key::cross});
    check(s.services.imports==imports+1,"moving away did not cancel save confirmation");
    s.press({Key::square});check(s.services.exports==exports+1 && s.services.last_save_title==games[0].title_id,"save export target");
    s.services.fail_save=true;s.press({Key::square});s.wait(0.3f);s.shoot("switch-save-export-failed");
    check(!s.launcher->done() && s.launcher->selected_game().empty(),"save failure launched or closed the app");
    s.services.fail_save=false;
    s.services.import_available=false;open_game(8);s.press({Key::cross});
    check(s.services.imports==imports+1,"unavailable save transfer invoked import");
    s.services.import_available=true;

    const auto modded=std::find_if(games.begin(),games.end(),[&](const auto &g){return !s.services.mods(g.title_id).empty();});
    check(modded!=games.end(),"fixture has no mods");
    if(modded!=games.end()) {
        s.restart();s.wait(0.8f);home_library(s,0.4f);
        for(auto at=games.begin();at!=modded;++at) s.press({Key::right});
        const bool master=s.services.mods_enabled(modded->title_id);
        s.press({Key::square});check(s.services.mods_enabled(modded->title_id)!=master,"library Mods master switch");
        s.press({Key::square});
        s.press({Key::triangle});for(int i=0;i<7;++i) s.press({Key::down});s.press({Key::cross});
        const auto mods=s.services.mods(modded->title_id);
        s.press({Key::cross});check(s.services.mods(modded->title_id)[0].enabled!=mods[0].enabled,"mod switch");
        s.press({Key::cross});
        s.press({Key::down,Key::down,Key::down,Key::down,Key::cross});
        const auto cheats=s.services.mods(modded->title_id).back().cheats;
        check(!cheats[0].enabled && cheats[1].enabled,"mutually exclusive frame-rate cheats");
        for(const auto &cheat:mods.back().cheats) s.services.set_cheat_enabled(modded->title_id,mods.back().name,cheat.name,cheat.enabled);
        s.services.has_mods=false;s.press({Key::circle,Key::cross});
        const int made=s.services.made_mod_folders;s.press({Key::square});
        check(s.services.made_mod_folders==made+1,"empty Mods Create folder action");
        s.services.has_mods=true;
    }
    s.services.set_preferences(prefs);s.services.set_game_settings(0,game_prefs);
    s.services.people=people;s.services.set_files_folder(saved);
    // The controller page and nested mapping preserve the original preference
    // writer, rejected-write rollback, and independent per-game overrides.
    const auto mapping_start=s.services.preferences();
    s.restart();s.wait(0.8f);
    home_shortcut(s,4);
    s.press({Key::down,Key::down,Key::cross});s.wait(0.4f);
    s.shoot("switch-home-mapping");
    check(s.services.preferences().mapping==mapping_start.mapping,"opening HOME mapping changed bindings");
    s.press({Key::right});
    const auto mapped=pe::ui::assign_button(mapping_start.mapping,0,
        (mapping_start.mapping[0]+1)%pe::ui::kPadButtons);
    check(s.services.preferences().mapping==mapped,"Controllers mapping action did not edit global bindings");
    check(s.services.game_settings(0).mapping==game_prefs.mapping &&
          s.services.game_settings(0).own_mapping==game_prefs.own_mapping,
          "HOME mapping changed per-game settings");
    s.press({Key::left});
    check(s.services.preferences().mapping==mapping_start.mapping,"HOME mapping could not restore bindings");
    s.services.fail_settings=true;s.press({Key::right});
    check(s.services.preferences().mapping==mapping_start.mapping,"rejected HOME mapping write changed bindings");
    s.services.fail_settings=false;s.press({Key::right});
    check(s.services.preferences().mapping==mapped,"HOME mapping did not roll back after a rejected write");
    s.press({Key::left,Key::circle});s.wait(.6f);s.render();
    check(cover_quads(s).empty(),"mapping Back skipped the Controllers page");
    s.press({Key::cross,Key::right});
    check(s.services.preferences().mapping==mapped,"mapping Back lost the controller mapping row");
    s.press({Key::left,Key::circle});s.wait(.6f);
    s.shoot("switch-mapping-returned-controllers");
    s.press({Key::circle});s.wait(0.7f);s.render();
    const auto returned=cover_quads(s);
    check(std::any_of(returned.begin(),returned.end(),[](const auto &q) {
        return visible_cover(q) && std::fabs(q.rect.w-384.0f)<0.5f;
    }),"Controllers Back did not return directly to HOME");
    s.shoot("switch-home-mapping-returned-home");
    s.press({Key::cross,Key::down,Key::down,Key::cross,Key::right});
    check(s.services.preferences().mapping==mapped,"HOME mapping Back lost Controllers shortcut focus");
    s.services.set_preferences(mapping_start);
    std::fprintf(stderr,"Switch usable-menu contract checks completed: %d checks\n",checks);
}

void switch_presentation_contract_checks(Stage &s)
{
    int checks=0;
    const auto check=[&](bool passed,const char *what) {
        ++checks;
        if(!passed){std::fprintf(stderr,"error: Switch presentation: %s\n",what);s.ok=false;}
    };
    const auto people=s.services.people;
    const auto prefs=s.services.preferences();
    const auto folder=s.services.saved_files_folder();
    const auto library=s.services.games();
    const auto last=s.services.home().last_file;
    s.launcher.reset();
    s.presentation_file=s.output+"/presentation-persistence.cfg";
    std::remove(s.presentation_file.c_str());
    const auto category=[&](int n) {
        s.restart();s.wait(1.0f);home_settings(s);
        for(int i=0;i<n;++i)s.press({Key::down});
        s.press({Key::cross});s.wait(0.3f);
    };
    const auto background_light=[&]() {
        s.render();
        const int x=s.width/2,y=static_cast<int>(s.height*(1.0f-120.0f/1080.0f));
        const std::size_t p=(static_cast<std::size_t>(y)*s.width+x)*4;
        return (static_cast<int>(s.pixels[p])+s.pixels[p+1]+s.pixels[p+2])/3;
    };
    // Basic White changes the whole presentation, survives a new launcher and
    // stays outside emulator/game preferences and the user's games folder.
    category(7);s.press({Key::cross});s.wait(0.4f);
    s.shoot("switch-theme-basic-white-settings");
    s.restart();s.wait(1.0f);
    check(background_light()>220,"Basic White did not survive launcher restart");
    s.shoot("switch-home-basic-white");
    const auto after=s.services.preferences();
    check(after.renderer==prefs.renderer && after.resolution==prefs.resolution &&
              after.filter==prefs.filter && after.volume==prefs.volume && after.language==prefs.language &&
              after.mapping==prefs.mapping,
          "UI theme changed emulator preferences");
    check(s.services.saved_files_folder()==folder,"UI theme changed the games folder");
    category(7);s.press({Key::down,Key::cross});s.wait(0.4f);
    s.restart();s.wait(1.0f);
    check(background_light()<90,"Basic Black did not survive launcher restart");

    // Multiple users no longer add an account picker to each launch. The
    // current real profile and original game path/timer are preserved.
    s.launcher.reset();s.services.people={{"Eden",true},{"Marina",false}};
    s.restart();s.wait(1.0f);s.press({Key::cross});s.wait(0.3f);
    check(s.launcher->selected_game()==s.services.game_path(last),"launch still waits for a user chooser or changes game path");
    check(s.services.people.size()==2 && s.services.people[0].playing && !s.services.people[1].playing,
          "launch changed the current real profile");
    s.shoot("switch-launch-current-user");
    check(!s.launcher->done(),"automatic current-user launch bypassed original launch delay");
    s.wait(1.1f);check(s.launcher->done(),"current-user launch never completed game handoff");

    // Inactive HOME avatars switch user with one confirm and remain on HOME.
    s.services.choose_profile(0);s.restart();s.wait(1.0f);
    s.press({Key::up,Key::right});
    // Confirm without advancing even one animation frame: the backend profile
    // and HOME state must be current before the confirmation frame is rendered.
    s.launcher->press(Key::cross);
    check(s.services.people[1].playing && !s.services.people[0].playing,"HOME avatar row selected wrong profile");
    s.shoot("switch-home-user-focus");
    s.press({Key::down,Key::cross});
    check(s.launcher->selected_game()==s.services.game_path(last),"inactive avatar opened a profile page instead of switching on HOME");
    check(s.services.people[1].playing && !s.services.people[0].playing,"game did not retain the newly selected current user");
    s.wait(1.1f);check(s.launcher->done(),"launch as newly selected user did not hand off");
    s.restart();s.wait(1.0f);
    check(s.services.people[1].playing,"default user switch did not survive a launcher restart");
    s.press({Key::up,Key::right,Key::cross});s.wait(0.3f);
    s.shoot("switch-home-current-user-page");
    s.press({Key::options});s.wait(0.3f);s.render();
    unsigned avatars=0;
    for(const auto &run:s.list.runs())if(run.texture==s.textures.stock_profiles())avatars+=run.count;
    check(avatars>8,"current avatar did not open its profile page and avatar editor");
    s.press({Key::circle,Key::circle});s.wait(0.4f);

    // Avatar and background editing belongs to local UI state; it neither
    // renames nor creates a user. Compare the persisted state before/after and
    // reopen the editor after a fresh launcher to exercise loading it again.
    category(0);s.press({Key::cross});s.wait(0.3f);
    std::string avatar_before,avatar_after;
    pe::read_file(s.presentation_file,&avatar_before);
    const auto editor_covers_edge=[&]() {
        for (const int x : {24,s.width-24}) {
            const std::size_t p=(static_cast<std::size_t>(s.height/2)*s.width+x)*4;
            for (int channel=0;channel<3;++channel)
                if (s.pixels[p+channel]<35 || s.pixels[p+channel]>60) return false;
        }
        return true;
    };
    s.press({Key::options});s.wait(0.3f);s.shoot("switch-user-avatar-grid");
    check(editor_covers_edge(),"icon editor exposes the previous screen at its edges");
    s.press({Key::right,Key::down,Key::cross});s.wait(0.3f);s.shoot("switch-user-background-grid");
    check(editor_covers_edge(),"background editor exposes the previous screen at its edges");
    s.press({Key::right,Key::right,Key::down,Key::cross});s.wait(0.3f);
    check(pe::read_file(s.presentation_file,&avatar_after) && avatar_after!=avatar_before,
          "avatar/background was not persisted");
    pe::ui::PresentationState stored;
    check(stored.load(s.presentation_file) && stored.avatar("Marina").icon==9 &&
              stored.avatar("Marina").color==10,
          "avatar editor persisted the wrong icon or background");
    check(s.services.people.size()==2 && s.services.people[0].name=="Eden" && s.services.people[1].name=="Marina",
          "avatar editor changed real profile identity");
    category(0);s.press({Key::cross,Key::options});s.wait(0.3f);s.shoot("switch-user-avatar-persisted");
    s.press({Key::circle,Key::circle});

    // Repeated HOME browsing must not mark games as played or reorder the scan.
    const auto scanned=s.services.games();
    check(scanned.size()==library.size() && std::equal(scanned.begin(),scanned.end(),library.begin(),
          [](const auto &a,const auto &b){return a.file==b.file;}),
          "presentation changes reordered the backend's scanned game list");
    s.launcher.reset();s.services.people=people;s.services.set_preferences(prefs);
    s.presentation_file.clear();s.restart();
    std::fprintf(stderr,"Switch profile, theme and presentation persistence checks completed: %d checks\n",checks);
}

void switch_home_launch_alignment_checks(Stage &s,const std::string &assets)
{
    int checks=0;
    const auto check=[&](bool passed,const std::string &what) {
        ++checks;
        if(!passed){std::fprintf(stderr,"error: Switch HOME launch: %s\n",what.c_str());s.ok=false;}
    };
    const auto people=s.services.people;
    pe::gfx::Image atlas;
    const bool loaded=pe::gfx::load_tga(assets+"/art/stock-ui.tga",&atlas);
    check(loaded,"authentic UI atlas is missing");
    if(loaded) {
        const auto uv=pe::ui::stock_assets::icon_uv(pe::ui::stock_assets::Icon::launch_effect);
        const int x=static_cast<int>(std::lround(uv.x*atlas.width));
        const int y=static_cast<int>(std::lround(uv.y*atlas.height));
        const int width=static_cast<int>(std::lround(uv.w*atlas.width));
        const int height=static_cast<int>(std::lround(uv.h*atlas.height));
        const bool valid=x>=0 && y>=0 && width>0 && height>0 && x+width<=atlas.width && y+height<=atlas.height;
        check(valid,"launch-effect UV is outside its atlas");
        if(valid) {
            bool mirrored=true;int low=255,high=0;
            const auto alpha=[&](int column,int row){return atlas.rgba[(static_cast<std::size_t>(y+row)*atlas.width+x+column)*4+3];};
            for(int row=0;row<height;++row)for(int column=0;column<width;++column) {
                const int value=alpha(column,row);low=std::min(low,value);high=std::max(high,value);
                mirrored=mirrored && std::abs(value-alpha(width-1-column,row))<=1 &&
                                       std::abs(value-alpha(column,height-1-row))<=1;
            }
            // ResidentMenu stores a quarter mask with mirrored UV0..2. A raw
            // stretched quarter passes center geometry checks but produces the
            // off-center grey rectangle; alpha symmetry detects that defect.
            check(mirrored,"launch-effect quarter mask was not mirrored on both axes");
            check(low==0 && high>250,"launch-effect transparency is flat or empty");
        }
    }
    s.launcher.reset();s.services.people={{"Eden",true}};
    for(int slot:{0,6}) {
        s.restart();s.wait(1.0f);home_library(s);
        s.press({Key::circle});for(int i=0;i<7;++i)s.press({Key::left});
        for(int i=0;i<slot;++i)s.press({Key::right});
        for(int i=0;i<3;++i){s.render();s.wait(0.4f);}s.render();
        // Identify the selected artwork by its current position: the leading
        // slot is at160.5, the settled seventh at1375.5, both full384px squares.
        const float expected_x=160.5f+405.0f*std::min(slot,3);
        const auto resting=cover_quads(s);
        const auto selected=std::find_if(resting.begin(),resting.end(),[&](const auto &q) {
            return visible_cover(q) && std::fabs(q.rect.x-expected_x)<0.5f && std::fabs(q.rect.w-384)<0.5f;
        });
        check(selected!=resting.end(),"cannot identify selected stock-size card before launch");
        if(selected==resting.end())continue;
        const float center_x=selected->rect.x+selected->rect.w*.5f;
        const float center_y=selected->rect.y+selected->rect.h*.5f;
        const auto texture=selected->texture;
        s.launcher->press(Key::cross);
        for(int frame=0;frame<=24;++frame) {
            s.render();
            const auto covers=cover_quads(s);
            const auto card=std::find_if(covers.begin(),covers.end(),[&](const auto &q){return q.texture==texture;});
            check(card!=covers.end(),"selected cover disappeared during confirmation phase");
            if(card!=covers.end()) {
                check(std::fabs(card->rect.x+card->rect.w*.5f-center_x)<0.75f &&
                          std::fabs(card->rect.y+card->rect.h*.5f-center_y)<0.75f,
                      "launch artwork drifted away from its selected tile center");
            }
            for(const auto &instance:s.list.instances()) {
                const int shape=static_cast<int>(instance.params[3]);
                // The white confirmation mask is a square on the artwork.
                // Exclude the fullscreen black handoff mask and static chrome.
                if(shape==static_cast<int>(pe::gfx::Shape::rounded_rect) &&
                   instance.color_top[0]>.99f && instance.color_top[1]>.99f && instance.color_top[2]>.99f &&
                   instance.color_top[3]>.001f && instance.rect[2]>300 && instance.rect[2]<500 &&
                   std::fabs(instance.rect[2]-instance.rect[3])<.5f) {
                    check(std::fabs(instance.rect[0]+instance.rect[2]*.5f-center_x)<.75f &&
                              std::fabs(instance.rect[1]+instance.rect[3]*.5f-center_y)<.75f,
                          "confirmation flash is off-center relative to selected artwork");
                }
            }
            const auto effect_uv=pe::ui::stock_assets::icon_uv(pe::ui::stock_assets::Icon::launch_effect);
            int visible_effects=0;
            for(const auto &run:s.list.runs())if(run.texture==s.textures.stock_ui())
                for(std::uint32_t i=run.first;i<run.first+run.count;++i) {
                    const auto &effect=s.list.instances()[i];
                    if(std::fabs(effect.extra[0]-effect_uv.x)<.00001f &&
                       std::fabs(effect.extra[1]-effect_uv.y)<.00001f &&
                       std::fabs(effect.extra[2]-(effect_uv.x+effect_uv.w))<.00001f &&
                       std::fabs(effect.extra[3]-(effect_uv.y+effect_uv.h))<.00001f && effect.color_top[3]>.001f) {
                        ++visible_effects;
                        check(std::fabs(effect.rect[0]+effect.rect[2]*.5f-center_x)<.75f &&
                                  std::fabs(effect.rect[1]+effect.rect[3]*.5f-center_y)<.75f,
                              "authored launch glow is off-center relative to the selected tile");
                        if(card!=covers.end())
                            check(run.clipped &&
                                      std::fabs(run.clip.x-card->rect.x)<.75f &&
                                      std::fabs(run.clip.y-card->rect.y)<.75f &&
                                      std::fabs(run.clip.w-card->rect.w)<.75f &&
                                      std::fabs(run.clip.h-card->rect.h)<.75f,
                                  "launch light spills beyond the pressed artwork or clips to the HOME row");
                    }
                }
            check(visible_effects==1,"confirmation must draw exactly one visible authored launch glow");
            if(frame==0 || frame==2 || frame==3 || frame==6 || frame==9 || frame==12 || frame==18 || frame==24) {
                char picture[80];std::snprintf(picture,sizeof(picture),"switch-home-launch-slot-%d-frame-%02d",slot+1,frame);
                s.shoot(picture);
            }
            check(glGetError()==GL_NO_ERROR,"launch alignment render reported GL error");
            s.frame();
        }
        s.wait(.8f);check(s.launcher->done(),"aligned launch did not reach original backend handoff");
    }
    s.launcher.reset();s.services.people=people;
    std::fprintf(stderr,"Switch HOME launch alignment checks completed: %d checks\n",checks);
}


// Exercise the v1.000.110 workflows through the actual custom launcher.
void update110_workflow_checks(Stage &s)
{
    int checks = 0;
    const auto check = [&](bool ok, const char *what) {
        ++checks; if(!ok) { std::fprintf(stderr,"error: v110 workflow: %s\n",what); s.ok=false; }
    };
    const auto original_people = s.services.people;
    const auto original_preferences = s.services.preferences();
    // Preview time advances without wall-clock delay. Give real asynchronous
    // service/keyboard futures time to finish before issuing another input.
    const auto settle_async = [&] {
        for(int i=0;i<100;++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            s.frame();
        }
    };
    const auto category = [&](int row) {
        s.restart(); s.wait(.8f); home_settings(s);
        for(int i=0;i<row;++i) s.press({Key::down});
        s.wait(.5f);
    };
    const auto select_game = [&](const char *name) {
        s.restart(); s.wait(.8f); home_library(s,.8f);
        auto games=s.services.games();
        std::stable_sort(games.begin(),games.end(),[](const auto &a,const auto &b){ return a.name<b.name; });
        int row=-1;
        for(int i=0;i<static_cast<int>(games.size());++i) if(games[static_cast<std::size_t>(i)].name==name) row=i;
        check(row>=0,"fixture game found");
        for(int i=0;i<row;++i) s.press({Key::right});
        s.wait(.5f);
    };
    s.fixture_services=nullptr; s.services.room={};
    s.services.people={{"Eden",true},{"Marina",false}};
    s.services.has_server=false;
    category(8); s.shoot("110-01-settings-downloads");
    s.press({Key::cross,Key::cross}); s.wait(.5f); s.shoot("110-02-downloads-not-configured");
    s.press({Key::circle}); s.wait(.5f); s.shoot("110-03-downloads-return-to-settings");

    s.services.has_server=true;
    select_game("Lighthouse Keeper"); s.shoot("110-04-library-remote");
    s.press({Key::square}); s.wait(.5f);
    check(!s.services.downloads_now().empty(),"remote Square queues a download");
    s.shoot("110-05-library-downloading");
    s.press({Key::cross}); s.wait(.5f); s.shoot("110-06-download-and-play");
    for(int i=0;i<3000 && s.launcher->selected_game().empty();++i) s.frame();
    check(!s.launcher->selected_game().empty(),"download completion reaches native game handoff");
    s.shoot("110-07-download-starting");
    bool installed=false;
    for(const auto &game:s.services.games()) if(game.name=="Lighthouse Keeper") installed=!game.remote;
    check(installed,"downloaded game becomes a local game");
    select_game("Lighthouse Keeper");
    s.press({Key::triangle}); s.wait(.5f);
    for(int i=0;i<9;++i) s.press({Key::down});
    s.wait(.5f); s.shoot("110-08-delete-downloaded-game");
    s.press({Key::cross});
    bool still_installed=false;
    for(const auto &game:s.services.games()) if(game.name=="Lighthouse Keeper") still_installed=!game.remote;
    check(still_installed,"first Delete press only asks for confirmation");
    s.wait(.5f); s.shoot("110-09-delete-confirmation");
    s.press({Key::cross}); s.wait(.6f);
    bool remote=false;
    for(const auto &game:s.services.games()) if(game.name=="Lighthouse Keeper") remote=game.remote;
    check(remote,"confirmed Delete keeps the remote entry");
    s.shoot("110-10-deleted-game-library");

    s.services.download_fails=true;
    select_game("Rune Gardens"); s.press({Key::cross}); s.wait(.5f);
    s.shoot("110-11-choose-source");
    s.press({Key::cross}); s.wait(8.0f); s.shoot("110-12-download-failure");
    check(s.launcher->selected_game().empty(),"failed download never launches");
    s.press({Key::square}); s.wait(.5f); s.shoot("110-13-download-cancelled");
    s.services.download_fails=false;
    category(8); s.press({Key::cross,Key::cross}); s.wait(.5f); s.shoot("110-14-download-sources");

    s.services.has_server=false;
    s.restart(); s.wait(.8f); home_shortcut(s,3); s.shoot("110-15-home-multiplayer");
    s.wait(.6f); s.shoot("110-16-multiplayer-choice");
    s.press({Key::cross}); settle_async(); s.wait(.6f); s.shoot("110-17-public-rooms");
    s.press({Key::down,Key::down,Key::down,Key::cross}); s.wait(.4f);
    check(s.services.room.phase==Eden::Multiplayer::Phase::Idle,"incompatible public room is blocked");
    s.shoot("110-18-room-incompatible");
    s.press({Key::up,Key::cross}); settle_async(); s.wait(.5f);
    check(s.services.last_room_connection.host.empty(),"cancelled password entry never joins");
    s.press({Key::up,Key::cross}); s.wait(.5f);
    check(s.services.last_room_connection.host=="192.0.2.1","public join uses selected room endpoint");
    check(s.services.saved_room.password=="private-secret","public join preserves saved private password");
    s.shoot("110-19-room-connecting");
    s.restart(); s.wait(.8f); s.press({Key::cross});
    check(s.launcher->selected_game().empty(),"connecting room blocks game start");
    s.services.room={Eden::Multiplayer::Phase::Connected,"","Friends' room",{"PlayerPS5","Desktop player"}};
    s.restart(); s.wait(.8f); home_shortcut(s,3); s.press({Key::down,Key::cross}); s.wait(.5f);
    s.shoot("110-20-room-connected");
    for(int i=3;i<=16;++i) s.services.room.members.push_back("Player "+std::to_string(i));
    for(int i=0;i<20;++i) s.press({Key::down});
    s.wait(.4f); s.shoot("110-21-room-members-scrolled");
    s.services.room.members.resize(2); s.wait(.4f); s.shoot("110-22-room-members-shrunk");
    s.restart(); s.wait(.8f); s.press({Key::up,Key::right,Key::cross});
    check(s.services.room.phase==Eden::Multiplayer::Phase::Idle,"changing profiles leaves room first");
    check(s.services.people[0].playing,"profile remains current during room disconnect");
    s.press({Key::cross}); check(s.services.people[1].playing,"second profile selection completes after disconnect");
    s.shoot("110-23-profile-after-room-disconnect");
    s.services.people={{"Eden",true},{"Marina",false}}; s.services.room={};

    s.services.save_sync_on=true; s.services.save_sync_conflict=true;
    s.restart(); s.wait(.8f); s.press({Key::cross}); s.wait(2.5f);
    check(s.launcher->selected_game().empty(),"save conflict blocks game handoff until answered");
    s.shoot("110-24-save-conflict");
    s.press({Key::down,Key::cross});
    for(int i=0;i<900 && s.launcher->selected_game().empty();++i) s.frame();
    check(!s.launcher->selected_game().empty(),"resolved save conflict starts current-profile game");
    s.shoot("110-25-save-sync-starting");
    s.services.save_sync_conflict=false; s.services.save_sync_fails=true;
    s.restart(); s.wait(.8f); s.press({Key::cross}); s.wait(3.0f);
    check(s.launcher->selected_game().empty(),"failed save sync waits for explicit user decision");
    s.shoot("110-26-save-sync-failed");
    s.press({Key::circle}); s.wait(.5f);
    check(s.launcher->selected_game().empty(),"cancelling failed save sync does not launch");
    s.services.save_sync_fails=false; s.services.save_sync_on=false;
    category(9); s.shoot("110-27-settings-save-sync");
    s.press({Key::cross,Key::cross}); s.wait(.5f); s.shoot("110-28-save-sync-profiles");
    s.press({Key::cross}); s.wait(.5f); s.shoot("110-29-pairing-servers");
    s.press({Key::cross}); s.wait(1.5f); s.shoot("110-30-pairing-qr-code");
    s.wait(10.0f); s.shoot("110-31-pairing-completed");
    s.press({Key::cross}); s.wait(.5f); s.shoot("110-32-linked-profiles");
    s.press({Key::square}); s.wait(.5f); s.shoot("110-33-unlink-confirmation");

    s.services.people=original_people; s.services.room={}; s.services.has_server=false;
    s.services.save_sync_on=false; s.services.save_sync_fails=false; s.services.save_sync_conflict=false;
    s.services.set_preferences(original_preferences); s.restart(); s.wait(.8f);
    std::fprintf(stderr,"v1.000.110 workflow checks completed: %d checks\n",checks);
}

void update1000_settings_checks(Stage &s)
{
    int checks = 0;
    const auto check = [&](bool passed, const char *what) {
        ++checks;
        if (!passed) { std::fprintf(stderr, "error: v1000 settings: %s\n", what); s.ok = false; }
    };
    const auto original_prefs = s.services.preferences();
    const auto original_game = s.services.game_settings(0);
    const int original_frame_gen = s.services.available_frame_gen;
    const auto game = s.services.games().front();
    s.services.set_game_settings(0, {});
    const auto global_video = [&] {
        s.restart(); s.wait(.8f); home_settings(s, .3f);
        s.press({Key::down, Key::cross}); s.wait(.3f);
    };
    const auto picker = [&](int value) {
        s.press({Key::cross});
        for (int i = 0; i < 24; ++i) s.press({Key::up});
        for (int i = 0; i < value; ++i) s.press({Key::down});
        s.press({Key::cross}); s.wait(.2f);
    };
    const auto open_game = [&](int root_row) {
        s.restart(); s.wait(.8f); home_library(s, .4f); s.press({Key::triangle});
        for (int i = 0; i < root_row; ++i) s.press({Key::down});
        s.press({Key::cross}); s.wait(.2f);
    };
    const auto render_ok = [&](const char *where) {
        s.shoot(where);
        check(glGetError() == GL_NO_ERROR, "settings render reported a GL error");
    };

    // Settings are changed through the launcher's real row navigation and writer.
    global_video();
    for (int i = 0; i < 4; ++i) s.press({Key::down});
    picker(1);
    check(s.services.preferences().astc == 1, "global ASTC did not reach preferences");
    check(s.services.game_settings(0).astc == -1, "global ASTC changed a game's override");
    global_video();
    for (int i = 0; i < 4; ++i) s.press({Key::down});
    s.press({Key::cross, Key::cross}); s.wait(.2f);
    check(s.services.preferences().astc == 1, "reopened global ASTC lost its saved value");
    picker(0);
    check(s.services.preferences().astc == 0, "reopened global ASTC choice did not persist");
    picker(1);
    s.services.fail_settings = true; picker(0);
    check(s.services.preferences().astc == 1, "rejected global ASTC write was retained");
    s.services.fail_settings = false;
    render_ok("v1000-global-astc");

    open_game(1);
    for (int i = 0; i < 3; ++i) s.press({Key::down});
    s.press({Key::right, Key::right});
    check(s.services.game_settings(0).astc == 1 && s.services.last_game_settings_title == game.title_id,
          "per-game ASTC changed the wrong title or field");
    check(s.services.preferences().astc == 1, "per-game ASTC changed global preferences");
    open_game(1);
    for (int i = 0; i < 3; ++i) s.press({Key::down});
    s.press({Key::left});
    check(s.services.game_settings(0).astc == 0, "reopened per-game ASTC lost its saved value");
    s.press({Key::left});
    check(s.services.game_settings(0).astc == -1, "per-game ASTC cannot follow global Settings");
    s.services.fail_settings = true; s.press({Key::right});
    check(s.services.game_settings(0).astc == -1, "rejected per-game ASTC write was retained");
    s.services.fail_settings = false; s.press({Key::right});
    check(s.services.game_settings(0).astc == 0, "per-game ASTC did not roll back after rejection");
    render_ok("v1000-game-astc");

    // The final visible row depends on whether frame generation exists and is ready.
    for (const int availability : {0, 1, 2}) {
        s.services.available_frame_gen = availability;
        global_video();
        const auto before = s.services.preferences();
        for (int i = 0; i < 40; ++i) s.press({Key::down});
        if (availability == 0) {
            s.press({Key::cross});
            check(s.services.preferences().hud == before.hud,
                  "opening overlay options changed the overlay switch");
            s.press({Key::circle, Key::up, Key::cross});
            check(s.services.preferences().hud != before.hud,
                  "overlay options Back did not return to Video above its opener");
        } else if (availability == 1) {
            s.press({Key::right, Key::cross});
            check(s.services.preferences().frame_gen_multiplier == before.frame_gen_multiplier,
                  "unavailable frame generation accepted a setting");
        } else {
            picker(2);
            check(s.services.preferences().frame_gen_multiplier == 2,
                  "global Video bounds lost the frame generation multiplier row");
        }
        check(s.services.preferences().astc == before.astc,
              "global Video bounds wrote ASTC instead of the final row");
        render_ok(("v1000-video-bottom-" + std::to_string(availability)).c_str());
        s.press({Key::circle, Key::circle}); s.wait(.7f); s.render();
        const auto covers = cover_quads(s);
        check(std::any_of(covers.begin(), covers.end(), [](const auto &q) {
            return visible_cover(q) && std::fabs(q.rect.w - 384.0f) < .5f;
        }), "Video Back did not return through Settings to HOME");
    }
    s.services.available_frame_gen = original_frame_gen;

    open_game(5);
    for (int i = 0; i < 40; ++i) s.press({Key::down});
    s.press({Key::right});
    check(s.services.game_settings(0).rng_seed == 0, "RNG right did not enable seed zero");
    const auto type_seed = [&](std::optional<std::int64_t> answer) {
        const auto queries = s.services.seed_queries.load();
        s.services.typed_seed = answer;
        s.press({Key::cross});
        for (int i = 0; i < 100 && s.services.seed_queries == queries; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        s.wait(.25f);
        check(s.services.seed_queries == queries + 1, "RNG row did not call the keyboard service");
    };
    type_seed(0);
    check(s.services.game_settings(0).rng_seed == 0 && s.services.last_seed_current == 0,
          "RNG keyboard cannot keep seed zero");
    type_seed(4294967295LL);
    check(s.services.game_settings(0).rng_seed == 4294967295LL, "RNG keyboard rejected uint32 maximum");
    for (const auto invalid : {-1LL, -2LL, 4294967296LL}) {
        type_seed(invalid);
        check(s.services.game_settings(0).rng_seed == 4294967295LL,
              "invalid RNG input replaced the saved seed");
    }
    type_seed(std::nullopt);
    check(s.services.game_settings(0).rng_seed == 4294967295LL,
          "cancelled RNG keyboard replaced the saved seed");
    s.services.fail_settings = true; type_seed(1234);
    check(s.services.game_settings(0).rng_seed == 4294967295LL,
          "rejected RNG settings write was retained");
    s.services.fail_settings = false; type_seed(1234);
    check(s.services.game_settings(0).rng_seed == 1234 && s.services.last_game_settings_title == game.title_id,
          "RNG accepted input did not persist for the selected title");
    render_ok("v1000-game-system-seed");
    open_game(5);
    s.press({Key::left});
    check(s.services.game_settings(0).rng_seed == -1, "RNG left cannot restore Random after reopening");
    s.press({Key::right});
    const auto queries = s.services.seed_queries.load();
    s.services.seed_wait_for_cancel = true; s.press({Key::cross});
    for (int i = 0; i < 100 && s.services.seed_queries == queries; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    check(s.services.seed_queries == queries + 1, "pending RNG keyboard never started");
    s.press({Key::circle});
    std::this_thread::sleep_for(std::chrono::milliseconds{5}); s.wait(.25f);
    s.services.seed_wait_for_cancel = false;
    check(s.services.game_settings(0).rng_seed == 0, "Back during pending RNG input saved a stale answer");
    s.press({Key::circle}); s.wait(.3f); s.press({Key::circle}); s.wait(.7f); s.render();
    check(!s.launcher->done() && s.launcher->selected_game().empty(),
          "closing RNG settings launched a game");
    const auto covers = cover_quads(s);
    check(std::any_of(covers.begin(), covers.end(), [](const auto &q) {
        return visible_cover(q) && std::fabs(q.rect.w - 261.0f) < .5f;
    }), "RNG settings Back lost the library return destination");
    check(s.services.preferences().astc == 1, "RNG editing changed global ASTC");
    s.services.set_preferences(original_prefs); s.services.set_game_settings(0, original_game);
    s.services.typed_seed.reset(); s.restart();
    std::fprintf(stderr, "v1.001.000 ASTC, RNG, bounds and navigation checks completed: %d checks\n", checks);
}

void update2000_settings_checks(Stage &s)
{
    int checks = 0;
    const auto check = [&](bool passed, const char *what) {
        ++checks;
        if (!passed) { std::fprintf(stderr, "error: v2000 settings: %s\n", what); s.ok = false; }
    };
    const auto original = s.services.preferences();
    const auto game = s.services.game_settings(0);
    const auto category = [&](int index) {
        s.restart(); s.wait(.8f); home_settings(s, .3f);
        for (int i = 0; i < index; ++i) s.press({Key::down});
        s.press({Key::cross}); s.wait(.3f);
    };
    auto prefs = original;
    prefs.vibration = true; prefs.vibration_strength = 100;
    s.services.set_preferences(prefs);
    const auto strength = [&](int value) {
        s.press({Key::cross});
        for (int i = 0; i < 5; ++i) s.press({Key::up});
        for (int i = 0; i < value; ++i) s.press({Key::down});
        s.press({Key::cross}); s.wait(.3f);
    };
    s.restart();s.wait(.8f);home_shortcut(s,4);s.press({Key::down});strength(2);
    check(s.services.preferences().vibration_strength == 75, "vibration strength not saved");
    check(s.services.last_vibration_pulse == 75, "strength preview did not match saved setting");
    s.services.fail_settings = true;
    const auto pulses = s.services.vibration_pulses;
    strength(1);
    check(s.services.preferences().vibration_strength == 75 && s.services.vibration_pulses == pulses,
          "rejected strength write changed controls or pulsed");
    s.services.fail_settings = false;
    s.press({Key::circle}); s.wait(.3f); strength(0);
    check(s.services.preferences().vibration_strength == 25, "strength lower bound");
    s.press({Key::up, Key::cross, Key::down}); strength(1);
    check(!s.services.preferences().vibration && s.services.preferences().vibration_strength == 50,
          "disabled vibration prevented strength persistence");
    const auto muted_pulses = s.services.vibration_pulses;
    strength(2);
    check(s.services.vibration_pulses == muted_pulses, "disabled vibration still pulsed");
    s.wait(.8f); s.shoot("v2000-vibration-strength");
    category(1);
    for (int i = 0; i < 7; ++i) s.press({Key::down});
    s.press({Key::cross}); s.wait(.3f);
    const auto before = s.services.preferences();
    s.press({Key::cross, Key::down, Key::right, Key::down, Key::right});
    const auto changed = s.services.preferences();
    check(changed.hud_colors != before.hud_colors && changed.hud_size == (before.hud_size + 1) % 3 &&
          changed.hud_corner == (before.hud_corner + 1) % 4, "overlay presentation controls");
    s.services.fail_settings = true; s.press({Key::right});
    check(s.services.preferences().hud_corner == changed.hud_corner, "rejected corner change was retained");
    s.services.fail_settings = false;
    for (int row = 0; row < 9; ++row) s.press({Key::down, Key::cross});
    check(s.services.preferences().hud_fields == (before.hud_fields ^ 0x1ff), "overlay field mask");
    s.wait(.8f); s.shoot("v2000-overlay-options-end");
    s.press({Key::circle, Key::up, Key::cross});
    check(s.services.preferences().hud != before.hud, "overlay Back lost Video selection");
    s.restart(); s.wait(.8f);
    check(s.services.preferences().hud_corner == changed.hud_corner, "overlay choice lost on reopen");
    check(s.services.game_settings(0).hud_size == game.hud_size &&
          s.services.game_settings(0).hud_corner == game.hud_corner, "global overlay changed per-game overrides");
    s.services.set_preferences(original);
    std::fprintf(stderr, "v1.002.000 vibration and overlay checks completed: %d checks\n", checks);
}

void integration_checks(Stage &s, const std::string &assets)
{
    switch_home_checks(s);
    switch_navigation_checks(s);
    switch_library_fidelity_checks(s);
    switch_menu_checks(s);
    switch_presentation_contract_checks(s);
    switch_home_launch_alignment_checks(s,assets);
    update110_workflow_checks(s);
    update1000_settings_checks(s);
    update2000_settings_checks(s);
}

void pictures(Stage &s,const std::string &assets)
{
    integration_checks(s, assets);
    // Home, as it settles after the app opens.
    s.restart(true);
    s.wait(0.42f);
    s.shoot("00-arriving");
    s.wait(3.0f);
    s.shoot("01-home");
    for(int shortcut=0;shortcut<4;++shortcut) {
        const char *names[]={"switch-shortcut-about","switch-shortcut-files","switch-shortcut-multiplayer","switch-shortcut-controllers"};
        s.restart();s.wait(1.0f);s.press({Key::down});
        for(int i=0;i<shortcut;++i)s.press({Key::right});
        s.wait(.6f);s.shoot(names[shortcut]);
    }
    s.restart();s.wait(1.0f);
    s.press({Key::right});
    s.wait(0.6f);
    s.shoot("02-home-second-game-focus");
    s.press({Key::down, Key::right, Key::right, Key::right, Key::right});
    s.wait(0.6f);
    s.shoot("03-home-settings-focus");
    s.press({Key::left, Key::left, Key::left, Key::left});
    s.wait(0.6f);
    s.shoot("04-home-about-focus");

    // Library.
    s.restart();
    s.wait(1.0f);
    for(int i=0;i<7;++i)s.press({Key::right});s.press({Key::cross});
    s.wait(0.14f);
    s.shoot("05-opening-library");
    s.wait(1.2f);
    s.shoot("06-library");
    for(int i=0;i<9;++i)s.press({Key::right});
    s.wait(0.8f);
    s.shoot("07-library-scrolled");
    s.press({Key::triangle, Key::right, Key::circle});
    s.wait(0.6f);
    s.shoot("08-library-handheld");
    // The Library's Mods switch: all of this game's mods off, and on again.
    s.press({Key::square});
    s.wait(0.6f);
    s.shoot("44-library-mods-off");
    s.press({Key::square});
    s.wait(0.6f);
    s.shoot("45-library-mods-on");
    s.press({Key::triangle});
    s.wait(0.8f);
    s.shoot("09-game-settings");
    // The game's own Video: a renderer of its own, then the output's refresh rate.
    s.press({Key::down, Key::cross});
    s.wait(0.6f);
    s.shoot("49-game-video");
    s.press({Key::right});
    s.wait(0.6f);
    s.shoot("10-game-settings-changed");
    s.press({Key::down, Key::down, Key::down});
    s.wait(.6f);s.shoot("v1000-game-astc-row");
    s.press({Key::down, Key::right, Key::right});
    s.wait(0.6f);
    s.shoot("40-game-refresh");
    // Its own Performance: Compile ahead switched on for this game only.
    s.press({Key::circle, Key::down, Key::cross, Key::right, Key::right});
    s.wait(0.6f);
    s.shoot("50-game-performance");
    // Its own Controls: a button mapping of its own, and A moved to another button.
    s.press({Key::circle, Key::down, Key::down, Key::cross, Key::down, Key::right});
    s.wait(0.6f);
    s.shoot("51-game-controls");
    s.press({Key::cross, Key::right});
    s.wait(0.6f);
    s.shoot("52-game-mapping");
    {
        const pe::ui::GameSettings game = s.services.game_settings(0);
        if (game.renderer != 0 || game.refresh != 1 || game.performance[0] != 1 || !game.own_mapping ||
            game.mapping[0] != 2 || game.mapping[3] != 1)
        {
            std::fprintf(stderr, "error: the game's own settings did not reach the settings\n");
            s.ok = false;
        }
    }
    // Back in the game's settings: the kinds it changed say how many.
    s.press({Key::circle, Key::circle});
    s.wait(0.6f);
    s.shoot("53-game-settings-changed");
    // The game's mods: the row, the list, one switched on, and a game that has none.
    s.press({Key::down, Key::down, Key::down});
    s.wait(0.6f);
    s.shoot("35-game-mods-row");
    s.press({Key::cross});
    s.wait(0.6f);
    s.shoot("36-mods");
    s.press({Key::down, Key::cross});
    s.wait(0.4f);
    s.shoot("37-mods-switched");
    // A mod that lists several cheats: each has its switch, and one frame rate takes the place
    // of the other.
    s.press({Key::down, Key::down, Key::down, Key::cross});
    s.wait(0.6f);
    s.shoot("46-mods-cheats");
    s.services.has_mods = false;
    s.press({Key::circle, Key::cross});
    s.wait(0.6f);
    s.shoot("38-mods-none");
    s.press({Key::square});
    s.wait(0.4f);
    s.shoot("39-mods-folder-made");
    s.services.has_mods = true;
    s.press({Key::circle, Key::down});
    s.wait(0.6f);
    s.shoot("32-game-save-data");
    s.press({Key::cross});
    s.wait(0.4f);
    s.shoot("33-game-save-asked");
    s.press({Key::square});
    s.wait(0.4f);
    s.shoot("34-game-save-exported");
    s.press({Key::circle});
    s.wait(0.3f);
    s.press({Key::cross});
    s.wait(0.30f);
    s.shoot("11-launching");
    s.wait(0.32f);
    s.shoot("12-launching-late");

    // A newer release the app cannot install itself: the notification at the top right, for ten
    // seconds.
    s.restart();
    s.wait(1.0f);
    s.services.update_version = "v1.000.060";
    s.services.update_installable = false;
    s.wait(1.2f);
    s.shoot("58-update-notice");
    s.wait(9.5f);
    s.shoot("59-update-notice-gone");
    s.services.update_installable = true;

    // One it can: the offer when the app opens, then the install (a step per frame in the
    // preview: 40 preparing, 360 downloading, 120 unpacking).
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(2.6f);
    s.shoot("70-update-offer");
    s.press({Key::right, Key::right});
    s.wait(0.4f);
    s.shoot("71-update-offer-skip");
    s.press({Key::left, Key::left, Key::cross});
    s.wait(0.3f);
    s.shoot("72-update-preparing");
    s.wait(3.2f);
    s.shoot("73-update-downloading");
    s.wait(3.5f);
    s.shoot("74-update-unpacking");
    s.wait(2.2f);
    s.shoot("75-update-ready");
    s.wait(0.9f);
    s.shoot("76-update-ready-late");
    // Cancelled while it downloads: the dialog closes, nothing changed.
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(2.6f);
    s.press({Key::cross});
    s.wait(1.5f);
    s.press({Key::circle});
    s.wait(0.1f);
    s.shoot("77-update-cancelling");
    s.wait(1.0f);
    s.shoot("78-update-cancelled");
    // A failure: what went wrong, and Try again or Close.
    s.services.update_version = "v1.000.060";
    s.services.update_fails = true;
    s.restart(true);
    s.wait(2.6f);
    s.press({Key::cross});
    s.wait(7.4f);
    s.shoot("79-update-failed");
    s.services.update_fails = false;

    // The release notes: What's new on the offer opens them; they scroll a few lines at a time
    // (Up/Down) or a page (L1/R1), give a little at the end, and Circle goes back to the offer.
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(2.6f);
    s.press({Key::right});
    s.wait(0.4f);
    s.shoot("80-update-offer-whats-new");
    s.press({Key::cross});
    s.wait(0.12f);
    s.shoot("81-update-notes-opening");
    s.wait(1.0f);
    s.shoot("82-update-notes");
    s.press({Key::down, Key::down}, 0.15f);
    s.wait(0.6f);
    s.shoot("83-update-notes-scrolled");
    s.press({Key::r1, Key::r1, Key::r1}, 0.15f);
    s.wait(0.7f);
    s.shoot("84-update-notes-end");
    s.press({Key::down});
    s.wait(0.05f);
    s.shoot("85-update-notes-end-give");
    s.press({Key::right});
    s.wait(0.4f);
    s.shoot("86-update-notes-back-button");
    s.press({Key::circle});
    s.wait(0.5f);
    s.shoot("87-update-offer-after-notes");
    // A release without notes: two buttons, as before.
    const std::string notes = s.services.update_notes;
    s.services.update_notes.clear();
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(2.6f);
    s.shoot("88-update-offer-no-notes");
    // Notes the catalog cut short end with where the rest is.
    s.services.update_notes = notes;
    s.services.update_notes_truncated = true;
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(2.6f);
    s.press({Key::triangle});
    s.wait(1.2f);
    // (The preview draws only when it takes a picture; the notes are laid out when first drawn.)
    s.shoot("89-update-notes-truncated");
    s.press({Key::r1, Key::r1, Key::r1, Key::r1}, 0.15f);
    s.wait(0.8f);
    s.shoot("89-update-notes-truncated-end");
    s.services.update_notes_truncated = false;

    // A game's file taken away while the menu shows it: within a moment it leaves the home screen
    // (another recent game takes its place) and the Library.
    s.restart();
    s.wait(1.0f);
    for (const pe::ui::Game &game : s.services.games())
        if (game.name == "Echoes of the Valley")
            s.services.removed.push_back(game.file);
    s.wait(2.5f);
    s.shoot("56-home-game-removed");
    home_library(s,0.0f);
    s.wait(1.2f);
    s.shoot("57-library-game-removed");
    s.services.removed.clear();

    // Settings and its dialogs.
    s.restart();
    s.wait(1.0f);
    home_settings(s,0.0f);
    s.wait(1.0f);
    s.shoot("13-settings");
    // Profiles, the first category: a new one, playing as it, and taking it off the list again.
    s.press({Key::cross,Key::cross});
    s.wait(0.8f);
    s.shoot("60-profiles");
    // The highlight follows the selection (it glides: the picture is taken once it has arrived).
    s.press({Key::down});
    s.wait(0.6f);
    s.shoot("60b-profiles-moved");
    s.press({Key::up});
    s.wait(0.2f);
    s.press({Key::down, Key::cross});
    s.wait(0.6f);
    s.shoot("61-profile-added");
    s.press({Key::cross});
    s.wait(0.6f);
    s.shoot("62-profile-playing");
    {
        const std::vector<pe::ui::Profile> people = s.services.profiles();
        bool reached = people.size() == 2 && !people[0].playing && people[1].playing;
        // The one that is playing cannot be taken off the list; the other one can, asked twice.
        s.press({Key::square, Key::square});
        reached = reached && s.services.profiles().size() == 2;
        s.press({Key::up, Key::cross, Key::down, Key::square});
        s.wait(0.6f);
        s.shoot("63-profile-remove-asked");
        reached = reached && s.services.profiles().size() == 2;
        s.press({Key::square});
        reached = reached && s.services.profiles().size() == 1 && s.services.profiles()[0].playing;
        if (!reached)
        {
            std::fprintf(stderr, "error: the profiles did not reach the services\n");
            s.ok = false;
        }
    }
    s.press({Key::circle, Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("14-video");
    const auto picture_choice=[&](int selected) {
        s.press({Key::cross});for(int i=0;i<20;++i)s.press({Key::up});
        for(int i=0;i<selected;++i)s.press({Key::down});s.press({Key::cross});
    };
    s.press({Key::down});picture_choice(1);
    s.wait(0.6f);
    s.shoot("15-video-saved");
    s.press({Key::down, Key::down, Key::down});
    s.wait(.6f);s.shoot("v1000-video-astc-row");
    s.press({Key::down});picture_choice(1);
    s.wait(0.6f);
    s.shoot("41-video-refresh");
    // The seventh row: the list scrolls to it.
    s.press({Key::down});
    s.wait(0.6f);
    s.shoot("43-video-overlay");
    // Performance: seven switches, four of them showing. Reactive flushing starts on, the
    // others off, and a press on each one reaches the settings.
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("46-performance");
    {
        const auto states = [&s]
        {
            const pe::ui::Preferences p = s.services.preferences();
            return std::array<bool, 7>{p.block_list, p.async_shaders, p.fast_gpu, p.unsafe_cpu,
                                       p.unsafe_dma, p.reactive_flushing, p.skip_invalidation};
        };
        const std::array<bool, 7> start = states();
        bool reached = start == std::array<bool, 7>{false, false, false, false, false, true, false};
        for (std::size_t row = 0; row < start.size(); ++row)
        {
            if (row != 0)
                s.press({Key::down});
            s.press({Key::cross});
            std::array<bool, 7> expected = start;
            for (std::size_t changed = 0; changed <= row; ++changed)
                expected[changed] = !start[changed];
            reached = reached && states() == expected;
            if (row == 2)
            {
                s.wait(0.6f);
                s.shoot("47-performance-changed");
            }
        }
        // The seventh row: the list scrolled to it.
        s.wait(0.6f);
        s.shoot("48-performance-scrolled");
        if (!reached)
        {
            std::fprintf(stderr, "error: the Performance switches did not reach the settings\n");
            s.ok = false;
        }
    }
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("16-audio");
    s.press({Key::circle,Key::circle});s.wait(.8f);s.restart();s.wait(1.0f);
    home_shortcut(s,4);
    s.wait(0.8f);
    s.shoot("17-controls");
    // The button mapping: B moved to Circle takes A's place (A gets Cross), then all back.
    s.press({Key::down, Key::down});
    s.wait(0.4f);
    s.shoot("54-controls-mapping-row");
    s.press({Key::cross, Key::down, Key::right});
    s.wait(0.6f);
    s.shoot("55-mapping");
    const pe::ui::ButtonMapping moved = s.services.preferences().mapping;
    s.press({Key::square});
    s.wait(0.4f);
    if (moved[0] != 0 || moved[1] != 1 || s.services.preferences().mapping != pe::ui::kDefaultMapping)
    {
        std::fprintf(stderr, "error: the button mapping did not reach the settings\n");
        s.ok = false;
    }
    s.press({Key::circle});
    s.wait(0.4f);
    s.press({Key::circle});s.wait(.8f);s.restart();s.wait(1.0f);home_settings(s);
    for(int i=0;i<4;++i)s.press({Key::down});
    s.press({Key::cross});
    s.wait(0.8f);
    s.shoot("30-accessibility");
    s.press({Key::down, Key::down});
    s.wait(0.6f);
    s.shoot("31-accessibility-motion");
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("18-diagnostics");
    s.press({Key::circle,Key::circle});s.wait(.8f);s.restart();s.wait(1.0f);home_shortcut(s,2);
    s.wait(1.0f);
    s.shoot("19-game-files");
    s.press({Key::down, Key::down, Key::triangle});
    s.wait(0.6f);
    s.shoot("20-game-files-saved");
    s.press({Key::circle});
    s.wait(0.5f);
    s.restart();s.wait(1.0f);home_settings(s);
    for(int i=0;i<6;++i)s.press({Key::down});
    s.press({Key::cross,Key::cross});
    s.wait(1.0f);
    s.shoot("21-language");
    s.press({Key::down, Key::down, Key::down, Key::down, Key::down, Key::down, Key::down,
             Key::down, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("22-language-chosen");

    // Two profiles: the home screen names who is playing.
    s.services.add_profile();
    s.restart();
    s.wait(2.0f);
    s.shoot("64-home-two-profiles");
    s.services.people.resize(1);

    // About.
    s.restart();
    s.wait(1.0f);
    s.press({Key::down, Key::cross});
    s.wait(1.0f);
    s.shoot("23-about");

    // What the home screen says when something is wrong, and before any game was played.
    s.services.setup_ready = false;
    s.restart();
    s.wait(2.0f);
    s.shoot("24-home-setup-required");
    s.services.setup_ready = true;
    s.services.launch_error = "The game's keys are missing from prod.keys.";
    s.restart();
    s.wait(2.0f);
    s.shoot("25-home-launch-failed");
    s.services.launch_error.clear();
    s.services.crash_report = "/data/prosperoeden/logs/crash-20261001-213000.txt";
    s.restart();
    s.wait(2.0f);
    s.shoot("25b-home-crash-report");
    s.services.crash_report.clear();
    s.services.has_history = false;
    s.restart();
    s.wait(2.0f);
    s.shoot("26-home-first-run");
    s.services.has_history = true;

    // Controllers: one, then a third one joining (caught mid-bounce), then all four.
    s.services.connected_controllers = 0b0001;
    s.restart();
    s.wait(2.0f);
    s.shoot("27-home-one-controller");
    s.services.connected_controllers = 0b0101;
    s.wait(0.2f);
    s.shoot("28-home-controller-joining");
    s.services.connected_controllers = 0b1111;
    s.wait(2.0f);
    s.shoot("29-home-four-controllers");
    s.services.connected_controllers = 0b0011;
}

// A walk through the launcher, one frame per call of frame().
void tour(Stage &s)
{
    s.services.connected_controllers = 0b0001;
    s.restart(true);
    s.wait(2.0f);
    // A second controller is switched on, then a third.
    s.services.connected_controllers = 0b0011;
    s.wait(1.2f);
    s.services.connected_controllers = 0b0111;
    s.wait(1.4f);
    s.press({Key::right, Key::left, Key::down, Key::right, Key::right, Key::left, Key::up}, 0.32f);
    s.wait(0.5f);
    s.press({Key::up}, 0.4f);
    s.press({Key::cross});
    s.wait(1.0f);
    s.press({Key::down, Key::down, Key::down, Key::down, Key::down, Key::down, Key::down,
             Key::down, Key::up, Key::up},
            0.22f);
    s.press({Key::right}, 0.6f);
    s.press({Key::triangle}, 0.8f);
    s.press({Key::down, Key::right, Key::down, Key::right}, 0.4f);
    s.press({Key::circle}, 0.6f);
    s.press({Key::circle}, 0.8f);
    s.press({Key::right}, 0.3f);
    s.press({Key::cross});
    s.wait(0.9f);
    s.press({Key::down, Key::down, Key::up}, 0.3f);
    s.press({Key::cross}, 0.8f);
    s.press({Key::right, Key::down, Key::left}, 0.4f);
    s.press({Key::circle}, 0.6f);
    s.press({Key::circle}, 0.9f);
    s.press({Key::down}, 0.5f);
    s.press({Key::cross});
    s.wait(1.2f);
}

// PE_TOUR=update: the app opens with a newer release listed; the offer, Update now, the download
// and unpacking, and ProsperoEden closing for the update.
void update_tour(Stage &s)
{
    s.services.connected_controllers = 0b0001;
    s.services.update_version = "v1.000.060";
    s.restart(true);
    s.wait(3.4f);
    // What's new: the notes open, scroll down a little at a time, a page, then back.
    s.press({Key::right}, 0.6f);
    s.press({Key::cross}, 1.6f);
    s.press({Key::down}, 0.7f);
    s.press({Key::down}, 0.7f);
    s.press({Key::down}, 0.9f);
    s.press({Key::r1}, 1.2f);
    s.press({Key::r1}, 1.4f);
    s.press({Key::circle}, 0.9f);
    s.press({Key::left}, 0.6f);
    s.press({Key::cross});
    s.wait(10.5f);
}

} // namespace

namespace
{

// Text that did not fit its place at full size, and how it was fitted (the smallest scale seen).
std::map<std::string, std::pair<float, bool>> fits;

void note_fit(std::string_view text, float scale, bool cut)
{
    auto [entry, added] = fits.try_emplace(std::string{text}, scale, cut);
    if (!added)
    {
        entry->second.first = std::min(entry->second.first, scale);
        entry->second.second = entry->second.second || cut;
    }
}

// The catalog reader, the pattern filler and the choice of catalog, checked before anything is drawn.
bool strings_check()
{
    pe::Catalog catalog;
    const std::size_t count = catalog.load("\xef\xbb\xbf# comment\n"
                                           "msgid \"\"\nmsgstr \"Language: xx\\n\"\n\n"
                                           "#. note\n#: file.cpp\n"
                                           "msgid \"Back\"\r\nmsgstr \"Voltar\"\r\n\n"
                                           "msgid \"Say \\\"{0}\\\" twice\"\n"
                                           "msgstr \"\"\n\"Diga \\\"{0}\\\" \"\n\"duas vezes\"\n\n"
                                           "msgid \"Untranslated\"\nmsgstr \"\"\n");
    const auto same = [](const std::vector<std::string> &left, std::initializer_list<const char *> right)
    { return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                        [](const std::string &a, const char *b) { return a == b; }); };
    const bool ok =
        count == 2 && catalog.find("Back") == "Voltar" &&
        catalog.find("Say \"{0}\" twice") == "Diga \"{0}\" duas vezes" &&
        catalog.find("Untranslated") == "Untranslated" && catalog.find("Missing") == "Missing" &&
        pe::fill("{0} OF {1}", {"3", "12"}) == "3 OF 12" && pe::fill("{1}{0}{2}", {"a", "b"}) == "ba" &&
        pe::fill("{x} {0", {"a"}) == "{x} {0" &&
        same(pe::catalog_candidates("fr-CA"), {"fr-CA", "fr-FR"}) &&
        same(pe::catalog_candidates("pt-PT"), {"pt-PT", "pt-BR"}) &&
        same(pe::catalog_candidates("es-419"), {"es-419", "es-ES"}) &&
        same(pe::catalog_candidates("de-DE"), {"de-DE"}) && pe::catalog_candidates("en-GB").empty() &&
        pe::catalog_candidates("en-US").empty() && pe::catalog_candidates("").empty();
    if (!ok)
        std::fprintf(stderr, "error: the launcher's text functions fail their check\n");
    return ok;
}


// The live desktop preview has exactly64 invented apps so its real six-column
// library can be scrolled through11 rows. This adapter and its placeholders are
// never linked into the console application. All other sample actions still
// reach FakeServices, including profile edits, mappings, saves and mod switches.
class InteractiveLibraryServices final : public pe::ui::Services
{
  public:
    static constexpr std::size_t kAppCount=64;
    explicit InteractiveLibraryServices(pe::host::FakeServices &base) : base_(base)
    {
        entries_=base_.games();
        if(entries_.size()>kAppCount)entries_.resize(kAppCount);
        std::vector<pe::ui::Game> artwork;
        for(const auto &game:entries_)if(!game.cover.empty())artwork.push_back(game);
        if(artwork.empty())artwork=entries_;
        if(artwork.empty())return;
        while(entries_.size()<kAppCount) {
            const std::size_t index=entries_.size();
            auto game=artwork[index%artwork.size()];
            char name[32];std::snprintf(name,sizeof(name),"Preview App %02u",static_cast<unsigned>(index+1));
            game.name=name;
            game.file=game.name+".nsp";
            game.format="NSP";
            game.title_id=0x0100F00000000000ull+static_cast<std::uint64_t>(index+1)*0x10000ull;
            game.addons.clear();game.addons_short.clear();game.language_note.clear();
            game.mods=game.mods_on=0;
            entries_.push_back(std::move(game));
        }
    }
    pe::ui::Home home() override {
        auto result=base_.home();
        result.system_status=std::to_string(games().size())+" preview apps";
        return result;
    }
    std::string clock() override {return base_.clock();}
    unsigned controllers() override {return base_.controllers();}
    std::string version() override {return base_.version();}
    bool take_update(pe::ui::UpdateOffer *offer) override {return base_.take_update(offer);}
    bool start_update() override {return base_.start_update();}
    pe::ui::UpdateStatus update_status() override {return base_.update_status();}
    void cancel_update() override {base_.cancel_update();}
    bool apply_update() override {return base_.apply_update();}
    void finish_update() override {base_.finish_update();}
    std::vector<pe::ui::Game> games() override {
        std::vector<pe::ui::Game> result;
        for(const auto &game:entries_)if(base_.game_exists(game.file))result.push_back(game);
        return result;
    }
    std::string game_path(const std::string &file) override {return base_.game_path(file);}
    bool game_exists(const std::string &file) override {return base_.game_exists(file);}
    bool docked(std::uint64_t title) override {return base_.docked(title);}
    bool set_docked(std::uint64_t title,bool value) override {return base_.set_docked(title,value);}
    pe::ui::GameSettings game_settings(std::uint64_t title) override {return base_.game_settings(title);}
    bool set_game_settings(std::uint64_t title,const pe::ui::GameSettings &value) override {return base_.set_game_settings(title,value);}
    pe::ui::Preferences preferences() override {return base_.preferences();}
    bool set_preferences(const pe::ui::Preferences &value) override {return base_.set_preferences(value);}
    const std::vector<std::string> &resolution_labels() override {return base_.resolution_labels();}
    const std::vector<std::string> &resolution_keys() override {return base_.resolution_keys();}
    const std::vector<std::string> &filter_labels() override {return base_.filter_labels();}
    const std::vector<std::string> &astc_labels() override {return base_.astc_labels();}
    int frame_gen_state() override {return base_.frame_gen_state();}
    std::optional<std::int64_t> ask_seed(std::int64_t current, const std::atomic<bool> &stop) override {
        return base_.ask_seed(current, stop);
    }
    const std::vector<std::string> &language_labels() override {return base_.language_labels();}
    std::string language_region(int n) override {return base_.language_region(n);}
    std::string setup_details() override {return base_.setup_details();}
    bool folders(const std::string &folder,std::vector<std::string> *names) override {return base_.folders(folder,names);}
    pe::ui::FolderInfo folder_info(const std::string &folder) override {return base_.folder_info(folder);}
    std::string files_folder() override {return base_.files_folder();}
    std::string saved_files_folder() override {return base_.saved_files_folder();}
    std::string default_files_folder() override {return base_.default_files_folder();}
    bool set_files_folder(const std::string &folder) override {return base_.set_files_folder(folder);}
    int filesystem_access() override {return base_.filesystem_access();}
    std::vector<pe::ui::Profile> profiles() override {return base_.profiles();}
    bool choose_profile(int index) override {return base_.choose_profile(index);}
    int add_profile() override {return base_.add_profile();}
    bool rename_profile(int index,int step) override {return base_.rename_profile(index,step);}
    bool remove_profile(int index) override {return base_.remove_profile(index);}
    bool save_transfer_available() override {return base_.save_transfer_available();}
    pe::ui::SaveSource save_import_source(std::uint64_t title) override {return base_.save_import_source(title);}
    bool save_import(std::uint64_t title,std::string *message) override {return base_.save_import(title,message);}
    bool save_export(std::uint64_t title,std::string *message) override {return base_.save_export(title,message);}
    std::vector<pe::ui::Mod> mods(std::uint64_t title) override {return base_.mods(title);}
    bool set_mod_enabled(std::uint64_t title,const std::string &name,bool enabled) override {return base_.set_mod_enabled(title,name,enabled);}
    bool set_cheat_enabled(std::uint64_t title,const std::string &mod,const std::string &cheat,bool enabled) override {return base_.set_cheat_enabled(title,mod,cheat,enabled);}
    bool mods_enabled(std::uint64_t title) override {return base_.mods_enabled(title);}
    bool set_mods_enabled(std::uint64_t title,bool enabled) override {return base_.set_mods_enabled(title,enabled);}
    std::string mods_folder(std::uint64_t title) override {return base_.mods_folder(title);}
    bool make_mods_folder(std::uint64_t title) override {return base_.make_mods_folder(title);}
    Eden::Multiplayer::RoomDirectory public_rooms(const std::atomic<bool> &stop) override {
        return base_.public_rooms(stop);
    }
    Eden::Multiplayer::Snapshot room_status() override {
        // Complete the invented connection inside the desktop preview only.
        if(base_.room.phase==Eden::Multiplayer::Phase::Connecting && ++room_polls_>=45)
            base_.room={Eden::Multiplayer::Phase::Connected,{},preview_room_,
                        {base_.last_room_connection.nickname,"Preview guest"}};
        return base_.room_status();
    }
    Eden::Multiplayer::Connection room_connection() override {return base_.room_connection();}
    void remember_room(const Eden::Multiplayer::Connection &connection,bool private_room) override {
        base_.remember_room(connection,private_room);
    }
    bool connect_public_room(const Eden::Multiplayer::Connection &connection) override {
        room_polls_=0;preview_room_="Public preview room";
        for(const auto &room:base_.directory.rooms)
            if(room.address==connection.host && room.port==connection.port)preview_room_=room.name;
        return base_.connect_public_room(connection);
    }
    bool connect_room(const Eden::Multiplayer::Connection &connection) override {
        room_polls_=0;preview_room_="Private preview room";
        return base_.connect_room(connection);
    }
    void leave_room() override {room_polls_=0;base_.leave_room();}
    std::optional<std::string> room_text(int field,const std::string &initial,
                                       const std::atomic<bool> &stop) override {
        return base_.room_text(field,initial,stop);
    }
    bool can_launch_game() override {return base_.can_launch_game();}
    bool load_image(const std::string &path,pe::gfx::Image *image) override {return base_.load_image(path,image);}
  private:
    pe::host::FakeServices &base_;
    std::vector<pe::ui::Game> entries_;
    int room_polls_=0;
    std::string preview_room_;
};

// Optional host capture mode for reviewing the same64-entry adapter used by the
// live browser. It is never enabled by normal interactive preview startup.
void interactive_library_pictures(Stage &s)
{
    s.restart();s.wait(1.0f);home_library(s);
    for(int i=0;i<3;++i){s.render();s.wait(.4f);}
    s.shoot("switch-library-64-top");
    s.press({Key::r1,Key::r1,Key::r1,Key::r1});s.wait(.8f);
    for(int i=0;i<3;++i){s.render();s.wait(.4f);}
    s.shoot("switch-library-64-end");
    s.restart(true);s.heard.clear();
}

// Interactive preview is host-only. Every input goes to the actual Launcher;
// FakeServices keeps all profile/settings/save actions confined to sample data.
int interactive(Stage &s, const std::string &assets)
{
    if(s.services.people.size()==1 && s.services.people[0].name=="Eden")
        s.services.people.push_back({"Marina",false});
    InteractiveLibraryServices samples{s.services};
    if(samples.games().size()!=InteractiveLibraryServices::kAppCount) {
        std::fprintf(stderr,"interactive preview could not create its64-app sample library\n");return 1;
    }
    // The launcher destructor joins its scan worker before the adapter leaves
    // scope, including malformed-input and GL-error exits from this function.
    struct SampleScope {
        Stage &stage;
        ~SampleScope(){stage.launcher.reset();stage.fixture_services=nullptr;}
    } sample_scope{s};
    s.launcher.reset();s.fixture_services=&samples;
    std::fprintf(stderr,"Interactive preview:64 sample apps; HOME retains its seven-game cap\n");
    std::string word, scene;
    if (!pe::read_file(assets+"/../../loading_text.glsl", &word) ||
        !pe::read_file(assets+"/../../loading_scene.glsl", &scene)) return 1;
    const std::string fragment=word+scene+
        "\nuniform vec2 size;uniform float seconds;out vec4 color;"
        "void main(){color=vec4(loading_scene(gl_FragCoord.xy,size,seconds),1.0);}\n";
    const char *vertex="void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                       "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
    pe::gfx::set_glsl_prefix("#version 330 core\n");
    GLuint program=pe::gfx::build_program("Interactive loading preview",vertex,fragment.c_str());
    pe::gfx::set_glsl_prefix("#version 450 core\n");
    if (!program) return 1;
    GLuint vao=0;glGenVertexArrays(1,&vao);
    const std::map<std::string,Key> buttons{{"up",Key::up},{"down",Key::down},
        {"left",Key::left},{"right",Key::right},{"cross",Key::cross},{"circle",Key::circle},
        {"square",Key::square},{"triangle",Key::triangle},{"options",Key::options},
        {"l1",Key::l1},{"r1",Key::r1}};
    stbi_write_png_compression_level=1;
    s.restart(true);float boot_seconds=0.0f;
    if(const char *capture=std::getenv("PE_CAPTURE_LIBRARY64");capture && std::string_view{capture}=="1")
        interactive_library_pictures(s);
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream input(line);std::string button;int steps=0;
        if (!(input>>button>>steps) || steps<0 || steps>30) return 2;
        if (button=="exit") break;
        if (button=="reset") {s.restart(true);boot_seconds=0;s.heard.clear();}
        else if (button!="frame") {
            const auto key=buttons.find(button);if(key==buttons.end()) return 2;
            if (s.launcher->done()) {
                if(key->second==Key::circle || key->second==Key::options) {
                    s.restart(false);boot_seconds=0;
                }
            } else s.launcher->press(key->second);
        }
        for(int i=0;i<steps;++i) {
            if(s.launcher->done()) boot_seconds+=1.0f/60.0f;
            else s.frame();
        }
        // Include cues caused directly by press, even when zero frames were requested.
        for(auto cue:s.launcher->take_cues()) s.heard.push_back(cue);
        if(s.launcher->done()) {
            glBindVertexArray(vao);glUseProgram(program);
            glUniform2f(glGetUniformLocation(program,"size"),s.width,s.height);
            glUniform1f(glGetUniformLocation(program,"seconds"),boot_seconds+
                       (s.services.preferences().reduce_motion ? 1000.0f : 0.0f));
            glViewport(0,0,s.width,s.height);glDrawArrays(GL_TRIANGLES,0,3);
            glReadPixels(0,0,s.width,s.height,GL_RGBA,GL_UNSIGNED_BYTE,s.pixels.data());
            glUseProgram(0);glBindVertexArray(0);
        } else s.render();
        const GLenum error=glGetError();
        if(error!=GL_NO_ERROR) {std::fprintf(stderr,"interactive GL error 0x%x\n",error);return 1;}
        std::vector<unsigned char> png;
        const auto write=[](void *context,void *data,int count) {
            auto &v=*static_cast<std::vector<unsigned char>*>(context);
            const auto *p=static_cast<unsigned char*>(data);v.insert(v.end(),p,p+count);
        };
        if(!stbi_write_png_to_func(write,&png,s.width,s.height,4,s.pixels.data(),s.width*4)) return 1;
        std::fprintf(stdout,"FRAME %zu %d %d %zu",png.size(),s.launcher->menu_volume(),
                     s.launcher->done() ? 1 : 0,s.heard.size());
        for(auto cue:s.heard) std::fprintf(stdout," %u",static_cast<unsigned>(cue));
        std::fputc('\n',stdout);std::fwrite(png.data(),1,png.size(),stdout);std::fflush(stdout);
        s.heard.clear();
    }
    glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <ui assets dir> <output dir> [--tour|--interactive|--checks] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    int argument = 3;
    const bool make_tour = argc > argument && std::strcmp(argv[argument], "--tour") == 0;
    const bool make_interactive = argc > argument && std::strcmp(argv[argument], "--interactive") == 0;
    const bool checks_only = argc > argument && std::strcmp(argv[argument], "--checks") == 0;
    if (make_tour || make_interactive || checks_only)
        ++argument;
    const int width = argc > argument + 1 ? std::atoi(argv[argument]) : 1920;
    const int height = argc > argument + 1 ? std::atoi(argv[argument + 1]) : 1080;

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    pe::gfx::set_glsl_prefix("#version 450 core\n");

    if (!strings_check())
        return 1;
    // PE_LANG=<tag>: the launcher in that language, as on a console set to it.
    if (const char *language = std::getenv("PE_LANG"); language != nullptr && language[0] != 0)
    {
        std::string used;
        for (const std::string &candidate : pe::catalog_candidates(language))
        {
            std::string catalog;
            if (pe::read_file(assets + "/lang/" + candidate + ".po", &catalog) &&
                pe::catalog().load(catalog) > 0)
            {
                used = candidate;
                break;
            }
        }
        std::fprintf(stderr, "language %s: catalog %s, %zu texts\n", language,
                     used.empty() ? "none (English)" : used.c_str(), pe::catalog().size());
        pe::ui::set_fit_report(note_fit);
    }

    pe::gfx::Font font;
    std::string font_data;
    if (!pe::read_file(assets + "/fonts/montserrat-medium.pefont", &font_data) ||
        !font.load(font_data))
    {
        std::fprintf(stderr, "cannot load the font: %s\n", font.error().c_str());
        return 1;
    }
    // The console's fonts for the scripts the baked one lacks (PE_SYSTEM_FONTS: copies of them).
    {
        const char *language = std::getenv("PE_LANG");
        const std::string tag = language != nullptr && language[0] != 0 ? language : "en-US";
        for (const std::string &folder : pe::gfx::system_font_folders())
        {
            std::vector<std::string> files = pe::gfx::system_font_files(folder, tag);
            if (files.empty())
                continue;
            std::fprintf(stderr, "system fonts: %zu files in %s\n", files.size(), folder.c_str());
            font.use_system_fonts(std::move(files), tag);
            break;
        }
        if (!pe::catalog().every([&font](std::string_view text) { return font.can_draw(text); }))
        {
            std::fprintf(stderr, "error: the catalog has characters no font here has; on the console the "
                                 "launcher would stay English (PE_SYSTEM_FONTS names a folder with the "
                                 "console's fonts)\n");
            return 1;
        }
    }
    pe::gfx::GlBatch batch;
    if (!batch.init())
        return 1;

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;

    pe::host::FakeServices services(output);
    // PE_LOOK=large,contrast,calm: the accessibility settings, in any combination.
    if (const char *wanted = std::getenv("PE_LOOK"); wanted != nullptr)
    {
        const std::string_view names{wanted};
        pe::ui::Preferences preferences = services.preferences();
        preferences.large_text = names.find("large") != std::string_view::npos;
        preferences.high_contrast = names.find("contrast") != std::string_view::npos;
        preferences.reduce_motion = names.find("calm") != std::string_view::npos;
        services.set_preferences(preferences);
    }
    pe::ui::Textures textures(batch, services);
    if (!textures.load_art(assets))
        std::fprintf(stderr, "warning: launcher art is incomplete\n");
    textures.set_output_scale(static_cast<float>(width) / 1920.0f);

    // PE_SPECIMEN=<file>: instead of the screens, one picture of the file's lines of text (a line
    // starting with "W " is wrapped to a column), to look at a script or a translation closely.
    if (const char *specimen = std::getenv("PE_SPECIMEN"); specimen != nullptr && specimen[0] != 0)
    {
        std::string lines;
        if (!pe::read_file(specimen, &lines))
        {
            std::fprintf(stderr, "cannot read %s\n", specimen);
            return 1;
        }
        const std::uint32_t texture = batch.create_font_texture(font);
        pe::gfx::DrawList list;
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, pe::ui::theme::kPanel);
        float x = 40.0f;
        float y = 60.0f;
        const float size = 30.0f;
        const float pitch = 44.0f;
        const float column = 900.0f;
        const auto advance = [&]
        {
            y += pitch;
            if (y > 1060.0f)
            {
                y = 60.0f;
                x += column + 40.0f;
            }
        };
        for (std::size_t start = 0; start < lines.size();)
        {
            std::size_t end = lines.find('\n', start);
            if (end == std::string::npos)
                end = lines.size();
            std::string_view line{lines.data() + start, end - start};
            start = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.substr(0, 2) == "W ")
            {
                for (const std::string &part : font.wrap(line.substr(2), size, column))
                {
                    list.rounded_rect({x, y - size, column, pitch - 6.0f}, 4.0f, pe::ui::theme::kRow);
                    list.text(font, texture, part, x, y, size, pe::ui::theme::kText);
                    advance();
                }
                continue;
            }
            if (line.substr(0, 2) == "F ")
            {
                // Cut to a narrow place, as a long title is.
                list.rounded_rect({x, y - size, 420.0f, pitch - 6.0f}, 4.0f, pe::ui::theme::kRow);
                list.text(font, texture, font.fit(line.substr(2), size, 420.0f), x, y, size, pe::ui::theme::kText);
                advance();
                continue;
            }
            list.text(font, texture, line, x, y, size, pe::ui::theme::kText);
            list.text(font, texture, line, x + column, y, 20.0f, pe::ui::theme::kLime, pe::gfx::Align::right);
            advance();
        }
        batch.sync_font_texture(texture, font);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        batch.draw(list, pe::gfx::fit_viewport(width, height), width, height);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        const std::string path = output + "/specimen.png";
        const bool written = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
        std::fprintf(stderr, "specimen: %zu instances, GL error 0x%x, system fonts read: %s\n", list.instances().size(),
                     glGetError(), font.system_fonts_read().c_str());
        return written ? 0 : 1;
    }

    Stage stage{services, textures, {&font, batch.create_font_texture(font)}, batch, width, height,
                output, nullptr, {}, {}, make_tour, true, {}, nullptr, {}, 0};
    stage.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_flip_vertically_on_write(1);
    if (make_interactive) return interactive(stage,assets);
    if (checks_only) {
        integration_checks(stage, assets);
        std::fprintf(stderr, "launcher integration checks: %s\n", stage.ok ? "passed" : "failed");
        return stage.ok ? 0 : 1;
    }
    const char *which_tour = std::getenv("PE_TOUR");
    if (make_tour && which_tour && std::strcmp(which_tour, "update") == 0)
        update_tour(stage);
    else if (make_tour)
        tour(stage);
    else
        pictures(stage,assets);
    if (!make_tour) {
        switch_opening_pictures(stage);
        switch_loading_picture(stage,assets);
    }
    std::fprintf(stderr, "cues heard: %zu\n", stage.heard.size());
    std::fprintf(stderr, "system fonts read: %s\n", font.system_fonts_read().c_str());
    for (const auto &[text, fit] : fits)
        std::fprintf(stderr, "%s %.2f: %s\n", fit.second ? "cut" : "shrunk", fit.first, text.c_str());
    return stage.ok ? 0 : 1;
}
