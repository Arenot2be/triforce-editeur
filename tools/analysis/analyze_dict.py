#!/usr/bin/env python3
"""Analyse le dictionnaire zstd du jeu Triforce."""

import struct
import sys
from pathlib import Path

DICT_PATH = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\moino\Games\Triforce\romfs\Pack\ZsDic.pack.zs'

def analyze_file(path):
    with open(path, 'rb') as f:
        data = f.read()
    
    size = len(data)
    print(f"=== Analyse de {path} ===")
    print(f"Taille: {size:,} bytes ({size/1024:.1f} Ko)")
    print()
    
    # Signature
    sig = data[:8]
    print(f"Signature (8 premiers octets): {sig.hex()}")
    print(f"  ASCII: {''.join(chr(b) if 32 <= b < 127 else '.' for b in sig)}")
    print()
    
    # Vérifier les types de fichiers connus
    checks = [
        (b'zstd', 'Frame zstd'),
        (b'\x28\xB5\x2F\xFD', 'Magic zstd frame'),
        (b'BFFH', 'En-tête Nintendo BFFH'),
        (b'PK', 'Archive ZIP/ZIP-like'),
        (b'\x1F\x9D', 'Gzip legacy'),
        (b'\x50\x4B', 'PK (ZIP)'),
    ]
    
    for pattern, name in checks:
        idx = data.find(pattern)
        if idx >= 0:
            print(f"✓ Signature '{name}' trouvée à offset {idx}")
        else:
            # Vérifier dans les premiers 16 octets
            if pattern[:2] in data[:16]:
                print(f"  ~ Signature '{name}' partiellement présente")
    
    print()
    
    # Analyser la structure
    print("--- Analyse structurelle ---")
    
    # Chercher des blocs de données
    i = 0
    block_num = 0
    while i < min(size, 1024):  # Premier Mo
        # Chercher des marqueurs
        if data[i:i+4] == b'\x28\xB5\x2F\xFD':
            print(f"  Bloc zstd frame #{block_num} à offset {i}")
            # Lire la taille du frame
            if i + 4 < size:
                frame_size = struct.unpack_from('<I', data, i + 4)[0]
                print(f"    Taille frame: {frame_size:,} bytes")
                block_num += 1
                i += 8
                continue
        elif data[i:i+4] == b'BFFH':
            print(f"  Bloc Nintendo BFFH à offset {i}")
            if i + 8 < size:
                block_size = struct.unpack_from('<I', data, i + 8)[0]
                print(f"    Taille bloc: {block_size:,} bytes")
            block_num += 1
            i += 12
            continue
        
        # Chercher des chaînes ASCII lisibles
        if all(32 <= b < 127 for b in data[i:i+4]) and i + 4 < size:
            # Vérifier si c'est un nom de fichier ou une chaîne significative
            end = i
            while end < min(size, i + 256) and 32 <= data[end] < 127:
                end += 1
            if end - i >= 8:  # Chaîne de 8+ caractères
                text = data[i:end].decode('ascii', errors='ignore')
                if any(kw in text.lower() for kw in ['dict', 'dic', 'zstd', 'pack', 'byml', 'vfx']):
                    print(f"  Chaîne significative à offset {i}: '{text}'")
        
        i += 1
    
    print()
    
    # Statistiques
    print("--- Statistiques ---")
    unique_bytes = len(set(data))
    print(f"Octets uniques: {unique_bytes}/256 ({unique_bytes/256*100:.1f}%)")
    
    # Distribution des octets
    from collections import Counter
    counter = Counter(data)
    print(f"\nTop 10 octets les plus fréquents:")
    for byte, count in counter.most_common(10):
        pct = count / size * 100
        char = chr(byte) if 32 <= byte < 127 else '.'
        print(f"  0x{byte:02X} ({char}): {count:>8,} ({pct:5.1f}%)")
    
    # Vérifier si c'est un dictionnaire zstd
    # Les dictionnaires zstd ont une structure spécifique
    print("\n--- Vérification dictionnaire zstd ---")
    if size > 12:
        # Format du dictionnaire zstd:
        # - Magic (4 bytes): 0xFD2FB528
        # - Version (1 byte)
        # - ID (4 bytes, optionnel)
        # - Checksum (4 bytes)
        # - Dictionary content
        
        magic = struct.unpack_from('<I', data, 0)[0]
        if magic == 0xFD2FB528:
            print("✓ C'est un dictionnaire zstd valide!")
            version = data[4]
            print(f"  Version: {version}")
            dict_id = struct.unpack_from('<I', data, 5)[0]
            print(f"  ID: 0x{dict_id:08X}")
            checksum = struct.unpack_from('<I', data, 9)[0]
            print(f"  Checksum: 0x{checksum:08X}")
            content_size = size - 13
            print(f"  Contenu: {content_size:,} bytes")
        else:
            print("✗ Magic zstd non trouvé (attendu: FD2FB528)")
            print(f"  Trouvé: {magic:08X}")

if __name__ == '__main__':
    path = Path(DICT_PATH)
    if not path.exists():
        print(f"Erreur: fichier non trouvé: {path}")
        sys.exit(1)
    
    analyze_file(path)
