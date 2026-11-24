#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Carte d'ombre : seulement la découpe alpha des matériaux qui en ont (feuillages, grilles…).
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
void main() {
    int flags = int(pc.scale.w + 0.5);
    if ((flags & 8193) == 8193 && texture(uTex, vUv).a < 0.5) discard;
}
