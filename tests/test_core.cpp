// Tests du moteur Spatial3D (sans FFmpeg ni Windows)
#include "layout.h"
#include "renderer.h"
#include "sources.h"
#include "vbap.h"
#include "live_core.h"
#include "dsp.h"
#include <random>
#include <cmath>
#include <cstdio>
#include <vector>

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
    const int D = 95;   // latence (2 ms - 1 trame)
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
            st.feed(chunk);
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
    printf("%d/%d verifications reussies\n", checks - fails, checks);
    return fails ? 1 : 0;
}
