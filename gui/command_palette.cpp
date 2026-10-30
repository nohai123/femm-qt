// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// command_palette.cpp — see command_palette.h.

#include "command_palette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString plain(QString t)
{
    t.remove('&');
    if (t.endsWith("...")) t.chop(3);
    if (t.endsWith(QChar(0x2026))) t.chop(1);
    return t.trimmed();
}

} // namespace

CommandPalette::CommandPalette(QMenuBar *menus, QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setMinimumWidth(520);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("Type a command…"));
    m_edit->installEventFilter(this);
    lay->addWidget(m_edit);
    m_list = new QListWidget;
    m_list->setMinimumHeight(320);
    lay->addWidget(m_list);

    // Collect every visible, enabled command from the menus, with its path
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *m, const QString &path) {
        for (QAction *a : m->actions()) {
            if (a->isSeparator() || !a->isVisible()) continue;
            if (a->menu()) { walk(a->menu(), path + " › " + plain(a->text())); continue; }
            if (!a->isEnabled() || plain(a->text()).isEmpty()) continue;
            m_entries.push_back({plain(a->text()), path, a});
        }
    };
    for (QAction *top : menus->actions())
        if (top->menu() && top->isVisible()) walk(top->menu(), plain(top->text()));

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(m_list, &QListWidget::itemActivated, this, [this]() { runCurrent(); });
    connect(m_list, &QListWidget::itemClicked, this, [this]() { runCurrent(); });
    filter(QString());
    m_edit->setFocus();
}

void CommandPalette::filter(const QString &text)
{
    m_list->clear();
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < m_entries.size(); i++) {
        const Entry &e = m_entries[i];
        const QString hay = e.label + " " + e.path;
        bool ok = true;
        for (const QString &w : words) ok &= hay.contains(w, Qt::CaseInsensitive);
        if (!ok) continue;
        QString shown = e.label;
        if (!e.action->shortcut().isEmpty())
            shown += "    " + e.action->shortcut().toString(QKeySequence::NativeText);
        auto *it = new QListWidgetItem(shown + "\n" + e.path);
        it->setData(Qt::UserRole, i);
        if (!e.action->icon().isNull()) it->setIcon(e.action->icon());
        m_list->addItem(it);
    }
    if (m_list->count()) m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return;
    QPointer<QAction> a = m_entries[it->data(Qt::UserRole).toInt()].action;
    accept();
    // After the palette is gone, so a command that opens a dialog gets focus
    QTimer::singleShot(0, [a]() { if (a) a->trigger(); });
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_edit && ev->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(ev);
        switch (k->key()) {
        case Qt::Key_Down:
            m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1));
            return true;
        case Qt::Key_Up:
            m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        case Qt::Key_Escape:
            reject();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(obj, ev);
}
