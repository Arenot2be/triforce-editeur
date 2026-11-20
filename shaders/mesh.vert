#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;   // (rgb : éclairage précalculé des sommets, 1 neutre ; a : opacité des couches)
layout(location = 3) in vec3 aNrm;   // (normale du modèle ; sans : 0)
layout(location = 4) in vec2 aBake;  // (coordonnée dans la carte d'éclairage précalculé ; sans : -1)
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vW;
layout(location = 2) out vec4 vCol;
layout(location = 3) out vec3 vN;
layout(location = 4) out vec2 vBake;
mat3 rx(float a) { float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 ry(float a) { float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rz(float a) { float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }
void main() {
    mat3 R = rz(pc.rot.z) * ry(pc.rot.y) * rx(pc.rot.x);
    vec3 w = R * (aPos * pc.scale.xyz) + pc.pos.xyz;
    // (normale : tournée comme l'objet, et corrigée de son échelle ; nulle si le modèle n'en a pas)
    vN = dot(aNrm, aNrm) > 0.0 ? R * (aNrm / max(abs(pc.scale.xyz), vec3(1e-4))) : vec3(0.0);
    vW = w;
    vUv = aUv;
    vCol = aCol;
    vBake = aBake;
    gl_Position = pc.vp * vec4(w, 1.0);
}
