// Spatial3D — briques portables du mode temps réel
// Deux cartes son n'ont jamais exactement la même horloge (VB-Cable, la vraie carte), et une carte
// virtuelle livre le son par paquets irréguliers. On rééchantillonne donc en continu avec un rapport
// ajusté par un régulateur PI qui surveille le POINT BAS du tampon (et non sa moyenne, faussée par
// les paquets), et la marge de sécurité grandit automatiquement après une coupure.
#pragma once
#include <cstddef>
#include <atomic>
#include <vector>

namespace s3d {

// Tampon circulaire sans verrou, un producteur (capture) / un consommateur (rendu) :
// aucun des deux fils n'attend jamais l'autre, aucune allocation après init().
class SpscRing {
public:
    void init(size_t capacityFloats);
    size_t push(const float* x, size_t n);    // renvoie le nombre écrit (le surplus est perdu si plein)
    size_t pop(float* dst, size_t max);       // renvoie le nombre lu
    size_t available() const;
private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    std::atomic<size_t> head_{ 0 }, tail_{ 0 };   // head : écriture, tail : lecture
};

// Rééchantillonneur multicanal à rapport variable (interpolation d'Hermite)
class AdaptiveResampler {
public:
    void init(int channels, size_t capacityFrames = 192000);   // mémoire réservée d'avance
    void push(const std::vector<float>& interleaved);
    void push(const float* interleaved, size_t frames);
    double available() const;               // trames d'entrée non consommées
    // Produit `frames` trames ; `ratio` = trames d'entrée consommées par trame de sortie.
    size_t pull(float* out, size_t frames, double ratio);
    void dropOldest(double keepFrames);
    void reset();
private:
    int ch_ = 2;
    std::vector<float> buf_;
    double pos_ = 1.0;
};

// Régulateur du rapport de rééchantillonnage
class DriftController {
public:
    void init(double inRate, double outRate, double marginMs = 10, double maxMarginMs = 200);
    // À appeler après chaque bloc rendu, avec ce qui reste dans le tampon. Renvoie le rapport suivant.
    double update(double residualFrames, size_t outFrames);
    void onUnderrun();                        // coupure : +10 ms de marge
    double ratio() const { return nominal_ * (1.0 + adj_); }
    double ppm() const { return adj_ * 1e6; }
    double driftPpm() const { return integ_ * 1e6; }     // dérive estimée (partie intégrale)
    double marginFrames() const { return margin_; }
    double primeFrames() const { return margin_ * 2; } // niveau de reprise après une coupure
    double marginMs() const { return margin_ / inRate_ * 1000; }
    double lowMs() const { return lastLow_ / inRate_ * 1000; }
private:
    double nominal_ = 1, inRate_ = 48000, outRate_ = 48000;
    double margin_ = 480, maxMargin_ = 9600;
    double winLow_ = 1e18, winTime_ = 0, lastLow_ = 0;
    double integ_ = 0, adj_ = 0;
};

// Étage d'entrée complet du temps réel : tampon + rééchantillonnage + régulation, avec FONDUS
// (2 ms) à chaque discontinuité pour qu'aucune coupure ne produise de clic :
//  - tampon presque vide : on joue ce qui reste en fondu de sortie, puis silence, puis fondu d'entrée ;
//  - tampon qui déborde (pause, veille…) : fondu enchaîné entre l'ancienne et la nouvelle position.
class LiveInputStage {
public:
    void init(int channels, double inRate, double outRate, int marginMs);
    void feed(const std::vector<float>& interleaved) { rs_.push(interleaved); }
    void feed(const float* interleaved, size_t frames) { rs_.push(interleaved, frames); }
    // Remplit `frames` trames d'entrée ; sourceIdle = aucune donnée reçue récemment (pas une coupure)
    void produce(float* out, size_t frames, bool sourceIdle);
    const DriftController& controller() const { return dc_; }
    int underruns() const { return underruns_; }
    int drops() const { return drops_; }
    bool priming() const { return priming_; }
private:
    int ch_ = 2, F_ = 96;
    double inRate_ = 48000;
    AdaptiveResampler rs_;
    DriftController dc_;
    bool priming_ = true;
    int fadeIn_ = 0;
    int underruns_ = 0, drops_ = 0;
    std::vector<float> old_;
};

} // namespace s3d
