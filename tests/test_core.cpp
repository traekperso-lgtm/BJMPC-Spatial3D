// Tests du moteur Spatial3D (sans FFmpeg ni Windows)
#include "layout.h"
#include "renderer.h"
#include "sources.h"
#include "vbap.h"
#include "live_core.h"
#include "dsp.h"
#include "convolver.h"
#include "hrtf_builtin.h"
#include "wav_reader.h"
#include "wav_writer.h"
#include <random>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

using namespace s3d;
static const double M_PI_F = 3.14159265358979323846;
static int fails = 0, checks = 0;
#define CHECK(c, msg) do { ++checks; if (!(c)) { ++fails; printf("ECHEC : %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static Layout L(const char* n) { Layout l; layoutFromName(n, l); return l; }
static float m(const std::vector<float>& M, int in, int o, int i) { return M[(size_t)o * in + i]; }
static std::vector<int> ids(const Layout& l) { std::vector<int> v; for (auto& s : l.speakers) v.push_back(s.id); return v; }

static void testIdentity() {
    for (const char* n : { "2.0", "5.1", "7.1", "7.1.4", "5.1.2" }) {
        Layout l = L(n);
        auto src = sourcesFromChannels(ids(l));
        const int c = (int)l.speakers.size();
        auto M = buildSpeakerMatrix(src, c, l, {});
        bool ok = true;
        for (int o = 0; o < c; ++o) for (int i = 0; i < c; ++i)
            if (std::fabs(m(M, c, o, i) - (o == i ? 1.f : 0.f)) > 1e-4f) ok = false;
        char b[64]; snprintf(b, sizeof b, "%s -> %s doit etre l'identite", n, n);
        CHECK(ok, b);
    }
}

static void testDownmix51to20() {
    Layout in = L("5.1"), out = L("2.0");
    auto src = sourcesFromChannels(ids(in));
    auto M = buildSpeakerMatrix(src, 6, out, {});
    const int FL = 0, FR = 1, FC = 2, LFE = 3, SL = 4, SR = 5; // ordre des bits
    CHECK(std::fabs(m(M, 6, 0, FC) - 0.7071f) < 1e-3f && std::fabs(m(M, 6, 1, FC) - 0.7071f) < 1e-3f, "Centre -> G/D a -3 dB");
    CHECK(m(M, 6, 0, SL) > 0.99f && m(M, 6, 1, SL) < 1e-4f, "Cote G -> Avant G");
    CHECK(m(M, 6, 1, SR) > 0.99f && m(M, 6, 0, SR) < 1e-4f, "Cote D -> Avant D");
    CHECK(std::fabs(m(M, 6, 0, LFE) - 0.5f) < 1e-4f, "LFE -> principales a -6 dB");
    CHECK(m(M, 6, 0, FL) > 0.99f, "Avant G conserve");
    (void)FR;
}

static void test71to51() {
    Layout in = L("7.1"), out = L("5.1");
    auto src = sourcesFromChannels(ids(in));
    auto M = buildSpeakerMatrix(src, 8, out, {});
    // entrée 7.1 : FL FR FC LFE BL BR SL SR (ordre des bits) ; sortie 5.1 : FL FR FC LFE SL SR
    const int inSL = 6, inBL = 4;
    CHECK(m(M, 8, 0, inSL) > 0.1f && m(M, 8, 4, inSL) > 0.5f, "Cote 7.1 (90) entre Avant G et Cote G 5.1 (110)");
    CHECK(m(M, 8, 4, inBL) > m(M, 8, 5, inBL) && m(M, 8, 5, inBL) > 0.f, "Arriere G 7.1 (135) surtout sur Cote G, un peu sur Cote D");
}

static void testHeights() {
    Layout l = L("7.1.4");
    Panner p(l);
    CHECK(p.is3D(), "7.1.4 doit etre en VBAP 3D");
    auto g = p.gains(0, 90);
    const int tfl = l.indexOf(CH_TFL), tfr = l.indexOf(CH_TFR), tbl = l.indexOf(CH_TBL), tbr = l.indexOf(CH_TBR);
    double top = g[tfl] * g[tfl] + g[tfr] * g[tfr] + g[tbl] * g[tbl] + g[tbr] * g[tbr];
    CHECK(top > 0.99, "Zenith -> uniquement les enceintes du haut");
    auto g2 = p.gains(0, -40);
    CHECK(g2[l.indexOf(CH_FC)] > 0.8 && g2[tfl] < 1e-6 && g2[tbl] < 1e-6,
          "Source sous l'horizon devant -> Centre dominant, rien en hauteur (HP imaginaire)");
    // balayage : puissance 1, pas de NaN
    bool ok = true;
    for (int az = -180; az < 180; az += 7) for (int el = -90; el <= 90; el += 9) {
        auto gg = p.gains(az, el, 0);
        double pw = 0; for (double v : gg) { if (!(v >= 0) || std::isnan(v)) ok = false; pw += v * v; }
        if (std::fabs(pw - 1) > 1e-6) ok = false;
    }
    CHECK(ok, "Balayage 7.1.4 : gains positifs, puissance constante");
    Layout l5 = L("5.1");
    Panner p5(l5);
    auto g3 = p5.gains(45, 45);
    CHECK(g3[l5.indexOf(CH_FL)] > 0.9 && g3[l5.indexOf(CH_SL)] > 0.1, "Haut avant G sur 5.1 -> entre Avant G et Cote G");
    auto g4 = p5.gains(20, 0, 60);
    int lit = 0; for (double v : g4) if (v > 0.05) ++lit;
    CHECK(lit >= 3, "Etalement 60 deg : plusieurs enceintes actives");
}

static void testAmbisonics() {
    Layout l = L("7.1.4");
    auto src = sourcesFromAmbisonics(4, 1);
    auto M = buildSpeakerMatrix(src, 4, l, {});
    auto loudest = [&](double az, double el) {
        const double a = az * 3.14159265358979 / 180, e = el * 3.14159265358979 / 180;
        const double b[4] = { 1, std::sin(a) * std::cos(e), std::sin(e), std::cos(a) * std::cos(e) };
        int best = -1; double bv = -1;
        for (size_t o = 0; o < l.speakers.size(); ++o) {
            double v = 0; for (int c = 0; c < 4; ++c) v += m(M, 4, (int)o, c) * b[c];
            if (v > bv) { bv = v; best = (int)o; }
        }
        return l.speakers[best].id;
    };
    CHECK(loudest(90, 0) == CH_SL, "FOA az 90 -> Cote G le plus fort");
    CHECK(loudest(0, 0) == CH_FC, "FOA az 0 -> Centre le plus fort");
    CHECK(loudest(-135, 0) == CH_BR, "FOA az -135 -> Arriere D le plus fort");
    CHECK(ambisonicOrderForChannels(4) == 1 && ambisonicOrderForChannels(9) == 2 && ambisonicOrderForChannels(6) == -1, "Ordres ambisoniques");
}

static void testBinaural() {
    const double fs = 48000;
    auto check = [&](double az, double& ildDb, int& lag) {
        VirtualSource s; s.az = az; s.el = 0; s.w = { 1.f }; s.label = "t";
        Layout hp = L("casque");
        RenderOptions o; o.room = 0;
        std::string err;
        auto r = makeRenderer({ s }, 1, hp, fs, o, err);
        const size_t N = 48000;
        std::vector<float> in(N), out(2 * N);
        unsigned x = 1;
        for (auto& v : in) { x = x * 1664525u + 1013904223u; v = ((x >> 8) / 16777216.f - 0.5f); }
        r->process(in.data(), out.data(), N);
        double el = 0, er = 0;
        for (size_t n = 0; n < N; ++n) { el += out[2 * n] * out[2 * n]; er += out[2 * n + 1] * out[2 * n + 1]; }
        ildDb = 10 * std::log10(el / er);
        double best = -1e30; lag = 0;
        for (int L2 = -40; L2 <= 40; ++L2) {
            double c = 0;
            for (size_t n = 100; n < N - 100; ++n) c += out[2 * n] * out[2 * (n + L2) + 1];
            if (c > best) { best = c; lag = L2; }
        }
    };
    double ild; int lag;
    check(90, ild, lag);
    printf("  casque az 90 : ILD %.1f dB, ITD %d ech (%.2f ms)\n", ild, lag, lag / 48.0);
    CHECK(ild > 3, "Source a gauche : oreille gauche plus forte");
    CHECK(lag > 15, "Source a gauche : l'oreille droite entend en retard (ITD)");
    check(0, ild, lag);
    CHECK(std::fabs(ild) < 0.2 && lag == 0, "Source devant : symetrique");
    check(-120, ild, lag);
    CHECK(ild < -3 && lag < -10, "Source arriere droite : oreille droite plus forte et en avance");
}


// Simulation : VB-Cable (48 kHz +150 ppm) -> carte réelle 48 kHz, 10 minutes.
// `bursty` : paquets irréguliers de 0 à 40 ms (comportement d'une carte virtuelle).
static void driftCase(bool bursty, double outRate) {
    const double inRate = 48000 * (1 + 150e-6);
    AdaptiveResampler rs; rs.init(2);
    DriftController dc; dc.init(48000, outRate);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jit(-0.003, 0.003), burst(0.0, 0.040);
    double tIn = 0, tOut = 0, phase = 0, nextBurst = 0;
    int underrunsAfter = 0, underruns = 0; bool priming = true;
    std::vector<float> chunk, out(480 * 2);
    float prev = 0; double maxStep = 0, ppmSum = 0, maxPpm = 0; long ppmN = 0;
    const double f = 997.0;
    const size_t blk = (size_t)(outRate / 100);
    while (tOut < 600) {
        double tNext = bursty ? nextBurst : tIn + jit(rng);
        if (tNext < tOut) {
            // production : tout ce qui s'est accumulé depuis le dernier paquet
            const double until = bursty ? nextBurst : tIn + 0.010;
            const size_t n = (size_t)std::max(0.0, std::round((until - tIn) * inRate));
            chunk.resize(n * 2);
            for (size_t k = 0; k < n; ++k) { const float v = (float)std::sin(phase); phase += 2 * M_PI_F * f / inRate; chunk[2 * k] = v; chunk[2 * k + 1] = v; }
            rs.push(chunk);
            tIn += n / inRate;
            if (bursty) nextBurst += burst(rng);
        } else {
            size_t got = blk;
            if (priming) { std::fill(out.begin(), out.end(), 0.f); if (rs.available() >= dc.primeFrames()) priming = false; }
            else {
                got = rs.pull(out.data(), blk, dc.ratio());
                if (got < blk) { ++underruns; if (tOut > 60) ++underrunsAfter; dc.onUnderrun(); rs.reset(); priming = true; }
                dc.update(rs.available(), blk);
            }
            if (tOut > 120) { ppmSum += dc.driftPpm(); ++ppmN; maxPpm = std::max(maxPpm, std::fabs(dc.ppm())); }
            if (tOut > 120 && !priming) {
                for (size_t n = 0; n < got; ++n) { maxStep = std::max(maxStep, (double)std::fabs(out[2 * n] - prev)); prev = out[2 * n]; }
            } else if (got) prev = out[2 * (got - 1)];
            tOut += blk / outRate;
        }
    }
    printf("  %s, sortie %.0f Hz : %d coupures (dont %d apres 1 min), marge finale %.0f ms, derive estimee %.0f ppm, correction max %.0f ppm\n",
           bursty ? "paquets irreguliers" : "paquets reguliers", outRate, underruns, underrunsAfter, dc.marginMs(), ppmSum / ppmN, maxPpm);
    CHECK(underrunsAfter == 0, "Temps reel : plus aucune coupure une fois la marge ajustee");
    CHECK(std::fabs(ppmSum / ppmN - 150) < 30, "Temps reel : la derive d'horloge est retrouvee");
    CHECK(maxPpm <= 2000.5, "Temps reel : correction plafonnee a 0,2 % (inaudible)");
    CHECK(maxStep < 2 * M_PI_F * f / outRate * 1.05, "Temps reel : signal continu (pas de craquement)");
}
static void testDrift() {
    driftCase(false, 44100);
    driftCase(true, 48000);
}

// Limiteur à anticipation : jamais au-dessus du seuil, gain qui varie en douceur
static void testLimiter() {
    const int fs = 48000, n = fs * 2;
    std::vector<float> x(n * 2);
    std::mt19937 rng(3); std::uniform_real_distribution<float> u(-1, 1);
    for (int i = 0; i < n; ++i) {
        const float burst = (i / 4800) % 3 == 1 ? 4.f : 0.5f;     // salves à +12 dB
        const float v = burst * std::sin(2 * (float)M_PI_F * 440 * i / fs) + 0.05f * u(rng);
        x[2 * i] = v; x[2 * i + 1] = -v;
    }
    std::vector<float> in = x;
    Limiter lim; lim.init(fs, 2, 0.97f);
    lim.process(x.data(), n);
    float peak = 0; double maxGainStep = 0, prevG = 1; int prevI = 0;
    const int D = lim.latencyFrames();   // latence d'anticipation
    for (int i = D; i < n; ++i) {
        peak = std::max(peak, std::fabs(x[2 * i]));
        if (std::fabs(in[2 * (i - D)]) > 0.2f) {
            const double g = x[2 * i] / in[2 * (i - D)];
            if (prevI) maxGainStep = std::max(maxGainStep, std::fabs(g - prevG) / (i - prevI));   // variation par echantillon
            prevG = g; prevI = i;
        }
    }
    printf("  limiteur : crete %.4f (seuil 0.97), reduction max %.1f dB, variation de gain max %.4f / echantillon\n", peak, lim.reductionDb(), maxGainStep);
    CHECK(peak <= 0.9701f, "Limiteur : aucune crete au-dessus du seuil");
    CHECK(maxGainStep < 0.012, "Limiteur : gain sans saut brutal (pas de clic)");
}

// Étage temps réel : coupures et débordements doivent rester sans clic
static void testLiveStage() {
    const double inRate = 48000 * (1 + 80e-6), outRate = 48000, f = 997;
    LiveInputStage st; st.init(1, 48000, outRate, 10);
    std::mt19937 rng(11); std::uniform_real_distribution<double> burst(0.0, 0.030);
    double tIn = 0, tOut = 0, phase = 0, nextIn = 0;
    std::vector<float> chunk, out(480);
    float prev = 0; double maxStep = 0;
    int stalls = 0, floods = 0;
    while (tOut < 300) {
        if (nextIn < tOut) {
            double until = nextIn;
            // toutes les 37 s : la source cale 80 ms (coupure) ; toutes les 53 s : elle livre 400 ms d'un coup
            if (std::fmod(nextIn, 37.0) < 0.03 && nextIn > 1) { nextIn += 0.080; ++stalls; }
            if (std::fmod(nextIn, 53.0) < 0.03 && nextIn > 1) { until += 0.400; ++floods; }
            const size_t n = (size_t)std::max(0.0, std::round((until - tIn) * inRate));
            chunk.resize(n);
            for (size_t k = 0; k < n; ++k) { chunk[k] = (float)std::sin(phase); phase += 2 * M_PI_F * f / inRate; }
            st.feed(chunk.data(), n);
            tIn += n / inRate;
            nextIn = std::max(nextIn, tIn) + burst(rng);
        } else {
            st.produce(out.data(), 480, false);
            for (float v : out) { maxStep = std::max(maxStep, (double)std::fabs(v - prev)); prev = v; }
            tOut += 480 / outRate;
        }
    }
    const double natural = 2 * M_PI_F * f / outRate;
    printf("  etage temps reel : %d calages, %d afflux -> %d coupures, %d sauts ; plus grand ecart entre echantillons %.3f (sinus seul : %.3f)\n",
           stalls, floods, st.underruns(), st.drops(), maxStep, natural);
    CHECK(st.underruns() > 0 && st.drops() > 0, "Etage temps reel : le scenario provoque bien coupures et sauts");
    CHECK(maxStep < natural * 1.15, "Etage temps reel : coupures et sauts sans clic (fondus)");
}

// Anneau capture -> rendu : sans verrou, ordre et contenu préservés entre deux fils
static void testSpscRing() {
    SpscRing r; r.init(4096);
    const size_t total = 2000000;
    std::atomic<bool> okOrder{ true };
    std::thread prod([&] {
        std::vector<float> buf(333); size_t v = 0;
        while (v < total) {
            const size_t n = std::min(buf.size(), total - v);
            for (size_t k = 0; k < n; ++k) buf[k] = (float)((v + k) % 65536);
            size_t done = 0;
            while (done < n) { done += r.push(buf.data() + done, n - done); if (done < n) std::this_thread::yield(); }
            v += n;
        }
    });
    std::vector<float> out(517); size_t got = 0;
    while (got < total) {
        const size_t n = r.pop(out.data(), out.size());
        for (size_t k = 0; k < n; ++k) if (out[k] != (float)((got + k) % 65536)) okOrder = false;
        got += n;
        if (!n) std::this_thread::yield();
    }
    prod.join();
    printf("  anneau sans verrou : %zu echantillons transferes entre deux fils\n", got);
    CHECK(okOrder && got == total && r.available() == 0, "Anneau SPSC : flux intact entre capture et rendu");
}

// Convolution partitionnée : identique à la convolution directe, latence d'un seul bloc
static void testConvolver() {
    std::mt19937 rng(5); std::uniform_real_distribution<float> u(-1, 1);
    const int S = 3, N = 300, B = 128, blocks = 40;
    std::vector<std::vector<float>> hl(S, std::vector<float>(N)), hr(S, std::vector<float>(N - 50));
    for (auto& h : hl) for (auto& v : h) v = u(rng) * 0.2f;
    for (auto& h : hr) for (auto& v : h) v = u(rng) * 0.2f;
    std::vector<std::vector<float>> x(S, std::vector<float>(B * blocks));
    for (auto& s : x) for (auto& v : s) v = u(rng);
    PartitionedConvolver pc; pc.init(hl, hr, B);
    std::vector<float> out(2 * B * blocks, 0.f);
    for (int b = 0; b < blocks; ++b) {
        const float* in[S]; for (int s = 0; s < S; ++s) in[s] = x[s].data() + b * B;
        pc.processBlock(in, out.data() + 2 * b * B);
    }
    double maxErr = 0;
    for (int n = 0; n < B * blocks; ++n) {
        double l = 0, r = 0;
        for (int s = 0; s < S; ++s) {
            for (int k = 0; k < N && k <= n; ++k) l += hl[s][k] * x[s][n - k];
            for (int k = 0; k < N - 50 && k <= n; ++k) r += hr[s][k] * x[s][n - k];
        }
        maxErr = std::max(maxErr, std::max(std::fabs(l - out[2 * n]), std::fabs(r - out[2 * n + 1])));
    }
    printf("  convolution partitionnee : %d tranches de %d, ecart max avec la convolution directe %.2e\n", pc.partitions(), B, maxErr);
    CHECK(maxErr < 1e-4, "Convolution partitionnee identique a la convolution directe");
    CHECK(pc.partitions() == 3, "Convolution : reponse decoupee en tranches de 128");
}

// Lecteur WAV : aller-retour avec l'écrivain (flottant 32 bits, masque 5.1)
static void testWavRoundTrip() {
    // fichier temporaire dans le dossier courant (portable Windows / Linux)
    const std::string pathS = "s3d_test_51.wav";
    const char* path = pathS.c_str();
    std::vector<float> x(6 * 1000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = std::sin(0.01f * i) * 0.5f;
    { WavWriter w; w.open(path, 6, 44100, 0x60F); w.write(x.data(), 1000); w.close(); }
    WavReader r; std::string err;
    CHECK(r.open(path, err), "WAV : ouverture");
    CHECK(r.channels() == 6 && r.sampleRate() == 44100, "WAV : canaux et frequence");
    CHECK(r.channelIds().size() == 6 && r.channelIds()[4] == CH_SL && r.channelIds()[5] == CH_SR, "WAV : disposition 5.1 lue dans le masque");
    std::vector<float> y(6 * 1000);
    size_t got = r.read(y.data(), 2000);
    bool same = got == 1000;
    for (size_t i = 0; same && i < x.size(); ++i) same = x[i] == y[i];
    CHECK(same, "WAV : echantillons relus a l'identique");
}

// Enceintes déplacées à la souris : réglage relu, matrice saine, source envoyée à la bonne enceinte
static void testSpeakerOverrides() {
    const std::string spec = formatSpeakerOverrides({ { "5.1", { { CH_FL, { 45, 2.0 } }, { CH_FR, { -45 } }, { CH_SL, { 100, 1.2 } } } } });
    auto o = parseSpeakerOverrides(spec);
    CHECK(o.size() == 1 && o["5.1"].size() == 3 && o["5.1"][CH_SL].az == 100 && std::fabs(o["5.1"][CH_SL].dist - 1.2) < 1e-9 && o["5.1"][CH_FR].dist == kDefaultSpeakerDist,
          "Positions : texte relu a l'identique (angles et distances)");
    Layout l; layoutFromName("5.1", l);
    applySpeakerOverrides(l, spec);
    CHECK(l.speakers[l.indexOf(CH_FL)].az == 45 && l.speakers[l.indexOf(CH_FR)].az == -45, "Positions : azimuts appliques");
    CHECK(!speakerMoveAllowed(l, l.indexOf(CH_FC), 43), "Positions : deux enceintes au meme endroit refusees");
    // une source à 45° tombe entièrement sur l'avant gauche déplacé à 45°
    auto src = sourcesFromChannels({ CH_FL });
    src[0].az = 45; src[0].el = 0;
    auto M = buildSpeakerMatrix(src, 1, l, {});
    CHECK(std::fabs(M[l.indexOf(CH_FL)] - 1.f) < 1e-3f, "Positions : source sur l'enceinte deplacee");
    // canaux 5.1 vers ce 5.1 asymétrique : gains finis, puissance conservée
    Layout in51; layoutFromName("5.1", in51);
    auto s51 = sourcesFromChannels(ids(in51));
    auto M2 = buildSpeakerMatrix(s51, 6, l, {});
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        if (s51[i].lfe) continue;
        double p = 0;
        for (int k = 0; k < 6; ++k) { const float g = M2[k * 6 + i]; if (!std::isfinite(g)) ok = false; p += g * g; }
        if (std::fabs(p - 1.0) > 0.02) ok = false;
    }
    CHECK(ok, "Positions : puissance conservee sur une disposition deplacee");
    // distances : l'enceinte la plus proche (SL 1,2 m) retardée et atténuée par rapport à la plus loin (2,5 m)
    auto al = computeDistanceAlignment(l);
    const int sl = l.indexOf(CH_SL), fr = l.indexOf(CH_FR);
    CHECK(al.active && std::fabs(al.delaySec[sl] - 1.3 / 343) < 1e-6 && al.delaySec[fr] == 0 &&
          std::fabs(al.gain[sl] - 1.2 / 2.5) < 1e-6, "Distances : retard et niveau compenses comme un ampli");
    Layout hp; layoutFromName("casque", hp);
    applySpeakerOverrides(hp, spec);
    CHECK(hp.headphones, "Positions : sans effet au casque");
}

// HRTF KEMAR intégrée : présente, symétrique de face, oreille proche plus forte sur le côté
static void testBuiltinHrtf() {
    CHECK(builtinHrirCount() == 710, "HRTF integree : 710 directions mesurees");
    std::vector<float> L, R;
    double gaz = 0, gel = 0;
    CHECK(builtinHrir(0, 0, 48000, L, R, &gaz, &gel) && gaz == 0 && gel == 0, "HRTF integree : direction de face trouvee");
    double el2 = 0, d2 = 0;
    for (size_t i = 0; i < L.size(); ++i) { el2 += L[i] * L[i]; d2 += (L[i] - R[i]) * (L[i] - R[i]); }
    CHECK(d2 < el2 * 0.05, "HRTF integree : face quasi symetrique");
    builtinHrir(90, 0, 48000, L, R);
    double eL = 0, eR = 0; for (size_t i = 0; i < L.size(); ++i) { eL += L[i] * L[i]; eR += R[i] * R[i]; }
    printf("  HRTF KEMAR : %zu points a 48 kHz, ILD large bande a 90 deg %.1f dB\n", L.size(), 10 * std::log10(eL / eR));
    CHECK(eL > eR * 4, "HRTF integree : oreille gauche dominante pour une source a gauche");
    std::vector<float> L44, R44;
    builtinHrir(90, 0, 44100, L44, R44);
    double s48 = 0, s44 = 0; for (float v : L) s48 += v; for (float v : L44) s44 += v;
    CHECK(std::fabs(L44.size() - L.size() * 44100.0 / 48000) <= 2 && std::fabs(s44 - s48) < 0.02 * std::fabs(s48) + 1e-3,
          "HRTF integree : reechantillonnage 44,1 kHz (longueur et gain DC)");
}

// Alignement des distances : retard exact, puis changement de réglage en fondu sans clic
static void testDistanceAligner() {
    const double fs = 48000;
    DistanceAligner a; a.init(2, fs);
    a.setTargets({ 0.001, 0.0 }, { 0.5, 1.0 });            // canal 0 : 48 éch. de retard, -6 dB
    std::vector<float> x(2 * 4800, 0.f);
    x[2 * 100] = 1.f; x[2 * 100 + 1] = 1.f;                 // impulsion (après le fondu initial ? non : 100 < 480)
    a.process(x.data(), 4800);
    // le premier réglage passe aussi par le fondu : on vérifie l'état final sur un second passage
    std::vector<float> y(2 * 4800, 0.f);
    y[2 * 1000] = 1.f; y[2 * 1000 + 1] = 1.f;
    a.process(y.data(), 4800);
    CHECK(std::fabs(y[2 * 1048] - 0.5f) < 1e-6f && std::fabs(y[2 * 1000 + 1] - 1.f) < 1e-6f && std::fabs(y[2 * 1000]) < 1e-6f,
          "Distances : retard de 48 echantillons et -6 dB exacts");
    // sinus continu, retard qui change en cours de route : pas de saut d'un échantillon à l'autre
    DistanceAligner b; b.init(1, fs);
    std::vector<float> s(48000);
    for (size_t n = 0; n < s.size(); ++n) s[n] = (float)std::sin(2 * M_PI_F * 440 * n / fs);
    float maxStep = 0, prev = 0;
    for (size_t off = 0; off < s.size(); off += 480) {
        if (off == 9600) b.setTargets({ 0.0042 }, { 0.8 });
        if (off == 24000) b.setTargets({ 0.0 }, { 1.0 });
        b.process(&s[off], 480);
        for (size_t n = off; n < off + 480; ++n) { if (n > 0) maxStep = std::max(maxStep, std::fabs(s[n] - prev)); prev = s[n]; }
    }
    printf("  alignement : pas maximal %.4f (sinus 440 Hz : %.4f)\n", maxStep, (float)(2 * M_PI_F * 440 / fs));
    CHECK(maxStep < 0.075f, "Distances : reglage modifie en pleine lecture sans clic");
}

// Rééchantillonneur : fidélité mesurée (sinus pur, rapport non entier comme une dérive d'horloge)
static double resamplerSnr(double freq, double ratio, double* gainDb) {
    AdaptiveResampler rs; rs.init(1, 400000); rs.setNominalRatio(ratio);
    const double fs = 48000;
    std::vector<float> x(96000);
    for (size_t n = 0; n < x.size(); ++n) x[n] = (float)(0.5 * std::sin(2 * M_PI_F * freq * n / fs));
    rs.push(x.data(), x.size());
    std::vector<float> y(80000);
    const size_t got = rs.pull(y.data(), y.size(), ratio);
    // ajustement d'un sinus à la fréquence attendue sur la partie stable, résidu = bruit + distorsion
    const double w = 2 * M_PI_F * freq * ratio / fs;
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    const size_t a = 2000, b = std::min(got, (size_t)70000);
    for (size_t n = a; n < b; ++n) {
        const double sn = std::sin(w * n), cn = std::cos(w * n);
        ss += sn * sn; cc += cn * cn; sc += sn * cn; ys += y[n] * sn; yc += y[n] * cn;
    }
    const double det = ss * cc - sc * sc;
    const double A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    double sig = 0, err = 0;
    for (size_t n = a; n < b; ++n) {
        const double fit = A * std::sin(w * n) + B * std::cos(w * n);
        sig += fit * fit; err += (y[n] - fit) * (y[n] - fit);
    }
    if (gainDb) *gainDb = 20 * std::log10(std::sqrt(A * A + B * B) / 0.5);
    return 10 * std::log10(sig / std::max(err, 1e-30));
}

static void testResamplerFidelity() {
    double g10 = 0, g18 = 0, g1 = 0;
    const double s1 = resamplerSnr(1000, 1.00021, &g1);
    const double s10 = resamplerSnr(10000, 1.00021, &g10);
    const double s18 = resamplerSnr(18000, 0.99987, &g18);
    printf("  reechantillonneur : RSB 1 kHz %.0f dB, 10 kHz %.0f dB, 18 kHz %.0f dB ; gain 10 kHz %+.3f dB, 18 kHz %+.3f dB\n",
           s1, s10, s18, g10, g18);
    CHECK(s1 > 80 && s10 > 80 && s18 > 70, "Reechantillonneur : rapport signal/bruit > 80 dB (10 kHz)");
    CHECK(std::fabs(g1) < 0.01 && std::fabs(g10) < 0.05 && std::fabs(g18) < 0.2, "Reechantillonneur : bande passante plate jusqu'a 18 kHz");
}

// HRTF interpolée : l'écart de temps entre oreilles varie sans à-coup d'un degré à l'autre
static void testHrtfInterpolation() {
    auto onset = [](const std::vector<float>& x) {
        float pk = 0; for (float v : x) pk = std::max(pk, std::fabs(v));
        for (size_t i = 0; i < x.size(); ++i) if (std::fabs(x[i]) >= 0.1f * pk) return (int)i;
        return 0;
    };
    std::vector<float> L, R;
    int prev = 0, maxJump = 0;
    bool mono = true;
    for (int az = 0; az <= 90; ++az) {
        builtinHrir(az, 0, 48000, L, R);
        const int itd = onset(R) - onset(L);
        if (az > 0) { maxJump = std::max(maxJump, std::abs(itd - prev)); if (itd < prev - 1) mono = false; }
        prev = itd;
    }
    builtinHrir(0, 45, 48000, L, R);                        // entre les anneaux 40° et 50° mesurés
    double e = 0; for (float v : L) e += v * v;
    printf("  HRTF interpolee : ITD de 0 a %d ech. sur 0-90 deg, saut max %d ech.\n", prev, maxJump);
    CHECK(mono && maxJump <= 2 && e > 0.01, "HRTF interpolee : ITD continue, hauteur 45 deg reconstruite");
}

// Marge : resserrement automatique sans coupure, +3 ms par coupure
static void testMarginTightening() {
    DriftController dc; dc.init(48000, 48000, 20);
    for (int k = 0; k < 100 * 300; ++k) dc.update(dc.marginFrames() + 200, 480);   // 5 min stables
    const double after = dc.marginMs();
    dc.onUnderrun();
    const double bumped = dc.marginMs();
    for (int k = 0; k < 100 * 300; ++k) dc.update(dc.marginFrames() + 200, 480);
    printf("  marge : 20 ms -> %.0f ms apres 5 min stables ; coupure -> %.0f ms ; 5 min plus tard %.0f ms\n", after, bumped, dc.marginMs());
    CHECK(after <= 16 && after >= 5 && std::fabs(bumped - after - 3) < 0.01 && dc.marginMs() > after,
          "Marge : se resserre seule et ne retombe pas sous le niveau d'une coupure recente");
}

// Casque : un haut-parleur virtuel rapproché sonne plus fort, plus loin plus faible
static void testVirtualDistance() {
    auto level = [](double dist) {
        auto src = sourcesFromChannels({ CH_FL });
        src[0].dist = dist;
        Layout hp; layoutFromName("casque", hp);
        RenderOptions o; o.room = 0.15; std::string e;
        auto r = makeRenderer(src, 1, hp, 48000, o, e);
        std::vector<float> x(48000, 0.f), y(96000);
        uint32_t seed = 12345;                               // bruit blanc reproductible
        for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = 0.3f * ((seed >> 8) / 8388608.f - 1.f); }
        r->process(x.data(), y.data(), 48000);
        double p = 0; for (size_t n = 24000; n < 96000; ++n) p += y[n] * y[n];
        return 10 * std::log10(p);
    };
    const double near = level(1.25), mid = level(2.5), far = level(5.0);
    printf("  casque, distance virtuelle : 1,25 m %+.1f dB, 5 m %+.1f dB (par rapport a 2,5 m)\n", near - mid, far - mid);
    CHECK(near - mid > 3 && far - mid < -3, "Casque : la distance d'un haut-parleur virtuel change son niveau");
}

int main() {
    testIdentity();
    testDownmix51to20();
    test71to51();
    testHeights();
    testAmbisonics();
    testBinaural();
    testDrift();
    testLimiter();
    testLiveStage();
    testSpscRing();
    testSpeakerOverrides();
    testBuiltinHrtf();
    testDistanceAligner();
    testResamplerFidelity();
    testHrtfInterpolation();
    testMarginTightening();
    testVirtualDistance();
    testConvolver();
    testWavRoundTrip();
    printf("%d/%d verifications reussies\n", checks - fails, checks);
    return fails ? 1 : 0;
}
