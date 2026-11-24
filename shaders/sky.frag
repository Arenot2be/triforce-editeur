#version 450
#extension GL_GOOGLE_include_directive : require
#define SCENE_SET 0
#include "scene.glsl"
// Ciel en arrière-plan : atmosphère du jeu (agl/SkyParam : diffusion de Rayleigh et de Mie, disque du soleil, sol),
// calée sur les tables précalculées de Env/GameScene ; sans SkyParam, un dégradé de la luminance moyenne du ciel.
// Puis le brouillard de la palette sur 3 km et la courbe de tons.
layout(location = 0) out vec4 outColor;
void main() {
    vec2 fc = gl_FragCoord.xy;
    vec2 ndc = vec2((fc.x - S.viewport.x) / S.viewport.z * 2.0 - 1.0, (S.viewport.y + S.viewport.w - fc.y) / S.viewport.w * 2.0 - 1.0);
    vec4 a = S.invVP * vec4(ndc, 0.0, 1.0), b = S.invVP * vec4(ndc, 0.9, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 L;
    if (S.skyG.w > 0.5) {
        // (atmosphère du jeu (SkyParam) : diffusion simple de Rayleigh et de Mie le long du rayon, la lumière du soleil
        //  atténuée jusqu'à chaque point, × la part de diffusion multiple tirée des tables précalculées)
        float Rg = S.skyX.x, Rt = S.skyX.y;
        vec3 o = vec3(0.0, Rg + max(0.001, S.eye.y * 0.001), 0.0), sd = S.sunDirS.xyz;
        float b = dot(o, dir), t = -b + sqrt(max(b * b - dot(o, o) + Rt * Rt, 0.0));
        bool ground = false;
        if (dir.y < 0.0) {
            float h = b * b - dot(o, o) + Rg * Rg;
            if (h >= 0.0 && -b - sqrt(h) > 0.0) { t = -b - sqrt(h); ground = true; }
        }
        float c = dot(dir, sd), gM = S.skyM.w;
        float pR = 0.0596831 * (1.0 + c * c), pM = 0.0795775 * (1.0 - gM * gM) / pow(max(1.0 + gM * gM - 2.0 * gM * c, 1e-4), 1.5);
        const int N = 16, M = 6;
        float ds = t / float(N);
        vec3 tv = vec3(0.0), acc = vec3(0.0);
        for (int i = 0; i < N; i++) {
            vec3 q = o + dir * ((float(i) + 0.5) * ds);
            float h = length(q) - Rg;
            float dR = exp(-h / S.skyR.w), dM = exp(-h / S.skyM.y);
            vec3 ext = S.skyR.rgb * dR + vec3(S.skyM.x * 1.11 * dM);
            tv += ext * ds * 0.5;
            float qb = dot(q, sd), ts = -qb + sqrt(max(qb * qb - dot(q, q) + Rt * Rt, 0.0));
            float dl = ts / float(M);
            vec3 ls = vec3(0.0);
            bool blocked = false;
            for (int j = 0; j < M; j++) {
                vec3 r = q + sd * ((float(j) + 0.5) * dl);
                float hh = length(r) - Rg;
                if (hh < 0.0) { blocked = true; break; }
                ls += (S.skyR.rgb * exp(-hh / S.skyR.w) + vec3(S.skyM.x * 1.11 * exp(-hh / S.skyM.y))) * dl;
            }
            if (!blocked) acc += exp(-(tv + ls)) * (S.skyR.rgb * dR * pR + vec3(S.skyM.x * S.skyM.z * dM * pM)) * ds;
            tv += ext * ds * 0.5;
        }
        float Etop = S.skyK.w;
        L = acc * S.skyK.rgb * Etop;
        if (ground) {
            // (sol vu de loin : sa couleur éclairée par le soleil et le ciel, derrière l'atmosphère traversée)
            L += exp(-tv) * S.skyG.rgb * (S.sunCol.rgb * max(sd.y, 0.0) + S.skyCol.rgb * 3.14159265) * 0.3183099;
        } else {
            // (disque du soleil : RenderSunSize × 0,27°, RenderSunIntensity, atténué par l'atmosphère)
            float cs = cos(radians(0.2666 * S.skyX.w));
            L += exp(-tv) * Etop * S.skyX.z * smoothstep(cs, mix(cs, 1.0, 0.25), c);
        }
    } else {
        vec3 base = S.skyCol.rgb;
        float lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
        float up = max(dir.y, 0.0);
        vec3 hor = mix(base, vec3(lum), 0.55) * 1.7, zen = base * 0.95;
        L = mix(hor, zen, pow(up, 0.4));
        float c = dot(dir, S.sunDirS.xyz);
        L *= 0.85 + 0.15 * (1.0 + c * c);
        float g = 0.8;
        L += S.sunCol.rgb * 0.004 * (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
        L += S.sunCol.rgb * smoothstep(0.99995, 0.99998, c) * 400.0;
        if (dir.y < 0.0) L = hor * mix(0.75, 0.35, clamp(-dir.y * 4.0, 0.0, 1.0));
    }
    vec3 lin = fogApply(L * 3.14159265, S.eye.xyz + dir * 3000.0);
    outColor = vec4(sceneTone(lin), 1.0);
}
