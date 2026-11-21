#include "live.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <thread>

namespace s3d {

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int listDevices() {
    std::string err;
    for (bool cap : { false, true }) {
        std::vector<Endpoint> v;
        if (!listEndpoints(cap, v, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
        printf("%s\n", cap ? "Entrees (capture) :" : "Sorties :");
        for (auto& e : v)
            printf("  %s %-50s %d can. %d Hz  %s%s\n", e.isDefault ? "*" : " ", e.name.c_str(), e.mixChannels, e.mixRate,
                   cap ? "" : e.formFactorName().c_str(), e.isVbCable() ? "  [VB-Cable]" : "");
    }
    printf("(* = par defaut)\n");
    return 0;
}

int restoreOutput() {
    std::string err;
    const std::wstring id = loadSavedOutput();
    if (id.empty()) { fprintf(stderr, "Aucune sortie memorisee\n"); return 1; }
    if (!setDefaultEndpoint(id, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
    Endpoint e; getEndpoint(id, false, e, err);
    printf("Sortie par defaut remise sur : %s\n", e.name.c_str());
    return 0;
}

int runLive(const LiveArgs& a, std::atomic<bool>& stop) {
    std::string err;
    Settings s = loadSettings();
    if (!a.outputName.empty()) {
        std::vector<Endpoint> outs;
        if (!listEndpoints(false, outs, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
        const Endpoint* real = nullptr;
        for (auto& e : outs)
            if (!e.isVbCable() && lower(e.name).find(lower(a.outputName)) != std::string::npos) { real = &e; break; }
        if (!real) { fprintf(stderr, "Erreur : aucune sortie ne contient '%s' (voir --peripheriques)\n", a.outputName.c_str()); return 1; }
        s.outputId = real->id;
        saveSettings(s);
    }
    s.layout = a.sortie; s.sofaPath = a.sofa; s.spreadDeg = a.spread; s.room = a.room; s.gainDb = a.gainDb;
    if (a.latencyMs != 6) s.marginMs = a.latencyMs;      // sinon : marge apprise mémorisée
    s.keepDefault = a.keepDefault; s.exclusive = s.exclusive || a.exclusive;
    if (a.standardLatency) s.lowLatency = false;

    LiveEngine eng;
    if (!eng.start(s, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
    const Scene sc = eng.scene();
    printf("Entree : %s\nSortie : %s\n\n%s\n", sc.inputName.c_str(), sc.outputName.c_str(), sc.plan.c_str());
    if (!eng.status().warning.empty()) printf("ATTENTION : %s\n", eng.status().warning.c_str());
    if (!s.keepDefault) printf("Sortie par defaut de Windows : VB-Cable (remise a la fermeture)\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
        const auto st0 = eng.status();
        if (!st0.captureMode.empty()) printf("Capture WASAPI : %s\n", st0.captureMode.c_str());
        if (!st0.outputMode.empty()) printf("Sortie WASAPI : %s\n", st0.outputMode.c_str());
        if (st0.totalLatencyMs > 0)
            printf("Latence estimee : %.1f ms (capture %.1f + marge %.0f + traitement %.1f + sortie %.1f)\n", st0.totalLatencyMs,
                   st0.capturePeriodMs, st0.marginMs, st0.processingMs, st0.outputBufferMs);
    }
    printf("Spatialisation en direct... (Ctrl+C ou fermer la fenetre pour arreter)\n");
    while (!stop && eng.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto st = eng.status();
        if (st.idle) printf("\r  En attente de son (aucune appli ne joue)...                                        ");
        else printf("\r  En direct | marge %3.0f ms (mini %4.1f) | derive %+4.0f ppm | entree %5.0f Hz | coupures %d | pertes VB-Cable %d   ",
                    st.marginMs, st.lowMs, st.driftPpm, st.inHz, st.underruns, st.captureGlitches);
        fflush(stdout);
    }
    const auto fin = eng.status();
    const std::string e = fin.error;
    eng.stop();
    // la marge apprise sert de point de départ la prochaine fois (moins de pops au démarrage)
    if ((int)fin.marginMs > s.marginMs) { Settings saved = loadSettings(); saved.marginMs = (int)fin.marginMs; saveSettings(saved); }
    printf("\nSortie par defaut remise sur : %s\n", sc.outputName.c_str());
    if (!e.empty()) { fprintf(stderr, "Erreur : %s\n", e.c_str()); return 1; }
    return 0;
}

} // namespace s3d
