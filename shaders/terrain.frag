#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Sol : textures des matières (Tex/MaterialAlb, couche donnée par la table des matières de la scène), mélangées
// d'après la carte des matières de chaque tuile (4 numéros et 4 poids par point), comme le nuanceur TerrainGround du
// jeu (voir plus bas). Relief (Tex/MaterialCmb, mêmes couches) : R et G, une normale.
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

// (comme le nuanceur du jeu, Shader/TerrainGround : les matières des 4 points voisins sont pondérées
//  bilinéairement ; les deux plus présentes sont gardées ; chacune a une hauteur = son poids + 1,8 × l'alpha de sa
//  couleur (si le poids atteint 0,065) ; le mélange t = 0,5 + 5/3 × (écart de la 1re − écart de la 2e), écarts à la
//  plus haute limités à 0,3 ; relief décodé × 2,007874 − 1,007874)
int gm[16]; float gw[16]; int gn = 0;
void gather(ivec3 p, float bw) {
    vec4 id = texelFetch(uIds, p, 0) * 255.0;
    vec4 w = texelFetch(uWts, p, 0);
    for (int k = 0; k < 4; k++) {
        int m = int(floor(id[k] + 0.5));
        if (w[k] <= 0.0 || m >= 127) continue;
        bool found = false;
        for (int j = 0; j < gn; j++) if (gm[j] == m) { gw[j] += w[k] * bw; found = true; }
        if (!found && gn < 16) { gm[gn] = m; gw[gn] = w[k] * bw; gn++; }
    }
}
struct Mat { vec4 alb; vec2 nrm; bool ok; };
Mat sampleMat(int m, vec2 w2, vec2 gx, vec2 gy) {
    Mat r = Mat(vec4(0.0), vec2(0.0), false);
    vec4 info = texelFetch(uMat, ivec3(m, 0, 0), 0) * 255.0;
    float layer = floor(info.r + 0.5);
    if (layer >= pc.scale.y) return r;
    vec2 rep = info.gb / 64.0;
    vec3 uvl = vec3(w2 * rep, layer);
    r.alb = textureGrad(uAlb, uvl, gx * rep, gy * rep);   // (gradients calculés hors des branches)
    if (pc.scale.z > 0.5) r.nrm = textureGrad(uCmb, uvl, gx * rep, gy * rep).rg * 2.007874 - 1.007874;
    r.ok = true;
    return r;
}

void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    if (n.y < 0.0) n = -n;
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float lit = 0.45 + 0.55 * abs(dot(n, L)), ndl = abs(dot(n, L));
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
        gn = 0;
        gather(ivec3(i0, layer), (1.0 - a.x) * (1.0 - a.y));
        gather(ivec3(i0 + ivec2(1, 0), layer), a.x * (1.0 - a.y));
        gather(ivec3(i0 + ivec2(0, 1), layer), (1.0 - a.x) * a.y);
        gather(ivec3(i0 + ivec2(1, 1), layer), a.x * a.y);
        int i1 = -1, i2 = -1;
        for (int j = 0; j < gn; j++) {
            if (i1 < 0 || gw[j] > gw[i1]) { i2 = i1; i1 = j; }
            else if (i2 < 0 || gw[j] > gw[i2]) i2 = j;
        }
        Mat m0 = i1 >= 0 ? sampleMat(gm[i1], uv, gx, gy) : Mat(vec4(0.0), vec2(0.0), false);
        Mat m1 = i2 >= 0 ? sampleMat(gm[i2], uv, gx, gy) : m0;
        if (m0.ok) {
            if (!m1.ok) m1 = m0;
            float w0 = gw[i1], w1 = i2 >= 0 ? gw[i2] : 0.0;
            float h0 = w0 + (w0 >= 0.065 ? m0.alb.a * 1.8 : 0.0);
            float h1 = w1 + (w1 >= 0.065 ? m1.alb.a * 1.8 : 0.0);
            float hm = max(h0, h1);
            float t = clamp(0.5 + 1.6666667 * ((min(h0 + 0.3, hm) - h0) - (min(h1 + 0.3, hm) - h1)), 0.0, 1.0);
            if (i2 < 0) t = 0.0;
            col = mix(m0.alb.rgb, m1.alb.rgb, t);
            if (pc.scale.z > 0.5) {
                // (normale du relief, dans le repère du sol : x vers +X du monde, y vers +Z, z le long de la normale)
                vec2 tt = clamp(mix(m0.nrm, m1.nrm, t), -0.98, 0.98);
                vec3 tn = vec3(tt, sqrt(max(0.0, 1.0 - dot(tt, tt))));
                vec3 T = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
                vec3 B = normalize(cross(T, n));
                if (dot(B, vec3(0.0, 0.0, 1.0)) < 0.0) B = -B;
                vec3 N = normalize(T * tn.x + B * tn.y + n * tn.z);
                lit = 0.4 + 0.6 * max(dot(N, L), 0.0);
                ndl = max(dot(N, L), 0.0);
            }
        } else {
            col = vCol.rgb;
        }
    } else {
        float steep = smoothstep(0.55, 0.8, 1.0 - abs(n.y));   // (pentes fortes : roche)
        col = mix(vCol.rgb, vec3(0.42, 0.41, 0.38), steep);
    }
    // (sondes de la scène : teinte ambiante autour de la caméra)
    vec3 pa = probeAmbient(n);
    vec3 litc = pa.x < 0.0 ? vec3(lit) : pc.m[1].a > 0.0 ? ndl * sunTint() + pa : lit * (0.45 * sunTint() + 0.55 * pa);
    outColor = vec4(gameTone(col * litc), 1.0);
}
