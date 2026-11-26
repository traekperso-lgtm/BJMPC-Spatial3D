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
    bool lowLatency = true;     // période minimale : IAudioClient3 en partagé, période mini en exclusif
};
// `bufferFrames` (facultatif) reçoit la taille du tampon de sortie obtenu.
// `modeInfo` (facultatif) reçoit le mode réellement obtenu, ex. « exclusif, 24 bits, 10.0 ms ».
// Si l'exclusif est refusé (périphérique occupé, format), repli automatique en partagé.
bool playWasapi(const std::wstring& id, int channels, uint32_t channelMask, int rate, const PlayOptions& opt,
                const PullFn& pull, std::atomic<bool>& stop, std::string& err, std::string* modeInfo = nullptr,
                int* bufferFrames = nullptr);

// Capture partagée au format de mixage du périphérique (float 32 bits)
class WasapiCapture {
public:
    WasapiCapture();
    ~WasapiCapture();
    bool open(const std::wstring& id, int bufferMs, std::string& err, bool lowLatency = true);
    double periodMs() const;     // période de capture obtenue
    std::string modeInfo() const;
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

// Volume et sourdine synchronisés entre CABLE Input (le périphérique par défaut, que touchent le
// curseur de la barre des tâches et les touches du clavier) et la sortie réelle (l'appareil maître).
// VB-Cable ignore volume et sourdine ; on les recopie donc sur la vraie carte, dans les deux sens :
// même position de curseur, même sourdine, et c'est la carte (ou le mélangeur de Windows) qui
// applique le volume, exactement comme sans Spatial3D. Seul cas où Spatial3D l'applique lui-même :
// sortie exclusive sur une carte sans volume matériel (Windows ne l'appliquerait pas).
// Suivi par notifications dans un fil dédié : aucun appel COM dans le fil audio, qui lit gain().
class EndpointVolumeWatch {
public:
    EndpointVolumeWatch();
    ~EndpointVolumeWatch();
    bool start(const std::wstring& cableId, const std::wstring& outputId, std::string& err);
    void stop();
    void setExclusive(bool on);  // sortie réellement ouverte en exclusif ?
    float gain() const;          // gain numérique à appliquer par Spatial3D (1 sauf cas exclusif)
    bool muted() const;          // état de l'appareil maître
    float levelDb() const;
    float levelPercent() const;  // position du curseur Windows (0..100)
    bool digital() const;        // volume appliqué par Spatial3D (exclusif sans volume matériel)
private:
    struct Impl;
    Impl* d_;
};

// Stabilité : priorité « au-dessus de la normale » et exclusion de l'économie d'énergie de Windows 11
// (EcoQoS bride les processus en arrière-plan ou réduits : source classique de craquements).
void prepareProcessForAudio();

// Réglages système (API IPolicyConfig, utilisée par le Panneau de configuration Son)
bool setDefaultEndpoint(const std::wstring& id, std::string& err);
bool setEndpoint71(const std::wstring& id, bool capture, int rate, std::string& err);

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

} // namespace s3d
