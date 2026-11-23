#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Modèles : couleur du matériau, avec ses couches quand il en a :
//  - texture de détail (« CmnTex_Map_DetailMap_… », bois…) répétée finement, qui module la couleur (0,5 = neutre) ;
//  - seconde couleur (la mousse…) posée sur les surfaces tournées vers le haut (normale du modèle), avec un bord
//    irrégulier tiré de la luminosité de la mousse ;
//  - couche posée par-dessus (matériaux « _sl ») : opacité dans l'alpha des sommets.
//  - éclairage précalculé du jeu (Bake/Scene : carte d'éclairage de la case de grille de l'objet), qui remplace alors
//    l'éclairage simple de l'éditeur.
// pc.scale.w : 1 couleur, 2 seconde couleur, 4 détail, 8 couche par-dessus, 16 éclairage précalculé (pc.color :
// décalage u, v et échelle u, v de la coordonnée « bake » dans la carte) ; d'après le nuanceur « model » du jeu :
// 32 carte de normales, 64 rugosité (canal en bits 10-11), 128 et 256 normales et RAH de la seconde couche,
// 512 deux couches fondues comme le graphe 3552031828 (falaises), 4096 rugosité = RAH, 8192 découpe alpha.

layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(set = 1, binding = 0) uniform sampler2D uTex2;
layout(set = 2, binding = 0) uniform sampler2D uDetail;
layout(set = 3, binding = 0) uniform sampler2D uLight;
layout(set = 4, binding = 0) uniform sampler2D uNrm;    // (carte de normales du jeu : x, y ; parfois rugosité en z)
layout(set = 5, binding = 0) uniform sampler2D uRgh;    // (rugosité, ou RAH : rugosité, occlusion, hauteur)
layout(set = 6, binding = 0) uniform sampler2D uNrm2;   // (seconde couche : normales)
layout(set = 7, binding = 0) uniform sampler2D uRah2;   // (seconde couche : RAH)
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vW;
layout(location = 2) in vec4 vCol;
layout(location = 3) in vec3 vN;
layout(location = 4) in vec2 vBake;
layout(location = 5) in vec2 vUv2;
layout(location = 6) flat in vec3 vEye;
layout(location = 7) in vec2 vUv3;
layout(location = 0) out vec4 outColor;
// (carte de normales du jeu : x, y sur 8 bits, décodés comme lui (× 2,007874 − 1,007874), z reconstruit)
vec3 decodeNrm(vec4 t) {
    vec2 xy = t.xy * 2.007874 - 1.007874;
    return vec3(xy, sqrt(clamp(1.0 - dot(xy, xy), 0.0, 1.0)));
}
// (repère tangent tiré des dérivées de la position et des coordonnées : le jeu prend la tangente des sommets)
vec3 perturb(vec3 n, vec3 tn, vec2 uv) {
    vec3 dp1 = dFdx(vW), dp2 = dFdy(vW);
    vec2 du1 = dFdx(uv), du2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 T = dp2perp * du1.x + dp1perp * du2.x;
    vec3 B = dp2perp * du1.y + dp1perp * du2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    if (!(invmax < 1e9)) return n;
    return normalize(mat3(T * invmax, B * invmax, n) * tn);
}
void main() {
    int flags = int(pc.scale.w + 0.5);
    vec3 nf = normalize(cross(dFdx(vW), dFdy(vW)));
    bool hasN = dot(vN, vN) > 1e-6;
    vec3 n = hasN ? normalize(vN) : nf;
    bool lm = (flags & 16) != 0 && vBake.x >= 0.0;
    vec4 c = (flags & 1) != 0 ? vec4(1.0) : pc.color;
    float layerK = 0.0, rough = 0.8, ao = 1.0;
    float alpha = (flags & 1) != 0 ? 1.0 : pc.color.a;
    if ((flags & 1) != 0) {
        c = texture(uTex, vUv);
        if ((flags & 8) == 0 && c.a < ((flags & 8192) != 0 ? 0.5 : 0.35)) discard;
        if ((flags & 4) != 0 && (flags & 512) == 0) {
            vec3 d = texture(uDetail, vUv * 8.0).rgb;
            c.rgb *= mix(vec3(1.0), clamp(d * 2.0, 0.0, 2.0), 0.55);
        }
        if ((flags & 512) != 0) {
            // (recettes de mélange lues dans les programmes du jeu ; m[0..2] : couleurs, m[3] : scalaires 0-3,
            //  m[4] : scalaires 5, 6, 7 ; vCol.g : couleur des sommets, 0,5 = neutre)
            int recipe = int(pc.m[0].a + 0.5);
            vec4 m = texture(uTex2, vUv2);
            float hA = (flags & 64) != 0 ? texture(uRgh, vUv).b : 0.5;
            float hB = (flags & 256) != 0 ? texture(uRah2, vUv2).b : 0.5;
            float v = clamp(vCol.g, 0.0, 1.0);
            float s0 = pc.m[3].x, s1 = pc.m[3].y, s2 = pc.m[3].z, s3 = pc.m[3].w, s5 = pc.m[4].x, s6 = pc.m[4].y, s7 = pc.m[4].z;
            float w = clamp(s1, 0.01, 1.0);
            float up = hasN ? n.y : abs(nf.y);
            vec3 A = c.rgb * pc.m[0].rgb;
            float k;
            if (recipe == 1) {   // (falaises : graphe 3552031828)
                float t0 = (clamp(up + s7, 0.0, 1.0) - 0.5) + (hB * 4.78 - 2.39 + s0);
                k = clamp((hA - t0) / w, 0.0, 1.0);
                if ((flags & 4) != 0 && abs(s5 - s6) > 1e-4) {
                    float mk = clamp((texture(uDetail, vUv3).x - s6) / (s5 - s6), 0.0, 1.0);
                    A = mix(A, m.rgb * pc.m[2].rgb, mk);
                }
            } else if (recipe == 2) {   // (bois des maisons : graphe 4069519554, détail multiplié)
                float t0 = (clamp(up, 0.0, 1.0) - 0.5) + (hB - 0.5) * s7 + s0;
                k = clamp((0.5 - t0) / w, 0.0, 1.0);
                if ((flags & 4) != 0) A = clamp(A * (1.0 + clamp(s6, 0.0, 1.0) * (texture(uDetail, vUv3).rgb - 1.0)), 0.0, 1.0);
            } else {   // (intérieurs : expression 4260383445, sans la pente)
                float t0 = (hB - 0.5) * s2 + (v - 0.5) * s3 + s0;
                k = clamp((hA - t0) / w, 0.0, 1.0);
                v = 0.5;
            }
            // (couleur des sommets en « overlay » : 0,5 la laisse telle quelle)
            float f = v < 0.5 ? 2.0 * v * (1.0 - k) : 1.0 - 2.0 * k * (1.0 - v);
            layerK = clamp(f, 0.0, 1.0);
            c.rgb = mix(A, m.rgb * pc.m[1].rgb, layerK);
        } else if ((flags & 2) != 0) {
            vec4 m = texture(uTex2, vUv * 4.0);
            // (moitié haute : la mousse vient sur les dessus ; sans normale du modèle, le côté est inconnu : |y|)
            float up = hasN ? n.y : abs(nf.y);
            float w = smoothstep(0.5, 0.85, up);
            // (bord de la mousse : plus franc là où sa texture est claire, comme un fondu par hauteur)
            float lum = dot(m.rgb, vec3(0.3, 0.55, 0.15));
            float k = smoothstep(0.15, 0.85, w + (lum - 0.35) * 0.6 * w * (1.0 - w) * 4.0);
            c.rgb = mix(c.rgb, m.rgb, k);
        }
        alpha = (flags & 8) != 0 ? c.a * clamp(vCol.a, 0.0, 1.0) : 1.0;
        if ((flags & 8) != 0 && alpha < 0.02) discard;
    }
    // (relief et rugosité du jeu ; la seconde couche a les siens, mêlés comme la couleur)
    if ((flags & 1) != 0 && (flags & 32) != 0) {
        vec3 tn = decodeNrm(texture(uNrm, vUv));
        if ((flags & 128) != 0) tn = normalize(mix(tn, decodeNrm(texture(uNrm2, vUv2)), layerK));
        n = perturb(n, tn, vUv);
        hasN = true;
    }
    if ((flags & 1) != 0 && (flags & 64) != 0) {
        vec4 r = texture(uRgh, vUv);
        int ch = (flags >> 10) & 3;
        rough = ch == 0 ? r.r : ch == 1 ? r.g : ch == 2 ? r.b : r.a;
        if ((flags & 4096) != 0) {
            ao = r.g;
            if ((flags & 256) != 0) { vec4 r2 = texture(uRah2, vUv2); rough = mix(rough, r2.r, layerK); ao = mix(ao, r2.g, layerK); }
        }
    }
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float lit = hasN ? 0.4 + 0.45 * max(dot(n, L), 0.0) + 0.15 * (0.5 + 0.5 * n.y) + 0.06 * abs(n.x)
                     : 0.42 + 0.5 * abs(dot(n, L)) + 0.08 * abs(n.x);
    // (éclairage précalculé des sommets quand le modèle en a : il remplace en partie l'éclairage simple de l'éditeur)
    float baked = clamp(vCol.r, 0.0, 2.5);
    lit *= mix(1.0, ao, 0.6);
    vec3 col = c.rgb * mix(lit, lit * baked * 1.15, 0.7);
    // (reflet du soleil selon la rugosité, discret : l'éditeur n'a pas l'éclairage du jeu)
    vec3 V = normalize(vEye - vW), H = normalize(L + V);
    float sm = (1.0 - rough) * (1.0 - rough);
    float spec = (flags & 64) != 0 ? sm * pow(max(dot(n, H), 0.0), mix(6.0, 96.0, sm)) * 0.35 : 0.0;
    col += vec3(1.0, 0.96, 0.88) * spec;
    if (lm) {
        // (carte du jeu, deux canaux : r = lumière du ciel reçue (occlusion ambiante), g = soleil reçu (ombres portées))
        vec2 B = texture(uLight, vBake * pc.color.zw + pc.color.xy).rg;
        float ndl = hasN ? max(dot(n, L), 0.0) : abs(dot(n, L));
        vec3 sky = vec3(0.62, 0.67, 0.74), sun = vec3(1.0, 0.94, 0.82) * 0.55;
        col = c.rgb * (0.03 + sky * B.r * 0.8 + sun * B.g * mix(0.45, 1.0, ndl));
    }
    if (pc.pos.w > 0.5) col = mix(col, vec3(1.0, 0.82, 0.3), pc.pos.w > 1.5 ? 0.18 : 0.38);
    outColor = vec4(col, alpha);
}
