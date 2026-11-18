#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vW;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 c = pc.color;
    if (pc.scale.w > 0.5) {
        c = texture(uTex, vUv);
        if (c.a < 0.35) discard;
    }
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    float lit = 0.42 + 0.5 * abs(dot(n, normalize(vec3(0.35, 1.0, 0.25)))) + 0.08 * abs(n.x);
    vec3 col = c.rgb * lit;
    if (pc.pos.w > 0.5) col = mix(col, vec3(1.0, 0.82, 0.3), pc.pos.w > 1.5 ? 0.18 : 0.38);
    outColor = vec4(col, 1.0);
}
