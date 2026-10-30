// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// material_picker.h — searchable material chooser for the editor toolbar.
//
// Lists the materials already in the document first, then every material in
// the physics' library (matlib.dat, statlib.dat, heatlib.dat, condlib.dat).
// Typing filters by any part of the name or its library folder ("N42",
// "copper", "M-19"). Picking a library material does not change the document;
// the Fill tool copies it in on first use (MainWindow::ensureMaterial).
#pragma once

#include <QComboBox>
#include <QString>
#include <QStringList>
#include <vector>

class DocumentBase;

struct LibraryMaterial {
    QString name;
    QString folder;   // "Hard Magnetic Materials / Sintered NdFeB Magnets"
};

// Every material in the library of a physics type (0..3), in file order.
std::vector<LibraryMaterial> libraryMaterials(int physicsType);

class MaterialPicker : public QComboBox
{
    Q_OBJECT
public:
    explicit MaterialPicker(QWidget *parent = nullptr);

    // Re-list for this document (its materials + its physics' library).
    void setDocument(DocumentBase *doc);
    QString currentMaterial() const;

    void showPopup() override { emit aboutToShowPopup(); QComboBox::showPopup(); }

signals:
    void materialChosen(const QString &name);
    // Lets the owner re-list (the document's materials may have changed).
    void aboutToShowPopup();

private:
    int m_physics = -1;
    std::vector<LibraryMaterial> m_lib;
};
