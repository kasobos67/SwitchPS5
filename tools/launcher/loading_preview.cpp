// ProsperoEden - Draw the game-loading screen's shader on a PC (host tool).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: loading_preview <headless dir> <output dir> [width height]
//        loading_preview <headless dir> - --video <seconds> [width height]  (raw RGBA on stdout)
// Compiles loading_wordmark.glsl + loading_scene.glsl exactly as the OpenGL backend does.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::string read_text(const std::string &path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

GLuint compile(GLenum stage, const std::string &source)
{
    const GLuint shader = glCreateShader(stage);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader failed:\n%s\n", log);
        return 0;
    }
    return shader;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <headless dir> <output dir> [--video seconds] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string source_dir = argv[1];
    const std::string output = argv[2];
    int argument = 3;
    float video_seconds = 0.0f;
    if (argc > argument + 1 && std::strcmp(argv[argument], "--video") == 0)
    {
        video_seconds = std::strtof(argv[argument + 1], nullptr);
        argument += 2;
    }
    const int width = argc > argument + 1 ? std::atoi(argv[argument]) : 1920;
    const int height = argc > argument + 1 ? std::atoi(argv[argument + 1]) : 1080;

    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
        return 1;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return 1;

    const std::string fragment =
        "#version 330 core\n" + read_text(source_dir + "/loading_wordmark.glsl") +
        read_text(source_dir + "/loading_scene.glsl") +
        "uniform vec2 size; uniform float seconds; out vec4 color;\n"
        "void main(){ color = vec4(loading_scene(gl_FragCoord.xy, size, seconds), 1.0); }\n";
    const GLuint vertex_shader = compile(
        GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                          "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}");
    const GLuint fragment_shader = compile(GL_FRAGMENT_SHADER, fragment);
    if (vertex_shader == 0 || fragment_shader == 0)
        return 1;
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
        return 1;
    std::fprintf(stderr, "fragment shader: %zu bytes\n", fragment.size());

    GLuint framebuffer = 0;
    GLuint color = 0;
    GLuint vao = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, width, height);
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "size"), static_cast<float>(width),
                static_cast<float>(height));

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    const auto render = [&](float seconds)
    {
        glUniform1f(glGetUniformLocation(program, "seconds"), seconds);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };
    if (video_seconds > 0.0f)
    {
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        for (int frame = 0; frame < static_cast<int>(video_seconds * 60.0f); ++frame)
        {
            render(static_cast<float>(frame) / 60.0f);
            for (int y = height - 1; y >= 0; --y)
                std::fwrite(pixels.data() + static_cast<std::size_t>(y) * stride, 1, stride, stdout);
        }
        return 0;
    }
    stbi_flip_vertically_on_write(1);
    bool ok = true;
    // The last two are the screen with reduced motion (1000 added): the same scenery, the ring moved.
    for (float seconds : {0.35f, 2.0f, 5.5f, 1002.0f, 1005.5f})
    {
        render(seconds);
        char name[64];
        if (seconds >= 1000.0f)
            std::snprintf(name, sizeof(name), "/loading-calm-%04.1fs.png", seconds - 1000.0f);
        else
            std::snprintf(name, sizeof(name), "/loading-%04.1fs.png", seconds);
        ok = stbi_write_png((output + name).c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
        std::fprintf(stderr, "%s GL error 0x%x\n", name, glGetError());
    }
    return ok ? 0 : 1;
}
