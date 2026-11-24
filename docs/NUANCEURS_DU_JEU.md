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

## Paramètres des matériaux (résolu)

ShaderAssign+0x20 : un tableau de 24 octets par paramètre, dans l'ordre du dictionnaire des paramètres (+0x28) :
empreinte du nom en +8, décalage u16 en +16 dans les données du matériau (FMAT+0x58, 0x390 octets).
`ns_tex_mtxK` = mode, échelle u, v, rotation, décalage u, v. (FMAT+0x40/+0x48/+0x50 : valeurs, nombres et
décalages des « render info ».) Exemple, falaise de Kokiri : scalar0 −0,3 (décalage de hauteur), scalar1 0,6
(largeur du fondu), scalar5/6 0,85/1 (masque d'occlusion), color1 (0,745 ; 1 ; 0,679) teinte de la mousse,
color2 (0,65 ; 0,5 ; 0,45) teinte des creux.

## Recettes de mélange (lues dans les programmes, appliquées)

Notations : hA, hB = hauteur (B) du RAH de la roche ou du bois et de la mousse ; v = couleur des sommets (rouge) ;
w = clamp(scalar1, 0,01, 1) ; k = clamp((h − t0) / w) ; poids de la mousse f = v < 0,5 ? 2v(1 − k) : 1 − 2k(1 − v).

- **3552031828** (falaises) : t0 = clamp(N·haut + scalar7) − 0,5 + hB × 4,78 − 2,39 + scalar0 ; h = hA ;
  couleur A = roche × color0, mêlée à mousse × color2 par le masque clamp((occlusion − scalar6)/(scalar5 − scalar6)) ;
  résultat = mix(A, mousse × color1, f).
- **4069519554** (bois des maisons) : t0 = clamp(N·haut) − 0,5 + (hB − 0,5) × scalar7 + scalar0 ; h = 0,5 ;
  A = clamp(bois × color0 × (1 + scalar6 × (détail − 1))) ; résultat = mix(A, mousse × color1, f).
- **4260383445** (expression : roche et bois des intérieurs) : t0 = (hB − 0,5) × scalar2 + (v − 0,5) × scalar3 + scalar0 ;
  h = hA ; résultat = mix(mousse × color0, bois × color1, k).

## Sol (TerrainGround, appliqué)

Matières des 4 points voisins pondérées bilinéairement ; les deux plus présentes gardées ; hauteur de chacune =
poids + 1,8 × alpha de sa couleur (si le poids ≥ 0,065) ; t = 0,5 + 5/3 × (min(h0 + 0,3, hmax) − h0 − min(h1 + 0,3,
hmax) + h1) ; couleur et relief mêlés par t.

## Éclairage, ciel, brouillard (AglShader.sharcb)

- Conteneur « BAHS » : 522 sections de contrôle ; le code de chaque programme est à 0x1BC000 + (u32 lu 104 octets
  avant sa section de contrôle). Liste des programmes : hdr_compose, volume_fog, volume_fog_post_filter,
  fog_pre_pass, sky_analyze, light_probe_interpolator, probe_analyze, hemi_to_env, screen_space_shadow, ssao…
- **hdr_compose** (appliqué : case « Tons du jeu ») : + flou lumineux, matrice de couleur 3×4, exposition, courbe
  ACES ajustée x(2,51x + 0,03) / (x(2,43x + 0,59) + 0,14), gamma 1/2,2 ; exposition locale par grille bilatérale
  (luminance 0,2126/0,7152/0,0722).
- **brouillard volumique** : grille 3D (en-diffusion et atténuation) ; tranche = (ln(d/proche) / ln(loin/proche))^(1/k) ;
  couleur = scène × 2^(−atténuation) + en-diffusion. Les densités et couleurs viennent de
  System/GameGraphics/VolumeFogAreaParam et de l'environnement (Env/GameScene…genvb).
- **éclairage des modèles** (passe cubemap du nuanceur « model ») : soleil × ombre × N·L / π + irradiance des sondes
  (harmoniques sphériques d'ordre 1 par objet, coefficients 0,282 et 0,163, reconstruction non linéaire), calculée par
  light_probe_interpolator depuis Bake/Probe.

## Appliqué dans l'éditeur

- Lecture des réglages de chaque matériau (`core/matshader.*`, `bfres.cpp`) : textures réelles de chaque rôle,
  coordonnées `_uK` de la couleur et de la seconde couche.
- `mesh.frag` : carte de normales décodée comme le jeu, rugosité (canal choisi par le matériau), occlusion des RAH,
  reflet discret, découpe alpha des matériaux qui en ont ; falaises à deux couches (roche + mousse) selon la pente et
  les hauteurs. Case « Relief » pour comparer.

## Pas encore appliqué (fichiers manquants)

- Sondes (Bake/Probe/Scene/<scène>/meta.bkprmb, resident.bkprcb, X*_Y*_Z*_*.bkprb), environnement
  (Env/GameScene.Nin_Ounce_NVN2.genvb : ciel, soleil, couleurs), brouillard (VolumeFogAreaParam).

## Lu mais pas encore appliqué (anciennes notes)

- Valeurs des paramètres des matériaux (`ns_param_*`, `ns_tex_mtx*`) : leur rangement dans le bfres (FMAT+0x50 :
  décalages, +0x58 : données) n'est pas encore compris ; l'éditeur prend des valeurs par défaut.
- Graphes 4069519554 (bois des maisons : détail), 4260383445 (expression : roche et mousse des intérieurs) et les
  autres : à traduire un par un.
- `TerrainGround` (72 programmes) : la carte des matières est lue par `textureGather` sur les 4 points voisins ;
  le nuanceur garde les matières présentes, les pondère par les poids bilinéaires (seuil 0,50186 sur les poids),
  puis lit couleur et relief des textures en tableau ; la normale du relief est décodée × 2,007874 − 1. Pas encore
  reporté dans `terrain.frag` (qui mélange déjà 4 matières par la hauteur).
- `SceneMaterial` (1 modèle), `AglShader.sharcb` (éclairage
  différé, ciel, brouillard) et `FoliageShader.sharcb` (préparation du feuillage) : métadonnées lues, code pas encore suivi.

## Sondes de lumière (Bake/Probe/Scene/<préfixe>/)

- `meta.bkprmb` : origine de la grille (ex. −208, 98, −126 pour la forêt Kokiri), blocs de 64 m.
- Fichiers de blocs `X?_Y?_Z?_<niveau>.bkprb` (niveaux 0, 1, 2 : 32³, 16³, 8³ cellules de 48 octets) et `resident.bkprcb` (4³, en-tête 0x20).
  Les enregistrements suivent les cellules : début = en-tête + 48·N³ + 0x24, nombre (u32) à début − 0x1C.
- Enregistrement de 72 octets : position (3 floats), u32, puis 28 demi-flottants :
  - h0..h15 : lumière renvoyée RVB en harmoniques d'ordre 1 (Y00, y, z, x), avec la visibilité du ciel en h3, h7, h11, h15 ;
  - h16..h27 : second jeu RVB (B).
- Éclairement : E = 0.886·L0 + 1.023·L1·n. Le ciel est pris constant (0.045, 0.06, 0.085) en attendant `Env/GameScene.*.genvb`.
- Dans l'éditeur : chaque objet reçoit l'ambiance des sondes voisines (pondération par l'inverse de la distance), normalisée
  par la luminance médiane ; le sol prend celle du point visé. Case « Sondes ».
