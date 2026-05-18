// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencontroller.h"

class X11ScreenController : public ScreenController
{
    Q_OBJECT
public:
    using ScreenController::ScreenController;
    bool isValid() const override { return false; }
    int outputCount() const override { return 0; }
    Mode mode(int) const override { return On; }
    void setMode(int, Mode) override {}
};

#include "screencontroller_x11.moc"
