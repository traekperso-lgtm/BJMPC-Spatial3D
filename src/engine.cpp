#include "engine.h"
#include "decoder.h"
#include "dsp.h"
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
    if (kv.count("sortie")) s.outputId = fromUtf8(kv["sortie"]);
    else s.outputId = loadSavedOutput();
    if (kv.count("disposition")) s.layout = kv["disposition"];
    if (kv.count("sofa")) s.sofaPath = kv["sofa"];
    s.spreadDeg = num("etalement", 0);
    s.room = num("piece", -1);
    s.gainDb = num("volume", 0);
    s.marginMs = (int)num("marge", 10);
    s.keepDefault = flag("garder_sortie", false);
    s.exclusive = flag("exclusif", false);
    s.liveOnLaunch = flag("temps_reel_au_lancement", false);
    s.closeToTray = flag("fermer_dans_zone_notif", true);
    s.startWithWindows = flag("demarrer_avec_windows", false);
    return s;
}

void saveSettings(const Settings& s) {
    std::ostringstream o;
    o << "sortie=" << toUtf8(s.outputId) << "\n"
      << "disposition=" << s.layout << "\n"
      << "sofa=" << s.sofaPath << "\n"
      << "etalement=" << s.spreadDeg << "\n"
      << "piece=" << s.room << "\n"
      << "volume=" << s.gainDb << "\n"
      << "marge=" << s.marginMs << "\n"
      << "garder_sortie=" << (s.keepDefault ? 1 : 0) << "\n"
      << "exclusif=" << (s.exclusive ? 1 : 0) << "\n"
      << "temps_reel_au_lancement=" << (s.liveOnLaunch ? 1 : 0) << "\n"
      << "fermer_dans_zone_notif=" << (s.closeToTray ? 1 : 0) << "\n"
      << "demarrer_avec_windows=" << (s.startWithWindows ? 1 : 0) << "\n";
    writeFile(appDataDir() + L"\\config.ini", o.str());
    if (!s.outputId.empty()) saveOutput(s.outputId);
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
    if (s.layout != "auto" && layoutFromName(s.layout, l)) return l;
    return layoutForEndpoint(e);
}

bool resolveOutput(const Settings& s, Endpoint& out, std::string& err) {
    std::vector<Endpoint> v;
    if (!listEndpoints(false, v, err)) return false;
    auto pick = [&](const std::wstring& id) {
        for (auto& e : v) if (!id.empty() && e.id == id && !e.isVbCable()) { out = e; return true; }
        return false;
    };
    if (pick(s.outputId) || pick(loadSavedOutput())) return true;
    for (auto& e : v) if (e.isDefault && !e.isVbCable()) { out = e; return true; }
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
    o.spreadDeg = std::clamp(s.spreadDeg, 0.0, 90.0);
    o.room = s.room >= 0 ? std::min(s.room, 1.0) : (out.headphones ? 0.15 : 0.0);
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
    FrameQueue queue;
    LiveInputStage stage;
    Limiter lim;
    std::thread capThread, renderThread;
    std::atomic<bool> stop{ false }, capStop{ false }, idle{ true };
    std::atomic<long long> lastDataMs{ -100000 }, capturedFrames{ 0 };
    std::atomic<int> underruns{ 0 }, drops{ 0 }, capGlitches{ 0 };
    std::string outputMode;
    std::atomic<double> lowMs{ 0 }, marginMs{ 0 }, driftPpm{ 0 };
    bool switched = false;
    int capCh = 0, capRate = 48000; uint32_t capMask = 0;
    Clock::time_point t0 = Clock::now();
    mutable std::mutex m;
    std::string error, warning;
    mutable long long lastFrames = 0, lastT = 0;
    mutable double inHz = 0;
    std::vector<float> incoming, inBuf;

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
        if (!cap.open(d.cableOut.id, 20, e)) { opened.set_value(e); return; }
        d.capCh = cap.channels(); d.capRate = cap.rate(); d.capMask = cap.mask();
        opened.set_value("");
        std::string runErr;
        cap.run([&](const float* x, size_t n, bool, bool discontinuity) {
            if (discontinuity) ++d.capGlitches;
            d.queue.push(x, n);
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
    const RenderOptions opt = optionsFor(s, outL);
    const int outRate = d.outEp.mixRate;
    d.renderer = makeRenderer(sources, d.capCh, outL, outRate, opt, err);
    if (!d.renderer) { d.capStop = true; d.capThread.join(); return false; }
    d.scene = makeScene(sources, d.capCh, outL, opt);
    d.scene.inIds = ids;
    d.scene.inputName = d.cableOut.name;
    d.scene.outputName = d.outEp.name;
    d.scene.plan = d.renderer->describe();

    d.queue.init(d.capCh);
    d.stage.init(d.capCh, d.capRate, outRate, std::clamp(s.marginMs, 5, 200));
    d.marginMs = d.stage.controller().marginMs();
    d.lim.init(outRate, d.renderer->outChannels());
    setGainDb(s.gainDb);

    if (!s.keepDefault) {
        std::string e2;
        if (setDefaultEndpoint(d.cableIn.id, e2)) d.switched = true;
        else d.warning = e2 + " - choisissez CABLE Input comme sortie dans vos applications.";
    }

    const int outCh = d.renderer->outChannels();
    const uint32_t outMask = (uint32_t)(outL.mask() & 0xFFFFFFFFu);
    running_ = true;
    d.renderThread = std::thread([this, outCh, outMask, outRate] {
        auto& d = *d_;
        auto pull = [&](float* dst, size_t frames) -> size_t {
            d.incoming.clear();
            d.queue.drainTo(d.incoming);
            if (!d.incoming.empty()) d.stage.feed(d.incoming);
            d.inBuf.resize(frames * d.capCh);
            const bool sourceIdle = d.nowMs() - d.lastDataMs.load() > 100;
            d.idle = sourceIdle;
            d.stage.produce(d.inBuf.data(), frames, sourceIdle);
            Meters::feed(meters.in, d.inBuf.data(), frames, d.capCh);
            d.renderer->process(d.inBuf.data(), dst, frames);
            const float g = gain_.load();
            if (g != 1.f) for (size_t k = 0; k < frames * outCh; ++k) dst[k] *= g;
            d.lim.process(dst, frames);
            Meters::feed(meters.out, dst, frames, outCh);
            const auto& dc = d.stage.controller();
            d.lowMs = dc.lowMs(); d.marginMs = dc.marginMs(); d.driftPpm = dc.driftPpm();
            d.underruns = d.stage.underruns(); d.drops = d.stage.drops();
            return frames;
        };
        std::string e, mode;
        PlayOptions po; po.bufferMs = 20; po.exclusive = d.set.exclusive;
        bool first = true;
        auto pullAndReport = [&](float* dst, size_t frames) -> size_t {
            if (first) { first = false; std::lock_guard<std::mutex> lk(d.m); d.outputMode = mode; }
            return pull(dst, frames);
        };
        playWasapi(d.outEp.id, outCh, outMask, outRate, po, pullAndReport, d.stop, e, &mode);
        if (!e.empty()) d.setError(e);
        d.capStop = true;
        running_ = false;
    });
    return true;
}

void LiveEngine::stop() {
    if (!d_) return;
    auto& d = *d_;
    d.stop = true; d.capStop = true;
    if (d.renderThread.joinable()) d.renderThread.join();
    if (d.capThread.joinable()) d.capThread.join();
    if (d.switched) { std::string e; setDefaultEndpoint(d.outEp.id, e); d.switched = false; }
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
    const long long f = d.capturedFrames.load(), t = d.nowMs();
    if (t - d.lastT >= 1000) {
        const double hz = (f - d.lastFrames) * 1000.0 / (double)(t - d.lastT);
        d.inHz = d.inHz == 0 ? hz : d.inHz + 0.3 * (hz - d.inHz);
        d.lastFrames = f; d.lastT = t;
    }
    st.inHz = d.inHz;
    st.error = d.error; st.warning = d.warning;
    return st;
}

Scene LiveEngine::scene() const { return d_ ? d_->scene : Scene{}; }

// =============================================================================================
//  Lecteur de fichiers
// =============================================================================================
struct FilePlayer::Impl {
    Decoder dec;
    std::unique_ptr<Renderer> renderer;
    Limiter lim;
    Scene scene;
    std::thread th;
    std::atomic<bool> stop{ false };
    std::string info;
    mutable std::mutex m;
    std::string error;
    std::vector<float> inBuf;
    int rate = 48000;
};

FilePlayer::FilePlayer() = default;
FilePlayer::~FilePlayer() { close(); }

void FilePlayer::close() {
    if (!d_) return;
    d_->stop = true;
    if (d_->th.joinable()) d_->th.join();
    d_.reset();
    active_ = false; finished_ = false; paused_ = false; pos_ = 0; duration_ = 0;
    meters.reset();
}

bool FilePlayer::open(const std::string& path, const Settings& s, std::string& err) {
    close();
    d_ = std::make_unique<Impl>();
    auto& d = *d_;
    Endpoint outEp;
    if (!resolveOutput(s, outEp, err)) return false;
    const Layout outL = layoutFor(s, outEp);
    if (!d.dec.open(path, outEp.mixRate, err)) return false;
    d.rate = d.dec.sampleRate();
    std::vector<VirtualSource> sources;
    if (d.dec.ambisonic() && ambisonicOrderForChannels(d.dec.channels()) > 0)
        sources = sourcesFromAmbisonics(d.dec.channels(), ambisonicOrderForChannels(d.dec.channels()));
    else
        sources = sourcesFromChannels(d.dec.channelIds());
    const RenderOptions opt = optionsFor(s, outL);
    d.renderer = makeRenderer(sources, d.dec.channels(), outL, d.rate, opt, err);
    if (!d.renderer) return false;
    d.scene = makeScene(sources, d.dec.channels(), outL, opt);
    if (!(d.dec.ambisonic() && ambisonicOrderForChannels(d.dec.channels()) > 0)) d.scene.inIds = d.dec.channelIds();
    d.scene.inputName = d.dec.info();
    d.scene.outputName = outEp.name;
    d.scene.plan = d.renderer->describe();
    d.info = d.dec.info();
    duration_ = d.dec.durationSec();
    const int outCh = d.renderer->outChannels(), inCh = d.dec.channels();
    d.lim.init(d.rate, outCh);
    setGainDb(s.gainDb);
    paused_ = false; finished_ = false; pos_ = 0;
    active_ = true;
    const uint32_t mask = (uint32_t)(outL.mask() & 0xFFFFFFFFu);
    const std::wstring outId = outEp.id;
    d.th = std::thread([this, outCh, inCh, mask, outId] {
        auto& d = *d_;
        // Fondu de 5 ms à chaque pause, reprise ou déplacement : pas de clic
        const int F = std::max(32, d.rate / 200);
        float fade = 0.f;                 // gain courant du fondu
        auto pull = [&](float* dst, size_t frames) -> size_t {
            std::fill(dst, dst + frames * outCh, 0.f);
            const double sk = seekTo_.load();
            const bool silence = paused_ || finished_ || sk >= 0;
            if (silence && fade <= 0.f) {
                if (sk >= 0) { seekTo_ = -1; d.dec.seek(sk); pos_ = sk; finished_ = false; }
                return frames;
            }
            // en fondu de sortie, on ne lit que ce qui est audible
            const size_t want = silence ? std::min<size_t>(frames, (size_t)F) : frames;
            size_t produced = 0;
            while (produced < want) {
                const size_t chunk = std::min<size_t>(want - produced, 2048);
                d.inBuf.resize(chunk * inCh);
                const size_t got = d.dec.read(d.inBuf.data(), chunk);
                if (!got) { finished_ = true; paused_ = true; break; }
                Meters::feed(meters.in, d.inBuf.data(), got, inCh);
                float* o = dst + produced * outCh;
                d.renderer->process(d.inBuf.data(), o, got);
                produced += got;
            }
            const float target = silence ? 0.f : 1.f, step = 1.f / F;
            const float g = gain_.load();
            for (size_t i = 0; i < frames; ++i) {
                if (fade < target) fade = std::min(target, fade + step);
                else if (fade > target) fade = std::max(target, fade - step);
                const float k = fade * g;
                for (int c = 0; c < outCh; ++c) dst[i * outCh + c] *= k;
            }
            d.lim.process(dst, frames);
            Meters::feed(meters.out, dst, produced, outCh);
            pos_ = pos_.load() + (double)produced / d.rate;
            return frames;
        };
        std::string e;
        PlayOptions po; po.bufferMs = 100;
        playWasapi(outId, outCh, mask, d.rate, po, pull, d.stop, e);
        if (!e.empty()) { std::lock_guard<std::mutex> lk(d.m); d.error = e; }
        active_ = false;
    });
    return true;
}

std::string FilePlayer::info() const { return d_ ? d_->info : std::string(); }
std::string FilePlayer::error() const { if (!d_) return {}; std::lock_guard<std::mutex> lk(d_->m); return d_->error; }
Scene FilePlayer::scene() const { return d_ ? d_->scene : Scene{}; }

} // namespace s3d
