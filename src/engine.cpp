#include "engine.h"
#include "dsp.h"
#include <cstring>
#include "live_core.h"
#include "renderer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <future>
#include <map>
#include <sstream>

namespace s3d {

// =============================================================================================
//  Fichiers de configuration
// =============================================================================================
std::wstring appDataDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring dir = n ? std::wstring(buf) + L"\\Spatial3D" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::string readFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return {};
    std::string s; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}
static void writeFile(const std::wstring& path, const std::string& s) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

std::wstring loadSavedOutput() {
    std::string s = readFile(appDataDir() + L"\\sortie.txt");
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return fromUtf8(s);
}
void saveOutput(const std::wstring& id) { writeFile(appDataDir() + L"\\sortie.txt", toUtf8(id)); }

Settings loadSettings() {
    Settings s;
    std::istringstream in(readFile(appDataDir() + L"\\config.ini"));
    std::map<std::string, std::string> kv;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    auto num = [&](const char* k, double def) { auto it = kv.find(k); return it == kv.end() ? def : atof(it->second.c_str()); };
    auto flag = [&](const char* k, bool def) { auto it = kv.find(k); return it == kv.end() ? def : it->second == "1"; };
    // « sortie= » des anciennes versions ignorée : la vraie sortie est celle choisie dans Windows
    if (kv.count("disposition")) s.layout = kv["disposition"];
    if (kv.count("sofa")) s.sofaPath = kv["sofa"];
    s.spreadDeg = num("etalement", 0);
    s.room = num("piece", -1);
    s.gainDb = num("volume", 0);
    s.marginMs = (int)num("marge", 6);
    s.keepDefault = flag("garder_sortie", false);
    s.exclusive = flag("exclusif", false);
    s.lowLatency = flag("faible_latence", true);
    s.followCableVolume = flag("suivre_volume_windows", true);
    if (kv.count("positions")) s.speakerPos = kv["positions"];
    s.liveOnLaunch = flag("temps_reel_au_lancement", false);
    s.closeToTray = flag("fermer_dans_zone_notif", true);
    s.startWithWindows = flag("demarrer_avec_windows", false);
    return s;
}

void saveSettings(const Settings& s) {
    std::ostringstream o;
    o << "disposition=" << s.layout << "\n"
      << "sofa=" << s.sofaPath << "\n"
      << "etalement=" << s.spreadDeg << "\n"
      << "piece=" << s.room << "\n"
      << "volume=" << s.gainDb << "\n"
      << "marge=" << s.marginMs << "\n"
      << "garder_sortie=" << (s.keepDefault ? 1 : 0) << "\n"
      << "exclusif=" << (s.exclusive ? 1 : 0) << "\n"
      << "faible_latence=" << (s.lowLatency ? 1 : 0) << "\n"
      << "suivre_volume_windows=" << (s.followCableVolume ? 1 : 0) << "\n"
      << "positions=" << s.speakerPos << "\n"
      << "temps_reel_au_lancement=" << (s.liveOnLaunch ? 1 : 0) << "\n"
      << "fermer_dans_zone_notif=" << (s.closeToTray ? 1 : 0) << "\n"
      << "demarrer_avec_windows=" << (s.startWithWindows ? 1 : 0) << "\n";
    writeFile(appDataDir() + L"\\config.ini", o.str());
}

void setStartWithWindows(bool on, const std::wstring& args) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + args;
        RegSetValueExW(k, L"Spatial3D", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"Spatial3D");
    }
    RegCloseKey(k);
}

// =============================================================================================
//  Utilitaires
// =============================================================================================
void Meters::feed(std::atomic<float>* m, const float* x, size_t frames, int ch) {
    const int n = std::min(ch, kMaxMeters);
    float pk[kMaxMeters] = {};
    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < n; ++c) pk[c] = std::max(pk[c], std::fabs(x[f * ch + c]));
    for (int c = 0; c < n; ++c) {
        float cur = m[c].load();
        while (pk[c] > cur && !m[c].compare_exchange_weak(cur, pk[c])) {}
    }
}

Layout layoutForEndpoint(const Endpoint& e) {
    Layout out;
    if (e.headphones) { layoutFromName("casque", out); return out; }
    uint32_t m = e.mixMask;
    if (e.physicalMask && (e.physicalMask & ~e.mixMask) == 0) m = e.physicalMask;
    return layoutFromMask(m);
}

Layout layoutFor(const Settings& s, const Endpoint& e) {
    Layout l;
    if (s.layout == "auto" || !layoutFromName(s.layout, l)) l = layoutForEndpoint(e);
    applySpeakerOverrides(l, s.speakerPos);
    return l;
}

bool resolveOutput(const Settings& s, Endpoint& out, std::string& err) {
    std::vector<Endpoint> v;
    if (!listEndpoints(false, v, err)) return false;
    auto pick = [&](const std::wstring& id) {
        for (auto& e : v) if (!id.empty() && e.id == id && !e.isVbCable()) { out = e; return true; }
        return false;
    };
    if (pick(s.outputId)) return true;
    for (auto& e : v) if (e.isDefault && !e.isVbCable()) { out = e; return true; }
    if (pick(loadSavedOutput())) return true;
    for (auto& e : v) if (!e.isVbCable()) { out = e; return true; }
    err = "Aucune sortie audio reelle trouvee";
    return false;
}

static bool findCable(Endpoint& in, Endpoint& out, std::string& err) {
    std::vector<Endpoint> r, c;
    if (!listEndpoints(false, r, err) || !listEndpoints(true, c, err)) return false;
    bool a = false, b = false;
    for (auto& e : r) if (e.isVbCable()) { in = e; a = true; break; }
    for (auto& e : c) if (e.isVbCable()) { out = e; b = true; break; }
    if (!a || !b) {
        err = "VB-Cable n'est pas installe (ou l'ordinateur n'a pas ete redemarre depuis son installation).";
        return false;
    }
    return true;
}

int configureCable() {
    std::string err;
    Endpoint cin, cout_;
    if (!findCable(cin, cout_, err)) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return 1; }
    const int rate = cin.mixRate ? cin.mixRate : 48000;
    bool ok = setEndpoint71(cin.id, false, rate, err);
    if (ok) ok = setEndpoint71(cout_.id, true, rate, err);
    if (!ok) { fprintf(stderr, "Erreur : %s\n", err.c_str()); return err.find("0x80070005") != std::string::npos ? 5 : 1; }
    printf("VB-Cable regle en 7.1 (%d Hz)\n", rate);
    return 0;
}

bool runElevated(const std::wstring& args) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW si{}; si.cbSize = sizeof si;
    si.fMask = SEE_MASK_NOCLOSEPROCESS; si.lpVerb = L"runas"; si.lpFile = exe;
    si.lpParameters = args.c_str(); si.nShow = SW_HIDE;
    if (!ShellExecuteExW(&si) || !si.hProcess) return false;
    WaitForSingleObject(si.hProcess, 60000);
    DWORD code = 1; GetExitCodeProcess(si.hProcess, &code);
    CloseHandle(si.hProcess);
    return code == 0;
}

static Scene makeScene(const std::vector<VirtualSource>& src, int inCh, const Layout& out, const RenderOptions& opt) {
    Scene sc;
    sc.out = out; sc.sources = src; sc.inChannels = inCh; sc.outChannels = out.headphones ? 2 : (int)out.speakers.size();
    if (!out.headphones) sc.matrix = buildSpeakerMatrix(src, inCh, out, opt);
    return sc;
}

static RenderOptions optionsFor(const Settings& s, const Layout& out) {
    RenderOptions o;
    // Réglages figés pour la fidélité : image nette (pas d'étalement), pièce virtuelle seulement au casque
    o.spreadDeg = 0;
    o.room = out.headphones ? 0.15 : 0.0;
    o.sofaPath = s.sofaPath;
    return o;
}

// =============================================================================================
//  Temps réel
// =============================================================================================
struct LiveEngine::Impl {
    using Clock = std::chrono::steady_clock;
    Settings set;
    Endpoint cableIn, cableOut, outEp;
    std::unique_ptr<Renderer> renderer;
    Scene scene;
    // déplacement des enceintes en direct : l'interface dépose, le fil audio échange et rend l'ancien
    std::atomic<Renderer*> pending{ nullptr }, retired{ nullptr };
    std::vector<VirtualSource> sources;
    RenderOptions opt;
    int outRate = 48000;
    std::vector<float> xfade;               // rendu du nouveau moteur pendant le fondu
    DistanceAligner align;                  // distances réelles des enceintes (retard + niveau)
    // historique de l'entrée du rendu : amorce le nouveau moteur avant l'échange (filtres HRTF pleins)
    static constexpr size_t kHist = 2048;
    std::vector<float> hist, primeIn, primeOut;
    size_t histW = 0;
    EndpointVolumeWatch vol;                // volume / sourdine Windows de CABLE Input
    bool followVol = false;
    std::string exclBusy;                   // applis qui jouent déjà sur la sortie : pas d'exclusif
    ~Impl() { delete pending.exchange(nullptr); delete retired.exchange(nullptr); }
    SpscRing ring;                         // capture -> rendu, sans verrou
    std::promise<void> go;                 // la capture démarre une fois le moteur prêt
    std::atomic<int> ringOverflow{ 0 };
    LiveInputStage stage;
    Limiter lim;
    std::thread capThread, renderThread;
    std::atomic<bool> stop{ false }, capStop{ false }, idle{ true };
    std::atomic<long long> lastDataMs{ -100000 }, capturedFrames{ 0 };
    std::atomic<int> underruns{ 0 }, drops{ 0 }, capGlitches{ 0 };
    std::string outputMode, captureMode;
    double capPeriodMs = 10, procMs = 0;
    std::atomic<double> outBufMs{ 0 };
    std::atomic<double> lowMs{ 0 }, marginMs{ 0 }, driftPpm{ 0 };
    bool switched = false;
    int capCh = 0, capRate = 48000; uint32_t capMask = 0;
    Clock::time_point t0 = Clock::now();
    mutable std::mutex m;
    std::string error, warning;
    mutable long long lastFrames = 0, lastT = 0;
    mutable double inHz = 0;
    std::vector<float> incoming, inBuf;      // préalloués avant le démarrage du rendu

    long long nowMs() const { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count(); }
    void setError(const std::string& e) { std::lock_guard<std::mutex> lk(m); if (error.empty()) error = e; }
};

LiveEngine::LiveEngine() = default;
LiveEngine::~LiveEngine() { stop(); }

bool LiveEngine::start(const Settings& s, std::string& err) {
    stop();
    d_ = std::make_unique<Impl>();
    auto& d = *d_;
    d.set = s;
    meters.reset();

    if (!findCable(d.cableIn, d.cableOut, err)) return false;
    if (!resolveOutput(s, d.outEp, err)) return false;
    saveOutput(d.outEp.id);

    // VB-Cable en 7.1 (sinon pas d'information de position)
    if (d.cableOut.mixChannels < 8) {
        if (configureCable() == 5) runElevated(L"--configurer-cable");
        std::string e2; getEndpoint(d.cableOut.id, true, d.cableOut, e2);
        if (d.cableOut.mixChannels < 8)
            d.warning = "VB-Cable reste en " + std::to_string(d.cableOut.mixChannels) +
                        " canaux : reglez CABLE Input en 7.1 (Panneau de configuration > Son > Configurer).";
    }

    // Capture : ouverte DANS son fil (objets COM créés et utilisés au même endroit)
    std::promise<std::string> opened;
    auto openedF = opened.get_future();
    d.capThread = std::thread([this, &opened] {
        auto& d = *d_;
        WasapiCapture cap;
        std::string e;
        if (!cap.open(d.cableOut.id, 20, e, d.set.lowLatency)) { opened.set_value(e); return; }
        d.capCh = cap.channels(); d.capRate = cap.rate(); d.capMask = cap.mask();
        d.capPeriodMs = cap.periodMs();
        { char b[96]; snprintf(b, sizeof b, "%s, période %.1f ms", cap.modeInfo().c_str(), d.capPeriodMs); d.captureMode = b; }
        auto goF = d.go.get_future();
        opened.set_value("");
        goF.wait();                                // tampon et moteur prêts
        if (d.capStop) return;
        enableFlushToZero();
        std::string runErr;
        const int ch = d.capCh;
        cap.run([&](const float* x, size_t n, bool, bool discontinuity) {
            if (discontinuity) ++d.capGlitches;
            if (d.ring.push(x, n * ch) < n * ch) ++d.ringOverflow;
            d.capturedFrames += (long long)n;
            d.lastDataMs = d.nowMs();
        }, d.capStop, runErr);
        if (!runErr.empty()) { d.setError(runErr); d.stop = true; }
    });
    err = openedF.get();
    if (!err.empty()) { d.capThread.join(); return false; }

    // Disposition d'entrée d'après le format de VB-Cable
    std::vector<int> ids;
    for (auto& sp : layoutFromMask(d.capMask).speakers) ids.push_back(sp.id);
    if ((int)ids.size() != d.capCh) {
        Layout l; ids.clear();
        layoutFromName(d.capCh == 8 ? "7.1" : d.capCh == 6 ? "5.1" : d.capCh == 4 ? "quad" : "2.0", l);
        for (auto& sp : l.speakers) ids.push_back(sp.id);
        ids.resize(d.capCh, -1);
    }
    auto sources = sourcesFromChannels(ids);
    const Layout outL = layoutFor(s, d.outEp);
    if (outL.headphones) applyVirtualSpeakerOverrides(sources, ids, s.speakerPos);
    const RenderOptions opt = optionsFor(s, outL);
    const int outRate = d.outEp.mixRate;
    d.renderer = makeRenderer(sources, d.capCh, outL, outRate, opt, err);
    d.sources = sources; d.opt = opt; d.outRate = outRate;
    if (!d.renderer) { d.capStop = true; d.go.set_value(); d.capThread.join(); return false; }
    d.scene = makeScene(sources, d.capCh, outL, opt);
    d.scene.inIds = ids;
    d.scene.inputName = d.cableOut.name;
    d.scene.outputName = d.outEp.name;
    d.scene.plan = d.renderer->describe();

    d.ring.init((size_t)d.capRate * d.capCh);        // 1 seconde de marge
    d.incoming.assign((size_t)d.capRate * d.capCh, 0.f);
    d.inBuf.assign((size_t)16384 * d.capCh, 0.f);
    d.xfade.assign((size_t)16384 * d.renderer->outChannels(), 0.f);
    d.hist.assign(Impl::kHist * d.capCh, 0.f); d.primeIn.assign(Impl::kHist * d.capCh, 0.f);
    d.primeOut.assign(Impl::kHist * d.renderer->outChannels(), 0.f); d.histW = 0;
    d.align.init(d.renderer->outChannels(), outRate, 0.02);
    { auto al = computeDistanceAlignment(outL); d.align.setTargets(al.delaySec, al.gain); }
    if (s.followCableVolume) {
        std::string ve;
        d.followVol = d.vol.start(d.cableIn.id, d.outEp.id, ve);
        if (!d.followVol && d.warning.empty()) d.warning = ve;
    }
    d.stage.init(d.capCh, d.capRate, outRate, std::clamp(s.marginMs, 5, 200));
    d.marginMs = d.stage.controller().marginMs();
    d.lim.init(outRate, d.renderer->outChannels());
    d.procMs = 1000.0 * (d.renderer->latencyFrames() + d.lim.latencyFrames()) / outRate
             + 1000.0 * AdaptiveResampler::kHalf / d.capRate;
    setGainDb(s.gainDb);

    if (!s.keepDefault) {
        std::string e2;
        if (setDefaultEndpoint(d.cableIn.id, e2)) d.switched = true;
        else d.warning = e2 + " - choisissez CABLE Input comme sortie dans vos applications.";
    }
    // Exclusif seulement sur une sortie libre : si une autre application y joue encore (réglée sur cette
    // carte précisément, ou sortie par défaut non basculée), Spatial3D envoie le son en partagé sur
    // cette sortie, mélangé par Windows avec elle, au lieu de la faire taire. Vérifié après la bascule
    // sur VB-Cable, le temps que les applications qui suivent la sortie par défaut l'aient quittée.
    if (s.exclusive) {
        if (d.switched) std::this_thread::sleep_for(std::chrono::milliseconds(400));
        for (auto& a : appsPlayingOn(d.outEp.id)) d.exclBusy += (d.exclBusy.empty() ? "" : ", ") + a;
    }

    const int outCh = d.renderer->outChannels();
    const uint32_t outMask = (uint32_t)(outL.mask() & 0xFFFFFFFFu);
    running_ = true;
    d.go.set_value();
    d.renderThread = std::thread([this, outCh, outMask, outRate] {
        auto& d = *d_;
        bool ftz = true;
        float volCur = d.followVol ? d.vol.gain() : 1.f;
        auto pull = [&](float* dst, size_t frames) -> size_t {
            if (ftz) { ftz = false; enableFlushToZero(); }
            const size_t got = d.ring.pop(d.incoming.data(), d.incoming.size());
            if (got) d.stage.feed(d.incoming.data(), got / d.capCh);
            if (frames * d.capCh > d.inBuf.size()) d.inBuf.resize(frames * d.capCh);
            const bool sourceIdle = d.nowMs() - d.lastDataMs.load() > 100;
            d.idle = sourceIdle;
            d.stage.produce(d.inBuf.data(), frames, sourceIdle);
            Meters::feed(meters.in, d.inBuf.data(), frames, d.capCh);
            d.renderer->process(d.inBuf.data(), dst, frames);
            // enceintes déplacées : fondu enchaîné ancienne -> nouvelle matrice sur ce bloc
            if (d.pending.load(std::memory_order_acquire) && !d.retired.load(std::memory_order_acquire) &&
                frames * outCh <= d.xfade.size()) {
                Renderer* nr = d.pending.exchange(nullptr);
                if (nr) {
                    // amorce : le nouveau moteur rejoue les ~43 dernières ms d'entrée (sortie ignorée)
                    const size_t H = Impl::kHist, ch = (size_t)d.capCh;
                    for (size_t f = 0; f < H; ++f) {
                        const size_t src = (d.histW + f) % H;
                        for (size_t c = 0; c < ch; ++c) d.primeIn[f * ch + c] = d.hist[src * ch + c];
                    }
                    nr->process(d.primeIn.data(), d.primeOut.data(), H);
                    nr->process(d.inBuf.data(), d.xfade.data(), frames);
                    const float inv = 1.f / (float)frames;
                    for (size_t f = 0; f < frames; ++f) {
                        const float t = (f + 1) * inv;
                        for (int c = 0; c < outCh; ++c) {
                            float& o = dst[f * outCh + c];
                            o += t * (d.xfade[f * outCh + c] - o);
                        }
                    }
                    d.retired.store(d.renderer.release(), std::memory_order_release);
                    d.renderer.reset(nr);
                }
            }
            for (size_t f = 0; f < frames; ++f) {           // historique de l'entrée du rendu
                std::memcpy(&d.hist[d.histW * d.capCh], &d.inBuf[f * d.capCh], sizeof(float) * d.capCh);
                d.histW = (d.histW + 1) % Impl::kHist;
            }
            d.align.process(dst, frames);
            // gain général x volume Windows de CABLE Input (rampe sur le bloc : ni clic ni zip)
            const float g = gain_.load();
            const float volT = d.followVol ? d.vol.gain() : 1.f;
            if (g != 1.f || volT != 1.f || volCur != 1.f) {
                const float step = (volT - volCur) / (float)frames;
                for (size_t f = 0; f < frames; ++f) {
                    volCur += step;
                    const float k = g * volCur;
                    for (int c = 0; c < outCh; ++c) dst[f * outCh + c] *= k;
                }
                volCur = volT;
            }
            d.lim.process(dst, frames);
            Meters::feed(meters.out, dst, frames, outCh);
            const auto& dc = d.stage.controller();
            d.lowMs = dc.lowMs(); d.marginMs = dc.marginMs(); d.driftPpm = dc.driftPpm();
            d.underruns = d.stage.underruns(); d.drops = d.stage.drops();
            return frames;
        };
        std::string e, mode;
        PlayOptions po; po.bufferMs = 20; po.exclusive = d.set.exclusive && d.exclBusy.empty(); po.lowLatency = d.set.lowLatency;
        int bufFrames = 0;
        bool first = true;
        auto pullAndReport = [&](float* dst, size_t frames) -> size_t {
            if (first) {
                first = false; d.outBufMs = 1000.0 * bufFrames / outRate;
                d.vol.setExclusive(mode.rfind("exclusif", 0) == 0);
                if (!d.exclBusy.empty()) mode += " (exclusif non pris : " + d.exclBusy + " joue déjà sur cette sortie)";
                std::lock_guard<std::mutex> lk(d.m); d.outputMode = mode;
            }
            return pull(dst, frames);
        };
        playWasapi(d.outEp.id, outCh, outMask, outRate, po, pullAndReport, d.stop, e, &mode, &bufFrames);
        if (!e.empty()) d.setError(e);
        d.capStop = true;
        running_ = false;
    });
    return true;
}

void LiveEngine::stop(bool restoreDefault) {
    if (!d_) return;
    auto& d = *d_;
    d.stop = true; d.capStop = true;
    if (d.renderThread.joinable()) d.renderThread.join();
    if (d.capThread.joinable()) d.capThread.join();
    d.vol.stop();
    if (d.switched && restoreDefault) { std::string e; setDefaultEndpoint(d.outEp.id, e); }
    d.switched = false;
    running_ = false;
    meters.reset();
}

LiveEngine::Status LiveEngine::status() const {
    Status st;
    if (!d_) return st;
    auto& d = *d_;
    st.running = running_;
    st.idle = d.idle;
    st.marginMs = d.marginMs; st.lowMs = d.lowMs; st.driftPpm = d.driftPpm;
    st.underruns = d.underruns;
    st.drops = d.drops;
    st.captureGlitches = d.capGlitches;
    std::lock_guard<std::mutex> lk(d.m);
    st.outputMode = d.outputMode;
    st.captureMode = d.captureMode;
    st.capturePeriodMs = d.capPeriodMs; st.processingMs = d.procMs; st.outputBufferMs = d.outBufMs;
    st.totalLatencyMs = st.outputBufferMs > 0 ? st.capturePeriodMs + st.marginMs + st.processingMs + st.outputBufferMs : 0;
    const long long f = d.capturedFrames.load(), t = d.nowMs();
    if (t - d.lastT >= 1000) {
        const double hz = (f - d.lastFrames) * 1000.0 / (double)(t - d.lastT);
        d.inHz = d.inHz == 0 ? hz : d.inHz + 0.3 * (hz - d.inHz);
        d.lastFrames = f; d.lastT = t;
    }
    st.inHz = d.inHz;
    st.error = d.error; st.warning = d.warning;
    st.followVolume = d.followVol;
    st.outputId = d.outEp.id; st.cableId = d.cableIn.id;
    if (d.followVol) {
        st.windowsMuted = d.vol.muted(); st.windowsVolumeDb = d.vol.levelDb();
        st.windowsVolumePct = d.vol.levelPercent(); st.volumeDigital = d.vol.digital();
    }
    return st;
}

Scene LiveEngine::scene() const {
    if (!d_) return Scene{};
    std::lock_guard<std::mutex> lk(d_->m);
    return d_->scene;
}

bool LiveEngine::moveSpeakers(const Layout& out) {
    if (!d_ || !running_) return false;
    auto& d = *d_;
    if (out.headphones || d.scene.out.headphones || out.speakers.size() != d.scene.out.speakers.size()) return false;
    { auto al = computeDistanceAlignment(out); d.align.setTargets(al.delaySec, al.gain); }
    bool sameAngles = true;
    for (size_t i = 0; i < out.speakers.size(); ++i)
        if (out.speakers[i].az != d.scene.out.speakers[i].az || out.speakers[i].el != d.scene.out.speakers[i].el) sameAngles = false;
    if (sameAngles) {                             // seule la distance a changé : pas de nouvelle matrice
        std::lock_guard<std::mutex> lk(d.m);
        d.scene.out = out;
        return true;
    }
    delete d.retired.exchange(nullptr);           // ancien moteur rendu par le fil audio
    std::string err;
    auto r = makeRenderer(d.sources, d.capCh, out, d.outRate, d.opt, err);
    if (!r || r->outChannels() != d.scene.outChannels) return false;
    Scene sc = makeScene(d.sources, d.capCh, out, d.opt);
    delete d.pending.exchange(r.release());        // remplace un déplacement pas encore appliqué
    std::lock_guard<std::mutex> lk(d.m);
    sc.inIds = d.scene.inIds; sc.inputName = d.scene.inputName; sc.outputName = d.scene.outputName;
    sc.plan = d.scene.plan;
    d.scene = std::move(sc);
    return true;
}

bool LiveEngine::moveVirtualSpeakers(const std::vector<VirtualSource>& src) {
    if (!d_ || !running_) return false;
    auto& d = *d_;
    if (!d.scene.out.headphones || src.size() != d.sources.size()) return false;
    std::vector<VirtualSource> ns = d.sources;
    for (size_t k = 0; k < ns.size(); ++k) { ns[k].az = src[k].az; ns[k].dist = src[k].dist; }
    delete d.retired.exchange(nullptr);
    std::string err;
    auto r = makeRenderer(ns, d.capCh, d.scene.out, d.outRate, d.opt, err);
    if (!r || r->outChannels() != d.scene.outChannels) return false;
    d.sources = ns;
    delete d.pending.exchange(r.release());
    std::lock_guard<std::mutex> lk(d.m);
    d.scene.sources = ns;
    return true;
}

} // namespace s3d
