// Spatial3D — moteurs de rendu
//  - MatrixRenderer : haut-parleurs (stéréo, 5.1, 7.1, 7.1.4…) par VBAP, matrice de gains statique
//  - BinauralRenderer : casque, chaque source virtuelle filtrée par une HRTF
//        * modèle paramétrique de tête sphérique (Brown & Duda) par défaut : ITD, ombre de tête,
//          échos du pavillon (élévation), assombrissement arrière
//        * ou HRTF mesurée (fichier .sofa) si compilé avec libmysofa
#pragma once
#include "layout.h"
#include "sources.h"
#include <memory>
#include <string>
#include <vector>

namespace s3d {

struct RenderOptions {
    double spreadDeg = 0;       // élargissement des sources (haut-parleurs)
    double lfeToMains = 0.5;    // LFE redirigé vers les principales si pas de caisson (≈ -6 dB)
    double room = 0.15;         // dose de pièce virtuelle au casque (0 = sec)
    std::string sofaPath;       // HRTF mesurée (optionnelle)
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual int inChannels() const = 0;
    virtual int outChannels() const = 0;
    // in : frames x inChannels entrelacés ; out : frames x outChannels entrelacés (écrasé)
    virtual void process(const float* in, float* out, size_t frames) = 0;
    virtual std::string describe() const = 0;
};

std::unique_ptr<Renderer> makeRenderer(const std::vector<VirtualSource>& sources, int inChannels,
                                       const Layout& out, double sampleRate,
                                       const RenderOptions& opt, std::string& err);

// Exposé pour les tests : matrice sortie x entrée
std::vector<float> buildSpeakerMatrix(const std::vector<VirtualSource>& sources, int inChannels,
                                      const Layout& out, const RenderOptions& opt);

bool sofaSupported();

} // namespace s3d
