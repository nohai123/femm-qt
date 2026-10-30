// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// command_palette.h — Cmd+K / Ctrl+K: find and run any menu command by typing
// part of its name ("mesh", "circuit", "zoom"), instead of remembering which
// of a dozen menus it lives in. Lists only commands that are currently
// visible and enabled, i.e. that apply to the active view.
#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;
class QMenuBar;

class CommandPalette : public QDialog
{
    Q_OBJECT
public:
    explicit CommandPalette(QMenuBar *menus, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void filter(const QString &text);
    void runCurrent();
    QLineEdit *m_edit;
    QListWidget *m_list;
    struct Entry { QString label; QString path; class QAction *action; };
    QList<Entry> m_entries;
};
