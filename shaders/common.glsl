layout(push_constant) uniform PC {
    mat4 vp;
    vec4 pos;     // xyz : position ; w : surbrillance (0 rien, 1 sélection, 2 survol)
    vec4 scale;   // xyz : échelle ; w : 1 si texturé
    vec4 rot;     // xyz : angles (radians, X puis Y puis Z) ; w : 1 si on dessine en fil de fer de sélection
    vec4 color;   // couleur sans texture
    vec4 m[5];    // (recettes de mélange des matériaux : couleurs A, B, masque — m[0].a : n° de recette — puis scalaires)
    vec4 amb[3];  // (sondes : a.rgb, M.b.x | M.r.xyz, M.b.y | M.g.xyz, M.b.z)
} pc;

// (courbe de tons du jeu, lue dans AglShader.sharcb « hdr_compose » : matrice de couleur, exposition, puis la courbe
//  ACES ajustée (x(2,51x + 0,03)) / (x(2,43x + 0,59) + 0,14) et le gamma 1/2,2 ; pc.m[2].a : exposition, 0 = sans)
vec3 gameTone(vec3 c) {
    float e = pc.m[2].a;
    if (e <= 0.0) return c;
    vec3 x = clamp(pow(max(c, vec3(0.0)), vec3(2.2)), 0.0, 10000.0) * e;
    vec3 t = clamp(x * (x * 2.51 + 0.03) / (x * (x * 2.43 + 0.59) + 0.14), 0.0, 1.0);
    return pow(t, vec3(0.4545454));
}

// (soleil du jeu (palette d'environnement, ou transmittance de l'atmosphère) : pc.m[1].a = couleur × force, 8 bits par
//  canal sur [0, 2] ; pc.m[4].a = 1 + élévation·4000 + azimut (dixièmes de degré, azimut depuis +Z vers +X).
//  pc.m[1].a > 0 : éclairage dans les unités du jeu (soleil de la palette = 1), sinon réglages simples de l'éditeur)
bool gameLight() { return pc.m[1].a > 0.0; }
vec3 sunTint() {
    if (!gameLight()) return vec3(1.0, 0.97, 0.9);
    uint u = uint(pc.m[1].a + 0.5);
    return vec3(float((u >> 16) & 255u), float((u >> 8) & 255u), float(u & 255u)) * (2.0 / 255.0);
}
vec3 sunDir() {
    if (pc.m[4].a < 0.5 || !gameLight()) return normalize(vec3(0.35, 1.0, 0.25));
    uint u = uint(pc.m[4].a + 0.5) - 1u;
    float el = radians(float(u / 4000u) * 0.1), az = radians(float(u % 4000u) * 0.1);
    return vec3(sin(az) * cos(el), sin(el), cos(az) * cos(el));
}
// (couleur finale à partir de l'éclairement, en lumière du jeu : albédo linéaire × éclairement × exposition de la
//  palette (qui contient le 1/π de Lambert), courbe ACES et gamma ; sans « Tons du jeu », simple retour au gamma)
vec3 gameShade(vec3 alb, vec3 E, vec3 add) {
    vec3 lin = pow(max(alb, vec3(0.0)), vec3(2.2)) * E + add;
    float e = pc.m[2].a;
    if (e <= 0.0) return pow(max(lin, vec3(0.0)), vec3(0.4545454));
    vec3 x = clamp(lin * e, 0.0, 10000.0);
    vec3 t = clamp(x * (x * 2.51 + 0.03) / (x * (x * 2.43 + 0.59) + 0.14), 0.0, 1.0);
    return pow(t, vec3(0.4545454));
}

// (éclairement ambiant des sondes de lumière du jeu (Bake/Probe), rapporté à la médiane vers le haut ; -1 sans sondes)
vec3 probeAmbient(vec3 n) {
    vec4 a = pc.amb[0], r = pc.amb[1], g = pc.amb[2];
    if (a.x == 0.0 && a.y == 0.0 && a.z == 0.0 && r.x == 0.0) return vec3(-1.0);
    vec3 e = a.rgb + vec3(dot(r.xyz, n), dot(g.xyz, n), dot(vec3(a.w, r.w, g.w), n));
    // (ciel du jeu connu (pc.m[1].a > 0) : unités absolues, soleil = 1 ; sinon rapporté à la médiane)
    return clamp(e, vec3(gameLight() ? 0.0005 : 0.05), vec3(gameLight() ? 50.0 : 3.0));
}
