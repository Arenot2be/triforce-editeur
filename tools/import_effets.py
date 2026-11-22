#!/usr/bin/env python3
"""
Outil d'import des effets pour TriforceEditeur

Importe les fichiers d'effets (.vsetb.byml.zs) depuis un zip vers le dossier Effect/ du jeu.

Utilisation:
    python import_effets.py <chemin_vers_zip> [chemin_vers_romfs]

Exemple:
    python import_effets.py effets_carte.zip C:/Users/moino/Games/Triforce/romfs
"""

import sys
import os
import zipfile
import shutil
from pathlib import Path


def find_game_romfs(base_path=None):
    """Trouve le dossier romfs du jeu Triforce."""
    if base_path and os.path.isdir(base_path):
        return base_path
    
    # Chemins courants
    candidates = [
        r"C:\Users\moino\Games\Triforce\romfs",
        r"D:\Games\Triforce\romfs",
        os.path.expanduser("~\Games\Triforce\romfs"),
    ]
    
    for path in candidates:
        if os.path.isdir(path):
            return path
    
    return None


def import_effects(zip_path, romfs_path=None):
    """Importe les effets depuis le zip vers le dossier Effect/ du jeu."""
    zip_path = Path(zip_path)
    if not zip_path.exists():
        print(f"Erreur: fichier zip introuvable: {zip_path}")
        return False
    
    # Trouver le dossier romfs
    if romfs_path is None:
        romfs_path = find_game_romfs()
    
    if romfs_path is None:
        print("Erreur: dossier romfs introuvable.")
        print("Chemin(s) cherché(s):")
        for path in [
            r"C:\Users\moino\Games\Triforce\romfs",
            r"D:\Games\Triforce\romfs",
        ]:
            print(f"  - {path}")
        print("\nUtilisation:")
        print(f"  python import_effets.py {zip_path} <chemin_vers_romfs>")
        return False
    
    romfs_path = Path(romfs_path)
    effect_dir = romfs_path / "Effect"
    
    # Créer le dossier Effect/ s'il n'existe pas
    effect_dir.mkdir(parents=True, exist_ok=True)
    print(f"Dossier de destination: {effect_dir}")
    
    # Compter les fichiers
    with zipfile.ZipFile(zip_path, 'r') as z:
        files = z.namelist()
        eftloc_files = [f for f in files if f.startswith('EftLoc')]
        cmntex_files = [f for f in files if f.startswith('CmnTex')]
        
        print(f"\nFichiers dans le zip:")
        print(f"  - EftLoc_*: {len(eftloc_files)} effets de localisation")
        print(f"  - CmnTex_*: {len(cmntex_files)} textures communes")
        print(f"  - Total: {len(files)} fichiers")
        
        # Extraire les fichiers
        print(f"\nExtraction en cours...")
        extracted = 0
        errors = 0
        
        for file_info in z.infolist():
            if not file_info.filename.startswith(('EftLoc', 'CmnTex')):
                continue
            
            dest = effect_dir / file_info.filename
            dest.parent.mkdir(parents=True, exist_ok=True)
            
            try:
                with z.open(file_info) as src, open(dest, 'wb') as dst:
                    shutil.copyfileobj(src, dst)
                extracted += 1
            except Exception as e:
                print(f"  Erreur avec {file_info.filename}: {e}")
                errors += 1
        
        print(f"\nRésultat:")
        print(f"  - Extraits: {extracted}")
        if errors > 0:
            print(f"  - Erreurs: {errors}")
    
    # Afficher un résumé des effets importés
    print(f"\n=== Résumé des effets importés ===")
    with zipfile.ZipFile(zip_path, 'r') as z:
        eftloc_files = sorted([f for f in z.namelist() if f.startswith('EftLoc')])
        
        # Classifier par type
        types = {}
        for f in eftloc_files:
            # Extraire le type depuis le nom (ex: EftLoc_Animal_Fish_Migration_00 -> Animal, Fish)
            parts = f.split('_')
            if len(parts) > 2:
                category = parts[1]
                types[category] = types.get(category, 0) + 1
        
        print(f"Catégories d'effets:")
        for cat, count in sorted(types.items(), key=lambda x: -x[1]):
            print(f"  {cat}: {count}")
    
    print(f"\n[OK] Import termine!")
    print(f"Les effets sont disponibles dans: {effect_dir}")
    print(f"\nPour les utiliser dans l'éditeur:")
    print(f"  1. Ouvrir TriforceEditeur.exe")
    print(f"  2. Charger une carte avec les acteurs EftLoc_*")
    print(f"  3. Les effets se chargeront automatiquement")
    
    return True


def analyze_effects(zip_path):
    """Analyse les effets dans le zip sans les extraire."""
    zip_path = Path(zip_path)
    if not zip_path.exists():
        print(f"Erreur: fichier zip introuvable: {zip_path}")
        return
    
    with zipfile.ZipFile(zip_path, 'r') as z:
        files = z.namelist()
        
        print(f"=== Analyse du zip: {zip_path.name} ===")
        print(f"Total fichiers: {len(files)}")
        
        # Classifier par préfixe
        prefixes = {}
        for f in files:
            prefix = f.split('_')[0] if '_' in f else f
            prefixes[prefix] = prefixes.get(prefix, 0) + 1
        
        print(f"\nClassification par type:")
        for prefix, count in sorted(prefixes.items(), key=lambda x: -x[1]):
            print(f"  {prefix}: {count}")
        
        # Statistiques de taille
        sizes = [z.getinfo(f).file_size for f in files]
        print(f"\nTailles:")
        print(f"  Min: {min(sizes):,} bytes")
        print(f"  Max: {max(sizes):,} bytes")
        print(f"  Moy: {sum(sizes)//len(sizes):,} bytes")
        print(f"  Total: {sum(sizes):,} bytes ({sum(sizes)/1024/1024:.1f} MB)")
        
        # Afficher les premiers fichiers de chaque type
        print(f"\nExemples par type:")
        for prefix in ['EftLoc', 'CmnTex']:
            sample = [f for f in files if f.startswith(prefix)][:5]
            if sample:
                print(f"  {prefix}:")
                for f in sample:
                    info = z.getinfo(f)
                    print(f"    {f} ({info.file_size:,} bytes)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    zip_path = sys.argv[1]
    romfs_path = sys.argv[2] if len(sys.argv) > 2 else None
    
    # Mode analyse si l'argument commence par '--analyze' ou '-a'
    if zip_path in ('--analyze', '-a'):
        if len(sys.argv) < 3:
            print("Utilisation: python import_effets.py --analyze <chemin_vers_zip>")
            sys.exit(1)
        analyze_effects(sys.argv[2])
    else:
        import_effects(zip_path, romfs_path)


if __name__ == '__main__':
    main()
