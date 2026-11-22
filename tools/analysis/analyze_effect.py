#!/usr/bin/env python3
"""
Analyseur d'effets pour TriforceEditeur

Analyse les fichiers .vsetb.byml.zs et affiche leur contenu structuré.

Utilisation:
    python analyze_effect.py <chemin_vers_fichier.zs>
    python analyze_effect.py <chemin_vers_dossier>  # Analyse tous les fichiers .zs

Note: Les fichiers .vsetb.byml.zs utilisent des dictionnaires zstd personnalisés.
      Pour décompresser, il faut les dictionnaires correspondants.
"""

import sys
import os
import struct
from pathlib import Path


def find_dictionary(file_path):
    """Tente de trouver le dictionnaire zstd pour un fichier."""
    file_name = os.path.basename(file_path).lower()
    
    # Mapping des fichiers vers leurs dictionnaires
    dict_mapping = {
        '.byml.zs': 'bcett.byml.zsdic',  # id 3
        '.zs': 'zs.zsdic',                 # id 1
        '.pack.zs': 'pack.zsdic',          # id 4
        '.bkprb': 'bkprb.zsdic',           # id 2
    }
    
    for ext, dict_name in dict_mapping.items():
        if file_name.endswith(ext):
            return dict_name
    
    return None


def read_zstd_frame_header(data):
    """Lit l'en-tête d'une frame zstd et retourne les métadonnées."""
    if len(data) < 8:
        return None
    
    # Magic number (4 bytes)
    magic = struct.unpack('<I', data[0:4])[0]
    if magic != 0xFD2FB528:
        return None
    
    # Frame info byte
    frame_info = data[4]
    frame_type = (frame_info >> 6) & 0x3
    window_desc = (frame_info >> 1) & 0x7
    single_segment = (frame_info >> 5) & 0x1
    content_checksum = (frame_info >> 2) & 0x1
    reserved = (frame_info >> 3) & 0x3
    dict_present = (frame_info >> 4) & 0x1
    
    result = {
        'magic': magic,
        'frame_type': frame_type,
        'window_desc': window_desc,
        'single_segment': single_segment,
        'content_checksum': content_checksum,
        'dict_present': dict_present,
    }
    
    # Dictionary ID (si présent)
    if dict_present:
        dict_id_size = (frame_info >> 4) & 0x3
        if dict_id_size == 1:
            result['dict_id'] = data[5] & 0xFF
        elif dict_id_size == 2:
            result['dict_id'] = struct.unpack('<H', data[5:7])[0] & 0xFFFF
        elif dict_id_size == 3:
            result['dict_id'] = struct.unpack('<I', data[5:9])[0] & 0xFFFFFF
    
    return result


def analyze_file(file_path):
    """Analyse un fichier .zs et affiche ses métadonnées."""
    file_path = Path(file_path)
    
    if not file_path.exists():
        print(f"Erreur: fichier introuvable: {file_path}")
        return
    
    with open(file_path, 'rb') as f:
        data = f.read()
    
    print(f"\n{'='*60}")
    print(f"Fichier: {file_path.name}")
    print(f"Taille: {len(data):,} bytes")
    print(f"{'='*60}")
    
    # Lire l'en-tête zstd
    header = read_zstd_frame_header(data)
    if header:
        print(f"\n[En-tête zstd]")
        print(f"  Magic: 0x{header['magic']:08X}")
        print(f"  Type de frame: {header['frame_type']} (2=unframed, 3=frame)")
        print(f"  Dictionnaire: {'Oui' if header['dict_present'] else 'Non'}")
        if 'dict_id' in header:
            print(f"  ID du dictionnaire: {header['dict_id']} (0x{header['dict_id']:X})")
        print(f"  Somme de contrôle contenu: {'Oui' if header['content_checksum'] else 'Non'}")
    else:
        print("\n[En-tête zstd] Non reconnu")
    
    # Afficher les premiers octets en hexadécimal
    print(f"\n[Premiers 64 octets (hex)]")
    for i in range(0, min(64, len(data)), 16):
        hex_part = ' '.join(f'{b:02X}' for b in data[i:i+16])
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
        print(f"  {i:04X}: {hex_part:<48} {ascii_part}")
    
    # Tenter de détecter le type de fichier
    print(f"\n[Type détecté]")
    if data[:4] == b'BFFH':
        print("  Format: BFFH (binaire VFX3)")
    elif data[:4] == b'BYML':
        print("  Format: BYML (Binary YAML)")
    elif data[:2] == b'\x1F\x\x8B':
        print("  Format: gzip")
    else:
        # Vérifier si c'est du texte lisible
        try:
            text = data.decode('utf-8', errors='strict')
            print(f"  Format: Texte UTF-8 ({len(text)} chars)")
        except:
            try:
                text = data.decode('utf-16', errors='strict')
                print(f"  Format: Texte UTF-16 ({len(text)} chars)")
            except:
                print("  Format: Binaire (non reconnu)")


def analyze_directory(dir_path):
    """Analyse tous les fichiers .zs dans un dossier."""
    dir_path = Path(dir_path)
    
    if not dir_path.is_dir():
        print(f"Erreur: {dir_path} n'est pas un dossier")
        return
    
    zs_files = list(dir_path.glob('*.zs'))
    print(f"Dossier: {dir_path}")
    print(f"Fichiers .zs trouvés: {len(zs_files)}")
    
    for f in sorted(zs_files)[:20]:  # Limiter à 20 fichiers
        analyze_file(f)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    path = Path(sys.argv[1])
    
    if path.is_dir():
        analyze_directory(path)
    elif path.is_file():
        analyze_file(path)
    else:
        print(f"Erreur: {path} n'existe pas")
        sys.exit(1)


if __name__ == '__main__':
    main()
