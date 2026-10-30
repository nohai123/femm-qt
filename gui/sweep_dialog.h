// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// sweep_dialog.h — Analysis > Parameter Sweep (magnetics).
//
// The two studies nearly every actuator or coil design needs, without
// writing a script: vary a circuit's current (L vs I, saturation) or move a
// group of objects step by step (force vs position, cogging torque), solve
// each step and plot inductance, flux linkage, force or torque against it.
// Internally it writes and runs the same Python a user would, so it is
// interruptible with Esc like any script.
#pragma once

#include <QDialog>

class EditorWidget;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QTableWidget;
class PlotWidget;

class SweepDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SweepDialog(EditorWidget *editor, QWidget *parent = nullptr);

    // The study as a Python script writing one CSV row per step to csvPath
    // (empty if the settings are incomplete). Public for the tests.
    QString script(const QString &csvPath) const;
    // Column titles matching the CSV
    QStringList columns() const;

private:
    void updateFields();
    void run();

    EditorWidget *m_editor;
    QComboBox *m_vary;          // 0 = circuit current, 1 = group position
    QComboBox *m_circuit;
    QSpinBox *m_group;
    QComboBox *m_direction;     // x / y
    QLineEdit *m_from, *m_to;
    QSpinBox *m_steps;
    QComboBox *m_outCircuit;
    QCheckBox *m_outL, *m_outFlux, *m_outFx, *m_outFy, *m_outT;
    QSpinBox *m_outGroup;
    QLabel *m_unitFrom, *m_unitTo, *m_status;
    QTableWidget *m_table;
    PlotWidget *m_plot;
};
