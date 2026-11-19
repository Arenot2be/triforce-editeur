#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec4 vCol;
layout(location = 1) in vec3 vW;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    float lit = 0.45 + 0.55 * abs(dot(n, normalize(vec3(0.35, 1.0, 0.25))));
    float steep = smoothstep(0.55, 0.8, 1.0 - abs(n.y));   // (pentes fortes : roche)
    vec3 col = mix(vCol.rgb, vec3(0.42, 0.41, 0.38), steep);
    outColor = vec4(col * lit, 1.0);
}
