// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// problem_check.h — checks a preprocessor model before it is analyzed and
// lists what would go wrong, each with a "Show" that zooms to the spot.
//
// The solver reports most of these only after meshing, as one line of text
// with no location (or, for a region without a label, not at all: it is
// silently left out of the mesh). Here they are found from the geometry
// (RegionMap) up front.
#pragma once

#include <QRectF>
#include <QString>
#include <vector>

class QWidget;
class EditorWidget;

struct ProblemIssue {
    enum Severity { Error, Warning, Hint } severity;
    QString text;
    QRectF where;              // drawing coordinates; empty = whole model
    std::vector<int> labels;   // block labels to select when shown
    std::vector<int> nodes;    // nodes to select when shown
};

std::vector<ProblemIssue> checkProblem(EditorWidget *editor);

// Runs the check; if anything turned up, shows it. Returns true if the
// analysis should go ahead (nothing found, or the user chose to continue).
bool confirmProblemCheck(QWidget *parent, EditorWidget *editor);
