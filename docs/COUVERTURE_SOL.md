# Couverture du sol (mousse, gazon, cailloux, sous-bois)

Ce que l'éditeur sait du rendu du jeu, lu dans les nuanceurs `Shader/Foliage.Product.110.product.Nin_Ounce_NVN2.bfsha`
(traduits avec l'outil `gpu/shadertool` de Triforce, puis SPIRV-Cross). Aucun fichier du jeu n'est dans ce dépôt.

## Fichiers

- `Foliage/Placement/Scene/<zone>/<scène>.<couche>.flnt.zs` : les points (cases de 4 m, 12 octets par point).
- `Pack/Foliage.pack.zs` : les sortes (`ResMossList`, `ResStoneList`, `ResTurfList`, `ResShootLeafUndergrowthList`),
  les couches des textures en tableau (`ResTexArray`), les teintes (`ColorScale`), les découpes des feuilles
  (`ResTexName` : `Poly1`, `Poly2`, `AverageColor`).
- `Model/Foliage.Foliage*.bfres` : de simples boîtes ; la forme est faite par le nuanceur.
- `Shader/Foliage...bfsha` : 14 modèles de nuanceur (moss, ornament_impostor, low_tree_shootleaf_impostor,
  low_tree_impostor, flower, stone, turf4, turf1, leaf, forest, stalk, blade, frost, leaf_near), chacun avec un
  nuanceur de tâche, des nuanceurs de maillage (mesh shaders) et des nuanceurs de pixels.

## Octets d'un point

| octet | rôle |
|---|---|
| 0-2 | position dans la case, 1/64 m |
| 3 | normale du sol : point i d'une spirale de Fibonacci, y = 1 − (2i + 1)/256, azimut i × 3,883 rad (angle d'or) |
| 4 | mousse : rayon de la touffe, R = (o4/510) / √s |
| 5 | sorte / fiche de feuille (`UboLeafTexInfo`, 32 octets par fiche) |
| 6 | mousse : ouverture du cône des brins, s = 0,043 + o6 × 0,003278 |
| 8 | mousse : taille des brins (0 vaut 255) |
| 9 | mousse : part du vent |
| 11 | gazon : sorte (`ResTurfList`) ; bits 27-28 du mot 8-11 : indicateurs |

## Mousse (nuanceur de maillage « moss »)

- Deux brins par point, chacun de 4 sommets (la découpe `Poly2` de la texture).
- Direction du brin k : cône autour de la normale, cos α = 1 − s/4, azimut k × angle d'or.
- Pied : P − n × 0,748 R + D × 0,748 R × (0,88 − o6 × 0,001286).
- Hauteur 0,115 m × o8/255, demi-largeur 0,0325 m × o8/255 ; la largeur suit la caméra autour de l'axe du brin,
  retournée au hasard.
- Le nuanceur de tâche écarte les points tournés vers l'arrière (selon le même cône) et éclaircit au loin au hasard.
- Couleur : texture de la couche (`Foliage_Moss_Alb`, couches données par `ResMoss`) sans teinte par point ;
  le nuanceur de pixels n'ajoute qu'un réglage de météo (humidité, saison).
