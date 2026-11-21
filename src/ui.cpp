#include "ui.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3d {

static constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------------------------
//  Palette
// ---------------------------------------------------------------------------------------------
static ImVec4 v4(int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
static const ImVec4 kBg = v4(16, 18, 24), kPanel = v4(24, 27, 36), kPanel2 = v4(32, 36, 48), kBorder = v4(48, 54, 70);
static const ImVec4 kText = v4(228, 232, 240), kDim = v4(140, 148, 166);
static const ImVec4 kAccent = v4(46, 196, 182), kAccentHi = v4(80, 222, 208);
static const ImVec4 kGreen = v4(34, 197, 94), kRed = v4(239, 68, 68), kAmber = v4(245, 158, 11), kViolet = v4(139, 92, 246);
static const ImVec4 kOrange = v4(255, 159, 67);

static ImU32 u32(const ImVec4& c, float a = -1) { ImVec4 x = c; if (a >= 0) x.w = a; return ImGui::ColorConvertFloat4ToU32(x); }
static ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
static ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }

static ImFont* font(int i) { auto& f = ImGui::GetIO().Fonts->Fonts; return i < f.Size ? f[i] : f[0]; }
enum { F_REGULAR = 0, F_TITLE = 1, F_HEAD = 2, F_SMALL = 3 };

void uiSetupStyle(float s) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(18, 16);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 9);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.WindowRounding = 0; st.ChildRounding = 10; st.FrameRounding = 7; st.GrabRounding = 7;
    st.PopupRounding = 8; st.TabRounding = 7; st.ScrollbarRounding = 8;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 0; st.TabBorderSize = 0;
    st.GrabMinSize = 14; st.ScrollbarSize = 12;
    st.ScaleAllSizes(s);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = kText; c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_WindowBg] = kBg; c[ImGuiCol_ChildBg] = kPanel; c[ImGuiCol_PopupBg] = kPanel2;
    c[ImGuiCol_Border] = kBorder; c[ImGuiCol_BorderShadow] = v4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kPanel2; c[ImGuiCol_FrameBgHovered] = v4(42, 47, 62); c[ImGuiCol_FrameBgActive] = v4(50, 56, 74);
    c[ImGuiCol_TitleBg] = kBg; c[ImGuiCol_TitleBgActive] = kBg;
    c[ImGuiCol_ScrollbarBg] = v4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = kBorder;
    c[ImGuiCol_CheckMark] = kAccent; c[ImGuiCol_SliderGrab] = kAccent; c[ImGuiCol_SliderGrabActive] = kAccentHi;
    c[ImGuiCol_Button] = kPanel2; c[ImGuiCol_ButtonHovered] = v4(48, 54, 72); c[ImGuiCol_ButtonActive] = v4(58, 65, 86);
    c[ImGuiCol_Header] = v4(46, 196, 182, 0.25f); c[ImGuiCol_HeaderHovered] = v4(46, 196, 182, 0.35f); c[ImGuiCol_HeaderActive] = v4(46, 196, 182, 0.5f);
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_Tab] = v4(0, 0, 0, 0); c[ImGuiCol_TabHovered] = kPanel2; c[ImGuiCol_TabSelected] = kPanel;
    c[ImGuiCol_TabSelectedOverline] = kAccent; c[ImGuiCol_TabDimmed] = v4(0, 0, 0, 0); c[ImGuiCol_TabDimmedSelected] = kPanel;
    c[ImGuiCol_PlotHistogram] = kAccent;
}

// ---------------------------------------------------------------------------------------------
//  Utilitaires
// ---------------------------------------------------------------------------------------------
static const char* shortName(int id) {
    switch (id) {
    case CH_FL: return "AvG"; case CH_FR: return "AvD"; case CH_FC: return "C"; case CH_LFE: return "LFE";
    case CH_BL: return "ArG"; case CH_BR: return "ArD"; case CH_FLC: return "AvGC"; case CH_FRC: return "AvDC";
    case CH_BC: return "ArC"; case CH_SL: return "CôG"; case CH_SR: return "CôD"; case CH_TC: return "Haut";
    case CH_TFL: return "HAvG"; case CH_TFC: return "HAvC"; case CH_TFR: return "HAvD";
    case CH_TBL: return "HArG"; case CH_TBC: return "HArC"; case CH_TBR: return "HArD";
    case CH_WIDE_L: return "LgG"; case CH_WIDE_R: return "LgD"; case CH_TSL: return "HCôG"; case CH_TSR: return "HCôD";
    default: return "?";
    }
}

static std::string mmss(double t) {
    char b[32]; const int s = std::max(0, (int)t);
    if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60);
    return b;
}

static void smooth(float* dst, const float* src, int n) {
    const float k = std::exp(-ImGui::GetIO().DeltaTime * 5.f);
    for (int i = 0; i < n; ++i) dst[i] = std::max(src[i], dst[i] * k);
}

static void sectionTitle(const char* t) {
    ImGui::PushFont(font(F_HEAD));
    ImGui::TextUnformatted(t);
    ImGui::PopFont();
}

static bool beginCard(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    return ImGui::BeginChild(id, ImVec2(-FLT_MIN, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}
static void endCard() { ImGui::EndChild(); ImGui::PopStyleColor(); }

static void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void wrappedColored(const ImVec4& c, const std::string& t) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------------------------
//  Vue de la scène (vue de dessus, avant en haut)
// ---------------------------------------------------------------------------------------------
static ImVec2 scenePos(ImVec2 c, float R, double az, double el) {
    const float a = (float)az * kPi / 180.f, e = (float)el * kPi / 180.f;
    const float r = R * std::cos(e);
    return ImVec2(c.x - std::sin(a) * r, c.y - std::cos(a) * r);
}

static void drawScene(const Scene* sc, const Layout& fallback, const float* inLv, const float* outLv, float height, float s) {
    const Layout& L = sc ? sc->out : fallback;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 c(p0.x + w * 0.5f, p0.y + height * 0.5f + 6 * s);
    const float R = std::min(w, height) * 0.36f;

    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), u32(kPanel), 10 * s);
    // anneaux : horizon et 45° d'élévation
    for (int k = 4; k >= 1; --k) dl->AddCircleFilled(c, R * (0.25f * k + 0.08f), u32(kAccent, 0.012f * k), 96);
    dl->AddCircle(c, R, u32(kBorder), 128, 1.5f * s);
    dl->AddCircle(c, R * 0.7071f, u32(kViolet, 0.35f), 96, 1.f * s);
    dl->AddLine(ImVec2(c.x - R * 1.08f, c.y), ImVec2(c.x + R * 1.08f, c.y), u32(kBorder, 0.5f), 1.f);
    dl->AddLine(ImVec2(c.x, c.y - R * 1.08f), ImVec2(c.x, c.y + R * 1.08f), u32(kBorder, 0.5f), 1.f);
    ImGui::PushFont(font(F_SMALL));
    auto centered = [&](ImVec2 at, ImU32 col, const char* t) {
        const ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(at.x - ts.x * 0.5f, at.y - ts.y * 0.5f), col, t);
    };
    centered(ImVec2(c.x, c.y - R - 40 * s), u32(kDim), "AVANT");
    centered(ImVec2(c.x, c.y + R + 40 * s), u32(kDim), "ARRIÈRE");

    // auditeur
    const float hr = R * 0.085f;
    dl->AddTriangleFilled(ImVec2(c.x, c.y - hr * 1.55f), ImVec2(c.x - hr * 0.45f, c.y - hr * 0.8f), ImVec2(c.x + hr * 0.45f, c.y - hr * 0.8f), u32(kText, 0.8f));
    dl->AddCircleFilled(c, hr, u32(kText, 0.9f), 48);
    dl->AddCircleFilled(ImVec2(c.x - hr, c.y), hr * 0.28f, u32(kText, 0.9f), 16);
    dl->AddCircleFilled(ImVec2(c.x + hr, c.y), hr * 0.28f, u32(kText, 0.9f), 16);
    if (L.headphones) {
        dl->PathArcTo(c, hr * 1.55f, kPi * 1.05f, kPi * 1.95f, 32);
        dl->PathStroke(u32(kAccent), 0, 3.5f * s);
        for (int side : { -1, 1 }) {
            const ImVec2 e(c.x + side * hr * 1.45f, c.y);
            const float lv = outLv ? outLv[side < 0 ? 0 : 1] : 0.f;
            dl->AddRectFilled(ImVec2(e.x - hr * 0.35f, e.y - hr * 0.7f), ImVec2(e.x + hr * 0.35f, e.y + hr * 0.7f),
                              u32(mix(kPanel2, kAccent, 0.4f + 0.6f * std::min(1.f, lv * 2.f))), 4 * s);
        }
    }

    // positions des enceintes
    std::vector<ImVec2> spk(L.speakers.size());
    int lfeIdx = -1;
    for (size_t i = 0; i < L.speakers.size(); ++i) {
        if (L.speakers[i].lfe) { lfeIdx = (int)i; continue; }
        spk[i] = scenePos(c, R, L.speakers[i].az, L.speakers[i].el);
    }
    const ImVec2 lfePos(p0.x + w - 46 * s, p0.y + height - 40 * s);
    if (lfeIdx >= 0) spk[lfeIdx] = lfePos;

    // sources (et liaisons vers les enceintes)
    if (sc) {
        const bool ambi = !sc->sources.empty() && sc->sources[0].label.rfind("Ambi", 0) == 0;
        for (size_t k = 0; k < sc->sources.size(); ++k) {
            const auto& src = sc->sources[k];
            if (src.lfe && L.headphones) continue;
            int col = 0; for (int i = 0; i < sc->inChannels; ++i) if (src.w[i] != 0.f) { col = i; break; }
            const float lv = inLv ? std::min(1.f, (ambi ? inLv[0] : inLv[std::min(col, 31)]) * 1.6f) : 0.f;
            const ImVec2 sp = src.lfe ? ImVec2(lfePos.x - 34 * s, lfePos.y) : scenePos(c, R * 0.74f, src.az, src.el);
            if (!L.headphones && !sc->matrix.empty()) {
                for (size_t o = 0; o < L.speakers.size(); ++o) {
                    float g = 0;
                    for (int i = 0; i < sc->inChannels; ++i) g += std::fabs(sc->matrix[o * sc->inChannels + i] * src.w[i]);
                    if (g < 0.04f) continue;
                    dl->AddLine(sp, spk[o], u32(kOrange, 0.08f + 0.55f * lv * std::min(1.f, g)), (1.f + 2.5f * std::min(1.f, g)) * s);
                }
            } else if (L.headphones && !src.lfe) {
                dl->AddLine(sp, c, u32(kOrange, 0.06f + 0.4f * lv), 1.2f * s);
            }
            const float rr = (ambi ? 5.f : 7.f) * s;
            dl->AddCircleFilled(sp, rr + 6 * s * lv, u32(kOrange, 0.18f * lv), 24);
            dl->AddCircleFilled(sp, rr, u32(mix(v4(120, 80, 40), kOrange, 0.35f + 0.65f * lv)), 24);
            if (ImGui::IsMouseHoveringRect(sp - ImVec2(rr, rr), sp + ImVec2(rr, rr))) {
                ImGui::BeginTooltip();
                ImGui::Text("Source : %s", src.label.c_str());
                if (!src.lfe) ImGui::Text("azimut %.0f°, élévation %.0f°", src.az, src.el);
                ImGui::Text("niveau %.1f dB", 20 * std::log10(std::max(1e-5f, inLv ? inLv[std::min(col, 31)] : 0.f)));
                ImGui::EndTooltip();
            }
        }
    }

    // enceintes
    if (!L.headphones) {
        for (size_t i = 0; i < L.speakers.size(); ++i) {
            const auto& sp = L.speakers[i];
            const bool top = sp.el > 10;
            const float lv = outLv ? std::min(1.f, outLv[std::min((int)i, 31)] * 1.6f) : 0.f;
            const float hs = (sp.lfe ? 15.f : top ? 10.f : 12.f) * s;
            const ImVec4 base = top ? kViolet : kAccent;
            const ImVec2 p = spk[i];
            if (lv > 0.01f) dl->AddCircleFilled(p, hs * (1.6f + lv), u32(base, 0.15f * lv), 32);
            dl->AddRectFilled(p - ImVec2(hs, hs), p + ImVec2(hs, hs), u32(mix(kPanel2, base, 0.35f + 0.65f * lv)), 4 * s);
            dl->AddRect(p - ImVec2(hs, hs), p + ImVec2(hs, hs), u32(base), 4 * s, 0, 1.5f * s);
            dl->AddCircleFilled(p, hs * 0.45f, u32(kBg, 0.7f), 16);
            const char* nm = shortName(sp.id);
            centered(ImVec2(p.x, p.y + hs + 10 * s), u32(kText, 0.85f), nm);
            if (ImGui::IsMouseHoveringRect(p - ImVec2(hs, hs), p + ImVec2(hs, hs))) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", channelName(sp.id));
                if (!sp.lfe) ImGui::Text("azimut %.0f°, élévation %.0f°", sp.az, sp.el);
                ImGui::Text("niveau %.1f dB", 20 * std::log10(std::max(1e-5f, outLv ? outLv[std::min((int)i, 31)] : 0.f)));
                ImGui::EndTooltip();
            }
        }
    }
    // légende
    const ImVec2 lg(p0.x + 14 * s, p0.y + height - 26 * s);
    dl->AddCircleFilled(ImVec2(lg.x + 5 * s, lg.y + 7 * s), 5 * s, u32(kOrange));
    dl->AddText(ImVec2(lg.x + 14 * s, lg.y), u32(kDim), "sources");
    if (!L.headphones) {
        dl->AddRectFilled(ImVec2(lg.x + 80 * s, lg.y + 2 * s), ImVec2(lg.x + 90 * s, lg.y + 12 * s), u32(kAccent), 2 * s);
        dl->AddText(ImVec2(lg.x + 96 * s, lg.y), u32(kDim), "enceintes");
        dl->AddRectFilled(ImVec2(lg.x + 172 * s, lg.y + 2 * s), ImVec2(lg.x + 182 * s, lg.y + 12 * s), u32(kViolet), 2 * s);
        dl->AddText(ImVec2(lg.x + 188 * s, lg.y), u32(kDim), "hauteur");
    }
    const std::string title = L.headphones ? "Casque — rendu binaural" : "Enceintes — " + L.name;
    dl->AddText(ImVec2(p0.x + 14 * s, p0.y + 10 * s), u32(kDim), title.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, height));
}

static void drawMeters(const Layout& L, int nIn, const float* inLv, const float* outLv, const Scene* sc, float s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 118 * s, barH = 64 * s, bw = 14 * s, gap = 7 * s;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), u32(kPanel), 10 * s);
    ImGui::PushFont(font(F_SMALL));
    float x = p0.x + 14 * s;
    auto group = [&](const char* title, int n, const float* lv, auto nameOf) {
        dl->AddText(ImVec2(x, p0.y + 8 * s), u32(kDim), title);
        for (int i = 0; i < n && i < 32; ++i) {
            const float db = 20 * std::log10(std::max(1e-6f, lv ? lv[i] : 0.f));
            const float f = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
            const char* nm = nameOf(i);
            const ImVec2 ts = ImGui::CalcTextSize(nm);
            const float slot = std::max(bw, ts.x);
            const float bx = x + (slot - bw) * 0.5f;
            const ImVec2 b0(bx, p0.y + 30 * s), b1(bx + bw, p0.y + 30 * s + barH);
            dl->AddRectFilled(b0, b1, u32(kPanel2), 3 * s);
            const ImVec4 col = db > -3 ? kRed : db > -12 ? kAmber : kGreen;
            dl->AddRectFilled(ImVec2(b0.x, b1.y - barH * f), b1, u32(col, 0.9f), 3 * s);
            dl->AddText(ImVec2(x + slot * 0.5f - ts.x * 0.5f, b1.y + 4 * s), u32(kDim), nm);
            x += slot + gap;
        }
        x += 22 * s;
    };
    if (sc && nIn > 0) {
        group("Entrée", nIn, inLv, [&](int i) -> const char* {
            if (i < (int)sc->inIds.size() && sc->inIds[i] >= 0) return shortName(sc->inIds[i]);
            static char b[32][6]; snprintf(b[i & 31], 6, "%d", i + 1); return b[i & 31];
        });
    }
    if (L.headphones) group("Sortie casque", 2, outLv, [](int i) -> const char* { return i ? "D" : "G"; });
    else group("Sortie", (int)L.speakers.size(), outLv, [&](int i) -> const char* { return shortName(L.speakers[i].id); });
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, h));
}

// ---------------------------------------------------------------------------------------------
//  Commandes communes
// ---------------------------------------------------------------------------------------------
static const char* kLayoutKeys[] = { "auto", "casque", "2.0", "2.1", "3.0", "quad", "5.0", "5.1", "7.1", "5.1.2", "5.1.4", "7.1.2", "7.1.4" };
static const char* kLayoutLabels[] = { "Automatique", "Casque (binaural)", "Stéréo 2.0", "2.1", "3.0", "Quadriphonie", "5.0", "5.1", "7.1",
                                        "5.1.2 (hauteurs)", "5.1.4 (hauteurs)", "7.1.2 (hauteurs)", "7.1.4 (hauteurs)" };

static void outputCombos(UiModel& m, UiActions& a) {
    std::string cur = m.outputSel >= 0 && m.outputSel < (int)m.outputs.size()
                          ? m.outputs[m.outputSel].name + "  ·  " + m.outputs[m.outputSel].layoutName : "Aucune sortie";
    ImGui::TextDisabled("Sortie audio");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##sortie", cur.c_str())) {
        for (int i = 0; i < (int)m.outputs.size(); ++i) {
            const std::string lbl = m.outputs[i].name + "  ·  " + m.outputs[i].layoutName;
            if (ImGui::Selectable(lbl.c_str(), i == m.outputSel)) {
                m.outputSel = i; m.settings.outputId = m.outputs[i].id;
                a.settingsChanged = a.needRestart = true;
            }
        }
        ImGui::EndCombo();
    }
    int li = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kLayoutKeys); ++i) if (m.settings.layout == kLayoutKeys[i]) li = i;
    ImGui::TextDisabled("Disposition");
    helpMarker("Automatique : la configuration déclarée dans Windows (Panneau de configuration > Son > Configurer). "
               "Choisissez « Casque » si un casque est branché sur une sortie vue comme des haut-parleurs.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##dispo", &li, kLayoutLabels, IM_ARRAYSIZE(kLayoutLabels))) {
        m.settings.layout = kLayoutKeys[li];
        a.settingsChanged = a.needRestart = true;
    }
}

static void volumeSlider(UiModel& m, UiActions& a) {
    ImGui::TextDisabled("Volume");
    float g = (float)m.settings.gainDb;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##vol", &g, -24.f, 12.f, "%+.1f dB")) { m.settings.gainDb = g; a.gainChanged = true; }
    if (ImGui::IsItemDeactivatedAfterEdit()) a.settingsChanged = true;
}

static void statusPill(const char* text, const ImVec4& col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float pad = ImGui::GetFontSize() * 0.7f, r = ImGui::GetFontSize() * 0.3f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + pad * 2 + r * 3, ts.y + pad * 0.8f);
    dl->AddRectFilled(p, p + sz, u32(col, 0.16f), sz.y * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + pad + r * 0.5f, p.y + sz.y * 0.5f), r, u32(col), 16);
    dl->AddText(ImVec2(p.x + pad + r * 2.5f, p.y + pad * 0.4f), u32(col), text);
    ImGui::Dummy(sz);
}

// ---------------------------------------------------------------------------------------------
//  Onglets
// ---------------------------------------------------------------------------------------------
static void liveTab(UiModel& m, UiActions& a, float s) {
    static float in[32], out[32];
    smooth(in, m.liveIn, 32); smooth(out, m.liveOut, 32);
    const float leftW = 380 * s;
    ImGui::BeginChild("##liveL", ImVec2(leftW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    beginCard("##livecmd");
    sectionTitle("Son de Windows en 3D");
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("Jeux, films, navigateur, musique : tout le son passe par Spatial3D via VB-Cable. "
                       "Arrêter ou quitter rend le son normal.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    const bool on = m.liveRunning;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? v4(127, 29, 29) : v4(13, 120, 110));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? v4(153, 27, 27) : v4(17, 148, 136));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? v4(185, 28, 28) : v4(20, 170, 156));
    ImGui::PushFont(font(F_HEAD));
    ImGui::BeginDisabled(!m.vbCableInstalled || m.liveStarting);
    if (ImGui::Button(m.liveStarting ? "Démarrage…" : on ? "■  Arrêter" : "▶  Activer le son 3D", ImVec2(-FLT_MIN, 54 * s))) a.toggleLive = true;
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    if (!m.vbCableInstalled) {
        wrappedColored(kAmber, "VB-Cable n'est pas installé (ou l'ordinateur n'a pas redémarré depuis). "
                               "Relancez build-windows.bat ou installez-le depuis le site de VB-Audio.");
        if (ImGui::Button("Télécharger VB-Cable", ImVec2(-FLT_MIN, 0))) a.openVbCableSite = true;
    }
    if (!m.liveError.empty()) wrappedColored(kRed, m.liveError);
    if (!m.liveWarning.empty()) wrappedColored(kAmber, m.liveWarning);
    endCard();

    ImGui::Spacing();
    beginCard("##liveout");
    outputCombos(m, a);
    volumeSlider(m, a);
    endCard();

    ImGui::Spacing();
    beginCard("##livestats");
    sectionTitle("État");
    if (ImGui::BeginTable("##st", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const std::string& v, const ImVec4* col = nullptr) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
            ImGui::TableNextColumn();
            if (col) ImGui::TextColored(*col, "%s", v.c_str()); else ImGui::TextUnformatted(v.c_str());
        };
        char b[160];
        if (!m.liveRunning) row("Spatialisation", "arrêtée");
        else {
            row("Signal", m.liveIdle ? "en attente de son" : "reçu", m.liveIdle ? &kDim : &kGreen);
            if (m.totalLatencyMs > 0) {
                snprintf(b, sizeof b, "%.1f ms", m.totalLatencyMs);
                row("Latence estimée", b, m.totalLatencyMs <= 25 ? &kGreen : nullptr);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Capture %.1f ms + marge %.0f ms + traitement %.1f ms + sortie %.1f ms",
                                      m.capturePeriodMs, m.marginMs, m.processingMs, m.outputBufferMs);
            }
            snprintf(b, sizeof b, "%.0f ms (point bas %.1f)", m.marginMs, m.lowMs); row("Marge VB-Cable", b);
            snprintf(b, sizeof b, "%+.0f ppm", m.driftPpm); row("Dérive d'horloge", b);
            snprintf(b, sizeof b, "%.0f Hz", m.inHz); row("Entrée VB-Cable", m.inHz > 0 ? b : "—");
            snprintf(b, sizeof b, "%d (fondues, sans clic)", m.underruns); row("Coupures", m.underruns ? b : "0", m.underruns > 3 ? &kAmber : nullptr);
            snprintf(b, sizeof b, "%d", m.captureGlitches); row("Pertes VB-Cable", b, m.captureGlitches > 0 ? &kAmber : nullptr);
            if (!m.liveCaptureMode.empty()) row("Capture WASAPI", m.liveCaptureMode);
            if (!m.liveOutputMode.empty()) row("Sortie WASAPI", m.liveOutputMode);
        }
        ImGui::EndTable();
    }
    endCard();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##liveR", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const float sceneH = ImGui::GetContentRegionAvail().y - 118 * s - ImGui::GetStyle().ItemSpacing.y;
    const Scene* sc = m.liveRunning ? &m.liveScene : nullptr;
    drawScene(sc, m.previewLayout, in, out, sceneH, s);
    drawMeters(sc ? sc->out : m.previewLayout, sc ? sc->inChannels : 0, in, out, sc, s);
    ImGui::EndChild();
}

static void settingsTab(UiModel& m, UiActions& a, float s) {
    ImGui::BeginChild("##set", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool two = avail > 900 * s;
    const float colW = two ? (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f : avail;
    ImGui::BeginChild("##setcol", ImVec2(colW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    beginCard("##s1");
    sectionTitle("Sortie");
    outputCombos(m, a);
    endCard();
    ImGui::Spacing();

    beginCard("##s2");
    sectionTitle("Rendu");
    float sp = (float)m.settings.spreadDeg;
    ImGui::TextDisabled("Étalement des sources");
    helpMarker("0° : chaque son est une image précise. Plus d'étalement = sons plus larges et enveloppants, "
               "utile avec peu d'enceintes ou pour la musique.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##spread", &sp, 0.f, 90.f, "%.0f°")) m.settings.spreadDeg = sp;
    if (ImGui::IsItemDeactivatedAfterEdit()) a.settingsChanged = a.needRestart = true;

    bool autoRoom = m.settings.room < 0;
    ImGui::TextDisabled("Pièce virtuelle (casque)");
    helpMarker("Un peu de réverbération sort le son de la tête au casque. Automatique : 15 % au casque, rien sur enceintes.");
    if (ImGui::Checkbox("Automatique##room", &autoRoom)) { m.settings.room = autoRoom ? -1 : 0.15; a.settingsChanged = a.needRestart = true; }
    if (!autoRoom) {
        float r = (float)m.settings.room * 100.f;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##room", &r, 0.f, 100.f, "%.0f %%")) m.settings.room = r / 100.f;
        if (ImGui::IsItemDeactivatedAfterEdit()) a.settingsChanged = a.needRestart = true;
    }

    ImGui::TextDisabled("HRTF mesurée (fichier .sofa)");
    helpMarker("Au casque, une HRTF mesurée sur une vraie tête donne un placement plus précis que le modèle calculé. "
               "Fichiers gratuits : bases SADIE II, ARI, HUTUBS (sofacoustics.org).");
    if (!m.sofaSupported) {
        ImGui::TextColored(kDim, "Indisponible dans cette version (compilée sans libmysofa).");
    } else {
        const std::string cur = m.settings.sofaPath.empty() ? "Aucune — modèle de tête calculé" : m.settings.sofaPath;
        ImGui::TextWrapped("%s", cur.c_str());
        if (ImGui::Button("Choisir…")) a.browseSofa = true;
        if (!m.settings.sofaPath.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Retirer")) { m.settings.sofaPath.clear(); a.settingsChanged = a.needRestart = true; }
        }
    }
    endCard();
    ImGui::Spacing();

    if (two) {
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##setcol2", ImVec2(colW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    }
    beginCard("##s3");
    sectionTitle("Temps réel");
    int mg = m.settings.marginMs;
    ImGui::TextDisabled("Marge de départ");
    helpMarker("Latence de sécurité entre VB-Cable et la carte son. Elle augmente seule à chaque coupure "
               "et la valeur apprise est gardée pour la prochaine fois.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##marge", &mg, 5, 100, "%d ms")) m.settings.marginMs = mg;
    if (ImGui::IsItemDeactivatedAfterEdit()) a.settingsChanged = a.needRestart = true;
    if (ImGui::Checkbox("Latence minimale (périodes WASAPI les plus courtes)", &m.settings.lowLatency)) a.settingsChanged = a.needRestart = true;
    helpMarker("Windows 10/11 : le moteur audio tourne à la période minimale du pilote (souvent 2,7 ms au lieu "
               "de 10 ms), en capture comme en sortie ; en exclusif, période matérielle minimale. Décochez si "
               "un vieux pilote produit des craquements.");
    if (ImGui::Checkbox("Sortie WASAPI exclusive", &m.settings.exclusive)) a.settingsChanged = a.needRestart = true;
    helpMarker("Spatial3D parle directement à la carte son, sans le mélangeur de Windows : latence plus faible "
               "et moins de risques de pops. La carte est alors réservée à Spatial3D (les autres applications ne "
               "peuvent plus l'utiliser en même temps). Repli automatique en mode partagé si la carte refuse.");
    if (ImGui::Checkbox("Ne pas basculer la sortie de Windows sur VB-Cable", &m.settings.keepDefault)) a.settingsChanged = a.needRestart = true;
    helpMarker("Cochez si vous préférez choisir « CABLE Input » vous-même dans chaque application.");
    if (ImGui::Button("Rétablir le son normal (sortie par défaut)")) a.restoreSound = true;
    endCard();
    ImGui::Spacing();

    beginCard("##s4");
    sectionTitle("Application");
    if (ImGui::Checkbox("Démarrer avec Windows (dans la zone de notification)", &m.settings.startWithWindows)) {
        a.settingsChanged = true; a.startWithWindowsChanged = true;
    }
    if (ImGui::Checkbox("Activer le son 3D au lancement", &m.settings.liveOnLaunch)) a.settingsChanged = true;
    if (ImGui::Checkbox("Fermer la fenêtre = réduire dans la zone de notification", &m.settings.closeToTray)) a.settingsChanged = true;
    endCard();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("Spatial3D %s — BJMPC Informatique. Interface : Dear ImGui (MIT). "
                       "Carte son virtuelle : VB-Cable de VB-Audio (donationware).", m.version.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------------------------
void uiDraw(UiModel& m, UiActions& a, float s) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    // En-tête
    ImGui::PushFont(font(F_TITLE));
    ImGui::TextColored(kAccent, "Spatial");
    ImGui::SameLine(0, 0);
    ImGui::TextUnformatted("3D");
    ImGui::PopFont();
    ImGui::SameLine();
    {
        const float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + ImGui::GetFontSize() * 0.55f);
        ImGui::TextDisabled("spatialisation 3D · BJMPC Informatique");
    }
    const char* st = m.liveRunning ? (m.liveIdle ? "Son 3D actif · en attente" : "Son 3D actif") : "Inactif";
    const ImVec4 stc = m.liveRunning ? (m.liveIdle ? kAmber : kGreen) : kDim;
    const float pillW = ImGui::CalcTextSize(st).x + ImGui::GetFontSize() * 2.6f;
    ImGui::SameLine(ImGui::GetWindowWidth() - pillW - ImGui::GetStyle().WindowPadding.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.35f);
    statusPill(st, stc);
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##tabs")) {
        auto fl = [&](int k) { return m.forceTab == k ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        if (ImGui::BeginTabItem("  Temps réel  ", nullptr, fl(0))) { liveTab(m, a, s); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Réglages  ", nullptr, fl(1))) { settingsTab(m, a, s); ImGui::EndTabItem(); }
        m.forceTab = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace s3d
