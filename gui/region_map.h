// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// region_map.h — the closed regions ("faces") of a preprocessor drawing.
//
// FEMM decides what a region is only when it meshes: every area enclosed by
// segments and arcs is one region, and the block label inside it says what
// material fills it. The editor used to show nothing of that -- just lines and
// a label dot -- so a missing or doubled label only surfaced as an error at
// mesh time.
//
// RegionMap recovers the regions directly from the geometry (a planar face
// traversal over segments and sampled arcs) so the editor can:
//   * fill each region with its material's colour,
//   * hatch regions that have no block label (or more than one),
//   * find the region under the cursor for the "assign material" tool.
//
// Regions are cached and rebuilt only when the geometry changes.
#pragma once

#include <QColor>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <vector>
#include <cstdint>

class DocumentBase;

class RegionMap
{
public:
    struct Face {
        QPolygonF poly;          // boundary, drawing coordinates, CCW
        double area = 0;
        QRectF bbox;
        std::vector<int> labels; // block labels whose innermost region this is
    };

    // Rebuilds what changed since the last call. Cheap when nothing did.
    void update(const DocumentBase &doc);

    // Faces, largest first (so painting in order leaves inner regions on top).
    const std::vector<Face> &faces() const { return m_faces; }

    // The innermost face containing (x, y), or -1 (outside every region).
    int faceAt(double x, double y) const;

private:
    void buildFaces(const DocumentBase &doc);
    void assignLabels(const DocumentBase &doc);

    std::vector<Face> m_faces;
    uint64_t m_geomHash = 0;
    uint64_t m_labelHash = 0;
    bool m_valid = false;
};

// Display colour for a material, by name: air is nearly clear, copper orange,
// steels grey, magnets red; anything else gets a stable pastel from its name.
QColor materialColor(const QString &name);
