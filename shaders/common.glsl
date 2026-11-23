layout(push_constant) uniform PC {
    mat4 vp;
    vec4 pos;     // xyz : position ; w : surbrillance (0 rien, 1 sélection, 2 survol)
    vec4 scale;   // xyz : échelle ; w : 1 si texturé
    vec4 rot;     // xyz : angles (radians, X puis Y puis Z) ; w : 1 si on dessine en fil de fer de sélection
    vec4 color;   // couleur sans texture
    vec4 m[5];    // (recettes de mélange des matériaux : couleurs A, B, masque — m[0].a : n° de recette — puis scalaires)
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
