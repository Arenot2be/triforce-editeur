#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in vec3 aTc;   // (position dans la tuile, 0..1, et numéro de la tuile)
layout(location = 0) out vec4 vCol;
layout(location = 1) out vec3 vW;
layout(location = 2) out vec3 vTc;
void main() {
    vW = aPos;
    vCol = aCol;
    vTc = aTc;
    gl_Position = pc.vp * vec4(aPos, 1.0);
}
