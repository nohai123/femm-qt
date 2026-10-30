// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// editor_sketch.cpp — CAD-style sketch tools and object snapping for
// EditorWidget.
//
// The classic FEMM workflow is two-stage: place nodes in Node mode, then
// connect pairs of them in Segment/Arc mode. The sketch tools collapse that
// into one step, the way a CAD sketcher works:
//
//   Line (L)       click, click, click ... -- a polyline; nodes are created
//                  where needed. Clicking the first point closes the contour;
//                  Enter, Esc, a double-click or a right-click ends it.
//   Rectangle (R)  two opposite corners.
//   Circle (C)     centre, then a point on the circle (two 180-degree arcs).
//   Arc (A)        start, end, then a point the arc passes through.
//
// While drawing, the point under the cursor snaps to existing geometry
// (endpoint, midpoint, arc centre, point on a line) or aligns horizontally /
// vertically with existing nodes; Shift constrains the direction to 45-degree
// steps. Typing a number sets the length (Tab: the angle) -- or width/height,
// or radius -- and Enter places the point; expressions such as 2*pi*r work.
//
// Everything goes through DocumentBase::AddNode / AddSegment / AddArcSegment,
// so intersections are split exactly as they are for the classic tools.

#include "editor_widget.h"
#include "coord_display.h"
#include "document.h"
#include "py_state.h"
#include "py_line_edit.h"

#include <QPainter>
#include <QPainterPath>
#include <QMap>
#include <QLineF>
#include <QMouseEvent>
#include "view_colors.h"
#include <QKeyEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <cmath>

namespace {

const QColor kSnapColor(230, 120, 0);
const QColor kPreviewColor(0, 120, 215);

double dist(double x0, double y0, double x1, double y1)
{
    return std::hypot(x1 - x0, y1 - y0);
}

// Parse a typed value; accepts a decimal comma and Python expressions.
bool evalInput(const QString &text, double &out)
{
    QString t = text.trimmed();
    if (t.isEmpty()) return false;
    t.replace(',', '.');
    bool ok;
    out = t.toDouble(&ok);
    if (ok) return true;
    return py_eval_double(t.toStdString().c_str(), &out);
}

const char *unitSuffix(int units)
{
    static const char *u[] = {"in", "mm", "cm", "m", "mil", "µm"};
    return (units >= 0 && units < 6) ? u[units] : "";
}

// Circle through three points. False if they are (nearly) collinear.
bool circumcircle(QPointF a, QPointF b, QPointF c, QPointF &ctr, double &r)
{
    const double d = 2 * (a.x() * (b.y() - c.y()) + b.x() * (c.y() - a.y()) +
                          c.x() * (a.y() - b.y()));
    const double scale = std::max({std::abs(a.x()), std::abs(a.y()), std::abs(b.x()),
                                   std::abs(b.y()), std::abs(c.x()), std::abs(c.y()), 1.0});
    if (std::abs(d) < 1e-12 * scale * scale) return false;
    const double a2 = a.x() * a.x() + a.y() * a.y();
    const double b2 = b.x() * b.x() + b.y() * b.y();
    const double c2 = c.x() * c.x() + c.y() * c.y();
    ctr.setX((a2 * (b.y() - c.y()) + b2 * (c.y() - a.y()) + c2 * (a.y() - b.y())) / d);
    ctr.setY((a2 * (c.x() - b.x()) + b2 * (a.x() - c.x()) + c2 * (b.x() - a.x())) / d);
    r = std::hypot(a.x() - ctr.x(), a.y() - ctr.y());
    return true;
}

// For an arc from s to e passing through m: the CCW start point, the CCW end
// point and the swept angle in degrees (FEMM arcs always run CCW n0 -> n1).
bool arcThrough(QPointF s, QPointF e, QPointF m, QPointF &ctr, double &r,
                bool &reversed, double &sweepDeg)
{
    if (!circumcircle(s, e, m, ctr, r)) return false;
    const double twoPi = 2 * M_PI;
    auto ang = [&](QPointF p) { return std::atan2(p.y() - ctr.y(), p.x() - ctr.x()); };
    const double as = ang(s), ae = ang(e), am = ang(m);
    const double span = std::fmod(ae - as + 2 * twoPi, twoPi);
    const double dm = std::fmod(am - as + 2 * twoPi, twoPi);
    if (dm < span) { reversed = false; sweepDeg = span * 180.0 / M_PI; }
    else           { reversed = true;  sweepDeg = (twoPi - span) * 180.0 / M_PI; }
    return true;
}

} // namespace

// ================================================================
// Tool selection
// ================================================================

void EditorWidget::setSketchTool(int tool)
{
    if (m_doc) m_doc->UnselectAll();
    m_sketchTool = tool;
    m_sketchPts.clear();
    m_chainStart = -1;
    m_input[0].clear(); m_input[1].clear(); m_inputField = 0;
    m_firstPoint = -1;
    // Selection, Move, Copy etc. act on segments while a drawing tool is up,
    // and on everything selected (group semantics) with the Select tool.
    if (tool == SketchSelect) m_editAction = 4;
    else if (tool != SketchNone) m_editAction = 1;
    emit sketchToolChanged(tool);
    emitSketchHint();
    update();
}

void EditorWidget::emitSketchHint()
{
    QString h;
    const bool started = !m_sketchPts.empty();
    switch (m_sketchTool) {
    case SketchLine:
        h = started ? tr("Line: click the next point, type a length (Tab: angle) + Enter. "
                         "Click the first point to close; Esc/Enter/right-click ends.")
                    : tr("Line: click the first point (Tab: enter coordinates).");
        break;
    case SketchRect:
        h = started ? tr("Rectangle: click the opposite corner, or type width (Tab: height) + Enter.")
                    : tr("Rectangle: click the first corner (Tab: enter coordinates).");
        break;
    case SketchCircle:
        h = started ? tr("Circle: click a point on the circle, or type the radius + Enter.")
                    : tr("Circle: click the centre (Tab: enter coordinates).");
        break;
    case SketchSelect:
        h = tr("Select: click an object (Shift adds), drag a window (left-to-right: inside, "
               "right-to-left: touching), drag a selected point to move it. Space: properties, "
               "Backspace: delete.");
        break;
    case SketchFill:
        h = m_fillMaterial.isEmpty()
              ? tr("Assign material: choose a material in the toolbar, then click inside regions.")
              : tr("Assign material: click inside a region to fill it with \"%1\".").arg(m_fillMaterial);
        break;
    case SketchArc3:
        h = m_sketchPts.empty() ? tr("Arc: click the start point.")
          : m_sketchPts.size() == 1 ? tr("Arc: click the end point.")
          : tr("Arc: click a point the arc passes through.");
        break;
    default:
        return;
    }
    emit statusMessage(h);
}

// ================================================================
// Snapping
// ================================================================

int EditorWidget::pickNode(double x, double y) const
{
    if (!m_doc || m_doc->nodelist.empty()) return -1;
    int j = m_doc->ClosestNode(x, y);
    if (j < 0) return -1;
    return m_doc->nodelist[j].GetDistance(x, y) <= 1.5 * pickTolerance() ? j : -1;
}

EditorWidget::Snap EditorWidget::computeSnap(double x, double y, bool shift, bool forNode) const
{
    Snap s;
    s.x = x; s.y = y;
    if (!m_doc) return s;
    const double tol = pickTolerance();
    const bool anchored = !m_sketchPts.empty();
    const QPointF anchor = anchored ? m_sketchPts.back() : QPointF();

    if (m_objectSnap && forNode) {
        // 1. Endpoints (existing nodes)
        int j = pickNode(x, y);
        if (j >= 0 && m_doc->nodelist[j].GetDistance(x, y) <= tol) {
            s.x = m_doc->nodelist[j].x; s.y = m_doc->nodelist[j].y;
            s.kind = SnapEndpoint;
            return s;
        }
        // 2. Arc centres and arc midpoints
        double best = tol;
        for (const auto &a : m_doc->arclist) {
            if (a.n0 < 0 || a.n1 < 0 || a.n0 >= (int)m_doc->nodelist.size() ||
                a.n1 >= (int)m_doc->nodelist.size()) continue;
            CComplex c; double R;
            m_doc->GetCircle(a, c, R);
            double d = dist(x, y, c.re, c.im);
            if (d < best) { best = d; s.x = c.re; s.y = c.im; s.kind = SnapCenter; }
            CComplex p0 = m_doc->nodelist[a.n0].CC();
            double half = a.ArcLength * M_PI / 360.0;
            CComplex mid = (p0 - c) * CComplex(std::cos(half), std::sin(half)) + c;
            d = dist(x, y, mid.re, mid.im);
            if (d < best) { best = d; s.x = mid.re; s.y = mid.im; s.kind = SnapMidpoint; }
        }
        // 3. Segment midpoints
        for (const auto &seg : m_doc->linelist) {
            const auto &n0 = m_doc->nodelist[seg.n0], &n1 = m_doc->nodelist[seg.n1];
            double mx = 0.5 * (n0.x + n1.x), my = 0.5 * (n0.y + n1.y);
            double d = dist(x, y, mx, my);
            if (d < best) { best = d; s.x = mx; s.y = my; s.kind = SnapMidpoint; }
        }
        if (s.kind != SnapFree) return s;

        // 4. On a segment / arc (nearest point)
        int k = m_doc->ClosestSegment(x, y);
        if (k >= 0 && std::abs(m_doc->ShortestDistance(x, y, k)) < tol) {
            const auto &n0 = m_doc->nodelist[m_doc->linelist[k].n0];
            const auto &n1 = m_doc->nodelist[m_doc->linelist[k].n1];
            double dx = n1.x - n0.x, dy = n1.y - n0.y, L2 = dx * dx + dy * dy;
            if (L2 > 0) {
                double t = std::clamp(((x - n0.x) * dx + (y - n0.y) * dy) / L2, 0.0, 1.0);
                s.x = n0.x + t * dx; s.y = n0.y + t * dy;
                s.kind = SnapOnLine;
                return s;
            }
        }
        k = m_doc->ClosestArcSegment(x, y);
        if (k >= 0 && m_doc->ShortestDistanceFromArc(CComplex(x, y), m_doc->arclist[k]) < tol) {
            CComplex c; double R;
            m_doc->GetCircle(m_doc->arclist[k], c, R);
            double a = std::atan2(y - c.im, x - c.re);
            s.x = c.re + R * std::cos(a); s.y = c.im + R * std::sin(a);
            s.kind = SnapOnLine;
            return s;
        }
    }

    // 5. Shift: constrain direction from the previous point to 45-degree steps
    if (shift && anchored) {
        double dx = x - anchor.x(), dy = y - anchor.y();
        double L = std::hypot(dx, dy);
        double a = std::round(std::atan2(dy, dx) / (M_PI / 4)) * (M_PI / 4);
        s.x = anchor.x() + L * std::cos(a);
        s.y = anchor.y() + L * std::sin(a);
        return s;
    }

    // 6. Grid
    if (m_snapToGrid && m_gridSize > 0) {
        s.x = m_gridSize * std::round(x / m_gridSize);
        s.y = m_gridSize * std::round(y / m_gridSize);
        s.kind = SnapGrid;
    }

    // 7. Horizontal / vertical alignment with existing nodes or the last point
    if (m_objectSnap && forNode) {
        double bx = tol, by = tol;
        auto consider = [&](double nx, double ny) {
            if (std::abs(s.x - nx) < bx && std::abs(s.y - ny) > tol) {
                bx = std::abs(s.x - nx); s.alignX = true; s.alignXFromY = ny; s.x = nx;
            }
            if (std::abs(s.y - ny) < by && std::abs(s.x - nx) > tol) {
                by = std::abs(s.y - ny); s.alignY = true; s.alignYFromX = nx; s.y = ny;
            }
        };
        if (anchored) consider(anchor.x(), anchor.y());
        for (const auto &n : m_doc->nodelist) consider(n.x, n.y);
    }
    return s;
}

// ================================================================
// Placing points
// ================================================================

int EditorWidget::nodeAtOrNew(double x, double y)
{
    const double d = 1.0 / m_mag;
    m_doc->AddNode(x, y, d);
    int j = m_doc->ClosestNode(x, y);
    if (j < 0 || m_doc->nodelist[j].GetDistance(x, y) > 2 * d) return -1;
    return j;
}

void EditorWidget::sketchPress(double x, double y, int snapKind)
{
    if (!m_doc) return;
    (void)snapKind;
    const QPointF p(x, y);

    switch (m_sketchTool) {
    case SketchFill:
        fillPress(x, y);
        return;
    case SketchLine: {
        if (m_sketchPts.empty()) {
            m_doc->pushUndo();
            m_chainStart = nodeAtOrNew(x, y);
            if (m_chainStart < 0) { emit statusMessage(tr("Cannot place a point here.")); return; }
            m_sketchPts.push_back(p);
            break;
        }
        const QPointF prev = m_sketchPts.back();
        m_doc->pushUndo();
        int n0 = nodeAtOrNew(prev.x(), prev.y());
        int n1 = nodeAtOrNew(x, y);
        if (n0 < 0 || n1 < 0) { emit statusMessage(tr("Cannot place a point here.")); return; }
        if (n0 != n1) m_doc->AddSegment(n0, n1);
        // Back at the start: the contour is closed, the chain is done.
        const QPointF first = m_sketchPts.front();
        if (m_sketchPts.size() >= 2 && dist(x, y, first.x(), first.y()) < 1e-9 * (1 + std::abs(first.x()) + std::abs(first.y()))) {
            sketchFinish();
            emit statusMessage(tr("Contour closed."));
            return;
        }
        m_sketchPts.push_back(p);
        break;
    }
    case SketchRect: {
        if (m_sketchPts.empty()) { m_sketchPts.push_back(p); break; }
        const QPointF a = m_sketchPts.front();
        if (std::abs(a.x() - x) < 1e-12 || std::abs(a.y() - y) < 1e-12) {
            emit statusMessage(tr("A rectangle needs a width and a height."));
            return;
        }
        m_doc->pushUndo();
        const double xs[4] = {a.x(), x, x, a.x()};
        const double ys[4] = {a.y(), a.y(), y, y};
        for (int i = 0; i < 4; i++) nodeAtOrNew(xs[i], ys[i]);
        for (int i = 0; i < 4; i++) {
            int n0 = nodeAtOrNew(xs[i], ys[i]);
            int n1 = nodeAtOrNew(xs[(i + 1) % 4], ys[(i + 1) % 4]);
            if (n0 >= 0 && n1 >= 0 && n0 != n1) m_doc->AddSegment(n0, n1);
        }
        sketchFinish();
        break;
    }
    case SketchCircle: {
        if (m_sketchPts.empty()) { m_sketchPts.push_back(p); break; }
        const QPointF c = m_sketchPts.front();
        const double r = dist(c.x(), c.y(), x, y);
        if (r <= 0) return;
        m_doc->pushUndo();
        // The nodes go where the user clicked and diametrically opposite.
        const double ex = 2 * c.x() - x, ey = 2 * c.y() - y;
        int n0 = nodeAtOrNew(x, y);
        int n1 = nodeAtOrNew(ex, ey);
        if (n0 < 0 || n1 < 0 || n0 == n1) { emit statusMessage(tr("Circle too small.")); return; }
        for (int k = 0; k < 2; k++) {
            FArcSegment a;
            a.n0 = nodeAtOrNew(k ? ex : x, k ? ey : y);
            a.n1 = nodeAtOrNew(k ? x : ex, k ? y : ey);
            a.ArcLength = 180.0;
            a.MaxSideLength = m_arcMaxSeg;
            m_doc->AddArcSegment(a);
        }
        sketchFinish();
        break;
    }
    case SketchArc3: {
        if (m_sketchPts.size() < 2) { m_sketchPts.push_back(p); break; }
        QPointF ctr; double r, sweep; bool rev;
        const QPointF s = m_sketchPts[0], e = m_sketchPts[1];
        if (!arcThrough(s, e, p, ctr, r, rev, sweep) || sweep < 1e-6 || sweep >= 360.0) {
            emit statusMessage(tr("The three points lie on a line -- pick another point."));
            return;
        }
        m_doc->pushUndo();
        FArcSegment a;
        a.n0 = nodeAtOrNew(rev ? e.x() : s.x(), rev ? e.y() : s.y());
        a.n1 = nodeAtOrNew(rev ? s.x() : e.x(), rev ? s.y() : e.y());
        if (a.n0 < 0 || a.n1 < 0 || a.n0 == a.n1) { sketchFinish(); return; }
        a.ArcLength = sweep;
        a.MaxSideLength = m_arcMaxSeg;
        m_doc->AddArcSegment(a);
        sketchFinish();
        break;
    }
    default:
        return;
    }
    m_input[0].clear(); m_input[1].clear(); m_inputField = 0;
    emitSketchHint();
    update();
}

void EditorWidget::sketchFinish()
{
    m_sketchPts.clear();
    m_chainStart = -1;
    m_input[0].clear(); m_input[1].clear(); m_inputField = 0;
    emitSketchHint();
    update();
}

// The point the next click would place: the snapped cursor, overridden by
// whatever has been typed into the length/angle (width/height, radius) fields.
bool EditorWidget::sketchTarget(double &x, double &y) const
{
    x = m_snap.x; y = m_snap.y;
    if (m_sketchPts.empty()) return m_hasCursor;
    const QPointF a = m_sketchPts.back();
    double v0, v1;
    const bool has0 = evalInput(m_input[0], v0), has1 = evalInput(m_input[1], v1);

    switch (m_sketchTool) {
    case SketchLine: {
        double L = dist(a.x(), a.y(), x, y);
        double ang = std::atan2(y - a.y(), x - a.x());
        if (has0) L = v0;
        if (has1) ang = v1 * M_PI / 180.0;
        x = a.x() + L * std::cos(ang);
        y = a.y() + L * std::sin(ang);
        return true;
    }
    case SketchRect: {
        // Typed sizes keep the direction the cursor is pointing in.
        if (has0) x = a.x() + (x >= a.x() ? 1 : -1) * std::abs(v0);
        if (has1) y = a.y() + (y >= a.y() ? 1 : -1) * std::abs(v1);
        return true;
    }
    case SketchCircle: {
        if (has0) {
            double ang = std::atan2(y - a.y(), x - a.x());
            if (!std::isfinite(ang) || (x == a.x() && y == a.y())) ang = 0;
            x = a.x() + std::abs(v0) * std::cos(ang);
            y = a.y() + std::abs(v0) * std::sin(ang);
        }
        return true;
    }
    default:
        return true;
    }
}

// ================================================================
// Keyboard
// ================================================================

bool EditorWidget::askPoint(const QString &title, double &x, double &y)
{
    const int coords = m_doc->Coords;
    double c1, c2;
    QString l1, l2;
    coorddisp::toDisplay(coords, x, y, c1, c2);
    coorddisp::entryLabels(coords, m_doc->ProblemType, l1, l2);
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto *form = new QFormLayout;
    auto *xEdit = new PyLineEdit;
    auto *yEdit = new PyLineEdit;
    xEdit->setText(QString::number(c1, 'g', 10));
    yEdit->setText(QString::number(c2, 'g', 10));
    xEdit->selectAll();
    form->addRow(l1, xEdit);
    form->addRow(l2, yEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    xEdit->setFocus();
    if (dlg.exec() != QDialog::Accepted) return false;
    coorddisp::fromDisplay(coords, xEdit->evaluateAsDouble(c1), yEdit->evaluateAsDouble(c2), x, y);
    return true;
}

bool EditorWidget::sketchKey(QKeyEvent *event)
{
    if (m_sketchTool == SketchNone || !m_doc) return false;
    const int key = event->key();
    const bool started = !m_sketchPts.empty();
    const bool typing = !m_input[0].isEmpty() || !m_input[1].isEmpty();
    const int nFields = (m_sketchTool == SketchLine || m_sketchTool == SketchRect) ? 2
                      : (m_sketchTool == SketchCircle ? 1 : 0);

    switch (key) {
    case Qt::Key_Escape:
        if (typing) { m_input[0].clear(); m_input[1].clear(); m_inputField = 0; update(); return true; }
        if (started) { sketchFinish(); return true; }
        return false;   // fall through to the classic Esc handling
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (started && typing) {
            double x, y;
            if (sketchTarget(x, y)) sketchPress(x, y, SnapFree);
            return true;
        }
        if (started) { sketchFinish(); return true; }
        return true;
    }
    case Qt::Key_Tab:
        if (started && nFields > 0) {
            m_inputField = (m_inputField + 1) % nFields;
            update();
            return true;
        }
        {
            // Not started: type the coordinates of the first point.
            double x = m_snap.x, y = m_snap.y;
            if (askPoint(tr("Enter Point"), x, y)) sketchPress(x, y, SnapFree);
        }
        return true;
    case Qt::Key_Backspace:
        if (typing) {
            m_input[m_inputField].chop(1);
            update();
            return true;
        }
        return false;
    default:
        break;
    }

    // Typing a value. Letters only once a value is underway, so that tool
    // shortcuts (L, R, C, A ...) still work; "2*pi" is fine.
    const QString t = event->text();
    if (started && nFields > 0 && t.size() == 1 && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        const QChar ch = t[0];
        const bool numeric = ch.isDigit() || QStringLiteral(".,-+*/()^").contains(ch);
        if (numeric || (ch.isLetter() && !m_input[m_inputField].isEmpty())) {
            m_input[m_inputField] += (ch == '^') ? QStringLiteral("**") : QString(ch);
            update();
            return true;
        }
    }
    return false;
}

// ================================================================
// Drawing
// ================================================================

void EditorWidget::drawSnapMarker(QPainter &p, const Snap &s)
{
    const QPointF c = toScreen(s.x, s.y);
    p.save();
    p.setBrush(Qt::NoBrush);
    if (s.alignX) {
        p.setPen(QPen(kSnapColor, 1, Qt::DashLine));
        p.drawLine(toScreen(s.x, s.alignXFromY), c);
    }
    if (s.alignY) {
        p.setPen(QPen(kSnapColor, 1, Qt::DashLine));
        p.drawLine(toScreen(s.alignYFromX, s.y), c);
    }
    p.setPen(QPen(kSnapColor, 2));
    const double r = 6;
    switch (s.kind) {
    case SnapEndpoint:
        p.drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        break;
    case SnapMidpoint: {
        QPolygonF tri;
        tri << QPointF(c.x(), c.y() - r) << QPointF(c.x() + r, c.y() + r * 0.8)
            << QPointF(c.x() - r, c.y() + r * 0.8);
        p.drawPolygon(tri);
        break;
    }
    case SnapCenter:
        p.drawEllipse(c, r, r);
        p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 3), QPointF(c.x(), c.y() + 3));
        break;
    case SnapOnLine:
        p.drawLine(QPointF(c.x() - r, c.y() - r), QPointF(c.x() + r, c.y() + r));
        p.drawLine(QPointF(c.x() - r, c.y() + r), QPointF(c.x() + r, c.y() - r));
        break;
    case SnapGrid:
        p.setPen(QPen(kSnapColor, 1));
        p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
        break;
    default:
        if (s.alignX || s.alignY) {
            p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
        }
        break;
    }
    p.restore();
}

void EditorWidget::drawSketchOverlay(QPainter &p)
{
    if (!m_doc || !m_hasCursor) return;
    if (m_sketchTool == SketchSelect) {
        if (m_selBand) {
            const QPoint cur = mapFromGlobal(QCursor::pos());
            const bool crossing = cur.x() < m_selPressPos.x();
            p.save();
            p.setPen(QPen(kPreviewColor, 1, crossing ? Qt::DashLine : Qt::SolidLine));
            QColor fill = crossing ? QColor(60, 200, 90, 40) : QColor(0, 120, 215, 40);
            p.setBrush(fill);
            p.drawRect(QRect(m_selPressPos, cur).normalized());
            p.restore();
        }
        return;
    }
    if (m_sketchTool == SketchFill) {
        // Outline the region a click would fill
        const int f = m_regions.faceAt(m_mx, m_my);
        if (f >= 0) {
            QPolygonF sp;
            for (const QPointF &pt : m_regions.faces()[f].poly) sp << toScreen(pt.x(), pt.y());
            p.save();
            QColor c = materialColor(m_fillMaterial);
            c.setAlpha(std::max(c.alpha(), 90));
            p.setBrush(c);
            p.setPen(QPen(kSnapColor, 2));
            p.drawPolygon(sp);
            p.restore();
        }
        return;
    }
    const bool creating = m_sketchTool != SketchNone || m_editAction == 0;
    if (!creating) return;

    const bool typing = !m_input[0].isEmpty() || !m_input[1].isEmpty();
    if (!typing) drawSnapMarker(p, m_snap);
    if (m_sketchTool == SketchNone || m_sketchPts.empty()) return;

    // Points placed so far (for the rectangle, circle and arc they are not
    // nodes yet)
    p.save();
    p.setPen(QPen(kPreviewColor, 1.5));
    p.setBrush(Qt::white);
    for (const QPointF &pt : m_sketchPts)
        p.drawEllipse(toScreen(pt.x(), pt.y()), 3.5, 3.5);
    p.restore();

    double tx, ty;
    if (!sketchTarget(tx, ty)) return;
    const QPointF a = m_sketchPts.back();
    const QPointF sa = toScreen(a.x(), a.y()), st = toScreen(tx, ty);

    p.save();
    p.setPen(QPen(kPreviewColor, 1.5, Qt::DashLine));
    p.setBrush(Qt::NoBrush);

    QString label;
    const QString unit = QString::fromUtf8(unitSuffix(m_doc->LengthUnits));
    auto fmt = [](double v) { return QString::number(v, 'g', 6); };
    QStringList fields;

    switch (m_sketchTool) {
    case SketchLine: {
        p.drawLine(sa, st);
        const double L = dist(a.x(), a.y(), tx, ty);
        const double ang = std::atan2(ty - a.y(), tx - a.x()) * 180.0 / M_PI;
        fields << QStringLiteral("L %1 %2").arg(m_input[0].isEmpty() ? fmt(L) : m_input[0], unit)
               << QStringLiteral("∠ %1°").arg(m_input[1].isEmpty() ? fmt(ang) : m_input[1]);
        break;
    }
    case SketchRect: {
        p.drawRect(QRectF(sa, st).normalized());
        fields << QStringLiteral("W %1 %2").arg(m_input[0].isEmpty() ? fmt(std::abs(tx - a.x())) : m_input[0], unit)
               << QStringLiteral("H %1 %2").arg(m_input[1].isEmpty() ? fmt(std::abs(ty - a.y())) : m_input[1], unit);
        break;
    }
    case SketchCircle: {
        const double r = dist(a.x(), a.y(), tx, ty);
        p.drawEllipse(sa, r * m_mag, r * m_mag);
        p.setPen(QPen(kPreviewColor, 1, Qt::DotLine));
        p.drawLine(sa, st);
        fields << QStringLiteral("R %1 %2").arg(m_input[0].isEmpty() ? fmt(r) : m_input[0], unit);
        break;
    }
    case SketchArc3: {
        if (m_sketchPts.size() == 1) { p.drawLine(sa, st); break; }
        QPointF ctr; double r, sweep; bool rev;
        const QPointF s = m_sketchPts[0], e = m_sketchPts[1];
        if (arcThrough(s, e, QPointF(tx, ty), ctr, r, rev, sweep)) {
            const QPointF from = rev ? e : s;
            const double a0 = std::atan2(from.y() - ctr.y(), from.x() - ctr.x()) * 180.0 / M_PI;
            const QPointF sc = toScreen(ctr.x(), ctr.y());
            const double rs = r * m_mag;
            // QPainter arc angles are visual (0 = 3 o'clock, positive =
            // counter-clockwise on screen), which is the same sense as the
            // y-up drawing coordinates the angles were measured in.
            QPainterPath path;
            path.arcMoveTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0);
            path.arcTo(QRectF(sc.x() - rs, sc.y() - rs, 2 * rs, 2 * rs), a0, sweep);
            p.drawPath(path);
            fields << QStringLiteral("R %1 %2").arg(fmt(r), unit)
                   << QStringLiteral("%1°").arg(fmt(sweep));
        } else {
            p.drawLine(toScreen(s.x(), s.y()), toScreen(e.x(), e.y()));
        }
        break;
    }
    default:
        break;
    }

    // Heads-up value box next to the cursor; the field being typed is framed.
    if (!fields.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(10);
        p.setFont(f);
        const QFontMetrics fm(f);
        double x = st.x() + 16, y = st.y() + 18;
        for (int i = 0; i < fields.size(); i++) {
            const QRectF r(x, y, fm.horizontalAdvance(fields[i]) + 10, fm.height() + 4);
            const bool active = (i == m_inputField) && !m_input[i].isEmpty();
            p.setPen(QPen(active ? kSnapColor : kPreviewColor, active ? 2 : 1));
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawRoundedRect(r, 3, 3);
            p.setPen(Qt::black);
            p.drawText(r, Qt::AlignCenter, fields[i]);
            x += r.width() + 4;
        }
    }
    p.restore();
}

// ================================================================
// Regions: material fill and the Fill (assign material) tool
// ================================================================

RegionMap &EditorWidget::regions()
{
    if (m_doc) m_regions.update(*m_doc);
    return m_regions;
}

void EditorWidget::fillPress(double x, double y)
{
    if (m_fillMaterial.isEmpty()) {
        emit statusMessage(tr("Choose a material in the toolbar first."));
        return;
    }
    m_regions.update(*m_doc);
    const int f = m_regions.faceAt(x, y);
    if (f < 0) {
        emit statusMessage(tr("Click inside a closed region."));
        return;
    }
    if (m_ensureMaterial && !m_ensureMaterial(m_fillMaterial)) {
        emit statusMessage(tr("Material \"%1\" is not available.").arg(m_fillMaterial));
        return;
    }
    const std::string mat = m_fillMaterial.toStdString();
    m_doc->pushUndo();
    const auto labels = m_regions.faces()[f].labels;   // copy: AddBlockLabel invalidates
    if (labels.empty()) {
        if (!m_doc->AddBlockLabel(x, y, 1.0 / m_mag)) {
            emit statusMessage(tr("Too close to a node or label -- click a little further inside."));
            return;
        }
        m_doc->blocklist.back().BlockType = mat;
    } else {
        for (int i : labels) m_doc->blocklist[i].BlockType = mat;
    }
    emit statusMessage(tr("Region filled with \"%1\".").arg(m_fillMaterial));
    update();
}

void EditorWidget::drawRegions(QPainter &p)
{
    if (!m_doc) return;
    m_regions.update(*m_doc);
    const auto &faces = m_regions.faces();
    if (faces.empty()) return;

    p.save();
    p.setPen(Qt::NoPen);
    QMap<QString, QColor> legend;
    // A "default" label supplies the material of every region without one.
    int defaultLabel = -1;
    for (int i = 0; i < (int)m_doc->blocklist.size(); i++)
        if (m_doc->blocklist[i].IsDefault) defaultLabel = i;
    for (const auto &f0 : faces) {
        auto f = f0;
        if (f.labels.empty() && defaultLabel >= 0) f.labels.push_back(defaultLabel);
        QPolygonF sp;
        sp.reserve(f.poly.size());
        for (const QPointF &pt : f.poly) sp << toScreen(pt.x(), pt.y());

        if (f.labels.size() == 1) {
            const auto &b = m_doc->blocklist[f.labels[0]];
            const QString name = QString::fromStdString(b.BlockType);
            if (name == "<No Mesh>") {
                p.setBrush(viewColors().background());
                p.drawPolygon(sp);
                p.setBrush(QBrush(QColor(150, 150, 150), Qt::Dense7Pattern));
            } else {
                QColor c = materialColor(name);
                if (c.alpha() == 0) {
                    // Label without a material: same warning as no label
                    p.setBrush(viewColors().background());
                    p.drawPolygon(sp);
                    p.setBrush(QBrush(QColor(220, 40, 40, 140), Qt::BDiagPattern));
                } else {
                    // Opaque base first so a region nested in another is not
                    // tinted by the outer region's colour.
                    p.setBrush(viewColors().background());
                    p.drawPolygon(sp);
                    p.setBrush(c);
                    legend.insert(name, c);
                }
            }
        } else {
            // No label (or several -- FEMM would refuse or guess): hatch red
            p.setBrush(viewColors().background());
            p.drawPolygon(sp);
            p.setBrush(QBrush(QColor(220, 40, 40, 140),
                              f.labels.empty() ? Qt::BDiagPattern : Qt::DiagCrossPattern));
        }
        p.drawPolygon(sp);
    }

    // Legend, top-left: materials, then boundary conditions in use
    QMap<QString, QColor> bcLegend;
    for (const auto &sg : m_doc->linelist)
        if (boundaryColor(sg.BoundaryMarker).isValid())
            bcLegend.insert(QString::fromStdString(sg.BoundaryMarker), boundaryColor(sg.BoundaryMarker));
    for (const auto &a : m_doc->arclist)
        if (boundaryColor(a.BoundaryMarker).isValid())
            bcLegend.insert(QString::fromStdString(a.BoundaryMarker), boundaryColor(a.BoundaryMarker));
    if (!legend.isEmpty() || !bcLegend.isEmpty()) {
        QFont font = p.font();
        font.setPointSize(9);
        p.setFont(font);
        const QFontMetrics fm(font);
        int y = 8;
        for (auto it = legend.cbegin(); it != legend.cend(); ++it) {
            QColor c = it.value();
            c.setAlpha(std::max(c.alpha(), 160));
            p.setPen(QPen(QColor(90, 90, 90), 1));
            p.setBrush(c);
            p.drawRect(QRectF(8, y, 12, 12));
            p.setPen(viewColors().text());
            p.drawText(QPointF(26, y + 10), it.key());
            y += fm.height() + 4;
        }
        for (auto it = bcLegend.cbegin(); it != bcLegend.cend(); ++it) {
            p.setPen(QPen(it.value(), 3));
            p.drawLine(QPointF(8, y + 6), QPointF(20, y + 6));
            p.setPen(viewColors().text());
            p.drawText(QPointF(26, y + 10), it.key());
            y += fm.height() + 4;
        }
    }
    p.restore();
}

// ================================================================
// Select tool
// ================================================================

bool EditorWidget::selectPress(QMouseEvent *event)
{
    if (!m_doc || event->button() != Qt::LeftButton) return false;
    const double x = m_mx, y = m_my, tol = pickTolerance();
    const bool add = event->modifiers() & Qt::ShiftModifier;

    // Nearest object within the pick tolerance: node, label, segment, arc
    int kind = -1, idx = -1;
    double best = tol;
    if (int j = m_doc->ClosestNode(x, y); j >= 0 && m_doc->nodelist[j].GetDistance(x, y) < best) {
        best = m_doc->nodelist[j].GetDistance(x, y); kind = 0; idx = j;
    }
    if (int j = m_doc->ClosestBlockLabel(x, y); j >= 0 && m_doc->blocklist[j].GetDistance(x, y) < best) {
        best = m_doc->blocklist[j].GetDistance(x, y); kind = 2; idx = j;
    }
    if (kind < 0) {
        if (int j = m_doc->ClosestSegment(x, y); j >= 0 && std::abs(m_doc->ShortestDistance(x, y, j)) < best) {
            best = std::abs(m_doc->ShortestDistance(x, y, j)); kind = 1; idx = j;
        }
        if (int j = m_doc->ClosestArcSegment(x, y);
            j >= 0 && m_doc->ShortestDistanceFromArc(CComplex(x, y), m_doc->arclist[j]) < best) {
            kind = 3; idx = j;
        }
    }

    auto isSel = [&](int k, int i) {
        switch (k) {
        case 0: return m_doc->nodelist[i].IsSelected;
        case 1: return m_doc->linelist[i].IsSelected;
        case 2: return m_doc->blocklist[i].IsSelected;
        default: return m_doc->arclist[i].IsSelected;
        }
    };
    auto setSel = [&](int k, int i, bool on) {
        switch (k) {
        case 0: m_doc->nodelist[i].IsSelected = on; break;
        case 1: m_doc->linelist[i].IsSelected = on; break;
        case 2: m_doc->blocklist[i].IsSelected = on; break;
        default: m_doc->arclist[i].IsSelected = on; break;
        }
    };

    m_selPending = true;
    m_selDragging = false;
    m_selBand = false;
    m_selPressPos = event->pos();
    m_selX0 = x; m_selY0 = y;

    if (kind < 0) {
        if (!add) m_doc->UnselectAll();
        m_selBand = true;            // becomes a window on drag
    } else if (add) {
        setSel(kind, idx, !isSel(kind, idx));
    } else if (!isSel(kind, idx)) {
        m_doc->UnselectAll();
        setSel(kind, idx, true);
    }
    update();
    return true;
}

bool EditorWidget::selectMove(QMouseEvent *event)
{
    if (!m_selPending || !(event->buttons() & Qt::LeftButton)) return false;
    if (m_selBand) { update(); return true; }

    if (!m_selDragging) {
        if ((event->pos() - m_selPressPos).manhattanLength() < 4) return true;
        // Start moving: remember where everything selected was
        m_doc->pushUndo();
        m_dragNodes.clear(); m_dragLabels.clear();
        std::vector<char> moveNode(m_doc->nodelist.size(), 0);
        for (size_t i = 0; i < m_doc->nodelist.size(); i++) moveNode[i] |= m_doc->nodelist[i].IsSelected;
        for (const auto &sg : m_doc->linelist) if (sg.IsSelected) moveNode[sg.n0] = moveNode[sg.n1] = 1;
        for (const auto &a : m_doc->arclist) if (a.IsSelected) moveNode[a.n0] = moveNode[a.n1] = 1;
        for (size_t i = 0; i < moveNode.size(); i++)
            if (moveNode[i]) m_dragNodes.push_back({(int)i, QPointF(m_doc->nodelist[i].x, m_doc->nodelist[i].y)});
        for (size_t i = 0; i < m_doc->blocklist.size(); i++)
            if (m_doc->blocklist[i].IsSelected)
                m_dragLabels.push_back({(int)i, QPointF(m_doc->blocklist[i].x, m_doc->blocklist[i].y)});
        if (m_dragNodes.empty() && m_dragLabels.empty()) { m_selPending = false; return true; }
        m_selDragging = true;
    }
    // Offset, with grid snap applied to the moved point
    double tx = m_mx, ty = m_my;
    applySnap(tx, ty);
    double sx = m_selX0, sy = m_selY0;
    applySnap(sx, sy);
    const double dx = tx - sx, dy = ty - sy;
    for (const auto &n : m_dragNodes) { m_doc->nodelist[n.first].x = n.second.x() + dx; m_doc->nodelist[n.first].y = n.second.y() + dy; }
    for (const auto &b : m_dragLabels) { m_doc->blocklist[b.first].x = b.second.x() + dx; m_doc->blocklist[b.first].y = b.second.y() + dy; }
    emit statusMessage(tr("Moving by (%1, %2)").arg(dx, 0, 'g', 6).arg(dy, 0, 'g', 6));
    update();
    return true;
}

bool EditorWidget::selectRelease(QMouseEvent *event)
{
    if (!m_selPending || event->button() != Qt::LeftButton) return false;
    m_selPending = false;
    if (m_selDragging) {
        m_selDragging = false;
        // Merge what now coincides, split what now crosses
        m_doc->EnforcePSLG();
        invalidateMeshDisplay();
    } else if (m_selBand) {
        m_selBand = false;
        if ((event->pos() - m_selPressPos).manhattanLength() >= 4) {
            const bool crossing = event->pos().x() < m_selPressPos.x();
            selectInRect(m_selX0, m_selY0, m_mx, m_my, crossing);
        }
    }
    update();
    return true;
}

void EditorWidget::selectInRect(double x0, double y0, double x1, double y1, bool crossing)
{
    const QRectF r = QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized();
    auto in = [&](double x, double y) { return r.contains(QPointF(x, y)); };
    for (auto &n : m_doc->nodelist) if (in(n.x, n.y)) n.IsSelected = true;
    for (auto &b : m_doc->blocklist) if (in(b.x, b.y)) b.IsSelected = true;
    for (auto &sg : m_doc->linelist) {
        const auto &a = m_doc->nodelist[sg.n0], &b = m_doc->nodelist[sg.n1];
        const bool ia = in(a.x, a.y), ib = in(b.x, b.y);
        if (crossing ? (ia || ib || QLineF(a.x, a.y, b.x, b.y).intersects(QLineF(r.topLeft(), r.bottomRight()), nullptr) == QLineF::BoundedIntersection
                                 || QLineF(a.x, a.y, b.x, b.y).intersects(QLineF(r.topRight(), r.bottomLeft()), nullptr) == QLineF::BoundedIntersection)
                     : (ia && ib))
            sg.IsSelected = true;
    }
    for (auto &arc : m_doc->arclist) {
        // Sample the arc: all samples inside (window) or any inside (crossing)
        CComplex c; double R;
        m_doc->GetCircle(arc, c, R);
        CComplex pt = m_doc->nodelist[arc.n0].CC();
        const int k = std::max(4, (int)std::ceil(arc.ArcLength / 5.0));
        const CComplex rot(std::cos(arc.ArcLength * M_PI / 180.0 / k), std::sin(arc.ArcLength * M_PI / 180.0 / k));
        int inside = in(pt.re, pt.im);
        for (int i = 0; i < k; i++) { pt = (pt - c) * rot + c; inside += in(pt.re, pt.im); }
        if (crossing ? inside > 0 : inside == k + 1) arc.IsSelected = true;
    }
}
