// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// problem_check.cpp — see problem_check.h.

#include "problem_check.h"
#include "editor_widget.h"
#include "region_map.h"
#include "document.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QRectF around(double x, double y, double r)
{
    return QRectF(x - r, y - r, 2 * r, 2 * r);
}

bool noMaterial(const std::string &m)
{
    return m.empty() || m == "<None>";
}

} // namespace

std::vector<ProblemIssue> checkProblem(EditorWidget *editor)
{
    std::vector<ProblemIssue> out;
    DocumentBase *doc = editor ? editor->document() : nullptr;
    if (!doc) return out;
    const RegionMap &rm = editor->regions();
    const auto &faces = rm.faces();

    double x0, y0, x1, y1;
    const bool hasGeom = doc->getBoundingBox(x0, y0, x1, y1);
    const double size = hasGeom ? std::max(x1 - x0, y1 - y0) : 1.0;
    const double pad = 0.05 * size;

    if (faces.empty()) {
        out.push_back({ProblemIssue::Error,
                       QObject::tr("The model has no closed region. Draw closed contours "
                                   "(e.g. with the Rectangle or Circle tool) and fill them with materials."),
                       QRectF(), {}, {}});
        return out;
    }

    bool hasDefault = false;
    for (const auto &b : doc->blocklist) hasDefault |= b.IsDefault;

    // Regions: missing or doubled labels
    for (const auto &f : faces) {
        const QRectF r = f.bbox.adjusted(-pad, -pad, pad, pad);
        if (f.labels.empty() && !hasDefault) {
            out.push_back({ProblemIssue::Error,
                           QObject::tr("A region has no material (no block label). It would be left "
                                       "out of the mesh. Use Assign Material (F) to fill it."),
                           r, {}, {}});
        } else if (f.labels.size() > 1) {
            out.push_back({ProblemIssue::Error,
                           QObject::tr("A region has %1 block labels; it needs exactly one.").arg(f.labels.size()),
                           r, f.labels, {}});
        }
    }

    // Labels: no material, or not inside any closed region
    for (int i = 0; i < (int)doc->blocklist.size(); i++) {
        const auto &b = doc->blocklist[i];
        if (noMaterial(b.BlockType)) {
            out.push_back({ProblemIssue::Error,
                           QObject::tr("A block label has no material assigned."),
                           around(b.x, b.y, 4 * pad), {i}, {}});
        }
        if (rm.faceAt(b.x, b.y) < 0) {
            out.push_back({ProblemIssue::Warning,
                           QObject::tr("A block label lies outside every closed region; it has no effect."),
                           around(b.x, b.y, 4 * pad), {i}, {}});
        }
    }

    // Open contour ends: nodes with exactly one segment/arc attached
    std::vector<int> degree(doc->nodelist.size(), 0);
    for (const auto &s : doc->linelist) { degree[s.n0]++; degree[s.n1]++; }
    for (const auto &a : doc->arclist) { degree[a.n0]++; degree[a.n1]++; }
    std::vector<int> open;
    for (int i = 0; i < (int)degree.size(); i++) if (degree[i] == 1) open.push_back(i);
    if (!open.empty()) {
        QRectF r;
        for (int i : open) {
            const auto &n = doc->nodelist[i];
            r = r.united(around(n.x, n.y, pad));
        }
        out.push_back({ProblemIssue::Warning,
                       QObject::tr("%n line end(s) not connected to anything (open contour).", "", (int)open.size()),
                       r, {}, open});
    }

    // Magnetics: no boundary condition anywhere
    if (doc->physicsType() == 0 && doc->numBoundaries() == 0) {
        out.push_back({ProblemIssue::Hint,
                       QObject::tr("No boundary condition is defined. Most models need one on the "
                                   "outer boundary -- e.g. Open Boundary (toolbar) for a field in free space."),
                       QRectF(), {}, {}});
    }
    return out;
}

bool confirmProblemCheck(QWidget *parent, EditorWidget *editor)
{
    const auto issues = checkProblem(editor);
    if (issues.empty()) return true;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Check Before Analysis"));
    dlg.resize(620, 320);

    int nErr = 0;
    for (const auto &i : issues) nErr += (i.severity == ProblemIssue::Error);
    auto *head = new QLabel(nErr
        ? QObject::tr("<b>%n problem(s) will stop the analysis or give wrong results.</b>", "", nErr)
        : QObject::tr("<b>A few things to check before analysing.</b>"));
    auto *sub = new QLabel(QObject::tr("Double-click an entry (or select it and press Show) to zoom to it."));

    auto *list = new QTreeWidget;
    list->setColumnCount(1);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setWordWrap(true);
    QStyle *st = dlg.style();
    for (int k = 0; k < (int)issues.size(); k++) {
        auto *it = new QTreeWidgetItem(list);
        it->setText(0, issues[k].text);
        it->setData(0, Qt::UserRole, k);
        it->setIcon(0, st->standardIcon(issues[k].severity == ProblemIssue::Error ? QStyle::SP_MessageBoxCritical
                                       : issues[k].severity == ProblemIssue::Warning ? QStyle::SP_MessageBoxWarning
                                       : QStyle::SP_MessageBoxInformation));
    }
    list->setCurrentItem(list->topLevelItem(0));

    auto *buttons = new QDialogButtonBox;
    QPushButton *show = buttons->addButton(QObject::tr("Show"), QDialogButtonBox::ActionRole);
    QPushButton *go = buttons->addButton(QObject::tr("Analyze Anyway"), QDialogButtonBox::AcceptRole);
    QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    (nErr ? cancel : go)->setDefault(true);

    auto showIssue = [&](QTreeWidgetItem *it) {
        if (!it) return;
        const ProblemIssue &is = issues[it->data(0, Qt::UserRole).toInt()];
        DocumentBase *doc = editor->document();
        doc->UnselectAll();
        for (int i : is.labels) if (i < (int)doc->blocklist.size()) doc->blocklist[i].IsSelected = true;
        for (int i : is.nodes) if (i < (int)doc->nodelist.size()) doc->nodelist[i].IsSelected = true;
        if (is.where.isValid())
            editor->zoomTo(is.where.left(), is.where.top(), is.where.right(), is.where.bottom());
        else
            editor->zoomFit();
        dlg.reject();
    };
    QObject::connect(show, &QPushButton::clicked, &dlg, [&]() { showIssue(list->currentItem()); });
    QObject::connect(list, &QTreeWidget::itemDoubleClicked, &dlg, [&](QTreeWidgetItem *it) { showIssue(it); });
    QObject::connect(go, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(head);
    lay->addWidget(sub);
    lay->addWidget(list, 1);
    lay->addWidget(buttons);
    return dlg.exec() == QDialog::Accepted;
}
