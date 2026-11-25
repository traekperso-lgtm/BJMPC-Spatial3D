// Spatial3D — HRTF mesurée intégrée à l'exécutable : tête artificielle MIT KEMAR
// (Gardner & Martin, MIT Media Lab, 1994), égalisée en champ diffus, 710 directions, 48 kHz.
// Sert au casque quand aucun fichier .sofa n'est choisi : un vrai rendu binaural sans dépendance.
#pragma once
#include <vector>

namespace s3d {

// Réponses gauche/droite de la direction mesurée la plus proche (az vers la gauche, el vers le haut),
// rééchantillonnées à `fs`. Renvoie false si les données sont absentes ou illisibles.
bool builtinHrir(double azDeg, double elDeg, double fs, std::vector<float>& left, std::vector<float>& right,
                 double* gotAz = nullptr, double* gotEl = nullptr);
int builtinHrirCount();

// Rééchantillonnage d'une réponse impulsionnelle courte (sinc fenêtrée), gain d'énergie conservé
std::vector<float> resampleIr(const std::vector<float>& x, double fromHz, double toHz);

} // namespace s3d
