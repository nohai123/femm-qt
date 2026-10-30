// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// gui_tests.cpp — offscreen tests for the preprocessor editor's interactive
// tools: the CAD-style sketch tools, object snapping, typed input, the node
// pick tolerance, trackpad navigation and undo of the mesh-size keys.
//
//   QT_QPA_PLATFORM=offscreen build/bin/femm_gui_tests

#include "editor_widget.h"
#include "mag_document.h"

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
