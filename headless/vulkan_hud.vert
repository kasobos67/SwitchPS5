// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
void main() {
    vec2 p = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
