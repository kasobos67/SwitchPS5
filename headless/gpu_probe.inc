// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled only into the explicitly selected graphics-only candidate.
#include "gpu_native_probe.inc"
namespace Eden {
void GraphicsWindow::RunGpuProbe() {
    Check(eglMakeCurrent(display, surface, surface, root), "probe current");
    SCOPE_EXIT { Cleanup(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                      EGL_NO_CONTEXT), "probe release"); };
    const auto ns = [](clockid_t id) -> long long {
        timespec t{};
        Check(clock_gettime(id, &t) == 0, "probe clock");
        return static_cast<long long>(t.tv_sec) * 1000000000LL + t.tv_nsec;
    };
    glFinish();
    for (unsigned memory_type : {12u,0u}) ProbeNativeQueue(memory_type);
    const auto started = ns(CLOCK_MONOTONIC);
    GLuint program = glCreateProgram(), vao{}, texture{}, framebuffer{}, query{};
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &texture);
    glGenFramebuffers(1, &framebuffer);
    glGenQueries(1, &query);
    SCOPE_EXIT {
        glFinish(); glUseProgram(0); glBindVertexArray(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteQueries(1, &query); glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture); glDeleteVertexArrays(1, &vao); glDeleteProgram(program);
    };
    const char* sources[]{
        "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}",
        "#version 330 core\nout vec4 color;void main(){color=vec4(0.25,0.5,0.75,1.0);}"};
    for (unsigned i=0; i<2; ++i) {
        GLuint shader=glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        glShaderSource(shader,1,&sources[i],nullptr); glCompileShader(shader);
        GLint ok{}; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        glAttachShader(program,shader); glDeleteShader(shader);
        Check(ok,"probe shader");
    }
    glLinkProgram(program);
    GLint linked{}; glGetProgramiv(program,GL_LINK_STATUS,&linked); Check(linked,"probe link");
    glUseProgram(program); glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindTexture(GL_TEXTURE_2D,texture); glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    // API-only control. With no queued draw, this is NOT a native empty GPU submission.
    for (unsigned rep=0;rep<4;++rep) {
        glFinish(); const auto t=ns(CLOCK_MONOTONIC), c=ns(CLOCK_THREAD_CPUTIME_ID);
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); Check(fence,"probe empty fence");
        glFlush(); const auto status=glClientWaitSync(fence,0,GL_TIMEOUT_IGNORED);
        const auto end=ns(CLOCK_MONOTONIC), cpu=ns(CLOCK_THREAD_CPUTIME_ID);
        glDeleteSync(fence);
        Check(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED,"probe fence completion");
        std::printf("EDEN_GPU_PROBE_CONTROL rep=%u wall_ns=%lld cpu_ns=%lld\n",rep,end-t,cpu-c);
    }
    const char* modes[]{"group","flush_each","finish_spin","query_wait","query_poll","present"};
    for (const unsigned width : {16u,1920u}) {
        const unsigned height=width==16 ? 16 : 1080;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"probe framebuffer");
        glViewport(0,0,width,height);
        for (unsigned mode=0;mode<6;++mode) for (const unsigned draws : {1u,16u,128u}) {
            for (unsigned rep=0;rep<4;++rep) {
                glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT); glFinish();
                const auto t=ns(CLOCK_MONOTONIC), c=ns(CLOCK_THREAD_CPUTIME_ID);
                unsigned ready=0;
                for (unsigned n=0;n<draws;++n) {
                    Check(ns(CLOCK_MONOTONIC)-started<90000000000LL,"probe 90s deadline");
                    if (mode==3 || mode==4) glBeginQuery(GL_SAMPLES_PASSED,query);
                    glDrawArrays(GL_TRIANGLES,0,3);
                    if (mode==1) glFlush();
                    if (mode==2) {
                        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
                        Check(fence,"probe busy fence"); glFlush();
                        const auto deadline=ns(CLOCK_MONOTONIC)+2000000000LL;
                        for (;;) {
                            const auto status=glClientWaitSync(fence,0,0);
                            if (status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED) break;
                            if (status!=GL_TIMEOUT_EXPIRED || ns(CLOCK_MONOTONIC)>=deadline) std::_Exit(1);
                            __builtin_ia32_pause();
                        }
                        glDeleteSync(fence);
                    }
                    if (mode==3 || mode==4) {
                        glEndQuery(GL_SAMPLES_PASSED);
                        GLuint result{};
                        glGetQueryObjectuiv(query,mode==3 ? GL_QUERY_RESULT : GL_QUERY_RESULT_AVAILABLE,&result);
                        if (mode==3) Check(result==width*height,"probe query samples");
                        else ready+=result!=0;
                    }
                }
                glFlush();
                const auto submitted=ns(CLOCK_MONOTONIC), submitted_cpu=ns(CLOCK_THREAD_CPUTIME_ID);
                if (mode==5) Check(eglSwapBuffers(display,surface),"probe presentation");
                glFinish();
                const auto end=ns(CLOCK_MONOTONIC), cpu=ns(CLOCK_THREAD_CPUTIME_ID);
                Check(glGetError()==GL_NO_ERROR,"probe GL error");
                // Readback follows the timed interval and validates actual GPU output.
                unsigned char pixel[4]{};
                glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                Check(pixel[0]==64 && pixel[1]==128 && pixel[2]==191 && pixel[3]==255,"probe pixel");
                std::printf("EDEN_GPU_PROBE mode=%s width=%u height=%u draws=%u rep=%u "
                    "issue_ns=%lld issue_cpu_ns=%lld finish_ns=%lld finish_cpu_ns=%lld "
                    "wall_ns=%lld cpu_ns=%lld available=%u pixel=64,128,191,255\n",
                    modes[mode],width,height,draws,rep,submitted-t,submitted_cpu-c,
                    end-submitted,cpu-submitted_cpu,end-t,cpu-c,ready);
            }
            std::fflush(stdout);
        }
    }
    std::puts("EDEN_GPU_PROBE_PASS cases=36 repeats=4 warmup_rep=0");
}
}
