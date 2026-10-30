// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// sweep_dialog.cpp — see sweep_dialog.h.

#include "sweep_dialog.h"
#include "editor_widget.h"
#include "document.h"
#include "mag_document.h"
#include "plot_widget.h"
#include "py_state.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString pyStr(const QString &s)
{
    QString o = s;
    o.replace("\\", "\\\\").replace("'", "\\'");
    return "'" + o + "'";
}

} // namespace

SweepDialog::SweepDialog(EditorWidget *editor, QWidget *parent)
    : QDialog(parent), m_editor(editor)
{
    setWindowTitle(tr("Parameter Sweep"));
    resize(900, 560);
    DocumentBase *doc = editor->document();
    const bool axi = doc->ProblemType == 1;

    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);

    auto *gv = new QGroupBox(tr("Vary"));
    auto *fv = new QFormLayout(gv);
    m_vary = new QComboBox;
    m_vary->addItems({tr("Circuit current"), tr("Position of a group")});
    fv->addRow(tr("Parameter:"), m_vary);
    m_circuit = new QComboBox;
    for (int i = 0; i < doc->numCircuits(); i++) m_circuit->addItem(QString::fromStdString(doc->circuitName(i)));
    fv->addRow(tr("Circuit:"), m_circuit);
    m_group = new QSpinBox;
    m_group->setRange(0, 100000);
    m_group->setValue(1);
    m_group->setToolTip(tr("Objects to move: give them a group number in their properties"));
    fv->addRow(tr("Group:"), m_group);
    m_direction = new QComboBox;
    m_direction->addItems(axi ? QStringList{"r", "z"} : QStringList{"x", "y"});
    m_direction->setCurrentIndex(1);
    fv->addRow(tr("Direction:"), m_direction);
    m_from = new QLineEdit("0");
    m_to = new QLineEdit("1");
    m_from->setObjectName("sweepFrom");
    m_to->setObjectName("sweepTo");
    m_unitFrom = new QLabel; m_unitTo = new QLabel;
    auto *rf = new QHBoxLayout; rf->addWidget(m_from); rf->addWidget(m_unitFrom);
    auto *rt = new QHBoxLayout; rt->addWidget(m_to); rt->addWidget(m_unitTo);
    fv->addRow(tr("From:"), rf);
    fv->addRow(tr("To:"), rt);
    m_steps = new QSpinBox;
    m_steps->setRange(2, 500);
    m_steps->setValue(11);
    m_steps->setObjectName("sweepSteps");
    fv->addRow(tr("Steps:"), m_steps);
    ll->addWidget(gv);

    auto *go = new QGroupBox(tr("Results"));
    auto *fo = new QFormLayout(go);
    m_outCircuit = new QComboBox;
    for (int i = 0; i < doc->numCircuits(); i++) m_outCircuit->addItem(QString::fromStdString(doc->circuitName(i)));
    m_outL = new QCheckBox(tr("Inductance"));
    m_outFlux = new QCheckBox(tr("Flux linkage"));
    m_outL->setChecked(doc->numCircuits() > 0);
    fo->addRow(tr("Circuit:"), m_outCircuit);
    fo->addRow(m_outL);
    fo->addRow(m_outFlux);
    m_outGroup = new QSpinBox;
    m_outGroup->setRange(0, 100000);
    m_outGroup->setValue(1);
    fo->addRow(tr("Force/torque on group:"), m_outGroup);
    m_outFx = new QCheckBox(tr("Force %1").arg(axi ? "r" : "x"));
    m_outFy = new QCheckBox(tr("Force %1").arg(axi ? "z" : "y"));
    m_outT = new QCheckBox(tr("Torque"));
    m_outT->setEnabled(!axi);
    fo->addRow(m_outFx);
    fo->addRow(m_outFy);
    fo->addRow(m_outT);
    ll->addWidget(go);

    m_status = new QLabel(tr("Each step is meshed and solved; Esc stops."));
    m_status->setWordWrap(true);
    ll->addWidget(m_status);
    ll->addStretch();

    auto *right = new QSplitter(Qt::Vertical);
    m_plot = new PlotWidget;
    m_table = new QTableWidget;
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    right->addWidget(m_plot);
    right->addWidget(m_table);
    right->setStretchFactor(0, 3);

    auto *split = new QSplitter;
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(1, 2);

    auto *bb = new QDialogButtonBox;
    QPushButton *runBtn = bb->addButton(tr("Run Sweep"), QDialogButtonBox::AcceptRole);
    QPushButton *copyBtn = bb->addButton(tr("Copy Table"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    connect(runBtn, &QPushButton::clicked, this, &SweepDialog::run);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString t;
        for (int c = 0; c < m_table->columnCount(); c++)
            t += (c ? "\t" : "") + m_table->horizontalHeaderItem(c)->text();
        t += "\n";
        for (int r = 0; r < m_table->rowCount(); r++) {
            for (int c = 0; c < m_table->columnCount(); c++)
                t += (c ? "\t" : "") + m_table->item(r, c)->text();
            t += "\n";
        }
        QApplication::clipboard()->setText(t);
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(split, 1);
    lay->addWidget(bb);

    connect(m_vary, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateFields(); });
    updateFields();
}

void SweepDialog::updateFields()
{
    static const char *units[] = {"in", "mm", "cm", "m", "mil", "\u00b5m"};
    const bool current = m_vary->currentIndex() == 0;
    m_circuit->setEnabled(current);
    m_group->setEnabled(!current);
    m_direction->setEnabled(!current);
    const int lu = m_editor->document()->LengthUnits;
    const QString u = current ? QStringLiteral("A") : QString::fromUtf8(lu >= 0 && lu < 6 ? units[lu] : "");
    m_unitFrom->setText(u);
    m_unitTo->setText(u);
    if (current && m_from->text() == "0" && m_to->text() == "1") m_to->setText("10");
}

QStringList SweepDialog::columns() const
{
    const bool byCurrent = m_vary->currentIndex() == 0;
    QStringList cols{byCurrent ? tr("Current [A]") : tr("Position")};
    if (m_outL->isChecked() && m_outCircuit->count()) cols << tr("Inductance [H]");
    if (m_outFlux->isChecked() && m_outCircuit->count()) cols << tr("Flux linkage [Wb]");
    if (m_outFx->isChecked()) cols << m_outFx->text() + " [N]";
    if (m_outFy->isChecked()) cols << m_outFy->text() + " [N]";
    if (m_outT->isChecked() && m_outT->isEnabled()) cols << tr("Torque [N\u00b7m]");
    return cols;
}

QString SweepDialog::script(const QString &csvPath) const
{
    const bool byCurrent = m_vary->currentIndex() == 0;
    if (byCurrent && m_circuit->count() == 0) return QString();
    bool ok1, ok2;
    const double a = QString(m_from->text()).replace(',', '.').toDouble(&ok1);
    const double b = QString(m_to->text()).replace(',', '.').toDouble(&ok2);
    if (!ok1 || !ok2) return QString();
    const int n = m_steps->value();
    const bool wantL = m_outL->isChecked() && m_outCircuit->count();
    const bool wantF = m_outFlux->isChecked() && m_outCircuit->count();
    const bool wantFx = m_outFx->isChecked(), wantFy = m_outFy->isChecked();
    const bool wantT = m_outT->isChecked() && m_outT->isEnabled();
    const bool dirX = m_direction->currentIndex() == 0;

    // What a user would write by hand
    QString py;
    QTextStream s(&py);
    s << "def _re(z):\n    return z.real if isinstance(z, complex) else z\n";
    s << "_a, _b, _n = " << QString::number(a, 'g', 17) << ", " << QString::number(b, 'g', 17) << ", " << n << "\n";
    s << "_out = open(" << pyStr(csvPath) << ", 'w')\n";
    if (byCurrent) s << "_c = " << pyStr(m_circuit->currentText()) << "\n";
    else s << "_g = " << m_group->value() << "\n_prev = 0.0\n";
    s << "try:\n";
    s << "  for _k in range(_n):\n";
    s << "    _v = _a + (_b - _a) * _k / (_n - 1)\n";
    if (byCurrent) {
        s << "    mi_modifycircprop(_c, 1, _v)\n";
    } else {
        s << "    _d = _v - _prev\n    _prev = _v\n";
        s << "    mi_clearselected()\n    mi_selectgroup(_g)\n";
        s << "    mi_movetranslate(" << (dirX ? "_d, 0" : "0, _d") << ")\n";
        s << "    mi_clearselected()\n";
    }
    s << "    mi_analyze(1)\n    mi_loadsolution()\n";
    s << "    _row = [_v]\n";
    if (wantL || wantF) {
        s << "    _i, _u, _phi = mo_getcircuitproperties(" << pyStr(m_outCircuit->currentText()) << ")\n";
        if (wantL) s << "    _row.append(_re(_phi / _i) if _i != 0 else 0)\n";
        if (wantF) s << "    _row.append(_re(_phi))\n";
    }
    if (wantFx || wantFy || wantT) {
        s << "    mo_clearblock()\n    mo_groupselectblock(" << m_outGroup->value() << ")\n";
        if (wantFx) s << "    _row.append(_re(mo_blockintegral(18)))\n";
        if (wantFy) s << "    _row.append(_re(mo_blockintegral(19)))\n";
        if (wantT) s << "    _row.append(_re(mo_blockintegral(22)))\n";
        s << "    mo_clearblock()\n";
    }
    s << "    _out.write(','.join([repr(x) for x in _row]) + '\\n')\n";
    s << "finally:\n  _out.close()\n";
    if (!byCurrent) {
        // Put the group back where it was
        s << "  mi_clearselected()\n  mi_selectgroup(_g)\n";
        s << "  mi_movetranslate(" << (dirX ? "-_prev, 0" : "0, -_prev") << ")\n  mi_clearselected()\n";
    }
    s.flush();
    return py;
}

void SweepDialog::run()
{
    DocumentBase *doc = m_editor->document();
    if (doc->physicsType() != 0) return;
    const bool byCurrent = m_vary->currentIndex() == 0;
    if (byCurrent && m_circuit->count() == 0) {
        QMessageBox::warning(this, windowTitle(), tr("The model has no circuit to vary."));
        return;
    }
    if (doc->filePath.empty()) {
        QMessageBox::warning(this, windowTitle(), tr("Save the model first -- each step is solved from its file."));
        return;
    }
    QTemporaryFile out(QDir::tempPath() + "/femm_sweep_XXXXXX.csv");
    out.open();
    out.close();
    const QString py = script(out.fileName());
    if (py.isEmpty()) return;
    const QStringList cols = columns();
    const int n = m_steps->value();

    m_status->setText(tr("Running %1 steps\u2026 (Esc stops)").arg(n));
    // The script leaves the last swept current in place; put the original back
    auto *mag = dynamic_cast<MagDocument *>(doc);
    FCircuit *swept = nullptr;
    CComplex oldAmps;
    if (byCurrent && mag)
        for (auto &c : mag->circproplist)
            if (QString::fromStdString(c.CircName) == m_circuit->currentText()) { swept = &c; oldAmps = c.Amps; }
    QApplication::setOverrideCursor(Qt::BusyCursor);
    char err[1024] = {};
    const int rc = exec_python(py.toUtf8().constData(), err, sizeof err);
    QApplication::restoreOverrideCursor();
    if (swept) swept->Amps = oldAmps;

    // Read whatever steps completed
    QFile f(out.fileName());
    std::vector<std::vector<double>> rows;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QStringList p = QString::fromUtf8(f.readLine()).trimmed().split(',');
            std::vector<double> r;
            for (const QString &x : p) r.push_back(x.toDouble());
            if (!r.empty()) rows.push_back(r);
        }
    }
    m_table->clear();
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); r++)
        for (int c = 0; c < cols.size() && c < (int)rows[r].size(); c++)
            m_table->setItem(r, c, new QTableWidgetItem(QString::number(rows[r][c], 'g', 6)));
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_plot->clearAll();
    for (int c = 1; c < cols.size(); c++) {
        PlotSeries ps;
        ps.label = cols[c];
        for (const auto &r : rows) if ((int)r.size() > c) { ps.x.push_back(r[0]); ps.y.push_back(r[c]); }
        m_plot->addSeries(ps);
    }
    m_plot->setAxisLabels(cols[0], cols.size() > 1 ? cols[1] : QString());
    m_plot->update();

    if (rc != 0)
        m_status->setText(tr("Stopped after %1 of %2 steps: %3").arg(rows.size()).arg(n).arg(QString::fromUtf8(err)));
    else
        m_status->setText(tr("Done: %1 steps.").arg(rows.size()));
    m_editor->update();
}
