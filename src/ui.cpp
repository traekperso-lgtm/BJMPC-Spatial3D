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


static void smooth(float* dst, const float* src, int n) {
    const float k = std::exp(-ImGui::GetIO().DeltaTime * 5.f);
    for (int i = 0; i < n; ++i) dst[i] = std::max(src[i], dst[i] * k);
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

// Vue de dessus à l'échelle : le grand cercle = 4 m. Une enceinte en hauteur est dessinée à sa
// distance horizontale (distance x cos élévation).
static constexpr float kSceneMeters = 4.f;
static ImVec2 speakerPos(ImVec2 c, float R, const Speaker& sp) {
    return scenePos(c, R * (float)sp.dist / kSceneMeters, sp.az, sp.el);
}

struct SceneEdit { bool moved = false, commit = false, srcMoved = false; };

// L : disposition affichée, modifiable à la souris (glisser une enceinte = nouvel azimut et distance)
static SceneEdit drawScene(Scene* sc, Layout& L, const float* inLv, const float* outLv, float height, float s) {
    SceneEdit ed;
    static int s_drag = -1, s_dragSrc = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##scene", ImVec2(w, height));      // capte la souris sur toute la vue
    const bool sceneHovered = ImGui::IsItemHovered();
    const ImVec2 c(p0.x + w * 0.5f, p0.y + height * 0.5f + 6 * s);
    const float R = std::min(w, height) * 0.36f;

    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), u32(kPanel), 10 * s);
    // anneaux de distance : 1, 2, 3 et 4 m
    for (int k = 4; k >= 1; --k) dl->AddCircleFilled(c, R * (0.25f * k + 0.08f), u32(kAccent, 0.012f * k), 96);
    dl->AddCircle(c, R, u32(kBorder), 128, 1.5f * s);
    for (int k = 1; k <= 3; ++k) dl->AddCircle(c, R * k / kSceneMeters, u32(kBorder, 0.45f), 96, 1.f * s);
    dl->AddLine(ImVec2(c.x - R * 1.08f, c.y), ImVec2(c.x + R * 1.08f, c.y), u32(kBorder, 0.5f), 1.f);
    dl->AddLine(ImVec2(c.x, c.y - R * 1.08f), ImVec2(c.x, c.y + R * 1.08f), u32(kBorder, 0.5f), 1.f);
    ImGui::PushFont(font(F_SMALL));
    auto centered = [&](ImVec2 at, ImU32 col, const char* t) {
        const ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(at.x - ts.x * 0.5f, at.y - ts.y * 0.5f), col, t);
    };
    for (int k = 1; k <= 4; ++k) {
            char mt[8]; snprintf(mt, sizeof mt, "%d m", k);
            dl->AddText(ImVec2(c.x + R * k / kSceneMeters * 0.7071f + 3 * s, c.y + R * k / kSceneMeters * 0.7071f), u32(kDim, 0.6f), mt);
        }
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
        spk[i] = speakerPos(c, R, L.speakers[i]);
    }
    const ImVec2 lfePos(p0.x + w - 46 * s, p0.y + height - 40 * s);
    if (lfeIdx >= 0) spk[lfeIdx] = lfePos;

    // déplacement des enceintes : glisser autour de l'auditeur, double-clic = position d'origine
    if (!L.headphones) {
        int hov = -1;
        const ImVec2 mp = ImGui::GetIO().MousePos;
        for (size_t i = 0; i < L.speakers.size(); ++i) {
            if (L.speakers[i].lfe) continue;
            const float hs = 14 * s;
            if (sceneHovered && std::fabs(mp.x - spk[i].x) <= hs && std::fabs(mp.y - spk[i].y) <= hs) hov = (int)i;
        }
        if (hov >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            double az, el; bool lfe;
            if (channelPosition(L.speakers[hov].id, L.mask(), az, el, lfe) && speakerMoveAllowed(L, hov, az)) {
                L.speakers[hov].az = az; L.speakers[hov].dist = kDefaultSpeakerDist; ed.moved = ed.commit = true;
            }
            s_drag = -1;
        } else if (hov >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            s_drag = hov;
        }
        if (s_drag >= (int)L.speakers.size()) s_drag = -1;
        if (s_drag >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dx = mp.x - c.x, dy = mp.y - c.y;
                if (dx * dx + dy * dy > 1.f) {
                    auto& sp = L.speakers[s_drag];
                    double az = std::round(std::atan2(-dx, -dy) * 180.0 / kPi);
                    if (az <= -180) az += 360;
                    // distance : rayon horizontal ramené à la distance réelle (enceintes en hauteur)
                    const double horiz = std::sqrt(dx * dx + dy * dy) / R * kSceneMeters;
                    double dist = horiz / std::max(0.2, std::cos(sp.el * kPi / 180.0));
                    dist = std::clamp(std::round(dist * 20.0) / 20.0, kMinSpeakerDist, kMaxSpeakerDist);
                    if (std::fabs(az - sp.az) >= 1.0 && speakerMoveAllowed(L, s_drag, az)) { sp.az = az; ed.moved = true; }
                    if (std::fabs(dist - sp.dist) >= 0.049) { sp.dist = dist; ed.moved = true; }
                }
            } else {
                ed.commit = true; s_drag = -1;
            }
        }
        if (hov >= 0 || s_drag >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ed.moved)
            for (size_t i = 0; i < L.speakers.size(); ++i)
                if (!L.speakers[i].lfe) spk[i] = speakerPos(c, R, L.speakers[i]);
    }

    // casque : les sources sont les haut-parleurs virtuels, déplaçables (angle et distance)
    auto srcPos = [&](const VirtualSource& v) {
        return L.headphones ? scenePos(c, R * (float)v.dist / kSceneMeters, v.az, v.el) : scenePos(c, R * 0.97f, v.az, v.el);
    };
    if (sc && L.headphones) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        int hov = -1;
        for (size_t k = 0; k < sc->sources.size(); ++k) {
            if (sc->sources[k].lfe) continue;
            const ImVec2 p = srcPos(sc->sources[k]);
            if (sceneHovered && std::fabs(mp.x - p.x) <= 11 * s && std::fabs(mp.y - p.y) <= 11 * s) hov = (int)k;
        }
        if (hov >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hov < (int)sc->inIds.size()) {
            const auto nom = sourcesFromChannels({ sc->inIds[hov] });
            if (!nom.empty()) { sc->sources[hov].az = nom[0].az; sc->sources[hov].dist = kDefaultSpeakerDist; ed.srcMoved = ed.commit = true; }
            s_dragSrc = -1;
        } else if (hov >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            s_dragSrc = hov;
        }
        if (s_dragSrc >= (int)sc->sources.size()) s_dragSrc = -1;
        if (s_dragSrc >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float dx = mp.x - c.x, dy = mp.y - c.y;
                if (dx * dx + dy * dy > 1.f) {
                    auto& v = sc->sources[s_dragSrc];
                    double az = std::round(std::atan2(-dx, -dy) * 180.0 / kPi);
                    if (az <= -180) az += 360;
                    const double horiz = std::sqrt(dx * dx + dy * dy) / R * kSceneMeters;
                    double dist = horiz / std::max(0.2, std::cos(v.el * kPi / 180.0));
                    dist = std::clamp(std::round(dist * 20.0) / 20.0, kMinSpeakerDist, kMaxSpeakerDist);
                    if (std::fabs(az - v.az) >= 1.0 || std::fabs(dist - v.dist) >= 0.049) { v.az = az; v.dist = dist; ed.srcMoved = true; }
                }
            } else { ed.commit = true; s_dragSrc = -1; }
        }
        if (hov >= 0 || s_dragSrc >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // sources (et liaisons vers les enceintes)
    if (sc) {
        const bool ambi = !sc->sources.empty() && sc->sources[0].label.rfind("Ambi", 0) == 0;
        for (size_t k = 0; k < sc->sources.size(); ++k) {
            const auto& src = sc->sources[k];
            if (src.lfe && L.headphones) continue;
            int col = 0; for (int i = 0; i < sc->inChannels; ++i) if (src.w[i] != 0.f) { col = i; break; }
            const float lv = inLv ? std::min(1.f, (ambi ? inLv[0] : inLv[std::min(col, 31)]) * 1.6f) : 0.f;
            const ImVec2 sp = src.lfe ? ImVec2(lfePos.x - 34 * s, lfePos.y) : srcPos(src);
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
            const float rr = (ambi ? 5.f : L.headphones ? 9.f : 7.f) * s;
            if (L.headphones && (int)k == s_dragSrc) {
                char dt[48]; snprintf(dt, sizeof dt, "%.0f°  %.2f m", src.az, src.dist);
                centered(ImVec2(sp.x, sp.y - rr - 12 * s), u32(kOrange), dt);
            }
            if (L.headphones && !src.lfe && k < sc->inIds.size())
                centered(ImVec2(sp.x, sp.y + rr + 9 * s), u32(kText, 0.75f), shortName(sc->inIds[k]));
            dl->AddCircleFilled(sp, rr + 6 * s * lv, u32(kOrange, 0.18f * lv), 24);
            dl->AddCircleFilled(sp, rr, u32(mix(v4(120, 80, 40), kOrange, 0.35f + 0.65f * lv)), 24);
            if (ImGui::IsMouseHoveringRect(sp - ImVec2(rr, rr), sp + ImVec2(rr, rr))) {
                ImGui::BeginTooltip();
                ImGui::Text("Source : %s", src.label.c_str());
                if (!src.lfe) ImGui::Text("azimut %.0f°, élévation %.0f°%s", src.az, src.el, L.headphones ? "" : "");
                if (L.headphones && !src.lfe) {
                    ImGui::Text("distance %.2f m", src.dist);
                    ImGui::TextDisabled("glisser : angle et distance · double-clic : position d'origine");
                }
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
            const bool dragging = (int)i == s_drag;
            dl->AddRect(p - ImVec2(hs, hs), p + ImVec2(hs, hs), u32(dragging ? kOrange : base), 4 * s, 0, (dragging ? 2.5f : 1.5f) * s);
            dl->AddCircleFilled(p, hs * 0.45f, u32(kBg, 0.7f), 16);
            const char* nm = shortName(sp.id);
            centered(ImVec2(p.x, p.y + hs + 10 * s), u32(kText, 0.85f), nm);
            if (dragging) {
                char dt[48]; snprintf(dt, sizeof dt, "%.0f°  %.2f m", sp.az, sp.dist);
                centered(ImVec2(p.x, p.y - hs - 12 * s), u32(kOrange), dt);
            }
            if (ImGui::IsMouseHoveringRect(p - ImVec2(hs, hs), p + ImVec2(hs, hs))) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", channelName(sp.id));
                if (!sp.lfe) ImGui::Text("azimut %.0f°, élévation %.0f°, distance %.2f m", sp.az, sp.el, sp.dist);
                ImGui::Text("niveau %.1f dB", 20 * std::log10(std::max(1e-5f, outLv ? outLv[std::min((int)i, 31)] : 0.f)));
                if (!sp.lfe) ImGui::TextDisabled("glisser : angle et distance · double-clic : position d'origine");
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
        const auto al = computeDistanceAlignment(L);
        if (al.active) dl->AddText(ImVec2(lg.x + 252 * s, lg.y), u32(kGreen), "distances compensées");
    }
    const std::string title = L.headphones ? "Casque — rendu binaural" : "Enceintes — " + L.name;
    dl->AddText(ImVec2(p0.x + 14 * s, p0.y + 10 * s), u32(kDim), title.c_str());
    ImGui::PopFont();
    return ed;
}

// Casque : mémorise les haut-parleurs virtuels déplacés (clé kVirtualSpeakersKey)
static void storeVirtualPositions(Settings& st, const Scene& sc) {
    auto o = parseSpeakerOverrides(st.speakerPos);
    auto& m = o[kVirtualSpeakersKey];
    m.clear();
    for (size_t k = 0; k < sc.sources.size() && k < sc.inIds.size(); ++k) {
        const auto& v = sc.sources[k];
        if (v.lfe) continue;
        const auto nom = sourcesFromChannels({ sc.inIds[k] });
        if (nom.empty()) continue;
        if (std::fabs(v.az - nom[0].az) >= 0.5 || std::fabs(v.dist - kDefaultSpeakerDist) >= 0.005)
            m[sc.inIds[k]] = SpeakerPos{ v.az, v.dist };
    }
    if (m.empty()) o.erase(kVirtualSpeakersKey);
    st.speakerPos = formatSpeakerOverrides(o);
}

// Mémorise dans les réglages les angles et distances qui diffèrent de la position d'origine
static void storeSpeakerPositions(Settings& st, const Layout& L) {
    auto o = parseSpeakerOverrides(st.speakerPos);
    auto& m = o[L.name];
    m.clear();
    for (auto& sp : L.speakers) {
        double az, el; bool lfe;
        if (sp.lfe || !channelPosition(sp.id, L.mask(), az, el, lfe)) continue;
        if (std::fabs(sp.az - az) >= 0.5 || std::fabs(sp.dist - kDefaultSpeakerDist) >= 0.005)
            m[sp.id] = SpeakerPos{ sp.az, sp.dist };
    }
    if (m.empty()) o.erase(L.name);
    st.speakerPos = formatSpeakerOverrides(o);
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

static void outputSection(UiModel& m, UiActions& a) {
    // La sortie n'est pas choisie ici : Spatial3D suit celle de Windows (icône haut-parleur de la barre
    // des tâches, ou Paramètres > Son). Changer de sortie dans Windows y déplace le son 3D.
    ImGui::TextDisabled("Sortie audio");
    helpMarker("Spatial3D utilise la sortie choisie dans Windows (icône haut-parleur de la barre des tâches, "
               "ou Paramètres > Son) et la suit : choisissez-en une autre dans Windows, le son 3D y passe. "
               "Pendant le son 3D, Windows affiche « CABLE Input » : c'est normal, le son passe par Spatial3D.");
    if (m.outputName.empty()) ImGui::TextColored(kAmber, "Aucune sortie audio réelle");
    else if (m.outputLayoutName.empty()) ImGui::TextWrapped("%s", m.outputName.c_str());
    else ImGui::TextWrapped("%s  ·  %s", m.outputName.c_str(), m.outputLayoutName.c_str());
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
//  Interface unique : commandes à gauche, scène à droite
// ---------------------------------------------------------------------------------------------
static void optionsSection(UiModel& m, UiActions& a) {
    ImGui::TextDisabled("WASAPI");
    if (ImGui::Checkbox("Latence minimale", &m.settings.lowLatency)) a.settingsChanged = a.needRestart = true;
    helpMarker("Windows 10/11 : le moteur audio tourne à la période minimale du pilote (souvent 2,7 ms au lieu "
               "de 10 ms), en capture comme en sortie ; en exclusif, période matérielle minimale. Décochez si "
               "un vieux pilote produit des craquements.");
    // Exclusif proposé seulement sur une sortie libre : si une autre application y joue déjà, Spatial3D
    // envoie le son en partagé sur cette sortie (mélangé par Windows) au lieu de lui couper le son.
    if (m.outputBusyBy.empty()) {
        if (ImGui::Checkbox("Sortie exclusive", &m.settings.exclusive)) a.settingsChanged = a.needRestart = true;
        helpMarker("Spatial3D parle directement à la carte son, sans le mélangeur de Windows : latence plus faible "
                   "et moins de risques de pops. La carte est alors réservée à Spatial3D. Proposé seulement quand "
                   "aucune autre application ne joue sur cette sortie ; repli automatique en mode partagé si la "
                   "carte refuse.");
    } else {
        std::string who;
        for (auto& n : m.outputBusyBy) who += (who.empty() ? "" : ", ") + n;
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("Sortie exclusive non proposée : %s joue déjà sur cette sortie. Spatial3D y envoie le "
                           "son en mode partagé, avec elle.", who.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::Checkbox("Ne pas basculer la sortie Windows", &m.settings.keepDefault)) a.settingsChanged = a.needRestart = true;
    helpMarker("Cochez si vous préférez choisir « CABLE Input » vous-même dans chaque application.");
    ImGui::Spacing();
    ImGui::TextDisabled("Démarrage");
    if (ImGui::Checkbox("Avec Windows (zone de notification)", &m.settings.startWithWindows)) {
        a.settingsChanged = true; a.startWithWindowsChanged = true;
    }
    if (ImGui::Checkbox("Son 3D activé au lancement", &m.settings.liveOnLaunch)) a.settingsChanged = true;
    if (ImGui::Checkbox("Fermer = réduire dans la zone de notification", &m.settings.closeToTray)) a.settingsChanged = true;
    ImGui::Spacing();
    if (ImGui::Button("Rétablir le son normal", ImVec2(-FLT_MIN, 0))) a.restoreSound = true;
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushFont(font(F_SMALL));
    ImGui::TextWrapped("Spatial3D %s — BJMPC Informatique. Dear ImGui (MIT), VB-Cable (VB-Audio), "
                       "HRTF KEMAR (MIT Media Lab).", m.version.c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

static void mainView(UiModel& m, UiActions& a, float s) {
    static float in[32], out[32];
    smooth(in, m.liveIn, 32); smooth(out, m.liveOut, 32);
    const float leftW = std::clamp(ImGui::GetContentRegionAvail().x * 0.27f, 290 * s, 360 * s);
    ImGui::BeginChild("##left", ImVec2(leftW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    // Marche / arrêt
    const bool on = m.liveRunning;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? v4(127, 29, 29) : v4(13, 120, 110));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? v4(153, 27, 27) : v4(17, 148, 136));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? v4(185, 28, 28) : v4(20, 170, 156));
    ImGui::PushFont(font(F_HEAD));
    ImGui::BeginDisabled(!m.vbCableInstalled || m.liveStarting);
    if (ImGui::Button(m.liveStarting ? "Démarrage…" : on ? "■  Arrêter" : "▶  Activer le son 3D", ImVec2(-FLT_MIN, 46 * s))) a.toggleLive = true;
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    if (!m.vbCableInstalled) {
        wrappedColored(kAmber, "VB-Cable n'est pas installé (ou l'ordinateur n'a pas redémarré depuis).");
        if (ImGui::Button("Télécharger VB-Cable", ImVec2(-FLT_MIN, 0))) a.openVbCableSite = true;
    }
    if (!m.liveError.empty()) wrappedColored(kRed, m.liveError);
    if (!m.liveWarning.empty()) wrappedColored(kAmber, m.liveWarning);
    ImGui::Spacing();

    beginCard("##out");
    outputSection(m, a);
    endCard();
    ImGui::Spacing();

    // État condensé : le détail est dans les infobulles
    beginCard("##state");
    if (ImGui::BeginTable("##st", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const std::string& v, const ImVec4* col = nullptr) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
            ImGui::TableNextColumn();
            if (col) ImGui::TextColored(*col, "%s", v.c_str()); else ImGui::TextUnformatted(v.c_str());
        };
        char b[200];
        if (!m.liveRunning) row("Son 3D", "arrêté");
        else {
            if (m.totalLatencyMs > 0) {
                snprintf(b, sizeof b, "%.1f ms", m.totalLatencyMs);
                row("Latence", b, m.totalLatencyMs <= 25 ? &kGreen : nullptr);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Capture %.1f ms + marge %.0f ms (point bas %.1f) + traitement %.1f ms + sortie %.1f ms\n"
                                      "Dérive d'horloge %+.0f ppm, entrée %.0f Hz",
                                      m.capturePeriodMs, m.marginMs, m.lowMs, m.processingMs, m.outputBufferMs, m.driftPpm, m.inHz);
            } else row("Latence", "…");
            if (m.followVolume) {
                if (m.windowsMuted) row("Volume", "sourdine", &kAmber);
                else { snprintf(b, sizeof b, "%.0f %%", m.windowsVolumePct); row("Volume", b); }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%.1f dB — %s", m.windowsVolumeDb,
                                      m.volumeDigital ? "appliqué par Spatial3D (exclusif sans volume matériel)" : "synchronisé avec la carte son");
            }
            const int bad = m.underruns + m.captureGlitches;
            snprintf(b, sizeof b, "%d", bad);
            row("Coupures", b, bad > 3 ? &kAmber : bad == 0 ? &kGreen : nullptr);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%d fondues côté sortie (sans clic), %d pertes côté VB-Cable", m.underruns, m.captureGlitches);
            row("WASAPI", m.liveOutputMode.rfind("exclusif", 0) == 0 ? "exclusif" : m.liveOutputMode.find("IAudioClient3") != std::string::npos ? "partagé faible latence" : "partagé");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Capture : %s\nSortie : %s", m.liveCaptureMode.c_str(), m.liveOutputMode.c_str());
        }
        ImGui::EndTable();
    }
    endCard();
    ImGui::Spacing();

    beginCard("##opt");
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    if (ImGui::CollapsingHeader("Options")) optionsSection(m, a);
    ImGui::PopStyleColor();
    endCard();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const float hintH = ImGui::GetTextLineHeightWithSpacing() + 4 * s;
    const float sceneH = ImGui::GetContentRegionAvail().y - 118 * s - hintH - 2 * ImGui::GetStyle().ItemSpacing.y;
    Scene* sc = m.liveRunning ? &m.liveScene : nullptr;
    Layout& L = sc ? m.liveScene.out : m.previewLayout;
    SceneEdit ed = drawScene(sc, L, in, out, sceneH, s);
    {
        const auto ov = parseSpeakerOverrides(m.settings.speakerPos);
        const bool custom = ov.count(L.headphones ? std::string(kVirtualSpeakersKey) : L.name) > 0;
        ImGui::PushFont(font(F_SMALL));
        ImGui::TextDisabled(!L.headphones ? "Glissez une enceinte (angle et distance) pour la placer comme chez vous."
                            : sc ? "Glissez un haut-parleur virtuel (angle et distance) pour placer le son autour de vous."
                                 : "Activez le son 3D pour placer les haut-parleurs virtuels autour de vous.");
        if (custom) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Positions d'origine")) {
                if (L.headphones) {
                    if (sc) {
                        for (size_t k = 0; k < sc->sources.size() && k < sc->inIds.size(); ++k) {
                            const auto nom = sourcesFromChannels({ sc->inIds[k] });
                            if (!nom.empty() && !sc->sources[k].lfe) { sc->sources[k].az = nom[0].az; sc->sources[k].dist = kDefaultSpeakerDist; }
                        }
                        ed.srcMoved = ed.commit = true;
                    } else {
                        auto o2 = ov; o2.erase(kVirtualSpeakersKey);
                        m.settings.speakerPos = formatSpeakerOverrides(o2); a.settingsChanged = true;
                    }
                } else {
                    for (auto& sp : L.speakers) {
                        double az, el; bool lfe;
                        if (!sp.lfe && channelPosition(sp.id, L.mask(), az, el, lfe)) { sp.az = az; sp.dist = kDefaultSpeakerDist; }
                    }
                    ed.moved = ed.commit = true;
                }
            }
        }
        ImGui::PopFont();
    }
    if (ed.moved) a.speakersMoved = true;
    if (ed.srcMoved) a.virtualMoved = true;
    if (ed.commit) {
        if (L.headphones) { if (sc) storeVirtualPositions(m.settings, *sc); }
        else storeSpeakerPositions(m.settings, L);
        a.settingsChanged = true;
    }
    drawMeters(L, sc ? sc->inChannels : 0, in, out, sc, s);
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

    mainView(m, a, s);
    m.forceTab = -1;
    ImGui::End();
}

} // namespace s3d
