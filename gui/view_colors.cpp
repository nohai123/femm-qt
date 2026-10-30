// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// view_colors.cpp — Centralized color table for FEMM drawing
// Default colors match the original FEMM StdAfx.h definitions.

#include "view_colors.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

bool femmDarkTheme()
{
    // ui/theme: 0 = follow the system, 1 = light, 2 = dark
    const int t = QSettings().value("ui/theme", 0).toInt();
    if (t == 1) return false;
    if (t == 2) return true;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// Global singleton
static ViewColors s_viewColors;

ViewColors &viewColors()
{
    static bool loaded = false;
    if (!loaded) {
        s_viewColors.load();
        loaded = true;
    }
    return s_viewColors;
}

void ViewColors::resetDefaults()
{
    // UI colors
    c[CI_Selected]      = QColor(255, 0, 0);
    c[CI_Background]    = QColor(255, 255, 255);
    c[CI_Mesh]          = QColor(213, 228, 20);  // dMeshColor from original FEMM
    c[CI_Block]         = QColor(0, 128, 0);
    c[CI_Line]          = QColor(0, 0, 255);
    c[CI_Grid]          = QColor(180, 180, 180);
    c[CI_Node]          = QColor(0, 0, 0);
    c[CI_Region]        = QColor(0, 128, 128);
    c[CI_Text]          = QColor(0, 0, 0);
    c[CI_RealFluxLine]  = QColor(0, 0, 0);
    c[CI_ImagFluxLine]  = QColor(0, 128, 0);
    c[CI_MaskLine]      = QColor(128, 0, 128);
    c[CI_Name]          = QColor(255, 128, 0);
    c[CI_RealVector]    = QColor(0, 0, 0);
    c[CI_ImagVector]    = QColor(128, 128, 128);

    // Density colormap (cyan → green → yellow → red → magenta)
    // Matching dColor00..dColor19 from femm42src/femm/StdAfx.h
    c[CI_Color00] = QColor(255,   0, 255);  // magenta (highest)
    c[CI_Color01] = QColor(255,  37, 195);
    c[CI_Color02] = QColor(255,  69, 147);
    c[CI_Color03] = QColor(255,  98, 108);
    c[CI_Color04] = QColor(255, 123,  76);
    c[CI_Color05] = QColor(255, 148,  51);
    c[CI_Color06] = QColor(255, 171,  31);
    c[CI_Color07] = QColor(255, 194,  16);
    c[CI_Color08] = QColor(255, 217,   6);
    c[CI_Color09] = QColor(255, 242,   1);
    c[CI_Color10] = QColor(242, 255,   1);
    c[CI_Color11] = QColor(217, 255,   6);
    c[CI_Color12] = QColor(194, 255,  16);
    c[CI_Color13] = QColor(171, 255,  31);
    c[CI_Color14] = QColor(148, 255,  51);
    c[CI_Color15] = QColor(123, 255,  76);
    c[CI_Color16] = QColor( 98, 255, 108);
    c[CI_Color17] = QColor( 69, 255, 147);
    c[CI_Color18] = QColor( 37, 255, 195);
    c[CI_Color19] = QColor(  0, 255, 255);  // cyan (lowest)

    // Greyscale
    c[CI_Grey00] = QColor( 55,  55,  55);
    c[CI_Grey01] = QColor( 65,  65,  65);
    c[CI_Grey02] = QColor( 75,  75,  75);
    c[CI_Grey03] = QColor( 85,  85,  85);
    c[CI_Grey04] = QColor( 95,  95,  95);
    c[CI_Grey05] = QColor(105, 105, 105);
    c[CI_Grey06] = QColor(115, 115, 115);
    c[CI_Grey07] = QColor(125, 125, 125);
    c[CI_Grey08] = QColor(135, 135, 135);
    c[CI_Grey09] = QColor(145, 145, 145);
    c[CI_Grey10] = QColor(155, 155, 155);
    c[CI_Grey11] = QColor(165, 165, 165);
    c[CI_Grey12] = QColor(175, 175, 175);
    c[CI_Grey13] = QColor(185, 185, 185);
    c[CI_Grey14] = QColor(195, 195, 195);
    c[CI_Grey15] = QColor(205, 205, 205);
    c[CI_Grey16] = QColor(215, 215, 215);
    c[CI_Grey17] = QColor(225, 225, 225);
    c[CI_Grey18] = QColor(235, 235, 235);
    c[CI_Grey19] = QColor(245, 245, 245);
}

void ViewColors::save() const
{
    QSettings settings;
    for (int i = 0; i < CI_COUNT; i++)
        settings.setValue(QString("colors/%1").arg(i), c[i].name());
}

void ViewColors::load()
{
    resetDefaults();  // start with defaults
    if (femmDarkTheme()) {
        // Dark drawing area. Only the defaults change: a colour the user set
        // in Preferences (stored below) still wins.
        c[CI_Background]   = QColor(32, 33, 36);
        c[CI_Line]         = QColor(110, 170, 255);
        c[CI_Node]         = QColor(225, 225, 225);
        c[CI_Grid]         = QColor(95, 98, 104);
        c[CI_Block]        = QColor(110, 210, 130);
        c[CI_Text]         = QColor(230, 230, 230);
        c[CI_RealFluxLine] = QColor(235, 235, 235);
        c[CI_ImagFluxLine] = QColor(120, 220, 140);
        c[CI_RealVector]   = QColor(235, 235, 235);
        c[CI_ImagVector]   = QColor(160, 160, 160);
        c[CI_Selected]     = QColor(255, 80, 80);
    }
    QSettings settings;
    for (int i = 0; i < CI_COUNT; i++) {
        QString key = QString("colors/%1").arg(i);
        if (settings.contains(key))
            c[i] = QColor(settings.value(key).toString());
    }
}
