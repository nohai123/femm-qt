// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// region_map.cpp — planar face traversal of the preprocessor geometry.
// See region_map.h.

#include "region_map.h"
#include "document.h"

#include <QHash>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace {

uint64_t mix(uint64_t h, double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}
uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

double signedArea(const QPolygonF &p)
{
    double a = 0;
    for (int i = 0, n = p.size(); i < n; i++) {
        const QPointF &u = p[i], &v = p[(i + 1) % n];
        a += u.x() * v.y() - v.x() * u.y();
    }
    return 0.5 * a;
}

bool inside(const QPolygonF &p, double x, double y)
{
    bool c = false;
    for (int i = 0, n = p.size(), j = n - 1; i < n; j = i++) {
        const QPointF &a = p[i], &b = p[j];
        if (((a.y() > y) != (b.y() > y)) &&
            (x < (b.x() - a.x()) * (y - a.y()) / (b.y() - a.y()) + a.x()))
            c = !c;
    }
    return c;
}

} // namespace

void RegionMap::update(const DocumentBase &doc)
{
    uint64_t g = 1469598103934665603ULL;
    for (const auto &n : doc.nodelist) { g = mix(g, n.x); g = mix(g, n.y); }
    for (const auto &s : doc.linelist) g = mix(g, (uint64_t)s.n0 * 1000003u + (uint64_t)s.n1);
    for (const auto &a : doc.arclist) {
        g = mix(g, (uint64_t)a.n0 * 1000003u + (uint64_t)a.n1);
        g = mix(g, a.ArcLength);
    }
    if (!m_valid || g != m_geomHash) {
        buildFaces(doc);
        m_geomHash = g;
        m_valid = true;
        m_labelHash = 0;
    }

    uint64_t l = 7;
    for (const auto &b : doc.blocklist) { l = mix(l, b.x); l = mix(l, b.y); }
    if (l != m_labelHash) {
        assignLabels(doc);
        m_labelHash = l;
    }
}

void RegionMap::buildFaces(const DocumentBase &doc)
{
    m_faces.clear();
    const int nn = (int)doc.nodelist.size();

    // Every edge as a polyline from its first node to its second.
    struct Edge { int a, b; std::vector<QPointF> pts; };
    std::vector<Edge> edges;
    edges.reserve(doc.linelist.size() + doc.arclist.size());
    for (const auto &s : doc.linelist) {
        if (s.n0 < 0 || s.n1 < 0 || s.n0 >= nn || s.n1 >= nn || s.n0 == s.n1) continue;
        edges.push_back({s.n0, s.n1, {QPointF(doc.nodelist[s.n0].x, doc.nodelist[s.n0].y),
                                      QPointF(doc.nodelist[s.n1].x, doc.nodelist[s.n1].y)}});
    }
    for (const auto &a : doc.arclist) {
        if (a.n0 < 0 || a.n1 < 0 || a.n0 >= nn || a.n1 >= nn || a.n0 == a.n1) continue;
        CComplex c; double R;
        doc.GetCircle(a, c, R);
        int k = std::max(2, (int)std::ceil(a.ArcLength / 3.0));
        const double dt = a.ArcLength * M_PI / (180.0 * k);
        Edge e{a.n0, a.n1, {}};
        CComplex p = doc.nodelist[a.n0].CC();
        const CComplex rot(std::cos(dt), std::sin(dt));
        e.pts.push_back(QPointF(p.re, p.im));
        for (int i = 1; i < k; i++) {
            p = (p - c) * rot + c;
            e.pts.push_back(QPointF(p.re, p.im));
        }
        e.pts.push_back(QPointF(doc.nodelist[a.n1].x, doc.nodelist[a.n1].y));
        edges.push_back(std::move(e));
    }
    if (edges.empty()) return;

    // Half-edges: 2e runs a->b, 2e+1 runs b->a.
    const int nh = 2 * (int)edges.size();
    auto origin = [&](int h) { const Edge &e = edges[h / 2]; return (h & 1) ? e.b : e.a; };
    auto dest = [&](int h) { const Edge &e = edges[h / 2]; return (h & 1) ? e.a : e.b; };
    auto outAngle = [&](int h) {
        const Edge &e = edges[h / 2];
        const QPointF &p0 = (h & 1) ? e.pts.back() : e.pts.front();
        const QPointF &p1 = (h & 1) ? e.pts[e.pts.size() - 2] : e.pts[1];
        return std::atan2(p1.y() - p0.y(), p1.x() - p0.x());
    };

    // Outgoing half-edges per node, sorted counter-clockwise by direction.
    std::vector<std::vector<int>> out(nn);
    std::vector<double> ang(nh);
    for (int h = 0; h < nh; h++) { ang[h] = outAngle(h); out[origin(h)].push_back(h); }
    std::vector<int> posInOut(nh);
    for (auto &v : out) {
        std::sort(v.begin(), v.end(), [&](int a, int b) { return ang[a] < ang[b]; });
        for (int i = 0; i < (int)v.size(); i++) posInOut[v[i]] = i;
    }

    // With the face on the left, the next half-edge leaves the destination as
    // the first one clockwise from the way we came in.
    auto next = [&](int h) {
        const int t = h ^ 1;                       // twin, starts at dest(h)
        const auto &v = out[dest(h)];
        const int i = posInOut[t];
        return v[(i - 1 + (int)v.size()) % (int)v.size()];
    };

    std::vector<char> used(nh, 0);
    for (int h0 = 0; h0 < nh; h0++) {
        if (used[h0]) continue;
        QPolygonF poly;
        int h = h0, guard = 0;
        do {
            used[h] = 1;
            const Edge &e = edges[h / 2];
            if (h & 1) for (int i = (int)e.pts.size() - 1; i > 0; i--) poly << e.pts[i];
            else       for (int i = 0; i + 1 < (int)e.pts.size(); i++) poly << e.pts[i];
            h = next(h);
        } while (h != h0 && ++guard < nh + 1);
        if (poly.size() < 3) continue;
        const double a = signedArea(poly);
        if (a <= 1e-14) continue;   // outer boundary of a component, or degenerate
        Face f;
        f.poly = poly;
        f.area = a;
        f.bbox = poly.boundingRect();
        m_faces.push_back(std::move(f));
    }
    std::sort(m_faces.begin(), m_faces.end(),
              [](const Face &a, const Face &b) { return a.area > b.area; });
}

int RegionMap::faceAt(double x, double y) const
{
    // Faces are sorted largest first, so the last hit is the innermost.
    int best = -1;
    for (int i = 0; i < (int)m_faces.size(); i++) {
        const Face &f = m_faces[i];
        if (!f.bbox.contains(QPointF(x, y))) continue;
        if (inside(f.poly, x, y)) best = i;
    }
    return best;
}

void RegionMap::assignLabels(const DocumentBase &doc)
{
    for (auto &f : m_faces) f.labels.clear();
    for (int i = 0; i < (int)doc.blocklist.size(); i++) {
        int f = faceAt(doc.blocklist[i].x, doc.blocklist[i].y);
        if (f >= 0) m_faces[f].labels.push_back(i);
    }
}

QColor materialColor(const QString &name)
{
    const QString n = name.toLower();
    if (n.isEmpty() || n == "<none>") return QColor(0, 0, 0, 0);
    if (n == "<no mesh>") return QColor(255, 255, 255, 0);
    if (n.startsWith("air") || n.contains("vacuum")) return QColor(200, 225, 255, 60);
    if (n.contains("copper") || n.startsWith("cu") || n.contains("awg") || n.contains("litz") ||
        n.contains("magnet wire") || n.contains("coil"))
        return QColor(225, 130, 60, 110);
    if (n.contains("aluminum") || n.contains("aluminium")) return QColor(170, 180, 195, 110);
    if (n.contains("ndfeb") || n.startsWith("n3") || n.startsWith("n4") || n.startsWith("n5") ||
        n.contains("magnet") || n.contains("alnico") || n.contains("smco") ||
        n.contains("ferrite") || n.contains("ceramic"))
        return QColor(220, 60, 70, 110);
    if (n.contains("steel") || n.contains("iron") || n.startsWith("m-") || n.contains("1010") ||
        n.contains("1018") || n.contains("1020") || n.contains("hiperco") || n.contains("permalloy") ||
        n.contains("silicon") || n.contains("lamination"))
        return QColor(120, 125, 135, 110);
    if (n.contains("water")) return QColor(80, 150, 230, 90);
    // Stable pastel from the name
    const uint h = qHash(name);
    return QColor::fromHsv((int)(h % 360), 110, 230, 110);
}

QColor boundaryColor(const std::string &name)
{
    if (name.empty() || name == "<None>") return QColor();
    static const QColor palette[] = {
        QColor(214, 39, 40), QColor(44, 160, 44), QColor(148, 103, 189), QColor(255, 127, 14),
        QColor(23, 190, 207), QColor(227, 119, 194), QColor(140, 86, 75), QColor(188, 189, 34),
    };
    return palette[qHash(QString::fromStdString(name)) % 8];
}
