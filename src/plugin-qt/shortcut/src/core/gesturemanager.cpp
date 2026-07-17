// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "gesturemanager.h"
#include "actionexecutor.h"
#include "gestureactioncatalog.h"
#include "serviceactionexecutor.h"
#include "translationmanager.h"
#include "backend/abstractgesturehandler.h"
#include "backend/x11/x11gestureactionexecutor.h"
#include "config/configloader.h"

#include <DGuiApplicationHelper>

#include <QDBusMetaType>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>

DGUI_USE_NAMESPACE

GestureManager::GestureManager(ConfigLoader *loader, ActionExecutor *executor,
                               TranslationManager *translationManager,
                               AbstractGestureHandler *gestureHandler,
                               X11GestureActionExecutor *x11ActionExecutor,
                               ServiceActionExecutor *serviceActionExecutor,
                               QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_gestureHandler(gestureHandler)
    , m_executor(executor)
    , m_translationManager(translationManager)
    , m_x11ActionExecutor(x11ActionExecutor)
    , m_serviceActionExecutor(serviceActionExecutor)
    , m_isWayland(DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform))
{
    qRegisterMetaType<GestureInfo>("GestureInfo");
    qRegisterMetaType<QList<GestureInfo>>("QList<GestureInfo>");
    qRegisterMetaType<GestureActionInfo>("GestureActionInfo");
    qRegisterMetaType<QList<GestureActionInfo>>("QList<GestureActionInfo>");
    qRegisterMetaType<GestureConfig>("GestureConfig");

    qDBusRegisterMetaType<GestureInfo>();
    qDBusRegisterMetaType<QList<GestureInfo>>();
    qDBusRegisterMetaType<GestureActionInfo>();
    qDBusRegisterMetaType<QList<GestureActionInfo>>();

    if (m_gestureHandler) {
        connect(m_gestureHandler, &AbstractGestureHandler::activated,
                this, &GestureManager::onGestureActivated);
        connect(m_gestureHandler, &AbstractGestureHandler::availabilityChanged,
                this, &GestureManager::onHandlerAvailabilityChanged);
    }
    connect(m_loader, &ConfigLoader::gestureConfigChanged,
            this, &GestureManager::onGestureConfigChanged);
    connect(m_loader, &ConfigLoader::configRemoved,
            this, &GestureManager::onGestureConfigRemoved);
    connect(m_loader, &ConfigLoader::gestureConfigAdded,
            this, &GestureManager::onGestureConfigAdded);
}

GestureManager::~GestureManager() = default;

void GestureManager::registerAllGestures()
{
    m_configuredGestures.clear();
    m_activeGestureIds.clear();

    for (const GestureConfig &config : m_loader->gestures()) {
        m_configuredGestures.insert(config.getId(), config);
        if (registerGesture(config))
            setGestureActive(config);
    }

    qInfo() << "GestureManager: configured" << m_configuredGestures.size()
            << "active" << m_activeGestureIds.size()
            << "backend" << (m_isWayland ? "treeland" : "x11");
}

void GestureManager::clearState()
{
    if (m_activeGestureIds.isEmpty() && (!m_isWayland || m_configuredGestures.isEmpty()))
        return;
    m_activeGestureIds.clear();
    if (m_isWayland)
        m_configuredGestures.clear();
    emit GestureInfosChanged();
}

QList<GestureInfo> GestureManager::ListAllGestures()
{
    QList<GestureConfig> gestures = m_configuredGestures.values();
    std::sort(gestures.begin(), gestures.end(),
              [](const GestureConfig &left, const GestureConfig &right) {
        if (left.displayOrder != right.displayOrder) {
            if (left.displayOrder < 0)
                return false;
            if (right.displayOrder < 0)
                return true;
            return left.displayOrder < right.displayOrder;
        }
        return left.getId() < right.getId();
    });

    QList<GestureInfo> list;
    list.reserve(gestures.size());
    for (const GestureConfig &config : std::as_const(gestures))
        list.append(toGestureInfo(config));
    return list;
}

QList<GestureActionInfo> GestureManager::availableActions(const GestureConfig &config) const
{
    QList<GestureActionInfo> result;
    const GestureActionId configuredAction = actionId(config);
    const QString configuredValue = config.triggerValue.value(0).trimmed();
    const GestureBackend backend = m_isWayland ? GestureBackend::Treeland : GestureBackend::X11;
    bool configuredActionFound = false;

    for (const GestureActionMetadata &action : GestureActionCatalog::actionsFor(config, backend)) {
        GestureActionInfo item;
        item.actionId = GestureActionCatalog::idString(action.id);
        item.displayName = translateServiceText(GestureActionCatalog::displayNameSource(action));
        item.supported = true;
        configuredActionFound = configuredActionFound || action.id == configuredAction;
        result.append(item);
    }

    // Keep an unsupported persisted value visible as the current selection,
    // but do not expose it as a selectable action for the active backend.
    // This is needed when switching between X11 and Treeland action catalogs.
    if (!configuredValue.isEmpty() && !configuredActionFound) {
        GestureActionInfo item;
        item.actionId = configuredValue;
        const GestureActionId knownAction = GestureActionCatalog::resolveKnownActionId(configuredValue);
        item.displayName = knownAction == GestureActionId::Invalid
                ? translateServiceText(QStringLiteral("Unknown gesture action"))
                : translateServiceText(GestureActionCatalog::displayNameSource(knownAction));
        item.supported = false;
        item.unavailableReason = translateServiceText(GestureActionCatalog::unsupportedReasonSource());
        result.prepend(item);
    }

    return result;
}

bool GestureManager::ModifyGesture(const QString &id, const QStringList &action)
{
    const auto it = m_configuredGestures.constFind(id);
    if (it == m_configuredGestures.constEnd() || action.size() != 1) {
        qWarning() << "GestureManager::ModifyGesture: invalid request:" << id << action;
        return false;
    }

    const GestureConfig oldConfig = it.value();
    if (!oldConfig.enabled || !oldConfig.modifiable
            || oldConfig.category == QLatin1String(CategoryKey::Custom)
            || oldConfig.triggerType != int(TriggerType::Action)) {
        return false;
    }

    GestureConfig newConfig = oldConfig;
    const GestureBackend backend = m_isWayland ? GestureBackend::Treeland : GestureBackend::X11;
    const GestureActionId newActionId = GestureActionCatalog::resolveActionId(
            newConfig, action.first(), backend);
    const GestureActionMetadata *definition = GestureActionCatalog::find(
            newConfig, newActionId, backend);
    if (!definition) {
        qWarning() << "GestureManager::ModifyGesture: unknown action:" << id << action.first();
        return false;
    }

    if (newActionId == actionId(oldConfig))
        return true;

    newConfig.triggerValue = {GestureActionCatalog::idString(newActionId)};
    const bool oldWasActive = setGestureInactive(id);
    if (oldWasActive)
        m_gestureHandler->unregisterGesture(id);

    const bool disabled = newActionId == GestureActionId::Disable;
    const bool registered = disabled || registerGesture(newConfig);
    if (registered && !disabled)
        setGestureActive(newConfig);
    if (!registered) {
        if (oldWasActive && registerGesture(oldConfig))
            setGestureActive(oldConfig);
        m_gestureHandler->commitSync();
        return false;
    }
    if (!m_gestureHandler->commitSync()) {
        return false;
    }

    m_configuredGestures.insert(id, newConfig);

    if (!m_loader->updateValue(id, QStringLiteral("triggerValue"),
                               QVariantList{int(newActionId)})) {
        m_configuredGestures.insert(id, oldConfig);
        m_gestureHandler->unregisterGesture(id);
        setGestureInactive(id);
        if (oldWasActive && registerGesture(oldConfig))
            setGestureActive(oldConfig);
        m_gestureHandler->commitSync();
        return false;
    }

    emit GestureInfosChanged();
    return true;
}

void GestureManager::onGestureConfigAdded(const GestureConfig &config)
{
    m_configuredGestures.insert(config.getId(), config);
    if (registerGesture(config)) {
        setGestureActive(config);
        m_gestureHandler->commitSync();
    }
    emit GestureInfosChanged();
}

void GestureManager::onGestureConfigChanged(const GestureConfig &config)
{
    const QString id = config.getId();
    const auto oldIt = m_configuredGestures.constFind(id);
    if (oldIt != m_configuredGestures.constEnd() && oldIt.value() == config)
        return;

    m_configuredGestures.insert(id, config);
    if (setGestureInactive(id))
        m_gestureHandler->unregisterGesture(id);

    if (registerGesture(config))
        setGestureActive(config);
    m_gestureHandler->commitSync();
    emit GestureInfosChanged();
}

void GestureManager::onGestureConfigRemoved(const QString &id)
{
    if (m_configuredGestures.remove(id) == 0)
        return;

    if (setGestureInactive(id)) {
        m_gestureHandler->unregisterGesture(id);
        m_gestureHandler->commitSync();
    }
    emit GestureInfosChanged();
}

void GestureManager::onGestureActivated(const QString &gestureId)
{
    if (!m_activeGestureIds.contains(gestureId))
        return;

    const auto it = m_configuredGestures.constFind(gestureId);
    if (it == m_configuredGestures.constEnd())
        return;

    const GestureConfig &config = it.value();
    if (config.triggerType == int(TriggerType::Action) && !isActionSupported(config)) {
        qWarning() << "GestureManager: ignoring unsupported gesture action: gesture"
                   << gestureId << "configured action" << config.triggerValue.value(0)
                   << "backend" << (m_isWayland ? "treeland" : "x11");
        return;
    }

    if (config.triggerType == int(TriggerType::Command)
            || config.triggerType == int(TriggerType::App)) {
        m_executor->execute(config);
    } else {
        const GestureBackend backend = m_isWayland ? GestureBackend::Treeland
                                                   : GestureBackend::X11;
        if (GestureActionCatalog::targetFor(actionId(config), backend)
                == GestureActionTarget::Service) {
            if (!m_serviceActionExecutor
                    || !m_serviceActionExecutor->execute(actionId(config), gestureId)) {
                return;
            }
        } else if (!m_isWayland && (!m_x11ActionExecutor || !m_x11ActionExecutor->execute(config))) {
            return;
        }
    }
    emit GestureActivated(gestureId, config.triggerValue);
}

void GestureManager::onHandlerAvailabilityChanged(bool available)
{
    m_activeGestureIds.clear();
    if (available) {
        for (const GestureConfig &config : std::as_const(m_configuredGestures)) {
            if (registerGesture(config))
                setGestureActive(config);
        }
        m_gestureHandler->commitSync();
    }
    emit GestureInfosChanged();
}

bool GestureManager::registerGesture(const GestureConfig &config)
{
    if (!m_gestureHandler || !config.isValid())
        return false;
    const GestureActionId configuredAction = actionId(config);
    if (configuredAction == GestureActionId::Disable)
        return false;

    const QString conflictId = LookupConflictGesture(config.gestureType,
                                                      config.fingerCount,
                                                      config.direction);
    if (!conflictId.isEmpty() && conflictId != config.getId()) {
        qWarning() << "GestureManager: gesture conflict:" << config.getId() << conflictId;
        return false;
    }
    GestureConfig backendConfig = config;
    if (config.triggerType == int(TriggerType::Action)) {
        const GestureBackend backend = m_isWayland ? GestureBackend::Treeland : GestureBackend::X11;
        const GestureActionMetadata *definition = GestureActionCatalog::find(
                config, configuredAction, backend);
        if (!definition) {
            qWarning() << "GestureManager: unsupported gesture action: gesture"
                       << config.getId() << "configured action"
                       << config.triggerValue.value(0) << "backend"
                       << (m_isWayland ? "treeland" : "x11");
            return false;
        }
        const GestureActionId registrationAction =
                GestureActionCatalog::registrationActionId(configuredAction, backend);
        backendConfig.triggerValue = {GestureActionCatalog::idString(registrationAction)};
    }
    return m_gestureHandler->registerGesture(backendConfig);
}

void GestureManager::setGestureActive(const GestureConfig &config)
{
    m_activeGestureIds.insert(config.getId());
}

bool GestureManager::setGestureInactive(const QString &id)
{
    return m_activeGestureIds.remove(id) > 0;
}

QString GestureManager::LookupConflictGesture(int gestureType, int fingerCount, int direction)
{
    for (const QString &id : m_activeGestureIds) {
        const auto it = m_configuredGestures.constFind(id);
        if (it == m_configuredGestures.constEnd())
            continue;

        const GestureConfig &config = it.value();
        if (config.gestureType == gestureType
                && config.fingerCount == fingerCount
                && config.direction == direction) {
            return id;
        }
    }
    return QString();
}

GestureInfo GestureManager::toGestureInfo(const GestureConfig &config) const
{
    GestureInfo info;
    info.id = config.getId();
    info.displayName = config.displayName;
    info.category = config.category;
    info.gestureType = config.gestureType;
    info.fingerCount = config.fingerCount;
    info.direction = config.direction;
    info.triggerType = config.triggerType;
    const GestureActionId configuredAction = actionId(config);
    info.triggerValue = config.triggerType == int(TriggerType::Action)
            && configuredAction != GestureActionId::Invalid
            ? QStringList{GestureActionCatalog::idString(configuredAction)}
            : config.triggerValue;
    info.localLanguageName = m_translationManager->translate(config.appId, config.displayName);
    info.localLanguageCategory = m_translationManager->translate(config.appId, config.category);
    info.isCustom = config.category == QLatin1String(CategoryKey::Custom);
    info.availableActions = availableActions(config);
    return info;
}

GestureActionId GestureManager::actionId(const GestureConfig &config) const
{
    if (config.triggerType != int(TriggerType::Action) || config.triggerValue.isEmpty())
        return GestureActionId::Invalid;
    return GestureActionCatalog::resolveKnownActionId(config.triggerValue.first());
}

bool GestureManager::isActionSupported(const GestureConfig &config) const
{
    if (config.triggerType != int(TriggerType::Action))
        return true;
    const GestureBackend backend = m_isWayland ? GestureBackend::Treeland : GestureBackend::X11;
    return GestureActionCatalog::find(config, actionId(config), backend) != nullptr;
}

QString GestureManager::translateServiceText(const QString &source) const
{
    return m_translationManager->translate(QLatin1String("org.deepin.dde.keybinding"), source);
}

QString GestureManager::GetGestureAvaiableActions(const QString &actionType, int fingerNum)
{
    GestureConfig config;
    config.gestureType = actionType == QLatin1String("swipe") ? int(GestureType::Swipe)
                                                               : int(GestureType::Hold);
    config.fingerCount = fingerNum;

    QJsonArray array;
    const GestureBackend backend = m_isWayland ? GestureBackend::Treeland : GestureBackend::X11;
    for (const GestureActionMetadata &action : GestureActionCatalog::actionsFor(config, backend)) {
        QJsonObject object;
        object.insert(QStringLiteral("Name"), GestureActionCatalog::idString(action.id));
        object.insert(QStringLiteral("Description"),
                      translateServiceText(GestureActionCatalog::displayNameSource(action)));
        object.insert(QStringLiteral("Supported"), true);
        object.insert(QStringLiteral("Reason"), QString());
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
