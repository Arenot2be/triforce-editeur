#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec4 vCol;
layout(location = 1) in vec3 vW;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    float lit = 0.5 + 0.5 * abs(dot(n, normalize(vec3(0.35, 1.0, 0.25))));
    outColor = vec4(vCol.rgb * lit, 1.0);
}
