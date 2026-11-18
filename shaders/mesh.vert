#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vW;
mat3 rx(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 ry(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rz(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }
void main() {
    vec3 w = rz(pc.rot.z) * ry(pc.rot.y) * rx(pc.rot.x) * (aPos * pc.scale.xyz) + pc.pos.xyz;
    vW = w;
    vUv = aUv;
    gl_Position = pc.vp * vec4(w, 1.0);
}
