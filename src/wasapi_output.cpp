#include "wasapi_output.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <propidl.h>
#include <avrt.h>
#include <endpointvolume.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace s3d {

// GUID et clés définis localement : identiques sous MSVC et MinGW, sans dépendre de initguid/uuid.lib
static const GUID kCLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID kIID_IMMDeviceEnumerator  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID kIID_IAudioClient         = { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const GUID kIID_IAudioClient3        = { 0x7ED4EE07, 0x8E67, 0x4CD4, { 0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42 } };
static const GUID kIID_IAudioEndpointVolume = { 0x5CDF2C82, 0x841E, 0x4546, { 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A } };
static const GUID kIID_IAudioEndpointVolumeCallback = { 0x657804FA, 0xD6AD, 0x4496, { 0x8A, 0x60, 0x35, 0x27, 0x52, 0xAF, 0x4F, 0x89 } };
static const GUID kIID_IUnknownS3D = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID kIID_IAudioRenderClient   = { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
static const GUID kIID_IAudioCaptureClient  = { 0xC8ADBD64, 0xE71E, 0x48A0, { 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17 } };
static const GUID kSUBTYPE_IEEE_FLOAT       = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID kSUBTYPE_PCM              = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID kCLSID_PolicyConfigClient = { 0x870AF99C, 0x171D, 0x4F9E, { 0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9 } };
static const GUID kIID_IPolicyConfig        = { 0xF8679F50, 0x850A, 0x41CF, { 0x9C, 0x72, 0x43, 0x0F, 0x29, 0x02, 0x90, 0xC8 } };
static const PROPERTYKEY kPKEY_FriendlyName     = { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };
static const PROPERTYKEY kPKEY_FormFactor       = { { 0x1DA5D803, 0xD492, 0x4EDD, { 0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E } }, 0 };
static const PROPERTYKEY kPKEY_PhysicalSpeakers = { { 0x1DA5D803, 0xD492, 0x4EDD, { 0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E } }, 3 };

// Interface non documentée mais stable depuis Windows 7 (utilisée par le Panneau de configuration Son,
// EarTrumpet, SoundSwitch…) : format d'un périphérique et périphérique par défaut.
struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

// IAudioClient3 (Windows 10+) déclarée localement : en-têtes MinGW anciens et MSVC identiques.
// Vtable : IAudioClient, puis IAudioClient2 (3 méthodes), puis IAudioClient3 (3 méthodes).
struct S3DAudioClient3 : public IAudioClient {
    virtual HRESULT STDMETHODCALLTYPE IsOffloadCapable(int, BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClientProperties(const void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBufferSizeLimits(const WAVEFORMATEX*, BOOL, REFERENCE_TIME*, REFERENCE_TIME*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSharedModeEnginePeriod(const WAVEFORMATEX*, UINT32*, UINT32*, UINT32*, UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSharedModeEnginePeriod(WAVEFORMATEX**, UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitializeSharedAudioStream(DWORD, UINT32, const WAVEFORMATEX*, LPCGUID) = 0;
};

template <class T> struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct ComInit {
    bool ok;
    ComInit() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComInit() { if (ok) CoUninitialize(); }
};

// Priorité « Pro Audio » du planificateur multimédia de Windows (MMCSS) pour les fils audio :
// évite qu'un jeu qui charge le processeur retarde le traitement (cause classique de pops).
struct MmcssScope {
    HANDLE h = nullptr;
    MmcssScope() { DWORD idx = 0; h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &idx); }
    ~MmcssScope() { if (h) AvRevertMmThreadCharacteristics(h); }
};

static std::string hr2s(const char* what, HRESULT hr) {
    char b[200]; snprintf(b, sizeof b, "%s (HRESULT 0x%08lX)", what, (unsigned long)hr); return b;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static uint32_t defaultMask(int ch) {
    switch (ch) {
    case 1: return 0x4; case 2: return 0x3; case 3: return 0x7; case 4: return 0x33;
    case 6: return 0x60F; case 8: return 0x63F; case 10: return 0x5063F; case 12: return 0x2D63F;
    default: return 0;
    }
}

bool Endpoint::isVbCable() const {
    return name.find("VB-Audio Virtual Cable") != std::string::npos ||
           name.rfind("CABLE Input", 0) == 0 || name.rfind("CABLE Output", 0) == 0;
}

std::string Endpoint::formFactorName() const {
    switch (formFactor) {
    case 0: return "peripherique reseau"; case 1: return "haut-parleurs"; case 2: return "sortie ligne";
    case 3: return "casque"; case 4: return "micro"; case 5: return "micro-casque"; case 6: return "combine";
    case 7: return "numerique"; case 8: return "S/PDIF"; case 9: return "HDMI / ecran";
    default: return "inconnu";
    }
}

static HRESULT enumerator(ComPtr<IMMDeviceEnumerator>& en) {
    return CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
}

static HRESULT openDevice(const std::wstring& id, bool capture, ComPtr<IMMDevice>& dev) {
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = enumerator(en);
    if (FAILED(hr)) return hr;
    if (id.empty()) return en->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, &dev);
    return en->GetDevice(id.c_str(), &dev);
}

static std::wstring defaultId(bool capture) {
    ComPtr<IMMDevice> dev;
    if (FAILED(openDevice(L"", capture, dev))) return {};
    LPWSTR id = nullptr; std::wstring r;
    if (SUCCEEDED(dev->GetId(&id)) && id) { r = id; CoTaskMemFree(id); }
    return r;
}

static void fillInfo(IMMDevice* dev, bool capture, const std::wstring& defId, Endpoint& out) {
    out.capture = capture;
    LPWSTR id = nullptr;
    if (SUCCEEDED(dev->GetId(&id)) && id) { out.id = id; CoTaskMemFree(id); }
    out.isDefault = !defId.empty() && out.id == defId;
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kPKEY_FriendlyName, &v)) && v.vt == VT_LPWSTR) out.name = toUtf8(v.pwszVal);
        PropVariantClear(&v);
        if (SUCCEEDED(props->GetValue(kPKEY_FormFactor, &v)) && v.vt == VT_UI4) out.formFactor = (int)v.ulVal;
        PropVariantClear(&v);
        if (SUCCEEDED(props->GetValue(kPKEY_PhysicalSpeakers, &v)) && v.vt == VT_UI4) out.physicalMask = v.ulVal;
        PropVariantClear(&v);
    }
    out.headphones = !capture && (out.formFactor == 3 || out.formFactor == 5);
    ComPtr<IAudioClient> ac;
    if (SUCCEEDED(dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac))) {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(ac->GetMixFormat(&mix)) && mix) {
            out.mixChannels = mix->nChannels;
            out.mixRate = (int)mix->nSamplesPerSec;
            if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
                out.mixMask = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;
            if (!out.mixMask) out.mixMask = defaultMask(out.mixChannels);
            CoTaskMemFree(mix);
        }
    }
}

bool getEndpoint(const std::wstring& id, bool capture, Endpoint& out, std::string& err) {
    ComInit com;
    ComPtr<IMMDevice> dev;
    HRESULT hr = openDevice(id, capture, dev);
    if (FAILED(hr)) { err = hr2s(id.empty() ? "Aucun peripherique par defaut" : "Peripherique introuvable", hr); return false; }
    fillInfo(dev.p, capture, defaultId(capture), out);
    if (!out.mixChannels) { err = "Format du peripherique illisible : " + out.name; return false; }
    return true;
}

bool listEndpoints(bool capture, std::vector<Endpoint>& out, std::string& err) {
    ComInit com;
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = enumerator(en);
    if (FAILED(hr)) { err = hr2s("Enumeration audio impossible", hr); return false; }
    ComPtr<IMMDeviceCollection> col;
    hr = en->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, &col);
    if (FAILED(hr)) { err = hr2s("Enumeration audio impossible", hr); return false; }
    UINT n = 0; col->GetCount(&n);
    const std::wstring def = defaultId(capture);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev))) continue;
        Endpoint e; fillInfo(dev.p, capture, def, e);
        out.push_back(e);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
struct ExclFmt { WORD bits, valid; bool isFloat; };

static WAVEFORMATEXTENSIBLE makeFormat(int channels, uint32_t mask, int rate, const ExclFmt& f) {
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels = (WORD)channels;
    wf.Format.nSamplesPerSec = (DWORD)rate;
    wf.Format.wBitsPerSample = f.bits;
    wf.Format.nBlockAlign = (WORD)(channels * f.bits / 8);
    wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
    wf.Format.cbSize = 22;
    wf.Samples.wValidBitsPerSample = f.valid;
    wf.dwChannelMask = mask;
    wf.SubFormat = f.isFloat ? kSUBTYPE_IEEE_FLOAT : kSUBTYPE_PCM;
    return wf;
}

// float -> format de la carte (mode exclusif), avec dither triangulaire en 16 bits
static void convertOut(const float* src, BYTE* dst, size_t n, const ExclFmt& f, uint32_t& rng) {
    auto rnd = [&] { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.f; };
    for (size_t i = 0; i < n; ++i) {
        float v = std::max(-1.f, std::min(1.f, src[i]));
        if (f.isFloat) { std::memcpy(dst + i * 4, &v, 4); continue; }
        if (f.bits == 16) {
            const float d = (rnd() - rnd()) / 32768.f;
            const int s = (int)std::lround(std::max(-1.f, std::min(1.f, v + d)) * 32767.f);
            const int16_t x = (int16_t)s; std::memcpy(dst + i * 2, &x, 2);
        } else if (f.bits == 24) {
            const int32_t s = (int32_t)std::lround(v * 8388607.f);
            dst[i * 3] = (BYTE)(s & 0xFF); dst[i * 3 + 1] = (BYTE)((s >> 8) & 0xFF); dst[i * 3 + 2] = (BYTE)((s >> 16) & 0xFF);
        } else {
            const int32_t s = (int32_t)std::max(-2147483647.0, std::min(2147483647.0, (double)v * 2147483647.0));
            std::memcpy(dst + i * 4, &s, 4);
        }
    }
}

// Partagé faible latence (IAudioClient3, Windows 10+) : le moteur audio de Windows tourne à la
// période minimale que le pilote accepte (souvent 2,7 ms au lieu de 10 ms). Exige le format de mixage.
static bool initLowLatencyShared(IAudioClient* ac, const WAVEFORMATEX* fmt, UINT32& periodFrames) {
    S3DAudioClient3* ac3 = nullptr;
    if (FAILED(ac->QueryInterface(kIID_IAudioClient3, (void**)&ac3)) || !ac3) return false;
    UINT32 def = 0, fund = 0, mn = 0, mx = 0;
    bool ok = SUCCEEDED(ac3->GetSharedModeEnginePeriod(fmt, &def, &fund, &mn, &mx)) && mn > 0 && mn < def;
    if (ok) ok = SUCCEEDED(ac3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, mn, fmt, nullptr));
    if (ok) periodFrames = mn;
    ac3->Release();
    return ok;
}

// Le format demandé est-il exactement le format de mixage (condition d'IAudioClient3) ?
static bool sameAsMix(const WAVEFORMATEX* mix, int channels, uint32_t mask, int rate) {
    if (!mix || mix->nChannels != channels || (int)mix->nSamplesPerSec != rate || mix->wBitsPerSample != 32) return false;
    if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return mask == defaultMask(channels);
    if (mix->wFormatTag != WAVE_FORMAT_EXTENSIBLE || mix->cbSize < 22) return false;
    auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
    return IsEqualGUID(x->SubFormat, kSUBTYPE_IEEE_FLOAT) && x->dwChannelMask == mask;
}

// Mode exclusif : choix du format (float 32, 24 dans 32, 24, 16) et de la période matérielle
static bool initExclusive(IMMDevice* dev, ComPtr<IAudioClient>& ac, int channels, uint32_t mask, int rate,
                          ExclFmt& chosen, HANDLE ev, bool minPeriod, std::string& why) {
    static const ExclFmt cands[] = { { 32, 32, true }, { 32, 24, false }, { 24, 24, false }, { 16, 16, false } };
    bool found = false;
    WAVEFORMATEXTENSIBLE wf{};
    for (const auto& f : cands) {
        wf = makeFormat(channels, mask, rate, f);
        if (ac->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&wf, nullptr) == S_OK) { chosen = f; found = true; break; }
    }
    if (!found) { why = "format " + std::to_string(channels) + " canaux / " + std::to_string(rate) + " Hz refuse en exclusif"; return false; }
    REFERENCE_TIME def = 0, mn = 0;
    ac->GetDevicePeriod(&def, &mn);
    if (minPeriod && mn > 0) def = mn;               // période matérielle minimale
    if (def <= 0) def = 100000;
    HRESULT hr = ac->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, def, def, (WAVEFORMATEX*)&wf, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 fr = 0; ac->GetBufferSize(&fr);
        def = (REFERENCE_TIME)(1e7 * fr / rate + 0.5);
        ac.reset();
        if (FAILED(dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac))) { why = "reactivation impossible"; return false; }
        hr = ac->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, def, def, (WAVEFORMATEX*)&wf, nullptr);
    }
    if (FAILED(hr)) {
        why = hr == AUDCLNT_E_DEVICE_IN_USE ? "carte deja utilisee par une autre application"
            : hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED ? "mode exclusif interdit dans les proprietes de la carte"
            : hr2s("refuse", hr);
        return false;
    }
    if (FAILED(ac->SetEventHandle(ev))) { why = "evenement refuse"; return false; }
    return true;
}

bool playWasapi(const std::wstring& id, int channels, uint32_t mask, int rate, const PlayOptions& opt,
                const PullFn& pull, std::atomic<bool>& stop, std::string& err, std::string* modeInfo,
                int* bufferFrames) {
    ComInit com;
    MmcssScope mmcss;
    ComPtr<IMMDevice> dev;
    HRESULT hr = openDevice(id, false, dev);
    if (FAILED(hr)) { err = hr2s("Peripherique de sortie introuvable", hr); return false; }
    ComPtr<IAudioClient> ac;
    hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac);
    if (FAILED(hr)) { err = hr2s("Activation IAudioClient impossible", hr); return false; }
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    bool exclusive = false;
    ExclFmt xf{ 32, 32, true };
    std::string mode;
    if (opt.exclusive) {
        std::string why;
        exclusive = initExclusive(dev.p, ac, channels, mask, rate, xf, ev, opt.lowLatency, why);
        if (!exclusive) {
            mode = "partagé (exclusif impossible : " + why + ")";
            ac.reset();
            hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac);
            if (FAILED(hr)) { CloseHandle(ev); err = hr2s("Activation IAudioClient impossible", hr); return false; }
        }
    }
    UINT32 llPeriod = 0;
    if (!exclusive) {
        bool ll = false;
        if (opt.lowLatency) {
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED(ac->GetMixFormat(&mix)) && sameAsMix(mix, channels, mask, rate))
                ll = initLowLatencyShared(ac.p, mix, llPeriod);
            if (mix) CoTaskMemFree(mix);
            if (!ll) {            // un essai refusé peut laisser le client dans un état indéterminé
                ac.reset();
                hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&ac);
                if (FAILED(hr)) { CloseHandle(ev); err = hr2s("Activation IAudioClient impossible", hr); return false; }
            }
        }
        if (!ll) {
            WAVEFORMATEXTENSIBLE wf = makeFormat(channels, mask, rate, { 32, 32, true });
            const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 0x80000000 /*AUTOCONVERTPCM*/ | 0x08000000 /*SRC_DEFAULT_QUALITY*/;
            hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, (REFERENCE_TIME)opt.bufferMs * 10000, 0, (WAVEFORMATEX*)&wf, nullptr);
            if (FAILED(hr)) { CloseHandle(ev); err = hr2s("Initialisation du flux de sortie refusee", hr); return false; }
        }
        ac->SetEventHandle(ev);
        if (mode.empty()) mode = ll ? "partagé faible latence (IAudioClient3)" : "partagé";
    }
    UINT32 bufFrames = 0;
    ac->GetBufferSize(&bufFrames);
    {
        // Format réellement envoyé : preuve visible du mode WASAPI obtenu
        char b[256];
        if (exclusive)
            snprintf(b, sizeof b, "exclusif — %s %d bits, %d Hz, %d canaux, période %.1f ms (directement à la carte son)",
                     xf.isFloat ? "flottant" : "entier", xf.valid, rate, channels, 1000.0 * bufFrames / rate);
        else if (llPeriod)
            snprintf(b, sizeof b, "%s — flottant 32 bits, %d Hz, %d canaux, période %.1f ms, tampon %.1f ms (via le mélangeur de Windows)",
                     mode.c_str(), rate, channels, 1000.0 * llPeriod / rate, 1000.0 * bufFrames / rate);
        else
            snprintf(b, sizeof b, "%s — flottant 32 bits, %d Hz, %d canaux, tampon %.0f ms (via le mélangeur de Windows)",
                     mode.c_str(), rate, channels, 1000.0 * bufFrames / rate);
        mode = b;
    }
    if (modeInfo) *modeInfo = mode;
    if (bufferFrames) *bufferFrames = (int)bufFrames;
    ComPtr<IAudioRenderClient> rc;
    hr = ac->GetService(kIID_IAudioRenderClient, (void**)&rc);
    if (FAILED(hr)) { CloseHandle(ev); err = hr2s("Service de rendu indisponible", hr); return false; }

    bool ended = false;
    std::vector<float> tmp;
    uint32_t rng = 12345;
    auto fill = [&](UINT32 frames) {
        BYTE* p = nullptr;
        if (FAILED(rc->GetBuffer(frames, &p))) return;
        const bool direct = !exclusive || (xf.isFloat && xf.bits == 32);
        float* f = direct ? reinterpret_cast<float*>(p) : (tmp.resize((size_t)frames * channels), tmp.data());
        size_t got = ended ? 0 : pull(f, frames);
        if (got < frames) {
            std::memset(f + got * channels, 0, (frames - got) * channels * sizeof(float));
            if (got == 0) ended = true;
        }
        if (!direct) convertOut(f, p, (size_t)frames * channels, xf, rng);
        rc->ReleaseBuffer(frames, 0);
    };

    fill(bufFrames);
    ac->Start();
    int drainEvents = 0;
    while (!stop.load()) {
        if (WaitForSingleObject(ev, 2000) != WAIT_OBJECT_0) { err = "Le peripherique audio ne repond plus"; break; }
        if (exclusive) {
            // exclusif évènementiel : chaque évènement = un tampon entier à remplir
            if (ended && ++drainEvents > 2) break;
            fill(bufFrames);
            continue;
        }
        UINT32 pad = 0;
        if (FAILED(ac->GetCurrentPadding(&pad))) { err = "Peripherique de sortie perdu (debranche ?)"; break; }
        if (ended) { if (pad == 0) break; continue; }
        if (bufFrames > pad) fill(bufFrames - pad);
    }
    ac->Stop();
    CloseHandle(ev);
    return err.empty();
}

// ---------------------------------------------------------------------------------------------
struct WasapiCapture::Impl {
    ComInit com;                       // garde COM vivant pour la durée des objets
    ComPtr<IMMDevice> dev;
    ComPtr<IAudioClient> ac;
    ComPtr<IAudioCaptureClient> cc;
    HANDLE ev = nullptr;
    int ch = 0, rate = 0, bits = 32, blockAlign = 0;
    bool isFloat = true;
    uint32_t mask = 0;
    std::vector<float> conv;
    double periodMs = 10;
    std::string mode;
    ~Impl() { if (ev) CloseHandle(ev); cc.reset(); ac.reset(); dev.reset(); }
};

WasapiCapture::WasapiCapture() : d_(new Impl) {}
WasapiCapture::~WasapiCapture() { delete d_; }
int WasapiCapture::channels() const { return d_->ch; }
int WasapiCapture::rate() const { return d_->rate; }
uint32_t WasapiCapture::mask() const { return d_->mask; }
double WasapiCapture::periodMs() const { return d_->periodMs; }
std::string WasapiCapture::modeInfo() const { return d_->mode; }

bool WasapiCapture::open(const std::wstring& id, int bufferMs, std::string& err, bool lowLatency) {
    auto& d = *d_;
    HRESULT hr = openDevice(id, true, d.dev);
    if (FAILED(hr)) { err = hr2s("Peripherique de capture introuvable", hr); return false; }
    hr = d.dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&d.ac);
    if (FAILED(hr)) { err = hr2s("Activation de la capture impossible", hr); return false; }
    WAVEFORMATEX* mix = nullptr;
    hr = d.ac->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) { err = hr2s("Format de capture illisible", hr); return false; }
    d.ch = mix->nChannels; d.rate = (int)mix->nSamplesPerSec; d.bits = mix->wBitsPerSample; d.blockAlign = mix->nBlockAlign;
    d.isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22) {
        auto* x = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
        d.mask = x->dwChannelMask;
        d.isFloat = IsEqualGUID(x->SubFormat, kSUBTYPE_IEEE_FLOAT) != 0;
    }
    if (!d.mask) d.mask = defaultMask(d.ch);
    UINT32 llPeriod = 0;
    bool ll = lowLatency && initLowLatencyShared(d.ac.p, mix, llPeriod);
    if (!ll && lowLatency) {
        d.ac.reset();
        hr = d.dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&d.ac);
        if (FAILED(hr)) { CoTaskMemFree(mix); err = hr2s("Activation de la capture impossible", hr); return false; }
    }
    if (!ll) hr = d.ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, (REFERENCE_TIME)bufferMs * 10000, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) { err = hr2s("Initialisation de la capture refusee", hr); return false; }
    if (ll) {
        d.periodMs = 1000.0 * llPeriod / d.rate;
        d.mode = "faible latence (IAudioClient3)";
    } else {
        REFERENCE_TIME def = 0, mn = 0;
        d.ac->GetDevicePeriod(&def, &mn);
        d.periodMs = def > 0 ? def / 10000.0 : 10.0;
        d.mode = "standard";
    }
    d.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    d.ac->SetEventHandle(d.ev);
    hr = d.ac->GetService(kIID_IAudioCaptureClient, (void**)&d.cc);
    if (FAILED(hr)) { err = hr2s("Service de capture indisponible", hr); return false; }
    return true;
}

bool WasapiCapture::run(const SinkFn& sink, std::atomic<bool>& stop, std::string& err) {
    ComInit com;
    MmcssScope mmcss;
    auto& d = *d_;
    if (FAILED(d.ac->Start())) { err = "Demarrage de la capture impossible"; return false; }
    while (!stop.load()) {
        WaitForSingleObject(d.ev, 200);     // sans son, VB-Cable peut ne rien envoyer : on reboucle
        UINT32 packet = 0;
        if (FAILED(d.cc->GetNextPacketSize(&packet))) { err = "Peripherique de capture perdu"; break; }
        while (packet > 0) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(d.cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            d.conv.resize((size_t)frames * d.ch);
            if (silent || !data) {
                std::fill(d.conv.begin(), d.conv.end(), 0.f);
            } else if (d.isFloat && d.bits == 32) {
                std::memcpy(d.conv.data(), data, d.conv.size() * sizeof(float));
            } else {
                const int bytes = d.bits / 8;
                for (size_t i = 0; i < d.conv.size(); ++i) {
                    const BYTE* s = data + i * bytes;
                    int32_t v = 0;
                    if (bytes == 2) v = (int32_t)(int16_t)(s[0] | (s[1] << 8)) << 16;
                    else if (bytes == 3) v = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24);
                    else if (bytes == 4) std::memcpy(&v, s, 4);
                    d.conv[i] = (float)v / 2147483648.f;
                }
            }
            sink(d.conv.data(), frames, silent, (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
            d.cc->ReleaseBuffer(frames);
            if (FAILED(d.cc->GetNextPacketSize(&packet))) break;
        }
    }
    d.ac->Stop();
    return err.empty();
}

// ---------------------------------------------------------------------------------------------
bool setDefaultEndpoint(const std::wstring& id, std::string& err) {
    ComInit com;
    ComPtr<IPolicyConfig> pc;
    HRESULT hr = CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, kIID_IPolicyConfig, (void**)&pc);
    if (FAILED(hr)) { err = hr2s("Interface de configuration audio indisponible", hr); return false; }
    for (ERole r : { eConsole, eMultimedia }) {
        hr = pc->SetDefaultEndpoint(id.c_str(), r);
        if (FAILED(hr)) { err = hr2s("Changement du peripherique par defaut refuse", hr); return false; }
    }
    return true;
}

bool setEndpoint71(const std::wstring& id, bool capture, int rate, std::string& err) {
    ComInit com;
    ComPtr<IPolicyConfig> pc;
    HRESULT hr = CoCreateInstance(kCLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, kIID_IPolicyConfig, (void**)&pc);
    if (FAILED(hr)) { err = hr2s("Interface de configuration audio indisponible", hr); return false; }
    if (!capture) {
        PROPVARIANT v; PropVariantInit(&v);
        v.vt = VT_UI4; v.ulVal = 0x63F;
        hr = pc->SetPropertyValue(id.c_str(), FALSE, kPKEY_PhysicalSpeakers, &v);
        if (FAILED(hr)) { err = hr2s("Configuration 7.1 refusee", hr); return false; }
    }
    for (int bits : { 24, 16 }) {
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 8;
        wf.Format.nSamplesPerSec = (DWORD)rate;
        wf.Format.wBitsPerSample = (WORD)bits;
        wf.Format.nBlockAlign = (WORD)(8 * bits / 8);
        wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
        wf.Format.cbSize = 22;
        wf.Samples.wValidBitsPerSample = (WORD)bits;
        wf.dwChannelMask = 0x63F;
        wf.SubFormat = kSUBTYPE_PCM;
        hr = pc->SetDeviceFormat(id.c_str(), (WAVEFORMATEX*)&wf, (WAVEFORMATEX*)&wf);
        if (SUCCEEDED(hr)) return true;
    }
    err = hr2s("Format 7.1 refuse par le peripherique", hr);
    return false;
}

// =============================================================================================
//  Volume / sourdine Windows d'un périphérique (CABLE Input)
// =============================================================================================
namespace {
struct VolumeCallback final : public IAudioEndpointVolumeCallback {
    LONG ref = 1;
    HANDLE ev;
    explicit VolumeCallback(HANDLE e) : ev(e) {}
    virtual ~VolumeCallback() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {
        if (IsEqualGUID(riid, kIID_IUnknownS3D) || IsEqualGUID(riid, kIID_IAudioEndpointVolumeCallback)) {
            *pp = static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK;
        }
        *pp = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    // Appelée par Windows à chaque changement : on réveille seulement le fil de suivi
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override { SetEvent(ev); return S_OK; }
};
} // namespace

struct EndpointVolumeWatch::Impl {
    std::thread th;
    std::atomic<bool> stop{ false };
    std::atomic<float> gain{ 1.f }, db{ 0.f };
    std::atomic<bool> muted{ false };
    HANDLE ev = nullptr;
};

EndpointVolumeWatch::EndpointVolumeWatch() : d_(new Impl) {}
EndpointVolumeWatch::~EndpointVolumeWatch() { stop(); delete d_; }
float EndpointVolumeWatch::gain() const { return d_->gain.load(std::memory_order_relaxed); }
bool EndpointVolumeWatch::muted() const { return d_->muted; }
float EndpointVolumeWatch::levelDb() const { return d_->db; }

bool EndpointVolumeWatch::start(const std::wstring& id, std::string& err) {
    stop();
    d_->stop = false;
    d_->ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::promise<std::string> ready;
    auto f = ready.get_future();
    d_->th = std::thread([this, id, &ready] {
        ComInit com;
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioEndpointVolume> vol;
        HRESULT hr = openDevice(id, false, dev);
        if (SUCCEEDED(hr)) hr = dev->Activate(kIID_IAudioEndpointVolume, CLSCTX_ALL, nullptr, (void**)&vol);
        if (FAILED(hr)) { ready.set_value(hr2s("Volume Windows de VB-Cable inaccessible", hr)); return; }
        auto* cb = new VolumeCallback(d_->ev);
        const bool reg = SUCCEEDED(vol->RegisterControlChangeNotify(cb));
        auto read = [&] {
            BOOL m = FALSE; float lv = 0.f, sc = 1.f;
            vol->GetMute(&m);
            vol->GetMasterVolumeLevelScalar(&sc);
            const bool haveDb = SUCCEEDED(vol->GetMasterVolumeLevel(&lv));
            float g = haveDb ? std::pow(10.f, std::min(lv, 0.f) / 20.f) : sc * sc;
            if (m || sc <= 0.0001f) g = 0.f;                 // curseur à zéro = silence, comme Windows
            d_->muted = m != FALSE;
            d_->db = haveDb ? lv : 20.f * std::log10(std::max(1e-5f, sc * sc));
            d_->gain = g;
        };
        read();
        ready.set_value("");
        while (!d_->stop) {
            // notification immédiate ; relecture de sécurité toutes les secondes
            WaitForSingleObject(d_->ev, 1000);
            if (!d_->stop) read();
        }
        if (reg) vol->UnregisterControlChangeNotify(cb);
        cb->Release();
    });
    err = f.get();
    if (!err.empty()) { stop(); return false; }
    return true;
}

void EndpointVolumeWatch::stop() {
    if (d_->th.joinable()) {
        d_->stop = true;
        if (d_->ev) SetEvent(d_->ev);
        d_->th.join();
    }
    if (d_->ev) { CloseHandle(d_->ev); d_->ev = nullptr; }
    d_->gain = 1.f; d_->muted = false; d_->db = 0.f;
}

} // namespace s3d
