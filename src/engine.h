// Spatial3D — moteur temps réel pilotable (Windows).
// Utilisés par l'interface graphique et par la ligne de commande.
#pragma once
#include "model.h"
#include "wasapi_output.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace s3d {

static constexpr int kMaxMeters = 32;

// Fichier %LOCALAPPDATA%\Spatial3D\config.ini
std::wstring appDataDir();
Settings loadSettings();
void saveSettings(const Settings& s);
void setStartWithWindows(bool on, const std::wstring& args);

// Crêtes par canal depuis la dernière lecture (le fil audio écrit, l'interface lit)
struct Meters {
    std::atomic<float> in[kMaxMeters];
    std::atomic<float> out[kMaxMeters];
    Meters() { reset(); }
    void reset() { for (auto& v : in) v = 0.f; for (auto& v : out) v = 0.f; }
    static void feed(std::atomic<float>* m, const float* x, size_t frames, int ch);
};

// Sortie réelle à utiliser : Spatial3D ne choisit pas, il suit Windows. Ordre : id imposé (ligne de
// commande --vers), sinon la sortie par défaut de Windows si ce n'est pas VB-Cable, sinon la dernière
// vraie sortie mémorisée (pendant le son 3D, la sortie par défaut est VB-Cable).
bool resolveOutput(const Settings& s, Endpoint& out, std::string& err);
Layout layoutFor(const Settings& s, const Endpoint& e);
Layout layoutForEndpoint(const Endpoint& e);

// ---------------------------------------------------------------------------------------------
class LiveEngine {
public:
    struct Status {
        bool running = false, idle = true;
        double marginMs = 0, lowMs = 0, driftPpm = 0, inHz = 0;
        int underruns = 0, drops = 0, captureGlitches = 0;
        std::string error, warning, outputMode, captureMode;
        // latence bout en bout estimée : capture + marge + traitement + limiteur + tampon de sortie
        double capturePeriodMs = 0, processingMs = 0, outputBufferMs = 0, totalLatencyMs = 0;
        bool followVolume = false, windowsMuted = false;   // volume / sourdine Windows de CABLE Input
        double windowsVolumeDb = 0, windowsVolumePct = 100;
        bool volumeDigital = false;        // appliqué par Spatial3D (exclusif sans volume matériel)
        std::wstring outputId;             // vraie sortie utilisée
        std::wstring cableId;              // CABLE Input (sortie par défaut pendant le son 3D)
    };
    LiveEngine();
    ~LiveEngine();
    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;
    bool start(const Settings& s, std::string& err);
    // remet la vraie sortie par défaut (restoreDefault = false : l'utilisateur vient d'en choisir une
    // autre dans Windows, on ne l'écrase pas)
    void stop(bool restoreDefault = true);
    bool running() const { return running_; }
    Status status() const;
    Scene scene() const;
    // Déplace les enceintes en direct (même disposition, nouveaux azimuts) : nouvelle matrice
    // calculée ici, échangée par le fil audio avec un fondu enchaîné. Sans effet au casque.
    bool moveSpeakers(const Layout& out);
    // Casque : nouveaux azimuts / distances des haut-parleurs virtuels (même nombre de sources)
    bool moveVirtualSpeakers(const std::vector<VirtualSource>& src);
    void setGainDb(double db) { gain_ = (float)std::pow(10.0, db / 20.0); }
    Meters meters;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::atomic<bool> running_{ false };
    std::atomic<float> gain_{ 1.f };
};

// Utilitaires partagés avec la ligne de commande
int configureCable();
bool runElevated(const std::wstring& args);
std::wstring loadSavedOutput();
void saveOutput(const std::wstring& id);

} // namespace s3d
