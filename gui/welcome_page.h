// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// welcome_page.h — what the main window shows when no document is open, and
// the one-step "New Problem" dialog.
//
// Before, an empty FEMM window was a grey area with a menu bar; New asked for
// a problem type from a bare list and every setting (units, planar or
// axisymmetric, depth, frequency) had to be found afterwards in Problem
// Definition. The ~70 shipped examples were inside the application bundle,
// where nobody finds them and where they cannot be saved or analysed anyway.
#pragma once

#include <QDialog>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QLabel;

struct NewProblemSettings {
    int physics = 0;        // 0 magnetics, 1 electrostatics, 2 heat flow, 3 current flow
    int lengthUnits = 1;    // 0 in, 1 mm, 2 cm, 3 m, 4 mils, 5 um
    int problemType = 0;    // 0 planar, 1 axisymmetric
    double depth = 1.0;     // planar only, in length units
    double frequency = 0;   // magnetics and current flow, Hz
};

class NewProblemDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewProblemDialog(int physics = 0, QWidget *parent = nullptr);
    NewProblemSettings settings() const;

private:
    void updateFields();
    QButtonGroup *m_type;
    QComboBox *m_units;
    QRadioButton *m_planar, *m_axi;
    QLineEdit *m_depth, *m_freq;
    QLabel *m_depthLabel, *m_freqLabel, *m_freqHint;
};

class WelcomePage : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);
    // Re-read the recent-files list.
    void refresh();

signals:
    void newProblem(int physics);
    void openFile(const QString &path);
    void openOther();

private:
    void fillExamples(const QString &filter);
    QListWidget *m_recent;
    QListWidget *m_examples;
    QLineEdit *m_search;
};

// The folder examples are copied to before opening (they ship read-only).
QString femmExamplesWorkDir();
