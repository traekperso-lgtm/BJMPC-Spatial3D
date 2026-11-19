// Spatial3D — VBAP (Vector Base Amplitude Panning, Pulkki 1997)
// 2D (paires) pour les dispositions horizontales, 3D (triangles sur l'enveloppe convexe)
// dès qu'il y a des enceintes en hauteur. Haut-parleurs imaginaires pour fermer la sphère.
#pragma once
#include "layout.h"
#include <vector>

namespace s3d {

struct Vec3 { double x, y, z; };
Vec3 dirVec(double azDeg, double elDeg);

class Panner {
public:
    explicit Panner(const Layout& out);
    // Gains (amplitude, puissance normalisée) par haut-parleur de sortie ; LFE toujours à 0.
    // spreadDeg > 0 élargit la source (MDAP).
    std::vector<double> gains(double azDeg, double elDeg, double spreadDeg = 0) const;
    bool is3D() const { return is3D_; }
    size_t triangleCount() const { return tris_.size(); }

private:
    struct Pair { int a, b; double inv[4]; };
    struct Tri { int v[3]; double inv[9]; };

    std::vector<double> pointGains(const Vec3& p) const;    // non normalisés, sur pos_
    std::vector<double> finalize(std::vector<double> g) const;
    bool gains2D(double az, std::vector<double>& g) const;

    int outCount_ = 0;
    bool is3D_ = false;
    bool surround_ = true;               // 2D : le cercle est-il entièrement couvert ?
    std::vector<int> map_;               // point du panner -> index haut-parleur (-1 = imaginaire)
    std::vector<Vec3> pos_;
    std::vector<double> az_;             // 2D
    std::vector<Pair> pairs_;
    std::vector<Tri> tris_;
    std::vector<std::vector<int>> imagNeighbors_;
};

} // namespace s3d
