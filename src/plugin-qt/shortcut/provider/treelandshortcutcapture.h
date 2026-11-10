// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QWaylandClientExtension>

#include "qwayland-treeland-shortcut-manager-unstable-v3.h"

class QWindow;

class TreelandShortcutCaptureSession
    : public QObject
    , public QtWayland::treeland_shortcut_capture_v3
{
    Q_OBJECT

public:
    explicit TreelandShortcutCaptureSession(struct ::treeland_shortcut_capture_v3 *object,
                                            QObject *parent = nullptr);
    ~TreelandShortcutCaptureSession() override;

Q_SIGNALS:
    void captured(const QString &key);
    void failed(uint32_t reason);

protected:
    void treeland_shortcut_capture_v3_captured(const QString &key) override;
    void treeland_shortcut_capture_v3_failed(uint32_t reason) override;

private:
    bool m_finished = false;
};

class TreelandShortcutCaptureManager
    : public QWaylandClientExtensionTemplate<TreelandShortcutCaptureManager>
    , public QtWayland::treeland_shortcut_manager_v3
{
    Q_OBJECT

public:
    explicit TreelandShortcutCaptureManager(QObject *parent = nullptr);
    ~TreelandShortcutCaptureManager() override;

    TreelandShortcutCaptureSession *captureNext(QWindow *window);
};
