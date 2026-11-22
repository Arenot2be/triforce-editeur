# Guide des Effets — TriforceEditeur

## Vue d'ensemble

L'éditeur supporte les effets de particules du jeu Triforce (Zelda: Majora's Mask HD). Les effets sont stockés dans le dossier `Effect/` du ROMFS sous forme de fichiers `.vsetb.byml.zs`.

## Structure des fichiers

### Fichiers d'effets (.vsetb.byml.zs)

Chaque fichier contient :
- **Esets** : Liste des ensembles de particules (ex: `_Move_00`, `_Loop_01`)
- **Textures** : Textures associées (masque et couleur)
- **PtclBin** : Binaire VFX3 contenant les émetteurs et modules

### Types d'effets

| Préfixe | Description | Quantité |
|---------|-------------|----------|
| `EftLoc_*` | Effets de localisation (environnement) | 335 |
| `CmnTex_*` | Textures communes (bougies, chandeliers) | 2 |

### Catégories principales

Les effets sont classés automatiquement par l'éditeur :

| Famille | Description | Exemples |
|---------|-------------|----------|
| Feuilles qui tombent | Feuilles, pétales, confettis | `Leaf`, `Petal` |
| Brume | Brouillard, nuages, volume | `Fog`, `Mist`, `Cloud` |
| Rides | Effets sur l'eau | `Ripple` |
| Embruns | Cascades, éclaboussures | `Waterfall`, `Splash` |
| Lueurs | Insectes, poussières lumineuses | `Insect`, `Firefly` |
| Rayons de lumière | Shaft de lumière | `LightShaft`, `Sunbeam` |
| Fumée | Brouillard au sol, vapeur | `Smoke`, `Steam` |
| Flammes | Feux, bougies, torches | `Fire`, `Candle`, `Torch` |
| Gouttes | Eau qui goutte | `Drip`, `Drop` |
| Bancs de poissons | Migration de poissons | `FishMigration` |
| Masque de brume | Retire la brume volumique | `RemoveDensity` |
| Abîme sombre | Zones sombres | `Abyss` |
| Bulles | Effets sous-marins | `Bubble`, `WaterFlow` |
| Débris rocheux | Pierres en suspension | `Debris`, `Erosion` |
| Aura | Chant de Saria | `SongOfSaria` |
| Aurore | Aurores boréales | `Aurora` |
| Tourbillon | Tourbillons d'eau | `Whirlpool` |

## Import des effets

### Méthode 1 : Script d'import automatique

```bash
python tools/import_effets.py effets_carte.zip C:/Users/moino/Games/Triforce/romfs
```

### Méthode 2 : Extraction manuelle

1. Extraire le contenu de `effets_carte.zip`
2. Copier les fichiers dans `C:/Users/moino/Games/Triforce/romfs/Effect/`
3. Les fichiers seront chargés automatiquement par l'éditeur

### Méthode 3 : Analyse sans extraction

```bash
python tools/import_effets.py --analyze effets_carte.zip
```

## Utilisation dans l'éditeur

### Chargement automatique

Les effets se chargent automatiquement lorsque :
1. L'éditeur charge une carte avec des acteurs `EftLoc_*`
2. Le fichier `.vsetb.byml.zs` correspondant existe dans `Effect/`
3. L'acteur a un `UserName` dans son `ELinkParam`

### Contrôle des effets

Dans l'interface de l'éditeur :
- **Case "Effets"** : Cocher/décocher pour afficher/masquer tous les effets
- **Liste des acteurs** : Cliquer sur un acteur `EftLoc_*` pour voir ses détails
- **Propriétés** : Afficher le nom, le type et les textures de l'effet

### Effets permanents vs temporaires

| Type | Description | Exemple |
|------|-------------|---------|
| Permanent | Toujours actif dans la zone | Nuages, brouillard, lueuses |
| Temporaire | Actif seulement pendant une action | Éclaboussures, poussière de course |

Détection automatique :
- Les acteurs `EftLoc_*` sont toujours permanents
- Pour les autres, l'éditeur vérifie si un ensemble tourne en continu (`_Move_00`, `_Loop_01`, etc.)

## Format des fichiers

### BYML (Binary YAML)

Les fichiers `.vsetb.byml.zs` sont compressés avec zstd et contiennent du BYML :

```yaml
root:
  Esets:
    - "EftLoc_Animal_Fish_Migration_00_Move_00"
    - "EftLoc_Animal_Fish_Migration_00_Loop_01"
  Textures:
    - name: "FishMigration_Texture_00_Fca"
    - name: "FishMigration_Texture_00_Fi"
  PtclBin: <binaire VFX3>
```

### Binaire VFX3 (PtclBin)

Format binaire commençant par `BFFH` :
- **Default** : Fiches de modules (émetteurs + noms)
- **StringPool** : Réserve de chaînes (noms des modules/émetteurs)

Chaque fiche pointe sur deux chaînes :
1. Nom du module (ex: `Smoke`, `Fire`, `Glow`)
2. Nom de l'émetteur (ex: `EmitSmoke`, `EmitFire`)

## Optimisations pour AMD RDNA 4

### Float16/Int8 natif

RDNA 4 supporte nativement les opérations float16 et int8 dans les shaders :
- Extension `VK_KHR_shader_float16_int8` activée
- Permet des calculs de particules plus précis avec moins de mémoire

### Robustness2

Extension `VK_KHR_robustness2` pour un accès mémoire sûr :
- `robustBufferAccess2` : Accès aux tampons même hors limites
- `robustImageAccess2` : Accès aux images même hors limites
- `nullDescriptor` : Support des descripteurs null

### Performance des particules

Avec RX 9070 XT (~8 Go VRAM) :
- Maximum de particules simultanées : ~120 000
- Les effets permanents sont pré-chargés
- Les effets temporaires sont déchargés quand inactifs

## Dépannage

### Effets non chargés

1. Vérifier que le fichier `.vsetb.byml.zs` existe dans `Effect/`
2. Vérifier que l'acteur a un `UserName` dans son `ELinkParam`
3. Vérifier les logs de l'éditeur pour les erreurs de chargement

### Effets mal affichés

1. Vérifier que les textures associées existent dans `Tex/`
2. Vérifier que le format de texture est compatible (B8G8R8A8, R5G6B5, etc.)
3. Vérifier que l'effet est de la bonne famille pour la zone

### Performances

1. Réduire le nombre d'effets actifs simultanément
2. Désactiver les effets permanents non nécessaires
3. Utiliser le mode "Effets décoché" pour tester sans particules

## Fichiers d'effet principaux

### Effets de localisation (EftLoc_*)

| Fichier | Type | Description |
|---------|------|-------------|
| `EftLoc_Animal_Fish_Migration_*` | Fish | Migration de poissons |
| `EftLoc_DeathMountain_Cloud*` | Fog | Nuages montagne du death |
| `EftLoc_DeathMountain_Volcano_*` | Smoke | Volcan montagne du death |
| `EftLoc_ZoraFountain_Fog_*` | Fog | Brouillard fontaine de zora |
| `EftLoc_CrumblingGanonCastle_Abyss_*` | Abyss | Abîme château de ganon |
| `EftLoc_BasicThunder_*` | Generic | Tonnerre basique |

### Textures communes (CmnTex_*)

| Fichier | Type | Description |
|---------|------|-------------|
| `CmnTex_Map_CityPropsSet_Candle_Candelabra_B_01` | Fire | Bougie chandelier B |
| `CmnTex_Map_CityPropsSet_Candle_Chandelier_A_01` | Fire | Bougie chandelier A |

## Ressources

- **Code source** : `src/core/effects.cpp`, `src/core/effects.hpp`
- **Tests** : `src/tests/tests.cpp` (ligne 111 : test de chargement d'effet)
- **Éditeur** : `src/app/editor.cpp` (lignes 675-680, 1087-1121)
- **UI** : `src/app/ui.cpp` (lignes 155-176, 526-539, 887-888)

## Mise à jour des effets

Pour mettre à jour les effets après modification du jeu :

```bash
# 1. Extraire les nouveaux effets du ROMFS
# (utiliser l'outil d'extraction du ROMFS avec les dictionnaires zstd)

# 2. Importer dans l'éditeur
python tools/import_effets.py nouveaux_effets.zip C:/Users/moino/Games/Triforce/romfs

# 3. Redémarrer l'éditeur
```

## Notes techniques

### Dictionnaires zstd

Les fichiers `.vsetb.byml.zs` utilisent des dictionnaires zstd personnalisés :
- `zs.zsdic` (id 1) : Pour les fichiers .zs génériques
- `bcett.byml.zsdic` (id 3) : Pour les fichiers BYML
- `pack.zsdic` (id 4) : Pour les fichiers .pack
- `bkprb.zsdic` (id 2) : Pour les fichiers .bkprb

### Détection GPU

L'éditeur détecte automatiquement la RX 9070 XT et active :
- Vulkan 1.3
- Float16/Int8 natif
- Robustness2
- Optimisations spécifiques RDNA 4

### Performance cible

Avec Ryzen 7 9800X3D + RX 9070 XT :
- FPS cible : 60+ avec tous les effets
- Chargement des effets : < 100ms par effet
- Mémoire VRAM : ~4 Go pour tous les effets permanents
