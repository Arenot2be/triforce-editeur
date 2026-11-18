#pragma once
// Modèles .bfres.mc (« MeshCodec ») : un BFRES compressé par zstd suivi d'une section FMSH qui contient les tampons
// d'index et de sommets encodés. Le décodage des tampons vient de dt-12345/MeshCodec (GPL-2.0).
#include "core/util.hpp"

namespace te {

bool isMeshCodec(const Bytes& raw);   // (en-tête MCPK)

// Renvoie le BFRES décompressé, ses tampons décodés placés après lui comme le fait le jeu : à l'adresse
// Align(Align(taille, 8) + 0x120, alignement) ; le u32 à Align(taille, 8) donne ce décalage.
Bytes meshCodecUnpack(const Bytes& raw);

}  // namespace te
