#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Sol : textures des matières (Tex/MaterialAlb, couche donnée par la table des matières de la scène), mélangées
// d'après la carte des matières de chaque tuile (4 numéros et 4 poids par point). Relief (Tex/MaterialCmb, mêmes
// couches) : R et G, une normale ; B, une hauteur, qui sert aussi au mélange (la matière la plus haute l'emporte).
// pc.scale : x réglage des répétitions, y nombre de couches, z 1 si relief, w 1 si les textures sont là ;
// vCol.a : côté de la grille de la tuile (points).
layout(set = 0, binding = 0) uniform sampler2DArray uAlb;
layout(set = 1, binding = 0) uniform sampler2DArray uIds;
layout(set = 2, binding = 0) uniform sampler2DArray uWts;
layout(set = 3, binding = 0) uniform sampler2DArray uMat;   // (128 x 1 : par matière, couche, répétitions u et v par mètre × 64)
layout(set = 4, binding = 0) uniform sampler2DArray uCmb;
layout(location = 0) in vec4 vCol;
layout(location = 1) in vec3 vW;
layout(location = 2) in vec3 vTc;
layout(location = 0) out vec4 outColor;

struct Acc { vec3 col; vec2 nrm; float h; float w; };

void point(ivec3 p, vec2 w2, vec2 gx, vec2 gy, float bw, inout Acc acc) {
    vec4 id = texelFetch(uIds, p, 0) * 255.0;
    vec4 w = texelFetch(uWts, p, 0);
    for (int k = 0; k < 4; k++) {
        float m = floor(id[k] + 0.5);
        if (w[k] <= 0.0 || m >= 127.0) continue;
        vec4 info = texelFetch(uMat, ivec3(int(m), 0, 0), 0) * 255.0;
        float layer = floor(info.r + 0.5);
        if (layer >= pc.scale.y) continue;
        vec2 rep = info.gb / 64.0;
        vec3 uvl = vec3(w2 * rep, layer);
        vec3 c = textureGrad(uAlb, uvl, gx * rep, gy * rep).rgb;   // (gradients calculés hors des branches)
        float ww = w[k] * bw;
        vec2 n = vec2(0.0);
        float h = 0.5;
        if (pc.scale.z > 0.5) {
            vec4 r = textureGrad(uCmb, uvl, gx * rep, gy * rep);
            n = r.rg * 2.0 - 1.0;
            h = r.b;
            ww *= pow(h + 0.2, 4.0);   // (mélange par la hauteur, sans trancher tout à fait)
        }
        acc.col += ww * c;
        acc.nrm += ww * n;
        acc.h += ww * h;
        acc.w += ww;
    }
}

void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    if (n.y < 0.0) n = -n;
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float lit = 0.45 + 0.55 * abs(dot(n, L));
    vec3 col;
    if (pc.scale.w > 0.5) {
        // (mélange bilinéaire des 4 points voisins de la grille de la tuile)
        float g = vCol.a - 1.0;   // (côté de la grille de cette tuile, en points, moins un)
        vec2 f = clamp(vTc.xy, 0.0, 1.0) * g;
        ivec2 i0 = ivec2(min(floor(f), vec2(g - 1.0)));
        vec2 a = f - vec2(i0);
        int layer = int(vTc.z + 0.5);
        vec2 uv = vW.xz * pc.scale.x;   // (pc.scale.x : réglage, 1 = valeurs du jeu)
        vec2 gx = dFdx(uv), gy = dFdy(uv);
        Acc acc = Acc(vec3(0.0), vec2(0.0), 0.0, 0.0);
        point(ivec3(i0, layer), uv, gx, gy, (1.0 - a.x) * (1.0 - a.y), acc);
        point(ivec3(i0 + ivec2(1, 0), layer), uv, gx, gy, a.x * (1.0 - a.y), acc);
        point(ivec3(i0 + ivec2(0, 1), layer), uv, gx, gy, (1.0 - a.x) * a.y, acc);
        point(ivec3(i0 + ivec2(1, 1), layer), uv, gx, gy, a.x * a.y, acc);
        if (acc.w > 1e-5) {
            col = acc.col / acc.w;
            if (pc.scale.z > 0.5) {
                // (normale du relief, dans le repère du sol : x vers +X du monde, y vers +Z, z le long de la normale)
                vec2 t = clamp(acc.nrm / acc.w * 1.6, -0.95, 0.95);   // (un peu accentué : la vue de l'éditeur n'a pas d'ombres)
                vec3 tn = vec3(t, sqrt(max(0.0, 1.0 - dot(t, t))));
                vec3 T = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
                vec3 B = normalize(cross(T, n));
                if (dot(B, vec3(0.0, 0.0, 1.0)) < 0.0) B = -B;
                vec3 N = normalize(T * tn.x + B * tn.y + n * tn.z);
                vec3 L2 = normalize(vec3(0.55, 0.75, 0.35));   // (lumière plus rasante pour lire le relief)
                lit = 0.38 + 0.62 * max(dot(N, L2), 0.0);
                col *= mix(0.82, 1.0, clamp(acc.h / acc.w * 1.4, 0.0, 1.0));   // (creux un peu plus sombres)
            }
        } else {
            col = vCol.rgb;
        }
    } else {
        float steep = smoothstep(0.55, 0.8, 1.0 - abs(n.y));   // (pentes fortes : roche)
        col = mix(vCol.rgb, vec3(0.42, 0.41, 0.38), steep);
    }
    outColor = vec4(col * lit, 1.0);
}
