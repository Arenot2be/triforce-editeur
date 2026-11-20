#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Particules des effets : quadrilatères déjà placés en coordonnées du monde (calculés chaque image).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vCol;
void main() {
    vUv = aUv;
    vCol = aCol;
    gl_Position = pc.vp * vec4(aPos, 1.0);
}
