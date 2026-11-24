#version 450
#extension GL_GOOGLE_include_directive : require
#define SCENE_SET 0
#include "scene.glsl"
// Ciel en arrière-plan : luminance moyenne du ciel du jeu (tables d'atmosphère de Env/GameScene, pour la hauteur du
// soleil), plus claire et moins bleue vers l'horizon, halo de Mie et disque du soleil ; puis le brouillard de la
// palette sur 3 km et la courbe de tons.
layout(location = 0) out vec4 outColor;
void main() {
    vec2 fc = gl_FragCoord.xy;
    vec2 ndc = vec2((fc.x - S.viewport.x) / S.viewport.z * 2.0 - 1.0, (S.viewport.y + S.viewport.w - fc.y) / S.viewport.w * 2.0 - 1.0);
    vec4 a = S.invVP * vec4(ndc, 0.0, 1.0), b = S.invVP * vec4(ndc, 0.9, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 base = S.skyCol.rgb;
    float lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
    float up = max(dir.y, 0.0);
    vec3 hor = mix(base, vec3(lum), 0.55) * 1.7, zen = base * 0.95;
    vec3 L = mix(hor, zen, pow(up, 0.4));
    float c = dot(dir, S.sunDirS.xyz);
    L *= 0.85 + 0.15 * (1.0 + c * c);
    float g = 0.8;
    L += S.sunCol.rgb * 0.004 * (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
    L += S.sunCol.rgb * smoothstep(0.99995, 0.99998, c) * 400.0;
    if (dir.y < 0.0) L = hor * mix(0.75, 0.35, clamp(-dir.y * 4.0, 0.0, 1.0));
    vec3 lin = fogApply(L * 3.14159265, S.eye.xyz + dir * 3000.0);
    outColor = vec4(sceneTone(lin), 1.0);
}
