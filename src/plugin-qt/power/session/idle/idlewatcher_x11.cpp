// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "idlewatcher.h"

class X11IdleWatcher : public IdleWatcher
{
    Q_OBJECT
public:
    using IdleWatcher::IdleWatcher;
    bool isValid() const override { return false; }
    void setTimeout(uint32_t) override {}
    void simulateActivity() override {}
    uint32_t idleTimeMs() const override { return 0; }
    bool isIdle() const override { return false; }
};

#include "idlewatcher_x11.moc"
