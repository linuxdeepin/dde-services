// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "background.h"

#include <QDBusInterface>
#include <pwd.h>

Background::Background()
{
}

Background::~Background()
{
}

void Background::setId(QString id)
{
    this->id = id;
}

QString Background::getId() const
{
    return id;
}

bool Background::getDeleteable()
{
    return deletable;
}
void Background::setDeletable(bool deletable)
{
    this->deletable = deletable;
}

QString Background::Thumbnail()
{
    return "";
}
