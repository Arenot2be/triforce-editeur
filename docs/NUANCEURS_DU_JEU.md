# Nuanceurs du jeu : ce qui est lu, ce qui est appliqué

Fichiers étudiés (romfs du joueur, jamais dans ce dépôt) : `Shader/Model`, `Foliage`, `TerrainGround`, `SceneMaterial`
`.Product.110.product.Nin_Ounce_NVN2.bfsha.zs`, `Shader/ExternalBinaryString.bfres.zs`,
`Shader/ApplicationPackage.Nin_Ounce_NVN2.release.sarc.zs` (contient en plus `AglShader.sharcb`, `FoliageShader.sharcb`).

## Méthode

1. **bfsha** (`tools/nuanceurs/bfsha.py`) : modèles de nuanceur à 0x80 + 0x100·i ; options statiques et dynamiques
   (0x28 octets : nom, choix, … masque u32 à +0x20, mot de la clé à +0x24, décalage à +0x25) ; programmes (64 octets,
   variation BNSH à +0x20) et leurs clés (mots u32) ; variation BNSH → programme binaire → code de chaque étape
   (`+8` contrôle, `+0x10` code, `+0x18` tailles). Blocs uniformes : 0x20 octets (variables, dictionnaire, valeurs par
   défaut, index, type, taille, nombre) ; variables de 16 octets (décalage + 1 à +12).
2. **Programme d'un matériau** (`match.py`, `decmat.py`) : options du matériau → programmes dont la clé a les mêmes
   choix (une variante par passe : material, cubemap, zonly, depthshadow, xlu_zprepass, dynamic…).
3. **Traduction** : `shadertool` de Triforce (recompilateur Eden), puis `spirv-opt -O` et SPIRV-Cross. Trois
   changements à l'outil pour lire les nuanceurs hors du jeu : la banque c1 (constantes du programme, dans le code à
   partir de contrôle+0x40), l'étape lue dans le contrôle (+0x56 : 6 tâche, 7 maillage, avec leurs tailles +0x58/+0x60),
   et une seule sortie (`one_exit`) seulement pour les nuanceurs de tâche et de maillage — sinon les nuanceurs de pixels
   perdaient leurs sorties.

## Matériaux (`model`, 23 346 programmes, 243 options statiques)

- Les noms des options et paramètres sont des empreintes de 64 bits dans les bfres ; `ExternalBinaryString.bfres`
  les nomme : empreintes triées à l'adresse lue en +0xB8, dictionnaire des noms dans le même ordre en +0xC0.
- Matériau (FMAT+0x10 → ShaderInfo) : +0 ShaderAssign (archive, modèle, dictionnaires : options +0x40, samplers +0x38,
  paramètres +0x28) ; +0x18 samplers du bfres dans l'ordre des samplers du bfsha (`_a0 _n0 _r0 _m0 _ao0 _s0 _array0..2
  bake0 bake1 _a1 _n1`) ; +0x28 bits des options booléennes ; +0x30 valeurs texte ; +0x38 numéros d'options ;
  +0x45 nombre de samplers, +0x46 de booléens, +0x48 de valeurs. FMAT+0x38 : noms des samplers dans l'ordre des textures.
- Graphe : `ns_textureN_sampler` = J parmi `_a0 _n0 _r0 _m0 _ao0 _s0 _a1 _n1` (100+ : textures en tableau),
  `ns_textureN_texcoord` → `ns_texcoordK_mapping` (0..3 : attribut `_uK`) et `ns_texcoordK_srt` (`ns_tex_mtxK`).
  Grandeurs : `ns_albedo_select`, `ns_normal_map_select`, `ns_roughness_select` + `_channel`, `ns_ao_select`,
  `ns_metalness_select`, `ns_emission_select`, `ns_alpha_select` + `_channel` : 0..7 texture N, 100.. paramètres,
  200.. sortie d'une opération ou d'un graphe (`ns_overwrite_surface_graph`, `ns_expression_evaluate_graph`), 300 constante.
- Bloc `gsys_material` (c5 dans les nuanceurs) : `ns_tex_mtx0..3` 0x00/0x20/0x40/0x60, `ns_param_color0..5` 0x80..0xD0,
  `ns_param_vector0..1` 0xE0/0xF0, `ns_param_texture0..7_array_index` 0x100.., `ns_param_scalar0..7` 0x120..0x13C,
  `ns_emission_intensity` 0x1A8, `gsys_alpha_test_ref_value` 0x1B0, `gsys_bake_st0/1` 0x1C0/0x1D0.
- Cartes de normales : x, y × 2,007874 − 1,007874, z = √(1 − x² − y²). Les `_Nrm` gardent souvent la rugosité en B ;
  les `_RAH` : rugosité, occlusion, hauteur.
- **Graphe 3552031828** (falaises de Kokiri) : roche (textures 0-2 en `_u1`, `ns_tex_mtx1`) et mousse (3-5, `_u1`,
  `ns_tex_mtx2`), carte de normales du modèle (6) et occlusion (7) en `_u0`. Mousse : couleur × `ns_param_color1`,
  poids = clamp((hauteur roche − t0) / largeur) avec t0 = (pente vers le haut − 0,5) + (hauteur mousse × 4,78 − 2,39 +
  scalar0), largeur = clamp(scalar1) ; puis mélange « overlay » avec la couleur des sommets ; masque `_gn0` entre
  `ns_param_color0` et `color2`.
- Éclairage (passe cubemap) : soleil × ombre × N·L / π, plus une irradiance par harmoniques sphériques d'ordre 1
  (0,282 ; 0,163) à reconstruction non linéaire, données par les sondes de la scène (Bake/Probe).

## Appliqué dans l'éditeur

- Lecture des réglages de chaque matériau (`core/matshader.*`, `bfres.cpp`) : textures réelles de chaque rôle,
  coordonnées `_uK` de la couleur et de la seconde couche.
- `mesh.frag` : carte de normales décodée comme le jeu, rugosité (canal choisi par le matériau), occlusion des RAH,
  reflet discret, découpe alpha des matériaux qui en ont ; falaises à deux couches (roche + mousse) selon la pente et
  les hauteurs. Case « Relief » pour comparer.

## Lu mais pas encore appliqué

- Valeurs des paramètres des matériaux (`ns_param_*`, `ns_tex_mtx*`) : leur rangement dans le bfres (FMAT+0x50 :
  décalages, +0x58 : données) n'est pas encore compris ; l'éditeur prend des valeurs par défaut.
- Graphes 4069519554 (bois des maisons : détail), 4260383445 (expression : roche et mousse des intérieurs) et les
  autres : à traduire un par un.
- `TerrainGround` (2 modèles : terrain_ground, terrain_zonly), `SceneMaterial` (1), `AglShader.sharcb` (éclairage
  différé, ciel, brouillard) et `FoliageShader.sharcb` (préparation du feuillage) : métadonnées lues, code pas encore suivi.
