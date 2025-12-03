// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xcbutils.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool xcb_debug = 1;

XcbUtils &XcbUtils::getInstance()
{
    static XcbUtils xcbUtils;
    return xcbUtils;
}

XcbUtils::XcbUtils(QObject *parent)
    : QObject(parent)
{
    connection = xcb_connect(nullptr, nullptr);
    if (!connection) {
        qWarning() << "xcb connect failed";
        return;
    }
    window = createWindows();
    if (window == XCB_WINDOW_NONE) {
        qWarning() << "xcb invalid window";
        return;
    }
    if (!isSelectionOwned("_XSETTINGS_S0")) {
        qWarning() << "owned _XSETTINGS_S0 failed";
        return;
    }
}

xcb_window_t XcbUtils::createWindows()
{
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    /* Get the first screen */
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = iter.data;

    /* Create the window */
    xcb_window_t window = xcb_generate_id(connection);
    if (window != XCB_WINDOW_NONE) {
        qDebug() << "xcb generate window:" << window;
    }
    xcb_create_window(connection,           /* Connection          */
                      XCB_COPY_FROM_PARENT, /* depth (same as root)*/
                      window,               /* window Id           */
                      screen->root,         /* parent window       */
                      0,
                      0, /* x, y                */
                      1,
                      1,                           /* width, height       */
                      0,                           /* border_width        */
                      XCB_WINDOW_CLASS_INPUT_ONLY, /* class               */
                      screen->root_visual,         /* visual              */
                      0,
                      nullptr); /* masks, not used yet */

    xcb_flush(connection);
    xcb_atom_t atom = getAtom("_XSETTINGS_S0");
    if (atom == XCB_ATOM_NONE) {
        qWarning() << "invalid atom";
        return XCB_WINDOW_NONE;
    }

    changeWindowPid(window);

    cookie = xcb_set_selection_owner_checked(connection, window, atom, XCB_CURRENT_TIME);
    // 检查错误
    error = xcb_request_check(connection, cookie);
    if (error) {
        qWarning() << "xcb set selection owner failed: " << error->error_code;
        return XCB_WINDOW_NONE;
    }
    return window;
}

xcb_atom_t XcbUtils::getAtom(const char *name, bool exist)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, strlen(name), name);

    xcb_generic_error_t *err = nullptr;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, &err);
    if (!reply) {
        if (err != nullptr) {
            fprintf(stderr, "get atom failed, errcode:%d\n", err->error_code);
        } else {
            fprintf(stderr, "get atom failed, unknown err.\n");
        }
        return 0;
    }

    xcb_atom_t atom = reply->atom;
    free(reply);
    if (xcb_debug) {
        qDebug() << "xcb get atom [" << name << "]: " << atom;
    }
    return atom;
}

bool XcbUtils::changeWindowPid(xcb_window_t window)
{
    pid_t pid = getpid();

    xcb_atom_t atom = getAtom("_NET_WM_PID");
    xcb_atom_t cardinal_atom = getAtom("CARDINAL");
    QByteArray pidArray;
    pidArray.push_back(char(pid));
    pidArray.push_back(char(pid >> 8));
    pidArray.push_back(char(pid >> 16));
    pidArray.push_back(char(pid >> 24));
    int32_t data[4];
    data[0] = int32_t(0xff & pid);
    data[1] = int32_t(0xff & (pid >> 8));
    data[2] = int32_t(0xff & (pid >> 16));
    data[3] = int32_t(0xff & (pid >> 24));

    // xcb_void_cookie_t cookie = xcb_change_property_checked(connection,XCB_PROP_MODE_REPLACE,window,atom,XCB_ATOM_CARDINAL,32,
    //                            4, pidArray.data());
    xcb_void_cookie_t cookie = xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, atom, cardinal_atom, 32, 1, &pid);
    // 检查错误
    xcb_generic_error_t *error = xcb_request_check(connection, cookie);
    if (error) {
        qWarning() << "xcb change property failed:" << error->error_code;
        return false;
    }

    return true;
}

bool XcbUtils::isSelectionOwned(QString prop)
{
    xcb_atom_t atom = getAtom(prop.toLatin1().data());

    xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(connection, atom);

    xcb_generic_error_t *err = nullptr;
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(connection, cookie, &err);
    if (err != nullptr || reply == nullptr) {
        return false;
    }
    if (reply->owner == 0 || reply->owner != window) {
        return false;
    }

    return true;
}

QByteArray XcbUtils::xcbPropertyReplyDataToArray(xcb_get_property_reply_t *reply)
{
    QByteArray arr;
    int length = xcb_get_property_value_length(reply);
    uint8_t *prop_data = (uint8_t *)xcb_get_property_value(reply);
    for (int i = 0; i < length; i++) {
        arr.push_back(prop_data[i]);
    }
    return arr;
}

QString XcbUtils::getXcbAtomName(xcb_atom_t atom)
{
    xcb_get_atom_name_cookie_t cookie;
    xcb_get_atom_name_reply_t *reply;

    cookie = xcb_get_atom_name(connection, atom);
    reply = xcb_get_atom_name_reply(connection, cookie, NULL);
    if (reply) {
        return xcb_get_atom_name_name(reply);
    }
    return "";
}

// 获取 _XSETTINGS_SETTINGS 属性
QByteArray XcbUtils::getXcbAtomProperty(xcb_atom_t atom)
{
    bool more = true;
    int offset = 0;
    xcb_get_property_cookie_t cookie;
    xcb_get_property_reply_t *reply;
    xcb_generic_error_t *error = nullptr;
    QByteArray settings;
    int i = 0;
    while (more) {
        i++;
        cookie = xcb_get_property(connection,
                                  0,      // 删除属性为 0
                                  window, // 根窗口
                                  atom,   // 要获取的 Atom
                                  XCB_ATOM_NONE,
                                  offset / 4, // 从 0 偏移量开始
                                  UINT32_MAX  // 获取尽可能多的数据
        );
        reply = xcb_get_property_reply(connection, cookie, &error);
        // 在窗口无效时，应当认为此native settings未初始化完成
        if (error && error->error_code == 3) {
            return settings;
        }

        // 查询这个原子类型的名称
        xcb_get_atom_name_cookie_t type_name_cookie = xcb_get_atom_name(connection, reply->type);
        xcb_get_atom_name_reply_t *type_name_reply = xcb_get_atom_name_reply(connection, type_name_cookie, NULL);

        if (type_name_reply) {
            int name_len = xcb_get_atom_name_name_length(type_name_reply);
            char *name = xcb_get_atom_name_name(type_name_reply);
            qWarning("属性类型名称: %.*s", name_len, name);
            free(type_name_reply);
        }
        void *value = xcb_get_property_value(reply);
        int value_len = xcb_get_property_value_length(reply);
        qWarning("属性值(字符串): %d |%s\n", value_len, (char *)value);

        const auto property_value_length = xcb_get_property_value_length(reply);
        settings.append(static_cast<const char *>(xcb_get_property_value(reply)), property_value_length);
        QStringList hex;
        for (int i = 0; i < property_value_length; ++i) {
            hex.append(QString::number((uint8_t)settings[i]));
            // hex<<(uint8_t)array.at(i);
        }
        offset += property_value_length;
        more = reply->bytes_after != 0;
        free(reply);
    }
    return settings;
}

bool XcbUtils::changeSettingProp(QByteArray data)
{
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *err;
    xcb_atom_t atom = getAtom("_XSETTINGS_SETTINGS");
    if (atom == 0) {
        return false;
    }
    cookie = xcb_change_property_checked(connection, PropModeReplace, window, atom, atom, 8, data.length(), data);
    err = xcb_request_check(connection, cookie);
    if (err) {
        qWarning() << "xcb change setting prop failed," << err->error_code;
        return false;
    }
    return true;
}

void XcbUtils::updateXResources(QVector<QPair<QString, QString>> xresourceInfos)
{
    QString datas = getXResources();

    if (datas.isEmpty()) {
        xresourceInfos.push_back(qMakePair(QString("*customization"), QString("-color")));
    } else {
        QVector<QPair<QString, QString>> infos = XcbUtils::unmarshalXResources(datas);

        for (auto info : infos) {
            bool bFound = false;
            for (auto xresourceInfo : xresourceInfos) {
                if (info.first == xresourceInfo.first) {
                    bFound = true;
                    break;
                }
            }

            if (!bFound) {
                xresourceInfos.push_back(info);
            }
        }
    }

    datas = XcbUtils::marshalXResources(xresourceInfos);

    XcbUtils::setXResources(const_cast<char *>(datas.toStdString().c_str()), datas.length());
}

char *XcbUtils::getXResources()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        return nullptr;
    }

    char *res = XResourceManagerString(dpy);
    if (res == nullptr) {
        XCloseDisplay(dpy);
        return nullptr;
    }

    char *ret = strdup(res);
    XCloseDisplay(dpy);
    return ret;
}

int XcbUtils::setXResources(char *data, unsigned long length)
{
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        return -1;
    }

    XChangeProperty(dpy, DefaultRootWindow(dpy), XA_RESOURCE_MANAGER, XA_STRING, 8, PropModeReplace, reinterpret_cast<const unsigned char *>(data), length);

    XCloseDisplay(dpy);
    return 0;
}

QVector<QPair<QString, QString>> XcbUtils::unmarshalXResources(const QString &datas)
{
    QVector<QPair<QString, QString>> infos;

    QStringList lines = datas.split("\n");
    for (auto line : lines) {
        if (line.isEmpty()) {
            continue;
        }

        QStringList arr = line.split(":\t");
        if (arr.size() != 2) {
            continue;
        }

        infos.push_back(qMakePair(arr[0], arr[1]));
    }

    return infos;
}

QString XcbUtils::marshalXResources(const QVector<QPair<QString, QString>> &infos)
{
    QString data;

    for (auto &info : infos) {
        data += info.first + ":\t" + info.second + "\n";
    }

    return data;
}
