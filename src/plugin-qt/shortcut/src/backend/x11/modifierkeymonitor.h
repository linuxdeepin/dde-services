// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QTimer>

// Forward declarations
typedef struct _XDisplay Display;
typedef struct xcb_connection_t xcb_connection_t;

/**
 * @brief 修饰键监听器抽象基类
 * 
 * 负责监听修饰键（Super/Alt/Control等）的单独按下和释放
 * 与X11KeyHandler解耦，可以独立测试和替换实现
 */
class ModifierKeyMonitor : public QObject
{
    Q_OBJECT
public:
    explicit ModifierKeyMonitor(QObject *parent = nullptr);
    virtual ~ModifierKeyMonitor() = default;

    /**
     * @brief 启动监听
     */
    virtual void start() = 0;
    
    /**
     * @brief 停止监听
     */
    virtual void stop() = 0;
    
    /**
     * @brief 通知有非修饰键被按下
     * 
     * 用于组合键检测：当修饰键被按住时，如果有其他键被按下，
     * 则认为这是一个组合键，不应该触发单独修饰键的快捷键
     */
    virtual void notifyNonModifierKeyPressed() = 0;

signals:
    /**
     * @brief 单独修饰键被释放
     * @param keysym X11 keysym (如 XK_Super_L, XK_Alt_L等)
     */
    void modifierKeyReleased(unsigned long keysym);
};

/**
 * @brief 基于轮询的修饰键监听器
 * 
 * 使用QTimer定时轮询键盘状态（XQueryKeymap）
 * 检测修饰键的按下和释放，并判断是否为单独按下
 */
class PollingModifierKeyMonitor : public ModifierKeyMonitor
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param conn XCB连接（用于检查键盘grab状态）
     * @param parent 父对象
     */
    explicit PollingModifierKeyMonitor(xcb_connection_t *conn, QObject *parent = nullptr);
    ~PollingModifierKeyMonitor() override;

    void start() override;
    void stop() override;
    void notifyNonModifierKeyPressed() override;

private slots:
    void checkModifierKeyState();

private:
    bool isKeyPressed(unsigned long keysym);
    bool isKeyboardGrabbed();
    
    xcb_connection_t *m_connection;  // XCB连接（用于grab检查）
    Display *m_display;              // Xlib Display（用于XQueryKeymap）
    QTimer *m_timer;                 // 轮询定时器
    
    // 修饰键状态跟踪
    bool m_lastSuperPressed;
    bool m_lastAltPressed;
    bool m_lastControlPressed;
    
    // 组合键检测标志
    bool m_nonModKeyPressed;
};
