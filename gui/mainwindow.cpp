// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// mainwindow.cpp — FEMM Qt main window
// Switches between preprocessor and postprocessor menus/toolbars
// based on the active MDI tab type.
#include "mainwindow.h"
#include "femm_resource.h"
#include "build_info.h"
#include "py_console.h"
#include "pre_prefs_dialog.h"
#include "script_prefs_dialog.h"
#include "coord_display.h"
#include "editor_widget.h"
#include "results_widget.h"
#include "scalar_results_widget.h"
#include "vector_choice.h"
#include "elec_results_doc.h"
#include "heat_results_doc.h"
#include "heat_document.h"
#include "curr_document.h"
#include "heat_matlib_dialog.h"
#include "curr_matlib_dialog.h"
#include "heat_handler.h"
#include "curr_results_doc.h"
#include "py_mag_postproc.h"
#include "py_elec_editor.h"
#include "py_elec_postproc.h"
#include "py_heat_editor.h"
#include "py_heat_postproc.h"
#include "py_curr_editor.h"
#include "py_curr_postproc.h"
#include "prop_list_dialog.h"
#include "matlib_dialog.h"
#include "elec_matlib_dialog.h"
#include "problem_dialog.h"
#include "mag_document.h"
#include "elec_document.h"
#include "results_doc.h"
#include "postproc_select.h"
#include "py_state.h"
#include "py_mag_editor.h"
#include "tcp_server.h"
#include "output_dialog.h"
#include "copy_move_dialog.h"
#include "solver_progress_dialog.h"
#include "complex.h"

#include <QMenuBar>
#include <QSocketNotifier>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QApplication>
#include <QKeyEvent>
#include <QClipboard>
#include <QPixmap>
#include <QFileDialog>
#include <QInputDialog>
#include <QMdiSubWindow>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTimer>
#include <QPainter>
#include <QCloseEvent>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileOpenEvent>
#include <QUrl>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QDesktopServices>

#include "py_line_edit.h"
#include "dxf_io.h"
#include "material_picker.h"
#include "problem_check.h"
#include "model_panel.h"
#include "results_summary.h"
#include "welcome_page.h"
#include "view_colors.h"
#include "command_palette.h"
#include "sweep_dialog.h"
#include "pattern_dialog.h"
#include "plot_widget.h"
#include "report_export.h"
#include "getting_started.h"
#include "region_map.h"
#include <QStackedWidget>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>

static bool femmOpenable(const QString &path);

static QIcon icon(const char *name)
{
    return QIcon(QString(":/icons/%1.png").arg(name));
}

// Line-art icons for the sketch tools, painted at 1x and 2x so they stay
// sharp on Retina displays (the legacy PNGs are 16x15 bitmaps).
static QIcon sketchIcon(int tool)
{
    QIcon ic;
    for (int scale : {1, 2}) {
        QPixmap pm(16 * scale, 15 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 0, 200), 1.4));
        p.setBrush(Qt::NoBrush);
        auto dot = [&](double x, double y) {
            p.save(); p.setPen(Qt::NoPen); p.setBrush(Qt::black);
            p.drawRect(QRectF(x - 1.5, y - 1.5, 3, 3)); p.restore();
        };
        switch (tool) {
        case EditorWidget::SketchLine: {
            QPolygonF pl; pl << QPointF(2, 12) << QPointF(6, 4) << QPointF(11, 10) << QPointF(14, 3);
            p.drawPolyline(pl);
            for (auto &pt : pl) dot(pt.x(), pt.y());
            break;
        }
        case EditorWidget::SketchRect:
            p.drawRect(QRectF(2.5, 3.5, 11, 8));
            dot(2.5, 3.5); dot(13.5, 11.5);
            break;
        case EditorWidget::SketchCircle:
            p.drawEllipse(QPointF(8, 7.5), 5.5, 5.5);
            dot(8, 7.5);
            break;
        case EditorWidget::SketchArc3:
            p.drawArc(QRectF(2, 3, 12, 14), 20 * 16, 140 * 16);
            dot(2.5, 7.5); dot(8, 3); dot(13.5, 7.5);
            break;
        case -1:   // Pattern: a ring of small squares
            for (int k = 0; k < 6; k++) {
                const double a = k * M_PI / 3;
                p.drawRect(QRectF(8 + 5 * std::cos(a) - 1.5, 7.5 + 5 * std::sin(a) - 1.5, 3, 3));
            }
            break;
        case EditorWidget::SketchTrim:
            p.drawLine(QPointF(2, 7.5), QPointF(14, 7.5));
            p.drawLine(QPointF(8, 2), QPointF(8, 13));
            p.setPen(QPen(QColor(220, 40, 40), 1.6));
            p.drawLine(QPointF(10, 5), QPointF(14, 10));
            p.drawLine(QPointF(14, 5), QPointF(10, 10));
            break;
        case EditorWidget::SketchOffset:
            p.drawRect(QRectF(2, 2, 12, 11));
            p.setPen(QPen(QColor(0, 0, 200), 1.2, Qt::DashLine));
            p.drawRect(QRectF(5, 5, 6, 5));
            break;
        case EditorWidget::SketchExtend:
            p.drawLine(QPointF(2, 7.5), QPointF(8, 7.5));
            p.drawLine(QPointF(13.5, 2), QPointF(13.5, 13));
            p.setPen(QPen(QColor(0, 0, 200), 1.2, Qt::DashLine));
            p.drawLine(QPointF(8, 7.5), QPointF(13, 7.5));
            break;
        case EditorWidget::SketchDimension:
            p.drawLine(QPointF(2, 11), QPointF(14, 11));
            p.drawLine(QPointF(2, 8), QPointF(2, 14));
            p.drawLine(QPointF(14, 8), QPointF(14, 14));
            p.setPen(QPen(QColor(230, 120, 0), 1.2));
            p.drawText(QRectF(0, 0, 16, 9), Qt::AlignCenter, QStringLiteral("12"));
            break;
        case EditorWidget::SketchSelect: {
            // Arrow pointer
            QPolygonF arrow;
            arrow << QPointF(4, 1.5) << QPointF(4, 12.5) << QPointF(6.8, 9.8) << QPointF(8.8, 13.8)
                  << QPointF(10.6, 13) << QPointF(8.6, 9) << QPointF(12.2, 9) ;
            p.setBrush(Qt::white);
            p.setPen(QPen(Qt::black, 1.1));
            p.drawPolygon(arrow);
            break;
        }
        case EditorWidget::SketchFill: {
            // Paint bucket
            QPolygonF bucket;
            bucket << QPointF(3, 6) << QPointF(8, 1.5) << QPointF(13, 6.5) << QPointF(8, 11.5);
            p.setBrush(QColor(225, 130, 60));
            p.drawPolygon(bucket);
            p.setBrush(QColor(0, 0, 200));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(13.5, 11.5), 1.6, 2.2);
            break;
        }
        }
        ic.addPixmap(pm);
    }
    return ic;
}

MainWindow::MainWindow(TcpServer *server, QWidget *parent)
    : QMainWindow(parent)
    , m_server(server)
{
    setWindowTitle("femm");
    resize(1024, 768);

    m_mdiArea = new QMdiArea(this);
    // Closing a tab returns to the most-recently-used one (e.g. close doc2.ans
    // -> back to doc2.fem), not Qt's default creation-order fallback.
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsClosable(true);
    m_mdiArea->setTabPosition(QTabWidget::South);
    m_mdiArea->setDocumentMode(true);

    // Welcome page while no document is open, the documents otherwise
    m_welcome = new WelcomePage;
    connect(m_welcome, &WelcomePage::newProblem, this, [this](int ph) { createNewProblem(ph, true); });
    connect(m_welcome, &WelcomePage::openFile, this, [this](const QString &p) { openPath(p); });
    connect(m_welcome, &WelcomePage::openOther, this, &MainWindow::onOpen);
    m_centralStack = new QStackedWidget;
    m_centralStack->addWidget(m_welcome);
    m_centralStack->addWidget(m_mdiArea);
    setCentralWidget(m_centralStack);

    // Style checked toolbar buttons
    setStyleSheet(
        "QToolBar QToolButton:checked {"
        "  background-color: #d0d0d0;"
        "  border: 1px inset #808080;"
        "}"
    );

    // Model panel: materials / boundaries / circuits + selection inspector
    m_modelPanel = new ModelPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_modelPanel);
    m_modelPanel->openPropertyList = [this](int type) {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        PropertyListDialog dlg(e->document(), type, this);
        dlg.exec();
        e->update();
    };
    m_modelPanel->openMaterialLibrary = [this]() { if (m_actMaterialLibrary) m_actMaterialLibrary->trigger(); };
    m_modelPanel->ensureMaterial = [this](EditorWidget *e, const QString &n) { return ensureMaterial(e, n); };
    m_modelPanel->hide();
    connect(m_modelPanel->toggleViewAction(), &QAction::triggered, this, [](bool on) {
        QSettings().setValue("ui/modelPanel", on);
    });

    // Results panel: circuit current, voltage, flux linkage, L and R
    m_resultsPanel = new ResultsSummaryPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_resultsPanel);
    m_resultsPanel->hide();
    connect(m_resultsPanel->toggleViewAction(), &QAction::triggered, this, [](bool on) {
        QSettings().setValue("ui/resultsPanel", on);
    });

    // Getting Started checklist (Help menu; shown with the first new model)
    m_gettingStarted = new GettingStartedPanel(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_gettingStarted);
    m_gettingStarted->hide();

    // Plot panel under the postprocessor (X-Y plots along contours)
    m_plotDock = new PlotDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_plotDock);
    m_plotDock->hide();

    createCommonMenus();
    createPreprocessorUI();
    createPostprocessorUI();
    createLeftToolbars();
    createStatusBar();


    // Python console: constructed eagerly (hidden) so the interpreter's
    // VM-loop hook is installed even if the user never opens the console.
    // That hook pumps the Qt event loop during a running script (keeping the
    // UI alive) and is what lets Esc interrupt. showPyConsole() just reveals it.
    m_pyConsole = new PyConsole(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_pyConsole);
    m_pyConsole->hide();
    connect(m_pyConsole, &PyConsole::scriptRunningChanged, this, [this](bool running) {
        if (running)
            statusBar()->showMessage(tr("Script running - press Esc to stop"));
        else
            statusBar()->clearMessage();
    });
    // Esc interrupts a running script, application-wide (see eventFilter()).
    qApp->installEventFilter(this);
    // Files dropped anywhere on the window open as File>Open would. Child
    // widgets that do not take drops themselves pass the drag up to here.
    setAcceptDrops(true);

    // Window control buttons on the right side of the menu bar
    {
        auto *spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        menuBar()->setCornerWidget(spacer, Qt::TopLeftCorner);

        m_winButtons = new QWidget;
        auto *winBtns = m_winButtons;
        auto *lay = new QHBoxLayout(winBtns);
        lay->setContentsMargins(0, 0, 4, 0);
        lay->setSpacing(2);
        auto mkBtn = [](const QString &text) {
            auto *b = new QPushButton(text);
            b->setFixedSize(26, 20);
            b->setFlat(true);
            b->setFont(QFont(b->font().family(), 10));
            return b;
        };
        auto *minBtn = mkBtn(QStringLiteral("\u2014"));   // —
        auto *maxBtn = mkBtn(QStringLiteral("\u25a1"));   // □
        auto *closeBtn = mkBtn(QStringLiteral("\u2715")); // ✕
        lay->addWidget(minBtn);
        lay->addWidget(maxBtn);
        lay->addWidget(closeBtn);
        menuBar()->setCornerWidget(winBtns, Qt::TopRightCorner);

        connect(minBtn, &QPushButton::clicked, [this]() {
            auto *sub = m_mdiArea->activeSubWindow();
            if (sub) sub->showMinimized();
        });
        connect(maxBtn, &QPushButton::clicked, [this]() {
            auto *sub = m_mdiArea->activeSubWindow();
            if (!sub) return;
            if (sub->isMaximized()) sub->showNormal();
            else sub->showMaximized();
        });
        connect(closeBtn, &QPushButton::clicked, [this]() {
            auto *sub = m_mdiArea->activeSubWindow();
            if (sub) sub->close();
        });
    }

    // Start with empty UI (no document open yet)
    showEmptyUI();

    // Switch UI when active tab changes
    connect(m_mdiArea, &QMdiArea::subWindowActivated,
            this, &MainWindow::onSubWindowActivated);

    // Drive the IPC server from socket-readable events instead of polling
    // on a 10ms QTimer. QSocketNotifier integrates with Qt's event loop and
    // wakes the GUI thread the moment data arrives on the socket — zero
    // polling latency. We need two notifiers: one on the listen socket
    // (fires when a new client connection is pending) and one on the
    // client socket (fires when a command arrives). The client notifier
    // is created lazily after acceptClient() succeeds, since we don't
    // have a client socket FD until then.
    if (m_server && m_server->isListening()) {
        m_listenNotifier = new QSocketNotifier(
            (qintptr)m_server->listenSocket(), QSocketNotifier::Read, this);
        connect(m_listenNotifier, &QSocketNotifier::activated,
                this, &MainWindow::onListenSocketReady);
    }

    // Throttle ticker for MDI subwindow repaints during action-command
    // bursts (e.g. a script drawing a motor with thousands of
    // mi_addnode/mi_addsegment calls). 100ms = ~10 FPS visual progress
    // during long bursts. Self-stopping when nothing is dirty.
    m_repaintTicker = new QTimer(this);
    m_repaintTicker->setInterval(100);
    connect(m_repaintTicker, &QTimer::timeout,
            this, &MainWindow::onRepaintTick);

    // Modified marker on editor tabs. Cheap: compares two integers per tab.
    m_modifiedTicker = new QTimer(this);
    m_modifiedTicker->setInterval(500);
    connect(m_modifiedTicker, &QTimer::timeout, this, &MainWindow::refreshModifiedMarks);
    m_modifiedTicker->start();

    // Autosave -- not for instances a binding launched to run a script
    if (!(m_server && m_server->exitOnDisconnect())) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave";
        m_autosaveDir = base + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(m_autosaveDir);
        m_autosaveLock = new QLockFile(m_autosaveDir + "/instance.lock");
        m_autosaveLock->setStaleLockTime(0);
        m_autosaveLock->tryLock();
        m_autosaveTimer = new QTimer(this);
        m_autosaveTimer->setInterval(QSettings().value("autosave/intervalSec", 120).toInt() * 1000);
        connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveNow);
        m_autosaveTimer->start();
        QTimer::singleShot(400, this, &MainWindow::offerRecovery);
    }
}

MainWindow::~MainWindow() {}

void MainWindow::onListenSocketReady()
{
    if (!m_server || !m_server->isListening()) return;
    // processEvents() will accept the pending connection and set up the
    // client socket. After it returns, check whether a client is now
    // connected and (if so) attach a notifier to its socket so subsequent
    // command bytes wake us up immediately.
    m_server->processEvents();
    if (m_server->clientSocket() != INVALID_SOCK && !m_clientNotifier) {
        m_clientNotifier = new QSocketNotifier(
            (qintptr)m_server->clientSocket(), QSocketNotifier::Read, this);
        connect(m_clientNotifier, &QSocketNotifier::activated,
                this, &MainWindow::onClientSocketReady);
    }
}

void MainWindow::onClientSocketReady()
{
    if (!m_server || !m_server->isListening()) return;
    // processEvents() will recv() any complete commands and run them.
    // If the client disconnected during processing, tear down the
    // notifier so we don't get spurious activations on the closed FD.
    m_server->processEvents();
    if (m_server->clientSocket() == INVALID_SOCK && m_clientNotifier) {
        m_clientNotifier->setEnabled(false);
        m_clientNotifier->deleteLater();
        m_clientNotifier = nullptr;
    }
}

// ================================================================
// Throttled MDI repaint after action commands
// ================================================================
//
// Each action command (mi_addnode etc.) calls scheduleDelayedRepaint via
// the IPC post-command callback. The first command after a quiet period
// gets an immediate repaint (so one-shot interactive commands feel
// snappy), and starts a 100ms ticker. Subsequent commands within the
// same burst just set a dirty flag, and the ticker fires every 100ms
// to do a coalesced repaint that catches all dirtying since the last
// tick. When a tick finds the flag clean (no commands since the last
// tick), the ticker stops itself — zero idle CPU.
//
// For a motor-drawing script that fires ~2500 actions in ~0.5s, this
// reduces repaint frequency from "one per command" (~hundreds of paints)
// to "one immediate + ~5 throttled + 1 final" (~7 paints). Combined with
// the previous Sleep/QTimer fixes, this should bring GUI motor draw time
// from ~3.35s down close to the headless 0.49s baseline.

void MainWindow::scheduleDelayedRepaint()
{
    if (!m_repaintTicker->isActive()) {
        // First command after a quiet period — repaint immediately for
        // instant feedback, and start the ticker so subsequent commands
        // in the burst are throttled.
        doRepaintNow();
        m_repaintTicker->start();
    } else {
        // Within an active throttle window — just mark dirty. The next
        // ticker tick will catch it.
        m_repaintDirty = true;
    }
}

void MainWindow::onRepaintTick()
{
    if (m_repaintDirty) {
        doRepaintNow();
        m_repaintDirty = false;
    } else {
        // No commands since the last tick — burst is over, stop the
        // ticker so we don't burn CPU on empty 100ms wakeups.
        m_repaintTicker->stop();
    }
}

void MainWindow::doRepaintNow()
{
    QMdiArea *mdi = findChild<QMdiArea*>();
    if (!mdi) return;
    for (auto *sub : mdi->subWindowList()) {
        if (sub->widget()) sub->widget()->update();
    }
}

// ================================================================
// Tab management
// ================================================================

EditorWidget *MainWindow::addEditorTab(DocumentBase *doc, const QString &title)
{
    auto *editor = new EditorWidget(doc);
    connect(editor, &EditorWidget::coordinatesChanged, this, &MainWindow::updateCoordinates);
    connect(editor, &EditorWidget::statusMessage, this, &MainWindow::updateStatus);
    connect(editor, &EditorWidget::editActionChanged, this, &MainWindow::onEditActionChanged);
    connect(editor, &EditorWidget::contextMenuRequested, this, [this, editor](const QPoint &pos) {
        showEditorContextMenu(editor, pos);
    });
    connect(editor, &EditorWidget::sketchToolChanged, this, [this](int tool) {
        if (tool > 0 && tool < 11 && m_actSketch[tool]) m_actSketch[tool]->setChecked(true);
    });

    // Apply saved preferences
    QSettings settings;
    editor->setShowGrid(settings.value("editor/showGrid", true).toBool());
    editor->setSnapToGrid(settings.value("editor/snapToGrid", false).toBool());
    editor->setShowNames(settings.value("editor/showNames", true).toBool());
    editor->setObjectSnap(settings.value("editor/objectSnap", true).toBool());
    editor->setFillRegions(settings.value("editor/fillRegions", true).toBool());
    editor->setEnsureMaterial([this, editor](const QString &name) { return ensureMaterial(editor, name); });

    // Apply default problem settings for new documents
    if (doc->filePath.empty()) {
        doc->LengthUnits = settings.value("defaults/lengthUnits", 1).toInt();
        doc->ProblemType = settings.value("defaults/problemType", 0).toInt();
        doc->Precision = settings.value("defaults/precision", 1e-8).toDouble();
        doc->MinAngle = settings.value("defaults/minAngle", 30.0).toDouble();
        doc->Depth = settings.value("defaults/depth", 1.0).toDouble();
        // SmartMesh is per-document-type
        int sm = settings.value("defaults/smartMesh", true).toBool() ? 1 : 0;
        auto *mag = dynamic_cast<MagDocument*>(doc);
        auto *elec = dynamic_cast<ElecDocument*>(doc);
        auto *heat = dynamic_cast<HeatDocument*>(doc);
        auto *curr = dynamic_cast<CurrDocument*>(doc);
        if (mag) mag->SmartMesh = sm;
        if (elec) elec->SmartMesh = sm;
        if (heat) heat->SmartMesh = sm;
        if (curr) curr->SmartMesh = sm;
    }

    // Auto-set grid size based on model extent (matching original FEMM)
    double xmin, ymin, xmax, ymax;
    if (doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
        double w = std::max(xmax - xmin, ymax - ymin);
        if (w > 0)
            editor->setGridSize(pow(10.0, floor(log10(w) - 0.5)));
    }

    auto *sub = m_mdiArea->addSubWindow(editor);
    sub->setWindowTitle(title);
    // Set per-physics document icon on the tab
    {
        const char *icoNames[] = {"mi.ico", "ei.ico", "hi.ico", "ci.ico"};
        int pt = doc->physicsType();
        if (pt >= 0 && pt < 4)
            sub->setWindowIcon(QIcon(QString(":/icons/") + icoNames[pt]));
    }
    sub->showMaximized();

    // Start in the Select tool, as a CAD program does: a stray click selects
    // instead of dropping a node.
    editor->setSketchTool(EditorWidget::SketchSelect);

    // Sync menu/toolbar grid checkmarks to match loaded preferences.
    syncPreprocessorMenus(editor);

    editor->zoomFit();
    return editor;
}

// OF warns when a solved problem has regions claimed by more than one block
// label, and opens the view in block-label mode with names showing so the
// flagged regions are visible (FemmviewDoc.cpp / belaviewDoc.cpp + the
// FirstDraw handling in FemmviewView.cpp / belaviewView.cpp).
// Deferred by one event-loop turn. At the call site the view's paint events
// have only been POSTED, not processed, so showing the dialog inline could put
// it over a blank or half-drawn window. Returning to the event loop first
// flushes those paints, so the warning lands over the drawn view with the
// flagged regions already visible. The parent doubles as the context object,
// so the lambda is dropped if the window goes away first.
// Help > About. Not QMessageBox::about: on macOS that is drawn as a native
// alert, and the platform keeps alerts narrow, so a text this long wraps into
// a column taller than the screen and the bottom of it is simply not there.
// A plain dialog chooses its own width, wraps the text as prose, and scrolls
// if the screen is still too short. Sized from the screen it will appear on,
// so it fits a laptop as well as a big display.
static void showAbout(QWidget *parent, const QString &title, const QString &text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);

    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setContentsMargins(12, 8, 12, 8);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto *scroll = new QScrollArea;
    scroll->setWidget(label);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    auto *top = new QHBoxLayout;
    if (parent && !parent->windowIcon().isNull()) {
        auto *iconLabel = new QLabel;
        iconLabel->setPixmap(parent->windowIcon().pixmap(64, 64));
        iconLabel->setAlignment(Qt::AlignTop);
        iconLabel->setContentsMargins(12, 8, 0, 8);
        top->addWidget(iconLabel, 0, Qt::AlignTop);
    }
    top->addWidget(scroll, 1);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    const QScreen *screen = parent ? parent->screen() : nullptr;
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    dlg.resize(qMin(720, avail.width() - 80), qMin(600, avail.height() - 120));
    dlg.exec();
}

static void warnMultiplyDefinedLabels(QWidget *parent)
{
    QTimer::singleShot(0, parent, [parent]() {
        QMessageBox::warning(parent, QObject::tr("Multiply Defined Regions"),
            QObject::tr("Some regions in the problem have been defined\n"
                        "by more than one block label.  These potentially\n"
                        "problematic regions will appear as selected in\n"
                        "the initial view."));
    });
}

void MainWindow::openResultsFile(const QString &ansPath, void *scriptDoc)
{
    QString tabName = QFileInfo(ansPath).fileName();
    bool isScalar = ansPath.endsWith(".res", Qt::CaseInsensitive) ||
                    ansPath.endsWith(".anh", Qt::CaseInsensitive) ||
                    ansPath.endsWith(".anc", Qt::CaseInsensitive);

    // Check if a results tab for this file is already open — reload instead of duplicating
    for (auto *sub : m_mdiArea->subWindowList()) {
        if (isScalar) {
            auto *sw = qobject_cast<ScalarResultsWidget *>(sub->widget());
            if (sw) {
                // Check file path via the doc view
                std::string fp;
                if (sw->document()) fp = sw->document()->filePath;
                else if (sw->heatDocument()) fp = sw->heatDocument()->filePath;
                else if (sw->currDocument()) fp = sw->currDocument()->filePath;
                if (!fp.empty() && QFileInfo(QString::fromStdString(fp)).fileName() == tabName) {
                    if (scriptDoc) {
                        // Swap to fresh doc from script
                        if (sw->physicsType() == 1)
                            sw->setDocument(static_cast<ElecResultsDoc*>(scriptDoc));
                        else if (sw->physicsType() == 2)
                            sw->setHeatDocument(static_cast<HeatResultsDoc*>(scriptDoc));
                        else if (sw->physicsType() == 3)
                            sw->setCurrDocument(static_cast<CurrResultsDoc*>(scriptDoc));
                    } else {
                        if (sw->document()) sw->document()->OpenDocument(ansPath.toStdString());
                        else if (sw->heatDocument()) sw->heatDocument()->OpenDocument(ansPath.toStdString());
                        else if (sw->currDocument()) sw->currDocument()->OpenDocument(ansPath.toStdString());
                    }
                    m_mdiArea->setActiveSubWindow(sub);
                    sw->zoomFit();
                    sw->update();
                    return;
                }
            }
        } else {
            auto *rw = qobject_cast<ResultsWidget *>(sub->widget());
            if (rw && rw->document() &&
                QFileInfo(QString::fromStdString(rw->document()->filePath)).fileName() == tabName) {
                if (scriptDoc) {
                    // Swap to the fresh doc from Python (old doc was already deleted)
                    rw->setDocument(static_cast<ResultsDoc*>(scriptDoc));
                } else {
                    rw->document()->OpenDocument(ansPath.toStdString());
                }
                m_mdiArea->setActiveSubWindow(sub);
                rw->zoomFit();
                rw->update();
                return;
            }
        }
    }

    QSettings settings;

    if (isScalar) {
        bool isHeat = ansPath.endsWith(".anh", Qt::CaseInsensitive);
        bool isCurr = ansPath.endsWith(".anc", Qt::CaseInsensitive);
        ScalarResultsWidget *sw = nullptr;
        size_t nodeCount = 0, elemCount = 0;
        bool bMultiLabel = false;

        if (isHeat) {
            // Heat flow postprocessor
            HeatResultsDoc *doc;
            if (scriptDoc) {
                doc = static_cast<HeatResultsDoc*>(scriptDoc);
            } else {
                doc = new HeatResultsDoc();
                if (!doc->OpenDocument(ansPath.toStdString())) {
                    QMessageBox::warning(this, "Error", QString("Failed to open %1").arg(ansPath));
                    delete doc;
                    return;
                }
            }
            sw = new ScalarResultsWidget(doc);
            nodeCount = doc->meshnode.size();
            elemCount = doc->meshelem.size();
            bMultiLabel = ppMarkMultiplyDefinedLabels(doc);

            // Apply heat postprocessor preferences
            doc->Smooth = settings.value("ho/smooth", true).toBool();
            int hdt = settings.value("ho/densityType", 1).toInt();
            if (hdt < 0 || hdt > 3) hdt = 1;
            sw->m_densityPlotType = hdt;
            sw->setShowDensity(sw->m_densityPlotType > 0);
            sw->setShowContours(settings.value("ho/showContours", true).toBool());
            sw->setShowMesh(settings.value("ho/mesh", false).toBool());
            sw->setNumContours(settings.value("ho/numContours", 19).toInt());
            sw->m_showLegend = settings.value("ho/legend", true).toBool();
            sw->m_greyScale = settings.value("ho/greyscale", false).toBool();
            sw->m_showPoints = settings.value("ho/points", true).toBool();
            sw->m_showNames = settings.value("ho/names", false).toBool();
            sw->m_showMask = settings.value("ho/showMask", false).toBool();
            sw->setShowGrid(settings.value("ho/grid", false).toBool());
            sw->setSnapToGrid(settings.value("ho/snap", false).toBool());

            // Auto-set grid size
            if (!doc->meshnode.empty()) {
                double xmin = doc->meshnode[0].x, xmax = xmin;
                double ymin = doc->meshnode[0].y, ymax = ymin;
                for (auto &nd : doc->meshnode) {
                    if (nd.x < xmin) xmin = nd.x;
                    if (nd.x > xmax) xmax = nd.x;
                    if (nd.y < ymin) ymin = nd.y;
                    if (nd.y > ymax) ymax = nd.y;
                }
                double w = std::max(xmax - xmin, ymax - ymin);
                if (w > 0) sw->setGridSize(pow(10.0, floor(log10(w) - 0.5)));
            }
        } else if (isCurr) {
            // Current flow postprocessor
            CurrResultsDoc *doc;
            if (scriptDoc) {
                doc = static_cast<CurrResultsDoc*>(scriptDoc);
            } else {
                doc = new CurrResultsDoc();
                if (!doc->OpenDocument(ansPath.toStdString())) {
                    QMessageBox::warning(this, "Error", QString("Failed to open %1").arg(ansPath));
                    delete doc;
                    return;
                }
                register_curr_postproc_py(doc);
            }
            sw = new ScalarResultsWidget(doc);
            nodeCount = doc->meshnode.size();
            elemCount = doc->meshelem.size();
            bMultiLabel = ppMarkMultiplyDefinedLabels(doc);

            doc->Smooth = settings.value("co/smooth", true).toBool();
            // 0=off, 1..13 = |V| .. |Jd|, ScalarPrefsDialog's list and the
            // widget's own numbering. Not heat's 0..3: copying that clamp
            // here turned every choice past Im(V) back into |V|.
            int cdt = settings.value("co/densityType", 1).toInt();
            if (cdt < 0 || cdt > 13) cdt = 1;
            sw->m_densityPlotType = cdt;
            sw->setShowDensity(sw->m_densityPlotType > 0);
            sw->setShowContours(settings.value("co/showContours", true).toBool());
            sw->setShowMesh(settings.value("co/mesh", false).toBool());
            sw->setNumContours(settings.value("co/numContours", 19).toInt());
            sw->m_showLegend = settings.value("co/legend", true).toBool();
            sw->m_greyScale = settings.value("co/greyscale", false).toBool();
            sw->m_showPoints = settings.value("co/points", true).toBool();
            sw->m_showNames = settings.value("co/names", false).toBool();
            sw->m_showMask = settings.value("co/showMask", false).toBool();
            sw->setShowGrid(settings.value("co/grid", false).toBool());
            sw->setSnapToGrid(settings.value("co/snap", false).toBool());

            // Auto-set grid size
            if (!doc->meshnode.empty()) {
                double xmin = doc->meshnode[0].x, xmax = xmin;
                double ymin = doc->meshnode[0].y, ymax = ymin;
                for (auto &nd : doc->meshnode) {
                    if (nd.x < xmin) xmin = nd.x;
                    if (nd.x > xmax) xmax = nd.x;
                    if (nd.y < ymin) ymin = nd.y;
                    if (nd.y > ymax) ymax = nd.y;
                }
                double w = std::max(xmax - xmin, ymax - ymin);
                if (w > 0) sw->setGridSize(pow(10.0, floor(log10(w) - 0.5)));
            }
        } else {
            // Electrostatics postprocessor
            ElecResultsDoc *doc;
            if (scriptDoc) {
                doc = static_cast<ElecResultsDoc*>(scriptDoc);
            } else {
                doc = new ElecResultsDoc();
                if (!doc->OpenDocument(ansPath.toStdString())) {
                    QMessageBox::warning(this, "Error", QString("Failed to open %1").arg(ansPath));
                    delete doc;
                    return;
                }
            }
            sw = new ScalarResultsWidget(doc);
            nodeCount = doc->meshnode.size();
            elemCount = doc->meshelem.size();
            bMultiLabel = ppMarkMultiplyDefinedLabels(doc);

            doc->Smooth = settings.value("eo/smooth", true).toBool();

            // Auto-set grid size
            if (!doc->meshnode.empty()) {
                double xmin = doc->meshnode[0].x, xmax = xmin;
                double ymin = doc->meshnode[0].y, ymax = ymin;
                for (auto &nd : doc->meshnode) {
                    if (nd.x < xmin) xmin = nd.x;
                    if (nd.x > xmax) xmax = nd.x;
                    if (nd.y < ymin) ymin = nd.y;
                    if (nd.y > ymax) ymax = nd.y;
                }
                double w = std::max(xmax - xmin, ymax - ymin);
                if (w > 0) sw->setGridSize(pow(10.0, floor(log10(w) - 0.5)));
            }
        }

        {
            QString pfx = (sw->physicsType() == 2) ? "ho" : (sw->physicsType() == 3) ? "co" : "eo";
            int sMode = settings.value(pfx + "/editAction", 0).toInt();
            sw->setMode(sMode);
            if (sMode == 1) m_ppActContour->setChecked(true);
            else if (sMode == 2) m_ppActArea->setChecked(true);
            else m_ppActPoint->setChecked(true);
        }
        connect(sw, &ScalarResultsWidget::coordinatesChanged, this, &MainWindow::updateCoordinates);
        connect(sw, &ScalarResultsWidget::statusMessage, this, &MainWindow::updateStatus);
        connect(sw, &ScalarResultsWidget::outputReady, [this](const QString &text) {
            outputDialog()->setOutput(text);
        });

        if (!isHeat && !isCurr) {
            // Apply electrostatics preferences (heat/current prefs already applied above)
            sw->setShowDensity(settings.value("eo/densityType", 3).toInt() > 0);
            sw->m_densityPlotType = settings.value("eo/densityType", 3).toInt();
            sw->setShowContours(settings.value("eo/showContours", true).toBool());
            sw->setShowMesh(settings.value("eo/mesh", false).toBool());
            sw->setNumContours(settings.value("eo/numContours", 19).toInt());
            sw->m_showLegend = settings.value("eo/legend", true).toBool();
            sw->m_greyScale = settings.value("eo/greyscale", false).toBool();
            sw->m_showPoints = settings.value("eo/points", true).toBool();
            sw->m_showNames = settings.value("eo/names", false).toBool();
            sw->m_showMask = settings.value("eo/showMask", false).toBool();
            sw->setShowGrid(settings.value("eo/grid", false).toBool());
            sw->setSnapToGrid(settings.value("eo/snap", false).toBool());
        }

        // Default vector plot. After every physics' density preference, since
        // a vector plot turns density off as the Vector Plot dialog does (the
        // legend shows one or the other). Copying the setting straight into
        // m_vectorType never set m_showVectors, so vectors never came on, and
        // was off by one: the setting counts Off as 0.
        {
            QString pfx = (sw->physicsType() == 2) ? "ho" : (sw->physicsType() == 3) ? "co" : "eo";
            int vt = settings.value(pfx + "/vectorType", 0).toInt();
            if (vt > 0) {
                sw->m_showVectors = true;
                sw->m_vectorType = vectorFieldOf(vt);
                sw->setShowDensity(false);
            }
        }

        // Multiply-defined regions override the stored view preferences, so the
        // labels just flagged are actually visible. Set before the menu sync.
        if (bMultiLabel) {
            sw->setMode(2);
            sw->m_showNames = true;
        }

        auto *sub = m_mdiArea->addSubWindow(sw);
        sub->setWindowTitle(tabName);
        sub->setWindowIcon(QIcon(QString(":/icons/") + (isHeat ? "ho.ico" : isCurr ? "co.ico" : "eo.ico")));
        sub->showMaximized();

        // Sync toolbar button states to match loaded preferences.
        syncPostprocessorMenus(sw);
        sw->zoomFit();

        updateStatus(QString("Loaded %1: %2 nodes, %3 elements")
            .arg(tabName)
            .arg(nodeCount).arg(elemCount));
        if (bMultiLabel) warnMultiplyDefinedLabels(this);
        addToRecentFiles(ansPath);
    } else {
        // Vector potential postprocessor (magnetics)
        ResultsDoc *doc;
        if (scriptDoc) {
            // Use the shared doc from script — already loaded
            doc = static_cast<ResultsDoc*>(scriptDoc);
        } else {
            // User opened .ans directly — create our own
            doc = new ResultsDoc();
            if (!doc->OpenDocument(ansPath.toStdString())) {
                QMessageBox::warning(this, "Error", QString("Failed to open %1").arg(ansPath));
                delete doc;
                return;
            }
        }
        auto *rw = new ResultsWidget(doc);
        bool bMultiLabel = ppMarkMultiplyDefinedLabels(doc);
        int ppMode = settings.value("pp/editAction", 0).toInt();
        if (bMultiLabel) ppMode = 2;   // block-label mode, so the flagged labels show
        rw->setMode(ppMode);
        if (ppMode == 1) m_ppActContour->setChecked(true);
        else if (ppMode == 2) m_ppActArea->setChecked(true);
        else m_ppActPoint->setChecked(true);
        connect(rw, &ResultsWidget::coordinatesChanged, this, &MainWindow::updateCoordinates);
        connect(rw, &ResultsWidget::statusMessage, this, &MainWindow::updateStatus);
        connect(rw, &ResultsWidget::outputReady, [this](const QString &text) {
            outputDialog()->setOutput(text);
        });

        doc->Smooth = settings.value("pp/smooth", true).toBool();
        doc->ShiftH = settings.value("pp/shiftH", true).toBool();
        int ppDensType = settings.value("pp/densityType", 1).toInt();
        rw->m_densityPlotType = ppDensType;
        rw->setShowDensity(ppDensType > 0);
        rw->setShowContours(settings.value("pp/showAr", true).toBool());
        rw->m_showAr = settings.value("pp/showAr", true).toBool();
        rw->m_showAi = settings.value("pp/showAi", false).toBool();
        rw->setShowMesh(settings.value("pp/mesh", false).toBool());
        rw->setNumContours(settings.value("pp/numContours", 19).toInt());
        rw->m_showLegend = settings.value("pp/legend", true).toBool();
        rw->m_greyScale = settings.value("pp/greyscale", false).toBool();
        rw->m_showPoints = settings.value("pp/points", true).toBool();
        rw->m_showNames = settings.value("pp/names", false).toBool();
        if (bMultiLabel) rw->m_showNames = true;
        rw->m_showMask = settings.value("pp/showMask", false).toBool();
        // Default vector plot: see the scalar postprocessors above.
        if (int vt = settings.value("pp/vectorType", 0).toInt(); vt > 0) {
            rw->m_showVectors = true;
            rw->m_vectorType = vectorFieldOf(vt);
            rw->setShowDensity(false);
        }
        rw->setShowGrid(settings.value("pp/grid", false).toBool());
        rw->setSnapToGrid(settings.value("pp/snap", false).toBool());

        if (!doc->meshnode.empty()) {
            double xmin = doc->meshnode[0].x, xmax = xmin;
            double ymin = doc->meshnode[0].y, ymax = ymin;
            for (auto &nd : doc->meshnode) {
                if (nd.x < xmin) xmin = nd.x;
                if (nd.x > xmax) xmax = nd.x;
                if (nd.y < ymin) ymin = nd.y;
                if (nd.y > ymax) ymax = nd.y;
            }
            double w = std::max(xmax - xmin, ymax - ymin);
            if (w > 0) rw->setGridSize(pow(10.0, floor(log10(w) - 0.5)));
        }

        auto *sub = m_mdiArea->addSubWindow(rw);
        sub->setWindowTitle(tabName);
        sub->setWindowIcon(QIcon(":/icons/mo.ico"));
        sub->showMaximized();

        // Sync toolbar button states to match loaded preferences.
        syncPostprocessorMenus(rw);
        rw->zoomFit();

        updateStatus(QString("Loaded %1: %2 nodes, %3 elements")
            .arg(tabName)
            .arg(doc->meshnode.size()).arg(doc->meshelem.size()));
        if (bMultiLabel) warnMultiplyDefinedLabels(this);
        addToRecentFiles(ansPath);
    }
}

EditorWidget *MainWindow::activeEditorWidget() const { return activeEditor(); }

EditorWidget *MainWindow::activeEditor() const
{
    auto *sub = m_mdiArea->activeSubWindow();
    if (!sub) sub = m_mdiArea->currentSubWindow();
    return sub ? qobject_cast<EditorWidget *>(sub->widget()) : nullptr;
}

ResultsWidget *MainWindow::activeResults() const
{
    auto *sub = m_mdiArea->activeSubWindow();
    if (!sub) sub = m_mdiArea->currentSubWindow();
    return sub ? qobject_cast<ResultsWidget *>(sub->widget()) : nullptr;
}

ScalarResultsWidget *MainWindow::activeScalarResults() const
{
    auto *sub = m_mdiArea->activeSubWindow();
    if (!sub) sub = m_mdiArea->currentSubWindow();
    return sub ? qobject_cast<ScalarResultsWidget *>(sub->widget()) : nullptr;
}

// Smoothing is the results document's own flag -- the one mo_smooth and its
// per-physics siblings set -- so toggling it needs no recomputation, only a
// repaint. The scalar widget holds one of three document types and exactly one
// of them is non-null.
void MainWindow::setPostprocessorSmoothing(bool on)
{
    if (auto *r = activeResults()) {
        if (auto *d = r->document()) d->Smooth = on;
        r->update();
        return;
    }
    if (auto *s = activeScalarResults()) {
        if (auto *d = s->document())      d->Smooth = on;
        if (auto *d = s->heatDocument())  d->Smooth = on;
        if (auto *d = s->currDocument())  d->Smooth = on;
        s->update();
    }
}

bool MainWindow::postprocessorSmoothing() const
{
    if (auto *r = activeResults())
        return r->document() ? r->document()->Smooth : true;
    if (auto *s = activeScalarResults()) {
        if (auto *d = s->document())      return d->Smooth;
        if (auto *d = s->heatDocument())  return d->Smooth;
        if (auto *d = s->currDocument())  return d->Smooth;
    }
    return true;
}

// ================================================================
// UI switching
// ================================================================

void MainWindow::onSubWindowActivated(QMdiSubWindow *sub)
{
    if (!sub) {
        // Qt emits subWindowActivated(nullptr) when focus moves to a
        // toolbar or menu — not just when all tabs are closed.  Only
        // tear down the UI and clear script pointers when there are truly
        // no subwindows left.
        if (m_mdiArea->subWindowList().isEmpty()) {
            if (m_uiState != -1) showEmptyUI();
            register_mag_editor_py(nullptr);
            register_elec_editor_py(nullptr);
            register_heat_editor_py(nullptr);
            register_curr_editor_py(nullptr);
            register_mag_postproc_py(nullptr);
            register_elec_postproc_py(nullptr);
            register_heat_postproc_py(nullptr);
            register_curr_postproc_py(nullptr);
        }
        return;
    }
    bool isPost = (qobject_cast<ResultsWidget *>(sub->widget()) != nullptr)
              || (qobject_cast<ScalarResultsWidget *>(sub->widget()) != nullptr);
    if (isPost && m_uiState != 1)
        showPostprocessorUI();
    else if (!isPost && m_uiState != 0)
        showPreprocessorUI();

    // Update the Python bindings' document pointer to match the newly-active
    // tab. Without this, documents opened manually via the GUI can't be
    // queried from scripting bindings (mathfemm, octavefemm, pyfemm, etc.)
    // because the Python layer's s_doc / s_rdoc would still point at whatever
    // was last opened via a script command. The register_*_py() functions
    // set the static document pointer AND (harmlessly) re-register the
    // same Python function pointers, so calling them on every tab switch is
    // safe and cheap.
    if (auto *ew = qobject_cast<EditorWidget*>(sub->widget())) {
        auto *doc = ew->document();
        if (auto *d = dynamic_cast<MagDocument*>(doc))
            register_mag_editor_py(d);
        else if (auto *d = dynamic_cast<ElecDocument*>(doc))
            register_elec_editor_py(d);
        else if (auto *d = dynamic_cast<HeatDocument*>(doc))
            register_heat_editor_py(d);
        else if (auto *d = dynamic_cast<CurrDocument*>(doc))
            register_curr_editor_py(d);
        if (m_actExteriorRegion)
            m_actExteriorRegion->setEnabled(doc && doc->ProblemType == 1);
        if (m_actCircuits)
            m_actCircuits->setText(doc && doc->physicsType() == 0
                                       ? tr("&Circuits...") : tr("&Conductors..."));
        // Refresh Grid-menu / toolbar checkmarks for the newly-active tab.
        syncPreprocessorMenus(ew);
    }
    else if (auto *rw = qobject_cast<ResultsWidget*>(sub->widget())) {
        if (rw->document())
            register_mag_postproc_py(rw->document());
        // Refresh View-menu checkmarks for the newly-active tab.
        syncPostprocessorMenus(rw);
    }
    else if (auto *sw = qobject_cast<ScalarResultsWidget*>(sub->widget())) {
        if (sw->document())
            register_elec_postproc_py(sw->document());
        else if (sw->heatDocument())
            register_heat_postproc_py(sw->heatDocument());
        else if (sw->currDocument())
            register_curr_postproc_py(sw->currDocument());
        // Refresh View-menu checkmarks for the newly-active tab.
        syncPostprocessorMenus(sw);
    }
}

void MainWindow::showEmptyUI()
{
    m_uiState = -1;
    if (m_centralStack) { m_welcome->refresh(); m_centralStack->setCurrentWidget(m_welcome); }
    if (m_modelPanel) m_modelPanel->hide();
    if (m_resultsPanel) m_resultsPanel->hide();
    if (m_plotDock) m_plotDock->hide();
    // Hide document-specific UI but keep File toolbar (New/Open)
    m_preTopBar1->show();
    m_preTopBar2->hide();
    m_ppTopBar->hide();
    m_leftNav->hide();
    m_preMenuProblem->menuAction()->setVisible(false);
    m_preMenuView->menuAction()->setVisible(false);
    m_preMenuGrid->menuAction()->setVisible(false);
    m_preMenuOperation->menuAction()->setVisible(false);
    m_preMenuProperties->menuAction()->setVisible(false);
    m_preMenuMesh->menuAction()->setVisible(false);
    m_preMenuAnalysis->menuAction()->setVisible(false);
    m_ppMenuView->menuAction()->setVisible(false);
    m_ppMenuOperation->menuAction()->setVisible(false);
    if (m_editMenu) m_editMenu->menuAction()->setVisible(false);
    if (m_windowMenu) m_windowMenu->menuAction()->setVisible(false);
    if (m_winButtons) m_winButtons->hide();
    m_actSave->setVisible(false);
    m_actSaveAs->setVisible(false);
    m_dxfSep->setVisible(false);
    m_importDxf->setVisible(false);
    m_exportDxf->setVisible(false);
}

void MainWindow::showPreprocessorUI()
{
    m_uiState = 0;
    if (m_centralStack) m_centralStack->setCurrentWidget(m_mdiArea);
    if (m_modelPanel) m_modelPanel->setVisible(QSettings().value("ui/modelPanel", true).toBool());
    if (m_resultsPanel) m_resultsPanel->hide();
    if (m_plotDock) m_plotDock->hide();
    m_actSave->setVisible(true);
    m_actSaveAs->setVisible(true);
    m_preTopBar1->show();
    m_preTopBar2->show();
    m_leftNav->show();
    if (m_editMenu) m_editMenu->menuAction()->setVisible(true);
    if (m_actUndo) m_actUndo->setVisible(true);
    if (m_actRedo) m_actRedo->setVisible(true);
    if (m_actDeleteSelected) m_actDeleteSelected->setVisible(true);
    if (m_windowMenu) m_windowMenu->menuAction()->setVisible(true);
    m_preMenuProblem->menuAction()->setVisible(true);
    m_preMenuView->menuAction()->setVisible(true);
    m_preMenuGrid->menuAction()->setVisible(true);
    m_preMenuOperation->menuAction()->setVisible(true);
    m_preMenuProperties->menuAction()->setVisible(true);
    m_preMenuMesh->menuAction()->setVisible(true);
    m_preMenuAnalysis->menuAction()->setVisible(true);
    m_ppTopBar->hide();
    m_ppMenuView->menuAction()->setVisible(false);
    m_ppMenuOperation->menuAction()->setVisible(false);
    if (m_winButtons) m_winButtons->show();
    m_dxfSep->setVisible(true);
    m_importDxf->setVisible(true);
    m_exportDxf->setVisible(true);
}

void MainWindow::showPostprocessorUI()
{
    m_uiState = 1;
    if (m_centralStack) m_centralStack->setCurrentWidget(m_mdiArea);
    if (m_modelPanel) m_modelPanel->hide();
    m_actSave->setVisible(false);
    m_actSaveAs->setVisible(false);
    m_preTopBar1->show();  // New/Open buttons visible in postprocessor too (matching OF)
    m_preTopBar2->hide();
    m_leftNav->show();
    if (m_editMenu) m_editMenu->menuAction()->setVisible(true);
    if (m_actUndo) m_actUndo->setVisible(false);
    if (m_actRedo) m_actRedo->setVisible(false);
    if (m_actDeleteSelected) m_actDeleteSelected->setVisible(false);
    if (m_windowMenu) m_windowMenu->menuAction()->setVisible(true);
    m_preMenuProblem->menuAction()->setVisible(false);
    m_preMenuView->menuAction()->setVisible(false);
    m_preMenuGrid->menuAction()->setVisible(false);
    m_preMenuOperation->menuAction()->setVisible(false);
    m_preMenuProperties->menuAction()->setVisible(false);
    m_preMenuMesh->menuAction()->setVisible(false);
    m_preMenuAnalysis->menuAction()->setVisible(false);
    m_ppTopBar->show();
    m_ppMenuView->menuAction()->setVisible(true);
    m_ppMenuOperation->menuAction()->setVisible(true);
    if (m_winButtons) m_winButtons->show();
    m_dxfSep->setVisible(false);
    m_importDxf->setVisible(false);
    m_exportDxf->setVisible(false);
}

void MainWindow::syncPostprocessorMenus(QWidget *active)
{
    bool density, contours, mesh, grid, snap, points, names;
    int mode = 0;
    if (auto *rw = qobject_cast<ResultsWidget *>(active)) {
        density  = rw->showDensity();
        contours = rw->showContours();
        mesh     = rw->showMesh();
        grid     = rw->showGrid();
        snap     = rw->snapToGrid();
        points   = rw->m_showPoints;
        names    = rw->m_showNames;
        mode     = rw->mode();
    } else if (auto *sw = qobject_cast<ScalarResultsWidget *>(active)) {
        density  = sw->showDensity();
        contours = sw->showContours();
        mesh     = sw->showMesh();
        grid     = sw->showGrid();
        snap     = sw->snapToGrid();
        points   = sw->m_showPoints;
        names    = sw->m_showNames;
        mode     = sw->mode();
    } else {
        return;  // not a postprocessor widget
    }

    // Block signals so setChecked() doesn't re-trigger the toggle handlers
    // and re-apply state back onto the (already-correct) widget.
    QAction *acts[] = { m_ppActDensity, m_ppActContours, m_ppActMesh,
                        m_ppMeshTB, m_showGridTB, m_snapGridTB,
                        m_ppActPoints, m_ppActNames, m_ppActSmooth };
    for (QAction *a : acts) a->blockSignals(true);
    m_ppActDensity->setChecked(density);
    m_ppActContours->setChecked(contours);
    m_ppActMesh->setChecked(mesh);
    m_ppMeshTB->setChecked(mesh);
    m_showGridTB->setChecked(grid);
    m_snapGridTB->setChecked(snap);
    m_ppActPoints->setChecked(points);
    m_ppActNames->setChecked(names);
    m_ppActSmooth->setChecked(postprocessorSmoothing());
    for (QAction *a : acts) a->blockSignals(false);

    // Re-sync the postprocessor mode (Point/Contour/Area) toolbar/menu to this
    // window — same fix as the preprocessor mode: it was only checked reactively
    // on a mode-button click, so switching windows left it stale. m_ppModeGroup
    // is exclusive; its actions are ordered 0=Point, 1=Contour, 2=Area to match
    // ResultsWidget::mode(), and they use triggered (not toggled), so setChecked
    // emits nothing.
    auto modeActs = m_ppModeGroup->actions();
    if (mode >= 0 && mode < modeActs.size())
        modeActs[mode]->setChecked(true);

    // "Circuit" only in magnetics; the scalar physics call the same thing a
    // conductor, which is what their dialog title and the rest of their UI say.
    const bool isMag = (qobject_cast<ResultsWidget *>(active) != nullptr);
    if (m_ppActCircProps)
        m_ppActCircProps->setText(isMag ? tr("Circuit &Properties...")
                                        : tr("Conductor &Properties..."));
    if (m_ppCircPropsTB) {
        m_ppCircPropsTB->setText(isMag ? tr("Circuit Props") : tr("Conductor Props"));
        m_ppCircPropsTB->setToolTip(isMag ? tr("View circuit properties")
                                          : tr("View conductor properties"));
    }
}

void MainWindow::syncPreprocessorMenus(EditorWidget *editor)
{
    if (!editor) return;
    bool grid = editor->showGrid();
    bool snap = editor->snapToGrid();
    bool names = editor->showNames();

    QAction *acts[] = { m_showGridMenu, m_showGridTB,
                        m_snapGridMenu, m_snapGridTB,
                        m_showNamesMenu };
    for (QAction *a : acts) if (a) a->blockSignals(true);
    m_showGridMenu->setChecked(grid);
    m_showGridTB->setChecked(grid);
    m_snapGridMenu->setChecked(snap);
    m_snapGridTB->setChecked(snap);
    if (m_showNamesMenu) m_showNamesMenu->setChecked(names);
    for (QAction *a : acts) if (a) a->blockSignals(false);

    // Re-sync the edit-mode (Node/Segment/Arc/Block/Group) toolbar buttons and
    // Operation-menu checkmarks to this editor's mode. Each window keeps its own
    // mode, so switching windows must reflect the newly-active one. (The mode
    // actions otherwise only update via editActionChanged, which fires when the
    // mode changes, not on a window switch.) m_modeGroup is exclusive, so
    // checking one unchecks the rest; they use triggered (not toggled), so
    // setChecked emits nothing.
    QAction *modeAct = nullptr;
    switch (editor->editAction()) {
        case 0: modeAct = m_actNode;    break;
        case 1: modeAct = m_actSegment; break;
        case 2: modeAct = m_actBlock;   break;
        case 3: modeAct = m_actArc;     break;
        case 4: modeAct = m_actGroup;   break;
    }
    if (editor->sketchTool() > 0 && editor->sketchTool() < 11)
        modeAct = m_actSketch[editor->sketchTool()];
    if (m_materialPicker) {
        m_materialPicker->setDocument(editor->document());
        m_materialPicker->setEditText(editor->fillMaterial());
    }
    if (modeAct) modeAct->setChecked(true);
    if (m_objectSnapMenu) {
        m_objectSnapMenu->blockSignals(true);
        m_objectSnapMenu->setChecked(editor->objectSnap());
        m_objectSnapMenu->blockSignals(false);
    }
}

// ================================================================
// Preprocessor slots
// ================================================================

void MainWindow::onNew()
{
    createNewProblem(QSettings().value("defaults/physics", 0).toInt(), true);
}

void MainWindow::createNewProblem(int physics, bool askSettings)
{
    NewProblemSettings ns;
    ns.physics = physics;
    if (askSettings) {
        NewProblemDialog dlg(physics, this);
        if (dlg.exec() != QDialog::Accepted) return;
        ns = dlg.settings();
        QSettings st;
        st.setValue("defaults/physics", ns.physics);
        st.setValue("defaults/lengthUnits", ns.lengthUnits);
        st.setValue("defaults/problemType", ns.problemType);
    }
    const int doctype = ns.physics;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "newdocument(%d)", doctype);
    char errmsg[256] = {};
    exec_python(cmd, errmsg, sizeof(errmsg));

    auto *e = activeEditor();
    if (!e || !e->document()) return;
    DocumentBase *doc = e->document();
    doc->LengthUnits = ns.lengthUnits;
    doc->ProblemType = ns.problemType;
    if (ns.problemType == 0) doc->Depth = ns.depth;
    if (auto *mag = dynamic_cast<MagDocument *>(doc)) mag->Frequency = ns.frequency;
    if (auto *curr = dynamic_cast<CurrDocument *>(doc)) if (askSettings) curr->Frequency = ns.frequency;

    // Start with the surrounding medium already in the model -- nearly every
    // problem needs it, and an empty material list is the first stumbling
    // block. (Only from File > New: scripts get the empty document they ask for.)
    static const char *air[] = {"Air", "Air (dry, CO2-free)", "Air", nullptr};
    if (doctype >= 0 && doctype < 4 && air[doctype]) {
        ensureMaterial(e, QString::fromUtf8(air[doctype]));
        e->setFillMaterial(QString::fromUtf8(air[doctype]));
    }
    e->markSaved();
    if (!QSettings().value("ui/gettingStartedShown", false).toBool()) {
        QSettings().setValue("ui/gettingStartedShown", true);
        m_gettingStarted->show();
    }
    e->setSketchTool(EditorWidget::SketchRect);
    e->setFocus();
    syncPreprocessorMenus(e);
}

bool MainWindow::ensureMaterial(EditorWidget *editor, const QString &name)
{
    DocumentBase *doc = editor ? editor->document() : nullptr;
    if (!doc || name.isEmpty()) return false;
    if (name == "<No Mesh>") return true;
    auto has = [&]() {
        for (int i = 0; i < doc->numMaterials(); i++)
            if (QString::fromStdString(doc->materialName(i)) == name) return true;
        return false;
    };
    if (has()) return true;
    // The script layer acts on the active document; make sure that is this one.
    for (auto *sub : m_mdiArea->subWindowList())
        if (sub->widget() == editor) { m_mdiArea->setActiveSubWindow(sub); break; }
    static const char *prefix[] = {"mi", "ei", "hi", "ci"};
    const int pt = doc->physicsType();
    if (pt < 0 || pt > 3) return false;
    QString lit = name;
    lit.replace("\\", "\\\\").replace("'", "\\'");
    const QByteArray cmd = QString("%1_getmaterial('%2')").arg(prefix[pt], lit).toUtf8();
    char errmsg[512] = {};
    exec_python(cmd.constData(), errmsg, sizeof(errmsg));
    if (m_materialPicker) m_materialPicker->setDocument(doc);
    return has();
}

void MainWindow::onOpen()
{
    QSettings settings;
    QString lastDir = settings.value("lastDirectory", QString()).toString();
    QString path = QFileDialog::getOpenFileName(this, "Open FEMM File", lastDir,
        "FEMM Files (*.fem *.fee *.feh *.fec);;Solution Files (*.ans *.res *.anh *.anc);;All Files (*)");
    if (path.isEmpty()) return;
    settings.setValue("lastDirectory", QFileInfo(path).absolutePath());
    openPath(path);
}

void MainWindow::openPath(const QString &path)
{
    if (path.endsWith(".ans", Qt::CaseInsensitive) || path.endsWith(".res", Qt::CaseInsensitive) ||
        path.endsWith(".anh", Qt::CaseInsensitive) || path.endsWith(".anc", Qt::CaseInsensitive)) {
        openResultsFile(path);
    } else {
        QString scriptPath = path;
        scriptPath.replace('\\', '/');
        std::string cmd = "open(\"" + scriptPath.toStdString() + "\")";
        char errmsg[1024] = {};
        if (exec_python(cmd.c_str(), errmsg, sizeof(errmsg)) != 0)
            QMessageBox::warning(this, "Error", QString("Failed to open %1:\n%2").arg(path, errmsg));
    }
    addToRecentFiles(path);
}

// The files a drop or a Finder open may hand us: the four problem documents
// and their solutions. Not .py -- running a script because it was dropped on
// the window, or double-clicked in Finder, is not something to do implicitly.
static bool femmOpenable(const QString &path)
{
    static const char *ext[] = {"fem", "fee", "feh", "fec", "ans", "res", "anh", "anc"};
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const char *e : ext)
        if (suffix == QLatin1String(e)) return true;
    return false;
}

static QStringList femmDroppedFiles(const QMimeData *mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl &u : mime->urls())
        if (u.isLocalFile() && femmOpenable(u.toLocalFile()))
            out << u.toLocalFile();
    return out;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!py_is_running() && !femmDroppedFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList files = femmDroppedFiles(event->mimeData());
    if (files.isEmpty() || py_is_running()) { event->ignore(); return; }
    event->acceptProposedAction();
    // Not inside the drop handler: opening runs the interpreter and can raise
    // a modal error box, and on macOS the source (Finder) waits for the drop
    // to complete before it will let go of the drag session.
    QTimer::singleShot(0, this, [this, files]() {
        for (const QString &f : files) openPath(f);
    });
}

void MainWindow::onSave()
{
    auto *editor = activeEditor();
    if (!editor || !editor->document()) return;
    auto *doc = editor->document();
    if (doc->filePath.empty()) { onSaveAs(); return; }
    if (!doc->SaveDocument(doc->filePath))
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Could not save to %1").arg(QString::fromStdString(doc->filePath)));
    else { editor->markSaved(); removeAutosave(editor); }
    auto *sub = m_mdiArea->activeSubWindow();
    if (sub) sub->setWindowTitle(QFileInfo(QString::fromStdString(doc->filePath)).fileName());
}

void MainWindow::onSaveAs()
{
    auto *editor = activeEditor();
    if (!editor || !editor->document()) return;
    QSettings settings;
    QString lastDir = settings.value("lastDirectory", QString()).toString();
    // Use appropriate file filter based on document type
    QString filter = "FEMM Files (*.fem);;All Files (*)";
    if (editor->document()->physicsType() == 1)
        filter = "Electrostatics Files (*.fee);;All Files (*)";
    else if (editor->document()->physicsType() == 2)
        filter = "Heat Flow Files (*.feh);;All Files (*)";
    else if (editor->document()->physicsType() == 3)
        filter = "Current Flow Files (*.fec);;All Files (*)";
    // Pre-populate with current filename if the document has been saved before
    QString suggested = lastDir;
    if (!editor->document()->filePath.empty())
        suggested = QString::fromStdString(editor->document()->filePath);
    QString path = QFileDialog::getSaveFileName(this, "Save FEMM File", suggested, filter);
    if (path.isEmpty()) return;
    // Normalize the extension to lowercase (matching open() and the script
    // saveas) so the stored path stays canonical and solver/results naming is
    // consistent across platforms.
    {
        int dot = path.lastIndexOf('.');
        int slash = qMax(path.lastIndexOf('/'), path.lastIndexOf('\\'));
        if (dot > slash) path = path.left(dot + 1) + path.mid(dot + 1).toLower();
    }
    settings.setValue("lastDirectory", QFileInfo(path).absolutePath());
    auto *doc = editor->document();
    doc->filePath = path.toStdString();
    if (!doc->SaveDocument(doc->filePath)) {
        QMessageBox::warning(this, tr("Save Failed"),
            tr("Could not save to %1").arg(path));
        return;
    }
    editor->markSaved();
    removeAutosave(editor);
    // Find the subwindow containing this editor (don't rely on activeSubWindow
    // which may be stale after the file dialog)
    for (auto *sub : m_mdiArea->subWindowList())
        if (sub->widget() == editor) { sub->setWindowTitle(QFileInfo(path).fileName()); break; }
    addToRecentFiles(path);
}

void MainWindow::addToRecentFiles(const QString &path)
{
    QSettings settings;
    QStringList recent = settings.value("recentFiles").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > MaxRecentFiles) recent.removeLast();
    settings.setValue("recentFiles", recent);
    updateRecentFilesMenu();
}

void MainWindow::removeFromRecentFiles(const QString &path)
{
    QSettings settings;
    QStringList recent = settings.value("recentFiles").toStringList();
    recent.removeAll(path);
    settings.setValue("recentFiles", recent);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();
    QSettings settings;
    QStringList recent = settings.value("recentFiles").toStringList();
    if (recent.isEmpty()) {
        m_recentFilesMenu->addAction(tr("(none)"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < recent.size(); i++) {
        QString display = QString("&%1  %2").arg(i + 1).arg(QFileInfo(recent[i]).fileName());
        QString fullPath = recent[i];
        m_recentFilesMenu->addAction(display, [this, fullPath]() {
            if (fullPath.endsWith(".ans", Qt::CaseInsensitive) ||
                fullPath.endsWith(".res", Qt::CaseInsensitive) ||
                fullPath.endsWith(".anh", Qt::CaseInsensitive) ||
                fullPath.endsWith(".anc", Qt::CaseInsensitive)) {
                openResultsFile(fullPath);
                addToRecentFiles(fullPath);
            } else {
                QString scriptPath = fullPath;
                scriptPath.replace('\\', '/');
                std::string cmd = "open(\"" + scriptPath.toStdString() + "\")";
                char errmsg[1024] = {};
                if (exec_python(cmd.c_str(), errmsg, sizeof(errmsg)) != 0) {
                    QMessageBox::warning(this, tr("Open Failed"),
                        tr("Could not open %1").arg(fullPath));
                    removeFromRecentFiles(fullPath);
                } else {
                    addToRecentFiles(fullPath);
                }
            }
        });
    }
}

void MainWindow::onNodeMode() { auto *e = activeEditor(); if (e) e->setEditAction(0); }
void MainWindow::onSegmentMode() { auto *e = activeEditor(); if (e) e->setEditAction(1); }
void MainWindow::onArcMode() { auto *e = activeEditor(); if (e) e->setEditAction(3); }
void MainWindow::onBlockMode() { auto *e = activeEditor(); if (e) e->setEditAction(2); }
void MainWindow::onGroupMode() { auto *e = activeEditor(); if (e) e->setEditAction(4); }

void MainWindow::onEditActionChanged(int action)
{
    switch (action) {
    case 0: m_actNode->setChecked(true); break;
    case 1: m_actSegment->setChecked(true); break;
    case 2: m_actBlock->setChecked(true); break;
    case 3: m_actArc->setChecked(true); break;
    case 4: m_actGroup->setChecked(true); break;
    }
}

void MainWindow::onZoomIn() { auto *e = activeEditor(); if (e) e->zoomIn(); }
void MainWindow::onZoomOut() { auto *e = activeEditor(); if (e) e->zoomOut(); }
void MainWindow::onZoomFit() { auto *e = activeEditor(); if (e) e->zoomFit(); }
void MainWindow::onZoomWindow() { auto *e = activeEditor(); if (e) e->zoomWindow(); }

void MainWindow::onCreateMesh()
{
    auto *e = activeEditor();
    if (!e || !e->document()) return;
    if (e->document()->filePath.empty()) { onSaveAs(); if (e->document()->filePath.empty()) return; }
    updateStatus("Creating mesh...");
    char errmsg[1024] = {};
    const char *meshCmd = "mi_createmesh()";
    if (e->document()->physicsType() == 1) meshCmd = "ei_createmesh()";
    else if (e->document()->physicsType() == 2) meshCmd = "hi_createmesh()";
    else if (e->document()->physicsType() == 3) meshCmd = "ci_createmesh()";
    if (exec_python(meshCmd, errmsg, sizeof(errmsg)) != 0) {
        updateStatus(QString("Mesh error: %1").arg(errmsg));
    }
    else {
        // Mesh data was passed directly via callback — no file I/O needed
        const int nodes = e->meshNodeCount(), elems = e->meshElementCount();
        QString msg = tr("Mesh: %1 nodes, %2 elements").arg(nodes).arg(elems);
        if (e->greyMeshLineCount() > 0)
            msg += tr(" -- grey = regions with no block label");
        // A rough sense of scale: very coarse meshes give poor field values,
        // very fine ones take long to solve
        if (elems > 0 && elems < 1000)
            msg += tr(" -- coarse: refine the mesh size of important regions for accurate results");
        else if (elems > 300000)
            msg += tr(" -- very fine: solving will take a while");
        updateStatus(msg);
    }
}

void MainWindow::onAnalyze()
{
    auto *e = activeEditor();
    if (!e || !e->document()) return;
    // Find missing materials, unlabelled regions etc. before meshing, with a
    // way to jump to each.
    if (!confirmProblemCheck(this, e)) { e->update(); return; }
    if (e->document()->filePath.empty()) { onSaveAs(); if (e->document()->filePath.empty()) return; }
    updateStatus("Running analysis...");
    char errmsg[1024] = {};
    const char *solveCmd = "mi_analyse(0)";
    if (e->document()->physicsType() == 1) solveCmd = "ei_analyse(0)";
    else if (e->document()->physicsType() == 2) solveCmd = "hi_analyse(0)";
    else if (e->document()->physicsType() == 3) solveCmd = "ci_analyse(0)";
    if (exec_python(solveCmd, errmsg, sizeof(errmsg)) != 0) {
        // A pre-mesh validation failure (e.g. a block label with no material)
        // returns before the solver runs, so there's no progress dialog to carry
        // the message -- surface it in a pop-up rather than an easily-missed
        // status line. Once the solver has run, though, its progress dialog has
        // already shown the failure along with the solver's own diagnostic, which
        // says more than the exit code this message would repeat.
        updateStatus(tr("Analysis failed"));
        if (!SolverProgressDialog::takeFailureReported())
            QMessageBox::warning(this, tr("Analysis Failed"), QString::fromUtf8(errmsg));
    } else {
        updateStatus("Analysis complete");
        e->setProperty("femmAnalyzedSerial", QVariant::fromValue<qulonglong>(e->document()->editSerial));
        // One click from model to results: open (or reload) the solution
        // instead of making the user press View Results as a second step.
        if (QSettings().value("analysis/autoShowResults", true).toBool())
            onViewResults();
    }
}

void MainWindow::onViewResults()
{
    auto *e = activeEditor();
    QString ansPath;
    if (e && e->document() && !e->document()->filePath.empty()) {
        std::string p = e->document()->filePath;
        // Derive results file path from problem file. Match the extension
        // case-insensitively (e.g. .FEM, .Fee) so an uppercase-named file still
        // resolves to its solution; each type maps to its own results suffix.
        std::string ext = (p.size() > 4) ? p.substr(p.size() - 4) : "";
        for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        std::string base = (p.size() > 4) ? p.substr(0, p.size() - 4) : p;
        if (ext == ".fem")      p = base + ".ans";
        else if (ext == ".fee") p = base + ".res";
        else if (ext == ".feh") p = base + ".anh";
        else if (ext == ".fec") p = base + ".anc";
        ansPath = QString::fromStdString(p);
    }
    if (ansPath.isEmpty()) {
        QSettings settings;
        QString lastDir = settings.value("lastDirectory", QString()).toString();
        ansPath = QFileDialog::getOpenFileName(this, "Open Results File", lastDir,
            "Solution Files (*.ans *.res *.anh *.anc);;All Files (*)");
        if (ansPath.isEmpty()) return;
        settings.setValue("lastDirectory", QFileInfo(ansPath).absolutePath());
    }
    openResultsFile(ansPath);
}

// ================================================================
// Postprocessor slots
// ================================================================

void MainWindow::setPreprocessorEditMode(int mode)
{
    auto *e = activeEditor();
    if (e) e->setEditAction(mode);

    QAction *act = nullptr;
    switch (mode) {
        case 0: act = m_actNode; break;
        case 1: act = m_actSegment; break;
        case 2: act = m_actBlock; break;
        case 3: act = m_actArc; break;
        case 4: act = m_actGroup; break;
    }
    if (act) act->setChecked(true);
}

void MainWindow::setPostprocessorEditMode(int mode)
{
    auto *r = activeResults();
    auto *s = activeScalarResults();
    if (r) r->setMode(mode);
    else if (s) s->setMode(mode);

    auto acts = m_ppModeGroup->actions();
    if (mode >= 0 && mode < acts.size())
        acts[mode]->setChecked(true);
}

void MainWindow::onPPPointMode()
{
    auto *r = activeResults(); if (r) { r->setMode(0); }
    else { auto *s = activeScalarResults(); if (s) s->setMode(0); }
    m_ppActPoint->setChecked(true);
    updateStatus("Point values mode: click to query field values");
}

void MainWindow::onPPContourMode()
{
    auto *r = activeResults(); if (r) { r->setMode(1); }
    else { auto *s = activeScalarResults(); if (s) s->setMode(1); }
    m_ppActContour->setChecked(true);
    updateStatus("Contour mode: click to add contour points, right-click to finish");
}

void MainWindow::onPPAreaMode()
{
    auto *r = activeResults(); if (r) { r->setMode(2); }
    else { auto *s = activeScalarResults(); if (s) s->setMode(2); }
    m_ppActArea->setChecked(true);
    updateStatus("Area mode: click to select/deselect blocks for integration");
}

void MainWindow::onPPToggleDensity()
{
    auto *r = activeResults();
    if (r) { r->setShowDensity(m_ppActDensity->isChecked()); }
    else { auto *s = activeScalarResults(); if (s) s->setShowDensity(m_ppActDensity->isChecked()); }
}

void MainWindow::onPPToggleContours()
{
    auto *r = activeResults();
    if (r) { r->setShowContours(m_ppActContours->isChecked()); }
    else { auto *s = activeScalarResults(); if (s) s->setShowContours(m_ppActContours->isChecked()); }
}

void MainWindow::onPPToggleMesh()
{
    auto *r = activeResults();
    if (r) { r->setShowMesh(m_ppActMesh->isChecked()); }
    else { auto *s = activeScalarResults(); if (s) s->setShowMesh(m_ppActMesh->isChecked()); }
}

// ================================================================
// Status bar
// ================================================================

int *MainWindow::activeCoords() const
{
    if (auto *e = activeEditor())        { if (e->document()) return &e->document()->Coords; }
    if (auto *r = activeResults())       { if (r->document()) return &r->document()->Coords; }
    if (auto *s = activeScalarResults()) {
        if (s->document())     return &s->document()->Coords;
        if (s->heatDocument()) return &s->heatDocument()->Coords;
        if (s->currDocument()) return &s->currDocument()->Coords;
    }
    return nullptr;
}

void MainWindow::updateCoordinates(double x, double y)
{
    // Check editor and results for problem type
    int pt = 0;
    auto *e = activeEditor();
    auto *r = activeResults();
    auto *s = activeScalarResults();
    if (e && e->document()) pt = e->document()->ProblemType;
    if (r && r->document()) pt = r->document()->ProblemType;
    if (s && s->document()) pt = s->document()->ProblemType;

    const int *coords = activeCoords();
    m_coordLabel->setText(coorddisp::statusText(coords ? *coords : 0, pt, x, y));
}

void MainWindow::showGridPropertiesDialog()
{
    auto *e = activeEditor();
    auto *r = activeResults();
    auto *s = activeScalarResults();
    if (!e && !r && !s) return;
    const double curSize = e ? e->gridSize() : (r ? r->gridSize() : s->gridSize());
    int *coords = activeCoords();

    QDialog gdlg(this);
    gdlg.setWindowTitle(tr("Grid Properties"));
    auto *form = new QFormLayout;
    auto *gEdit = new PyLineEdit;
    gEdit->setText(QString::number(curSize));
    form->addRow(tr("Grid Size"), gEdit);

    // Coordinates is document state, so it only appears when a document is in
    // focus. Grid size is view state and is always editable.
    QComboBox *cbox = nullptr;
    if (coords) {
        cbox = new QComboBox;
        cbox->addItem(tr("Cartesian"));
        cbox->addItem(tr("Polar"));
        cbox->setCurrentIndex(*coords == 1 ? 1 : 0);
        form->addRow(tr("Coordinates"), cbox);
    }

    auto *gbtns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(gbtns, &QDialogButtonBox::accepted, &gdlg, &QDialog::accept);
    connect(gbtns, &QDialogButtonBox::rejected, &gdlg, &QDialog::reject);
    auto *glay = new QVBoxLayout(&gdlg);
    glay->addLayout(form);
    glay->addWidget(gbtns);

    if (gdlg.exec() != QDialog::Accepted) return;
    const double size = gEdit->evaluateAsDouble(curSize);
    if (e) e->setGridSize(size); else if (r) r->setGridSize(size); else s->setGridSize(size);
    QSettings().setValue("editor/gridSize", size);
    if (coords && cbox) *coords = cbox->currentIndex();
}

void MainWindow::updateStatus(const QString &msg)
{
    m_statusLabel->setText(msg);
}


// ================================================================
// Common menus (File, Edit, Window, Help — always visible)
// ================================================================

void MainWindow::createCommonMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::onNew);
    file->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::onOpen);
    m_actSave = file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::onSave);
    m_actSaveAs = file->addAction(tr("Save &As..."), QKeySequence(), this, &MainWindow::onSaveAs);
    file->addSeparator();
    m_recentFilesMenu = file->addMenu(tr("Recent &Files"));
    updateRecentFilesMenu();
    m_dxfSep = file->addSeparator();
    m_importDxf = file->addAction(tr("&Import DXF..."), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (!e || !e->document()) return;
        QSettings settings;
        QString lastDir = settings.value("lastDirectory", QString()).toString();
        QString path = QFileDialog::getOpenFileName(this, tr("Import DXF"), lastDir,
            tr("DXF Files (*.dxf);;All Files (*)"));
        if (path.isEmpty()) return;
        settings.setValue("lastDirectory", QFileInfo(path).absolutePath());
        auto *doc = e->document();
        doc->pushUndo();
        if (!ReadDXF(*doc, path.toStdString())) {
            QMessageBox::warning(this, tr("Import DXF"), tr("Failed to read DXF file."));
            e->update();
            return;
        }
        if (doc->nodelist.empty()) {
            QMessageBox::information(this, tr("Import DXF"), tr("No geometry found in file."));
            e->update();
            return;
        }
        // Compute suggested tolerance: 1e-4 of bounding box, rounded to 1 sig fig
        // (same formula as OF)
        double x0 = doc->nodelist[0].x, x1 = x0;
        double y0 = doc->nodelist[0].y, y1 = y0;
        for (auto &nd : doc->nodelist) {
            if (nd.x < x0) x0 = nd.x;
            if (nd.x > x1) x1 = nd.x;
            if (nd.y < y0) y0 = nd.y;
            if (nd.y > y1) y1 = nd.y;
        }
        double R = sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0)) * 1.e-04;
        if (R > 0)
            R = floor(R / pow(10.0, floor(log10(R)))) * pow(10.0, floor(log10(R)));
        else
            R = 1.e-06;

        // Prompt for tolerance
        QDialog dlg(this);
        dlg.setWindowTitle(tr("DXF Import Tolerance"));
        auto *form = new QFormLayout;
        auto *tolEdit = new PyLineEdit;
        tolEdit->setText(QString::number(R, 'g', 15));
        form->addRow(tr("Tolerance:"), tolEdit);
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() != QDialog::Accepted) {
            doc->undo();  // revert the import
            e->update();
            return;
        }
        double tol = tolEdit->evaluateAsDouble(R);
        doc->EnforcePSLG(tol);
        e->zoomFit();
    });
    m_exportDxf = file->addAction(tr("&Export DXF..."), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (!e || !e->document()) return;
        QSettings settings;
        QString lastDir = settings.value("lastDirectory", QString()).toString();
        QString path = QFileDialog::getSaveFileName(this, tr("Export DXF"), lastDir,
            tr("DXF Files (*.dxf);;All Files (*)"));
        if (path.isEmpty()) return;
        settings.setValue("lastDirectory", QFileInfo(path).absolutePath());
        if (!WriteDXF(*e->document(), path.toStdString()))
            QMessageBox::warning(this, tr("Export DXF"), tr("Failed to write DXF file."));
        else
            updateStatus(tr("Exported %1").arg(QFileInfo(path).fileName()));
    });
    file->addSeparator();
    // close(), not QApplication::quit: closeEvent asks about unsaved documents.
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, [this]() { close(); });

    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    QMenu *edit = m_editMenu;
    m_actUndo = edit->addAction(tr("&Undo"), QKeySequence::Undo, [this]() {
        auto *e = activeEditor();
        if (e && e->document() && e->document()->canUndo()) {
            e->document()->undo();
            e->update();
        }
    });
    m_actRedo = edit->addAction(tr("&Redo"), QKeySequence::Redo, [this]() {
        auto *e = activeEditor();
        if (e && e->document() && e->document()->canRedo()) {
            e->document()->redo();
            e->update();
        }
    });
    edit->addSeparator();
    edit->addAction(tr("&Select Region"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->selectWindow();
    });
    edit->addAction(tr("Co&py"), QKeySequence(), this, &MainWindow::onCopy);
    m_actDeleteSelected = edit->addAction(tr("&Delete Selected"), QKeySequence::Delete, [this]() {
        auto *e = activeEditor();
        if (e) e->deleteSelected();   // shared with Backspace; see keyPressEvent
    });
    edit->addAction(tr("&Move"), QKeySequence(), this, &MainWindow::onMove);
    edit->addAction(tr("Sc&ale"), QKeySequence(), this, &MainWindow::onScale);
    edit->addAction(tr("Mi&rror"), QKeySequence(), this, &MainWindow::onMirror);
    edit->addAction(tr("&Pattern..."), QKeySequence(), this, &MainWindow::onPattern);
    edit->addAction(tr("Create &Radius..."), QKeySequence(), this, &MainWindow::onCreateRadius);
    edit->addSeparator();
    // Deliberately no keyboard shortcut: Ctrl+C here would shadow the ordinary
    // Ctrl+C copy in every child window (notably the Output dialog's selectable
    // text), and OF bound no shortcut to this either.
    edit->addAction(tr("Copy as &Bitmap"), QKeySequence(), [this]() {
        QWidget *target = nullptr;
        auto *e = activeEditor(); if (e) target = e;
        if (!target) { auto *r = activeResults(); if (r) target = r; }
        if (!target) { auto *s = activeScalarResults(); if (s) target = s; }
        if (!target) return;
        QPixmap pix = target->grab();
        QApplication::clipboard()->setPixmap(pix);
        updateStatus(tr("View copied to clipboard"));
    });
    edit->addSeparator();
    QAction *prefsAct = edit->addAction(tr("&Preferences..."), QKeySequence::Preferences, [this]() {
        auto *e = activeEditor();
        if (e) {
            int pt = e->document() ? e->document()->physicsType() : 0;
            PrePrefsDialog dlg(pt, this);
            dlg.exec();
            return;
        }
        auto *r = activeResults();
        if (r) { r->showPrefsDialog(this); return; }
        auto *s = activeScalarResults();
        if (s) { s->showPrefsDialog(this); return; }
    });
    // macOS: FEMM > Settings..., Cmd+,
    prefsAct->setMenuRole(QAction::PreferencesRole);
    // Application-wide rather than per-document, so it lives beside
    // Preferences rather than inside it -- and unlike Preferences it is
    // available with no document open.
    edit->addAction(tr("&Scripting Preferences..."), QKeySequence(), [this]() {
        ScriptPrefsDialog dlg(this);
        dlg.exec();
    });
}

// ================================================================
// Preprocessor menus and top toolbars
// ================================================================

void MainWindow::createPreprocessorUI()
{
    // --- Menus (inserted after Edit) ---
    // We add them to the menu bar; they'll be hidden/shown via menuAction()->setVisible()

    m_preMenuView = menuBar()->addMenu(tr("&View"));
    m_preMenuView->addAction(tr("Zoom &In"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->zoomIn();
    });
    m_preMenuView->addAction(tr("Zoom &Out"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->zoomOut();
    });
    m_preMenuView->addAction(tr("Zoom &Natural"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->zoomFit();
    });
    m_preMenuView->addAction(tr("Zoom to &Selection"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->zoomSelection();
    });
    m_preMenuView->addAction(tr("Zoom &Window"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->zoomWindow();
    });
    // Rarely used: keyboard zoom and scrolling (the arrow keys scroll too)
    QMenu *nav = m_preMenuView->addMenu(tr("Na&vigation"));
    nav->addAction(tr("&Keyboard Zoom..."), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (!e) return;
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Keyboard Zoom"));
        auto *form = new QFormLayout;
        auto *leftEdit = new PyLineEdit; auto *botEdit = new PyLineEdit;
        auto *rightEdit = new PyLineEdit; auto *topEdit = new PyLineEdit;
        double x0, y0, x1, y1;
        e->viewBounds(x0, y0, x1, y1);
        leftEdit->setText(QString::number(x0, 'g', 6));
        botEdit->setText(QString::number(y0, 'g', 6));
        rightEdit->setText(QString::number(x1, 'g', 6));
        topEdit->setText(QString::number(y1, 'g', 6));
        form->addRow(tr("Left:"), leftEdit);
        form->addRow(tr("Bottom:"), botEdit);
        form->addRow(tr("Right:"), rightEdit);
        form->addRow(tr("Top:"), topEdit);
        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *lay = new QVBoxLayout(&dlg);
        lay->addLayout(form);
        lay->addWidget(btns);
        if (dlg.exec() == QDialog::Accepted) {
            e->zoomTo(leftEdit->evaluateAsDouble(x0), botEdit->evaluateAsDouble(y0),
                      rightEdit->evaluateAsDouble(x1), topEdit->evaluateAsDouble(y1));
        }
    });
    m_preMenuView->addSeparator();
    nav->addAction(tr("Scroll &Left"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->panLeft();
    });
    nav->addAction(tr("Scroll &Right"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->panRight();
    });
    nav->addAction(tr("Scroll &Up"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->panUp();
    });
    nav->addAction(tr("Scroll &Down"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->panDown();
    });
    m_preMenuView->addSeparator();
    m_preMenuView->addAction(m_modelPanel->toggleViewAction());
    m_fillRegionsMenu = m_preMenuView->addAction(tr("&Fill Regions by Material"));
    m_fillRegionsMenu->setCheckable(true);
    m_fillRegionsMenu->setChecked(QSettings().value("editor/fillRegions", true).toBool());
    connect(m_fillRegionsMenu, &QAction::toggled, [this](bool on) {
        QSettings().setValue("editor/fillRegions", on);
        for (auto *sub : m_mdiArea->subWindowList())
            if (auto *e = qobject_cast<EditorWidget *>(sub->widget())) e->setFillRegions(on);
    });
    m_showNamesMenu = m_preMenuView->addAction(tr("Show Block &Names"));
    m_showNamesMenu->setCheckable(true);
    m_showNamesMenu->setChecked(true);
    connect(m_showNamesMenu, &QAction::toggled, [this](bool on) {
        auto *e = activeEditor(); if (e) { e->setShowNames(on); }
    });
    m_preMenuView->addAction(tr("Show &Orphans"), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        auto *doc = e->document();
        doc->UnselectAll();
        bool found = false;
        // Count how many lines/arcs reference each node
        std::vector<int> refcount(doc->nodelist.size(), 0);
        for (auto &s : doc->linelist) { refcount[s.n0]++; refcount[s.n1]++; }
        for (auto &a : doc->arclist) { refcount[a.n0]++; refcount[a.n1]++; }
        // Nodes referenced by exactly one segment/arc endpoint are orphaned
        for (int i = 0; i < (int)doc->nodelist.size(); i++) {
            if (refcount[i] == 1) { doc->nodelist[i].IsSelected = true; found = true; }
        }
        if (found) {
            for (auto &s : doc->linelist)
                if (doc->nodelist[s.n0].IsSelected || doc->nodelist[s.n1].IsSelected)
                    s.IsSelected = true;
            for (auto &a : doc->arclist)
                if (doc->nodelist[a.n0].IsSelected || doc->nodelist[a.n1].IsSelected)
                    a.IsSelected = true;
        }
        e->update();
        if (found) updateStatus(tr("Orphaned nodes/segments selected"));
        else updateStatus(tr("No orphaned nodes found"));
    });

    // "Problem" holds only one entry, and between 24eb1f2 and this commit it
    // was a bare QAction on the menu bar for that reason -- one click instead
    // of a single-item menu. That is invisible on macOS: there, QMenuBar is a
    // proxy for the system menu bar, and Cocoa's accepts only submenus at the
    // top level, so the action rendered nowhere. With no toolbar button and no
    // shortcut, Problem Definition -- and with it the problem type, units,
    // precision, min angle and the magnetics FREQUENCY -- was unreachable on
    // macOS from 18 Jul 2026 until this commit.
    //
    // Only macOS was affected, and the reason is not "macOS is special": it is
    // whether the desktop supplies a GLOBAL menu bar. QMenuBar delegates to a
    // system bar only when the platform theme offers one -- always on macOS;
    // on Linux only with KDE's Global Menu widget, a GNOME appmenu extension,
    // or the old Unity. Stock Ubuntu is GNOME with no global bar, so Qt draws
    // the menu bar itself and the bare action rendered fine there (verified).
    //
    // A menu unconditionally anyway: the working Linux case is a default that
    // a user can switch off, one shape everywhere beats an #ifdef, and nothing
    // in a test can catch the regression -- the QAction is valid and enabled
    // on every platform; only a native bar declines to draw it. The menu is
    // the guard.
    m_preMenuProblem = menuBar()->addMenu(tr("&Problem"));
    m_preMenuProblem->addAction(tr("&Problem Definition..."), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        ProblemDialog dlg(e->document(), this);
        dlg.exec();
    });

    m_preMenuGrid = menuBar()->addMenu(tr("&Grid"));
    m_showGridMenu = m_preMenuGrid->addAction(tr("&Show Grid"));
    m_showGridMenu->setCheckable(true);
    m_showGridMenu->setChecked(QSettings().value("editor/showGrid", true).toBool());
    connect(m_showGridMenu, &QAction::toggled, [this](bool on) {
        auto *e = activeEditor(); if (e) e->setShowGrid(on);
        if (m_showGridTB && m_showGridTB->isChecked() != on) {
            m_showGridTB->blockSignals(true);
            m_showGridTB->setChecked(on);
            m_showGridTB->blockSignals(false);
        }
    });
    m_snapGridMenu = m_preMenuGrid->addAction(tr("S&nap to Grid"));
    m_snapGridMenu->setCheckable(true);
    m_snapGridMenu->setChecked(QSettings().value("editor/snapToGrid", false).toBool());
    connect(m_snapGridMenu, &QAction::toggled, [this](bool on) {
        auto *e = activeEditor(); if (e) e->setSnapToGrid(on);
        if (m_snapGridTB && m_snapGridTB->isChecked() != on) {
            m_snapGridTB->blockSignals(true);
            m_snapGridTB->setChecked(on);
            m_snapGridTB->blockSignals(false);
        }
    });
    m_objectSnapMenu = m_preMenuGrid->addAction(tr("&Object Snap"));
    m_objectSnapMenu->setCheckable(true);
    m_objectSnapMenu->setToolTip(tr("Snap to endpoints, midpoints, centres and lines while drawing"));
    m_objectSnapMenu->setChecked(QSettings().value("editor/objectSnap", true).toBool());
    connect(m_objectSnapMenu, &QAction::toggled, [this](bool on) {
        QSettings().setValue("editor/objectSnap", on);
        for (auto *sub : m_mdiArea->subWindowList())
            if (auto *e = qobject_cast<EditorWidget *>(sub->widget())) e->setObjectSnap(on);
    });
    // Mnemonic is Z, not S: "&Show Grid" already claims S in this menu, and a
    // duplicate accelerator only cycles the highlight instead of activating.
    m_preMenuGrid->addAction(tr("Set Grid Si&ze..."), QKeySequence(),
                             [this]() { showGridPropertiesDialog(); });

    m_preMenuOperation = menuBar()->addMenu(tr("&Operation"));
    // Mode items added after toolbar creation (need m_actNode etc.)

    m_preMenuProperties = menuBar()->addMenu(tr("P&roperties"));
    m_preMenuProperties->addAction(tr("&Materials..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        PropertyListDialog dlg(e->document(), 2, this);
        dlg.exec();
        e->update();
    });
    m_preMenuProperties->addAction(tr("&Boundary Conditions..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        PropertyListDialog dlg(e->document(), 1, this);
        dlg.exec();
        e->update();
    });
    m_preMenuProperties->addAction(tr("&Point Properties..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        PropertyListDialog dlg(e->document(), 0, this);
        dlg.exec();
        e->update();
    });
    // Magnetics calls these "Circuits"; the scalar physics (electrostatics,
    // heat flow, current flow) call them "Conductors". Same dialog; the label
    // is retargeted per active document in the tab-switch handler below.
    m_actCircuits = m_preMenuProperties->addAction(tr("&Circuits..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        PropertyListDialog dlg(e->document(), 3, this);
        dlg.exec();
        e->update();
    });
    m_actExteriorRegion = m_preMenuProperties->addAction(tr("&Exterior Region..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        auto *doc = e->document();

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Exterior Region Properties"));
        auto *form = new QFormLayout;
        auto *Zo = new PyLineEdit; Zo->setText(QString::number(doc->extZo, 'g', 15));
        form->addRow(tr("Center of the Exterior Region"), Zo);
        auto *Ro = new PyLineEdit; Ro->setText(QString::number(doc->extRo, 'g', 15));
        form->addRow(tr("Radius of the Exterior Region"), Ro);
        auto *Ri = new PyLineEdit; Ri->setText(QString::number(doc->extRi, 'g', 15));
        form->addRow(tr("Radius of the Interior Region"), Ri);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() == QDialog::Accepted) {
            doc->extZo = Zo->evaluateAsDouble(0);
            doc->extRo = Ro->evaluateAsDouble(0);
            doc->extRi = Ri->evaluateAsDouble(0);
        }
    });
    m_preMenuProperties->addSeparator();
    m_preMenuProperties->addAction(tr("&Air Space Around Model"), QKeySequence(), this, &MainWindow::onAirSpace)
        ->setToolTip(tr("Surround the model with an open boundary and fill the space with air -- for fields in free space"));
    m_actMaterialLibrary = m_preMenuProperties->addAction(tr("Materials &Library..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        if (e->document()->physicsType() == 1) {
            ElecMatLibDialog dlg(e->document(), this);
            dlg.exec();
            e->update();
            return;
        }
        if (e->document()->physicsType() == 2) {
            HeatMatLibDialog dlg(e->document(), this);
            dlg.exec();
            e->update();
            return;
        }
        if (e->document()->physicsType() == 3) {
            CurrMatLibDialog dlg(e->document(), this);
            dlg.exec();
            e->update();
            return;
        }
        MatLibDialog dlg(e->document(), this);
        dlg.exec();
        e->update();
    });

    m_preMenuMesh = menuBar()->addMenu(tr("&Mesh"));
    m_preMenuMesh->addAction(tr("&Create Mesh"), QKeySequence(), this, &MainWindow::onCreateMesh);
    m_preMenuMesh->addAction(tr("&Show Mesh"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (!e) return;
        // Pure display toggle. If nothing has been meshed this session there is
        // no mesh to show -- prompt to Create Mesh rather than reading stale
        // (and possibly deleted) .node/.ele off disk.
        if (e->meshEdgeCount() == 0) { updateStatus("No mesh to display — run Create Mesh first"); return; }
        e->setShowMesh(!e->showMesh());
    });
    m_preMenuMesh->addAction(tr("&Purge Mesh"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (!e) return;
        e->clearMesh();
        updateStatus("Mesh cleared");
    });

    m_preMenuAnalysis = menuBar()->addMenu(tr("&Analysis"));
    m_preMenuAnalysis->addAction(tr("&Analyze"), QKeySequence(), this, &MainWindow::onAnalyze);
    m_preMenuAnalysis->addAction(tr("&View Results"), QKeySequence(), this, &MainWindow::onViewResults);
    m_preMenuAnalysis->addSeparator();
    m_preMenuAnalysis->addAction(tr("Parameter &Sweep..."), QKeySequence(), [this]() {
        auto *e = activeEditor();
        if (!e || !e->document()) return;
        if (e->document()->physicsType() != 0) {
            QMessageBox::information(this, tr("Parameter Sweep"),
                tr("Parameter sweeps are available for magnetics problems."));
            return;
        }
        if (e->document()->filePath.empty()) { onSaveAs(); if (e->document()->filePath.empty()) return; }
        SweepDialog dlg(e, this);
        dlg.exec();
    });

    // --- Top toolbar 1: File ---
    m_preTopBar1 = addToolBar(tr("File"));
    m_preTopBar1->setIconSize(QSize(16, 15));
    m_preTopBar1->setMovable(false);
    m_preTopBar1->addAction(icon("new"), tr("New"), this, &MainWindow::onNew);
    m_preTopBar1->addAction(icon("open_file"), tr("Open"), this, &MainWindow::onOpen);

    // --- Top toolbar 2: Editor ---
    m_preTopBar2 = addToolBar(tr("Editor"));
    m_preTopBar2->setIconSize(QSize(16, 15));
    m_preTopBar2->setMovable(false);

    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);

    // Sketch tools first: they are the quickest way to draw geometry.
    {
        struct { int tool; const char *name; const char *tip; } tools[] = {
            {EditorWidget::SketchSelect, QT_TR_NOOP("Select"), QT_TR_NOOP("Select and move (V) -- click, drag a window, drag points")},
            {EditorWidget::SketchLine,   QT_TR_NOOP("Line"),      QT_TR_NOOP("Line / polyline (L) -- click points, nodes are created automatically")},
            {EditorWidget::SketchRect,   QT_TR_NOOP("Rectangle"), QT_TR_NOOP("Rectangle (R) -- two opposite corners")},
            {EditorWidget::SketchCircle, QT_TR_NOOP("Circle"),    QT_TR_NOOP("Circle (C) -- centre, then radius")},
            {EditorWidget::SketchArc3,   QT_TR_NOOP("3-Point Arc"), QT_TR_NOOP("3-point arc (A) -- start, end, point on the arc")},
        };
        for (auto &t : tools) {
            QAction *a = m_preTopBar2->addAction(sketchIcon(t.tool), tr(t.name));
            a->setCheckable(true);
            a->setToolTip(tr(t.tip));
            m_modeGroup->addAction(a);
            const int tool = t.tool;
            connect(a, &QAction::triggered, this, [this, tool]() {
                auto *e = activeEditor(); if (e) e->setSketchTool(tool);
            });
            m_actSketch[tool] = a;
        }
        m_preTopBar2->addSeparator();

        // Assign material: pick it here, then click regions with the bucket
        QAction *fill = m_preTopBar2->addAction(sketchIcon(EditorWidget::SketchFill), tr("Assign Material"));
        fill->setCheckable(true);
        fill->setToolTip(tr("Assign material (F) -- click inside a region to fill it with the chosen material"));
        m_modeGroup->addAction(fill);
        connect(fill, &QAction::triggered, this, [this]() {
            auto *e = activeEditor(); if (!e) return;
            if (m_materialPicker) e->setFillMaterial(m_materialPicker->currentMaterial());
            e->setSketchTool(EditorWidget::SketchFill);
        });
        m_actSketch[EditorWidget::SketchFill] = fill;
        // Edit tools: trim, offset, dimension
        m_preTopBar2->addSeparator();
        struct { int tool; const char *name; const char *tip; } edits[] = {
            {EditorWidget::SketchTrim, QT_TR_NOOP("Trim"), QT_TR_NOOP("Trim (T) -- click a piece of a line or arc to remove it")},
            {EditorWidget::SketchOffset, QT_TR_NOOP("Offset"), QT_TR_NOOP("Offset (O) -- parallel copy of a contour at a distance")},
            {EditorWidget::SketchExtend, QT_TR_NOOP("Extend"), QT_TR_NOOP("Extend (E) -- lengthen a line to the next line or arc")},
            {EditorWidget::SketchDimension, QT_TR_NOOP("Dimension"), QT_TR_NOOP("Dimension (D) -- show lengths and radii; click to set an exact value")},
        };
        for (auto &t : edits) {
            QAction *a = m_preTopBar2->addAction(sketchIcon(t.tool), tr(t.name));
            a->setCheckable(true);
            a->setToolTip(tr(t.tip));
            m_modeGroup->addAction(a);
            const int tool = t.tool;
            connect(a, &QAction::triggered, this, [this, tool]() {
                auto *e = activeEditor(); if (e) e->setSketchTool(tool);
            });
            m_actSketch[tool] = a;
        }
        m_actPattern = m_preTopBar2->addAction(sketchIcon(-1), tr("Pattern"), this, &MainWindow::onPattern);
        m_actPattern->setToolTip(tr("Pattern -- circular or linear copies of the selection, with preview"));
        m_materialPicker = new MaterialPicker;
        m_preTopBar2->addWidget(m_materialPicker);
        connect(m_materialPicker, &MaterialPicker::aboutToShowPopup, this, [this]() {
            auto *e = activeEditor();
            m_materialPicker->setDocument(e ? e->document() : nullptr);
        });
        connect(m_materialPicker, &MaterialPicker::materialChosen, this, [this](const QString &name) {
            auto *e = activeEditor(); if (!e) return;
            e->setFillMaterial(name);
            e->setSketchTool(EditorWidget::SketchFill);
            e->setFocus();
        });
        m_preTopBar2->addSeparator();
    }

    m_actNode = m_preTopBar2->addAction(icon("node"), tr("Node"));
    m_actNode->setCheckable(true); m_actNode->setChecked(true);
    m_actNode->setToolTip(tr("Node mode")); m_modeGroup->addAction(m_actNode);
    connect(m_actNode, &QAction::triggered, this, &MainWindow::onNodeMode);

    m_actSegment = m_preTopBar2->addAction(icon("segment"), tr("Segment"));
    m_actSegment->setCheckable(true); m_actSegment->setToolTip(tr("Segment mode"));
    m_modeGroup->addAction(m_actSegment);
    connect(m_actSegment, &QAction::triggered, this, &MainWindow::onSegmentMode);

    m_actArc = m_preTopBar2->addAction(icon("arc"), tr("Arc"));
    m_actArc->setCheckable(true); m_actArc->setToolTip(tr("Arc segment mode"));
    m_modeGroup->addAction(m_actArc);
    connect(m_actArc, &QAction::triggered, this, &MainWindow::onArcMode);

    m_actBlock = m_preTopBar2->addAction(icon("block"), tr("Block Label"));
    m_actBlock->setCheckable(true); m_actBlock->setToolTip(tr("Block label mode"));
    m_modeGroup->addAction(m_actBlock);
    connect(m_actBlock, &QAction::triggered, this, &MainWindow::onBlockMode);

    m_actGroup = m_preTopBar2->addAction(icon("group"), tr("Group"));
    m_actGroup->setCheckable(true); m_actGroup->setToolTip(tr("Group mode"));
    m_modeGroup->addAction(m_actGroup);
    connect(m_actGroup, &QAction::triggered, this, &MainWindow::onGroupMode);

    // Populate Operation menu now that toolbar mode actions exist
    auto addModeMenuItem = [&](const QString &text, QAction *tbAction) {
        auto *act = m_preMenuOperation->addAction(text);
        act->setCheckable(true);
        m_modeGroup->addAction(act);
        connect(act, &QAction::triggered, [tbAction]() { tbAction->trigger(); });
        connect(tbAction, &QAction::toggled, [act](bool on) {
            act->blockSignals(true);
            act->setChecked(on);
            act->blockSignals(false);
        });
    };
    addModeMenuItem(tr("Se&lect"), m_actSketch[EditorWidget::SketchSelect]);
    addModeMenuItem(tr("&Line"), m_actSketch[EditorWidget::SketchLine]);
    addModeMenuItem(tr("&Rectangle"), m_actSketch[EditorWidget::SketchRect]);
    addModeMenuItem(tr("&Circle"), m_actSketch[EditorWidget::SketchCircle]);
    addModeMenuItem(tr("3-&Point Arc"), m_actSketch[EditorWidget::SketchArc3]);
    addModeMenuItem(tr("Assign &Material"), m_actSketch[EditorWidget::SketchFill]);
    addModeMenuItem(tr("&Trim"), m_actSketch[EditorWidget::SketchTrim]);
    addModeMenuItem(tr("O&ffset"), m_actSketch[EditorWidget::SketchOffset]);
    addModeMenuItem(tr("&Extend"), m_actSketch[EditorWidget::SketchExtend]);
    addModeMenuItem(tr("&Dimension"), m_actSketch[EditorWidget::SketchDimension]);
    m_preMenuOperation->addSeparator();
    addModeMenuItem(tr("&Node"), m_actNode);
    addModeMenuItem(tr("&Segment"), m_actSegment);
    addModeMenuItem(tr("&Arc Segment"), m_actArc);
    addModeMenuItem(tr("&Block"), m_actBlock);
    addModeMenuItem(tr("&Group"), m_actGroup);
    m_preMenuOperation->addSeparator();
    m_preMenuOperation->addAction(tr("&Open Selected"), QKeySequence(), [this]() {
        auto *e = activeEditor(); if (e) e->openSelected();
    });

    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("properties"), tr("Properties"), [this]() {
        auto *e = activeEditor(); if (e) e->openSelected();
    })->setToolTip(tr("Open properties (Space)"));

    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("mesh"), tr("Mesh"), this, &MainWindow::onCreateMesh);
    m_preTopBar2->addAction(icon("analyze"), tr("Analyze"), this, &MainWindow::onAnalyze);
    m_preTopBar2->addAction(icon("results"), tr("Results"), this, &MainWindow::onViewResults);

    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("undo"), tr("Undo"), [this]() {
        auto *e = activeEditor();
        if (e && e->document() && e->document()->canUndo()) {
            e->document()->undo();
            e->update();
        }
    });

    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("rectselect"), tr("Select Window"), [this]() {
        auto *e = activeEditor();
        if (e) e->selectWindow();
    });
    m_preTopBar2->addAction(icon("circselect"), tr("Select Circle"), [this]() {
        auto *e = activeEditor();
        if (e) e->selectCircle();
    });

    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("move"), tr("Move"), this, &MainWindow::onMove);
    m_preTopBar2->addAction(icon("copy"), tr("Copy"), this, &MainWindow::onCopy);
    m_preTopBar2->addAction(icon("scale"), tr("Scale"), this, &MainWindow::onScale);
    m_preTopBar2->addAction(icon("mirror"), tr("Mirror"), this, &MainWindow::onMirror);
    m_preTopBar2->addAction(icon("createradius"), tr("Create Radius"),
        this, &MainWindow::onCreateRadius);
    m_preTopBar2->addAction(icon("delete"), tr("Delete"),
        this, &MainWindow::onDeleteSelected);
    m_preTopBar2->addSeparator();
    m_preTopBar2->addAction(icon("openboundary"), tr("Open Boundary"), [this]() {
        auto *e = activeEditor(); if (!e || !e->document()) return;
        auto *doc = e->document();

        // Compute defaults matching original FEMM
        double x0 = 1e16, x1 = -1e16, y0 = 1e16, y1 = -1e16;
        for (auto &nd : doc->nodelist) {
            if (nd.x < x0) x0 = nd.x;
            if (nd.x > x1) x1 = nd.x;
            if (nd.y < y0) y0 = nd.y;
            if (nd.y > y1) y1 = nd.y;
        }
        if (doc->nodelist.empty()) { x0 = x1 = y0 = y1 = 0; }

        double defR, defX, defY;
        if (doc->ProblemType == 0) {
            defR = 0.75 * sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
            defX = (x0 + x1) / 2.0;
            defY = (y0 + y1) / 2.0;
        } else {
            defX = 0;
            defY = (y0 + y1) / 2.0;
            defR = 1.5 * sqrt(x1*x1 + (y1-y0)*(y1-y0)/4.0);
        }

        // Build dialog
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Open Boundary (IABC)"));
        auto *form = new QFormLayout;
        auto *layers = new PyLineEdit;
        layers->setText("7");
        auto *radius = new PyLineEdit;
        radius->setText(QString::number(defR, 'g', 15));
        auto *cx = new PyLineEdit;
        cx->setText(QString::number(defX, 'g', 15));
        auto *cy = new PyLineEdit;
        cy->setText(QString::number(defY, 'g', 15));
        auto *edgeType = new QComboBox;
        edgeType->addItem(tr("Dirichlet"));
        edgeType->addItem(tr("Neumann"));
        edgeType->setCurrentIndex(doc->physicsType() == 0 ? 0 : 1);
        form->addRow(tr("Number of Layers (1-12):"), layers);
        form->addRow(tr("Radius:"), radius);
        form->addRow(tr("Center, Horizontal:"), cx);
        form->addRow(tr("Center, Vertical:"), cy);
        form->addRow(tr("Outer Edge Type:"), edgeType);
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() != QDialog::Accepted) return;

        int nLayers = (int)layers->evaluateAsDouble(7);
        if (nLayers < 1) nLayers = 1;
        if (nLayers > 12) nLayers = 12;
        double r = radius->evaluateAsDouble(defR);
        double xc = cx->evaluateAsDouble(defX);
        double yc = cy->evaluateAsDouble(defY);
        int bc = edgeType->currentIndex();

        doc->pushUndo();
        const char *prefix = "mi";
        if (doc->physicsType() == 1) prefix = "ei";
        else if (doc->physicsType() == 2) prefix = "hi";
        else if (doc->physicsType() == 3) prefix = "ci";
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s_makeABC(%d,%.17g,%.17g,%.17g,%d)",
                 prefix, nLayers, r, xc, yc, bc);
        char errmsg[512] = {0};
        if (exec_python(cmd, errmsg, sizeof(errmsg)) != 0)
            QMessageBox::warning(this, "IABC Error", QString("makeABC failed:\n%1").arg(errmsg));
        e->update();
    });
}

// ================================================================
// Postprocessor menus and toolbar
// ================================================================

void MainWindow::createPostprocessorUI()
{
    // --- Menus ---
    m_ppMenuView = menuBar()->addMenu(tr("&View"));
    m_ppMenuView->addAction(m_resultsPanel->toggleViewAction());
    m_ppMenuView->addAction(m_plotDock->toggleViewAction());
    {
        QMenu *cm = m_ppMenuView->addMenu(tr("Color &Map"));
        auto *grp = new QActionGroup(this);
        const char *names[] = {QT_TR_NOOP("Classic FEMM"), QT_TR_NOOP("Viridis (colour-blind safe)"), QT_TR_NOOP("Turbo (rainbow)")};
        for (int i = 0; i < 3; i++) {
            QAction *a = cm->addAction(tr(names[i]));
            a->setCheckable(true);
            a->setChecked(femmColormap() == i);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, i]() {
                QSettings().setValue("pp/colormap", i);
                applyTheme();
            });
        }
    }
    m_ppMenuView->addSeparator();
    m_ppActDensity = m_ppMenuView->addAction(tr("&Density Plot"));
    m_ppActDensity->setCheckable(true); m_ppActDensity->setChecked(true);
    connect(m_ppActDensity, &QAction::toggled, this, &MainWindow::onPPToggleDensity);

    m_ppActContours = m_ppMenuView->addAction(tr("&Contour Plot"));
    m_ppActContours->setCheckable(true); m_ppActContours->setChecked(true);
    connect(m_ppActContours, &QAction::toggled, this, &MainWindow::onPPToggleContours);

    // Same handler the toolbar button uses. These three menu items used to
    // report the feature as unimplemented while the toolbar did it perfectly
    // well, so a user who reached for the menu was told the wrong thing.
    m_ppMenuView->addAction(tr("&Vector Plot..."), QKeySequence(), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showVectorPlotDialog(this); return; }
        auto *r = activeResults();
        if (r) {
            r->showVectorPlotDialog(this);
            if (r->m_showVectors) m_ppActDensity->setChecked(false);
        }
    });

    m_ppMenuView->addSeparator();
    m_ppActMesh = m_ppMenuView->addAction(tr("Show &Mesh"));
    m_ppActMesh->setCheckable(true); m_ppActMesh->setChecked(false);
    connect(m_ppActMesh, &QAction::toggled, this, &MainWindow::onPPToggleMesh);

    m_ppMenuView->addSeparator();
    // Labelled per physics in syncPostprocessorMenus: magnetics has circuits,
    // the scalar physics have conductors, and the dialog this opens is titled
    // accordingly. The preprocessor already made the same distinction.
    m_ppActCircProps = m_ppMenuView->addAction(tr("Circuit &Properties..."), QKeySequence(), [this]() {
        auto *r = activeResults(); if (r) { r->showCircuitDialog(this); return; }
        auto *s = activeScalarResults(); if (s) { s->showCircuitDialog(this); return; }
    });
    m_ppMenuView->addAction(tr("&Problem Info"), QKeySequence(), [this]() {
        auto *r = activeResults(); if (r) { r->showProblemInfo(this); return; }
        auto *s = activeScalarResults(); if (s) { s->showProblemInfo(this); return; }
    });
    m_ppMenuView->addAction(tr("Plot &B-H Curves..."), QKeySequence(), [this]() {
        auto *r = activeResults(); if (r) r->showBHCurves(this);
    });
    m_ppMenuView->addSeparator();
    m_ppActNames = m_ppMenuView->addAction(tr("Show Block &Names"));
    m_ppActNames->setCheckable(true);
    connect(m_ppActNames, &QAction::toggled, [this](bool on) {
        auto *r = activeResults(); if (r) { r->m_showNames = on; r->update(); }
        else { auto *s = activeScalarResults(); if (s) { s->m_showNames = on; s->update(); } }
    });
    m_ppActPoints = m_ppMenuView->addAction(tr("Show &Points"));
    m_ppActPoints->setCheckable(true);
    m_ppActPoints->setChecked(true);
    connect(m_ppActPoints, &QAction::toggled, [this](bool on) {
        auto *r = activeResults(); if (r) { r->m_showPoints = on; r->update(); }
        else { auto *s = activeScalarResults(); if (s) { s->m_showPoints = on; s->update(); } }
    });

    // Smoothing is a per-solution property, not just a start-up default: it
    // lives on the results document (the same flag mo_smooth sets), so it can
    // be toggled for the window in front of you without disturbing the
    // preference that seeds new ones.
    m_ppActSmooth = m_ppMenuView->addAction(tr("S&moothing"));
    m_ppActSmooth->setCheckable(true);
    m_ppActSmooth->setChecked(true);
    connect(m_ppActSmooth, &QAction::toggled, [this](bool on) {
        setPostprocessorSmoothing(on);
    });

    m_ppMenuOperation = menuBar()->addMenu(tr("O&peration"));
    m_ppMenuOperation->addAction(tr("Export &Report (PDF)..."), QKeySequence(), [this]() {
        QWidget *view = activeResults() ? (QWidget *)activeResults() : (QWidget *)activeScalarResults();
        if (!view) return;
        QString suggest = QSettings().value("lastDirectory").toString() + "/report.pdf";
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Report"), suggest, tr("PDF (*.pdf)"));
        if (path.isEmpty()) return;
        if (exportResultsReport(this, view, path)) updateStatus(tr("Report written to %1").arg(path));
    });
    m_ppMenuOperation->addAction(tr("Compare with &Another Solution..."), QKeySequence(), [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Compare With"),
            QSettings().value("lastDirectory").toString(),
            tr("Solution Files (*.ans *.res *.anh *.anc)"));
        if (path.isEmpty()) return;
        openResultsFile(path);
        // Side by side instead of tabs (Window > Tabs goes back)
        m_mdiArea->setViewMode(QMdiArea::SubWindowView);
        m_mdiArea->tileSubWindows();
    });
    m_ppMenuOperation->addSeparator();
    m_ppMenuOperation->addAction(tr("&Point Values"), QKeySequence(), this, &MainWindow::onPPPointMode);
    m_ppMenuOperation->addAction(tr("&Contours"), QKeySequence(), this, &MainWindow::onPPContourMode);
    m_ppMenuOperation->addAction(tr("&Areas"), QKeySequence(), this, &MainWindow::onPPAreaMode);
    m_ppMenuOperation->addSeparator();
    m_ppMenuOperation->addAction(tr("Plot &X-Y..."), QKeySequence(), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showXYPlotDialog(this); return; }
        auto *r = activeResults(); if (r) { r->showXYPlotDialog(this); return; }
    });
    m_ppMenuOperation->addAction(tr("&Integrate..."), QKeySequence(), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showIntegrateDialog(this, outputDialog()); return; }
        auto *r = activeResults(); if (r) { r->showIntegrateDialog(this, outputDialog()); return; }
    });

    // Window and Help are added separately and always visible
    // (they're created at the end of createCommonMenus or here)

    // --- Top toolbar ---
    m_ppTopBar = addToolBar(tr("Postprocessor"));
    m_ppTopBar->setIconSize(QSize(16, 15));
    m_ppTopBar->setMovable(false);

    m_ppModeGroup = new QActionGroup(this);
    m_ppModeGroup->setExclusive(true);

    m_ppActPoint = m_ppTopBar->addAction(icon("pp_point"), tr("Point Values"));
    m_ppActPoint->setCheckable(true); m_ppActPoint->setChecked(true);
    m_ppActPoint->setToolTip(tr("Point values mode — click to query fields"));
    m_ppModeGroup->addAction(m_ppActPoint);
    connect(m_ppActPoint, &QAction::triggered, this, &MainWindow::onPPPointMode);

    m_ppActContour = m_ppTopBar->addAction(icon("pp_contour"), tr("Contour"));
    m_ppActContour->setCheckable(true);
    m_ppActContour->setToolTip(tr("Contour mode — define path for line integrals"));
    m_ppModeGroup->addAction(m_ppActContour);
    connect(m_ppActContour, &QAction::triggered, this, &MainWindow::onPPContourMode);

    m_ppActArea = m_ppTopBar->addAction(icon("pp_area"), tr("Area"));
    m_ppActArea->setCheckable(true);
    m_ppActArea->setToolTip(tr("Area mode — select blocks for integration"));
    m_ppModeGroup->addAction(m_ppActArea);
    connect(m_ppActArea, &QAction::triggered, this, &MainWindow::onPPAreaMode);

    m_ppTopBar->addSeparator();
    m_ppTopBar->addAction(icon("pp_plot"), tr("Plot X-Y"), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showXYPlotDialog(this); return; }
        auto *r = activeResults(); if (r) { r->showXYPlotDialog(this); return; }
    })->setToolTip(tr("Plot field values along contour"));
    m_ppTopBar->addAction(icon("pp_integrate"), tr("Integrate"), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showIntegrateDialog(this, outputDialog()); return; }
        auto *r = activeResults(); if (r) { r->showIntegrateDialog(this, outputDialog()); return; }
    })->setToolTip(tr("Line or block integral"));
    m_ppCircPropsTB = m_ppTopBar->addAction(icon("pp_circprops"), tr("Circuit Props"), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showCircuitDialog(this); return; }
        auto *r = activeResults(); if (r) { r->showCircuitDialog(this); return; }
    });

    m_ppTopBar->addSeparator();
    m_ppMeshTB = m_ppTopBar->addAction(icon("pp_showmesh"), tr("Show Mesh"));
    m_ppMeshTB->setCheckable(true);
    m_ppMeshTB->setToolTip(tr("Toggle mesh display"));
    connect(m_ppMeshTB, &QAction::toggled, [this](bool on) {
        auto *r = activeResults(); if (r) r->setShowMesh(on);
        else { auto *s = activeScalarResults(); if (s) s->setShowMesh(on); }
        m_ppActMesh->setChecked(on);
    });

    m_ppTopBar->addAction(icon("pp_cplot"), tr("Contour Plot..."), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showContourPlotDialog(this); m_ppActContours->setChecked(s->showContours()); return; }
        auto *r = activeResults(); if (r) { r->showContourPlotDialog(this); m_ppActContours->setChecked(r->showContours()); return; }
    })->setToolTip(tr("Configure contour plot"));
    m_ppTopBar->addAction(icon("pp_dplot"), tr("Density Plot..."), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showDensityPlotDialog(this); m_ppActDensity->setChecked(s->showDensity()); return; }
        auto *r = activeResults(); if (r) { r->showDensityPlotDialog(this); m_ppActDensity->setChecked(r->showDensity()); return; }
    })->setToolTip(tr("Configure density plot"));
    m_ppTopBar->addAction(icon("pp_vplot"), tr("Vector Plot..."), [this]() {
        auto *s = activeScalarResults(); if (s) { s->showVectorPlotDialog(this); return; }
        auto *r = activeResults();
        if (r) {
            r->showVectorPlotDialog(this);
            if (r->m_showVectors) m_ppActDensity->setChecked(false);
        }
    })->setToolTip(tr("Configure vector plot"));
}

// ================================================================
// Left toolbars (always visible — Script + Navigation)
// ================================================================

void MainWindow::createLeftToolbars()
{
    m_leftScript = new QToolBar(tr("Script"), this);
    m_leftScript->setOrientation(Qt::Vertical);
    m_leftScript->setMovable(false);
    m_leftScript->setIconSize(QSize(16, 15));
    addToolBar(Qt::LeftToolBarArea, m_leftScript);
    m_leftScript->addAction(icon("lua_script"), tr("Python Script"), [this]() {
        QSettings settings;
        QString lastDir = settings.value("lastDirectory", QString()).toString();
        QString path = QFileDialog::getOpenFileName(this, "Open Python Script", lastDir,
            "Python Scripts (*.py);;All Files (*)");
        if (path.isEmpty()) return;
        settings.setValue("lastDirectory", QFileInfo(path).absolutePath());
        QString scriptPath = path; scriptPath.replace('\\', '/');
        QString scriptDir = QFileInfo(path).absolutePath();
        scriptDir.replace('\\', '/');
        std::string chdirCmd = "setcurrentdirectory(\"" + scriptDir.toStdString() + "\")";
        char errmsg[1024] = {};
        exec_python(chdirCmd.c_str(), errmsg, sizeof(errmsg));
        updateStatus(QString("Running %1...").arg(QFileInfo(path).fileName()));
        if (exec_python_file(scriptPath.toStdString().c_str(), errmsg, sizeof(errmsg)) != 0) {
            if (strstr(errmsg, "KeyboardInterrupt")) {
                updateStatus("Script interrupted");   // user pressed Esc
                QMessageBox::information(this, tr("Script Interrupted"),
                                         tr("Script interrupted."));
            } else {
                updateStatus("Script failed");        // clear "Running..." before the dialog
                QMessageBox::warning(this, tr("Script Error"), QString::fromUtf8(errmsg));
            }
        } else {
            updateStatus("Script completed");
        }
    });
    m_leftScript->addAction(icon("lua_console"), tr("Python Console"), [this]() {
        showPyConsole();
    });

    m_leftNav = new QToolBar(tr("Navigation"), this);
    m_leftNav->setOrientation(Qt::Vertical);
    m_leftNav->setMovable(false);
    m_leftNav->setIconSize(QSize(16, 15));
    addToolBar(Qt::LeftToolBarArea, m_leftNav);

    // These work for both preprocessor and postprocessor (magnetics + scalar)
    m_leftNav->addAction(icon("zoomin"), tr("Zoom In"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->zoomIn(); else if (r) r->zoomIn(); else if (s) s->zoomIn();
    });
    m_leftNav->addAction(icon("zoomout"), tr("Zoom Out"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->zoomOut(); else if (r) r->zoomOut(); else if (s) s->zoomOut();
    });
    m_leftNav->addAction(icon("zoomnatural"), tr("Zoom Fit"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->zoomFit(); else if (r) r->zoomFit(); else if (s) s->zoomFit();
    });
    m_leftNav->addAction(icon("zoomwindow"), tr("Zoom Window"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->zoomWindow(); else if (r) r->zoomWindow(); else if (s) s->zoomWindow();
    });
    m_leftNav->addSeparator();
    m_leftNav->addAction(icon("panup"), tr("Pan Up"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->panUp(); else if (r) r->panUp(); else if (s) s->panUp();
    });
    m_leftNav->addAction(icon("pandown"), tr("Pan Down"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->panDown(); else if (r) r->panDown(); else if (s) s->panDown();
    });
    m_leftNav->addAction(icon("panleft"), tr("Pan Left"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->panLeft(); else if (r) r->panLeft(); else if (s) s->panLeft();
    });
    m_leftNav->addAction(icon("panright"), tr("Pan Right"), [this]() {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->panRight(); else if (r) r->panRight(); else if (s) s->panRight();
    });
    m_leftNav->addSeparator();
    m_showGridTB = m_leftNav->addAction(icon("showgrid"), tr("Show Grid"));
    m_showGridTB->setCheckable(true);
    m_showGridTB->setChecked(QSettings().value("editor/showGrid", true).toBool());
    connect(m_showGridTB, &QAction::toggled, [this](bool on) {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->setShowGrid(on); else if (r) r->setShowGrid(on); else if (s) s->setShowGrid(on);
        if (m_showGridMenu && m_showGridMenu->isChecked() != on) {
            m_showGridMenu->blockSignals(true);
            m_showGridMenu->setChecked(on);
            m_showGridMenu->blockSignals(false);
        }
    });
    m_snapGridTB = m_leftNav->addAction(icon("snapgrid"), tr("Snap Grid"));
    m_snapGridTB->setCheckable(true);
    m_snapGridTB->setChecked(QSettings().value("editor/snapToGrid", false).toBool());
    connect(m_snapGridTB, &QAction::toggled, [this](bool on) {
        auto *e = activeEditor(); auto *r = activeResults(); auto *s = activeScalarResults();
        if (e) e->setSnapToGrid(on); else if (r) r->setSnapToGrid(on); else if (s) s->setSnapToGrid(on);
        if (m_snapGridMenu && m_snapGridMenu->isChecked() != on) {
            m_snapGridMenu->blockSignals(true);
            m_snapGridMenu->setChecked(on);
            m_snapGridMenu->blockSignals(false);
        }
    });
    m_leftNav->addAction(icon("gridsize"), tr("Grid Size"),
                         [this]() { showGridPropertiesDialog(); });

    // Add Window and Help menus
    m_windowMenu = menuBar()->addMenu(tr("&Window"));
    {
        // Appearance: follow the system, or force light / dark drawing colours
        QMenu *ap = m_windowMenu->addMenu(tr("&Appearance"));
        auto *grp = new QActionGroup(this);
        const char *names[] = {QT_TR_NOOP("Follow System"), QT_TR_NOOP("Light"), QT_TR_NOOP("Dark")};
        const int cur = QSettings().value("ui/theme", 0).toInt();
        for (int i = 0; i < 3; i++) {
            QAction *a = ap->addAction(tr(names[i]));
            a->setCheckable(true);
            a->setChecked(i == cur);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, i]() {
                QSettings().setValue("ui/theme", i);
                applyTheme();
            });
        }
        // Language: takes effect at the next start
        QMenu *lm = m_windowMenu->addMenu(tr("&Language"));
        auto *lg = new QActionGroup(this);
        const QString curLang = QSettings().value("ui/language").toString();
        const struct { const char *code; const char *name; } langs[] = {
            {"", QT_TR_NOOP("System Default")}, {"en", "English"}, {"de", "Deutsch"}};
        for (const auto &l : langs) {
            QAction *a = lm->addAction(l.code[0] ? QString::fromUtf8(l.name) : tr(l.name));
            a->setCheckable(true);
            a->setChecked(curLang == QLatin1String(l.code));
            lg->addAction(a);
            const QString code = QString::fromLatin1(l.code);
            connect(a, &QAction::triggered, this, [this, code]() {
                QSettings().setValue("ui/language", code);
                QMessageBox::information(this, tr("Language"),
                    tr("The new language is used from the next start of FEMM."));
            });
        }
        m_windowMenu->addSeparator();
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() { applyTheme(); });
    }
    QMenu *window = m_windowMenu;
    window->addAction(tr("&Cascade"), QKeySequence(), [this]() { m_mdiArea->cascadeSubWindows(); });
    window->addAction(tr("&Tile"), QKeySequence(), [this]() {
        m_mdiArea->setViewMode(QMdiArea::SubWindowView);
        m_mdiArea->tileSubWindows();
    });
    window->addAction(tr("T&abs"), QKeySequence(), [this]() {
        m_mdiArea->setViewMode(QMdiArea::TabbedView);
        for (auto *sub : m_mdiArea->subWindowList()) sub->showMaximized();
    });
    QAction *listStart = window->addSeparator();
    // Dynamically populate with open windows when menu is shown
    connect(window, &QMenu::aboutToShow, [this, window, listStart]() {
        // Remove old dynamic entries: everything after the separator
        auto actions = window->actions();
        const int first = actions.indexOf(listStart) + 1;
        for (int i = first; i > 0 && i < actions.size(); i++)
            window->removeAction(actions[i]);
        // Add current subwindows
        auto subs = m_mdiArea->subWindowList();
        for (int i = 0; i < subs.size(); i++) {
            QString title = subs[i]->windowTitle();
            QAction *a = window->addAction(QString("%1. %2").arg(i + 1).arg(title));
            a->setCheckable(true);
            a->setChecked(subs[i] == m_mdiArea->activeSubWindow());
            connect(a, &QAction::triggered, [this, sub = subs[i]]() {
                m_mdiArea->setActiveSubWindow(sub);
                sub->showMaximized();
            });
        }
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Getting Started"), QKeySequence(), [this]() { m_gettingStarted->show(); m_gettingStarted->raise(); });
    // Cmd+K (Ctrl+K): run any command by name
    help->addAction(tr("Command Palette..."), QKeySequence(Qt::CTRL | Qt::Key_K), this, [this]() {
        CommandPalette pal(menuBar(), this);
        const QRect g = geometry();
        pal.adjustSize();
        pal.move(g.center().x() - pal.width() / 2, g.top() + 80);
        pal.exec();
    });
    help->addSeparator();
    help->addAction(tr("&Manual"), QKeySequence(Qt::Key_F1), [this]() {
        // Beside the executable on Windows and Linux, where the installer and
        // the build scripts put it; Contents/Resources/ in a macOS bundle,
        // because a document in Contents/MacOS/ is sealed as nested code by
        // codesign; and ../share/femm for a split (FHS) install. femmResource
        // knows all three, and is the same search python/, the PyBridge worker
        // and the material-library originals use.
        const QString pdfPath =
            QString::fromStdString(femmResource("manual.pdf"));
        if (QFile::exists(pdfPath))
            QDesktopServices::openUrl(QUrl::fromLocalFile(pdfPath));
        else
            QMessageBox::warning(this, tr("Manual Not Found"),
                tr("Could not find manual.pdf in the application directory."));
    });
    help->addSeparator();
    help->addAction(tr("&About"), QKeySequence(), [this]() {
        // The build number is what tells one build of a version from another
        // in a bug report -- the job FEMM 4.2's About box gave __DATE__. See
        // common/build_info.h; the title stays the bare version so the window
        // caption does not move about from build to build.
        showAbout(this, QString("femm %1").arg(femm_version()),
            QString("Finite Element Method Magnetics, Version %1 (build %2)\n")
                .arg(femm_version(), femm_build()) +
            "Copyright © 2026 David Meeker and contributors\n\n"
            "The femm-qt application (this GUI, the libfemm engine, the IPC "
            "server, the tangle mesher, and the language bindings) is released "
            "under the MIT License.\n\n"
            "The finite element field solvers (fkn, belasolv, hsolv, csolv) are "
            "released under the GNU General Public License, version 3 or later. "
            "This software is provided WITHOUT ANY WARRANTY, to the extent "
            "permitted by law. See the bundled LICENSE and COPYING files for the "
            "full terms.\n\n"
            "This program uses the Qt framework under the GNU Lesser General "
            "Public License v3. See Help > About Qt for details.\n\n"
            "This program embeds MicroPython 1.28.0 (Copyright © 2013-2026 "
            "Damien P. George and contributors) and ulab (Copyright © 2019 "
            "Zoltán Vörös and contributors), both under the MIT License. See "
            "micropython/LICENSE.micropython and "
            "micropython/ulab/LICENSE for details.\n\n"
            "MicroPython's memory is managed by the Boehm-Demers-Weiser "
            "garbage collector (Copyright © 1988-2025 Hans-J. Boehm, Alan J. "
            "Demers, Xerox Corporation, Silicon Graphics, Hewlett-Packard and "
            "Ivan Maidanski), under an X11-style license. See "
            "LICENSE-bdwgc.txt for details.\n\n"
            "The pure-Python standard-library modules are from micropython-lib "
            "(Copyright © 2013-2026 micropython-lib contributors), under the "
            "MIT License. See LICENSE-micropython-lib.txt for details.\n\n"
            "It also uses a general complex eigen-decomposition adapted from "
            "melihaltun/complexEigenPairSolver (Copyright © 2018 melihaltun), "
            "under the MIT License. See LICENSE-melihaltun.txt for details.\n\n"
            "The bundled Lua 4 client uses Lua (Copyright © 1994-2026 Lua.org, "
            "PUC-Rio) and LuaSocket (Copyright © 2004-2007 Diego Nehab). See "
            "Lua-COPYRIGHT.txt and LuaSocket-COPYRIGHT.txt for details.");
    });
    help->addAction(tr("About &Qt"), QKeySequence(), []() {
        QApplication::aboutQt();
    });
}

void MainWindow::createStatusBar()
{
    m_coordLabel = new QLabel("(x=0, y=0)");
    m_coordLabel->setMinimumWidth(250);
    statusBar()->addWidget(m_coordLabel);
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel, 1);
    if (m_server && m_server->isListening()) {
        QLabel *ipcLabel = new QLabel(QString("IPC: localhost:%1").arg(m_server->port()));
        statusBar()->addPermanentWidget(ipcLabel);
    }
}

void MainWindow::onCreateRadius()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;
    auto *doc = e->document();
    int nodeIdx = -1;
    for (int i = 0; i < (int)doc->nodelist.size(); i++) {
        if (doc->nodelist[i].IsSelected && doc->CanCreateRadius(i)) {
            nodeIdx = i; break;
        }
    }
    if (nodeIdx < 0) {
        QMessageBox::information(this, tr("Create Radius"),
            tr("Select a node at the junction of exactly two lines/arcs."));
        return;
    }
    QDialog rdlg(this);
    rdlg.setWindowTitle(tr("Create Radius"));
    auto *rlay = new QHBoxLayout(&rdlg);
    rlay->addWidget(new QLabel(tr("Radius:")));
    auto *rEdit = new PyLineEdit; rEdit->setText("1"); rlay->addWidget(rEdit);
    auto *rbtns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(rbtns, &QDialogButtonBox::accepted, &rdlg, &QDialog::accept);
    connect(rbtns, &QDialogButtonBox::rejected, &rdlg, &QDialog::reject);
    rlay->addWidget(rbtns);
    if (rdlg.exec() != QDialog::Accepted) return;
    double r = rEdit->evaluateAsDouble();
    if (r <= 0) return;
    doc->pushUndo();
    if (!doc->CreateRadius(nodeIdx, r))
        QMessageBox::warning(this, tr("Create Radius"),
            tr("Could not make a radius of the prescribed dimension."));
    e->update();
}

void MainWindow::onMove()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;
    CopyMoveDialog dlg(true, this);
    if (dlg.exec() != QDialog::Accepted) return;
    e->document()->pushUndo();
    if (dlg.isTranslate())
        e->document()->TranslateMove(dlg.dx(), dlg.dy(), e->editAction());
    else
        e->document()->RotateMove(CComplex(dlg.aboutX(), dlg.aboutY()),
                                  dlg.angle(), e->editAction());
    e->invalidateMeshDisplay();
    e->update();
}

void MainWindow::onCopy()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;
    CopyMoveDialog dlg(false, this);
    if (dlg.exec() != QDialog::Accepted) return;
    e->document()->pushUndo();
    if (dlg.isTranslate())
        e->document()->TranslateCopy(dlg.dx(), dlg.dy(), dlg.copies(), e->editAction());
    else
        e->document()->RotateCopy(CComplex(dlg.aboutX(), dlg.aboutY()),
                                  dlg.angle(), dlg.copies(), e->editAction());
    e->update();
}

void MainWindow::onAirSpace()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;
    DocumentBase *d = e->document();
    static const char *prefix[] = {"mi", "ei", "hi", nullptr};
    static const char *air[] = {"Air", "Air (dry, CO2-free)", "Air", nullptr};
    const int pt = d->physicsType();
    if (pt < 0 || pt > 2) {
        QMessageBox::information(this, tr("Air Space"), tr("Open boundaries are available for magnetics, electrostatics and heat flow."));
        return;
    }
    double x0, y0, x1, y1;
    if (!d->getBoundingBox(x0, y0, x1, y1)) {
        QMessageBox::information(this, tr("Air Space"), tr("Draw the model first."));
        return;
    }
    d->pushUndo();
    char err[512] = {};
    if (exec_python((QString(prefix[pt]) + "_makeABC()").toUtf8().constData(), err, sizeof err) != 0) {
        QMessageBox::warning(this, tr("Air Space"), QString::fromUtf8(err));
        return;
    }
    // Fill the gap(s) between the model and the shells with air
    if (!ensureMaterial(e, QString::fromUtf8(air[pt]))) return;
    const int filled = e->fillUnlabeledOutside(QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized(), air[pt]);
    e->zoomFit();
    e->update();
    updateStatus(tr("Open boundary added; %n region(s) filled with air.", "", filled));
}

void MainWindow::onPattern()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;
    DocumentBase *d = e->document();
    bool any = false;
    for (const auto &n : d->nodelist) any |= n.IsSelected;
    for (const auto &s2 : d->linelist) any |= s2.IsSelected;
    for (const auto &a : d->arclist) any |= a.IsSelected;
    for (const auto &b : d->blocklist) any |= b.IsSelected;
    if (!any) {
        QMessageBox::information(this, tr("Pattern"),
            tr("Select what should be repeated first (Select tool: click or drag a window)."));
        return;
    }
    PatternDialog dlg(e, this);
    // Keep the drawing visible beside the dialog
    dlg.move(geometry().right() - dlg.sizeHint().width() - 40, geometry().top() + 120);
    if (dlg.exec() == QDialog::Accepted) dlg.apply();
}

void MainWindow::showEditorContextMenu(EditorWidget *e, const QPoint &pos)
{
    QMenu m(this);
    m.addAction(tr("Properties..."), [e]() {
        // The dialog for what is selected: labels, else lines/arcs, else nodes
        const DocumentBase *d = e->document();
        bool lab = false, edge = false;
        for (const auto &b : d->blocklist) lab |= b.IsSelected;
        for (const auto &s2 : d->linelist) edge |= s2.IsSelected;
        for (const auto &a : d->arclist) edge |= a.IsSelected;
        e->openSelectedAs(lab ? 2 : edge ? 1 : 0);
    });
    m.addAction(tr("Delete"), [e]() { e->deleteSelected(); });
    m.addSeparator();
    m.addAction(tr("Move..."), this, &MainWindow::onMove);
    m.addAction(tr("Copy..."), this, &MainWindow::onCopy);
    m.addAction(tr("Pattern..."), this, &MainWindow::onPattern);
    m.addAction(tr("Mirror..."), this, &MainWindow::onMirror);
    m.addAction(tr("Scale..."), this, &MainWindow::onScale);
    m.addAction(tr("Create Radius..."), this, &MainWindow::onCreateRadius);
    m.addSeparator();
    m.addAction(tr("Offset"), [e]() { e->setSketchTool(EditorWidget::SketchOffset); });
    m.addAction(tr("Trim"), [e]() { e->setSketchTool(EditorWidget::SketchTrim); });
    m.addAction(tr("Dimension"), [e]() { e->setSketchTool(EditorWidget::SketchDimension); });
    m.addSeparator();
    m.addAction(tr("Extend"), [e]() { e->setSketchTool(EditorWidget::SketchExtend); });
    m.addSeparator();
    m.addAction(tr("Zoom to Selection"), [e]() { e->zoomSelection(); });
    m.addAction(tr("Zoom to Fit"), [e]() { e->zoomFit(); });
    m.exec(pos);
}

void MainWindow::onScale()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;

    // Default the base point to the center of the bounding box of the SELECTED
    // geometry, so scaling grows/shrinks about what is being scaled rather than
    // about the origin. Matches OF (cdrawView.cpp OnEditScale).
    double cx = 0, cy = 0;
    double xmin, ymin, xmax, ymax;
    if (e->document()->getBoundingBox(xmin, ymin, xmax, ymax, true)) {
        cx = (xmin + xmax) / 2.0;
        cy = (ymin + ymax) / 2.0;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Scale"));
    auto *lay = new QGridLayout(&dlg);
    lay->addWidget(new QLabel(tr("Base point, x:")), 0, 0);
    auto *bx = new PyLineEdit; bx->setText(QString::number(cx, 'g', 10)); lay->addWidget(bx, 0, 1);
    lay->addWidget(new QLabel(tr("Base point, y:")), 1, 0);
    auto *by = new PyLineEdit; by->setText(QString::number(cy, 'g', 10)); lay->addWidget(by, 1, 1);
    lay->addWidget(new QLabel(tr("Scale factor:")), 2, 0);
    auto *sf = new PyLineEdit; sf->setText("1"); lay->addWidget(sf, 2, 1);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns, 3, 0, 1, 2);

    if (dlg.exec() != QDialog::Accepted) return;
    e->document()->pushUndo();
    e->document()->ScaleMove(bx->evaluateAsDouble(), by->evaluateAsDouble(),
                             sf->evaluateAsDouble(1.0), e->editAction());
    e->invalidateMeshDisplay();
    e->update();
}

void MainWindow::onMirror()
{
    auto *e = activeEditor(); if (!e || !e->document()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Mirror"));

    // Two points, each as a labelled group holding side-by-side x and y edits
    // (matching OF's IDD_MIRRORDLG).
    auto *ax = new PyLineEdit; ax->setText("0");
    auto *ay = new PyLineEdit; ay->setText("0");
    auto *bxe = new PyLineEdit; bxe->setText("0");
    auto *bye = new PyLineEdit; bye->setText("0");

    auto makePointGroup = [](const QString &title, PyLineEdit *xEdit, PyLineEdit *yEdit) {
        auto *box = new QGroupBox(title);
        auto *row = new QHBoxLayout(box);
        row->addWidget(xEdit);
        row->addWidget(yEdit);
        return box;
    };

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(makePointGroup(tr("First point on line of symmetry"), ax, ay));
    lay->addWidget(makePointGroup(tr("Second point on line of symmetry"), bxe, bye));
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;
    e->document()->pushUndo();
    e->document()->MirrorSelected(ax->evaluateAsDouble(), ay->evaluateAsDouble(),
                                  bxe->evaluateAsDouble(), bye->evaluateAsDouble(),
                                  e->editAction());
    e->invalidateMeshDisplay();
    e->update();
}

void MainWindow::onDeleteSelected()
{
    // Toolbar Delete button. Same implementation as Edit|Delete Selected and
    // as Backspace in the drawing, so the three cannot drift apart.
    auto *e = activeEditor();
    if (e) e->deleteSelected();
}

void MainWindow::showPyConsole()
{
    // Constructed eagerly in the ctor; just reveal it.
    m_pyConsole->show();
    m_pyConsole->raise();
}

void MainWindow::hidePyConsole()
{
    if (m_pyConsole) m_pyConsole->hide();
}

void MainWindow::clearPyConsole()
{
    if (m_pyConsole) m_pyConsole->clearOutput();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // macOS never puts a document on the command line. Double-clicking a .fem
    // in Finder, or dropping one on the Dock icon, arrives as a QFileOpenEvent
    // on the application instead -- at launch as well as while running -- and
    // the Info.plist already registers us as the editor for all four problem
    // types, so without this Finder starts FEMM and the file is dropped on the
    // floor. Deferred for the same reason as dropEvent. Other platforms never
    // send it.
    if (event->type() == QEvent::FileOpen && obj == qApp) {
        const QString f = static_cast<QFileOpenEvent *>(event)->file();
        if (!f.isEmpty() && femmOpenable(f)) {
            QTimer::singleShot(0, this, [this, f]() {
                if (!py_is_running()) openPath(f);
            });
            return true;
        }
    }
    // While a script is running, Esc interrupts it (application-wide). The GUI
    // is otherwise blocked in the interpreter; PyConsole's VM-loop event pump
    // dispatches this keypress, which raises KeyboardInterrupt at the next VM
    // branch. Esc carries no modifier, so it doesn't shadow Ctrl+C/Copy, and
    // this only fires while a script is actually running.
    // Closing an editor tab with unsaved changes asks first -- but never when
    // a script (console, Run Script, or an IPC client such as pyfemm) closes
    // it: mi_close() must not block on a dialog nobody is there to answer.
    if (event->type() == QEvent::Close && !m_closingAll && !py_is_running()) {
        if (auto *sub = qobject_cast<QMdiSubWindow *>(obj)) {
            if (!confirmDiscard(sub)) {
                event->ignore();
                return true;
            }
        }
    }
    if (event->type() == QEvent::KeyPress && py_is_running()) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            py_request_interrupt();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ================================================================
// Unsaved changes
// ================================================================

bool MainWindow::documentDiffersFromDisk(DocumentBase *doc) const
{
    if (!doc) return false;
    if (doc->filePath.empty())
        return !doc->nodelist.empty() || !doc->linelist.empty() ||
               !doc->arclist.empty() || !doc->blocklist.empty() ||
               doc->numMaterials() > 0 || doc->numBoundaries() > 0 ||
               doc->numCircuits() > 0;

    QFile onDisk(QString::fromStdString(doc->filePath));
    if (!onDisk.open(QIODevice::ReadOnly)) return true;

    QTemporaryFile tmp(QDir::tempPath() + "/femm_cmp_XXXXXX" +
                       QString::fromStdString(doc->fileExtension()));
    if (!tmp.open()) return true;
    tmp.close();
    if (!doc->SaveDocument(tmp.fileName().toStdString())) return true;
    QFile current(tmp.fileName());
    if (!current.open(QIODevice::ReadOnly)) return true;
    return current.readAll() != onDisk.readAll();
}

bool MainWindow::confirmDiscard(QMdiSubWindow *sub)
{
    auto *editor = sub ? qobject_cast<EditorWidget *>(sub->widget()) : nullptr;
    if (!editor || !documentDiffersFromDisk(editor->document())) return true;

    m_mdiArea->setActiveSubWindow(sub);
    QString name = sub->windowTitle();
    if (name.endsWith(QStringLiteral(" •"))) name.chop(2);
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("Do you want to save the changes to \"%1\"?").arg(name));
    box.setInformativeText(tr("Your changes will be lost if you don't save them."));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
    case QMessageBox::Save:
        onSave();
        // Save As may have been cancelled, or the write may have failed.
        return !documentDiffersFromDisk(editor->document());
    case QMessageBox::Discard:
        removeAutosave(editor);
        return true;
    default:
        return false;
    }
}

QString MainWindow::autosavePathFor(EditorWidget *editor) const
{
    if (m_autosaveDir.isEmpty() || !editor || !editor->document()) return QString();
    QString id = editor->property("femmAutosaveId").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::Id128).left(12);
        editor->setProperty("femmAutosaveId", id);
    }
    return m_autosaveDir + "/" + id + QString::fromStdString(editor->document()->fileExtension());
}

void MainWindow::removeAutosave(EditorWidget *editor)
{
    const QString p = autosavePathFor(editor);
    if (p.isEmpty()) return;
    QFile::remove(p);
    QFile::remove(p + ".json");
}

void MainWindow::autosaveNow()
{
    if (py_is_running()) return;   // never in the middle of a script
    for (auto *sub : m_mdiArea->subWindowList()) {
        auto *editor = qobject_cast<EditorWidget *>(sub->widget());
        if (!editor || !editor->document()) continue;
        DocumentBase *doc = editor->document();
        if (!documentDiffersFromDisk(doc)) { removeAutosave(editor); continue; }
        const QString p = autosavePathFor(editor);
        if (p.isEmpty() || !doc->SaveDocument(p.toStdString())) continue;
        QJsonObject meta;
        meta["original"] = QString::fromStdString(doc->filePath);
        meta["title"] = sub->windowTitle();
        meta["time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        QFile f(p + ".json");
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(meta).toJson());
    }
}

void MainWindow::offerRecovery()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave";
    struct Item { QString file, original, title, time, dir; };
    QList<Item> items;
    QStringList deadDirs;
    for (const QString &d : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = base + "/" + d;
        if (dir == m_autosaveDir) continue;
        QLockFile lock(dir + "/instance.lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) continue;   // another FEMM is still running there
        lock.unlock();
        deadDirs << dir;
        for (const QString &f : QDir(dir).entryList({"*.fem", "*.fee", "*.feh", "*.fec"}, QDir::Files)) {
            QFile mf(dir + "/" + f + ".json");
            QJsonObject meta;
            if (mf.open(QIODevice::ReadOnly)) meta = QJsonDocument::fromJson(mf.readAll()).object();
            items.push_back({dir + "/" + f, meta["original"].toString(), meta["title"].toString(),
                             meta["time"].toString(), dir});
        }
    }
    if (items.isEmpty()) {
        for (const QString &d : deadDirs) QDir(d).removeRecursively();
        return;
    }
    QStringList names;
    for (const auto &it : items)
        names << QStringLiteral("\u2022 %1 (%2)").arg(it.original.isEmpty() ? tr("untitled") : QFileInfo(it.original).fileName(),
                                                      QDateTime::fromString(it.time, Qt::ISODate).toString("dd.MM. HH:mm"));
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setText(tr("FEMM did not close normally last time."));
    box.setInformativeText(tr("These models had unsaved changes:\n%1\n\nRecover them?").arg(names.join("\n")));
    QPushButton *recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(recover);
    box.exec();
    if (box.clickedButton() == recover) {
        for (const auto &it : items) {
            openPath(it.file);
            auto *e = activeEditor();
            if (!e || !e->document()) continue;
            // Back under its own name, but still unsaved
            e->document()->filePath = it.original.toStdString();
            for (auto *sub : m_mdiArea->subWindowList())
                if (sub->widget() == e)
                    sub->setWindowTitle(it.original.isEmpty() ? tr("Recovered") : QFileInfo(it.original).fileName());
            e->document()->pushUndo();   // counts as modified
        }
    }
    for (const QString &d : deadDirs) QDir(d).removeRecursively();
}

void MainWindow::applyTheme()
{
    viewColors().load();
    for (auto *sub : m_mdiArea->subWindowList())
        if (sub->widget()) sub->widget()->update();
}

void MainWindow::refreshModifiedMarks()
{
    if (m_modelPanel && m_modelPanel->isVisible()) m_modelPanel->refresh(activeEditor());
    if (auto *e = activeEditor()) m_lastEditor = e;
    if (m_lastEditor) {
        bool alive = false;
        for (auto *sub : m_mdiArea->subWindowList()) alive |= sub->widget() == m_lastEditor;
        if (!alive) m_lastEditor = nullptr;
    }
    if (m_gettingStarted && m_gettingStarted->isVisible())
        m_gettingStarted->refresh(m_lastEditor, m_lastEditor && m_lastEditor->property("femmAnalyzedSerial").isValid());
    if (m_resultsPanel) {
        // Magnetics only (circuits); shown with a magnetics results tab
        auto *rw = (m_uiState == 1) ? activeResults() : nullptr;
        const bool want = rw && QSettings().value("ui/resultsPanel", true).toBool();
        if (want != m_resultsPanel->isVisible()) m_resultsPanel->setVisible(want);
        if (want) m_resultsPanel->refresh(rw->document());
    }

    // Results tabs whose model has been edited since it was analysed say so.
    static const QString outdated = tr(" (outdated)");
    QHash<QString, bool> stale;   // model base name -> edited since analysis
    for (auto *sub : m_mdiArea->subWindowList()) {
        auto *editor = qobject_cast<EditorWidget *>(sub->widget());
        if (!editor || !editor->document() || editor->document()->filePath.empty()) continue;
        const QVariant v = editor->property("femmAnalyzedSerial");
        if (!v.isValid()) continue;
        const QString base = QFileInfo(QString::fromStdString(editor->document()->filePath)).completeBaseName();
        stale[base] = stale.value(base) || v.toULongLong() != editor->document()->editSerial;
    }
    for (auto *sub : m_mdiArea->subWindowList()) {
        if (qobject_cast<EditorWidget *>(sub->widget())) continue;
        QString title = sub->windowTitle();
        const bool marked = title.endsWith(outdated);
        const QString base = marked ? title.left(title.size() - outdated.size()) : title;
        const bool isStale = stale.value(QFileInfo(base).completeBaseName(), false);
        if (isStale && !marked) sub->setWindowTitle(base + outdated);
        else if (!isStale && marked) sub->setWindowTitle(base);
    }

    static const QString mark = QStringLiteral(" •");
    for (auto *sub : m_mdiArea->subWindowList()) {
        auto *editor = qobject_cast<EditorWidget *>(sub->widget());
        if (!editor) continue;
        QString title = sub->windowTitle();
        const bool marked = title.endsWith(mark);
        const bool modified = editor->hasUnsavedEdits() ||
                              (editor->document() && editor->document()->filePath.empty() &&
                               documentDiffersFromDisk(editor->document()));
        if (modified && !marked) sub->setWindowTitle(title + mark);
        else if (!modified && marked) { title.chop(mark.size()); sub->setWindowTitle(title); }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (auto *sub : m_mdiArea->subWindowList()) {
        if (!confirmDiscard(sub)) {
            event->ignore();
            return;
        }
    }
    // Every document has been dealt with; the sub-windows must not ask again
    // as they are torn down.
    m_closingAll = true;
    // A clean exit leaves nothing to recover
    if (!m_autosaveDir.isEmpty()) {
        if (m_autosaveLock) m_autosaveLock->unlock();
        QDir(m_autosaveDir).removeRecursively();
    }
    event->accept();
}

OutputDialog *MainWindow::outputDialog()
{
    if (!m_outputDlg)
        m_outputDlg = new OutputDialog(this);
    return m_outputDlg;
}
