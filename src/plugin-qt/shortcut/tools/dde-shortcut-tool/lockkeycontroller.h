// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef LOCKKEYCONTROLLER_H
#define LOCKKEYCONTROLLER_H

#include "basecontroller.h"

namespace Dtk {
namespace Core {
class DConfig;
}
}

// Forward declarations for X11
typedef struct xcb_connection_t xcb_connection_t;
typedef struct _XCBKeySymbols xcb_key_symbols_t;
typedef struct xcb_screen_t xcb_screen_t;

/**
 * @brief Lock key controller
 * 
 * Handles CapsLock and NumLock OSD display
 * Supports X11 and Wayland
 */
class LockKeyController : public BaseController 
{
    Q_OBJECT
public:
    explicit LockKeyController(QObject *parent = nullptr);
    ~LockKeyController() override;
    
    // BaseController interface
    QString name() const override { return "lockkey"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

private:
    // CapsLock handling
    void handleCapsLockOSD();
    
    // NumLock handling
    void handleNumLockOSD();
    
    // Query lock state (returns -1 on failure, 0 for off, 1 for on)
    int queryCapsLockState();
    int queryNumLockState();
    
    // Check if CapsLock OSD should be shown
    bool shouldShowCapsLockOSD();
    
    // Show OSD
    void showOSD(const QString &signal);
    
    // Detect current session type
    bool isWayland() const;
    
    // X11 related
    void initX11();
    void cleanupX11();

    int queryCapsLockStateX11();
    int queryNumLockStateX11();

    int queryCapsLockStateWayland();
    int queryNumLockStateWayland();
    
private:
    Dtk::Core::DConfig *m_keyboardConfig;
    
    // X11 connection
    xcb_connection_t *m_connection;
    xcb_key_symbols_t *m_keySymbols;
    xcb_screen_t *m_screen;
    
    bool m_isWayland;
};

#endif // LOCKKEYCONTROLLER_H
