// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <QDebug>

class Background {

public:
    Background();
    ~Background();
    void setId(QString id);
    QString getId() const;
    bool getDeleteable();
    void setDeletable(bool deletable);
    QString Thumbnail();

private:
    QString id;
    bool deletable;
};

#endif
