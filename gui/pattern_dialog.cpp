// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// pattern_dialog.cpp — see pattern_dialog.h.

#include "pattern_dialog.h"
#include "editor_widget.h"
#include "document.h"
#include "py_line_edit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

double val(QLineEdit *e, double def)
{
    return static_cast<PyLineEdit *>(e)->evaluateAsDouble(def);
}

} // namespace

PatternDialog::PatternDialog(EditorWidget *editor, QWidget *parent)
    : QDialog(parent), m_editor(editor)
{
    setWindowTitle(tr("Pattern"));
    DocumentBase *doc = editor->document();

    // Defaults: rotate about the origin (motors are drawn around it); step
    // to the right by the selection's width
    double x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    doc->getBoundingBox(x0, y0, x1, y1, true);

    auto *lay = new QVBoxLayout(this);
    auto *top = new QFormLayout;
    m_kind = new QComboBox;
    m_kind->addItems({tr("Circular"), tr("Linear")});
    top->addRow(tr("Pattern:"), m_kind);
    m_count = new QSpinBox;
    m_count->setRange(2, 1000);
    m_count->setValue(4);
    m_count->setToolTip(tr("Number of instances including the original"));
    top->addRow(tr("Instances:"), m_count);
    lay->addLayout(top);

    m_pages = new QStackedWidget;
    {
        auto *pg = new QWidget;
        auto *f = new QFormLayout(pg);
        f->setContentsMargins(0, 0, 0, 0);
        m_cx = new PyLineEdit; m_cx->setText("0");
        m_cy = new PyLineEdit; m_cy->setText("0");
        f->addRow(tr("Centre x:"), m_cx);
        f->addRow(tr("Centre y:"), m_cy);
        m_full = new QCheckBox(tr("Spread over a full circle"));
        m_full->setChecked(true);
        f->addRow(m_full);
        m_total = new PyLineEdit; m_total->setText("90");
        f->addRow(tr("Angle between instances (deg):"), m_total);
        m_pages->addWidget(pg);
    }
    {
        auto *pg = new QWidget;
        auto *f = new QFormLayout(pg);
        f->setContentsMargins(0, 0, 0, 0);
        m_dx = new PyLineEdit; m_dx->setText(QString::number(x1 - x0 > 0 ? (x1 - x0) * 1.5 : 1, 'g', 6));
        m_dy = new PyLineEdit; m_dy->setText("0");
        f->addRow(tr("Spacing x:"), m_dx);
        f->addRow(tr("Spacing y:"), m_dy);
        m_pages->addWidget(pg);
    }
    lay->addWidget(m_pages);
    auto *hint = new QLabel(tr("The dashed preview in the drawing shows the copies."));
    hint->setStyleSheet("color: gray");
    lay->addWidget(hint);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_pages->setCurrentIndex(i); preview(); });
    connect(m_count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { preview(); });
    connect(m_full, &QCheckBox::toggled, this, [this](bool on) { m_total->setEnabled(!on); preview(); });
    for (QLineEdit *e : {m_cx, m_cy, m_total, m_dx, m_dy})
        connect(e, &QLineEdit::textChanged, this, [this]() { preview(); });
    m_total->setEnabled(false);
    preview();
}

PatternDialog::~PatternDialog()
{
    m_editor->setGhosts({});
}

std::vector<QTransform> PatternDialog::transforms() const
{
    std::vector<QTransform> out;
    const int n = m_count->value();
    if (m_kind->currentIndex() == 0) {
        const double cx = val(m_cx, 0), cy = val(m_cy, 0);
        const double step = m_full->isChecked() ? 360.0 / n : val(m_total, 90);
        for (int k = 1; k < n; k++) {
            QTransform t;
            t.translate(cx, cy);
            t.rotate(step * k);
            t.translate(-cx, -cy);
            out.push_back(t);
        }
    } else {
        const double dx = val(m_dx, 1), dy = val(m_dy, 0);
        for (int k = 1; k < n; k++) out.push_back(QTransform::fromTranslate(dx * k, dy * k));
    }
    return out;
}

void PatternDialog::preview()
{
    m_editor->setGhosts(transforms());
}

void PatternDialog::apply()
{
    DocumentBase *doc = m_editor->document();
    const int copies = m_count->value() - 1;
    // Group semantics: everything selected (nodes, lines, arcs, labels)
    doc->pushUndo();
    if (m_kind->currentIndex() == 0) {
        const double step = m_full->isChecked() ? 360.0 / m_count->value() : val(m_total, 90);
        doc->RotateCopy(CComplex(val(m_cx, 0), val(m_cy, 0)), step, copies, 4);
    } else {
        doc->TranslateCopy(val(m_dx, 1), val(m_dy, 0), copies, 4);
    }
    m_editor->setGhosts({});
    m_editor->update();
}
