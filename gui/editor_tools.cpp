// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// editor_tools.cpp — editing tools for EditorWidget beyond drawing:
//
//   Trim (T)       click a piece of a line or arc to delete it. Crossings
//                  are already split into separate pieces by the PSLG
//                  bookkeeping, so "the piece between two intersections" is
//                  exactly one segment or arc.
//   Offset (O)     click a contour, move to the side, click (or type the
//                  distance + Enter): a parallel copy of the whole connected
//                  chain -- lines shifted, arcs concentric, corners joined.
//                  Insulation layers, air gaps, coil formers.
//   Dimension (D)  shows lengths and radii; click a segment to give it an
//                  exact length (the end nearer the click moves), an arc or
//                  circle to give it an exact radius.
//
// Plus the ghost preview used by the Pattern dialog.

#include "editor_widget.h"
#include "document.h"
#include "py_state.h"
#include "view_colors.h"

#include <QInputDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <set>

namespace {

const QColor kToolColor(230, 120, 0);
const QColor kGhostColor(0, 120, 215);

bool evalNumber(QString t, double &out)
{
    t = t.trimmed();
    if (t.isEmpty()) return false;
    t.replace(',', '.');
    bool ok;
    out = t.toDouble(&ok);
    if (ok) return true;
    return py_eval_double(t.toStdString().c_str(), &out);
}

// Intersection of the infinite lines a0-a1 and b0-b1.
bool lineIntersect(QPointF a0, QPointF a1, QPointF b0, QPointF b1, QPointF &out)
{
    const double d = (a1.x() - a0.x()) * (b1.y() - b0.y()) - (a1.y() - a0.y()) * (b1.x() - b0.x());
    if (std::abs(d) < 1e-14) return false;
    const double t = ((b0.x() - a0.x()) * (b1.y() - b0.y()) - (b0.y() - a0.y()) * (b1.x() - b0.x())) / d;
    out = a0 + t * (a1 - a0);
    return true;
}

} // namespace

// ================================================================
// Picking
// ================================================================

int EditorWidget::pickEdge(double x, double y, bool &isArc) const
{
    const double tol = pickTolerance();
    int best = -1;
    double bd = tol;
    isArc = false;
    if (int j = m_doc->ClosestSegment(x, y); j >= 0) {
        const double d = std::abs(m_doc->ShortestDistance(x, y, j));
        if (d < bd) { bd = d; best = j; }
    }
    if (int j = m_doc->ClosestArcSegment(x, y); j >= 0) {
        const double d = m_doc->ShortestDistanceFromArc(CComplex(x, y), m_doc->arclist[j]);
        if (d < bd) { best = j; isArc = true; }
    }
    return best;
}

// ================================================================
// Trim
// ================================================================

void EditorWidget::trimAt(double x, double y)
{
    bool isArc;
    const int k = pickEdge(x, y, isArc);
    if (k < 0) { emit statusMessage(tr("Click on a line or arc piece to remove it.")); return; }
    m_doc->pushUndo();
    m_doc->UnselectAll();
    int n0, n1;
    if (isArc) { n0 = m_doc->arclist[k].n0; n1 = m_doc->arclist[k].n1; m_doc->arclist[k].IsSelected = true; m_doc->DeleteSelectedArcSegments(); }
    else       { n0 = m_doc->linelist[k].n0; n1 = m_doc->linelist[k].n1; m_doc->linelist[k].IsSelected = true; m_doc->DeleteSelectedSegments(); }
    // End points left with nothing attached go too (higher index first)
    std::vector<int> ends{std::max(n0, n1), std::min(n0, n1)};
    for (int n : ends) {
        if (n < 0 || n >= (int)m_doc->nodelist.size()) continue;
        bool used = false;
        for (const auto &s : m_doc->linelist) used |= (s.n0 == n || s.n1 == n);
        for (const auto &a : m_doc->arclist) used |= (a.n0 == n || a.n1 == n);
        if (!used && m_doc->nodelist[n].BoundaryMarker.empty()) {
            m_doc->UnselectAll();
            m_doc->nodelist[n].IsSelected = true;
            m_doc->DeleteSelectedNodes();
        }
    }
    m_doc->UnselectAll();
    emit statusMessage(tr("Piece removed."));
    update();
}

// ================================================================
// Offset
// ================================================================

// The connected chain through the clicked edge: walk both ways while the
// node in between joins exactly two edges.
std::vector<EditorWidget::ChainEdge> EditorWidget::chainFrom(int edge, bool isArc, bool &closed) const
{
    struct E { bool arc; int i; int a, b; };
    std::vector<E> all;
    for (int i = 0; i < (int)m_doc->linelist.size(); i++) all.push_back({false, i, m_doc->linelist[i].n0, m_doc->linelist[i].n1});
    for (int i = 0; i < (int)m_doc->arclist.size(); i++) all.push_back({true, i, m_doc->arclist[i].n0, m_doc->arclist[i].n1});
    std::vector<std::vector<int>> at(m_doc->nodelist.size());
    for (int e = 0; e < (int)all.size(); e++) { at[all[e].a].push_back(e); at[all[e].b].push_back(e); }

    int start = -1;
    for (int e = 0; e < (int)all.size(); e++) if (all[e].arc == isArc && all[e].i == edge) start = e;
    std::vector<ChainEdge> chain;
    if (start < 0) return chain;

    // Forward from the start edge's b end, then backward from its a end
    std::deque<ChainEdge> dq;
    dq.push_back({all[start].arc, all[start].i, false});
    std::set<int> used{start};
    closed = false;
    auto walk = [&](int node, bool forward) {
        int cur = node;
        while (true) {
            if (at[cur].size() != 2) return;
            int nxt = -1;
            for (int e : at[cur]) if (!used.count(e)) nxt = e;
            if (nxt < 0) { closed = true; return; }
            used.insert(nxt);
            const bool rev = forward ? (all[nxt].b == cur) : (all[nxt].a == cur);
            if (forward) dq.push_back({all[nxt].arc, all[nxt].i, rev});
            else dq.push_front({all[nxt].arc, all[nxt].i, rev});
            cur = forward ? (rev ? all[nxt].a : all[nxt].b) : (rev ? all[nxt].b : all[nxt].a);
        }
    };
    walk(all[start].b, true);
    if (!closed) walk(all[start].a, false);
    chain.assign(dq.begin(), dq.end());
    return chain;
}

// Geometry of the chain offset by d to the LEFT of its direction of travel.
// Returns polyline pieces: lines as {p0, p1}, arcs with centre/radius.
std::vector<EditorWidget::OffsetPiece> EditorWidget::offsetChain(const std::vector<ChainEdge> &chain,
                                                                 bool closed, double d) const
{
    std::vector<OffsetPiece> out;
    for (const auto &ce : chain) {
        OffsetPiece p;
        p.arc = ce.arc;
        if (!ce.arc) {
            const auto &s = m_doc->linelist[ce.index];
            QPointF a(m_doc->nodelist[s.n0].x, m_doc->nodelist[s.n0].y), b(m_doc->nodelist[s.n1].x, m_doc->nodelist[s.n1].y);
            if (ce.reversed) std::swap(a, b);
            const QPointF t = b - a;
            const double L = std::hypot(t.x(), t.y());
            if (L <= 0) continue;
            const QPointF n(-t.y() / L, t.x() / L);   // left normal
            p.p0 = a + d * n; p.p1 = b + d * n;
        } else {
            const auto &arc = m_doc->arclist[ce.index];
            CComplex c; double R;
            m_doc->GetCircle(arc, c, R);
            p.centre = QPointF(c.re, c.im);
            // Stored arcs run CCW n0->n1; travelling CCW the centre is on the left
            const double R2 = ce.reversed ? R + d : R - d;
            if (R2 <= 0) return {};
            p.radius = R2;
            p.sweep = arc.ArcLength;
            p.maxSeg = arc.MaxSideLength;
            const QPointF a(m_doc->nodelist[arc.n0].x, m_doc->nodelist[arc.n0].y), b(m_doc->nodelist[arc.n1].x, m_doc->nodelist[arc.n1].y);
            const QPointF a2 = p.centre + (a - p.centre) * (R2 / R), b2 = p.centre + (b - p.centre) * (R2 / R);
            p.ccw0 = a2; p.ccw1 = b2;
            p.p0 = ce.reversed ? b2 : a2;
            p.p1 = ce.reversed ? a2 : b2;
        }
        out.push_back(p);
    }
    // Join corners between consecutive straight pieces
    const int n = (int)out.size();
    for (int i = 0; i < n; i++) {
        const int j = i + 1;
        if (j >= n && !closed) break;
        OffsetPiece &A = out[i], &B = out[j % n];
        if (!A.arc && !B.arc) {
            QPointF x;
            if (lineIntersect(A.p0, A.p1, B.p0, B.p1, x)) { A.p1 = x; B.p0 = x; }
        }
    }
    return out;
}

void EditorWidget::offsetPress(double x, double y)
{
    if (m_offsetChain.empty()) {
        bool isArc;
        const int k = pickEdge(x, y, isArc);
        if (k < 0) { emit statusMessage(tr("Click on the contour to offset.")); return; }
        m_offsetChain = chainFrom(k, isArc, m_offsetClosed);
        m_input[0].clear(); m_inputField = 0;
        emit statusMessage(tr("Offset: move to the side and click, or type the distance + Enter. Esc cancels."));
        update();
        return;
    }
    double d;
    if (!offsetDistance(d) || d == 0) return;
    const auto pieces = offsetChain(m_offsetChain, m_offsetClosed, d);
    if (pieces.empty()) { emit statusMessage(tr("Offset too large for an arc in the contour.")); return; }
    m_doc->pushUndo();
    const double tol = 1.0 / m_mag;
    auto node = [&](QPointF p) { m_doc->AddNode(p.x(), p.y(), tol); return m_doc->ClosestNode(p.x(), p.y()); };
    std::vector<QPointF> joins;   // gaps between pieces that do not meet
    for (size_t i = 0; i < pieces.size(); i++) {
        const auto &p = pieces[i];
        if (!p.arc) {
            int a = node(p.p0), b = node(p.p1);
            if (a != b) m_doc->AddSegment(a, b);
        } else {
            FArcSegment as;
            as.n0 = node(p.ccw0); as.n1 = node(p.ccw1);
            as.ArcLength = p.sweep; as.MaxSideLength = p.maxSeg;
            if (as.n0 != as.n1 || p.sweep >= 359.999) m_doc->AddArcSegment(as);
        }
        const size_t j = i + 1;
        if (j < pieces.size() || m_offsetClosed) {
            const auto &q = pieces[j % pieces.size()];
            if (std::hypot(p.p1.x() - q.p0.x(), p.p1.y() - q.p0.y()) > tol) {
                int a = node(p.p1), b = node(q.p0);
                if (a != b) m_doc->AddSegment(a, b);
            }
        }
    }
    m_offsetChain.clear();
    m_input[0].clear();
    emit statusMessage(tr("Offset %1 created.").arg(std::abs(d)));
    update();
}

// Signed distance (positive = left of the chain's direction): typed value
// with the side the cursor is on, or the cursor's own distance.
bool EditorWidget::offsetDistance(double &d) const
{
    if (m_offsetChain.empty()) return false;
    // Side and distance from the clicked chain's first piece
    const auto &ce = m_offsetChain.front();
    double side = 1, dist = 0;
    if (!ce.arc) {
        const auto &s = m_doc->linelist[ce.index];
        QPointF a(m_doc->nodelist[s.n0].x, m_doc->nodelist[s.n0].y), b(m_doc->nodelist[s.n1].x, m_doc->nodelist[s.n1].y);
        if (ce.reversed) std::swap(a, b);
        const QPointF t = b - a;
        const double L = std::hypot(t.x(), t.y());
        if (L <= 0) return false;
        const double cross = (t.x() * (m_my - a.y()) - t.y() * (m_mx - a.x())) / L;
        side = cross >= 0 ? 1 : -1;
        dist = std::abs(cross);
    } else {
        CComplex c; double R;
        m_doc->GetCircle(m_doc->arclist[ce.index], c, R);
        const double r = std::hypot(m_mx - c.re, m_my - c.im);
        const bool inside = r < R;
        // Inside = towards the centre = left when travelling CCW
        side = (inside != ce.reversed) ? 1 : -1;
        dist = std::abs(r - R);
    }
    double typed;
    if (evalNumber(m_input[0], typed)) dist = std::abs(typed);
    d = side * dist;
    return true;
}

// ================================================================
// Dimension
// ================================================================

void EditorWidget::dimensionAt(double x, double y)
{
    bool isArc;
    const int k = pickEdge(x, y, isArc);
    if (k < 0) { emit statusMessage(tr("Click a line to set its length, or an arc/circle to set its radius.")); return; }
    if (!isArc) {
        auto &s = m_doc->linelist[k];
        QPointF a(m_doc->nodelist[s.n0].x, m_doc->nodelist[s.n0].y), b(m_doc->nodelist[s.n1].x, m_doc->nodelist[s.n1].y);
        const double L = std::hypot(b.x() - a.x(), b.y() - a.y());
        // The end nearer the click moves
        const bool moveB = std::hypot(x - b.x(), y - b.y()) < std::hypot(x - a.x(), y - a.y());
        bool ok;
        const QString t = QInputDialog::getText(this, tr("Length"), tr("New length:"), QLineEdit::Normal,
                                                QString::number(L, 'g', 10), &ok);
        double L2;
        if (!ok || !evalNumber(t, L2) || L2 <= 0 || L <= 0) return;
        m_doc->pushUndo();
        const QPointF fixed = moveB ? a : b, moving = moveB ? b : a;
        const QPointF np = fixed + (moving - fixed) * (L2 / L);
        auto &nd = m_doc->nodelist[moveB ? s.n1 : s.n0];
        nd.x = np.x(); nd.y = np.y();
        m_doc->EnforcePSLG();
        emit statusMessage(tr("Length set to %1.").arg(L2));
    } else {
        auto &arc = m_doc->arclist[k];
        CComplex c; double R;
        m_doc->GetCircle(arc, c, R);
        // A full circle (two arcs on the same two nodes) grows about its centre
        int partner = -1;
        for (int i = 0; i < (int)m_doc->arclist.size(); i++)
            if (i != k && ((m_doc->arclist[i].n0 == arc.n1 && m_doc->arclist[i].n1 == arc.n0) ||
                           (m_doc->arclist[i].n0 == arc.n0 && m_doc->arclist[i].n1 == arc.n1)))
                partner = i;
        const bool circle = partner >= 0 && std::abs(arc.ArcLength + m_doc->arclist[partner].ArcLength - 360) < 1e-6;
        bool ok;
        const QString t = QInputDialog::getText(this, circle ? tr("Circle Radius") : tr("Arc Radius"),
                                                tr("New radius:"), QLineEdit::Normal, QString::number(R, 'g', 10), &ok);
        double R2;
        if (!ok || !evalNumber(t, R2) || R2 <= 0) return;
        if (circle) {
            m_doc->pushUndo();
            for (int n : {arc.n0, arc.n1}) {
                auto &nd = m_doc->nodelist[n];
                nd.x = c.re + (nd.x - c.re) * (R2 / R);
                nd.y = c.im + (nd.y - c.im) * (R2 / R);
            }
        } else {
            // Ends stay; the sweep follows from the chord
            const auto &p0 = m_doc->nodelist[arc.n0], &p1 = m_doc->nodelist[arc.n1];
            const double chord = std::hypot(p1.x - p0.x, p1.y - p0.y);
            if (R2 < chord / 2) {
                emit statusMessage(tr("The radius must be at least half the chord (%1).").arg(chord / 2));
                return;
            }
            m_doc->pushUndo();
            double sweep = 2 * std::asin(chord / (2 * R2)) * 180.0 / M_PI;
            if (arc.ArcLength > 180) sweep = 360 - sweep;
            arc.ArcLength = sweep;
        }
        m_doc->EnforcePSLG();
        emit statusMessage(tr("Radius set to %1.").arg(R2));
    }
    invalidateMeshDisplay();
    update();
}

void EditorWidget::drawDimensions(QPainter &p)
{
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    p.setPen(kToolColor);
    auto fmt = [](double v) { return QString::number(v, 'g', 5); };
    for (const auto &s : m_doc->linelist) {
        const auto &a = m_doc->nodelist[s.n0], &b = m_doc->nodelist[s.n1];
        const double L = std::hypot(b.x - a.x, b.y - a.y);
        const QPointF m = toScreen((a.x + b.x) / 2, (a.y + b.y) / 2);
        p.drawText(m + QPointF(4, -4), fmt(L));
    }
    std::set<std::pair<int, int>> seen;
    for (const auto &arc : m_doc->arclist) {
        const auto key = std::minmax(arc.n0, arc.n1);
        if (seen.count(key)) continue;
        seen.insert(key);
        CComplex c; double R;
        m_doc->GetCircle(arc, c, R);
        const double half = arc.ArcLength * M_PI / 360.0;
        CComplex mid = (m_doc->nodelist[arc.n0].CC() - c) * CComplex(std::cos(half), std::sin(half)) + c;
        p.drawText(toScreen(mid.re, mid.im) + QPointF(4, -4), QStringLiteral("R%1").arg(fmt(R)));
    }
}

// ================================================================
// Overlays for the tools, and the pattern preview
// ================================================================

void EditorWidget::drawToolOverlay(QPainter &p)
{
    if (!m_doc) return;
    if (m_sketchTool == SketchDimension) drawDimensions(p);

    if ((m_sketchTool == SketchTrim || m_sketchTool == SketchDimension || m_sketchTool == SketchExtend ||
         (m_sketchTool == SketchOffset && m_offsetChain.empty())) && m_hasCursor) {
        // Highlight the piece under the cursor
        bool isArc;
        const int k = pickEdge(m_mx, m_my, isArc);
        if (k >= 0) {
            p.save();
            p.setPen(QPen(m_sketchTool == SketchTrim ? QColor(220, 40, 40) : kToolColor, 3,
                          m_sketchTool == SketchTrim ? Qt::DashLine : Qt::SolidLine));
            if (!isArc) {
                const auto &s = m_doc->linelist[k];
                p.drawLine(toScreen(m_doc->nodelist[s.n0].x, m_doc->nodelist[s.n0].y),
                           toScreen(m_doc->nodelist[s.n1].x, m_doc->nodelist[s.n1].y));
            } else {
                const auto &a = m_doc->arclist[k];
                CComplex c; double R;
                m_doc->GetCircle(a, c, R);
                const double a0 = std::atan2(m_doc->nodelist[a.n0].y - c.im, m_doc->nodelist[a.n0].x - c.re) * 180 / M_PI;
                const QPointF sc = toScreen(c.re, c.im);
                const double rs = R * m_mag;
                QPainterPath path;
                path.arcMoveTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0);
                path.arcTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0, a.ArcLength);
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
            p.restore();
        }
    }

    if (m_sketchTool == SketchOffset && !m_offsetChain.empty()) {
        double d;
        if (offsetDistance(d)) {
            const auto pieces = offsetChain(m_offsetChain, m_offsetClosed, d);
            p.save();
            p.setPen(QPen(kGhostColor, 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            for (const auto &pc : pieces) {
                if (!pc.arc) { p.drawLine(toScreen(pc.p0.x(), pc.p0.y()), toScreen(pc.p1.x(), pc.p1.y())); continue; }
                const double a0 = std::atan2(pc.ccw0.y() - pc.centre.y(), pc.ccw0.x() - pc.centre.x()) * 180 / M_PI;
                const QPointF sc = toScreen(pc.centre.x(), pc.centre.y());
                const double rs = pc.radius * m_mag;
                QPainterPath path;
                path.arcMoveTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0);
                path.arcTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0, pc.sweep);
                p.drawPath(path);
            }
            // Distance box at the cursor
            const QString txt = QStringLiteral("d %1").arg(m_input[0].isEmpty() ? QString::number(std::abs(d), 'g', 6) : m_input[0]);
            QFont f = p.font(); f.setPointSize(10); p.setFont(f);
            const QPointF c = toScreen(m_mx, m_my) + QPointF(16, 18);
            const QRectF r(c, QSizeF(QFontMetrics(f).horizontalAdvance(txt) + 10, QFontMetrics(f).height() + 4));
            p.setPen(QPen(m_input[0].isEmpty() ? kGhostColor : kToolColor, m_input[0].isEmpty() ? 1 : 2));
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawRoundedRect(r, 3, 3);
            p.setPen(Qt::black);
            p.drawText(r, Qt::AlignCenter, txt);
            p.restore();
        }
    }

    // Pattern preview: ghosts of the selection
    if (!m_ghosts.empty()) {
        p.save();
        p.setPen(QPen(kGhostColor, 1.2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        std::vector<char> nodeSel(m_doc->nodelist.size(), 0);
        for (size_t i = 0; i < nodeSel.size(); i++) nodeSel[i] = m_doc->nodelist[i].IsSelected;
        for (const QTransform &t : m_ghosts) {
            auto scr = [&](double x, double y) { const QPointF q = t.map(QPointF(x, y)); return toScreen(q.x(), q.y()); };
            for (const auto &s : m_doc->linelist)
                if (s.IsSelected) p.drawLine(scr(m_doc->nodelist[s.n0].x, m_doc->nodelist[s.n0].y),
                                             scr(m_doc->nodelist[s.n1].x, m_doc->nodelist[s.n1].y));
            for (const auto &a : m_doc->arclist) {
                if (!a.IsSelected) continue;
                CComplex c; double R;
                m_doc->GetCircle(a, c, R);
                CComplex pt = m_doc->nodelist[a.n0].CC();
                const int k = std::max(4, (int)std::ceil(a.ArcLength / 5));
                const CComplex rot(std::cos(a.ArcLength * M_PI / 180 / k), std::sin(a.ArcLength * M_PI / 180 / k));
                QPointF prev = scr(pt.re, pt.im);
                for (int i = 0; i < k; i++) { pt = (pt - c) * rot + c; const QPointF cur = scr(pt.re, pt.im); p.drawLine(prev, cur); prev = cur; }
            }
            for (size_t i = 0; i < nodeSel.size(); i++)
                if (nodeSel[i]) p.drawRect(QRectF(scr(m_doc->nodelist[i].x, m_doc->nodelist[i].y) - QPointF(2, 2), QSizeF(4, 4)));
            for (const auto &b : m_doc->blocklist)
                if (b.IsSelected) p.drawEllipse(scr(b.x, b.y), 3, 3);
        }
        p.restore();
    }
}

bool EditorWidget::toolKey(QKeyEvent *event)
{
    if (m_sketchTool != SketchOffset || m_offsetChain.empty()) return false;
    const int key = event->key();
    if (key == Qt::Key_Escape) { m_offsetChain.clear(); m_input[0].clear(); update(); return true; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { offsetPress(m_mx, m_my); return true; }
    if (key == Qt::Key_Backspace) { m_input[0].chop(1); update(); return true; }
    const QString t = event->text();
    if (t.size() == 1 && (t[0].isDigit() || QStringLiteral(".,+-*/()").contains(t[0]) ||
                          (t[0].isLetter() && !m_input[0].isEmpty()))) {
        m_input[0] += t;
        update();
        return true;
    }
    return false;
}

// ================================================================
// Extend
// ================================================================

void EditorWidget::extendAt(double x, double y)
{
    bool isArc;
    const int k = pickEdge(x, y, isArc);
    if (k < 0 || isArc) { emit statusMessage(tr("Click near the end of a straight line.")); return; }
    const auto seg = m_doc->linelist[k];
    const auto &A = m_doc->nodelist[seg.n0], &B = m_doc->nodelist[seg.n1];
    // The end nearer the click is the one that grows
    const bool fromB = std::hypot(x - B.x, y - B.y) < std::hypot(x - A.x, y - A.y);
    const QPointF e = fromB ? QPointF(B.x, B.y) : QPointF(A.x, A.y);
    const QPointF o = fromB ? QPointF(A.x, A.y) : QPointF(B.x, B.y);
    const QPointF dir = e - o;
    const double L = std::hypot(dir.x(), dir.y());
    if (L <= 0) return;
    const QPointF u = dir / L;
    const int endNode = fromB ? seg.n1 : seg.n0;

    // Nearest hit of the ray e + t*u (t > 0) with any other line or arc
    double best = 1e300;
    for (int i = 0; i < (int)m_doc->linelist.size(); i++) {
        if (i == k) continue;
        const auto &s2 = m_doc->linelist[i];
        if (s2.n0 == endNode || s2.n1 == endNode) continue;
        const QPointF p(m_doc->nodelist[s2.n0].x, m_doc->nodelist[s2.n0].y), q(m_doc->nodelist[s2.n1].x, m_doc->nodelist[s2.n1].y);
        const QPointF r = q - p;
        const double den = u.x() * r.y() - u.y() * r.x();
        if (std::abs(den) < 1e-14) continue;
        const QPointF w = p - e;
        const double t = (w.x() * r.y() - w.y() * r.x()) / den;
        const double sp = (w.x() * u.y() - w.y() * u.x()) / den;
        if (t > 1e-9 * L && sp >= -1e-9 && sp <= 1 + 1e-9) best = std::min(best, t);
    }
    for (const auto &a : m_doc->arclist) {
        if (a.n0 == endNode || a.n1 == endNode) continue;
        CComplex c; double R;
        m_doc->GetCircle(a, c, R);
        const QPointF f = e - QPointF(c.re, c.im);
        const double bq = 2 * (f.x() * u.x() + f.y() * u.y()), cq = f.x() * f.x() + f.y() * f.y() - R * R;
        const double disc = bq * bq - 4 * cq;
        if (disc < 0) continue;
        for (double t : {(-bq - std::sqrt(disc)) / 2, (-bq + std::sqrt(disc)) / 2}) {
            if (t <= 1e-9 * L) continue;
            // On the arc's sweep?
            const QPointF hp = e + t * u;
            const CComplex p0 = m_doc->nodelist[a.n0].CC();
            const double a0 = std::atan2(p0.im - c.im, p0.re - c.re);
            double ah = std::atan2(hp.y() - c.im, hp.x() - c.re) - a0;
            while (ah < 0) ah += 2 * M_PI;
            if (ah <= a.ArcLength * M_PI / 180 + 1e-9) best = std::min(best, t);
        }
    }
    if (best >= 1e299) { emit statusMessage(tr("Nothing to extend to in that direction.")); return; }
    m_doc->pushUndo();
    auto &nd = m_doc->nodelist[endNode];
    nd.x = e.x() + best * u.x();
    nd.y = e.y() + best * u.y();
    m_doc->EnforcePSLG();
    invalidateMeshDisplay();
    emit statusMessage(tr("Extended by %1.").arg(best));
    update();
}

// ================================================================
// Air around the model
// ================================================================

int EditorWidget::fillUnlabeledOutside(const QRectF &model, const std::string &material)
{
    if (!m_doc) return 0;
    m_regions.update(*m_doc);
    int filled = 0;
    for (int f = 0; f < (int)m_regions.faces().size(); f++) {
        const auto face = m_regions.faces()[f];
        if (!face.labels.empty()) continue;
        if (model.adjusted(-1e-9, -1e-9, 1e-9, 1e-9).contains(face.bbox)) continue;   // a hole of the model
        // A point inside this region and in no smaller one
        const QRectF b = face.bbox;
        bool placed = false;
        for (int i = 1; i < 40 && !placed; i++)
            for (int j = 1; j < 40 && !placed; j++) {
                const double px = b.left() + b.width() * i / 40.0, py = b.top() + b.height() * j / 40.0;
                if (m_regions.faceAt(px, py) != f) continue;
                if (m_doc->AddBlockLabel(px, py, 0.02 * std::max(b.width(), b.height()))) {
                    m_doc->blocklist.back().BlockType = material;
                    placed = true;
                }
            }
        filled += placed;
        m_regions.update(*m_doc);
    }
    update();
    return filled;
}
