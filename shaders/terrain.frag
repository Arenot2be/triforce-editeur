#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Sol : textures des matières (Tex/MaterialAlb, une couche par numéro de matière), mélangées d'après la carte des
// matières de chaque tuile (4 numéros et 4 poids par point). pc.scale : x répétitions par mètre, y nombre de couches,
// w 1 si les textures sont là ; vCol.a : côté de la grille de la tuile (points).
layout(set = 0, binding = 0) uniform sampler2DArray uAlb;
layout(set = 1, binding = 0) uniform sampler2DArray uIds;
layout(set = 2, binding = 0) uniform sampler2DArray uWts;
layout(location = 0) in vec4 vCol;
layout(location = 1) in vec3 vW;
layout(location = 2) in vec3 vTc;
layout(location = 0) out vec4 outColor;

vec4 point(ivec3 p, vec2 uv, vec2 gx, vec2 gy) {   // (couleur pondérée, somme des poids des matières connues)
    vec4 id = texelFetch(uIds, p, 0) * 255.0;
    vec4 w = texelFetch(uWts, p, 0);
    vec4 c = vec4(0.0);
    for (int k = 0; k < 4; k++) {
        float m = floor(id[k] + 0.5);
        if (w[k] <= 0.0 || m >= 127.0 || m >= pc.scale.y) continue;
        c += w[k] * vec4(textureGrad(uAlb, vec3(uv, m), gx, gy).rgb   /* (gradients calculés hors des branches) */, 1.0);
    }
    return c;
}

void main() {
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    float lit = 0.45 + 0.55 * abs(dot(n, normalize(vec3(0.35, 1.0, 0.25))));
    vec3 col;
    if (pc.scale.w > 0.5) {
        // (mélange bilinéaire des 4 points voisins de la grille de la tuile)
        float g = vCol.a - 1.0;   // (côté de la grille de cette tuile, en points, moins un)
        vec2 f = clamp(vTc.xy, 0.0, 1.0) * g;
        ivec2 i0 = ivec2(min(floor(f), vec2(g - 1.0)));
        vec2 a = f - vec2(i0);
        int layer = int(vTc.z + 0.5);
        vec2 uv = vW.xz * pc.scale.x;
        vec2 gx = dFdx(uv), gy = dFdy(uv);
        vec4 c = point(ivec3(i0, layer), uv, gx, gy) * (1.0 - a.x) * (1.0 - a.y) + point(ivec3(i0 + ivec2(1, 0), layer), uv, gx, gy) * a.x * (1.0 - a.y) +
                 point(ivec3(i0 + ivec2(0, 1), layer), uv, gx, gy) * (1.0 - a.x) * a.y + point(ivec3(i0 + ivec2(1, 1), layer), uv, gx, gy) * a.x * a.y;
        col = c.a > 0.001 ? c.rgb / c.a : vCol.rgb;
    } else {
        float steep = smoothstep(0.55, 0.8, 1.0 - abs(n.y));   // (pentes fortes : roche)
        col = mix(vCol.rgb, vec3(0.42, 0.41, 0.38), steep);
    }
    outColor = vec4(col * lit, 1.0);
}
