# Triforce — éditeur de cartes (C++ / Vulkan)

Éditeur des cartes de la recompilation PC Triforce, avec une vue 3D en Vulkan. C'est la version native de
l'éditeur du modkit : mêmes fichiers de travail, même construction du mod.

## Installation

1. Télécharge `TriforceEditeur.zip` depuis la page **Releases** du dépôt (version « dernier »).
2. Décompresse-le où tu veux, puis lance `TriforceEditeur.exe`.
3. Au premier lancement, clique sur **Dossiers** si le romfs du jeu n'est pas à l'endroit habituel
   (`%USERPROFILE%\Games\Triforce\romfs`).

Il faut une carte graphique compatible Vulkan avec un pilote à jour (toutes les Radeon et GeForce récentes).

## Dossiers

- **romfs** : les fichiers du jeu. Ils ne sont jamais modifiés.
- **Dossier de travail** (`Documents\Triforce-modkit\travail`) : tes cartes modifiées, au format texte du modkit.
  Le modkit Python et cet éditeur lisent et écrivent les mêmes fichiers.
- **Mods** : là où « Construire le mod » écrit le mod (`mods\<nom>\romfs`), à côté du jeu.

Les réglages sont gardés dans `Documents\Triforce-modkit\editeur.ini`.

## Utilisation

- **À gauche** : les cartes (une `*` marque celles que tu as modifiées), les couches, les catégories d'objets
  (cases à cocher, couleur des boîtes) et la liste des objets (clic : sélectionner et centrer).
- **Vue 3D** :
  - clic gauche sur un objet : le sélectionner ; Ctrl+clic : en ajouter ; Maj+glisser : sélection rectangle ;
  - clic gauche glissé dans le vide : tourner autour du point visé ; clic droit glissé : se déplacer ; molette : zoomer ;
  - ZQSD ou flèches : avancer, glisser ; A / E : descendre, monter ; Maj : plus vite ;
  - « Vue de dessus » : vue orthographique, glisser pour se déplacer.
- **Poignées** : flèches rouge, verte, bleue pour déplacer le long de X, Y, Z ; carré central pour déplacer à
  l'horizontale ; anneau jaune pour tourner autour de la verticale ; avec « Incliner », anneaux rouge (X) et bleu (Z).
  Ctrl pendant le geste : par pas de 1 m ou de 15°. Plusieurs objets sélectionnés bougent et tournent ensemble.
- **À droite** : position, rotation (X, Y, Z en degrés, avec le cap et l'inclinaison), boutons Gauche 90° /
  Droite 90°, Redresser, Centrer, Dupliquer, Supprimer, Poser au sol, et toutes les propriétés de l'objet en texte
  (« Appliquer le texte » pour valider ; les types du fichier sont gardés).
- **Raccourcis** : Ctrl+Z / Ctrl+Y annuler / rétablir, Ctrl+S enregistrer, Ctrl+D dupliquer, Suppr supprimer,
  F centrer, G poser sur le terrain, R / Maj+R tourner de 90°, Échap désélectionner.

## Modèles, textures, terrain

Les modèles (BFRES) et textures (BNTX : BC1/2/3/7 lues par la carte graphique, ASTC décodées par le processeur)
sont chargés en arrière-plan. Les objets sans modèle sont des boîtes de la couleur de leur catégorie. Le terrain de
la zone est choisi d'après le nom de la carte ; la liste « Terrain » permet d'en prendre un autre ou aucun.
Le compteur « illisibles » ouvre la liste des textures qui n'ont pas pu être affichées, avec la raison.

Les modèles `.bfres.mc` (format « MeshCodec » : BFRES compressé, tampons de sommets et d'index encodés) sont
décodés avec le code de [dt-12345/MeshCodec](https://github.com/dt-12345/MeshCodec).

Les modèles dont la géométrie est en **meshlets** (environ 12 %, surtout falaises, rochers, clôtures et salles ; le
jeu les décode sur la carte graphique, dans ses mesh shaders) sont lus aussi : positions et triangles du niveau de
détail le plus fin, avec leurs coordonnées de texture (pour les variantes du format reconnues ; sinon en blanc
ombré).
Pour un modèle illisible, le panneau de sélection propose « Exporter ce modèle pour analyse », qui écrit le modèle
décompressé dans `Documents\Triforce-modkit\analyse` (pour ton usage : ne le partage pas publiquement).

## Compiler soi-même

```
cmake -S . -B build -A x64
cmake --build build --config Release --target TriforceEditeur
```

Les dépendances (GLFW, Dear ImGui, volk, Vulkan-Headers, zstd, MeshCodec) sont téléchargées par CMake. Les shaders sont déjà
compilés en SPIR-V dans `src/app/shaders_spv.hpp` (à refaire avec `tools/gen_shaders.py` après une modification de
`shaders/`). `te_tests` et `tests/compare.py` comparent les formats à ceux du modkit Python.

## Licences

Code de l'éditeur : écrit pour ce projet. Décodage ASTC HDR d'après l'encodeur de référence d'ARM (Apache 2.0).
Dépendances : GLFW (zlib), Dear ImGui (MIT), volk (MIT), Vulkan-Headers (Apache 2.0), zstd (BSD), police DroidSans
(Apache 2.0), MeshCodec de dt-12345 (GPL-2.0, en partie d'après meshoptimizer, MIT). Comme il contient MeshCodec,
l'exécutable est distribué sous GPL-2.0 ; `LICENSE-MeshCodec.txt` en donne le texte. Aucun fichier du jeu n'est inclus.
