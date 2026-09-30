// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only intermediate images: locate the pass that loses the scene.
#include <glad/glad.h>
#include <vector>
#include <cstdio>
#include <string>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include "performance.h"
extern "C" void ps5_opengl_heap_snapshot(const char*, unsigned);

static void CaptureDevelopmentPass() {
    static bool debug_installed = false;
    if (!debug_installed) {
        debug_installed = true;
        glEnable(GL_DEBUG_OUTPUT);
        const char* threaded = std::getenv("PS5_GLTHREAD");
        if (!threaded || std::strcmp(threaded, "1") != 0)
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback([](GLenum, GLenum type, GLuint id, GLenum, GLsizei length,
                                  const GLchar* message, const void*) {
            static std::mutex error_mutex;
            const std::lock_guard guard{error_mutex};
            static std::vector<std::string> errors;
            if (type != GL_DEBUG_TYPE_ERROR || errors.size() >= 32) return;
            for (const auto& error : errors)
                if (error.compare(0, error.size(), message, length) == 0) return;
            errors.emplace_back(message, length);
            std::printf("EDEN_DEV_GL_ERROR id=%u message=%.*s\n", id, length, message);
            if (errors.back().find("OUT_OF_MEMORY") != std::string::npos)
                ps5_opengl_heap_snapshot("gl_allocation_failure", 0);
        }, nullptr);
    }
    if (!Eden::Performance::capture_passes.load()) return;
    static GLint previous = 0;
    static unsigned sequence = 0;
    GLint current = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current);
    if (current == previous) return;
    const GLuint framebuffer = current;
    previous = current;
    if (!framebuffer || !glIsFramebuffer(framebuffer)) return;
    const GLenum prior_error = glGetError();
    GLint type = 0, texture = 0, level = 0, layer = 0, width = 0, height = 0, format = 0;
    const auto attachment = [&](GLenum name, GLint* value) {
        glGetNamedFramebufferAttachmentParameteriv(framebuffer, GL_COLOR_ATTACHMENT0, name, value);
    };
    attachment(GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_TEXTURE) return;
    attachment(GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
    attachment(GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
    attachment(GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &layer);
    glGetTextureLevelParameteriv(texture, level, GL_TEXTURE_WIDTH, &width);
    glGetTextureLevelParameteriv(texture, level, GL_TEXTURE_HEIGHT, &height);
    glGetTextureLevelParameteriv(texture, level, GL_TEXTURE_INTERNAL_FORMAT, &format);
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) return;
    --Eden::Performance::capture_passes;
    const GLenum names[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_IMAGE_HEIGHT,
                           GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_IMAGES};
    GLint saved[6], buffer = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (unsigned i = 0; i < 6; ++i) {
        glGetIntegerv(names[i], &saved[i]);
        glPixelStorei(names[i], i ? 0 : 1);
    }
    std::vector<unsigned char> rgba(size_t(width) * height * 4);
    glGetTextureSubImage(texture, level, 0, 0, layer, width, height, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, GLsizei(rgba.size()), rgba.data());
    const GLenum error = glGetError();
    for (unsigned i = 0; i < 6; ++i) glPixelStorei(names[i], saved[i]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    char path[128];
    std::snprintf(path, sizeof(path), "/download0/eden-headless-g7/pass-%u.ppm", sequence);
    if (!error) {
        if (auto* file = std::fopen(path, "wb")) {
            std::fprintf(file, "P6\n%d %d\n255\n", width, height);
            std::vector<unsigned char> row(size_t(width) * 3);
            for (int y = height - 1; y >= 0; --y) {
                for (int x = 0; x < width; ++x)
                    for (int c = 0; c < 3; ++c)
                        row[x * 3 + c] = rgba[(size_t(y) * width + x) * 4 + c];
                std::fwrite(row.data(), 1, row.size(), file);
            }
            std::fclose(file);
        }
    }
    std::printf("EDEN_DEV_PASS index=%u fbo=%u texture=%d format=%x size=%dx%d layer=%d error=%x prior_error=%x\n",
                sequence++, framebuffer, texture, format, width, height, layer, error, prior_error);
}
