// Spatial3D — sources virtuelles lues dans le fichier
// Chaque source virtuelle = une direction + une combinaison linéaire des canaux d'entrée.
//  - Fichier multicanal : 1 source par canal, placée à la position nominale du canal.
//  - Fichier Ambisonique (AmbiX, ACN/SN3D, ordre 1 ou 2) : décodage vers 12 directions
//    réparties uniformément (icosaèdre), poids max-rE — base du décodage AllRAD.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "layout.h"

namespace s3d {

struct VirtualSource {
    std::string label;
    double az = 0, el = 0;
    double dist = kDefaultSpeakerDist;   // casque : distance du haut-parleur virtuel (m)
    bool lfe = false;
    std::vector<float> w;   // poids par canal d'entrée
};

std::vector<VirtualSource> sourcesFromChannels(const std::vector<int>& channelIds);
std::vector<VirtualSource> sourcesFromAmbisonics(int inputChannels, int order);
int ambisonicOrderForChannels(int ch);

// Casque : positions des haut-parleurs virtuels réglées à la souris (clé kVirtualSpeakersKey).
// src[k] correspond au canal ids[k] (sources issues de sourcesFromChannels).
void applyVirtualSpeakerOverrides(std::vector<VirtualSource>& src, const std::vector<int>& ids, const std::string& spec);   // 4 -> 1, 9 -> 2, 16 -> 3 ... ; -1 si impossible

} // namespace s3d
