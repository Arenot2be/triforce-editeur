#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Eau : la carte de normales du matériau d'eau du jeu (ou des vagues calculées s'il n'y en a pas), défilant dans
// deux sens ; reflet du ciel selon l'angle (Fresnel), couleur du fond, éclat du soleil.
// pc.color : rgb = couleur de l'eau en profondeur, a = temps (s) ; pc.scale.w = 1 : carte de normales liée ;
// pc.rot.w = 1 : carte signée (déjà entre -1 et 1)
layout(set = 0, binding = 0) uniform sampler2D uNrm;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vW;
layout(location = 2) flat in vec4 vEye;
layout(location = 0) out vec4 outColor;

vec2 nrmAt(vec2 uv) {
    vec2 t = texture(uNrm, uv).rg;
    return pc.rot.w > 0.5 ? t : t * 2.0 - 1.0;
}
vec2 waves(vec2 p, float t) {
    // (pente de quelques ondes : dérivées de sinus, en x et z)
    vec2 g = vec2(0.0);
    g += 0.35 * vec2(0.8, 0.6) * cos(dot(p, vec2(0.8, 0.6)) * 1.3 + t * 1.1);
    g += 0.25 * vec2(-0.5, 0.86) * cos(dot(p, vec2(-0.5, 0.86)) * 2.1 + t * 1.7);
    g += 0.15 * vec2(0.95, -0.3) * cos(dot(p, vec2(0.95, -0.3)) * 3.7 + t * 2.3);
    g += 0.08 * vec2(-0.2, -0.98) * cos(dot(p, vec2(-0.2, -0.98)) * 6.1 + t * 3.1);
    return g;
}
void main() {
    float t = pc.color.a;
    vec3 ng = normalize(cross(dFdx(vW), dFdy(vW)));
    if (ng.y < 0.0) ng = -ng;
    vec2 slope;
    if (pc.scale.w > 0.5) {
        // (deux couches de la carte du jeu, à des échelles et des vitesses différentes ; coordonnées du monde si le
        //  modèle n'a pas de coordonnées de texture utiles)
        vec2 uv = length(fwidth(vUv)) > 1e-6 ? vUv : vW.xz * 0.1;
        slope = nrmAt(uv + t * vec2(0.012, 0.007)) * 0.6 + nrmAt(uv * 1.7 + t * vec2(-0.009, 0.013)) * 0.4;
        slope += 0.25 * waves(vW.xz * 0.6, t);
    } else {
        slope = waves(vW.xz * 0.6, t);
    }
    // (surface surtout horizontale : la pente penche la normale autour de la normale géométrique)
    vec3 tx = normalize(cross(vec3(0, 0, 1), ng) + vec3(1e-4, 0, 0));
    vec3 tz = cross(ng, tx);
    vec3 n = normalize(ng + 0.35 * (slope.x * tx + slope.y * tz));
    vec3 v = vEye.w > 0.5 ? normalize(vEye.xyz - vW) : normalize(vEye.xyz);
    float nv = clamp(dot(n, v), 0.0, 1.0);
    float fres = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    vec3 r = reflect(-v, n);
    vec3 sky = mix(vec3(0.78, 0.86, 0.9), vec3(0.32, 0.52, 0.78), clamp(r.y, 0.0, 1.0));
    vec3 deep = pc.color.rgb;
    // (un peu de lumière diffuse dans l'eau, pour que la couleur du fond ne soit pas uniforme)
    vec3 L = normalize(vec3(0.35, 0.75, 0.35));
    vec3 body = deep * (0.75 + 0.35 * max(dot(n, L), 0.0));
    vec3 col = mix(body, sky, fres);
    float spec = pow(max(dot(r, L), 0.0), 180.0) * 1.6 + pow(max(dot(r, L), 0.0), 24.0) * 0.12;
    col += vec3(1.0, 0.97, 0.9) * spec;
    if (pc.pos.w > 0.5) col = mix(col, vec3(1.0, 0.82, 0.3), pc.pos.w > 1.5 ? 0.18 : 0.38);
    outColor = vec4(col, clamp(mix(0.62, 0.95, fres) + spec, 0.0, 1.0));
}
