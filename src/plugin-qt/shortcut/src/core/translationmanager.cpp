// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "translationmanager.h"

#include <QCoreApplication>
#include <QTranslator>
#include <QDebug>
#include <QLocale>
#include <QDir>

TranslationManager::TranslationManager(QObject *parent)
    : QObject(parent)
{

}

TranslationManager::~TranslationManager()
{
    qDeleteAll(m_translators);
}

void TranslationManager::init()
{
    reload();
}

void TranslationManager::reload()
{
    // Delete old translators
    qDeleteAll(m_translators);
    m_translators.clear();

    QString currentLanguage = QLocale::system().name();
    
    QDir dir(getTranslationPath());
    if (!dir.exists()) {
        qWarning() << "Translation directory does not exist:" << dir.absolutePath();
        return;
    }

    // Iterate over all subdirectories (each is an appId)
    QStringList appDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &appId : appDirs) {
        QDir appDir(dir.filePath(appId));
        
        // Look for {appId}_{lang}.qm
        QString fileName = QString("%1_%2.qm").arg(appId).arg(currentLanguage);
        if (!appDir.exists(fileName)) {
            qWarning() << "Translation file does not exist:" << fileName << "for language:" << currentLanguage;
            continue;
        }
        
        QTranslator *translator = new QTranslator(this);
        if (translator->load(appDir.absoluteFilePath(fileName))) {
            qInfo() << "Loaded translator for appId:" << appId << "from" << appDir.absoluteFilePath(fileName);
            m_translators.insert(appId, translator);
        } else {
            qWarning() << "Failed to load translator for appId:" << appId << "from" << appDir.absoluteFilePath(fileName);
            delete translator;
        }
    }
}

QString TranslationManager::translate(const QString &appId, const QString &key) const
{
    if (m_translators.contains(appId)) {
        // Use appId as context as per proposal
        QString result = m_translators[appId]->translate(appId.toUtf8().constData(), key.toUtf8().constData());
        if (!result.isEmpty()) {
            return result;
        }
    }

    qWarning() << "Translation not found for key:" << key << "in appId:" << appId;
    return key; // Fallback to key
}

void TranslationManager::onLocaleChanged()
{
    reload();
}

QString TranslationManager::getTranslationPath() const
{
    QString envPath = qEnvironmentVariable("DDE_SHORTCUT_I18N_PATH");
    if (!envPath.isEmpty()) {
        return envPath;
    }
    return "/usr/share/deepin/org.deepin.dde.keybinding/translations";
}
