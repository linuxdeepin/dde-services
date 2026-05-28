// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QMap>

class QTranslator;

class TranslationManager : public QObject
{
    Q_OBJECT
public:
    explicit TranslationManager(QObject *parent = nullptr);
    ~TranslationManager() override;

    void init();
    void reload();

    QString translate(const QString &appId, const QString &key) const;

private slots:
    void onLocaleChanged();

private:
    void loadAppTranslations(const QString &appId);
    QString getTranslationPath() const;

    QMap<QString, QTranslator*> m_translators;
};
