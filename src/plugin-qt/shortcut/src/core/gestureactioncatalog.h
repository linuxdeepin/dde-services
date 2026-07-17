// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "triggeractioncatalog.h"
#include "shortcutconfig.h"

#include <QList>
#include <QString>

enum class GestureBackend {
    X11,
    Treeland
};

enum class GestureActionTarget {
    Backend, // Executed natively by the X11 or Treeland backend.
    Service  // Reported as activated, then executed by dde-services.
};

struct GestureActionMetadata {
    GestureActionId id = GestureActionId::Invalid;
    const char *displayName = "";
};

class GestureActionCatalog
{
public:
    static const QList<GestureActionMetadata> &metadata();
    static QList<GestureActionMetadata> actionsFor(const GestureConfig &config);
    static QList<GestureActionMetadata> actionsFor(
            const GestureConfig &config, GestureBackend backend);
    static GestureActionId resolveActionId(
            const GestureConfig &config, const QString &value);
    static GestureActionId resolveActionId(
            const GestureConfig &config, const QString &value, GestureBackend backend);
    static GestureActionId resolveKnownActionId(const QString &value);
    static const GestureActionMetadata *find(
            const GestureConfig &config, GestureActionId actionId);
    static const GestureActionMetadata *find(
            const GestureConfig &config, GestureActionId actionId, GestureBackend backend);
    static GestureActionTarget targetFor(GestureActionId actionId, GestureBackend backend);
    static GestureActionId registrationActionId(GestureActionId actionId, GestureBackend backend);
    static QString unsupportedReasonSource();
    static QString displayNameSource(const GestureActionMetadata &action);
    static QString displayNameSource(GestureActionId actionId);
    static QString idString(GestureActionId actionId);
};
