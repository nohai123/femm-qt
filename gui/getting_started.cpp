// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// getting_started.cpp — see getting_started.h.

#include "getting_started.h"
#include "editor_widget.h"
#include "region_map.h"
#include "document.h"

#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

GettingStartedPanel::GettingStartedPanel(QWidget *parent)
    : QDockWidget(tr("Getting Started"), parent)
{
    setObjectName("GettingStartedPanel");
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_text->setTextFormat(Qt::RichText);
    m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_text->setContentsMargins(8, 8, 8, 8);
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_text);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    setWidget(scroll);
    setMinimumWidth(240);
    refresh(nullptr, false);
}

void GettingStartedPanel::refresh(EditorWidget *editor, bool solved)
{
    struct Step { QString title, how; bool done; };
    std::vector<Step> steps;
    DocumentBase *d = editor ? editor->document() : nullptr;

    bool regions = false, allFilled = false, source = false, boundary = false;
    if (d) {
        const auto &faces = editor->regions().faces();
        regions = !faces.empty();
        bool hasDefault = false;
        for (const auto &b : d->blocklist) hasDefault |= b.IsDefault;
        allFilled = regions;
        for (const auto &f : faces) {
            if (f.labels.size() > 1 || (f.labels.empty() && !hasDefault)) allFilled = false;
            for (int i : f.labels) {
                const std::string &m = d->blocklist[i].BlockType;
                if (m.empty() || m == "<None>") allFilled = false;
            }
        }
        // A source: a coil (circuit with turns), a magnet, or for the other
        // physics a boundary/conductor that drives the problem
        for (const auto &b : d->blocklist) {
            source |= (!b.InCircuit.empty() && b.InCircuit != "<None>");
            source |= d->materialCoercivity(b.BlockType) != 0;
        }
        if (d->physicsType() != 0) source = d->numBoundaries() > 0 || d->numCircuits() > 0;
        for (const auto &s : d->linelist) boundary |= !s.BoundaryMarker.empty() && s.BoundaryMarker != "<None>";
        for (const auto &a : d->arclist) boundary |= !a.BoundaryMarker.empty() && a.BoundaryMarker != "<None>";
    }

    steps.push_back({tr("Create a problem"), tr("File &gt; New, or a tile on the start page."), d != nullptr});
    steps.push_back({tr("Draw closed regions"),
                     tr("<b>R</b> rectangle, <b>C</b> circle, <b>L</b> polyline (click the start point to close). "
                        "Exact sizes: type them while drawing. Each closed area becomes a region."), regions});
    steps.push_back({tr("Give every region a material"),
                     tr("<b>F</b> (Assign Material): pick a material in the toolbar, click into each region. "
                        "Red hatching = still missing."), allFilled});
    if (!d || d->physicsType() == 0)
        steps.push_back({tr("Add a source"),
                         tr("Properties &gt; Circuits: add one with a current, then select the coil region and set "
                            "circuit and turns in the Model panel. Or use a magnet material."), source});
    else
        steps.push_back({tr("Define what drives the problem"),
                         tr("Properties &gt; Boundary Conditions / Conductors: a voltage, temperature or current."), source});
    steps.push_back({tr("Set the outer boundary"),
                     tr("Toolbar <b>Open Boundary</b> for a field in free space (then fill the new outer region "
                        "with air), or a boundary condition on the outer lines (Select, right-click &gt; Properties)."),
                     boundary});
    steps.push_back({tr("Solve"), tr("Toolbar <b>Analyze</b>. The check lists anything still missing."), solved});
    steps.push_back({tr("Read the results"),
                     tr("Point at the field to read values; the Results panel shows current, flux linkage and "
                        "inductance. Operation &gt; Plot X-Y along a contour, Export Report for a PDF."), false});

    QString html = "<p><b>" + tr("Your first model, step by step") + "</b></p>";
    bool nextShown = false;
    for (const auto &st : steps) {
        const bool next = !st.done && !nextShown;
        if (next) nextShown = true;
        html += QStringLiteral("<p>%1 %2%3</p>")
                    .arg(st.done ? QStringLiteral("✅") : next ? QStringLiteral("➡️") : QStringLiteral("⬜"),
                         next ? "<b>" + st.title + "</b>" : st.title,
                         next ? "<br><span style='color:gray'>" + st.how + "</span>" : QString());
    }
    if (html != m_last) { m_text->setText(html); m_last = html; }
}
