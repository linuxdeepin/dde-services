// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutcapturecontroller.h"

#include "core/qkeysequenceconverter.h"
#include "treelandshortcutcapture.h"
#include "x11shortcutcapture.h"

#include <QGuiApplication>
#include <QKeySequence>
#include <QWindow>

ShortcutCaptureController::ShortcutCaptureController(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName() == QLatin1String("wayland"))
        m_waylandCapture = new TreelandShortcutCaptureManager(this);
    else
        m_x11Capture = new X11ShortcutCapture(this);
}

ShortcutCaptureController::~ShortcutCaptureController()
{
    cancel();
}

bool ShortcutCaptureController::capture(
        QObject *owner,
        QWindow *window,
        std::function<void(const QString &)> completed,
        std::function<void(const QString &)> failed)
{
    if (!owner || !window || isActive())
        return false;

    m_owner = owner;
    m_completed = std::move(completed);
    m_failed = std::move(failed);
    m_active = true;
    m_ownerDestroyedConnection = connect(owner,
                                         &QObject::destroyed,
                                         this,
                                         [this] { cancel(); },
                                         Qt::SingleShotConnection);

    if (m_waylandCapture) {
        m_waylandSession = m_waylandCapture->captureNext(window);
        if (!m_waylandSession) {
            clearActiveCapture();
            return false;
        }
        connect(m_waylandSession,
                &TreelandShortcutCaptureSession::captured,
                this,
                &ShortcutCaptureController::finishSuccess);
        connect(m_waylandSession,
                &TreelandShortcutCaptureSession::failed,
                this,
                [this](uint32_t reason) {
                    finishFailure(tr("Compositor shortcut capture failed (%1)").arg(reason));
                });
        return true;
    }

    if (!m_x11Capture || !m_x11Capture->start()) {
        clearActiveCapture();
        return false;
    }
    connect(m_x11Capture,
            &X11ShortcutCapture::captured,
            this,
            &ShortcutCaptureController::finishSuccess,
            Qt::SingleShotConnection);
    connect(m_x11Capture,
            &X11ShortcutCapture::failed,
            this,
            &ShortcutCaptureController::finishFailure,
            Qt::SingleShotConnection);
    return true;
}

void ShortcutCaptureController::cancel(QObject *owner)
{
    if (!isActive() || (owner && owner != m_owner))
        return;

    if (m_x11Capture)
        m_x11Capture->cancel();
    if (m_waylandSession) {
        disconnect(m_waylandSession, nullptr, this, nullptr);
        m_waylandSession->deleteLater();
    }
    clearActiveCapture();
}

QString ShortcutCaptureController::normalizeCapturedShortcut(const QString &shortcut)
{
    const auto normalizeSequence = [](const QString &value) -> QString {
        const QKeySequence sequence = QKeySequence::fromString(value,
                                                                QKeySequence::PortableText);
        if (sequence.isEmpty() || sequence.count() != 1)
            return {};
        return sequence.toString(QKeySequence::PortableText);
    };

    QString normalized = normalizeSequence(shortcut);
    if (!normalized.isEmpty())
        return normalized;
    normalized = normalizeSequence(QKeySequenceConverter::xkbToQKeySequence(shortcut));
    return normalized;
}

void ShortcutCaptureController::finishSuccess(const QString &shortcut)
{
    const QString normalized = normalizeCapturedShortcut(shortcut);
    if (normalized.isEmpty()) {
        finishFailure(tr("The captured key combination is invalid"));
        return;
    }

    const QPointer<QObject> owner = m_owner;
    auto completed = std::move(m_completed);
    clearActiveCapture();
    if (owner && completed)
        completed(normalized);
}

void ShortcutCaptureController::finishFailure(const QString &reason)
{
    const QPointer<QObject> owner = m_owner;
    auto failed = std::move(m_failed);
    clearActiveCapture();
    if (owner && failed)
        failed(reason);
}

void ShortcutCaptureController::clearActiveCapture()
{
    disconnect(m_ownerDestroyedConnection);
    m_ownerDestroyedConnection = {};
    if (m_x11Capture)
        disconnect(m_x11Capture, nullptr, this, nullptr);
    if (m_waylandSession)
        disconnect(m_waylandSession, nullptr, this, nullptr);
    m_waylandSession.clear();
    m_owner.clear();
    m_completed = {};
    m_failed = {};
    m_active = false;
}
