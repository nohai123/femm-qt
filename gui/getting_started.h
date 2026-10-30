// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// getting_started.h — Help > Getting Started: a checklist panel that walks a
// first model from geometry to results. Each step ticks itself off by
// looking at the model (closed regions drawn, every region has a material,
// a source is defined, a boundary condition is set, solved) and the next
// open step says how to do it, with the tool's shortcut.
#pragma once

#include <QDockWidget>

class QLabel;
class EditorWidget;

class GettingStartedPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit GettingStartedPanel(QWidget *parent = nullptr);
    // editor: the model being worked on (or null); solved: a results tab of
    // this model is open
    void refresh(EditorWidget *editor, bool solved);

private:
    QLabel *m_text;
    QString m_last;
};
