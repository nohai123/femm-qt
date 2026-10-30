// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// hover_readout.h - small value box that follows the cursor in the
// postprocessors, so the field at any point can be read by just pointing
// (before, it took Point mode + a click + reading the Output window).
// A child label, not painted by the view: moving it repaints only the few
// pixels it uncovers instead of the whole density plot.
#pragma once

#include <QLabel>
#include <QWidget>

class HoverReadout : public QLabel
{
public:
    explicit HoverReadout(QWidget *view) : QLabel(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("QLabel { background: rgba(255,255,255,235); color: black;"
                      " border: 1px solid #0078d7; border-radius: 3px; padding: 2px 5px; }");
        QFont f = font(); f.setPointSize(10); setFont(f);
        hide();
    }
    // Empty text hides it.
    void showAt(const QPoint &cursor, const QString &text)
    {
        if (text.isEmpty()) { hide(); return; }
        setText(text);
        adjustSize();
        QPoint p = cursor + QPoint(16, 16);
        const QWidget *v = parentWidget();
        if (p.x() + width() > v->width()) p.setX(cursor.x() - width() - 8);
        if (p.y() + height() > v->height()) p.setY(cursor.y() - height() - 8);
        move(p);
        show();
        raise();
    }
};
