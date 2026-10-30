// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// material_picker.cpp — see material_picker.h.

#include "material_picker.h"
#include "material_lib_path.h"
#include "document.h"
#include "utf8_io.h"

#include <QCompleter>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <cstdio>

std::vector<LibraryMaterial> libraryMaterials(int physicsType)
{
    static const char *libs[] = {"matlib.dat", "statlib.dat", "heatlib.dat", "condlib.dat"};
    std::vector<LibraryMaterial> out;
    if (physicsType < 0 || physicsType > 3) return out;

    FILE *fp = femm_fopen(materialLibPath(libs[physicsType]), "rt");
    if (!fp) return out;
    static const QRegularExpression keyRe(QStringLiteral("^\\s*<(\\w+)>\\s*=?\\s*\"?([^\"]*)\"?"));
    QStringList folders;
    bool pendingFolder = false;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        const QString line = QString::fromUtf8(buf).trimmed();
        if (!line.startsWith('<')) continue;
        const auto m = keyRe.match(line);
        if (!m.hasMatch()) continue;
        const QString key = m.captured(1).toLower();
        if (key == "beginfolder") { folders << QString(); pendingFolder = true; }
        else if (key == "endfolder") { if (!folders.isEmpty()) folders.removeLast(); }
        else if (key == "foldername" && pendingFolder && !folders.isEmpty()) {
            folders.last() = m.captured(2).trimmed();
            pendingFolder = false;
        }
        else if (key == "blockname") {
            out.push_back({m.captured(2).trimmed(), folders.join(QStringLiteral(" / "))});
        }
    }
    fclose(fp);
    return out;
}

MaterialPicker::MaterialPicker(QWidget *parent)
    : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setMinimumContentsLength(18);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMaxVisibleItems(25);
    setToolTip(tr("Material for the Fill tool -- type to search the model and the library"));
    lineEdit()->setPlaceholderText(tr("Material..."));

    auto *comp = new QCompleter(model(), this);
    comp->setFilterMode(Qt::MatchContains);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setCompletionRole(Qt::DisplayRole);
    setCompleter(comp);

    connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        const QString n = currentMaterial();
        if (!n.isEmpty()) emit materialChosen(n);
    });
}

QString MaterialPicker::currentMaterial() const
{
    const int i = findText(currentText(), Qt::MatchFixedString);
    if (i >= 0) {
        const QString n = itemData(i).toString();
        return n.isEmpty() ? QString() : n;
    }
    return QString();
}

void MaterialPicker::setDocument(DocumentBase *doc)
{
    const QString keep = currentMaterial();
    const int physics = doc ? doc->physicsType() : -1;
    if (physics != m_physics) {
        m_lib = libraryMaterials(physics);
        m_physics = physics;
    }

    blockSignals(true);
    clear();
    auto *m = qobject_cast<QStandardItemModel *>(model());
    auto addHeader = [&](const QString &t) {
        addItem(t);
        if (m) m->item(count() - 1)->setEnabled(false);
    };

    QStringList inDoc;
    if (doc) {
        for (int i = 0; i < doc->numMaterials(); i++)
            inDoc << QString::fromStdString(doc->materialName(i));
    }
    if (!inDoc.isEmpty()) {
        addHeader(tr("— In this model —"));
        for (const QString &n : inDoc) addItem(n, n);
    }
    addItem(QStringLiteral("<No Mesh>"), QStringLiteral("<No Mesh>"));
    if (!m_lib.empty()) {
        addHeader(tr("— Library —"));
        for (const auto &lm : m_lib) {
            if (inDoc.contains(lm.name)) continue;
            addItem(lm.name, lm.name);
            setItemData(count() - 1, lm.folder, Qt::ToolTipRole);
        }
    }
    const int k = keep.isEmpty() ? -1 : findData(keep);
    if (k >= 0) setCurrentIndex(k);
    else { setCurrentIndex(-1); setEditText(QString()); }
    blockSignals(false);
}
