// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// welcome_page.cpp — see welcome_page.h.

#include "welcome_page.h"
#include "femm_resource.h"
#include "py_line_edit.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

struct PhysicsInfo { const char *title; const char *blurb; const char *icon; const char *ext; };
const PhysicsInfo kPhysics[4] = {
    {QT_TRANSLATE_NOOP("WelcomePage", "Magnetics"),
     QT_TRANSLATE_NOOP("WelcomePage", "Coils, magnets, motors, actuators, transformers; DC or AC"),
     ":/icons/mi.ico", "fem"},
    {QT_TRANSLATE_NOOP("WelcomePage", "Electrostatics"),
     QT_TRANSLATE_NOOP("WelcomePage", "Capacitance, insulation, electric field strength"),
     ":/icons/ei.ico", "fee"},
    {QT_TRANSLATE_NOOP("WelcomePage", "Heat Flow"),
     QT_TRANSLATE_NOOP("WelcomePage", "Temperatures, heat sinks, thermal resistance"),
     ":/icons/hi.ico", "feh"},
    {QT_TRANSLATE_NOOP("WelcomePage", "Current Flow"),
     QT_TRANSLATE_NOOP("WelcomePage", "Resistance, current density, AC conduction"),
     ":/icons/ci.ico", "fec"},
};

int physicsOfExt(const QString &ext)
{
    const QString e = ext.toLower();
    for (int i = 0; i < 4; i++) if (e == kPhysics[i].ext) return i;
    return -1;
}

// The [Comment] of a FEMM problem file, for a one-line description.
QString problemComment(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    static const QRegularExpression re(QStringLiteral("^\\[Comment\\]\\s*=\\s*\"(.*)\"\\s*$"),
                                       QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < 40 && !f.atEnd(); i++) {
        const auto m = re.match(QString::fromUtf8(f.readLine()).trimmed());
        if (m.hasMatch()) return m.captured(1).replace("\\n", " ").trimmed();
    }
    return QString();
}

QLabel *heading(const QString &t)
{
    auto *l = new QLabel(t);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    l->setFont(f);
    return l;
}

} // namespace

QString femmPrereleaseWarning()
{
#ifdef FEMM_PRERELEASE
    return QCoreApplication::translate("WelcomePage",
        "Pre-production release: this is a test build of a reworked user interface. "
        "Its results are NOT yet validated and must not be relied on for design "
        "decisions. Check important values against the official FEMM release.");
#else
    return QString();
#endif
}

QString femmExamplesWorkDir()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath();
    return docs + "/FEMM Examples";
}

// ================================================================
// New Problem dialog
// ================================================================

NewProblemDialog::NewProblemDialog(int physics, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Problem"));
    auto *lay = new QVBoxLayout(this);

    lay->addWidget(new QLabel(tr("<b>What do you want to simulate?</b>")));
    auto *grid = new QGridLayout;
    m_type = new QButtonGroup(this);
    for (int i = 0; i < 4; i++) {
        auto *b = new QToolButton;
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(QIcon(kPhysics[i].icon));
        b->setIconSize(QSize(32, 32));
        b->setText(QStringLiteral("%1\n%2").arg(QCoreApplication::translate("WelcomePage", kPhysics[i].title),
                                                QCoreApplication::translate("WelcomePage", kPhysics[i].blurb)));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        b->setMinimumHeight(52);
        m_type->addButton(b, i);
        grid->addWidget(b, i / 2, i % 2);
    }
    if (auto *b = m_type->button(physics)) b->setChecked(true);
    lay->addLayout(grid);

    auto *form = new QFormLayout;
    m_units = new QComboBox;
    m_units->addItems({tr("Inches"), tr("Millimeters"), tr("Centimeters"), tr("Meters"),
                       tr("Mils"), tr("Micrometers")});
    m_units->setCurrentIndex(QSettings().value("defaults/lengthUnits", 1).toInt());
    form->addRow(tr("Length units:"), m_units);

    auto *geom = new QHBoxLayout;
    m_planar = new QRadioButton(tr("Planar (2D cross-section)"));
    m_axi = new QRadioButton(tr("Axisymmetric (rotationally symmetric)"));
    (QSettings().value("defaults/problemType", 0).toInt() == 1 ? m_axi : m_planar)->setChecked(true);
    geom->addWidget(m_planar);
    geom->addWidget(m_axi);
    form->addRow(tr("Geometry:"), geom);

    m_depth = new PyLineEdit;
    m_depth->setText(QString::number(QSettings().value("defaults/depth", 1.0).toDouble(), 'g', 10));
    m_depthLabel = new QLabel(tr("Depth:"));
    m_depth->setToolTip(tr("Length of the model into the page, in the length units above"));
    form->addRow(m_depthLabel, m_depth);

    m_freq = new PyLineEdit;
    m_freq->setText("0");
    m_freqLabel = new QLabel(tr("Frequency (Hz):"));
    m_freqHint = new QLabel(tr("0 = DC / static"));
    m_freqHint->setStyleSheet("color: gray");
    auto *fr = new QHBoxLayout;
    fr->addWidget(m_freq);
    fr->addWidget(m_freqHint);
    form->addRow(m_freqLabel, fr);
    lay->addLayout(form);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_type, &QButtonGroup::idClicked, this, [this](int) { updateFields(); });
    connect(m_planar, &QRadioButton::toggled, this, [this](bool) { updateFields(); });
    updateFields();
}

void NewProblemDialog::updateFields()
{
    const int t = m_type->checkedId();
    const bool freq = (t == 0 || t == 3);
    m_freq->setVisible(freq); m_freqLabel->setVisible(freq); m_freqHint->setVisible(freq);
    if (t == 3 && m_freq->text() == "0") m_freq->setText("60");
    if (t == 0 && m_freq->text() == "60") m_freq->setText("0");
    m_freqHint->setText(t == 3 ? tr("current flow is solved at a frequency") : tr("0 = DC / static"));
    const bool planar = m_planar->isChecked();
    m_depth->setVisible(planar); m_depthLabel->setVisible(planar);
}

NewProblemSettings NewProblemDialog::settings() const
{
    NewProblemSettings s;
    s.physics = std::max(0, m_type->checkedId());
    s.lengthUnits = m_units->currentIndex();
    s.problemType = m_axi->isChecked() ? 1 : 0;
    s.depth = static_cast<PyLineEdit *>(m_depth)->evaluateAsDouble(1.0);
    s.frequency = std::abs(static_cast<PyLineEdit *>(m_freq)->evaluateAsDouble(0));
    return s;
}

// ================================================================
// Welcome page
// ================================================================

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("WelcomePage");
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(32);

    // Left: new + recent
    auto *left = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("FEMM"));
    QFont tf = title->font(); tf.setPointSizeF(tf.pointSizeF() * 2.4); tf.setBold(true); title->setFont(tf);
    left->addWidget(title);
    left->addWidget(new QLabel(tr("Finite Element Method Magnetics \u2014 2D and axisymmetric field simulation")));
    if (!femmPrereleaseWarning().isEmpty()) {
        auto *warn = new QLabel(QStringLiteral("\u26a0\ufe0f ") + femmPrereleaseWarning());
        warn->setWordWrap(true);
        warn->setStyleSheet("QLabel { background: #fdecea; color: #8a1c13; border: 1px solid #e0a29b;"
                            " border-radius: 4px; padding: 8px; }");
        left->addSpacing(8);
        left->addWidget(warn);
    }
    left->addSpacing(16);
    left->addWidget(heading(tr("New")));
    auto *grid = new QGridLayout;
    for (int i = 0; i < 4; i++) {
        auto *b = new QToolButton;
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(QIcon(kPhysics[i].icon));
        b->setIconSize(QSize(32, 32));
        b->setText(QStringLiteral("%1\n%2").arg(tr(kPhysics[i].title), tr(kPhysics[i].blurb)));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        b->setMinimumHeight(56);
        connect(b, &QToolButton::clicked, this, [this, i]() { emit newProblem(i); });
        grid->addWidget(b, i / 2, i % 2);
    }
    left->addLayout(grid);
    left->addSpacing(12);

    auto *recentHead = new QHBoxLayout;
    recentHead->addWidget(heading(tr("Recent")));
    recentHead->addStretch();
    auto *open = new QPushButton(tr("Open\u2026"));
    connect(open, &QPushButton::clicked, this, &WelcomePage::openOther);
    recentHead->addWidget(open);
    left->addLayout(recentHead);
    m_recent = new QListWidget;
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        emit openFile(it->data(Qt::UserRole).toString());
    });
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        emit openFile(it->data(Qt::UserRole).toString());
    });
    left->addWidget(m_recent, 1);
    auto *tips = new QLabel(tr("Tip: draw with L (line), R (rectangle), C (circle), A (arc); "
                               "fill regions with F; two fingers pan, pinch zooms."));
    tips->setWordWrap(true);
    tips->setStyleSheet("color: gray");
    left->addWidget(tips);
    outer->addLayout(left, 3);

    // Right: examples
    auto *right = new QVBoxLayout;
    right->addWidget(heading(tr("Examples")));
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search examples (e.g. motor, coil, capacitor)\u2026"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &WelcomePage::fillExamples);
    right->addWidget(m_search);
    m_examples = new QListWidget;
    m_examples->setWordWrap(true);
    m_examples->setMinimumWidth(340);
    connect(m_examples, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString src = it->data(Qt::UserRole).toString();
        QDir().mkpath(femmExamplesWorkDir());
        const QString dst = femmExamplesWorkDir() + "/" + QFileInfo(src).fileName();
        if (!QFileInfo::exists(dst)) {
            QFile::copy(src, dst);
            QFile::setPermissions(dst, QFile::permissions(dst) | QFileDevice::WriteOwner);
        }
        emit openFile(dst);
    });
    right->addWidget(m_examples, 1);
    auto *where = new QLabel(tr("Examples open as copies in %1").arg(QDir::toNativeSeparators(femmExamplesWorkDir())));
    where->setWordWrap(true);
    where->setStyleSheet("color: gray");
    right->addWidget(where);
    outer->addLayout(right, 2);

    refresh();
    fillExamples(QString());
}

void WelcomePage::refresh()
{
    m_recent->clear();
    const QStringList recent = QSettings().value("recentFiles").toStringList();
    for (const QString &p : recent) {
        QFileInfo fi(p);
        if (!fi.exists()) continue;
        auto *it = new QListWidgetItem(fi.fileName() + QStringLiteral("   \u2014   ") +
                                       QDir::toNativeSeparators(fi.absolutePath()));
        const int ph = physicsOfExt(fi.suffix());
        if (ph >= 0) it->setIcon(QIcon(kPhysics[ph].icon));
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        m_recent->addItem(it);
    }
    if (m_recent->count() == 0) {
        auto *it = new QListWidgetItem(tr("No recent files yet"));
        it->setFlags(Qt::NoItemFlags);
        m_recent->addItem(it);
    }
}

void WelcomePage::fillExamples(const QString &filter)
{
    m_examples->clear();
    const QDir dir(QString::fromStdString(femmResource("examples")));
    const QStringList files = dir.entryList({"*.fem", "*.fee", "*.feh", "*.fec", "*.FEM", "*.FEE", "*.FEH", "*.FEC"},
                                            QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString &f : files) {
        const QString path = dir.filePath(f);
        const QString comment = problemComment(path);
        if (!filter.isEmpty() && !f.contains(filter, Qt::CaseInsensitive) &&
            !comment.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *it = new QListWidgetItem(comment.isEmpty() ? f : QStringLiteral("%1 \u2014 %2").arg(f, comment));
        const int ph = physicsOfExt(QFileInfo(f).suffix());
        if (ph >= 0) it->setIcon(QIcon(kPhysics[ph].icon));
        it->setData(Qt::UserRole, path);
        it->setToolTip(comment);
        m_examples->addItem(it);
    }
    if (m_examples->count() == 0) {
        auto *it = new QListWidgetItem(files.isEmpty() ? tr("No examples installed") : tr("No match"));
        it->setFlags(Qt::NoItemFlags);
        m_examples->addItem(it);
    }
}
