// Spatial3D — spatialisation 3D pour Windows
// Lit un fichier audio, détecte la configuration de sortie de Windows (stéréo, 5.1, 7.1, 7.1.4, casque)
// et place chaque son au meilleur endroit : VBAP sur haut-parleurs, HRTF au casque.
#include "decoder.h"
#include "dsp.h"
#include "layout.h"
#include "renderer.h"
#include "sources.h"
#include "wav_writer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include "engine.h"
#include "live.h"
#include "wasapi_output.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

using namespace s3d;

static const char* kVersion = "0.4.1";
static std::atomic<bool> gStop{ false };

#ifdef _WIN32
static HANDLE gCleanupDone = nullptr;
// Ctrl+C, ou fermeture de la fenêtre : on laisse au mode temps réel le temps de rendre la sortie à Windows
static BOOL WINAPI onCtrl(DWORD type) {
    gStop = true;
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT)
        if (gCleanupDone) WaitForSingleObject(gCleanupDone, 4000);
    return TRUE;
}
#endif

struct Args {
    std::string input, wavOut, sortie = "auto", sofa;
    double spread = -1, room = -1, gainDb = 0;
    bool forceAmbi = false, infoOnly = false, help = false, list = false;
    bool live = false, devices = false, configCable = false, restore = false, keepDefault = false, exclusive = false;
    std::string inName, outName;
    double latency = 10;
};

static void usage() {
    printf(
        "Spatial3D %s - spatialisation 3D (BJMPC Informatique)\n\n"
        "Usage : spatial3d-cli <fichier> [options]      lecture d'un fichier\n"
        "        spatial3d-cli --temps-reel [options]   son de Windows en direct (via VB-Cable)\n"
        "(interface graphique : Spatial3D.exe)\n\n"
        "  --sortie <disposition>  auto (defaut : configuration Windows), casque, 2.0, 2.1, 3.0,\n"
        "                          quad, 5.0, 5.1, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4\n"
        "  --wav <fichier.wav>     rendu dans un fichier au lieu de la lecture\n"
        "  --etalement <degres>    elargit chaque source (0-90, defaut 0)\n"
        "  --piece <0..1>          piece virtuelle au casque (defaut 0.15)\n"
        "  --sofa <fichier.sofa>   HRTF mesuree pour le casque (au lieu du modele parametrique)\n"
        "  --ambisonique           traite le fichier comme de l'Ambisonique AmbiX (4 ou 9 canaux)\n"
        "  --volume <dB>           gain general (defaut 0)\n"
        "  --info                  affiche l'analyse et le plan de placement sans jouer\n"
        "  --dispositions          liste les dispositions connues\n\n"
        "Temps reel :\n"
        "  --temps-reel            capte VB-Cable (7.1) et spatialise vers la vraie sortie\n"
        "  --vers <nom>            vraie sortie (partie du nom ; memorisee pour la suite)\n"
        "  --entree <nom>          autre entree que VB-Cable\n"
        "  --latence <ms>          marge initiale (defaut 10, augmente seule si coupure)\n"
        "  --garder-sortie         ne pas basculer la sortie par defaut de Windows sur VB-Cable\n"
        "  --exclusif              sortie WASAPI exclusive (latence minimale, sans melangeur Windows)\n"
        "  --peripheriques         liste les sorties et entrees audio\n"
        "  --configurer-cable      regle VB-Cable en 7.1\n"
        "  --restaurer             remet la vraie sortie par defaut (apres un plantage)\n", kVersion);
}

static bool parse(int argc, char** argv, Args& a, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&](std::string& dst) { if (i + 1 >= argc) { err = s + " attend une valeur"; return false; } dst = argv[++i]; return true; };
        auto nextD = [&](double& dst) { std::string v; if (!next(v)) return false; char* e = nullptr; dst = strtod(v.c_str(), &e); if (!e || *e) { err = s + " : nombre invalide"; return false; } return true; };
        if (s == "-h" || s == "--aide" || s == "--help") a.help = true;
        else if (s == "--sortie" || s == "--output") { if (!next(a.sortie)) return false; }
        else if (s == "--wav") { if (!next(a.wavOut)) return false; }
        else if (s == "--sofa") { if (!next(a.sofa)) return false; }
        else if (s == "--etalement" || s == "--spread") { if (!nextD(a.spread)) return false; }
        else if (s == "--piece" || s == "--room") { if (!nextD(a.room)) return false; }
        else if (s == "--volume") { if (!nextD(a.gainDb)) return false; }
        else if (s == "--ambisonique" || s == "--ambisonic") a.forceAmbi = true;
        else if (s == "--info") a.infoOnly = true;
        else if (s == "--dispositions") a.list = true;
        else if (s == "--temps-reel" || s == "--live") a.live = true;
        else if (s == "--vers" || s == "--to") { if (!next(a.outName)) return false; }
        else if (s == "--entree" || s == "--from") { if (!next(a.inName)) return false; }
        else if (s == "--latence") { if (!nextD(a.latency)) return false; }
        else if (s == "--garder-sortie") a.keepDefault = true;
        else if (s == "--exclusif") a.exclusive = true;
        else if (s == "--peripheriques" || s == "--devices") a.devices = true;
        else if (s == "--configurer-cable") a.configCable = true;
        else if (s == "--restaurer") a.restore = true;
        else if (!s.empty() && s[0] == '-') { err = "Option inconnue : " + s; return false; }
        else if (a.input.empty()) a.input = s;
        else { err = "Un seul fichier a la fois"; return false; }
    }
    return true;
}

static std::string mmss(double t) {
    char b[32]; const int s = (int)t; snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60); return b;
}

int run(int argc, char** argv) {
    Args a; std::string err;
    if (!parse(argc, argv, a, err)) { fprintf(stderr, "Erreur : %s\n\n", err.c_str()); usage(); return 2; }
    if (a.list) { for (auto& n : knownLayoutNames()) printf("  %s\n", n.c_str()); return 0; }
    if (a.help) { usage(); return 0; }
#ifdef _WIN32
    if (a.devices) return listDevices();
    if (a.configCable) return configureCable();
    if (a.restore) return restoreOutput();
    if (a.live) {
        LiveArgs la;
        la.inputName = a.inName; la.outputName = a.outName; la.sortie = a.sortie; la.sofa = a.sofa;
        la.spread = a.spread >= 0 ? std::min(a.spread, 90.0) : 0; la.room = a.room; la.gainDb = a.gainDb;
        la.latencyMs = (int)a.latency; la.keepDefault = a.keepDefault; la.exclusive = a.exclusive;
        return runLive(la, gStop);
    }
#else
    if (a.devices || a.configCable || a.restore || a.live) { fprintf(stderr, "Erreur : option disponible sous Windows uniquement\n"); return 2; }
#endif
    if (a.input.empty()) { usage(); return 2; }

    // ---- 1. Configuration de sortie ----
    Layout out;
    int deviceRate = 0;
    const bool play = a.wavOut.empty();
    if (a.sortie == "auto") {
#ifdef _WIN32
        Endpoint dev;
        if (!queryDefaultDevice(dev, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
        deviceRate = dev.mixRate;
        printf("Sortie Windows : %s\n", dev.name.c_str());
        printf("  type : %s | format de mixage : %d canaux, %d Hz (%s)",
               dev.formFactorName().c_str(), dev.mixChannels, dev.mixRate, describeMask(dev.mixMask).c_str());
        if (dev.physicalMask) printf(" | haut-parleurs configures : %s", describeMask(dev.physicalMask).c_str());
        printf("\n");
        if (dev.isVbCable()) printf("  (la sortie par defaut est VB-Cable : un mode --temps-reel tourne peut-etre deja)\n");
        out = layoutForEndpoint(dev);
        if (out.speakers.empty()) { fprintf(stderr, "Erreur : disposition Windows non reconnue\n"); return 1; }
#else
        layoutFromName("2.0", out);
        printf("Pas de detection hors Windows : sortie 2.0 par defaut\n");
#endif
    } else if (!layoutFromName(a.sortie, out)) {
        fprintf(stderr, "Erreur : disposition inconnue '%s' (voir --dispositions)\n", a.sortie.c_str());
        return 2;
    }
#ifndef _WIN32
    if (play && !a.infoOnly) { fprintf(stderr, "Erreur : la lecture directe n'existe que sous Windows ; utilisez --wav\n"); return 2; }
#endif

    // ---- 2. Lecture du fichier source ----
    Decoder dec;
    if (!dec.open(a.input, play ? deviceRate : 0, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
    const int rate = dec.sampleRate();
    printf("Source : %s\n", dec.info().c_str());

    std::vector<VirtualSource> sources;
    const bool ambi = a.forceAmbi || dec.ambisonic();
    if (ambi) {
        const int order = ambisonicOrderForChannels(dec.channels());
        if (order < 1) { fprintf(stderr, "Erreur : %d canaux ne correspondent a aucun ordre ambisonique\n", dec.channels()); return 1; }
        if (order > 2) printf("Note : ordre ambisonique %d reduit a l'ordre 2\n", order);
        sources = sourcesFromAmbisonics(dec.channels(), order);
    } else {
        sources = sourcesFromChannels(dec.channelIds());
    }

    // ---- 3. Moteur de rendu ----
    RenderOptions opt;
    opt.spreadDeg = a.spread >= 0 ? std::min(a.spread, 90.0) : 0;
    opt.room = a.room >= 0 ? std::min(a.room, 1.0) : (out.headphones ? 0.15 : 0.0);
    opt.sofaPath = a.sofa;
    auto renderer = makeRenderer(sources, dec.channels(), out, rate, opt, err);
    if (!renderer) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
    printf("\n%s\n", renderer->describe().c_str());
    if (a.infoOnly) return 0;

    // ---- 4. Chaîne : décodage -> placement -> gain -> limiteur ----
    const int inCh = dec.channels(), outCh = renderer->outChannels();
    const float gain = (float)std::pow(10.0, a.gainDb / 20.0);
    Limiter lim; lim.init(rate, outCh);
    std::vector<float> inBuf;
    size_t done = 0;
    const double total = dec.durationSec();
    auto lastPrint = std::chrono::steady_clock::now();

    auto pull = [&](float* dst, size_t frames) -> size_t {
        size_t produced = 0;
        while (produced < frames && !gStop) {
            const size_t chunk = std::min<size_t>(frames - produced, 2048);
            inBuf.resize(chunk * inCh);
            const size_t got = dec.read(inBuf.data(), chunk);
            if (!got) break;
            float* o = dst + produced * outCh;
            renderer->process(inBuf.data(), o, got);
            if (gain != 1.f) for (size_t k = 0; k < got * outCh; ++k) o[k] *= gain;
            lim.process(o, got);
            produced += got;
        }
        done += produced;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPrint > std::chrono::milliseconds(500)) {
            lastPrint = now;
            printf("\r  > %s / %s   ", mmss((double)done / rate).c_str(), total > 0 ? mmss(total).c_str() : "--:--");
            fflush(stdout);
        }
        return produced;
    };

    uint32_t mask32 = (uint32_t)(out.mask() & 0xFFFFFFFFu);
    if (a.wavOut.size()) {
        WavWriter w;
        if (!w.open(a.wavOut, outCh, rate, mask32)) { fprintf(stderr, "Erreur : ecriture de %s impossible\n", a.wavOut.c_str()); return 1; }
        std::vector<float> buf(4096 * outCh);
        size_t got;
        while ((got = pull(buf.data(), 4096)) > 0) w.write(buf.data(), got);
        w.close();
        printf("\rRendu termine : %s (%s, %d canaux)        \n", a.wavOut.c_str(), mmss((double)done / rate).c_str(), outCh);
    } else {
#ifdef _WIN32
        printf("Lecture... (Ctrl+C pour arreter)\n");
        if (!playWasapi(L"", outCh, mask32, rate, PlayOptions{ 200, false }, pull, gStop, err)) { fprintf(stderr, "\nErreur : %s\n", err.c_str()); return 1; }
        printf("\n%s\n", gStop ? "Arrete." : "Termine.");
#endif
    }
    if (lim.reductionDb() < -0.5f)
        printf("Limiteur : jusqu'a %.1f dB de reduction (essayez --volume -%d)\n", lim.reductionDb(), (int)std::ceil(-lim.reductionDb()));
    return 0;
}

#ifdef _WIN32
int main() {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    gCleanupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(onCtrl, TRUE);
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> store;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
        store.push_back(s);
    }
    LocalFree(wargv);
    std::vector<char*> argv;
    for (auto& s : store) argv.push_back(&s[0]);
    const int rc = run(argc, argv.data());
    SetEvent(gCleanupDone);
    return rc;
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
