layout(push_constant) uniform PC {
    mat4 vp;
    vec4 pos;     // xyz : position ; w : surbrillance (0 rien, 1 sélection, 2 survol)
    vec4 scale;   // xyz : échelle ; w : 1 si texturé
    vec4 rot;     // xyz : angles (radians, X puis Y puis Z) ; w : 1 si on dessine en fil de fer de sélection
    vec4 color;   // couleur sans texture
} pc;
