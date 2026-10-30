// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// pattern_dialog.h — Edit > Pattern: circular or linear array of the
// selection with a live preview in the drawing (rotor poles, stator slots,
// coil turns, cooling fins). Replaces working out "angle per copy" for the
// Copy dialog by hand: give the number of instances and the total angle (or
// the spacing) and see the result before committing.
#pragma once

#include <QDialog>
#include <QTransform>
#include <vector>

class EditorWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QCheckBox;

class PatternDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PatternDialog(EditorWidget *editor, QWidget *parent = nullptr);
    ~PatternDialog() override;

    // The copies (not the original) as transforms of drawing coordinates
    std::vector<QTransform> transforms() const;
    // Applies the pattern to the document (one undo step)
    void apply();

private:
    void preview();
    EditorWidget *m_editor;
    QComboBox *m_kind;
    QStackedWidget *m_pages;
    QLineEdit *m_cx, *m_cy, *m_total, *m_dx, *m_dy;
    QSpinBox *m_count;
    QCheckBox *m_full;
};
