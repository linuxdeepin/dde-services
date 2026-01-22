// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "shortcutconfig.h"

#include <QObject>
#include <QVariant>
#include <QDBusContext>
#include <QDBusArgument>
#include <QDBusMetaType>


class ConfigLoader;
class ActionExecutor;
class AbstractGestureHandler;
class TranslationManager;

struct GestureInfo {
    QString id;
    QString displayName;
    int category;
    int gestureType;
    int fingerCount;
    int direction;
    int triggerType;
    QStringList triggerValue;
    QString localLanguageName;
};
Q_DECLARE_METATYPE(GestureInfo)

class GestureManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Gesture1")

public:
    explicit GestureManager(ConfigLoader *loader, ActionExecutor *executor, 
                            TranslationManager *translationManager,
                            AbstractGestureHandler *gestureHandler,
                            QObject *parent = nullptr);
    ~GestureManager() override;

    void registerAllGestures();

    /**
     * @brief Clear internal state (called when protocol disconnects)
     */
    void clearState();

public slots:
    // DBus Methods
    Q_SCRIPTABLE QList<GestureInfo> ListAllGestures();
    Q_SCRIPTABLE bool ModifyGesture(const QString &id, const QStringList &action);

signals:
    // DBus Signals
    Q_SCRIPTABLE void GestureInfosChanged();
    Q_SCRIPTABLE void GestureActivated(const QString &id, const QStringList &triggerValue);

private slots:
    void onGestureConfigAdded(const GestureConfig &config);
    void onGestureConfigChanged(const GestureConfig &config);
    void onGestureConfigRemoved(const QString &id);
    void onGestureActivated(const QString &gestureId);

private:
    bool registerGesture(const GestureConfig &config);
    QString LookupConflictGesture(int gestureType, int fingerCount, int direction);

    ConfigLoader *m_loader;
    AbstractGestureHandler *m_gestureHandler;
    ActionExecutor *m_executor;
    TranslationManager *m_translationManager;
    
    // id(gestureId) -> GestureConfig
    QMap<QString, GestureConfig> m_gestureConfigsMap;
};

Q_DECLARE_METATYPE(QList<GestureInfo>)

inline QDBusArgument &operator<<(QDBusArgument &argument, const GestureInfo &info) {
    argument.beginStructure();
    argument << info.id << info.displayName << info.category << info.gestureType << info.fingerCount << info.direction << info.triggerType << info.triggerValue << info.localLanguageName;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, GestureInfo &info) {
    argument.beginStructure();
    argument >> info.id >> info.displayName >> info.category >> info.gestureType >> info.fingerCount >> info.direction >> info.triggerType >> info.triggerValue >> info.localLanguageName;
    argument.endStructure();
    return argument;
}
