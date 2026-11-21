// Spatial3D — briques DSP : réverbération FDN, limiteur
#pragma once
#include <vector>
#include <cstddef>
#include <utility>

namespace s3d {

// Petite pièce (FDN 8 lignes, matrice de Hadamard) pour externaliser le rendu casque.
class RoomReverb {
public:
    void init(double fs, double rt60 = 0.35, double predelayMs = 12);
    // entrée mono, sortie stéréo décorrélée (ajoutée à outL/outR)
    void process(const float* in, float* outLR, size_t frames, float wet);
private:
    static constexpr int N = 8;
    std::vector<float> line_[N];
    size_t pos_[N] = {};
    float fb_[N] = {};
    float lp_[N] = {};
    float damp_ = 0.3f;
    std::vector<float> pre_;
    size_t prePos_ = 0;
};

// Désactive les nombres dénormaux (très petits) dans le fil courant : sans cela, la queue d'une
// réverbération ou d'un filtre qui s'éteint peut ralentir le processeur d'un facteur 10 à 100.
void enableFlushToZero();

// Limiteur crête lié sur tous les canaux, à anticipation (2 ms) : le gain descend en douceur AVANT
// la crête (filtre min glissant + moyenne glissante), sans jamais la laisser dépasser le seuil,
// puis remonte lentement. Ajoute une latence de 1 ms.
class Limiter {
public:
    void init(double fs, int channels, float thresholdLin = 0.97f, double releaseMs = 120, double lookaheadMs = 1.5);
    void process(float* interleaved, size_t frames);
    float reductionDb() const;
    int latencyFrames() const { return L_ - 1; }
private:
    int ch_ = 2, L_ = 1;
    float thr_ = 0.97f, env_ = 1.f, rel_ = 0.001f, minEnv_ = 1.f;
    std::vector<float> delay_;            // L_-1 trames
    size_t dpos_ = 0;
    std::vector<std::pair<long long, float>> minq_;   // file monotone (indice, gain requis)
    size_t minHead_ = 0;
    std::vector<float> avg_;
    size_t apos_ = 0;
    double avgSum_ = 0;
    long long n_ = 0;
    std::vector<float> frame_;
};

} // namespace s3d
