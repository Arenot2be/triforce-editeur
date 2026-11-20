#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Particules des effets. pc.scale.x : forme —
//   0 tache douce ; 1 texture de couleur du jeu (_Fca) ; 2 texture d'intensité du jeu (_Fi) dans une tache douce ;
//   3 anneau (ride) ; 4 rayon (fondu aux deux bouts et sur les bords) ; 5 anneau dans la texture d'intensité ;
//   6 texture d'intensité seule (sa forme est dans l'image, comme une ride) ; 7 brin d'herbe (pointe en haut, pied sombre)
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 outColor;
void main() {
    int mode = int(pc.scale.x + 0.5);
    vec2 d = vUv * 2.0 - 1.0;
    float r = length(d);
    float a = 1.0;
    vec3 c = vec3(1.0);
    if (mode == 1) {
        vec4 t = texture(uTex, vUv);
        c = t.rgb;
        a = t.a;
    } else if (mode == 2) {
        vec4 t = texture(uTex, vUv);
        // (texture d'une seule composante : rangée en rouge, ou en transparence, selon le fichier)
        a = min(t.r, t.a) * smoothstep(1.0, 0.35, r);
    } else if (mode == 6) {
        vec4 t = texture(uTex, vUv);
        a = min(t.r, t.a);
    } else if (mode == 7) {
        float w = vUv.y * 0.95 + 0.05;
        a = 1.0 - smoothstep(w - 0.2, w, abs(d.x));
        c = vec3(mix(1.0, 0.5, vUv.y));
    } else if (mode == 3 || mode == 5) {
        a = smoothstep(0.62, 0.86, r) * smoothstep(1.0, 0.9, r);
        if (mode == 5) a *= 0.5 + texture(uTex, vUv * 0.5).r;
    } else if (mode == 4) {
        a = smoothstep(0.0, 0.25, vUv.y) * smoothstep(1.0, 0.55, vUv.y) * (1.0 - abs(d.x));
        a *= a;
    } else {
        a = smoothstep(1.0, 0.0, r);
        a *= a;
    }
    if (a * vCol.a < 0.003) discard;
    outColor = vec4(c * vCol.rgb, a * vCol.a);
}
