// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/portalshortcuttypes.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class ShortcutCaptureController;
class WaylandForeignParent;

class ShortcutEditorRow : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutEditorRow(const PortalShortcutUpdate &shortcut,
                               ShortcutCaptureController *captureController,
                               QWidget *parent = nullptr);

    PortalShortcutUpdate update() const;
    void setInteractionEnabled(bool enabled);

private:
    void startCapture();
    void setHotkey(const QString &hotkey);

    PortalShortcutUpdate m_shortcut;
    ShortcutCaptureController *const m_captureController;
    QCheckBox *m_enabled = nullptr;
    QLineEdit *m_hotkey = nullptr;
    QPushButton *m_record = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_restore = nullptr;
    QLabel *m_status = nullptr;
};

class GlobalShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode {
        Bind,
        Configure,
    };

    GlobalShortcutsDialog(Mode mode,
                          const QString &appId,
                          const QString &parentWindow,
                          const PortalShortcutUpdateList &shortcuts,
                          ShortcutCaptureController *captureController,
                          WaylandForeignParent *foreignParent,
                          QWidget *parent = nullptr);

    PortalShortcutUpdateList updates() const;
    void setBusy(bool busy);
    void showError(const QString &message);
    void cancelSilently();
    void present(const QString &activationToken = {});

Q_SIGNALS:
    void bindAccepted(const PortalShortcutUpdateList &updates);
    void bindCancelled();
    void applyRequested(const PortalShortcutUpdateList &updates);

private:
    void applyParentWindow();

    Mode m_mode;
    QString m_parentWindow;
    ShortcutCaptureController *const m_captureController;
    WaylandForeignParent *const m_foreignParent;
    QList<ShortcutEditorRow *> m_rows;
    QWidget *m_rowsContainer = nullptr;
    QLabel *m_error = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    bool m_silentClose = false;
    bool m_parentApplied = false;
    bool m_parentApplyPending = false;
    int m_parentApplyAttempts = 0;
};
