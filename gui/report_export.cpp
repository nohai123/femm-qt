// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// report_export.cpp — see report_export.h.

#include "report_export.h"
#include "results_widget.h"
#include "results_doc.h"
#include "scalar_results_widget.h"
#include "elec_results_doc.h"
#include "heat_results_doc.h"
#include "curr_results_doc.h"

#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <cmath>

namespace {

QString esc(const QString &s) { return s.toHtmlEscaped(); }

QString num(double v) { return QString::number(v, 'g', 5); }

QString units(int lu)
{
    static const char *u[] = {"inches", "millimeters", "centimeters", "meters", "mils", "micrometers"};
    return QString::fromLatin1(lu >= 0 && lu < 6 ? u[lu] : "?");
}

} // namespace

bool exportResultsReport(QWidget *parent, QWidget *view, const QString &path)
{
    QString title, file, physics, info, circuits;
    if (auto *rw = qobject_cast<ResultsWidget *>(view)) {
        ResultsDoc *d = rw->document();
        if (!d) return false;
        file = QString::fromStdString(d->filePath);
        physics = QObject::tr("Magnetics");
        info = QObject::tr("<tr><td>Problem type</td><td>%1</td></tr><tr><td>Length units</td><td>%2</td></tr>"
                           "<tr><td>Frequency</td><td>%3 Hz</td></tr>")
                   .arg(d->ProblemType ? QObject::tr("axisymmetric") : QObject::tr("planar"),
                        units(d->LengthUnits), num(d->Frequency));
        if (!d->circproplist.empty()) {
            const bool ac = d->Frequency != 0;
            circuits = "<h3>" + QObject::tr("Circuits") + "</h3><table border=1 cellspacing=0 cellpadding=4>"
                       "<tr><th>" + QObject::tr("Circuit") + "</th><th>I [A]</th><th>U [V]</th><th>&Phi; [Wb]</th>"
                       "<th>L [H]</th><th>R [&Omega;]</th></tr>";
            for (int k = 0; k < (int)d->circproplist.size(); k++) {
                const CComplex I = d->circproplist[k].Amps, V = d->GetVoltageDrop(k), F = d->GetFluxLinkage(k);
                auto v = [&](CComplex z) { return ac ? num(abs(z)) : num(z.re); };
                const bool hasI = abs(I) > 0;
                circuits += "<tr><td>" + esc(QString::fromStdString(d->circproplist[k].CircName)) + "</td><td>" + v(I) +
                            "</td><td>" + v(V) + "</td><td>" + v(F) + "</td><td>" +
                            (hasI ? num(Re(F / I)) : QStringLiteral("&ndash;")) + "</td><td>" +
                            (hasI ? num(Re(V / I)) : QStringLiteral("&ndash;")) + "</td></tr>";
            }
            circuits += "</table>";
            if (ac) circuits += "<p><small>" + QObject::tr("AC: magnitudes; L and R from the complex ratios.") + "</small></p>";
        }
    } else if (auto *sw = qobject_cast<ScalarResultsWidget *>(view)) {
        static const char *names[] = {"", "Electrostatics", "Heat Flow", "Current Flow"};
        const int pt = sw->physicsType();
        physics = QObject::tr(pt >= 1 && pt <= 3 ? names[pt] : "");
        if (sw->document()) file = QString::fromStdString(sw->document()->filePath);
        else if (sw->heatDocument()) file = QString::fromStdString(sw->heatDocument()->filePath);
        else if (sw->currDocument()) file = QString::fromStdString(sw->currDocument()->filePath);
    } else {
        return false;
    }
    title = QFileInfo(file).completeBaseName();

    // The view as it is on screen, at twice the resolution
    const QPixmap pix = view->grab();
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    pix.save(&buf, "PNG");

    QString html = "<h1>" + esc(title) + "</h1>"
                   "<p>" + esc(physics) + " &middot; " + esc(file) + "<br><small>" +
                   QObject::tr("Report created %1 with FEMM").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")) +
                   "</small></p>"
                   "<p><img src=\"view.png\" width=\"620\"></p>";
    if (!info.isEmpty()) html += "<h3>" + QObject::tr("Problem") + "</h3><table cellpadding=3>" + info + "</table>";
    html += circuits;

    QTextDocument doc;
    doc.addResource(QTextDocument::ImageResource, QUrl("view.png"), QImage::fromData(png, "PNG"));
    doc.setHtml(html);

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(150);
    pdf.setTitle(title);
    pdf.setCreator("FEMM");
    QPainter painter;
    if (!painter.begin(&pdf)) {
        QMessageBox::warning(parent, QObject::tr("Export Report"), QObject::tr("Could not write %1").arg(path));
        return false;
    }
    const QRectF page = pdf.pageLayout().paintRectPixels(pdf.resolution());
    doc.setPageSize(page.size());
    doc.setTextWidth(page.width());
    // Scale so the 620-px-wide picture fills the text width
    painter.scale(page.width() / 680.0, page.width() / 680.0);
    doc.setTextWidth(680);
    doc.drawContents(&painter);
    painter.end();
    return true;
}
