// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_summary.cpp — see results_summary.h.

#include "results_summary.h"
#include "results_doc.h"
#include "complex_format.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString num(double v)
{
    return QString::number(v, 'g', 5);
}

// Real quantities print as numbers; AC ones as magnitude with phase.
QString val(CComplex z, bool ac)
{
    if (!ac) return num(z.re);
    const double mag = abs(z);
    const double ph = (mag == 0) ? 0 : std::atan2(z.im, z.re) * 180.0 / M_PI;
    return QStringLiteral("%1 ∠%2°").arg(num(mag), QString::number(ph, 'f', 1));
}

} // namespace

ResultsSummaryPanel::ResultsSummaryPanel(QWidget *parent)
    : QDockWidget(tr("Results"), parent)
{
    setObjectName("ResultsSummaryPanel");
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({tr("Circuit"), tr("Current [A]"), tr("Voltage [V]"),
                                        tr("Flux linkage [Wb]"), tr("Inductance [H]"),
                                        tr("Resistance [Ω]")});
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_table, 1);

    auto *row = new QHBoxLayout;
    auto *copy = new QPushButton(tr("Copy"));
    copy->setToolTip(tr("Copy the table as tab-separated text"));
    connect(copy, &QPushButton::clicked, this, [this]() {
        QString t;
        for (int c = 0; c < m_table->columnCount(); c++)
            t += (c ? "\t" : "") + m_table->horizontalHeaderItem(c)->text();
        t += "\n";
        for (int r = 0; r < m_table->rowCount(); r++) {
            for (int c = 0; c < m_table->columnCount(); c++)
                t += (c ? "\t" : "") + (m_table->item(r, c) ? m_table->item(r, c)->text() : QString());
            t += "\n";
        }
        QApplication::clipboard()->setText(t);
    });
    row->addStretch();
    row->addWidget(copy);
    lay->addLayout(row);
    setWidget(w);
}

void ResultsSummaryPanel::refresh(ResultsDoc *doc)
{
    QDateTime stamp;
    if (doc && !doc->filePath.empty())
        stamp = QFileInfo(QString::fromStdString(doc->filePath)).lastModified();
    if (doc == m_doc && stamp == m_stamp) return;
    m_doc = doc;
    m_stamp = stamp;
    rebuild();
}

void ResultsSummaryPanel::rebuild()
{
    m_table->setRowCount(0);
    if (!m_doc) { m_info->setText(tr("No magnetics solution open.")); return; }
    const bool ac = m_doc->Frequency != 0;
    const int n = (int)m_doc->circproplist.size();
    if (n == 0) {
        m_info->setText(tr("This model has no circuits. Define one (Properties > Circuits) "
                           "and assign it to coil regions to see current, flux linkage and inductance here."));
        return;
    }
    m_info->setText(ac ? tr("AC solution at %1 Hz -- values are amplitudes with phase.").arg(m_doc->Frequency)
                       : tr("DC solution."));
    m_table->setRowCount(n);
    for (int k = 0; k < n; k++) {
        const auto &c = m_doc->circproplist[k];
        const CComplex I = c.Amps;
        const CComplex V = m_doc->GetVoltageDrop(k);
        const CComplex F = m_doc->GetFluxLinkage(k);
        const bool hasI = abs(I) > 0;
        const QString L = hasI ? (ac ? num(Re(F / I)) : num(F.re / I.re)) : QStringLiteral("–");
        const QString R = hasI ? (ac ? num(Re(V / I)) : num(V.re / I.re)) : QStringLiteral("–");
        const QString cells[6] = {QString::fromStdString(c.CircName), val(I, ac), val(V, ac), val(F, ac), L, R};
        for (int col = 0; col < 6; col++) {
            auto *it = new QTableWidgetItem(cells[col]);
            if (col > 0) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(k, col, it);
        }
        if (!hasI) m_table->item(k, 4)->setToolTip(tr("Inductance needs a non-zero circuit current."));
    }
}
