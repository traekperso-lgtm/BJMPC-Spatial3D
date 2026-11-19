// Spatial3D — Windows : périphériques audio, lecture et capture WASAPI
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace s3d {

struct Endpoint {
    std::wstring id;             // identifiant MMDevice (vide = périphérique par défaut)
    std::string name;            // nom convivial
    bool capture = false;
    bool isDefault = false;
    int mixChannels = 0;
    int mixRate = 0;
    uint32_t mixMask = 0;        // masque du format de mixage Windows
    uint32_t physicalMask = 0;   // « Configurer les haut-parleurs » (PKEY_AudioEndpoint_PhysicalSpeakers)
    int formFactor = -1;         // 1 = haut-parleurs, 3 = casque, 5 = micro-casque, 9 = HDMI…
    bool headphones = false;
    bool isVbCable() const;      // « CABLE Input / CABLE Output (VB-Audio Virtual Cable) »
    std::string formFactorName() const;
};

bool listEndpoints(bool capture, std::vector<Endpoint>& out, std::string& err);
bool getEndpoint(const std::wstring& id, bool capture, Endpoint& out, std::string& err);   // id vide = défaut
inline bool queryDefaultDevice(Endpoint& out, std::string& err) { return getEndpoint(L"", false, out, err); }

// Lecture : `pull` remplit `frames` trames entrelacées et renvoie le nombre produit (0 = fin)
using PullFn = std::function<size_t(float* dst, size_t frames)>;
struct PlayOptions {
    int bufferMs = 20;          // mode partagé
    bool exclusive = false;     // mode exclusif : la carte son pour nous seuls, sans mélangeur Windows
};
// `modeInfo` (facultatif) reçoit le mode réellement obtenu, ex. « exclusif, 24 bits, 10.0 ms ».
// Si l'exclusif est refusé (périphérique occupé, format), repli automatique en partagé.
bool playWasapi(const std::wstring& id, int channels, uint32_t channelMask, int rate, const PlayOptions& opt,
                const PullFn& pull, std::atomic<bool>& stop, std::string& err, std::string* modeInfo = nullptr);

// Capture partagée au format de mixage du périphérique (float 32 bits)
class WasapiCapture {
public:
    WasapiCapture();
    ~WasapiCapture();
    bool open(const std::wstring& id, int bufferMs, std::string& err);
    int channels() const;
    int rate() const;
    uint32_t mask() const;
    // Boucle bloquante (à lancer dans un fil) ; `sink` reçoit des trames float entrelacées
    // discontinuity = Windows signale une perte de données en amont (VB-Cable en retard)
    using SinkFn = std::function<void(const float* data, size_t frames, bool silent, bool discontinuity)>;
    bool run(const SinkFn& sink, std::atomic<bool>& stop, std::string& err);
private:
    struct Impl;
    Impl* d_;
};

// Réglages système (API IPolicyConfig, utilisée par le Panneau de configuration Son)
bool setDefaultEndpoint(const std::wstring& id, std::string& err);
bool setEndpoint71(const std::wstring& id, bool capture, int rate, std::string& err);

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

} // namespace s3d
