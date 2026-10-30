// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// model_panel.cpp — see model_panel.h.

#include "model_panel.h"
#include "editor_widget.h"
#include "material_picker.h"
#include "region_map.h"
#include "document.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <cmath>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cstring>

namespace {

enum Category { CatMaterials = 2, CatBoundaries = 1, CatCircuits = 3, CatPoints = 0 };
enum Role { RoleCategory = Qt::UserRole, RoleName };

uint64_t mixs(uint64_t h, const std::string &s)
{
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
    return (h ^ 0xff) * 1099511628211ULL;
}
uint64_t mixi(uint64_t h, uint64_t v)
{
    return (h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
}

QIcon swatch(const QColor &c0)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    QColor c = c0;
    c.setAlpha(std::max(c.alpha(), 170));
    p.setPen(QColor(90, 90, 90));
    p.setBrush(c);
    p.drawRect(0, 0, 11, 11);
    return QIcon(pm);
}

} // namespace

ModelPanel::ModelPanel(QWidget *parent)
    : QDockWidget(tr("Model"), parent)
{
    setObjectName("ModelPanel");
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr("Click: select what uses it.  Double-click: edit."));
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) { onItemClicked(it); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        if (openPropertyList) openPropertyList(it->data(0, RoleCategory).toInt());
        m_treeSig = 0;
    });

    auto *btns = new QHBoxLayout;
    auto *lib = new QPushButton(tr("Library…"));
    lib->setToolTip(tr("Copy materials from the material library into the model"));
    connect(lib, &QPushButton::clicked, this, [this]() { if (openMaterialLibrary) openMaterialLibrary(); m_treeSig = 0; });
    btns->addWidget(lib);
    btns->addStretch();

    // ---- Inspector ----
    auto *insp = new QWidget;
    auto *il = new QVBoxLayout(insp);
    il->setContentsMargins(0, 6, 0, 0);
    m_selTitle = new QLabel;
    m_selTitle->setWordWrap(true);
    QFont bf = m_selTitle->font(); bf.setBold(true); m_selTitle->setFont(bf);
    il->addWidget(m_selTitle);
    m_stack = new QStackedWidget;
    il->addWidget(m_stack);

    auto editAll = [this](int action) {
        auto *b = new QPushButton(tr("All Properties…"));
        connect(b, &QPushButton::clicked, this, [this, action]() {
            if (!m_editor) return;
            m_editor->openSelectedAs(action);
            m_selSig = 0;
        });
        return b;
    };

    // page 0: nothing selected
    auto *none = new QLabel(tr("Right-click objects in the drawing (or click an entry above) to select them."));
    none->setWordWrap(true);
    none->setStyleSheet("color: gray");
    m_stack->addWidget(none);

    // page 1: block labels
    {
        auto *pg = new QWidget;
        auto *f = new QFormLayout(pg);
        f->setContentsMargins(0, 0, 0, 0);
        m_labelMaterial = new MaterialPicker;
        f->addRow(tr("Material:"), m_labelMaterial);
        m_labelCircuit = new QComboBox;
        m_circuitLabel = new QLabel(tr("Circuit:"));
        f->addRow(m_circuitLabel, m_labelCircuit);
        m_labelTurns = new QSpinBox;
        m_labelTurns->setRange(-100000, 100000);
        m_labelTurns->setToolTip(tr("Number of turns; negative reverses the current direction"));
        m_turnsLabel = new QLabel(tr("Turns:"));
        f->addRow(m_turnsLabel, m_labelTurns);
        // Mesh: automatic, or coarse -> fine relative to the model size
        m_meshSlider = new QSlider(Qt::Horizontal);
        m_meshSlider->setRange(0, 10);
        m_meshSlider->setPageStep(1);
        m_meshSlider->setToolTip(tr("Element size in this region: left = automatic, further right = finer mesh"));
        m_meshInfo = new QLabel;
        m_meshInfo->setStyleSheet("color: gray");
        auto *mw = new QWidget;
        auto *ml = new QVBoxLayout(mw);
        ml->setContentsMargins(0, 0, 0, 0);
        ml->addWidget(m_meshSlider);
        ml->addWidget(m_meshInfo);
        f->addRow(tr("Mesh:"), mw);
        f->addRow(editAll(2));
        m_stack->addWidget(pg);

        connect(m_labelMaterial, &MaterialPicker::aboutToShowPopup, this, [this]() {
            if (m_editor) m_labelMaterial->setDocument(m_editor->document());
        });
        connect(m_labelMaterial, &MaterialPicker::materialChosen, this, [this](const QString &name) {
            if (m_updating || !m_editor) return;
            if (ensureMaterial && !ensureMaterial(m_editor, name)) return;
            DocumentBase *d = m_editor->document();
            d->pushUndo();
            for (auto &b : d->blocklist) if (b.IsSelected) b.BlockType = name.toStdString();
            m_editor->update();
            m_treeSig = 0;
        });
        connect(m_labelCircuit, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            if (m_updating || !m_editor) return;
            DocumentBase *d = m_editor->document();
            const std::string c = m_labelCircuit->currentData().toString().toStdString();
            d->pushUndo();
            for (auto &b : d->blocklist) if (b.IsSelected) b.InCircuit = c;
            m_editor->update();
            m_treeSig = 0;
        });
        connect(m_meshSlider, &QSlider::valueChanged, this, [this](int k) {
            const double h = k ? modelSize() * 0.2 * std::pow(0.6, k - 1) : 0;
            m_meshInfo->setText(k ? tr("element size about %1").arg(h, 0, 'g', 3) : tr("automatic"));
            if (m_updating || !m_editor) return;
            DocumentBase *d = m_editor->document();
            d->pushUndo();
            // FEMM stores the size as a target triangle area
            for (auto &b : d->blocklist) if (b.IsSelected) b.MaxArea = k ? M_PI * h * h / 4 : 0;
            m_editor->update();
        });
        connect(m_labelTurns, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
            if (m_updating || !m_editor) return;
            DocumentBase *d = m_editor->document();
            d->pushUndo();
            for (auto &b : d->blocklist) if (b.IsSelected) b.Turns = v;
            m_editor->update();
        });
    }
    // page 2: segments / arcs
    {
        auto *pg = new QWidget;
        auto *f = new QFormLayout(pg);
        f->setContentsMargins(0, 0, 0, 0);
        m_segBoundary = new QComboBox;
        f->addRow(tr("Boundary:"), m_segBoundary);
        f->addRow(editAll(1));
        m_stack->addWidget(pg);
        connect(m_segBoundary, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            if (m_updating || !m_editor) return;
            DocumentBase *d = m_editor->document();
            const std::string bc = m_segBoundary->currentData().toString().toStdString();
            d->pushUndo();
            for (auto &s : d->linelist) if (s.IsSelected) s.BoundaryMarker = bc;
            for (auto &a : d->arclist) if (a.IsSelected) a.BoundaryMarker = bc;
            m_editor->update();
            m_treeSig = 0;
        });
    }
    // page 3: nodes
    {
        auto *pg = new QWidget;
        auto *f = new QFormLayout(pg);
        f->setContentsMargins(0, 0, 0, 0);
        m_nodeProp = new QComboBox;
        f->addRow(tr("Point property:"), m_nodeProp);
        f->addRow(editAll(0));
        m_stack->addWidget(pg);
        connect(m_nodeProp, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            if (m_updating || !m_editor) return;
            DocumentBase *d = m_editor->document();
            const std::string pp = m_nodeProp->currentData().toString().toStdString();
            d->pushUndo();
            for (auto &n : d->nodelist) if (n.IsSelected) n.BoundaryMarker = pp;
            m_editor->update();
        });
    }

    auto *split = new QSplitter(Qt::Vertical);
    auto *top = new QWidget;
    auto *tl = new QVBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(m_tree);
    tl->addLayout(btns);
    split->addWidget(top);
    split->addWidget(insp);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split);
    setWidget(w);
    setMinimumWidth(230);
}

uint64_t ModelPanel::treeSignature() const
{
    uint64_t h = 1469598103934665603ULL;
    if (!m_editor || !m_editor->document()) return h;
    const DocumentBase *d = m_editor->document();
    h = mixi(h, (uint64_t)(uintptr_t)d);
    for (int i = 0; i < d->numMaterials(); i++) h = mixs(h, d->materialName(i));
    for (int i = 0; i < d->numBoundaries(); i++) h = mixs(h, "b" + d->boundaryName(i));
    for (int i = 0; i < d->numCircuits(); i++) h = mixs(h, "c" + d->circuitName(i));
    for (int i = 0; i < d->numPointProps(); i++) h = mixs(h, "p" + d->pointPropName(i));
    for (const auto &b : d->blocklist) { h = mixs(h, b.BlockType); h = mixs(h, b.InCircuit); }
    for (const auto &s : d->linelist) h = mixs(h, s.BoundaryMarker);
    for (const auto &a : d->arclist) h = mixs(h, a.BoundaryMarker);
    return h;
}

uint64_t ModelPanel::selectionSignature() const
{
    uint64_t h = 7;
    if (!m_editor || !m_editor->document()) return h;
    const DocumentBase *d = m_editor->document();
    h = mixi(h, (uint64_t)(uintptr_t)d);
    uint64_t i = 0;
    for (const auto &n : d->nodelist) { if (n.IsSelected) h = mixi(h, i); i++; }
    for (const auto &s : d->linelist) { if (s.IsSelected) h = mixi(h, i); i++; }
    for (const auto &a : d->arclist) { if (a.IsSelected) h = mixi(h, i); i++; }
    for (const auto &b : d->blocklist) {
        if (b.IsSelected) { h = mixi(h, i); h = mixs(h, b.BlockType); h = mixs(h, b.InCircuit); h = mixi(h, (uint64_t)b.Turns); }
        i++;
    }
    for (const auto &s : d->linelist) if (s.IsSelected) h = mixs(h, s.BoundaryMarker);
    for (const auto &a : d->arclist) if (a.IsSelected) h = mixs(h, a.BoundaryMarker);
    for (int k = 0; k < d->numCircuits(); k++) h = mixs(h, d->circuitName(k));
    for (int k = 0; k < d->numBoundaries(); k++) h = mixs(h, d->boundaryName(k));
    return h;
}

void ModelPanel::refresh(EditorWidget *editor)
{
    if (editor != m_editor) { m_editor = editor; m_treeSig = m_selSig = 0; }
    // Never rebuild under the user's hands
    QWidget *f = focusWidget();
    const bool busy = f && isAncestorOf(f) && f != m_tree;
    const uint64_t ts = treeSignature();
    if (ts != m_treeSig) { m_treeSig = ts; rebuildTree(); }
    if (!busy) {
        const uint64_t ss = selectionSignature();
        if (ss != m_selSig) { m_selSig = ss; rebuildInspector(); }
    }
}

void ModelPanel::rebuildTree()
{
    // Remember which categories were collapsed
    QSet<int> collapsed;
    for (int i = 0; i < m_tree->topLevelItemCount(); i++)
        if (!m_tree->topLevelItem(i)->isExpanded())
            collapsed << m_tree->topLevelItem(i)->data(0, RoleCategory).toInt();
    m_tree->clear();
    if (!m_editor || !m_editor->document()) return;
    const DocumentBase *d = m_editor->document();
    const int phys = d->physicsType();

    auto cat = [&](int c, const QString &title, int n) {
        auto *it = new QTreeWidgetItem(m_tree);
        it->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(n));
        it->setData(0, RoleCategory, c);
        QFont f = it->font(0); f.setBold(true); it->setFont(0, f);
        it->setExpanded(!collapsed.contains(c));
        return it;
    };

    // Materials, with the number of regions using each
    auto *mats = cat(CatMaterials, tr("Materials"), d->numMaterials());
    for (int i = 0; i < d->numMaterials(); i++) {
        const std::string n = d->materialName(i);
        int uses = 0;
        for (const auto &b : d->blocklist) uses += (b.BlockType == n);
        auto *it = new QTreeWidgetItem(mats);
        it->setText(0, QString::fromStdString(n));
        it->setIcon(0, swatch(materialColor(QString::fromStdString(n))));
        it->setText(1, uses ? QString::number(uses) : QStringLiteral("–"));
        it->setToolTip(1, tr("%n region(s) use this material", "", uses));
        it->setData(0, RoleCategory, CatMaterials);
        it->setData(0, RoleName, QString::fromStdString(n));
    }
    auto *bcs = cat(CatBoundaries, tr("Boundary Conditions"), d->numBoundaries());
    for (int i = 0; i < d->numBoundaries(); i++) {
        const std::string n = d->boundaryName(i);
        int uses = 0;
        for (const auto &s : d->linelist) uses += (s.BoundaryMarker == n);
        for (const auto &a : d->arclist) uses += (a.BoundaryMarker == n);
        auto *it = new QTreeWidgetItem(bcs);
        it->setText(0, QString::fromStdString(n));
        it->setText(1, uses ? QString::number(uses) : QStringLiteral("–"));
        it->setData(0, RoleCategory, CatBoundaries);
        it->setData(0, RoleName, QString::fromStdString(n));
    }
    auto *circ = cat(CatCircuits, phys == 0 ? tr("Circuits") : tr("Conductors"), d->numCircuits());
    for (int i = 0; i < d->numCircuits(); i++) {
        const std::string n = d->circuitName(i);
        int uses = 0;
        for (const auto &b : d->blocklist) uses += (b.InCircuit == n);
        auto *it = new QTreeWidgetItem(circ);
        it->setText(0, QString::fromStdString(n));
        if (phys == 0) it->setText(1, uses ? QString::number(uses) : QStringLiteral("–"));
        it->setData(0, RoleCategory, CatCircuits);
        it->setData(0, RoleName, QString::fromStdString(n));
    }
    auto *pts = cat(CatPoints, tr("Point Properties"), d->numPointProps());
    for (int i = 0; i < d->numPointProps(); i++) {
        auto *it = new QTreeWidgetItem(pts);
        it->setText(0, QString::fromStdString(d->pointPropName(i)));
        it->setData(0, RoleCategory, CatPoints);
        it->setData(0, RoleName, QString::fromStdString(d->pointPropName(i)));
    }
}

void ModelPanel::onItemClicked(QTreeWidgetItem *item)
{
    if (!m_editor || !item || !item->parent()) return;
    const int c = item->data(0, RoleCategory).toInt();
    const std::string n = item->data(0, RoleName).toString().toStdString();
    DocumentBase *d = m_editor->document();
    if (c == CatMaterials || (c == CatCircuits && d->physicsType() == 0)) {
        m_editor->setEditAction(2);
        for (auto &b : d->blocklist)
            b.IsSelected = (c == CatMaterials ? b.BlockType : b.InCircuit) == n;
    } else if (c == CatBoundaries || c == CatCircuits) {
        m_editor->setEditAction(1);
        for (auto &s : d->linelist) s.IsSelected = (c == CatBoundaries ? s.BoundaryMarker : s.InConductor) == n;
        for (auto &a : d->arclist) a.IsSelected = (c == CatBoundaries ? a.BoundaryMarker : a.InConductor) == n;
    } else if (c == CatPoints) {
        m_editor->setEditAction(0);
        for (auto &nd : d->nodelist) nd.IsSelected = nd.BoundaryMarker == n;
    }
    m_editor->update();
    m_selSig = 0;
}

void ModelPanel::rebuildInspector()
{
    m_updating = true;
    int nl = 0, ns = 0, nn = 0;
    const DocumentBase *d = m_editor ? m_editor->document() : nullptr;
    if (d) {
        for (const auto &b : d->blocklist) nl += b.IsSelected;
        for (const auto &s : d->linelist) ns += s.IsSelected;
        for (const auto &a : d->arclist) ns += a.IsSelected;
        for (const auto &n : d->nodelist) nn += n.IsSelected;
    }
    auto fillCombo = [](QComboBox *cb, int count, auto nameOf, const std::string &current, bool mixed) {
        cb->clear();
        cb->addItem(QStringLiteral("<None>"), QStringLiteral("<None>"));
        for (int i = 0; i < count; i++) {
            const QString n = QString::fromStdString(nameOf(i));
            cb->addItem(n, n);
        }
        if (mixed) { cb->insertItem(0, QObject::tr("(mixed)"), QString()); cb->setCurrentIndex(0); }
        else {
            const int k = cb->findData(QString::fromStdString(current.empty() ? "<None>" : current));
            cb->setCurrentIndex(k >= 0 ? k : 0);
        }
    };

    if (!d || (nl + ns + nn) == 0) {
        m_selTitle->setText(tr("Nothing selected"));
        m_stack->setCurrentIndex(0);
    } else if (nl) {
        m_selTitle->setText(tr("%n region label(s) selected", "", nl));
        const FBlockLabel *first = nullptr;
        bool mixMat = false, mixCirc = false, mixTurns = false;
        for (const auto &b : d->blocklist) {
            if (!b.IsSelected) continue;
            if (!first) { first = &b; continue; }
            mixMat |= b.BlockType != first->BlockType;
            mixCirc |= b.InCircuit != first->InCircuit;
            mixTurns |= b.Turns != first->Turns;
        }
        m_labelMaterial->setDocument(m_editor->document());
        m_labelMaterial->setEditText(mixMat ? tr("(mixed)") : QString::fromStdString(first->BlockType));
        const bool mag = d->physicsType() == 0;
        m_labelCircuit->setVisible(mag); m_circuitLabel->setVisible(mag);
        m_labelTurns->setVisible(mag); m_turnsLabel->setVisible(mag);
        fillCombo(m_labelCircuit, d->numCircuits(), [d](int i) { return d->circuitName(i); },
                  first->InCircuit, mixCirc);
        m_labelTurns->setValue(mixTurns ? 0 : first->Turns);
        {
            int k = 0;
            if (first->MaxArea > 0) {
                const double h = std::sqrt(4 * first->MaxArea / M_PI), L = modelSize();
                k = std::clamp((int)std::lround(1 + std::log(h / (0.2 * L)) / std::log(0.6)), 1, 10);
            }
            m_meshSlider->setValue(k);
            m_meshInfo->setText(k ? tr("element size about %1").arg(std::sqrt(4 * first->MaxArea / M_PI), 0, 'g', 3)
                                  : tr("automatic"));
        }
        m_stack->setCurrentIndex(1);
    } else if (ns) {
        m_selTitle->setText(tr("%n segment(s)/arc(s) selected", "", ns));
        std::string bc; bool first = true, mixed = false;
        for (const auto &s : d->linelist) if (s.IsSelected) { if (first) { bc = s.BoundaryMarker; first = false; } else mixed |= s.BoundaryMarker != bc; }
        for (const auto &a : d->arclist) if (a.IsSelected) { if (first) { bc = a.BoundaryMarker; first = false; } else mixed |= a.BoundaryMarker != bc; }
        fillCombo(m_segBoundary, d->numBoundaries(), [d](int i) { return d->boundaryName(i); }, bc, mixed);
        m_stack->setCurrentIndex(2);
    } else {
        m_selTitle->setText(tr("%n node(s) selected", "", nn));
        std::string pp; bool first = true, mixed = false;
        for (const auto &n : d->nodelist) if (n.IsSelected) { if (first) { pp = n.BoundaryMarker; first = false; } else mixed |= n.BoundaryMarker != pp; }
        fillCombo(m_nodeProp, d->numPointProps(), [d](int i) { return d->pointPropName(i); }, pp, mixed);
        m_stack->setCurrentIndex(3);
    }
    m_updating = false;
}

double ModelPanel::modelSize() const
{
    double x0, y0, x1, y1;
    if (!m_editor || !m_editor->document() || !m_editor->document()->getBoundingBox(x0, y0, x1, y1)) return 1;
    return std::max(x1 - x0, y1 - y0);
}
