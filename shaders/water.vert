#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Eau : comme mesh.vert, avec en plus la position de l'œil (perspective) ou la direction de vue (vue de dessus)
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vW;
layout(location = 2) flat out vec4 vEye;   // (xyz : œil, w = 1 ; ou xyz : direction vers l'œil, w = 0)
mat3 rx(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 ry(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rz(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }
void main() {
    vec3 w = rz(pc.rot.z) * ry(pc.rot.y) * rx(pc.rot.x) * (aPos * pc.scale.xyz) + pc.pos.xyz;
    vW = w;
    vUv = aUv;
    gl_Position = pc.vp * vec4(w, 1.0);
    // (l'œil est le point que la projection envoie sur x = y = w = 0)
    vec4 e = inverse(pc.vp) * vec4(0.0, 0.0, 1.0, 0.0);
    if (abs(e.w) > 1e-7) vEye = vec4(e.xyz / e.w, 1.0);
    else {
        vec3 d = normalize(e.xyz);   // (vue de dessus : l'œil est au-dessus)
        vEye = vec4(d.y < 0.0 ? -d : d, 0.0);
    }
}
