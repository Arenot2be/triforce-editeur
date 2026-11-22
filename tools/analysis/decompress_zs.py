#!/usr/bin/env python3
"""
Outil de décompression des fichiers .zs de Tears of the Kingdom.
Réplique la fonctionnalité de ZsDicSharp (BladesawStudios/RSTBTool).

Les dictionnaires zstd de TOTK ont un format standard avec:
- Magic: 0xEC30A437 (4 bytes, little-endian)
- Dictionary ID: 4 bytes (little-endian)
- CRC32: 4 bytes
- Contenu du dictionnaire

Chaque fichier .zs peut contenir un ID de dictionnaire dans son en-tête frame.
Si l'ID ne correspond à aucun dictionnaire chargé, le code utilise le nom du fichier
pour déterminer le dictionnaire approprié (fallback).
"""

import struct
import sys
import os
from pathlib import Path
import zstandard as zstd


class ZsDictionary:
    """Énumération des dictionnaires zstd de TOTK."""
    Unset = 0
    Common = 1  # zs.zsdic - pour tout le reste
    Pack = 2    # pack.zsdic - pour les fichiers .pack.zs
    BcettByml = 3  # bcett.byml.zsdic


class ZsDic:
    """Charge et gère les dictionnaires zstd de Tears of the Kingdom."""
    
    DICTIONARY_MAGIC = 0xEC30A437
    
    def __init__(self):
        self._dictionaries = {
            ZsDictionary.Common: None,
            ZsDictionary.Pack: None,
            ZsDictionary.BcettByml: None,
        }
        self._by_id = {}  # dict_id -> dict_data (pour correspondance par ID)
        self._extra = {}  # nom sans .zsdic -> dict_data (dictionnaires supplémentaires)
        self._decompressors = {}  # (dict_type, dict_data) -> decompressor
    
    @classmethod
    def from_pack_file(cls, pack_path: str):
        """Charge les dictionnaires depuis un fichier ZsDic.pack (SARC décompressé)."""
        dic = cls()
        
        with open(pack_path, 'rb') as f:
            pack_data = f.read()
        
        # Parser le SARC pour extraire les dictionnaires
        sarc_files = cls._parse_sarc(pack_data)
        
        for name, data in sarc_files:
            # Vérifier si c'est un dictionnaire zstd valide
            if len(data) >= 8:
                magic = struct.unpack_from('<I', data, 0)[0]
                dict_id = struct.unpack_from('<I', data, 4)[0]
                
                if magic == cls.DICTIONARY_MAGIC and dict_id != 0:
                    dic._by_id[dict_id] = data
            
            # Associer au type de dictionnaire approprié
            if name == 'zs.zsdic':
                dic._dictionaries[ZsDictionary.Common] = data
            elif name == 'pack.zsdic':
                dic._dictionaries[ZsDictionary.Pack] = data
            elif name == 'bcett.byml.zsdic':
                dic._dictionaries[ZsDictionary.BcettByml] = data
            elif name.lower().endswith('.zsdic'):
                extra_name = name[:-len('.zsdic')]
                dic._extra[extra_name] = data
        
        return dic
    
    @staticmethod
    def _parse_sarc(sarc_data: bytes):
        """Parse un fichier SARC et retourne une liste de (nom, données)."""
        if sarc_data[:4] != b'SARC':
            raise ValueError(f"Magic SARC invalide: {sarc_data[:4]}")
        
        # Format SARC (nbsi/sarc):
        # Magic(4) + FileSize(u16,2) + Version(u32,4) + DataOffset(u32,8) + FileSize2(u32,12)
        # Total header = 20 bytes
        # FileSize u16@2 = size of header in u32 units (so actual header size = value * 4)
        # Mais en pratique, le header fait toujours 20 bytes pour les fichiers de dictionnaires
        
        # SFAT est toujours après le header
        header_size = 20  # Header SARC standard = 20 bytes
        sfat_offset = header_size
        
        if sarc_data[sfat_offset:sfat_offset+4] != b'SFAT':
            raise ValueError(f"Magic SFAT invalide à offset {sfat_offset}: {sarc_data[sfat_offset:sfat_offset+4]}")
        
        sfat_node_count = struct.unpack_from('<H', sarc_data, sfat_offset + 6)[0]
        node_offset = sfat_offset + 12  # SFAT header = 12 bytes
        
        # SFNT header
        sfnt_offset = node_offset + (sfat_node_count * 8)  # Chaque entry SFAT = 8 bytes
        if sarc_data[sfnt_offset:sfnt_offset+4] != b'SFNT':
            raise ValueError(f"Magic SFNT invalide à offset {sfnt_offset}: {sarc_data[sfnt_offset:sfnt_offset+4]}")
        
        sfnt_name_count = struct.unpack_from('<H', sarc_data, sfnt_offset + 6)[0]
        name_table_offset = struct.unpack_from('<I', sarc_data, sfnt_offset + 8)[0]
        
        # Data offset (dans le header SARC)
        data_offset = struct.unpack_from('<I', sarc_data, 12)[0]
        
        # Lire les noms
        names = []
        for i in range(sfnt_name_count):
            name_offset_idx = name_table_offset + (i * 4)
            name_rel_offset = struct.unpack_from('<I', sarc_data, name_offset_idx)[0]
            
            # Lire le nom (terminé par null)
            abs_offset = name_rel_offset * 4
            name_bytes = b''
            j = abs_offset
            while j < len(sarc_data) and sarc_data[j] != 0:
                name_bytes += bytes([sarc_data[j]])
                j += 1
            names.append(name_bytes.decode('utf-8'))
        
        # Lire les données pour chaque entry SFAT
        files = []
        for i in range(sfat_node_count):
            entry_offset = node_offset + (i * 8)
            data_begin = struct.unpack_from('<I', sarc_data, entry_offset)[0]
            data_end = struct.unpack_from('<I', sarc_data, entry_offset + 4)[0]
            
            file_data = sarc_data[data_offset + data_begin:data_offset + data_end]
            files.append((names[i] if i < len(names) else f'entry_{i}', file_data))
        
        return files
    
    @staticmethod
    def dictionary_for(path: str) -> int:
        """Détermine quel dictionnaire utiliser pour un fichier donné."""
        name = Path(path).name.lower()
        
        if name.endswith('zsdic.pack.zs'):
            return ZsDictionary.Unset
        elif name.endswith('.bcett.byml.zs'):
            return ZsDictionary.BcettByml
        elif name.endswith('.pack.zs'):
            return ZsDictionary.Pack
        elif name.endswith('.zs'):
            return ZsDictionary.Common
        else:
            return ZsDictionary.Unset
    
    @staticmethod
    def frame_dictionary_id(frame: bytes) -> int:
        """Lit l'ID du dictionnaire depuis l'en-tête d'un frame zstd."""
        if len(frame) < 5 or frame[0:4] != b'\x28\xB5\x2F\xFD':
            return 0
        
        descriptor = frame[4]
        # Bit 4 (0x10) détermine si la taille du contenu est présente
        at = 5 if (descriptor & 0x10) else 6
        
        field_size = descriptor & 3
        if field_size == 1 and len(frame) >= at + 1:
            return frame[at]
        elif field_size == 2 and len(frame) >= at + 2:
            return struct.unpack_from('<H', frame, at)[0]
        elif field_size == 3 and len(frame) >= at + 4:
            return struct.unpack_from('<I', frame, at)[0]
        
        return 0
    
    def decompress(self, data: bytes, path: str = '') -> bytes:
        """Décompresse des données zstd avec le dictionnaire approprié.
        
        Suit la même logique de fallback que ZsDicSharp (C#) :
        1. Essayer le dictionnaire indiqué par l'ID dans le frame zstd
        2. Essayer le dictionnaire primaire basé sur le nom du fichier
        3. Fallback vers Common si le primaire échoue
        4. Fallback vers Pack si c'est un .pack.zs
        5. Essayer TOUS les dictionnaires disponibles comme dernier recours
        """
        # Essayer de trouver le dictionnaire par ID dans le frame
        dict_id = self.frame_dictionary_id(data)
        if dict_id in self._by_id:
            try:
                return self._decompress_with_dict(data, self._by_id[dict_id])
            except Exception:
                pass  # Le frame ID pointe vers un dict qui ne fonctionne pas, continuer
        
        # Fallback: utiliser le nom du fichier pour déterminer le dictionnaire primaire
        primary = self.dictionary_for(path) if path else ZsDictionary.Common
        
        # Tenter le dictionnaire primaire
        tried = set()
        if primary != ZsDictionary.Unset:
            tried.add(primary)
            dict_data = self._dictionaries.get(primary)
            if dict_data is not None:
                try:
                    return self._decompress_with_dict(data, dict_data)
                except Exception:
                    pass
        
        # Fallback vers Common (si pas déjà essayé)
        if ZsDictionary.Common not in tried:
            common_data = self._dictionaries.get(ZsDictionary.Common)
            if common_data is not None:
                try:
                    return self._decompress_with_dict(data, common_data)
                except Exception:
                    pass
            tried.add(ZsDictionary.Common)
        
        # Fallback vers Pack (si pas déjà essayé et si c'est un .pack.zs)
        if ZsDictionary.Pack not in tried:
            if path and path.lower().endswith('.pack.zs'):
                pack_data = self._dictionaries.get(ZsDictionary.Pack)
                if pack_data is not None:
                    try:
                        return self._decompress_with_dict(data, pack_data)
                    except Exception:
                        pass
            tried.add(ZsDictionary.Pack)
        
        # DERNIER RECOURS: essayer TOUS les dictionnaires disponibles
        # Certains fichiers ont un frame ID incorrect ou manquant
        for dict_key, dict_data in self._dictionaries.items():
            if dict_key not in tried and dict_data is not None:
                try:
                    return self._decompress_with_dict(data, dict_data)
                except Exception:
                    pass
        
        # Essayer aussi les dictionnaires chargés par ID uniquement
        for dict_id_val, dict_data in self._by_id.items():
            try:
                return self._decompress_with_dict(data, dict_data)
            except Exception:
                pass
        
        raise ValueError(f"Impossible de décompresser avec les dictionnaires disponibles")
    
    def _decompress_with_dict(self, data: bytes, dict_data: bytes) -> bytes:
        """Décompresse avec un dictionnaire spécifique."""
        # Créer le compression dict (le fichier .zsdic complet est utilisé)
        cdict = zstd.ZstdCompressionDict(dict_data)
        dctx = zstd.ZstdDecompressor(dict_data=cdict)
        return dctx.decompress(data)
    
    def decompress_file(self, path: str, output_path: str = None) -> bytes:
        """Décompresse un fichier .zs et retourne les données décompressées."""
        with open(path, 'rb') as f:
            data = f.read()
        
        result = self.decompress(data, path)
        
        if output_path:
            with open(output_path, 'wb') as f:
                f.write(result)
        
        return result


def find_zs_files(romfs_path: str, pattern: str = '*.zs') -> list:
    """Trouve tous les fichiers .zs dans le romfs."""
    romfs = Path(romfs_path)
    return sorted(romfs.rglob(pattern))


def main():
    """Point d'entrée principal."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Décompresse les fichiers .zs de TOTK')
    parser.add_argument('input', help='Fichier ou dossier d\'entrée')
    parser.add_argument('-o', '--output', help='Dossier de sortie (par défaut: input.decompressed/)')
    parser.add_argument('--dicts', default=None, help='Chemin vers ZsDic.pack (SARC décompressé)')
    parser.add_argument('--list', action='store_true', help='Liste les fichiers sans décompresser')
    parser.add_argument('--verbose', '-v', action='store_true', help='Sortie verbose')
    
    args = parser.parse_args()
    
    # Charger les dictionnaires
    if args.dicts:
        zs_dic = ZsDic.from_pack_file(args.dicts)
    else:
        # Essayer de charger depuis le jeu (SARC décompressé)
        game_path = r'C:\Users\moino\Games\Triforce'
        dict_path = Path(game_path) / 'romfs' / 'Pack' / 'ZsDic.pack'
        if dict_path.exists():
            zs_dic = ZsDic.from_pack_file(str(dict_path))
            print(f"Dictionnaires chargés depuis: {dict_path}")
        else:
            # Utiliser les dictionnaires locaux directement
            local_dicts = Path('tools/analysis/zstd_dicts')
            if local_dicts.exists():
                zs_dic = ZsDic()
                for dict_file in sorted(local_dicts.glob('*.zsdic')):
                    dict_data = dict_file.read_bytes()
                    magic = struct.unpack_from('<I', dict_data, 0)[0]
                    dict_id = struct.unpack_from('<I', dict_data, 4)[0]
                    if magic == ZsDic.DICTIONARY_MAGIC and dict_id != 0:
                        zs_dic._by_id[dict_id] = dict_data
                        print(f"  Dict ID 0x{dict_id:08X} chargé depuis {dict_file.name}")
                    
                    name = dict_file.name
                    if name == 'zs.zsdic':
                        zs_dic._dictionaries[ZsDictionary.Common] = dict_data
                    elif name == 'pack.zsdic':
                        zs_dic._dictionaries[ZsDictionary.Pack] = dict_data
                    elif name == 'bcett.byml.zsdic':
                        zs_dic._dictionaries[ZsDictionary.BcettByml] = dict_data
                
                print(f"Dictionnaires chargés depuis: {local_dicts}")
            else:
                print("ERREUR: Aucun dictionnaire trouvé!")
                sys.exit(1)
    
    # Trouver les fichiers d'entrée
    input_path = Path(args.input)
    if input_path.is_file():
        files = [input_path]
    elif input_path.is_dir():
        files = find_zs_files(str(input_path))
    else:
        print(f"ERREUR: {args.input} n'existe pas")
        sys.exit(1)
    
    if not files:
        print("Aucun fichier .zs trouvé")
        sys.exit(0)
    
    # Déterminer le dossier de sortie
    if args.output:
        output_dir = Path(args.output)
    else:
        output_dir = Path(str(input_path) + '.decompressed')
    output_dir.mkdir(parents=True, exist_ok=True)
    
    if args.list:
        print(f"Fichiers trouvés ({len(files)}):")
        for f in files:
            size = f.stat().st_size
            dict_type = ZsDic.dictionary_for(str(f))
            dict_name = {
                ZsDictionary.Unset: 'None',
                ZsDictionary.Common: 'zs.zsdic',
                ZsDictionary.Pack: 'pack.zsdic',
                ZsDictionary.BcettByml: 'bcett.byml.zsdic',
            }.get(dict_type, 'Unknown')
            print(f"  {size:>10,} bytes [{dict_name:>15}] {f}")
        return
    
    # Décompresser les fichiers
    success = 0
    failed = 0
    
    for i, f in enumerate(files):
        rel_path = f.relative_to(input_path) if input_path != Path('.') else f
        out_path = output_dir / str(rel_path).replace('.zs', '')
        out_path.parent.mkdir(parents=True, exist_ok=True)  # Créer les sous-dossiers manquants
        
        try:
            result = zs_dic.decompress_file(str(f), str(out_path))
            ratio = len(result) / f.stat().st_size * 100
            print(f"[{i+1}/{len(files)}] OK: {f.name} ({f.stat().st_size:,} -> {len(result):,} bytes, {ratio:.1f}%)")
            success += 1
        except Exception as e:
            print(f"[{i+1}/{len(files)}] ERREUR: {f.name}: {e}")
            failed += 1
        
        if args.verbose:
            print(f"  -> {out_path}")
    
    print(f"\nRésultats: {success} réussi(s), {failed} échoué(s)")


if __name__ == '__main__':
    main()
