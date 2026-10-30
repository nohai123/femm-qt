// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// view_navigation.h - wheel / trackpad / pinch navigation shared by the
// editor (EditorWidget) and both post-processors (ResultsWidget,
// ScalarResultsWidget).
//
// All three views use the same transform: drawing = screen / mag + origin,
// with screen y flipped (yd = (height()-1-ys)/mag + oy). They differed only in
// which members hold mag/ox/oy, so the navigation lives here once.
//
// Behaviour:
//   * Mouse wheel (discrete clicks, no pixel delta): zoom about the cursor,
//     1.25x per click -- unchanged from before.
//   * Trackpad two-finger scroll (pixel delta / scroll phase): PAN. Before
//     this, a trackpad could only zoom and panning needed a middle button,
//     which a MacBook does not have.
//   * Trackpad scroll with Cmd (Ctrl elsewhere) held: smooth zoom.
//   * Pinch (macOS native zoom gesture): smooth zoom about the cursor.
#pragma once

#include <QWidget>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <cmath>

namespace femmnav {

static const double kMinMag = 0.01;

// Zoom by `factor` keeping the drawing point under `pos` fixed.
inline void zoomAbout(const QWidget *w, QPointF pos, double factor,
                      double &mag, double &ox, double &oy)
{
    const double xd = pos.x() / mag + ox;
    const double yd = (w->height() - 1 - pos.y()) / mag + oy;
    mag *= factor;
    if (mag < kMinMag) mag = kMinMag;
    ox = xd - pos.x() / mag;
    oy = yd - (w->height() - 1 - pos.y()) / mag;
}

// Handle a wheel event. Returns false if the event was ignored.
inline bool wheel(const QWidget *w, QWheelEvent *e, double &mag, double &ox, double &oy)
{
    const bool trackpad = !e->pixelDelta().isNull() || e->phase() != Qt::NoScrollPhase;
    const bool zoomMod = e->modifiers() & Qt::ControlModifier;  // Cmd on macOS

    if (trackpad && !zoomMod) {
        const QPoint d = e->pixelDelta().isNull() ? e->angleDelta() / 8 : e->pixelDelta();
        ox -= d.x() / mag;
        oy += d.y() / mag;
        return true;
    }
    if (trackpad) {
        const int dy = e->pixelDelta().isNull() ? e->angleDelta().y() / 8 : e->pixelDelta().y();
        zoomAbout(w, e->position(), std::pow(1.01, dy), mag, ox, oy);
        return true;
    }
    // Ignore tiny deltas (spurious events during a two-finger tap)
    if (std::abs(e->angleDelta().y()) < 15) return false;
    zoomAbout(w, e->position(), e->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25, mag, ox, oy);
    return true;
}

// Handle a native pinch gesture. Returns true if `ev` was one.
inline bool gesture(const QWidget *w, QEvent *ev, double &mag, double &ox, double &oy)
{
    if (ev->type() != QEvent::NativeGesture) return false;
    auto *g = static_cast<QNativeGestureEvent *>(ev);
    if (g->gestureType() == Qt::ZoomNativeGesture) {
        zoomAbout(w, w->mapFromGlobal(g->globalPosition()), 1.0 + g->value(), mag, ox, oy);
        return true;
    }
    if (g->gestureType() == Qt::SmartZoomNativeGesture) {
        // Two-finger double tap: let the caller decide (usually zoom-fit).
        return false;
    }
    return false;
}

} // namespace femmnav
