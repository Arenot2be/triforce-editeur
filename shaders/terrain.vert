#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 0) out vec4 vCol;
layout(location = 1) out vec3 vW;
void main() {
    vW = aPos;
    vCol = aCol;
    gl_Position = pc.vp * vec4(aPos, 1.0);
}
