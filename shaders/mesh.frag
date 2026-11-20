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
// décalage u, v et échelle u, v de la coordonnée « bake » dans la carte)
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(set = 1, binding = 0) uniform sampler2D uTex2;
layout(set = 2, binding = 0) uniform sampler2D uDetail;
layout(set = 3, binding = 0) uniform sampler2D uLight;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vW;
layout(location = 2) in vec4 vCol;
layout(location = 3) in vec3 vN;
layout(location = 4) in vec2 vBake;
layout(location = 0) out vec4 outColor;
void main() {
    int flags = int(pc.scale.w + 0.5);
    vec3 nf = normalize(cross(dFdx(vW), dFdy(vW)));
    bool hasN = dot(vN, vN) > 1e-6;
    vec3 n = hasN ? normalize(vN) : nf;
    bool lm = (flags & 16) != 0 && vBake.x >= 0.0;
    vec4 c = (flags & 1) != 0 ? vec4(1.0) : pc.color;
    float alpha = (flags & 1) != 0 ? 1.0 : pc.color.a;
    if ((flags & 1) != 0) {
        c = texture(uTex, vUv);
        if ((flags & 8) == 0 && c.a < 0.35) discard;
        if ((flags & 4) != 0) {
            vec3 d = texture(uDetail, vUv * 8.0).rgb;
            c.rgb *= mix(vec3(1.0), clamp(d * 2.0, 0.0, 2.0), 0.55);
        }
        if ((flags & 2) != 0) {
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
    vec3 L = normalize(vec3(0.35, 1.0, 0.25));
    float lit = hasN ? 0.4 + 0.45 * max(dot(n, L), 0.0) + 0.15 * (0.5 + 0.5 * n.y) + 0.06 * abs(n.x)
                     : 0.42 + 0.5 * abs(dot(n, L)) + 0.08 * abs(n.x);
    // (éclairage précalculé des sommets quand le modèle en a : il remplace en partie l'éclairage simple de l'éditeur)
    float baked = clamp(vCol.r, 0.0, 2.5);
    vec3 col = c.rgb * mix(lit, lit * baked * 1.15, 0.7);
    if (lm) {
        vec3 L2 = texture(uLight, vBake * pc.color.zw + pc.color.xy).rgb;
        // (lumière reçue, précalculée par le jeu : on garde un peu du relief de l'éclairage simple)
        col = c.rgb * (L2 * 1.5 + 0.06) * (0.85 + 0.15 * lit);
    }
    if (pc.pos.w > 0.5) col = mix(col, vec3(1.0, 0.82, 0.3), pc.pos.w > 1.5 ? 0.18 : 0.38);
    outColor = vec4(col, alpha);
}
