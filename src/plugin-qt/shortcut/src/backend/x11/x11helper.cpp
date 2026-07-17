// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <X11/Xlib.h>

#include <cstring>

extern "C" {
    unsigned long x11StringToKeysym(const char* str) {
        const KeySym keysym = XStringToKeysym(str);
        if (keysym != NoSymbol)
            return keysym;
        if (str && std::strlen(str) == 1)
            return static_cast<unsigned char>(str[0]);
        return NoSymbol;
    }
}
