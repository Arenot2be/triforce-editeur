// Décodeur ASTC, port de triforce_modkit/astc.js : écrit d'après la spécification Khronos ; les modes de couleur HDR
// (2, 3, 7, 11, 14, 15) suivent astcenc_color_unquantize.cpp de l'encodeur de référence d'ARM (licence Apache 2.0,
// https://github.com/ARM-software/astc-encoder).
#include "core/astc.hpp"

#include <array>
#include <cmath>
#include <set>

namespace te {

namespace {
const int Q[21][4] = {{2, 1, 0, 0}, {3, 0, 1, 0}, {4, 2, 0, 0}, {5, 0, 0, 1}, {6, 1, 1, 0}, {8, 3, 0, 0}, {10, 1, 0, 1},
                      {12, 2, 1, 0}, {16, 4, 0, 0}, {20, 2, 0, 1}, {24, 3, 1, 0}, {32, 5, 0, 0}, {40, 3, 0, 1}, {48, 4, 1, 0},
                      {64, 6, 0, 0}, {80, 4, 0, 1}, {96, 5, 1, 0}, {128, 7, 0, 0}, {160, 5, 0, 1}, {192, 6, 1, 0}, {256, 8, 0, 0}};
int iseBits(int n, int q) {
    return n * Q[q][1] + (Q[q][2] ? (8 * n + 4) / 5 : 0) + (Q[q][3] ? (7 * n + 2) / 3 : 0);
}
uint32_t bitsAt(const uint8_t* blk, int pos, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int p = pos + i;
        if (p < 128) v |= uint32_t((blk[p >> 3] >> (p & 7)) & 1) << i;
    }
    return v;
}

std::array<int, 5> trits(int T) {
    auto b = [&](int i) { return (T >> i) & 1; };
    int C, t4, t3, t2, t1, t0;
    if (((T >> 2) & 7) == 7) { C = (((T >> 5) & 7) << 2) | (T & 3); t4 = 2; t3 = 2; }
    else { C = T & 0x1F; if (((T >> 5) & 3) == 3) { t4 = 2; t3 = b(7); } else { t4 = b(7); t3 = (T >> 5) & 3; } }
    auto c = [&](int i) { return (C >> i) & 1; };
    if ((C & 3) == 3) { t2 = 2; t1 = c(4); t0 = (c(3) << 1) | (c(2) & (c(3) ^ 1)); }
    else if (((C >> 2) & 3) == 3) { t2 = 2; t1 = 2; t0 = C & 3; }
    else { t2 = c(4); t1 = (C >> 2) & 3; t0 = (c(1) << 1) | (c(0) & (c(1) ^ 1)); }
    return {t0, t1, t2, t3, t4};
}
std::array<int, 3> quints(int Qv) {
    auto b = [&](int i) { return (Qv >> i) & 1; };
    int q0, q1, q2, C;
    if (((Qv >> 1) & 3) == 3 && ((Qv >> 5) & 3) == 0) {
        q2 = (b(0) << 2) | ((b(4) & (b(0) ^ 1)) << 1) | (b(3) & (b(0) ^ 1)); q1 = 4; q0 = 4;
    } else {
        if (((Qv >> 1) & 3) == 3) { q2 = 4; C = (((Qv >> 3) & 3) << 3) | ((~(Qv >> 5) & 3) << 1) | b(0); }
        else { q2 = (Qv >> 5) & 3; C = Qv & 0x1F; }
        if ((C & 7) == 5) { q1 = 4; q0 = (C >> 3) & 3; } else { q1 = (C >> 3) & 3; q0 = C & 7; }
    }
    return {q0, q1, q2};
}
void decodeISE(const uint8_t* blk, int start, int count, int q, int* out) {
    int end = start + iseBits(count, q);
    auto read = [&](int p, int n) -> int { if (p >= end) return 0; if (p + n > end) n = end - p; return int(bitsAt(blk, p, n)); };
    int nb = Q[q][1];
    int m[72] = {};
    int pos = start;
    if (Q[q][2]) {
        static const int tb[5] = {2, 2, 1, 2, 1};
        // Zen 5 : auto-vectorisation via /Qpar + AVX-512
        for (int blk5 = 0; blk5 < count; blk5 += 5) {
            int T = 0, shift = 0;
            for (int k = 0; k < 5; k++) {
                m[blk5 + k] = read(pos, nb); pos += nb;
                T |= read(pos, tb[k]) << shift; pos += tb[k]; shift += tb[k];
            }
            auto t = trits(T);
            for (int k = 0; k < 5 && blk5 + k < count; k++) out[blk5 + k] = (t[size_t(k)] << nb) | m[blk5 + k];
        }
    } else if (Q[q][3]) {
        static const int qb[3] = {3, 2, 2};
        // Zen 5 : auto-vectorisation via /Qpar + AVX-512
        for (int blk3 = 0; blk3 < count; blk3 += 3) {
            int Qv = 0, shift = 0;
            for (int k = 0; k < 3; k++) {
                m[blk3 + k] = read(pos, nb); pos += nb;
                Qv |= read(pos, qb[k]) << shift; pos += qb[k]; shift += qb[k];
            }
            auto qq = quints(Qv);
            for (int k = 0; k < 3 && blk3 + k < count; k++) out[blk3 + k] = (qq[size_t(k)] << nb) | m[blk3 + k];
        }
    } else {
        // Boucle simple : le compilateur MSVC avec /Qpar /arch:AVX512 auto-vectorise
        for (int k = 0; k < count; k++) { out[k] = read(pos, nb); pos += nb; }
    }
}

int repl(int v, int from, int to) {
    int r = 0, have = 0;
    while (have < to) {
        int s = to - have;
        r = s >= from ? (r << from) | v : (r << s) | (v >> (from - s));
        have += std::min(from, s);
    }
    return r;
}
int unqColor(int v, int q) {
    int nb = Q[q][1], tr = Q[q][2], qu = Q[q][3];
    if (!tr && !qu) return repl(v, nb, 8);
    int m = v & ((1 << nb) - 1), D = v >> nb;
    auto bit = [&](int i) { return (m >> i) & 1; };
    int A = bit(0) ? 0x1FF : 0, B = 0, C = 0;
    int b = bit(1), c = bit(2), d = bit(3), e = bit(4), f = bit(5);
    if (tr) {
        if (nb == 1) C = 204;
        else if (nb == 2) { C = 93; B = (b << 8) | (b << 4) | (b << 2) | (b << 1); }
        else if (nb == 3) { C = 44; B = (c << 8) | (b << 7) | (c << 3) | (b << 2) | (c << 1) | b; }
        else if (nb == 4) { C = 22; B = (d << 8) | (c << 7) | (b << 6) | (d << 2) | (c << 1) | b; }
        else if (nb == 5) { C = 11; B = (e << 8) | (d << 7) | (c << 6) | (b << 5) | (e << 1) | d; }
        else if (nb == 6) { C = 5; B = (f << 8) | (e << 7) | (d << 6) | (c << 5) | (b << 4) | f; }
    } else {
        if (nb == 1) C = 113;
        else if (nb == 2) { C = 54; B = (b << 8) | (b << 3) | (b << 2); }
        else if (nb == 3) { C = 26; B = (c << 8) | (b << 7) | (c << 2) | (b << 1) | c; }
        else if (nb == 4) { C = 13; B = (d << 8) | (c << 7) | (b << 6) | (d << 1) | c; }
        else if (nb == 5) { C = 6; B = (e << 8) | (d << 7) | (c << 6) | (b << 5) | e; }
    }
    int T = D * C + B;
    T ^= A;
    T = (A & 0x80) | (T >> 2);
    return T & 0xFF;
}
int unqWeight(int v, int q) {
    int n = Q[q][0], nb = Q[q][1], tr = Q[q][2], qu = Q[q][3], r;
    if (!tr && !qu) r = repl(v, nb, 6);
    else if (n == 3) { static const int t[3] = {0, 32, 63}; r = t[v]; }
    else if (n == 5) { static const int t[5] = {0, 16, 32, 47, 63}; r = t[v]; }
    else {
        int m = v & ((1 << nb) - 1), D = v >> nb;
        int b = (m >> 1) & 1, c = (m >> 2) & 1;
        int A = (m & 1) ? 0x7F : 0, B = 0, C = 0;
        if (tr) { if (nb == 1) C = 50; else if (nb == 2) { C = 23; B = (b << 6) | (b << 2) | b; } else if (nb == 3) { C = 11; B = (c << 6) | (b << 5) | (c << 1) | b; } }
        else { if (nb == 1) C = 28; else if (nb == 2) { C = 13; B = (b << 6) | (b << 1); } }
        int T = D * C + B;
        T ^= A;
        r = (A & 0x20) | (T >> 2);
    }
    return r > 32 ? r + 1 : r;
}

using V4 = std::array<int, 4>;
int clamp8(int x) { return x < 0 ? 0 : x > 255 ? 255 : x; }
void bts(int a, int b, int& oa, int& ob) { b = (b >> 1) | (a & 0x80); a = (a >> 1) & 0x3F; if (a & 0x20) a -= 0x40; oa = a; ob = b; }
V4 bc(int r, int g, int b, int a) { return {(r + b) >> 1, (g + b) >> 1, b, a}; }

bool endpoints(int mode, const int* v, V4& e0, V4& e1) {
    switch (mode) {
        case 0: e0 = {v[0], v[0], v[0], 255}; e1 = {v[1], v[1], v[1], 255}; return true;
        case 1: { int L0 = (v[0] >> 2) | (v[1] & 0xC0), L1 = std::min(L0 + (v[1] & 0x3F), 255); e0 = {L0, L0, L0, 255}; e1 = {L1, L1, L1, 255}; return true; }
        case 4: e0 = {v[0], v[0], v[0], v[2]}; e1 = {v[1], v[1], v[1], v[3]}; return true;
        case 5: {
            int a1, b0, a3, b2;
            bts(v[1], v[0], a1, b0); bts(v[3], v[2], a3, b2);
            e0 = {b0, b0, b0, b2}; e1 = {clamp8(b0 + a1), clamp8(b0 + a1), clamp8(b0 + a1), clamp8(b2 + a3)};
            return true;
        }
        case 6: e0 = {(v[0] * v[3]) >> 8, (v[1] * v[3]) >> 8, (v[2] * v[3]) >> 8, 255}; e1 = {v[0], v[1], v[2], 255}; return true;
        case 8:
            if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) { e0 = {v[0], v[2], v[4], 255}; e1 = {v[1], v[3], v[5], 255}; }
            else { e0 = bc(v[1], v[3], v[5], 255); e1 = bc(v[0], v[2], v[4], 255); }
            return true;
        case 9: {
            int a1, b0, a3, b2, a5, b4;
            bts(v[1], v[0], a1, b0); bts(v[3], v[2], a3, b2); bts(v[5], v[4], a5, b4);
            if (a1 + a3 + a5 >= 0) { e0 = {b0, b2, b4, 255}; e1 = {b0 + a1, b2 + a3, b4 + a5, 255}; }
            else { e0 = bc(b0 + a1, b2 + a3, b4 + a5, 255); e1 = bc(b0, b2, b4, 255); }
            for (auto& x : e0) x = clamp8(x);
            for (auto& x : e1) x = clamp8(x);
            return true;
        }
        case 10: e0 = {(v[0] * v[3]) >> 8, (v[1] * v[3]) >> 8, (v[2] * v[3]) >> 8, v[4]}; e1 = {v[0], v[1], v[2], v[5]}; return true;
        case 12:
            if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) { e0 = {v[0], v[2], v[4], v[6]}; e1 = {v[1], v[3], v[5], v[7]}; }
            else { e0 = bc(v[1], v[3], v[5], v[7]); e1 = bc(v[0], v[2], v[4], v[6]); }
            return true;
        case 13: {
            int a1, b0, a3, b2, a5, b4, a7, b6;
            bts(v[1], v[0], a1, b0); bts(v[3], v[2], a3, b2); bts(v[5], v[4], a5, b4); bts(v[7], v[6], a7, b6);
            if (a1 + a3 + a5 >= 0) { e0 = {b0, b2, b4, b6}; e1 = {b0 + a1, b2 + a3, b4 + a5, b6 + a7}; }
            else { e0 = bc(b0 + a1, b2 + a3, b4 + a5, b6 + a7); e1 = bc(b0, b2, b4, b6); }
            for (auto& x : e0) x = clamp8(x);
            for (auto& x : e1) x = clamp8(x);
            return true;
        }
        default: return false;
    }
}

void hdrRGBO(const int* v, V4& e0, V4& e1) {
    int v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];
    int modeval = ((v0 & 0xC0) >> 6) | (((v1 & 0x80) >> 7) << 2) | (((v2 & 0x80) >> 7) << 3);
    int majcomp, mode;
    if ((modeval & 0xC) != 0xC) { majcomp = modeval >> 2; mode = modeval & 3; }
    else if (modeval != 0xF) { majcomp = modeval & 3; mode = 4; }
    else { majcomp = 0; mode = 5; }
    int red = v0 & 0x3F, green = v1 & 0x1F, blue = v2 & 0x1F, scale = v3 & 0x1F;
    int b0 = (v1 >> 6) & 1, b1 = (v1 >> 5) & 1, b2 = (v2 >> 6) & 1, b3 = (v2 >> 5) & 1, b4 = (v3 >> 7) & 1, b5 = (v3 >> 6) & 1, b6 = (v3 >> 5) & 1;
    int oh = 1 << mode;
    if (oh & 0x30) green |= b0 << 6;
    if (oh & 0x3A) green |= b1 << 5;
    if (oh & 0x30) blue |= b2 << 6;
    if (oh & 0x3A) blue |= b3 << 5;
    if (oh & 0x3D) scale |= b6 << 5;
    if (oh & 0x2D) scale |= b5 << 6;
    if (oh & 0x04) scale |= b4 << 7;
    if (oh & 0x3B) red |= b4 << 6;
    if (oh & 0x04) red |= b3 << 6;
    if (oh & 0x10) red |= b5 << 7;
    if (oh & 0x0F) red |= b2 << 7;
    if (oh & 0x05) red |= b1 << 8;
    if (oh & 0x0A) red |= b0 << 8;
    if (oh & 0x05) red |= b0 << 9;
    if (oh & 0x02) red |= b6 << 9;
    if (oh & 0x01) red |= b3 << 10;
    if (oh & 0x02) red |= b5 << 10;
    static const int shs[6] = {1, 1, 2, 3, 4, 5};
    int sh = shs[mode];
    red <<= sh; green <<= sh; blue <<= sh; scale <<= sh;
    if (mode != 5) { green = red - green; blue = red - blue; }
    if (majcomp == 1) std::swap(red, green);
    else if (majcomp == 2) std::swap(red, blue);
    auto m0 = [](int x) { return std::max(x, 0); };
    e0 = {m0(red - scale) << 4, m0(green - scale) << 4, m0(blue - scale) << 4, 0x7800};
    e1 = {m0(red) << 4, m0(green) << 4, m0(blue) << 4, 0x7800};
}
void hdrRGB(const int* v, V4& e0, V4& e1) {
    int v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3], v4 = v[4], v5 = v[5];
    int modeval = ((v1 & 0x80) >> 7) | (((v2 & 0x80) >> 7) << 1) | (((v3 & 0x80) >> 7) << 2);
    int majcomp = ((v4 & 0x80) >> 7) | (((v5 & 0x80) >> 7) << 1);
    if (majcomp == 3) {
        e0 = {v0 << 8, v2 << 8, (v4 & 0x7F) << 9, 0x7800};
        e1 = {v1 << 8, v3 << 8, (v5 & 0x7F) << 9, 0x7800};
        return;
    }
    int a = v0 | ((v1 & 0x40) << 2), b0 = v2 & 0x3F, b1 = v3 & 0x3F, c = v1 & 0x3F, d0 = v4 & 0x7F, d1 = v5 & 0x7F;
    static const int dbitsT[8] = {7, 6, 7, 6, 5, 6, 5, 6};
    int dbits = dbitsT[modeval];
    int t0 = (v2 >> 6) & 1, t1 = (v3 >> 6) & 1, t2 = (v4 >> 6) & 1, t3 = (v5 >> 6) & 1, t4 = (v4 >> 5) & 1, t5 = (v5 >> 5) & 1;
    int oh = 1 << modeval;
    if (oh & 0xA4) a |= t0 << 9;
    if (oh & 0x8) a |= t2 << 9;
    if (oh & 0x50) a |= t4 << 9;
    if (oh & 0x50) a |= t5 << 10;
    if (oh & 0xA0) a |= t1 << 10;
    if (oh & 0xC0) a |= t2 << 11;
    if (oh & 0x4) c |= t1 << 6;
    if (oh & 0xE8) c |= t3 << 6;
    if (oh & 0x20) c |= t2 << 7;
    if (oh & 0x5B) { b0 |= t0 << 6; b1 |= t1 << 6; }
    if (oh & 0x12) { b0 |= t2 << 7; b1 |= t3 << 7; }
    if (oh & 0xAF) { d0 |= t4 << 5; d1 |= t5 << 5; }
    if (oh & 0x5) { d0 |= t2 << 6; d1 |= t3 << 6; }
    int sx = 32 - dbits;
    d0 = int32_t(uint32_t(d0) << sx) >> sx;
    d1 = int32_t(uint32_t(d1) << sx) >> sx;
    int vs = (modeval >> 1) ^ 3;
    int k = 1 << vs;
    a *= k; b0 *= k; b1 *= k; c *= k; d0 *= k; d1 *= k;
    auto cl = [](int x) { return std::min(std::max(x, 0), 4095); };
    int r1 = cl(a), g1 = cl(a - b0), bl1 = cl(a - b1), r0 = cl(a - c), g0 = cl(a - b0 - c - d0), bl0 = cl(a - b1 - c - d1);
    if (majcomp == 1) { std::swap(r0, g0); std::swap(r1, g1); }
    else if (majcomp == 2) { std::swap(r0, bl0); std::swap(r1, bl1); }
    e0 = {r0 << 4, g0 << 4, bl0 << 4, 0x7800};
    e1 = {r1 << 4, g1 << 4, bl1 << 4, 0x7800};
}
void hdrAlpha(int v6, int v7, int& o0, int& o1) {
    int modeval = ((v6 >> 7) & 1) | ((v7 >> 6) & 2);
    v6 &= 0x7F; v7 &= 0x7F;
    if (modeval == 3) { o0 = v6 << 5; o1 = v7 << 5; }
    else {
        v6 |= (v7 << (modeval + 1)) & 0x780;
        v7 &= 0x3F >> modeval; v7 ^= 32 >> modeval; v7 -= 32 >> modeval;
        v6 = v6 << (4 - modeval); v7 = v7 * (1 << (4 - modeval));
        v7 = std::min(std::max(v6 + v7, 0), 0xFFF);
        o0 = v6; o1 = v7;
    }
    o0 <<= 4; o1 <<= 4;
}

struct EP { V4 e0, e1; bool lns[4] = {false, false, false, false}; bool ok = false; };

EP endpoints16(int mode, const int* v, bool srgb, bool hdr) {
    EP r;
    V4 a, b;
    if (endpoints(mode, v, a, b)) {
        for (int k = 0; k < 4; k++) {
            r.e0[size_t(k)] = srgb ? (a[size_t(k)] << 8) | 0x80 : a[size_t(k)] * 257;
            r.e1[size_t(k)] = srgb ? (b[size_t(k)] << 8) | 0x80 : b[size_t(k)] * 257;
        }
        r.ok = true;
        return r;
    }
    bool alphaHdr = false, alphaDefault = false;
    V4 e0, e1;
    if (mode == 2) {
        int y0, y1;
        if (v[1] >= v[0]) { y0 = v[0] << 4; y1 = v[1] << 4; } else { y0 = (v[1] << 4) + 8; y1 = (v[0] << 4) - 8; }
        e0 = {y0 << 4, y0 << 4, y0 << 4, 0}; e1 = {y1 << 4, y1 << 4, y1 << 4, 0}; alphaDefault = true;
    } else if (mode == 3) {
        int y0, y1;
        if (v[0] & 0x80) { y0 = ((v[1] & 0xE0) << 4) | ((v[0] & 0x7F) << 2); y1 = (v[1] & 0x1F) << 2; }
        else { y0 = ((v[1] & 0xF0) << 4) | ((v[0] & 0x7F) << 1); y1 = (v[1] & 0xF) << 1; }
        y1 = std::min(y1 + y0, 0xFFF);
        e0 = {y0 << 4, y0 << 4, y0 << 4, 0}; e1 = {y1 << 4, y1 << 4, y1 << 4, 0}; alphaDefault = true;
    } else if (mode == 7) { hdrRGBO(v, e0, e1); alphaDefault = true; }
    else if (mode == 11) { hdrRGB(v, e0, e1); alphaDefault = true; }
    else if (mode == 14) { hdrRGB(v, e0, e1); e0[3] = v[6]; e1[3] = v[7]; }
    else { hdrRGB(v, e0, e1); hdrAlpha(v[6], v[7], e0[3], e1[3]); alphaHdr = true; }
    if (alphaDefault) {
        if (hdr) { e0[3] = 0x7800; e1[3] = 0x7800; alphaHdr = true; } else { e0[3] = 0xFF; e1[3] = 0xFF; }
    }
    if (!hdr) return r;   // (profil LDR : erreur pour cette partition)
    bool lns[4] = {true, true, true, alphaHdr};
    for (int k = 0; k < 4; k++) {
        r.lns[k] = lns[k];
        r.e0[size_t(k)] = lns[k] ? e0[size_t(k)] : (srgb ? (e0[size_t(k)] << 8) | 0x80 : e0[size_t(k)] * 257);
        r.e1[size_t(k)] = lns[k] ? e1[size_t(k)] : (srgb ? (e1[size_t(k)] << 8) | 0x80 : e1[size_t(k)] * 257);
    }
    r.ok = true;
    return r;
}

double half(int h) {
    double s = (h & 0x8000) ? -1 : 1;
    int ex = (h >> 10) & 0x1F, m = h & 0x3FF;
    if (ex == 0) return s * m * std::ldexp(1.0, -24);
    if (ex == 31) return m ? NAN : s * INFINITY;
    return s * (1 + m / 1024.0) * std::ldexp(1.0, ex - 15);
}
int lns2half(int p) {
    int mc = p & 0x7FF, ec = int(uint32_t(p) >> 11);
    int mt = mc < 512 ? mc * 3 : mc < 1536 ? mc * 4 - 512 : mc * 5 - 2048;
    return std::min((ec << 10) | (mt >> 3), 0x7BFF);
}
int to8(double f) { return f > 0 ? (f < 1 ? int(std::floor(f * 255 + 0.5)) : 255) : 0; }

uint32_t hash52(uint32_t p) {
    p ^= p >> 15; p -= p << 17; p += p << 7; p += p << 4; p ^= p >> 5;
    p += p << 16; p ^= p >> 7; p ^= p >> 3; p ^= p << 6; p ^= p >> 17;
    return p;
}
int partitionOf(int seed, int x, int y, int count, bool small) {
    if (small) { x <<= 1; y <<= 1; }
    seed += (count - 1) * 1024;
    uint32_t r = hash52(uint32_t(seed));
    int s[12] = {int(r & 15), int((r >> 4) & 15), int((r >> 8) & 15), int((r >> 12) & 15), int((r >> 16) & 15), int((r >> 20) & 15),
                 int((r >> 24) & 15), int((r >> 28) & 15), int((r >> 18) & 15), int((r >> 22) & 15), int((r >> 26) & 15),
                 int(((r >> 30) | (r << 2)) & 15)};
    for (auto& v : s) v *= v;
    int sh1, sh2;
    if (seed & 1) { sh1 = seed & 2 ? 4 : 5; sh2 = count == 3 ? 6 : 5; }
    else { sh1 = count == 3 ? 6 : 5; sh2 = seed & 2 ? 4 : 5; }
    int sh3 = seed & 0x10 ? sh1 : sh2;
    for (int i = 0; i < 12; i++) s[i] >>= i < 8 ? (i % 2 ? sh2 : sh1) : sh3;
    int a = int((uint32_t(s[0] * x + s[1] * y) + (r >> 14)) & 0x3F), b = int((uint32_t(s[2] * x + s[3] * y) + (r >> 10)) & 0x3F);
    int c = int((uint32_t(s[4] * x + s[5] * y) + (r >> 6)) & 0x3F), d = int((uint32_t(s[6] * x + s[7] * y) + (r >> 2)) & 0x3F);
    if (count < 4) d = 0;
    if (count < 3) c = 0;
    if (a >= b && a >= c && a >= d) return 0;
    if (b >= c && b >= d) return 1;
    if (c >= d) return 2;
    return 3;
}

struct Ctx {
    uint8_t* out;
    int w, h;
    bool srgb, hdr, patch;
    AstcStats st;
    std::vector<std::pair<int, int>> list;
};
const uint8_t ERR[4] = {255, 0, 255, 255};

void decodeBlock(Ctx& c, const uint8_t* blk, int bw, int bh, int ox, int oy) {
    auto put = [&](int x, int y, const uint8_t* px) {
        int X = ox + x, Y = oy + y;
        if (X >= c.w || Y >= c.h) return;
        std::memcpy(c.out + (size_t(Y) * size_t(c.w) + size_t(X)) * 4, px, 4);
    };
    auto fill = [&](const uint8_t* px) { for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) put(x, y, px); };
    auto fail = [&](const char* why) { c.st.why[why]++; c.st.n++; c.list.emplace_back(ox, oy); fill(ERR); };
    if (c.patch) {
        bool zero = true;
        for (int k = 0; k < 16; k++) if (blk[k]) { zero = false; break; }
        if (zero) { c.st.empty++; c.list.emplace_back(ox, oy); static const uint8_t Z[4] = {0, 0, 0, 0}; fill(Z); return; }
    }
    int mode = int(bitsAt(blk, 0, 11));
    if ((mode & 0x1FF) == 0x1FC) {
        if (bitsAt(blk, 10, 2) != 3) return fail("bloc uni réservé");
        uint32_t ls = bitsAt(blk, 12, 13), hs = bitsAt(blk, 25, 13), lt = bitsAt(blk, 38, 13), ht = bitsAt(blk, 51, 13);
        if ((ls >= hs || lt >= ht) && !(ls == 0x1FFF && hs == 0x1FFF && lt == 0x1FFF && ht == 0x1FFF)) return fail("bloc uni hors limites");
        uint8_t px[4];
        if (mode & 0x200) {
            c.st.hdr++;
            if (!c.hdr) return fail("bloc uni HDR");
            for (int k = 0; k < 4; k++) px[k] = uint8_t(to8(half(int(bitsAt(blk, 64 + 16 * k, 16)))));
        } else {
            for (int k = 0; k < 4; k++) px[k] = uint8_t(bitsAt(blk, 64 + 16 * k, 16) >> 8);
        }
        fill(px);
        return;
    }
    int bq = (mode >> 4) & 1, H = (mode >> 9) & 1, D = (mode >> 10) & 1, A = (mode >> 5) & 3, gx, gy;
    if (mode & 3) {
        bq |= (mode & 3) << 1;
        int B = (mode >> 7) & 3;
        switch ((mode >> 2) & 3) {
            case 0: gx = B + 4; gy = A + 2; break;
            case 1: gx = B + 8; gy = A + 2; break;
            case 2: gx = A + 2; gy = B + 8; break;
            default: B &= 1; if (mode & 0x100) { gx = B + 2; gy = A + 2; } else { gx = A + 2; gy = B + 6; }
        }
    } else {
        bq |= ((mode >> 2) & 3) << 1;
        if (((mode >> 2) & 3) == 0) return fail("mode de bloc réservé");
        int B = (mode >> 9) & 3;
        switch ((mode >> 7) & 3) {
            case 0: gx = 12; gy = A + 2; break;
            case 1: gx = A + 2; gy = 12; break;
            case 2: gx = A + 6; gy = B + 6; D = 0; H = 0; break;
            default: {
                int k = (mode >> 5) & 3;
                if (k == 0) { gx = 6; gy = 10; } else if (k == 1) { gx = 10; gy = 6; } else return fail("mode de bloc réservé");
            }
        }
    }
    int wq = bq - 2 + 6 * H, planes = D + 1, wcount = gx * gy * planes;
    if (wq < 0 || wq > 11 || wcount > 64 || gx > bw || gy > bh) return fail("grille de poids invalide");
    int wbits = iseBits(wcount, wq);
    if (wbits < 24 || wbits > 96) return fail("nombre de poids invalide");
    int parts = int(bitsAt(blk, 11, 2)) + 1;
    if (parts == 4 && D) return fail("4 partitions + double plan");
    int below = 128 - wbits, cstart, seed = 0;
    int cems[4];
    if (parts == 1) { cems[0] = int(bitsAt(blk, 13, 4)); cstart = 17; }
    else {
        seed = int(bitsAt(blk, 13, 10));
        uint32_t low = bitsAt(blk, 23, 6);
        cstart = 29;
        if ((low & 3) == 0) for (int i = 0; i < parts; i++) cems[i] = int((low >> 2) & 15);
        else {
            int hs = 3 * parts - 4;
            below -= hs;
            uint32_t enc = low | (bitsAt(blk, below, hs) << 6);
            int base = int(enc & 3);
            enc >>= 2;
            int cls[4], val[4];
            for (int i = 0; i < parts; i++) { cls[i] = int(enc & 1); enc >>= 1; }
            for (int i = 0; i < parts; i++) { val[i] = int(enc & 3); enc >>= 2; }
            for (int i = 0; i < parts; i++) cems[i] = ((cls[i] + base - 1) << 2) | val[i];
        }
    }
    int plane2 = -1;
    if (D) { below -= 2; plane2 = int(bitsAt(blk, below, 2)); }
    int nints = 0;
    for (int i = 0; i < parts; i++) nints += ((cems[i] >> 2) + 1) * 2;
    if (nints > 18) return fail("trop de couleurs");
    int avail = below - cstart, cq = -1;
    for (int q = 20; q >= 0; q--) if (iseBits(nints, q) <= avail) { cq = q; break; }
    if (cq < 4) return fail("couleurs trop peu précises");
    int craw[72];
    decodeISE(blk, cstart, nints, cq, craw);
    for (int i = 0; i < nints; i++) craw[i] = unqColor(craw[i], cq);
    EP eps[4];
    int ci = 0;
    bool bad = false;
    for (int i = 0; i < parts; i++) {
        int c2 = cems[i], n = ((c2 >> 2) + 1) * 2;
        eps[i] = endpoints16(c2, craw + ci, c.srgb, c.hdr);
        ci += n;
        if (c2 == 2 || c2 == 3 || c2 == 7 || c2 == 11 || c2 == 14 || c2 == 15) c.st.hdr++;
        if (!eps[i].ok) bad = true;
    }
    if (bad) { c.st.n++; c.st.why["HDR (profil LDR)"]++; }
    uint8_t rev[16];
    for (int i = 0; i < 16; i++) {
        uint8_t b = blk[15 - i], r = 0;
        for (int k = 0; k < 8; k++) r |= uint8_t(((b >> k) & 1) << (7 - k));
        rev[i] = r;
    }
    int wraw[72] = {};
    decodeISE(rev, 0, wcount, wq, wraw);
    for (int i = 0; i < wcount; i++) wraw[i] = unqWeight(wraw[i], wq);
    // Zen 5 : boucle de blending SIMD — /Qpar auto-vectorise, AVX-512 pour les flottants
    int Ds = (1024 + (bw >> 1)) / (bw - 1), Dt = (1024 + (bh >> 1)) / (bh - 1);
    bool small = bw * bh < 31;
    auto gw = [&](int pl, int i) { return i < gx * gy ? wraw[i * planes + pl] : 0; };
    for (int t = 0; t < bh; t++) {
        for (int s = 0; s < bw; s++) {
            int gs = ((Ds * s) * (gx - 1) + 32) >> 6, gt = ((Dt * t) * (gy - 1) + 32) >> 6;
            int js = gs >> 4, fs = gs & 15, jt = gt >> 4, ft = gt & 15, v0 = js + jt * gx;
            int w11 = (fs * ft + 8) >> 4, w10 = ft - w11, w01 = fs - w11, w00 = 16 - fs - ft + w11;
            auto wt = [&](int pl) { return (gw(pl, v0) * w00 + gw(pl, v0 + 1) * w01 + gw(pl, v0 + gx) * w10 + gw(pl, v0 + gx + 1) * w11 + 8) >> 4; };
            int w1 = wt(0), w2 = D ? wt(1) : w1;
            int p = parts > 1 ? partitionOf(seed, s, t, parts, small) : 0;
            const EP& ep = eps[p];
            if (!ep.ok) { put(s, t, ERR); continue; }
            uint8_t px[4];
            for (int k = 0; k < 4; k++) {
                int a = ep.e0[size_t(k)], b = ep.e1[size_t(k)], wk = k == plane2 ? w2 : w1;
                int cc = a + (((b - a) * wk + 32) >> 6);
                px[k] = uint8_t(ep.lns[k] ? to8(half(lns2half(cc))) : cc >> 8);
            }
            put(s, t, px);
        }
    }
}
}  // namespace

Bytes astcDecode(const uint8_t* data, size_t size, int w, int h, int bw, int bh, bool srgb, bool hdr, bool patch, AstcStats* st) {
    Bytes out(size_t(w) * size_t(h) * 4);
    Ctx c{out.data(), w, h, srgb, hdr, patch, {}, {}};
    int xb = (w + bw - 1) / bw, yb = (h + bh - 1) / bh;
    static const uint8_t zero[16] = {};
    for (int by = 0; by < yb; by++)
        for (int bx = 0; bx < xb; bx++) {
            size_t o = (size_t(by) * size_t(xb) + size_t(bx)) * 16;
            decodeBlock(c, o + 16 <= size ? data + o : zero, bw, bh, bx * bw, by * bh);
        }
    if (patch && !c.list.empty()) {
        std::set<std::pair<int, int>> bad;
        for (auto& [x, y] : c.list) bad.insert({x / bw, y / bh});
        double sum[4] = {0, 0, 0, 0};
        long cnt = 0;
        for (int y = 0; y < h; y += 2)
            for (int x = 0; x < w; x += 2) {
                if (bad.count({x / bw, y / bh})) continue;
                const uint8_t* p = out.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                for (int k = 0; k < 4; k++) sum[k] += p[k];
                cnt++;
            }
        uint8_t avg[4] = {128, 128, 128, 255};
        if (cnt) for (int k = 0; k < 4; k++) avg[k] = uint8_t(std::floor(sum[k] / double(cnt) + 0.5));
        for (auto& [bx, by] : c.list)
            for (int y = by; y < std::min(h, by + bh); y++)
                for (int x = bx; x < std::min(w, bx + bw); x++) std::memcpy(out.data() + (size_t(y) * size_t(w) + size_t(x)) * 4, avg, 4);
        c.st.patched = int(c.list.size());
    }
    if (st) *st = c.st;
    return out;
}

}  // namespace te
