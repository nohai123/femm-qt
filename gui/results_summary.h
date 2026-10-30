// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_summary.h — "Results" dock: the key numbers of a magnetics solution
// at a glance.
//
// For every circuit: current, voltage drop, flux linkage, and from them the
// inductance (flux / current) and resistance (voltage / current). Before,
// each circuit had to be looked up in View > Circuit Properties one at a time
// and the inductance worked out by hand. Copy puts the table on the clipboard
// as tab-separated text (pastes straight into a spreadsheet).
#pragma once

#include <QDockWidget>
#include <QDateTime>

class QLabel;
class QTableWidget;
class ResultsDoc;

class ResultsSummaryPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit ResultsSummaryPanel(QWidget *parent = nullptr);

    // Cheap to call often: recomputes only for a new document or a reloaded
    // solution file.
    void refresh(ResultsDoc *doc);

private:
    void rebuild();

    void rebuildSelection();
    ResultsDoc *m_doc = nullptr;
    QString m_selSig;
    QLabel *m_selInfo;
    QTableWidget *m_selTable;
    QDateTime m_stamp;
    QLabel *m_info;
    QTableWidget *m_table;
};
