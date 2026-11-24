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

- Le brouillard volumique (palette et zones lues ; unités des densités à établir dans le nuanceur volume_fog) ; le dessin
  du ciel (blocs d'en-diffusion du skybin, paramétrage pas encore établi) ; mélange entre deux palettes horaires ;
  ombres portées.

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
- Éclairement : E = 0.886·L0 + 1.023·L1·n (π·Y00 et 2π/3·Y1).
- Sens des trois parts (vérifié sur la forêt Kokiri et le village Cocorico) : A = lumières locales cuites (orangé,
  très fort près des lanternes) ; B = soleil renvoyé (jaune-vert, lié à la visibilité du ciel) ; h3/h7/h11/h15 =
  projection de la visibilité du ciel (jusqu'à ~2,1 : 1,77 pour un hémisphère dégagé). Ciel = luminance du ciel ×
  visibilité. B a l'ordre de grandeur attendu pour un soleil d'éclairement 1 : les sondes sont dans les unités des
  tables d'atmosphère.
- Les quatre niveaux sont lus ; autour d'un point, seul le plus fin présent compte (niveau 0 : une sonde tous les 2 m).
  Quelques sondes très fortes et orangées (près des lanternes, ex. −80, 115, −47) sont réelles : lumières locales cuites.
- Dans l'éditeur, avec le ciel du jeu : éclairement en unités absolues (soleil = 1), soleil × N·L + sondes, sans
  ombres portées ; clic droit sur « Sondes » pour forcer l'ambiance. Sans le ciel : rapporté à la médiane.
- Chaque objet reçoit l'ambiance des sondes voisines (pondération par l'inverse de la distance), normalisée
  par la luminance médiane ; le sol prend celle du point visé. Case « Sondes ».

## Ciel (Env/GameScene.Nin_Ounce_NVN2.genvb)

- SARC : `gamescene.bgenv` (AAMP v2, type « genv ») et `res/master_field.skybin` (aussi `Master_Field.skybin`, identique).
  Le bgenv associe chaque ensemble (Master_Field, KokiriForest, SacredForestMeadow, HyruleField_Child/Adult,
  DeathMountainTrail_Adult, ZoraRiver, KakarikoVillage, Dungeon) au même skybin.
- skybin, 1 212 416 octets sans en-tête, RGBA demi-flottants (alpha nul), rangées linéaires :
  - 0x000000 : transmittance 512 × 32 (x = altitude, 0 = sol ; y = direction, ligne 31 = zénith, ~21,5 = horizon au sol).
    Au zénith (0,972 ; 0,931 ; 0,837). Les lignes suivent la loi de masse d'air à quelques pour cent près.
  - 0x020000 : quatre blocs de 256 Kio d'en-diffusion, chacun 64 × 64 × 8.
  - 0x120000 : éclairement du ciel 512 × 8 (x = altitude ; y = hauteur du soleil, mu = −0,2 + 1,2·y/7).
- Programmes qui les fabriquent et les lisent : `Lib/agl/agl_resource…sarc` → `agl_technique_pfx.sharcb`
  (sky_transmittance, sky_irradiance, sky_inscatter, sky_bake_*, sky_postfx_sky/ground, sky_calc_sh ; constantes
  cSunIntensity, cRadiusAtmosphere, cScatteringCoeffRayleigh/Mie…). Conteneur BAHS : base du code en +0x1C
  (0x1B0000 ici), contrôle 0x98761234, code à base + (u32 104 octets avant le contrôle) ; chaque programme décrit
  ses options et l'indice de sa première variante.
- Dans l'éditeur : couleur du soleil = transmittance du zénith ^ masse d'air (Kasten–Young) ; luminance du ciel =
  éclairement du ciel / π, pour la hauteur du soleil de l'éditeur (66,7°).

## Brouillard (System/GameGraphics/VolumeFogAreaParam)

- Un fichier par zone (KokiriForest, KokiriForest_WetArea_01/02, HyruleField_KokiriArea…), ne gardant que ce qui
  change : Fog1Params/Fog2Params (AbsorptionRate, EmissionRate, MieScatterRate en RGBA — A sert de force —,
  RayleighScatterRate, MieScatterAnisotropy, Near/FarDensity et leurs fondus, HeightFalloff, HeightYOffset,
  NoiseStrength, UseHeightMap), ShapeType (empreinte de la forme), ShapeGradientWidth.
- KokiriForest : seulement de la diffusion Rayleigh (0,8), densité qui décroît avec la hauteur (0,5), rien avant 9–10 m.
- Les valeurs de base sont dans la palette d'environnement (VolumeFogParams1/2, ci-dessous) ; les zones s'y ajoutent.

## Palettes d'environnement (Pack/EnvPalette.pack)

- SARC de BYML : WorldMgr/ResEnvPaletteTimeTable (32), ResEnvPalette (191), ResEnvPaletteAreaUnique (13),
  ResFixedEnvPalette (26), ResWeatherInfluenceParam (12).
- Table horaire (ex. KokiriForest) : IsForceMainLight, MainLight_Elevation / _Azimuth (forêt Kokiri : 50,76° et
  333,36°, soleil fixe), TimeTableArray d'entrées {Hour, Min, EnvPalette, BlendTime, courbe}. Kokiri : Noon dès 5 h 30,
  Night dès 19 h 30. MainField_Bluesky : soleil calculé selon l'heure (non forcé), 8 palettes de BeforeSunrise à Night.
- Palette (ex. KokiriForest_Noon) : soleil BgDifColor × température BgDifTemperature (4500 K) × BgDifIntensity (10 000
  lux ; champ ouvert 110 000) ; SkySunColorIntensity (7 500, intensité du soleil pour le ciel) ; ExposureValue (EV 10 ;
  champ 14) ; WhiteBalanceTemperature (6 500 K) ; DirectionalLightBounceScale (0,75), SkyLightBounceScale,
  AmbientIntensity ; HemiLight* ; Bloom* ; SkyRParam_* (amplificateurs Rayleigh 1,2 et Mie 50) ; brouillard
  VolumeFogParams1/2 (Absorption/Emission/Mie/Rayleigh : couleur et densité, Near/FarDensity et fondus, EHFFalloff et
  EHFYOffset pour la hauteur, MieAnisotropy, NoiseStrength) et VolumeFogProbeSkyLightScale (6).
- Unités vérifiées par l'exposition : unité / (π · 1,2 · 2^EV) donne 2,59 pour Kokiri et 1,78 pour le champ : même ordre,
  l'éclairement du soleil est bien en lux et l'exposition photographique (EV100).
- Dans l'éditeur (case « Sondes ») : palette de la scène à l'heure choisie (clic droit, 12 h par défaut ; table du
  nom de la scène, sinon MainField_Bluesky) ; soleil = couleur de la palette, direction forcée sinon celle de
  l'éditeur ; ambiance = lumières locales + renvoi × DirectionalLightBounceScale + ciel × (SkySunColorIntensity /
  BgDifIntensity) × SkyLightBounceScale ; couleur = ACES(albédo linéaire × éclairement × exposition), avec une
  correction en IL (clic droit sur « Tons du jeu »). Convention d'azimut supposée : depuis +Z vers +X.

## Fondu des palettes, ombres, lumières locales, brouillard, ciel (éditeur)

- **Fondu** : l'entrée de la table horaire commencée en dernier est fondue avec la précédente pendant son BlendTime
  (minutes), selon sa courbe (« Hermit » : points 0 → 1, tangentes Data[1] et Data[3] ; IsBlendCurve faux : linéaire).
  Éclairements (lux) mêlés en logarithme, EV et le reste linéairement. Soleil non fixé (champ) : course simple, levé à
  6 h à l'est, 60° à midi, couché à 18 h, la lune en face la nuit (approximation : le jeu le calcule autrement).
- **Ombres** : carte orthographique 4096² dans la direction du soleil, centrée sur le point visé (rayon 35 à 220 m selon
  la distance de la caméra, calée sur les texels), modèles (avec découpe alpha), feuillage et terrain ; filtre 3 × 3.
- **Lumières locales** : les 16 lumières ponctuelles de Bake/Light les plus proches du point visé, intensité lue en
  candelas (I / d² lux ; cohérent avec la part A des sondes autour des lanternes), portée là où l'éclairement tombe
  sous 0,002 unité. Les projecteurs (intensités de l'ordre de 10⁶) et les lumières rectangulaires ne sont pas encore
  utilisés : leur unité n'est pas établie. La part A des sondes contient déjà ces lumières, cuites : l'éclairage direct
  s'y ajoute.
- **Brouillard** : deux couches de la palette, intégrées en 16 pas le long du rayon : densité près / loin selon la
  distance, décroissance exp(−EHFFalloff · (y − y₀) / 10 m) au-dessus de y₀ = EHFYOffset (+ hauteur du sol au point visé
  si UseHeightMap) ; diffusion Rayleigh (teinte λ⁻⁴, phase de Rayleigh) et de Mie (MieColor, Henyey-Greenstein avec
  MieAnisotropy) éclairées par le soleil × VolumeFogMainLightScale et l'ambiance × VolumeFogProbeSkyLightScale ;
  absorption AbsorptionColor × densité ; émission EmissionColor × densité × luminance du ciel. **Unité des densités
  supposée** : 1,5·10⁻⁴ par mètre (avec elle, la couche 2 de Kokiri — densité 40 au-delà de 120 m — ferme l'horizon
  de la forêt et la couche 1 reste légère) ; réglable (clic droit sur « Brouillard »). Zones (VolumeFogAreaParam) pas
  encore appliquées : il faut les acteurs de zone de la carte.
- **Ciel** : voir « Réglages AGL » ci-dessous ; sans eux, dégradé de la luminance moyenne des tables d'atmosphère.

## Réglages AGL (dossier agl/ : BYML « agl__…Param »)

- Dossiers : BloomParam, ColorCorrectionParam, CubeMapMgrParam, DepthOfFieldParam, DirectionalLightParam (23),
  EnvObjMgrParam (objets d'environnement par scène), FilterAAParam, HemisphereLightParam, LocalExposureParam,
  OccludedEffectMgrParam, OfxLensFlarePresetParam, ProjectorParam, ShadowPrePassParam, SkyParam, StarParam.
- **SkyParam/MainField** : atmosphère « Static » (celle des tables du skybin : Rayleigh (0,00411 ; 0,011275 ;
  0,028364) par km sur 5 km, Mie 0,0018 par km sur 1 km, g 0,8 ; sol 6 360 km, haut 6 420 km) — vérifié : la
  transmittance au zénith qu'elle donne (0,98 ; 0,95 ; 0,87) est celle du skybin à 1 % près, et l'éclairement du ciel
  en diffusion simple vaut la moitié de celui des tables (le reste : diffusion multiple). Réglages du ciel dessiné :
  MieAmplifierRendering 8, MieSymmetricalPropRendering 0,85, RenderSunIntensity 100, RenderSunSize 1, GroundColor ;
  atmosphère « Dynamic » (Rayleigh sur 24 km, Mie sur 2 km, × 1 800 pour le Mie) pas utilisée ; ScatterFog (brume
  lointaine, 0 à 15 km) et AdhocFog (verdâtre, 0 à 300 m) pas encore appliqués.
- Ciel de l'éditeur : diffusion simple de Rayleigh et de Mie le long du rayon (16 pas, 6 vers le soleil), × la part de
  diffusion multiple (éclairement des tables / éclairement en diffusion simple, par canal, pour ce soleil), × l'intensité
  du ciel de la palette ; disque du soleil ; sol vu de loin.
- **ShadowPrePassParam** : par scène avec « $parent » (KokiriForest → MainField) ; MainField : ombres dynamiques fondues
  de 80 à 200 m (DynamicShadowFarFadeStart / End), PCF de largeur 2, FaceNormalBias 20. Appliqué : le fondu.
- **ProjectorParam** : Kokiri a « leaves_shadow » (projection d'ombres de feuillage sur la forêt, depuis (0 ; 903,2 ;
  501,8) vers (0 ; 100 ; −54,8), 18,1°) et le champ « cloud_proj » (ombres des nuages) ; leur texture n'est pas nommée
  ici — pas encore appliqués.
- **DirectionalLightParam/dir_main** : direction (−0,398 ; −0,618 ; −0,678), couleur (1,30 ; 0,95 ; 0,65), intensité
  41,3 : soleil par défaut, remplacé en jeu par la palette.

## TerrainArc (.tscb)

- Les 25 scènes de terrain envoyées se lisent toutes avec le lecteur de l'éditeur : en-tête « TSCB », version 0x10D,
  taille du monde à 0x10 (500 m ; 250 pour Death Mountain Crater, Dodongo, Fire Temple, Goron City, Ice Cavern ; 125
  pour Zora's Domain), échelle des hauteurs à 0x14 (800 m), 90 matières (même table partout : couche, 3 flottants nuls,
  répétitions u et v par mètre).
- `KokiriForest_01_sub` : variante complète du terrain de la forêt (dossier reçu : 102 .hght, 102 .mate, 42 .extm) —
  toutes ses tuiles existent dans le principal aux mêmes places ; aucune n'a de trou ; hauteurs de 100 à 138 m au centre
  (le sol des cartes de la forêt, vers y = 100–120) et 100 m plats sur les bords. Elle s'accompagne d'éclairages
  précalculés « child » (voir ci-dessous). L'éditeur l'utilise à
  la place du principal pour les cartes « Child » (case « _sub (enfant) »).

## Éclairages précalculés du terrain (.extm)

- `<tuile>.<child|adult>.bake_ao.extm` et `.bake_gi.extm`, seulement pour les tuiles grossières (Kokiri : niveaux 0 à 2,
  soit 21 tuiles). TARB ; en-tête des données : nombre de textures à 0x03 (AO 1, GI 6), code de format ensuite (06, 09),
  taille 256 à 0x0C ; données à 0x34.
- **AO** : une texture BC5 de 65 × 65 blocs (260 × 260 texels), rangée à la Switch avec 17 GOB de large et des blocs de
  8 GOB (2 rangées de blocs : 139 264 octets) ; canal R = occlusion (blanc = dégagé), canal G presque partout à 1. La
  tuile occupe à peu près les texels 2 à 258 ; les dix premières rangées et les colonnes au-delà de 250 portent des
  motifs en tirets (bordure) : l'éditeur ne lit que l'intérieur. Appliqué : multiplie l'ambiance (sondes) du terrain,
  tuile précalculée la plus fine qui couvre chaque sommet, interpolation bilinéaire.
- **GI** : six textures de même taille et même rangement, en BC6H (blocs de 16 octets aux modes 1, 11, 12, 13, 14 ;
  ex. bloc constant de mode 13 ≈ 0,00023) — sans doute un cube d'ambiance (±X, ±Y, ±Z). Pas encore appliqué : il faut
  le décodage BC6H et l'échelle par rapport aux sondes.
