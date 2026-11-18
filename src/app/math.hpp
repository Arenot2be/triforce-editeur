// Petits outils de géométrie : vecteurs, matrices (colonnes, comme GLSL), rotations X puis Y puis Z.
#pragma once
#include <array>
#include <cmath>

namespace te {

struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double a, double b, double c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double k) const { return {x * k, y * k, z * k}; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    double& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
};
inline double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(const V3& a) { return std::sqrt(dot(a, a)); }
inline V3 norm(const V3& a) { double l = len(a); return l > 0 ? a * (1 / l) : a; }

using M3 = std::array<std::array<double, 3>, 3>;   // (lignes)
inline M3 eulerMat(const V3& r) {
    double cx = std::cos(r.x), sx = std::sin(r.x), cy = std::cos(r.y), sy = std::sin(r.y), cz = std::cos(r.z), sz = std::sin(r.z);
    return {{{cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz}, {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz}, {-sy, sx * cy, cx * cy}}};
}
inline M3 matMul(const M3& a, const M3& b) {
    M3 o{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    return o;
}
inline V3 matVec(const M3& m, const V3& v) {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}
inline M3 transpose(const M3& m) { M3 o{}; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i][j] = m[j][i]; return o; }
inline M3 axisMat(int i, double a) {
    double c = std::cos(a), s = std::sin(a);
    if (i == 0) return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
    if (i == 1) return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}
inline double wrapA(double a) { return std::atan2(std::sin(a), std::cos(a)); }

// angles d'une matrice : des deux écritures possibles, celle qui reste la plus proche de ref (comme la page web)
inline V3 matEuler(const M3& M, const V3* ref) {
    const double PI = 3.14159265358979323846;
    double sy = std::max(-1.0, std::min(1.0, -M[2][0])), y = std::asin(sy);
    V3 sols[2];
    int ns = 1;
    if (std::fabs(std::cos(y)) > 1e-6) {
        double x = std::atan2(M[2][1], M[2][2]), z = std::atan2(M[1][0], M[0][0]);
        sols[0] = {x, y, z};
        sols[1] = {wrapA(x + PI), wrapA(PI - y), wrapA(z + PI)};
        ns = 2;
    } else {
        double z = ref ? ref->z : 0, cz = std::cos(z), szz = std::sin(z);
        double x = sy > 0 ? std::atan2(M[0][1] * cz + M[1][1] * szz, M[1][1] * cz - M[0][1] * szz)
                          : std::atan2(-M[0][1] * cz - M[1][1] * szz, M[1][1] * cz - M[0][1] * szz);
        sols[0] = {wrapA(x), y, z};
        M3 chk = eulerMat(sols[0]);
        if (std::fabs(chk[0][1] - M[0][1]) + std::fabs(chk[1][1] - M[1][1]) > 1e-3) sols[0] = {std::atan2(-M[1][2], M[1][1]), y, 0};
    }
    auto clean = [](double v) { return std::fabs(v) < 1e-7 ? 0.0 : v; };
    if (!ref) return {clean(sols[0].x), clean(sols[0].y), clean(sols[0].z)};
    auto cost = [&](const V3& e) { return std::fabs(wrapA(e.x - ref->x)) + std::fabs(wrapA(e.y - ref->y)) + std::fabs(wrapA(e.z - ref->z)); };
    V3 best = sols[0];
    if (ns == 2 && cost(sols[1]) < cost(sols[0]) - 1e-9) best = sols[1];
    V3 out;
    for (int k = 0; k < 3; k++) {
        double o = (*ref)[k] + wrapA(best[k] - (*ref)[k]);
        out[k] = std::fabs(o) < 1e-7 ? 0 : (std::fabs(o) > PI + 1e-6 ? wrapA(o) : o);
    }
    return out;
}

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // (colonnes)
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }
};
inline M4 mul(const M4& a, const M4& b) {
    M4 o;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.at(r, k) * b.at(k, c);
            o.at(r, c) = s;
        }
    return o;
}
// perspective (profondeur 0..1, Vulkan) ; y vers le haut (la fenêtre est retournée par le viewport)
inline M4 perspective(double fovy, double aspect, double n, double f) {
    M4 o;
    double t = 1 / std::tan(fovy / 2);
    o.at(0, 0) = float(t / aspect);
    o.at(1, 1) = float(t);
    o.at(2, 2) = float(f / (n - f));
    o.at(2, 3) = float(n * f / (n - f));
    o.at(3, 2) = -1;
    o.at(3, 3) = 0;
    return o;
}
inline M4 ortho(double l, double r, double b, double t, double n, double f) {
    M4 o;
    o.at(0, 0) = float(2 / (r - l));
    o.at(1, 1) = float(2 / (t - b));
    o.at(2, 2) = float(1 / (n - f));
    o.at(0, 3) = float(-(r + l) / (r - l));
    o.at(1, 3) = float(-(t + b) / (t - b));
    o.at(2, 3) = float(n / (n - f));
    return o;
}
inline M4 lookAt(const V3& eye, const V3& at, const V3& up) {
    V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
    M4 o;
    o.at(0, 0) = float(s.x); o.at(0, 1) = float(s.y); o.at(0, 2) = float(s.z);
    o.at(1, 0) = float(u.x); o.at(1, 1) = float(u.y); o.at(1, 2) = float(u.z);
    o.at(2, 0) = float(-f.x); o.at(2, 1) = float(-f.y); o.at(2, 2) = float(-f.z);
    o.at(0, 3) = float(-dot(s, eye));
    o.at(1, 3) = float(-dot(u, eye));
    o.at(2, 3) = float(dot(f, eye));
    return o;
}
inline bool invert(const M4& a, M4& out) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-30f) return false;
    for (int i = 0; i < 16; i++) out.m[i] = inv[i] / det;
    return true;
}

}  // namespace te
