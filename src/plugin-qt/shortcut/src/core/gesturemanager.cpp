// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "gesturemanager.h"
#include "config/configloader.h"
#include "backend/abstractgesturehandler.h"
#include "actionexecutor.h"
#include "translationmanager.h"

#include <QDebug>
#include <QDBusConnection>

GestureManager::GestureManager(ConfigLoader *loader, ActionExecutor *executor, 
                            TranslationManager *translationManager,
                            AbstractGestureHandler *gestureHandler,
                            QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_gestureHandler(gestureHandler)
    , m_executor(executor)
    , m_translationManager(translationManager)
{
    qRegisterMetaType<GestureInfo>("GestureInfo");
    qRegisterMetaType<QList<GestureInfo>>("QList<GestureInfo>");

    qDBusRegisterMetaType<GestureInfo>();
    qDBusRegisterMetaType<QList<GestureInfo>>();


    // Connect handler signals if not already connected
    connect(m_gestureHandler, &AbstractGestureHandler::activated, this, &GestureManager::onGestureActivated);

    // Connect loader signals for future updates
    connect(m_loader, &ConfigLoader::gestureConfigChanged, this, &GestureManager::onGestureConfigChanged);
    connect(m_loader, &ConfigLoader::configRemoved, this, &GestureManager::onGestureConfigRemoved);
    connect(m_loader, &ConfigLoader::gestureConfigAdded, this, &GestureManager::onGestureConfigAdded);
}

GestureManager::~GestureManager()
{
}

void GestureManager::registerAllGestures()
{
    qDebug() << "GestureManager: Registering all gestures...";

    // Clear existing registrations first
    m_gestureConfigsMap.clear();
    
    // Load existing configs
    QList<GestureConfig> gestures = m_loader->gestures();
    for (const auto &config : gestures) {
        if (registerGesture(config)) {
            m_gestureConfigsMap[config.getId()] = config;
        }
    }

    qDebug() << "GestureManager: Registered" << m_gestureConfigsMap.size() << "gestures";
}

void GestureManager::clearState()
{
    qWarning() << "GestureManager: Clearing internal state due to protocol disconnection";
    m_gestureConfigsMap.clear();
}

QList<GestureInfo> GestureManager::ListAllGestures()
{
    QList<GestureInfo> list;
    for (const auto &config : m_gestureConfigsMap) {    
        GestureInfo info;
        info.id = config.getId();
        info.displayName = config.displayName;
        info.category = config.category;
        info.gestureType = config.gestureType;
        info.fingerCount = config.fingerCount;
        info.direction = config.direction;
        info.triggerType = config.triggerType;
        info.triggerValue = config.triggerValue;
        info.triggerValue = config.triggerValue;
        info.localLanguageName = m_translationManager->translate(config.appId, config.displayName);
        list.append(info);
    }
    return list;
}

bool GestureManager::ModifyGesture(const QString &id, const QStringList &action)
{
    if (!m_gestureConfigsMap.contains(id)) {
        qWarning() << "GestureManager::ModifyGesture: Gesture not found:" << id;
        return false;
    }

    GestureConfig config = m_gestureConfigsMap[id];
    if (!config.enabled || !config.modifiable) {
        qWarning() << "GestureManager::ModifyGesture: Gesture is not modifiable or enabled:" << id
                   << "enabled:" << config.enabled << "modifiable:" << config.modifiable;
        return false;
    }

    // Update config
    config.triggerValue = action;

    // Unregister and re-register
    m_gestureHandler->unregisterGesture(id);
    m_gestureConfigsMap.remove(id);
    if (m_gestureHandler->registerGesture(config)) {
        m_gestureConfigsMap[id] = config;
        m_gestureHandler->commit();
    } else {
        qWarning() << "GestureManager::ModifyGesture: Failed to register gesture:" << id;
        return false;
    }

    // Update DConfig
    m_loader->updateValue(id, "triggerValue", action);

    emit GestureInfosChanged();
    return true;
}

void GestureManager::onGestureConfigAdded(const GestureConfig &config)
{
    if (registerGesture(config)) {
        m_gestureConfigsMap[config.getId()] = config;
        m_gestureHandler->commit();
    }
}

void GestureManager::onGestureConfigChanged(const GestureConfig &config)
{
    if (!m_gestureConfigsMap.contains(config.getId())) {
        if (!config.enabled) {
            // new one, but disabled, skip
            return;
        } else {
            // new one, enable
            if (registerGesture(config)) {
                m_gestureConfigsMap[config.getId()] = config;
                m_gestureHandler->commit();
            }
        }
    } else {
        GestureConfig &old = m_gestureConfigsMap[config.getId()];
        if (!config.enabled) {
            // enable to disable
            m_gestureHandler->unregisterGesture(config.getId());
            m_gestureHandler->commit();
            m_gestureConfigsMap.remove(config.getId());
        } else if (old.fingerCount != config.fingerCount || old.direction != config.direction || old.gestureType != config.gestureType) {
            // update gesture
            m_gestureHandler->unregisterGesture(config.getId());
            m_gestureConfigsMap.remove(config.getId());
            if (registerGesture(config)) {
                m_gestureConfigsMap[config.getId()] = config;
            }
            m_gestureHandler->commit();
        } else {
            // other changes
            m_gestureConfigsMap[config.getId()] = config;
        }
    }
    
    emit GestureInfosChanged();
}

void GestureManager::onGestureConfigRemoved(const QString &id)
{
    if (m_gestureConfigsMap.contains(id)) {
        m_gestureConfigsMap.remove(id);
        m_gestureHandler->unregisterGesture(id);
        m_gestureHandler->commit();

        emit GestureInfosChanged();
    } else {
        qWarning() << "GestureManager: Gesture not found:" << id;
    }
}

void GestureManager::onGestureActivated(const QString &gestureId)
{
    qDebug() << "GestureManager: Gesture activated:" << gestureId;
    
    if (m_gestureConfigsMap.contains(gestureId)) {
        const auto &config = m_gestureConfigsMap[gestureId];
        
        // Execute the action if it's command or app type
        if (config.triggerType == (int)TriggerType::Command || config.triggerType == (int)TriggerType::App) {
            m_executor->execute(config);
        }
        
        emit GestureActivated(gestureId, config.triggerValue);
    }
}

bool GestureManager::registerGesture(const GestureConfig &config)
{
    if (!m_gestureHandler) {
        return false;
    }

    if (!config.isValid()) {
        qWarning() << "GestureManager: Gesture is disabled or invalid, skipping registration:"
                   << "Enabled:" << config.enabled
                   << "AppId:" << config.appId
                   << "DisplayName:" << config.displayName
                   << "GestureType:" << config.gestureType
                   << "FingerCount:" << config.fingerCount
                   << "Direction:" << config.direction;
        return false;
    }

    // Check for conflicts before registering
    auto conflictId = LookupConflictGesture(config.gestureType, config.fingerCount, config.direction);
    if (!conflictId.isEmpty()) {
        qWarning() << "GestureManager: Gesture conflict detected during init:"
                   << "Config:" << config.getId()
                   << "GestureType:" << config.gestureType
                   << "FingerCount:" << config.fingerCount
                   << "Direction:" << config.direction
                   << "Conflicts with:" << conflictId
                   << "- Skipping registration";
        return false;
    }

    return m_gestureHandler->registerGesture(config);
}


QString GestureManager::LookupConflictGesture(int gestureType, int fingerCount, int direction)
{
    for (const GestureConfig &config : m_gestureConfigsMap) {
        if (config.gestureType == gestureType && config.fingerCount == fingerCount && config.direction == direction) {
            return config.getId();
        }
    }

    return QString(); // Empty struct if no conflict
}
