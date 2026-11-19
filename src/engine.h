// Spatial3D — moteurs pilotables (Windows) : temps réel et lecteur de fichiers.
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

// Sortie réelle à utiliser (id réglé, sinon mémorisé, sinon défaut si ce n'est pas VB-Cable)
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
        std::string error, warning, outputMode;
    };
    LiveEngine();
    ~LiveEngine();
    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;
    bool start(const Settings& s, std::string& err);
    void stop();                       // remet la vraie sortie par défaut
    bool running() const { return running_; }
    Status status() const;
    Scene scene() const;
    void setGainDb(double db) { gain_ = (float)std::pow(10.0, db / 20.0); }
    Meters meters;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::atomic<bool> running_{ false };
    std::atomic<float> gain_{ 1.f };
};

// ---------------------------------------------------------------------------------------------
class FilePlayer {
public:
    FilePlayer();
    ~FilePlayer();
    FilePlayer(const FilePlayer&) = delete;
    FilePlayer& operator=(const FilePlayer&) = delete;
    bool open(const std::string& pathUtf8, const Settings& s, std::string& err);   // ouvre et joue
    void close();
    void togglePause() { paused_ = !paused_; }
    bool paused() const { return paused_; }
    void seek(double seconds) { seekTo_ = seconds; }
    double position() const { return pos_; }
    double duration() const { return duration_; }
    bool active() const { return active_; }        // ouvert (en lecture ou en pause)
    bool finished() const { return finished_; }
    std::string info() const;
    std::string error() const;
    Scene scene() const;
    void setGainDb(double db) { gain_ = (float)std::pow(10.0, db / 20.0); }
    Meters meters;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::atomic<bool> paused_{ false }, active_{ false }, finished_{ false };
    std::atomic<double> seekTo_{ -1 }, pos_{ 0 }, duration_{ 0 };
    std::atomic<float> gain_{ 1.f };
};

// Utilitaires partagés avec la ligne de commande
int configureCable();
bool runElevated(const std::wstring& args);
std::wstring loadSavedOutput();
void saveOutput(const std::wstring& id);

} // namespace s3d
