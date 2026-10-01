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
#include "pe/ui/launcher.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
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

    void restart(bool first_start = false)
    {
        launcher = std::make_unique<pe::ui::Launcher>(services, textures, fonts, first_start);
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
        std::fprintf(stderr, "%s: %zu instances, %zu draw calls, GL error 0x%x%s\n", name,
                     list.instances().size(), batch.last_draw_calls(), glGetError(),
                     written ? "" : " (not written)");
        ok = ok && written;
    }
};

void pictures(Stage &s)
{
    // Home, as it settles after the app opens.
    s.restart(true);
    s.wait(0.42f);
    s.shoot("00-arriving");
    s.wait(3.0f);
    s.shoot("01-home");
    s.press({Key::right});
    s.wait(0.6f);
    s.shoot("02-home-details-focus");
    s.press({Key::down, Key::right});
    s.wait(0.6f);
    s.shoot("03-home-recent-focus");
    s.press({Key::up, Key::up, Key::right});
    s.wait(0.6f);
    s.shoot("04-home-settings-focus");

    // Library.
    s.restart();
    s.wait(1.0f);
    s.press({Key::up, Key::cross});
    s.wait(0.14f);
    s.shoot("05-opening-library");
    s.wait(1.2f);
    s.shoot("06-library");
    s.press({Key::down, Key::down, Key::down, Key::down, Key::down, Key::down, Key::down,
             Key::down, Key::down});
    s.wait(0.8f);
    s.shoot("07-library-scrolled");
    s.press({Key::right});
    s.wait(0.6f);
    s.shoot("08-library-handheld");
    s.press({Key::triangle});
    s.wait(0.8f);
    s.shoot("09-game-settings");
    s.press({Key::down, Key::right});
    s.wait(0.6f);
    s.shoot("10-game-settings-changed");
    s.press({Key::circle});
    s.wait(0.3f);
    s.press({Key::cross});
    s.wait(0.30f);
    s.shoot("11-launching");
    s.wait(0.32f);
    s.shoot("12-launching-late");

    // Settings and its dialogs.
    s.restart();
    s.wait(1.0f);
    s.press({Key::up, Key::right, Key::cross});
    s.wait(1.0f);
    s.shoot("13-settings");
    s.press({Key::cross});
    s.wait(0.8f);
    s.shoot("14-video");
    s.press({Key::down, Key::right});
    s.wait(0.6f);
    s.shoot("15-video-saved");
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("16-audio");
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("17-controls");
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("18-diagnostics");
    s.press({Key::circle, Key::down, Key::cross});
    s.wait(1.0f);
    s.shoot("19-game-files");
    s.press({Key::down, Key::down, Key::triangle});
    s.wait(0.6f);
    s.shoot("20-game-files-saved");
    s.press({Key::circle});
    s.wait(0.5f);
    s.press({Key::down, Key::cross});
    s.wait(1.0f);
    s.shoot("21-language");
    s.press({Key::down, Key::down, Key::down, Key::down, Key::down, Key::down, Key::down,
             Key::down, Key::down, Key::cross});
    s.wait(0.8f);
    s.shoot("22-language-chosen");

    // About.
    s.restart();
    s.wait(1.0f);
    s.press({Key::up, Key::right, Key::right, Key::cross});
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

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <ui assets dir> <output dir> [--tour] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    int argument = 3;
    const bool make_tour = argc > argument && std::strcmp(argv[argument], "--tour") == 0;
    if (make_tour)
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

    pe::gfx::Font font;
    std::string font_data;
    if (!pe::read_file(assets + "/fonts/montserrat-medium.pefont", &font_data) ||
        !font.load(font_data))
    {
        std::fprintf(stderr, "cannot load the font: %s\n", font.error().c_str());
        return 1;
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
    pe::ui::Textures textures(batch, services);
    if (!textures.load_art(assets))
        std::fprintf(stderr, "warning: launcher art is incomplete\n");
    textures.set_output_scale(static_cast<float>(width) / 1920.0f);

    Stage stage{services, textures, {&font, batch.create_font_texture(font)}, batch, width, height,
                output, nullptr, {}, {}, make_tour, true, {}};
    stage.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_flip_vertically_on_write(1);
    if (make_tour)
        tour(stage);
    else
        pictures(stage);
    std::fprintf(stderr, "cues heard: %zu\n", stage.heard.size());
    return stage.ok ? 0 : 1;
}
