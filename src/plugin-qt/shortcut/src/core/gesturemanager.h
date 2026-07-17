// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "triggeractioncatalog.h"
#include "shortcutconfig.h"

#include <QObject>
#include <QSet>
#include <QVariant>
#include <QDBusContext>
#include <QDBusArgument>
#include <QDBusMetaType>


class ConfigLoader;
class ActionExecutor;
class AbstractGestureHandler;
class TranslationManager;
class X11GestureActionExecutor;
class ServiceActionExecutor;
struct GestureActionInfo {
    QString actionId;
    QString displayName;
    bool supported = false;
    QString unavailableReason;
};
Q_DECLARE_METATYPE(GestureActionInfo)

struct GestureInfo {
    QString id;
    QString displayName;
    QString category;
    int gestureType;
    int fingerCount;
    int direction;
    int triggerType;
    QStringList triggerValue;
    QString localLanguageName;
    QString localLanguageCategory;
    bool isCustom = false;
    QList<GestureActionInfo> availableActions;
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
                            X11GestureActionExecutor *x11ActionExecutor,
                            ServiceActionExecutor *serviceActionExecutor,
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
    Q_SCRIPTABLE QString GetGestureAvaiableActions(const QString &actionType, int fingerNum);

signals:
    // DBus Signals
    Q_SCRIPTABLE void GestureInfosChanged();
    Q_SCRIPTABLE void GestureActivated(const QString &id, const QStringList &triggerValue);

private slots:
    void onGestureConfigAdded(const GestureConfig &config);
    void onGestureConfigChanged(const GestureConfig &config);
    void onGestureConfigRemoved(const QString &id);
    void onGestureActivated(const QString &gestureId);
    void onHandlerAvailabilityChanged(bool available);

private:
    bool registerGesture(const GestureConfig &config);
    void setGestureActive(const GestureConfig &config);
    bool setGestureInactive(const QString &id);
    QString LookupConflictGesture(int gestureType, int fingerCount, int direction);
    GestureInfo toGestureInfo(const GestureConfig &config) const;
    GestureActionId actionId(const GestureConfig &config) const;
    QList<GestureActionInfo> availableActions(const GestureConfig &config) const;
    bool isActionSupported(const GestureConfig &config) const;
    QString translateServiceText(const QString &source) const;

    ConfigLoader *m_loader;
    AbstractGestureHandler *m_gestureHandler;
    ActionExecutor *m_executor;
    TranslationManager *m_translationManager;
    X11GestureActionExecutor *m_x11ActionExecutor;
    ServiceActionExecutor *m_serviceActionExecutor;
    
    QMap<QString, GestureConfig> m_configuredGestures;
    QSet<QString> m_activeGestureIds;
    bool m_isWayland = false;
};

Q_DECLARE_METATYPE(QList<GestureInfo>)
Q_DECLARE_METATYPE(QList<GestureActionInfo>)

inline QDBusArgument &operator<<(QDBusArgument &argument, const GestureInfo &info) {
    argument.beginStructure();
    argument << info.id << info.displayName << info.category << info.gestureType
             << info.fingerCount << info.direction << info.triggerType << info.triggerValue
             << info.localLanguageName << info.localLanguageCategory << info.isCustom
             << info.availableActions;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, GestureInfo &info) {
    argument.beginStructure();
    argument >> info.id >> info.displayName >> info.category >> info.gestureType
             >> info.fingerCount >> info.direction >> info.triggerType >> info.triggerValue
             >> info.localLanguageName >> info.localLanguageCategory >> info.isCustom
             >> info.availableActions;
    argument.endStructure();
    return argument;
}

inline QDBusArgument &operator<<(QDBusArgument &argument, const GestureActionInfo &item) {
    argument.beginStructure();
    argument << item.actionId << item.displayName << item.supported << item.unavailableReason;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, GestureActionInfo &item) {
    argument.beginStructure();
    argument >> item.actionId >> item.displayName >> item.supported >> item.unavailableReason;
    argument.endStructure();
    return argument;
}
