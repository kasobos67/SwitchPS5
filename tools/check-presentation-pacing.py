from pathlib import Path

source = (Path(__file__).parents[1] / "headless" / "graphics.cpp").read_text()
make_current = source[source.index("void MakeCurrent() override") : source.index("void DoneCurrent() override")]

assert "eglMakeCurrent(display, surface, surface, context)" in make_current
assert make_current.index("eglMakeCurrent") < make_current.index("eglSwapInterval(display, 0)")
assert "window_owner && !swap_interval_set" in make_current
assert 'EDEN_EGL_SWAP_INTERVAL value=0' in make_current
assert '"F%.0f S%.0f W%.0f"' in source
assert 'vec4(0.0196,0.0392,0.0039,1.0)' in source
assert 'vec4(0.7216,0.9490,0.0471,1.0)' in source
assert 'glUniform1i(glGetUniformLocation(hud_program, "startup"), startup)' in source
main = (Path(__file__).parents[1] / "headless" / "main.cpp").read_text()
assert main.index("window.SetSystem(system)") < main.index("system.Initialize()")
assert "Settings::values.use_asynchronous_gpu_emulation = true;" in main
cmake = (Path(__file__).parents[1] / "headless" / "CMakeLists.txt").read_text()
assert 'Common::SPSCQueue<CommandDataContainer, 8>' in cmake
print("presentation pacing check passed")
