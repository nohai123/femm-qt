// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// editor_widget.h — FEMM geometry editor widget
// Matches the original FEMM interaction model:
//   - EditAction selects the current mode (node/segment/arc/block/group)
//   - Left click creates the entity for the current mode
//   - Right click toggles selection of the nearest entity of that type
//   - Space bar opens property dialog for selected entities
//   - Middle-button drag pans the view
#ifndef EDITOR_WIDGET_H
#define EDITOR_WIDGET_H

#include <QWidget>
#include <QColor>
#include <QPointF>
#include <vector>
#include <functional>
#include "region_map.h"

class DocumentBase;

class EditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EditorWidget(DocumentBase *doc, QWidget *parent = nullptr);
    ~EditorWidget();

    DocumentBase *document() const { return m_doc; }

    // EditAction: 0=Node, 1=Segment, 2=Block Label, 3=Arc Segment, 4=Group
    void setEditAction(int action);
    int editAction() const { return m_editAction; }

    // Delete everything currently selected. Shared by Edit|Delete Selected
    // (bound to Delete) and by Backspace handled in keyPressEvent.
    void deleteSelected();

    // Zoom/pan
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomTo(double x0, double y0, double x1, double y1);
    void viewBounds(double &x0, double &y0, double &x1, double &y1) const;
    void zoomWindow();   // enter zoom-window mode
    void selectWindow(); // enter select-window mode
    void selectCircle(); // enter select-circle mode
    void panLeft();
    void panRight();
    void panUp();
    void panDown();

    // Grid
    void setShowGrid(bool on) { m_showGrid = on; update(); }
    bool showGrid() const { return m_showGrid; }
    void setSnapToGrid(bool on) { m_snapToGrid = on; }
    bool snapToGrid() const { return m_snapToGrid; }
    void setGridSize(double size) { m_gridSize = size; update(); }
    double gridSize() const { return m_gridSize; }

    // Display options
    void setShowNames(bool on) { m_showNames = on; update(); }
    bool showNames() const { return m_showNames; }
    void setShowMesh(bool on) { m_showMesh = on; update(); }
    bool showMesh() const { return m_showMesh; }

    // For operations that relocate existing geometry (Move/Scale/Mirror): the
    // mesh no longer conforms, but the node/segment/arc COUNTS are unchanged,
    // so the paint-time check cannot see it. Mesh data is kept -- Show Mesh
    // turns it back on. Callers repaint themselves.
    void invalidateMeshDisplay() { m_showMesh = false; }
    void loadMeshFromData(int numNodes, const double *nx, const double *ny,
                          int numTris, const int *triNodes, const int *triRegion);
    void clearMesh();
    int meshNodeCount() const { return (int)m_meshNodes.size(); }
    int meshEdgeCount() const { return (int)m_meshLines.size() + (int)m_greyMeshLines.size(); }
    int greyMeshLineCount() const { return (int)m_greyMeshLines.size(); }

    // CAD-style sketch tools (editor_sketch.cpp). While one is active, left
    // clicks place points anywhere -- nodes are created as needed -- instead
    // of following the classic node-then-connect workflow. The classic edit
    // action stays set (to Segment) so right-click selection, Move, Copy etc.
    // keep working on what the tool drew. setEditAction() leaves the tool.
    enum SketchTool { SketchNone = 0, SketchLine, SketchRect, SketchCircle, SketchArc3,
                      SketchFill,     // Fill: click a region to assign a material
                      SketchSelect }; // Select: click/drag to select, drag to move
    void setSketchTool(int tool);
    int sketchTool() const { return m_sketchTool; }

    // Object snap: endpoints, midpoints, arc centres, points on lines, plus
    // horizontal/vertical alignment with existing nodes.
    void setObjectSnap(bool on) { m_objectSnap = on; update(); }
    bool objectSnap() const { return m_objectSnap; }

    // Regions filled with their material colour; unlabelled regions hatched.
    void setFillRegions(bool on) { m_fillRegions = on; update(); }
    bool fillRegions() const { return m_fillRegions; }
    // Material the Fill tool assigns. ensureMaterial is called before it is
    // used and must make sure the document has it (e.g. from the library);
    // it returns false if that was not possible.
    void setFillMaterial(const QString &name) { m_fillMaterial = name; }
    QString fillMaterial() const { return m_fillMaterial; }
    void setEnsureMaterial(std::function<bool(const QString &)> fn) { m_ensureMaterial = std::move(fn); }
    // Region bookkeeping, for the problem check before meshing
    RegionMap &regions();

    // Modified tracking: the document's edit serial at the last save/open.
    void markSaved();
    bool hasUnsavedEdits() const;

    // Open properties for selected entities (space bar)
    void openSelected();
    // Same, for one entity type (0 nodes, 1 segments+arcs, 2 block labels)
    // regardless of the current mode. Used by the Model panel.
    void openSelectedAs(int action);

signals:
    void coordinatesChanged(double x, double y);
    void statusMessage(const QString &msg);
    void editActionChanged(int action);
    void sketchToolChanged(int tool);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override { (void)next; return false; }
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate transforms
    QPointF toScreen(double xd, double yd) const;
    void toDrawing(int xs, int ys, double &xd, double &yd) const;
    void applySnap(double &x, double &y) const;

    // Drawing helpers
    void drawGrid(QPainter &p);
    void drawNodes(QPainter &p);
    void drawSegments(QPainter &p);
    void drawArcSegments(QPainter &p);
    void drawBlockLabels(QPainter &p);
    void drawMesh(QPainter &p);

    DocumentBase *m_doc;
    unsigned long m_savedSerial = 0;

    // ---- Sketch tools / snapping (editor_sketch.cpp) ----
    enum SnapKind { SnapFree = 0, SnapGrid, SnapEndpoint, SnapMidpoint,
                    SnapCenter, SnapOnLine };
    struct Snap {
        double x = 0, y = 0;
        int kind = SnapFree;
        // Alignment guides: the snapped point shares x (or y) with this node
        bool alignX = false, alignY = false;
        double alignXFromY = 0, alignYFromX = 0;
    };
    Snap computeSnap(double x, double y, bool shift, bool forNode) const;
    void sketchPress(double x, double y, int snapKind);
    void sketchFinish();
    bool sketchKey(QKeyEvent *event);
    bool sketchTarget(double &x, double &y) const;   // cursor or typed values
    void drawSketchOverlay(QPainter &p);
    void drawSnapMarker(QPainter &p, const Snap &s);
    void emitSketchHint();
    int nodeAtOrNew(double x, double y);
    int pickNode(double x, double y) const;           // -1 if none near
    bool askPoint(const QString &title, double &x, double &y);
    double pickTolerance() const { return 10.0 / m_mag; }

    void fillPress(double x, double y);
    // Select tool (editor_sketch.cpp)
    bool selectPress(QMouseEvent *event);
    bool selectMove(QMouseEvent *event);
    bool selectRelease(QMouseEvent *event);
    void selectInRect(double x0, double y0, double x1, double y1, bool crossing);
    bool m_selPending = false;       // left button down in the select tool
    bool m_selDragging = false;      // ...and moving the selection
    bool m_selBand = false;          // ...or dragging a selection window
    QPoint m_selPressPos;
    double m_selX0 = 0, m_selY0 = 0; // press point, drawing coords
    std::vector<std::pair<int, QPointF>> m_dragNodes, m_dragLabels;  // originals
    void drawRegions(QPainter &p);
    RegionMap m_regions;
    bool m_fillRegions = true;
    QString m_fillMaterial;
    std::function<bool(const QString &)> m_ensureMaterial;

    int m_sketchTool = SketchNone;
    bool m_objectSnap = true;
    std::vector<QPointF> m_sketchPts;   // points placed so far (drawing coords)
    int m_chainStart = -1;              // first node of the current polyline
    Snap m_snap;                        // snap under the cursor
    bool m_hasCursor = false;
    QString m_input[2];                 // typed length/angle (or width/height)
    int m_inputField = 0;

    // View transform
    double m_ox = 0.0, m_oy = 0.0;
    double m_mag = 100.0;

    // Edit state
    int m_editAction = 0;   // 0=Node, 1=Seg, 2=Block, 3=Arc, 4=Group
    int m_firstPoint = -1;  // first node for segment/arc creation

    // Persistent arc dialog values (carry over between draws)
    double m_arcAngle = 90.0;
    double m_arcMaxSeg = 10.0;
    int m_arcBdryIdx = 0;   // 0 = "<None>"

    // Grid
    bool m_showGrid = true;
    bool m_snapToGrid = false;
    double m_gridSize = 1.0;

    // Display
    bool m_showNames = true;
    bool m_showMesh = false;

    // Mesh display (loaded from tangle output)
    struct MeshNode { double x, y; };
    struct MeshEdge { int n0, n1; };
    std::vector<MeshNode> m_meshNodes;
    std::vector<MeshEdge> m_meshLines;     // edges in defined regions (yellow)
    std::vector<MeshEdge> m_greyMeshLines; // edges in undefined regions (grey)

    // Geometry the displayed mesh was built against. If a node, segment or arc
    // is added or deleted the mesh no longer conforms, so it stops being shown
    // (OF does the same). Block labels are deliberately not counted: they do
    // not change the triangulation's shape.
    void hideMeshIfGeometryChanged();
    size_t m_meshGeomNodes = 0, m_meshGeomLines = 0, m_meshGeomArcs = 0;

    // Pan state (middle-button drag)
    bool m_panning = false;
    QPoint m_panStart;
    double m_panOxStart = 0.0, m_panOyStart = 0.0;

    // Zoom window / select window / select circle mode
    int m_zoomWndFlag = 0;     // 0=inactive, 1=waiting, 2=dragging
    int m_selectWndFlag = 0;
    int m_selectCircFlag = 0;
    double m_wndX0 = 0, m_wndY0 = 0;  // start corner/center in drawing coords
    QPoint m_rubberStart;              // start in screen coords

    // Current mouse position in drawing coords
    double m_mx = 0, m_my = 0;

    // Colors (matching original FEMM)
    static constexpr QColor nodeColor() { return QColor(0, 0, 0); }
    static constexpr QColor lineColor() { return QColor(0, 0, 255); }
    static constexpr QColor blockColor() { return QColor(0, 128, 0); }
    static constexpr QColor selColor() { return QColor(255, 0, 0); }
    static constexpr QColor gridColor() { return QColor(220, 220, 220); }
    static constexpr QColor nameColor() { return QColor(255, 128, 0); }
};

#endif // EDITOR_WIDGET_H
