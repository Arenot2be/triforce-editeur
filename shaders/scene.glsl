// Lumière de la scène, commune aux nuanceurs qui l'utilisent (définir SCENE_SET avant l'inclusion) : ombres du soleil,
// lumières locales (lanternes…), brouillard volumique de la palette d'environnement, ciel. Les éclairements sont comptés
// dans l'unité de l'éditeur (le soleil de la palette vaut 1) ; l'exposition de la palette contient le 1/π de Lambert.
layout(std140, set = SCENE_SET, binding = 0) uniform Scene {
    mat4 lightVP;      // (projection de la carte d'ombre)
    mat4 invVP;        // (inverse de la projection de la vue, pour le ciel)
    vec4 eye;          // (position de l'œil)
    vec4 viewport;     // (x, y, largeur, hauteur en pixels)
    vec4 shadowP;      // (x : ombres, y : 1 / taille, z : force, w : décalage le long de la normale)
    vec4 sunDirS;      // (direction vers le soleil ; w : lumière du jeu)
    vec4 sunCol;       // (éclairement du soleil ; w : exposition, 0 = sans courbe de tons)
    vec4 skyCol;       // (luminance moyenne du ciel ; w : brouillard)
    vec4 ambUp;        // (éclairement ambiant vers le haut au point visé ; w : hauteur du sol au point visé)
    vec4 fogA[2];      // (densité près, début et fin de son fondu, densité loin)
    vec4 fogB[2];      // (début et fin du fondu loin, décroissance avec la hauteur par mètre, hauteur de départ)
    vec4 fogRay[2];    // (diffusion Rayleigh rgb par mètre ; w : sol comme hauteur de départ)
    vec4 fogMie[2];    // (diffusion de Mie rgb par mètre ; w : anisotropie)
    vec4 fogAbs[2];    // (absorption rgb par mètre ; w : couche active)
    vec4 fogEmit[2];   // (émission rgb par mètre, en luminance du ciel)
    vec4 fogLight;     // (x : échelle du soleil, y : échelle du ciel)
    vec4 lightN;       // (x : nombre de lumières locales)
    vec4 lightPos[16]; // (position ; w : portée)
    vec4 lightCol[16]; // (intensité rgb, en unités × m² ; w : cos de l'angle extérieur, −2 = ponctuelle)
    vec4 lightDir[16]; // (direction du projecteur ; w : cos de l'angle intérieur)
} S;
layout(set = SCENE_SET, binding = 1) uniform sampler2DShadow uShadow;

// (ombre portée du soleil : carte orthographique autour du point visé, filtre 3 × 3)
float sunShadow(vec3 wp, vec3 n) {
    if (S.shadowP.x < 0.5) return 1.0;
    vec4 p = S.lightVP * vec4(wp + n * S.shadowP.w, 1.0);
    vec3 q = p.xyz / p.w;
    vec2 uv = q.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.002))) || any(greaterThan(uv, vec2(0.998))) || q.z > 1.0 || q.z < 0.0) return 1.0;
    float s = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) s += texture(uShadow, vec3(uv + vec2(x, y) * S.shadowP.y, q.z - 0.00004));
    s /= 9.0;
    // (fondu vers « éclairé » au bord de la carte)
    float edge = smoothstep(0.0, 0.08, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
    return mix(1.0, mix(1.0, s, S.shadowP.z), edge);
}

// (lumières locales : ponctuelles et projecteurs, éclairement I / d² avec une fin douce à la portée)
vec3 localLights(vec3 wp, vec3 n, bool hasN) {
    vec3 e = vec3(0.0);
    int cnt = int(S.lightN.x + 0.5);
    for (int i = 0; i < cnt; i++) {
        vec3 d = S.lightPos[i].xyz - wp;
        float d2 = dot(d, d), r = S.lightPos[i].w;
        if (d2 > r * r) continue;
        vec3 l = d * inversesqrt(max(d2, 1e-6));
        float ndl = hasN ? max(dot(n, l), 0.0) : abs(dot(n, l)) * 0.7 + 0.3;
        float win = 1.0 - d2 / (r * r);
        win *= win;
        float cone = 1.0;
        if (S.lightCol[i].w > -1.5) cone = smoothstep(S.lightCol[i].w, S.lightDir[i].w, dot(-l, S.lightDir[i].xyz));
        e += S.lightCol[i].rgb * (ndl * win * cone / max(d2, 0.04));
    }
    return e;
}

float fogRamp(int k, float d) {
    vec4 a = S.fogA[k], b = S.fogB[k];
    float nearD = a.x * smoothstep(a.y, max(a.z, a.y + 1e-3), d);
    return mix(nearD, a.w, smoothstep(b.x, max(b.y, b.x + 1e-3), d));
}
float fogHeight(int k, float y) {
    float y0 = S.fogB[k].w + (S.fogRay[k].w > 0.5 ? S.ambUp.w : 0.0);
    return exp(-S.fogB[k].z * max(0.0, y - y0));
}
// (brouillard le long du rayon œil → point : 16 pas ; lumière du soleil (phases de Rayleigh et de Henyey-Greenstein),
//  du ciel et émission ; « lin » est dans la convention des éclairements, d'où le π de la luminance diffusée)
vec3 fogApply(vec3 lin, vec3 wp) {
    if (S.skyCol.w < 0.5) return lin;
    vec3 ro = S.eye.xyz, rd = wp - ro;
    float D = length(rd);
    if (D < 1e-3) return lin;
    rd /= D;
    float c = dot(rd, S.sunDirS.xyz);
    float pR = 0.0596831 * (1.0 + c * c);
    vec3 Esun = S.sunCol.rgb * S.fogLight.x, Eamb = S.ambUp.rgb * S.fogLight.y * 0.0795775, Lsky = S.skyCol.rgb * S.fogLight.y;
    const int N = 16;
    float ds = D / float(N);
    vec3 T = vec3(1.0), L = vec3(0.0);
    for (int i = 0; i < N; i++) {
        float t = (float(i) + 0.5) * ds;
        float y = ro.y + rd.y * t;
        vec3 st = vec3(0.0), src = vec3(0.0);
        for (int k = 0; k < 2; k++) {
            if (S.fogAbs[k].w < 0.5) continue;
            float dn = fogRamp(k, t) * fogHeight(k, y);
            if (dn <= 0.0) continue;
            float g = S.fogMie[k].w;
            float pM = 0.0795775 * (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
            vec3 sR = S.fogRay[k].rgb * dn, sM = S.fogMie[k].rgb * dn;
            st += sR + sM + S.fogAbs[k].rgb * dn;
            src += sR * (pR * Esun + Eamb) + sM * (pM * Esun + Eamb) + S.fogEmit[k].rgb * dn * Lsky;
        }
        vec3 tr = exp(-st * ds);
        L += T * src * mix(vec3(ds), (1.0 - tr) / max(st, vec3(1e-6)), step(vec3(1e-6), st));
        T *= tr;
        if (max(T.r, max(T.g, T.b)) < 0.003) break;
    }
    return lin * T + L * 3.14159265;
}
// (courbe de tons du jeu sur une valeur dans la convention des éclairements)
vec3 sceneTone(vec3 lin) {
    float e = S.sunCol.w;
    if (e <= 0.0) return pow(max(lin, vec3(0.0)), vec3(0.4545454));
    vec3 x = clamp(lin * e, 0.0, 10000.0);
    vec3 t = clamp(x * (x * 2.51 + 0.03) / (x * (x * 2.43 + 0.59) + 0.14), 0.0, 1.0);
    return pow(t, vec3(0.4545454));
}
