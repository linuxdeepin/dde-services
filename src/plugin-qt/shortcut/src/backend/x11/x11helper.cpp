// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <X11/Xlib.h>

extern "C" {
    unsigned long x11StringToKeysym(const char* str) {
        return XStringToKeysym(str);
    }
}
