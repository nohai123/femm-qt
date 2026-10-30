// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// gui_tests.cpp — offscreen tests for the preprocessor editor's interactive
// tools: the CAD-style sketch tools, object snapping, typed input, the node
// pick tolerance, trackpad navigation and undo of the mesh-size keys.
//
//   QT_QPA_PLATFORM=offscreen build/bin/femm_gui_tests

#include "editor_widget.h"
#include "mag_document.h"
#include "problem_check.h"
#include "model_panel.h"
#include "results_widget.h"
#include "results_doc.h"
#include "results_summary.h"
#include "welcome_page.h"
#include "command_palette.h"
#include "view_colors.h"
#include <QSettings>
#include <QTranslator>
#include "sweep_dialog.h"
#include "pattern_dialog.h"
#include "getting_started.h"
#include "report_export.h"
#include "plot_widget.h"
#include "xy_plot.h"
#include "py_state.h"
#include <QProcess>
#include <QTemporaryDir>
#include "script_callbacks.h"
#include "py_mag_editor.h"
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QMenuBar>
#include <QLineEdit>
#include <QListWidget>
#include <QToolButton>
#include <QSignalSpy>
#include <QTableWidget>
#include "material_picker.h"
#include <QTreeWidget>
#include <QSpinBox>
#include <QLabel>

#include <QApplication>
#include <QtTest/QtTest>
#include <cmath>

class GuiTests : public QObject
{
    Q_OBJECT

    MagDocument *doc = nullptr;
    EditorWidget *ed = nullptr;

    // Screen position of a drawing point, from the editor's visible bounds.
    QPoint scr(double x, double y) const
    {
        double x0, y0, x1, y1;
        ed->viewBounds(x0, y0, x1, y1);
        const double mag = (ed->width() - 1) / (x1 - x0);
        return QPoint((int)std::lround((x - x0) * mag),
                      (int)std::lround((ed->height() - 1) - (y - y0) * mag));
    }
    void click(double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::mouseMove(ed, scr(x, y));
        QTest::mouseClick(ed, Qt::LeftButton, mods, scr(x, y));
    }
    void type(const QString &s)
    {
        for (QChar c : s) QTest::keyClick(ed, c.toLatin1());
    }
    int findNode(double x, double y) const
    {
        for (size_t i = 0; i < doc->nodelist.size(); i++)
            if (std::hypot(doc->nodelist[i].x - x, doc->nodelist[i].y - y) < 1e-6) return (int)i;
        return -1;
    }

private slots:
    void init()
    {
        doc = new MagDocument;
        ed = new EditorWidget(doc);   // owns doc
        ed->resize(600, 600);
        ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(ed));
        ed->setObjectSnap(true);
        ed->setSnapToGrid(false);
        ed->zoomTo(-1, -1, 5, 5);
    }
    void cleanup()
    {
        delete ed;
        ed = nullptr; doc = nullptr;
    }

    void polylineClosesContour()
    {
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 0); click(4, 0); click(4, 3); click(0, 3);
        click(0, 0);   // back to the start: closes and ends the chain
        QCOMPARE((int)doc->nodelist.size(), 4);
        QCOMPARE((int)doc->linelist.size(), 4);
        // The chain ended: the next click starts a new one, no segment yet
        click(2, 1.5);
        QCOMPARE((int)doc->linelist.size(), 4);
    }

    void polylineEndsOnEscape()
    {
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 0); click(2, 0); click(2, 2);
        QTest::keyClick(ed, Qt::Key_Escape);
        click(4, 4);
        QCOMPARE((int)doc->linelist.size(), 2);
    }

    void rectangle()
    {
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(3, 2);
        QCOMPARE((int)doc->nodelist.size(), 4);
        QCOMPARE((int)doc->linelist.size(), 4);
        QVERIFY(findNode(3, 0) >= 0);
        QVERIFY(findNode(0, 2) >= 0);
    }

    void circle()
    {
        ed->setSketchTool(EditorWidget::SketchCircle);
        click(2, 2); click(3, 2);
        QCOMPARE((int)doc->nodelist.size(), 2);
        QCOMPARE((int)doc->arclist.size(), 2);
        QVERIFY(findNode(3, 2) >= 0);
        QVERIFY(findNode(1, 2) >= 0);
        for (auto &a : doc->arclist) QCOMPARE(a.ArcLength, 180.0);
    }

    void threePointArcGoesThroughThirdPoint()
    {
        ed->setSketchTool(EditorWidget::SketchArc3);
        click(0, 0); click(2, 0); click(1, 1);
        QCOMPARE((int)doc->arclist.size(), 1);
        const auto &a = doc->arclist[0];
        QVERIFY(std::abs(a.ArcLength - 180.0) < 1e-6);
        // CCW from n0 to n1 passes over the top: n0 must be (2,0)
        QCOMPARE(a.n0, findNode(2, 0));
        QCOMPARE(a.n1, findNode(0, 0));
    }

    void typedLengthAndAngle()
    {
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 0);
        QTest::mouseMove(ed, scr(1, 1));
        type("2.5");
        QTest::keyClick(ed, Qt::Key_Tab);
        type("90");
        QTest::keyClick(ed, Qt::Key_Return);
        QVERIFY2(findNode(0, 2.5) >= 0, "typed length 2.5 at 90 degrees");
        QCOMPARE((int)doc->linelist.size(), 1);
    }

    void typedExpressionWithComma()
    {
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0);
        QTest::mouseMove(ed, scr(1, 1));
        type("1,5");
        QTest::keyClick(ed, Qt::Key_Tab);
        type("2");
        QTest::keyClick(ed, Qt::Key_Return);
        QVERIFY(findNode(1.5, 2) >= 0);
    }

    void snapsToMidpointAndSplits()
    {
        doc->AddNode(0, 0, 1e-3); doc->AddNode(4, 0, 1e-3);
        doc->AddSegment(0, 1);
        ed->setSketchTool(EditorWidget::SketchLine);
        click(2.03, 0.04);            // within the pick tolerance of the midpoint
        QVERIFY2(findNode(2, 0) >= 0, "snapped exactly onto the midpoint");
        QCOMPARE((int)doc->linelist.size(), 2);   // the segment was split
    }

    void snapsToEndpoint()
    {
        doc->AddNode(1, 1, 1e-3);
        ed->setSketchTool(EditorWidget::SketchLine);
        click(1.04, 0.97); click(3, 1);
        QCOMPARE((int)doc->nodelist.size(), 2);
        QCOMPARE((int)doc->linelist.size(), 1);
    }

    void shiftConstrainsTo45Degrees()
    {
        ed->setObjectSnap(false);
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 0);
        click(3, 0.4, Qt::ShiftModifier);
        QCOMPARE((int)doc->nodelist.size(), 2);
        QVERIFY(std::abs(doc->nodelist[1].y) < 1e-9);
    }

    void segmentModeIgnoresFarClicks()
    {
        doc->AddNode(0, 0, 1e-3); doc->AddNode(4, 0, 1e-3);
        ed->setEditAction(1);
        click(2, 3);      // nowhere near a node
        click(4, 0);
        QCOMPARE((int)doc->linelist.size(), 0);
        click(0, 0); click(4, 0);
        QCOMPARE((int)doc->linelist.size(), 1);
    }

    void meshSizeKeysUndo()
    {
        doc->AddBlockLabel(1, 1, 1e-3);
        doc->blocklist[0].MaxArea = 4.0;
        QTest::keyClick(ed, Qt::Key_F3);
        QCOMPARE(doc->blocklist[0].MaxArea, 1.0);
        doc->undo();
        QCOMPARE(doc->blocklist[0].MaxArea, 4.0);
    }

    void trackpadScrollPansPinchZooms()
    {
        double a0, b0, a1, b1;
        ed->viewBounds(a0, b0, a1, b1);
        QWheelEvent pan(QPointF(300, 300), ed->mapToGlobal(QPointF(300, 300)),
                        QPoint(40, 0), QPoint(0, 0), Qt::NoButton, Qt::NoModifier,
                        Qt::ScrollUpdate, false);
        QApplication::sendEvent(ed, &pan);
        double c0, d0, c1, d1;
        ed->viewBounds(c0, d0, c1, d1);
        QVERIFY2(std::abs((c1 - c0) - (a1 - a0)) < 1e-9, "scale unchanged");
        QVERIFY2(c0 < a0, "content moved right");

        QWheelEvent wheel(QPointF(300, 300), ed->mapToGlobal(QPointF(300, 300)),
                          QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(ed, &wheel);
        double e0, f0, e1, f1;
        ed->viewBounds(e0, f0, e1, f1);
        QVERIFY2((e1 - e0) < (c1 - c0), "mouse wheel zooms in");
    }

    // Square 0..4 with a circle (r=1 at 2,2) inside and a dangling line.
    void buildSquareWithCircle()
    {
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(4, 4);
        ed->setSketchTool(EditorWidget::SketchCircle);
        click(2, 2); click(3, 2);
        ed->setSketchTool(EditorWidget::SketchLine);
        click(4, 4); click(4.8, 4.8);
        QTest::keyClick(ed, Qt::Key_Escape);
    }

    void regionsFound()
    {
        buildSquareWithCircle();
        auto &faces = ed->regions().faces();
        QCOMPARE((int)faces.size(), 2);
        QVERIFY(std::abs(faces[0].area - 16.0) < 1e-9);          // square (outer polygon)
        QVERIFY(std::abs(faces[1].area - M_PI) < 0.02);          // circle (sampled)
        QCOMPARE(ed->regions().faceAt(0.5, 0.5), 0);
        QCOMPARE(ed->regions().faceAt(2.1, 2.1), 1);
        QCOMPARE(ed->regions().faceAt(4.5, 2), -1);
    }

    void fillToolAddsAndReassignsLabels()
    {
        buildSquareWithCircle();
        QStringList asked;
        ed->setEnsureMaterial([&](const QString &n) {
            asked << n;
            FMaterialProp m; m.BlockName = n.toStdString();
            doc->blockproplist.push_back(m);
            return true;
        });
        ed->setFillMaterial("Air");
        ed->setSketchTool(EditorWidget::SketchFill);
        click(0.5, 0.5);                       // annulus between square and circle
        QCOMPARE((int)doc->blocklist.size(), 1);
        QCOMPARE(QString::fromStdString(doc->blocklist[0].BlockType), QString("Air"));
        ed->setFillMaterial("Pure Iron");
        click(2.2, 2.1);                       // inside the circle: a second label
        QCOMPARE((int)doc->blocklist.size(), 2);
        click(2.0, 1.6);                       // same region again: reassigned, not added
        QCOMPARE((int)doc->blocklist.size(), 2);
        click(4.5, 1);                         // outside every region: nothing
        QCOMPARE((int)doc->blocklist.size(), 2);
        QCOMPARE(asked, QStringList({"Air", "Pure Iron", "Pure Iron"}));
        const auto &faces = ed->regions().faces();
        QCOMPARE((int)faces[0].labels.size(), 1);
        QCOMPARE((int)faces[1].labels.size(), 1);
        doc->undo();                            // the reassignment is undoable
        QCOMPARE((int)doc->blocklist.size(), 2);
    }

    void problemCheckFindsIssues()
    {
        // Empty model
        auto is = checkProblem(ed);
        QCOMPARE((int)is.size(), 1);
        QCOMPARE(is[0].severity, ProblemIssue::Error);

        buildSquareWithCircle();   // two regions, no labels, one dangling line
        is = checkProblem(ed);
        int err = 0, warn = 0;
        for (auto &i : is) { err += i.severity == ProblemIssue::Error; warn += i.severity == ProblemIssue::Warning; }
        QCOMPARE(err, 2);          // two unlabelled regions
        QCOMPARE(warn, 1);         // the open line end

        // Label both regions; one without material, one doubled
        doc->AddBlockLabel(0.5, 0.5, 1e-3);
        doc->blocklist.back().BlockType = "Air";
        doc->AddBlockLabel(2.2, 2.2, 1e-3);          // <None>
        doc->AddBlockLabel(1.8, 1.8, 1e-3);
        doc->blocklist.back().BlockType = "Air";
        is = checkProblem(ed);
        QStringList texts;
        for (auto &i : is) if (i.severity == ProblemIssue::Error) texts << i.text;
        QCOMPARE(texts.size(), 2);
        QVERIFY(texts.join("|").contains("2 block labels"));
        QVERIFY(texts.join("|").contains("no material"));

        // A default label covers unlabelled regions
        cleanup(); init();
        buildSquareWithCircle();
        doc->AddBlockLabel(0.5, 0.5, 1e-3);
        doc->blocklist.back().BlockType = "Air";
        doc->blocklist.back().IsDefault = true;
        is = checkProblem(ed);
        for (auto &i : is) QVERIFY(i.severity != ProblemIssue::Error);
    }

    void modelPanelSelectsAndEdits()
    {
        buildSquareWithCircle();
        for (const char *n : {"Air", "Pure Iron"}) {
            FMaterialProp m; m.BlockName = n; doc->blockproplist.push_back(m);
        }
        doc->AddBlockLabel(0.5, 0.5, 1e-3); doc->blocklist.back().BlockType = "Air";
        doc->AddBlockLabel(2.2, 2.2, 1e-3); doc->blocklist.back().BlockType = "Air";

        ModelPanel panel;
        panel.ensureMaterial = [](EditorWidget *, const QString &) { return true; };
        panel.show();
        panel.refresh(ed);
        auto *tree = panel.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QTreeWidgetItem *mats = tree->topLevelItem(0);
        QCOMPARE(mats->childCount(), 2);
        QCOMPARE(mats->child(0)->text(1), QString("2"));   // Air used twice
        // Clicking "Air" selects both labels
        emit tree->itemClicked(mats->child(0), 0);
        QCOMPARE((int)doc->blocklist[0].IsSelected + (int)doc->blocklist[1].IsSelected, 2);
        // Inspector: change material of the selection
        doc->blocklist[0].IsSelected = false;
        panel.refresh(ed);
        MaterialPicker *mp = nullptr;
        for (auto *p : panel.findChildren<MaterialPicker *>()) mp = p;
        QVERIFY(mp);
        mp->setDocument(doc);
        emit mp->materialChosen("Pure Iron");
        QCOMPARE(QString::fromStdString(doc->blocklist[1].BlockType), QString("Pure Iron"));
        QCOMPARE(QString::fromStdString(doc->blocklist[0].BlockType), QString("Air"));
        doc->undo();
        QCOMPARE(QString::fromStdString(doc->blocklist[1].BlockType), QString("Air"));

        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) { panel.resize(260, 420); panel.refresh(ed); panel.grab().save(dir + "/model_panel.png"); }
    }

    // Needs a solved magnetics problem: FEMM_GUI_TEST_ANS=<file.ans>
    void resultsHoverShowsValues()
    {
        const QString ans = qEnvironmentVariable("FEMM_GUI_TEST_ANS");
        if (ans.isEmpty()) QSKIP("FEMM_GUI_TEST_ANS not set");
        ed->hide();   // offscreen windows overlap; the editor would get the moves
        auto *rd = new ResultsDoc;
        QVERIFY(rd->OpenDocument(ans.toStdString()));
        ResultsWidget rw(rd);
        rw.resize(600, 500);
        rw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rw));
        QTest::qWait(50);
        rw.zoomFit();

        auto move = [&](QPointF p) {
            QMouseEvent mv(QEvent::MouseMove, p, rw.mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&rw, &mv);
        };
        // Calibrate screen -> drawing from two moves, then aim at an element
        double cx = 0, cy = 0;
        connect(&rw, &ResultsWidget::coordinatesChanged, [&](double x, double y) { cx = x; cy = y; });
        rw.setSnapToGrid(false);
        move(QPointF(100, 100)); const double ax = cx, ay = cy;
        move(QPointF(400, 300)); const double bx = cx, by = cy;
        const double sx = 300 / (bx - ax), sy = 200 / (by - ay);
        // Walk the cursor until it is over a meshed point; the readout must
        // then be showing |B|, and it must be hidden off the mesh.
        QLabel *hover = nullptr;
        bool sawOff = false;
        for (int x = 2; x < 600 && !hover; x += 3) {
            for (int y = 2; y < 500 && !hover; y += 3) {
                move(QPointF(x, y));
                ResultsDoc::PointVals u;
                const bool on = rd->GetPointValues(cx, cy, u);
                QLabel *vis = nullptr;
                for (auto *l : rw.findChildren<QLabel *>()) if (l->isVisible() && l->text().contains("|B|")) vis = l;
                if (on) { QVERIFY2(vis, "readout shown over the mesh"); hover = vis; }
                else if (!vis) sawOff = true;
            }
        }
        QVERIFY(sawOff);
        QVERIFY2(hover, "hover readout visible over the mesh");
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) rw.grab().save(dir + "/results_hover.png");
    }

    void resultsSummaryListsCircuits()
    {
        const QString ans = qEnvironmentVariable("FEMM_GUI_TEST_ANS");
        if (ans.isEmpty()) QSKIP("FEMM_GUI_TEST_ANS not set");
        ResultsDoc rd;
        QVERIFY(rd.OpenDocument(ans.toStdString()));
        ResultsSummaryPanel panel;
        panel.refresh(&rd);
        auto *t = panel.findChild<QTableWidget *>();
        QCOMPARE(t->rowCount(), (int)rd.circproplist.size());
        for (int k = 0; k < t->rowCount(); k++)
            QCOMPARE(t->item(k, 0)->text(), QString::fromStdString(rd.circproplist[k].CircName));
        panel.refresh(nullptr);
        QCOMPARE(t->rowCount(), 0);
    }

    void newProblemDialogAndWelcome()
    {
        ed->hide();
        NewProblemDialog dlg(3);
        auto st = dlg.settings();
        QCOMPARE(st.physics, 3);
        QCOMPARE(st.frequency, 60.0);       // current flow defaults to 60 Hz
        WelcomePage wp;
        wp.resize(1000, 640);
        wp.show();
        QVERIFY(QTest::qWaitForWindowExposed(&wp));
        QSignalSpy spy(&wp, &WelcomePage::newProblem);
        QList<QToolButton *> buttons;   // the four "New" tiles (not the search field's clear button)
        for (auto *b : wp.findChildren<QToolButton *>()) if (b->text().contains('\n')) buttons << b;
        QCOMPARE(buttons.size(), 4);
        buttons[1]->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) {
            wp.grab().save(dir + "/welcome.png");
            dlg.show(); QTest::qWait(30); dlg.grab().save(dir + "/new_problem.png");
        }
    }

    void drag(double x0, double y0, double x1, double y1, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::mouseMove(ed, scr(x0, y0));
        QTest::mousePress(ed, Qt::LeftButton, mods, scr(x0, y0));
        for (int i = 1; i <= 5; i++) {
            const double t = i / 5.0;
            QMouseEvent mv(QEvent::MouseMove, QPointF(scr(x0 + t * (x1 - x0), y0 + t * (y1 - y0))),
                           ed->mapToGlobal(QPointF(scr(x0 + t * (x1 - x0), y0 + t * (y1 - y0)))),
                           Qt::NoButton, Qt::LeftButton, mods);
            QApplication::sendEvent(ed, &mv);
        }
        QTest::mouseRelease(ed, Qt::LeftButton, mods, scr(x1, y1));
    }

    void selectToolClickWindowAndDrag()
    {
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(2, 1);          // rectangle A
        click(3, 0); click(4, 1);          // rectangle B
        ed->setSketchTool(EditorWidget::SketchSelect);
        auto nsel = [&]() { int n = 0; for (auto &x : doc->linelist) n += x.IsSelected; return n; };

        click(1, 0.02);                     // bottom edge of A
        QCOMPARE(nsel(), 1);
        click(3.5, 1.0, Qt::ShiftModifier); // add top edge of B
        QCOMPARE(nsel(), 2);
        click(2.5, 2.5);                    // empty space clears
        QCOMPARE(nsel(), 0);

        // Window left-to-right around A only: A's 4 segments, not B's
        drag(-0.5, -0.5, 2.5, 1.5);
        QCOMPARE(nsel(), 4);
        // Crossing right-to-left through B's left half: touches 3 of B's segments
        click(2.5, 2.5);
        drag(3.5, 1.5, 2.8, -0.5);
        QCOMPARE(nsel(), 3);

        // Drag a node of A to a new place; undo restores it
        click(2.5, 2.5);
        const int n = findNode(2, 1);
        QVERIFY(n >= 0);
        drag(2, 1, 2, 2);
        QVERIFY(findNode(2, 1) < 0);
        QVERIFY(findNode(2, 2) >= 0);
        doc->undo();
        QVERIFY(findNode(2, 1) >= 0);
    }

    void germanTranslationLoads()
    {
        QTranslator tr;
        QVERIFY(tr.load(":/i18n/femm_de.qm"));
        QCoreApplication::installTranslator(&tr);
        WelcomePage wp;
        bool found = false;
        for (auto *b : wp.findChildren<QToolButton *>()) found |= b->text().startsWith("Magnetik");
        QVERIFY2(found, "welcome tiles in German");
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) {
            ed->hide();
            wp.resize(1000, 640); wp.show(); QTest::qWait(30);
            wp.grab().save(dir + "/welcome_de.png");
            NewProblemDialog dlg(0); dlg.show(); QTest::qWait(30);
            dlg.grab().save(dir + "/new_problem_de.png");
        }
        QCoreApplication::removeTranslator(&tr);
    }

    // Runs the script the sweep dialog writes in the real engine.
    // Needs FEMM_GUI_TEST_SWEEP=<model.fem with circuit "coil" and a linear
    // core> and FEMM_GUI_TEST_HEADLESS=<femm_headless>.
    void sweepScriptGivesConstantInductance()
    {
        const QString model = qEnvironmentVariable("FEMM_GUI_TEST_SWEEP");
        const QString headless = qEnvironmentVariable("FEMM_GUI_TEST_HEADLESS");
        if (model.isEmpty() || headless.isEmpty()) QSKIP("FEMM_GUI_TEST_SWEEP / _HEADLESS not set");
        auto *mdoc = new MagDocument;
        QVERIFY(mdoc->OpenDocument(model.toStdString()));
        auto *e2 = new EditorWidget(mdoc);
        SweepDialog dlg(e2);
        dlg.findChild<QLineEdit *>("sweepFrom")->setText("1");
        dlg.findChild<QLineEdit *>("sweepTo")->setText("3");
        dlg.findChild<QSpinBox *>("sweepSteps")->setValue(3);
        QTemporaryDir tmp;
        const QString csv = tmp.filePath("out.csv"), py = tmp.filePath("sweep.py");
        const QString body = dlg.script(csv);
        QVERIFY(!body.isEmpty());
        QCOMPARE(dlg.columns().size(), 2);
        QFile f(py);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(("opendocument('" + model + "')\n" + body).toUtf8());
        f.close();
        QProcess proc;
        proc.start(headless, {"-run", py});
        QVERIFY(proc.waitForFinished(300000));
        QFile out(csv);
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(out.readAll()).trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        double L[3];
        for (int i = 0; i < 3; i++) {
            const QStringList p = lines[i].split(',');
            QCOMPARE(p[0].toDouble(), 1.0 + i);
            L[i] = p[1].toDouble();
        }
        QVERIFY2(L[0] > 1e-4 && L[0] < 1e-2, qPrintable(QString::number(L[0])));
        QVERIFY(std::abs(L[2] - L[0]) < 1e-6 * L[0]);    // linear core
        delete e2;
    }

    void trimRemovesPieceBetweenCrossings()
    {
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 1); click(4, 1); QTest::keyClick(ed, Qt::Key_Escape);   // horizontal
        click(2, 0); click(2, 2); QTest::keyClick(ed, Qt::Key_Escape);   // vertical crossing it
        QCOMPARE((int)doc->linelist.size(), 4);    // split at the crossing
        ed->setSketchTool(EditorWidget::SketchTrim);
        click(3, 1.02);                             // right half of the horizontal
        QCOMPARE((int)doc->linelist.size(), 3);
        QVERIFY(findNode(4, 1) < 0);                // its loose end went too
        QVERIFY(findNode(2, 1) >= 0);               // the crossing stays
    }

    void offsetRectangleInward()
    {
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(4, 3);
        ed->setSketchTool(EditorWidget::SketchOffset);
        click(2, 0.02);                             // pick the bottom edge -> whole rectangle
        QTest::mouseMove(ed, scr(2, 1));            // inside
        type("0.5");
        QTest::keyClick(ed, Qt::Key_Return);
        QCOMPARE((int)doc->linelist.size(), 8);
        QVERIFY(findNode(0.5, 0.5) >= 0);
        QVERIFY(findNode(3.5, 2.5) >= 0);
    }

    void offsetCircleOutward()
    {
        ed->setSketchTool(EditorWidget::SketchCircle);
        click(2, 2); click(3, 2);
        ed->setSketchTool(EditorWidget::SketchOffset);
        click(2, 3.0);                              // top of the circle
        QTest::mouseMove(ed, scr(2, 3.6));          // outside
        type("0.5");
        QTest::keyClick(ed, Qt::Key_Return);
        QCOMPARE((int)doc->arclist.size(), 4);
        QVERIFY(findNode(3.5, 2) >= 0);
        QVERIFY(findNode(0.5, 2) >= 0);
    }

    void offsetPreviewScreenshot()
    {
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (dir.isEmpty()) QSKIP("FEMM_GUI_TEST_SHOTS not set");
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 0); click(3, 0); click(4, 2); click(1, 3); click(0, 0);
        ed->setSketchTool(EditorWidget::SketchOffset);
        click(1.5, 0.02);
        QTest::mouseMove(ed, scr(1.5, 0.6));
        ed->grab().save(dir + "/offset_preview.png");
        QTest::keyClick(ed, Qt::Key_Escape);
        ed->setSketchTool(EditorWidget::SketchDimension);
        QTest::mouseMove(ed, scr(3.5, 1.0));
        ed->grab().save(dir + "/dimension.png");
    }

    void circularPatternOfSlot()
    {
        // A small square "slot" at (2,0)..(2.5,0.5), copied 6x around the origin
        ed->setSketchTool(EditorWidget::SketchRect);
        click(2, 0); click(2.5, 0.5);
        ed->zoomTo(-4, -4, 4, 4);
        ed->setSketchTool(EditorWidget::SketchSelect);
        drag(1.8, -0.2, 2.7, 0.7);                 // window around the slot
        int sel = 0; for (auto &x : doc->linelist) sel += x.IsSelected;
        QCOMPARE(sel, 4);
        PatternDialog dlg(ed);
        dlg.findChild<QSpinBox *>()->setValue(6);
        QCOMPARE((int)dlg.transforms().size(), 5);
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) ed->grab().save(dir + "/pattern_preview.png");
        dlg.apply();
        QCOMPARE((int)doc->linelist.size(), 24);
        // The copy at 60 degrees: corner (2,0) -> (1, 1.732)
        bool found = false;
        for (auto &n : doc->nodelist) found |= std::hypot(n.x - 1.0, n.y - std::sqrt(3.0)) < 1e-9;
        QVERIFY(found);
        doc->undo();
        QCOMPARE((int)doc->linelist.size(), 4);
    }

    void gettingStartedTicksOff()
    {
        GettingStartedPanel gs;
        auto text = [&]() { return gs.findChild<QLabel *>()->text(); };
        gs.refresh(ed, false);
        QCOMPARE(text().count(QChar(0x2705)), 1);           // problem exists
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(2, 2);
        gs.refresh(ed, false);
        QCOMPARE(text().count(QChar(0x2705)), 2);           // + closed region
        FMaterialProp m; m.BlockName = "Air"; doc->blockproplist.push_back(m);
        doc->AddBlockLabel(1, 1, 1e-3); doc->blocklist.back().BlockType = "Air";
        gs.refresh(ed, false);
        QCOMPARE(text().count(QChar(0x2705)), 3);           // + materials
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) { gs.resize(300, 520); gs.show(); QTest::qWait(30); gs.grab().save(dir + "/getting_started.png"); }
    }

    void reportAndPlotDock()
    {
        const QString ans = qEnvironmentVariable("FEMM_GUI_TEST_ANS");
        if (ans.isEmpty()) QSKIP("FEMM_GUI_TEST_ANS not set");
        ed->hide();
        auto *rd = new ResultsDoc;
        QVERIFY(rd->OpenDocument(ans.toStdString()));
        ResultsWidget rw(rd);
        rw.resize(600, 450); rw.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rw));
        QTemporaryDir tmp;
        const QString pdf = tmp.filePath("r.pdf");
        QVERIFY(exportResultsReport(nullptr, &rw, pdf));
        QFile f(pdf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.read(5) == "%PDF-");
        QVERIFY(f.size() > 10000);                          // contains the picture

        // X-Y plots go to the dock when one exists
        PlotDock dock;
        struct P { double x; std::vector<double> vals; };
        std::vector<P> pts{{0, {1}}, {1, {2}}, {2, {1.5}}};
        xyplot::showAlongContour(nullptr, "|B|", pts);
        QVERIFY(dock.isVisible());
        QVERIFY(dock.windowTitle().contains("|B|"));
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (!dir.isEmpty()) QFile::copy(pdf, dir + "/report.pdf");
    }

    void commandPaletteFindsAndRuns()
    {
        QMenuBar mb;
        QMenu *mesh = mb.addMenu("&Mesh");
        int created = 0, hidden = 0;
        mesh->addAction("&Create Mesh", [&]() { created++; });
        QAction *h = mesh->addAction("Purge Mesh", [&]() { hidden++; });
        h->setVisible(false);
        mb.addMenu("&View")->addAction("Zoom &In");
        CommandPalette pal(&mb);
        auto *list = pal.findChild<QListWidget *>();
        auto *edit = pal.findChild<QLineEdit *>();
        QCOMPARE(list->count(), 2);                // hidden command not listed
        edit->setText("mesh cre");
        QCOMPARE(list->count(), 1);
        QTest::keyClick(edit, Qt::Key_Return);
        QTRY_COMPARE(created, 1);
        QCOMPARE(hidden, 0);
    }

    // Not a check: with FEMM_GUI_TEST_SHOTS=<dir> set, saves pictures of the
    // previews for a human (or Claude) to look at.
    void screenshots()
    {
        const QString dir = qEnvironmentVariable("FEMM_GUI_TEST_SHOTS");
        if (dir.isEmpty()) QSKIP("FEMM_GUI_TEST_SHOTS not set");
        doc->AddNode(0, 0, 1e-3); doc->AddNode(4, 0, 1e-3); doc->AddSegment(0, 1);
        ed->setSketchTool(EditorWidget::SketchLine);
        click(0, 3);
        QTest::mouseMove(ed, scr(2.02, 0.05));
        ed->grab().save(dir + "/line_snap_midpoint.png");
        type("2");
        ed->grab().save(dir + "/line_typed_length.png");
        QTest::keyClick(ed, Qt::Key_Escape);
        QTest::keyClick(ed, Qt::Key_Escape);
        ed->setSketchTool(EditorWidget::SketchArc3);
        click(1, 1); click(3, 1);
        QTest::mouseMove(ed, scr(2, 2.2));
        ed->grab().save(dir + "/arc3_preview.png");
        QTest::keyClick(ed, Qt::Key_Escape);
        ed->setSketchTool(EditorWidget::SketchCircle);
        click(2, 2.5);
        QTest::mouseMove(ed, scr(3.2, 3));
        ed->grab().save(dir + "/circle_preview.png");
        QTest::keyClick(ed, Qt::Key_Escape);

        cleanup(); init();
        buildSquareWithCircle();
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0.3, 0.3); click(1.2, 1.0);
        ed->setEnsureMaterial([&](const QString &n) {
            FMaterialProp m; m.BlockName = n.toStdString();
            doc->blockproplist.push_back(m); return true;
        });
        ed->setFillMaterial("Air");
        ed->setSketchTool(EditorWidget::SketchFill);
        click(3.5, 3.5);
        ed->setFillMaterial("M-19 Steel");
        click(2.2, 2.2);
        ed->setFillMaterial("Copper");
        QTest::mouseMove(ed, scr(0.7, 0.6));
        ed->grab().save(dir + "/regions_fill.png");
        // Same drawing, dark theme, with a boundary condition on the square
        for (auto &sg : doc->linelist) sg.BoundaryMarker = "A=0";
        QSettings().setValue("ui/theme", 2);
        viewColors().load();
        ed->grab().save(dir + "/regions_dark.png");
        QSettings().remove("ui/theme");
        viewColors().load();
    }

    void modifiedTracking()
    {
        QVERIFY(!ed->hasUnsavedEdits());
        ed->setSketchTool(EditorWidget::SketchRect);
        click(0, 0); click(1, 1);
        QVERIFY(ed->hasUnsavedEdits());
        ed->markSaved();
        QVERIFY(!ed->hasUnsavedEdits());
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
