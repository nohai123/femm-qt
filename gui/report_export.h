// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// report_export.h — postprocessor File > Export Report: one PDF with the
// picture of the solution, the problem settings and (magnetics) the circuit
// table with inductance and resistance -- ready to attach to a design note.
#pragma once

class QWidget;
class QString;

// view: the results widget (ResultsWidget or ScalarResultsWidget) to picture.
// Returns false (and has told the user) if writing failed.
bool exportResultsReport(QWidget *parent, QWidget *view, const QString &path);
