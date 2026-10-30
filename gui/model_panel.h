// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// model_panel.h — "Model" dock beside the preprocessor.
//
// Top: everything the model defines -- materials (with how many regions use
// each), boundary conditions, circuits, point properties. Clicking an entry
// selects what uses it in the drawing; double-clicking opens its editor.
//
// Bottom: an inspector for the current selection. The common properties --
// a region's material, circuit and turns, a segment's boundary condition, a
// node's point property -- are edited in place; everything else is one click
// away in the full property dialog. Replaces hunting for "Space" and modal
// dialogs for the everyday edits.
#pragma once

#include <QDockWidget>
#include <QString>
#include <cstdint>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class EditorWidget;
class MaterialPicker;

class ModelPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit ModelPanel(QWidget *parent = nullptr);

    // Show this editor's model; cheap to call often (rebuilds only on change).
    void refresh(EditorWidget *editor);

    // Opens the property list (0 point props, 1 boundaries, 2 materials,
    // 3 circuits) -- supplied by MainWindow, which owns those dialogs.
    std::function<void(int)> openPropertyList;
    std::function<void()> openMaterialLibrary;
    std::function<bool(EditorWidget *, const QString &)> ensureMaterial;

private:
    void rebuildTree();
    void rebuildInspector();
    void onItemClicked(QTreeWidgetItem *item);
    uint64_t treeSignature() const;
    uint64_t selectionSignature() const;

    EditorWidget *m_editor = nullptr;
    uint64_t m_treeSig = 0, m_selSig = 0;
    bool m_updating = false;

    QTreeWidget *m_tree;
    QLabel *m_selTitle;
    QStackedWidget *m_stack;
    // Block label page
    MaterialPicker *m_labelMaterial;
    QComboBox *m_labelCircuit;
    QSpinBox *m_labelTurns;
    QLabel *m_turnsLabel;
    QLabel *m_circuitLabel;
    // Segment/arc page
    QComboBox *m_segBoundary;
    // Node page
    QComboBox *m_nodeProp;
};
