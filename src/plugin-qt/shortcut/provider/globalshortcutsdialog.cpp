// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "globalshortcutsdialog.h"

#include "shortcutcapturecontroller.h"
#include "waylandforeignparent.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

ShortcutEditorRow::ShortcutEditorRow(const PortalShortcutUpdate &shortcut,
                                     ShortcutCaptureController *captureController,
                                     QWidget *parent)
    : QWidget(parent)
    , m_shortcut(shortcut)
    , m_captureController(captureController)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 6);

    auto *description = new QLabel(shortcut.description, this);
    description->setTextFormat(Qt::PlainText);
    description->setToolTip(Qt::convertFromPlainText(shortcut.description));
    description->setText(description->fontMetrics().elidedText(shortcut.description,
                                                                Qt::ElideRight,
                                                                520));
    description->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(description);

    auto *editorLayout = new QHBoxLayout;
    m_enabled = new QCheckBox(tr("Enabled"), this);
    m_enabled->setChecked(shortcut.enabled && !shortcut.hotkeys.isEmpty());
    editorLayout->addWidget(m_enabled);

    m_hotkey = new QLineEdit(this);
    m_hotkey->setReadOnly(true);
    m_hotkey->setPlaceholderText(tr("Not set"));
    setHotkey(shortcut.hotkeys.value(0));
    editorLayout->addWidget(m_hotkey, 1);

    m_record = new QPushButton(tr("Record"), this);
    m_clear = new QPushButton(tr("Clear"), this);
    m_restore = new QPushButton(tr("Restore Suggested"), this);
    m_restore->setEnabled(!shortcut.preferredKeys.isEmpty());
    editorLayout->addWidget(m_record);
    editorLayout->addWidget(m_clear);
    editorLayout->addWidget(m_restore);
    layout->addLayout(editorLayout);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->hide();
    layout->addWidget(m_status);

    connect(m_record, &QPushButton::clicked, this, &ShortcutEditorRow::startCapture);
    connect(m_clear, &QPushButton::clicked, this, [this] {
        m_captureController->cancel(this);
        setHotkey({});
        m_enabled->setChecked(false);
        m_status->hide();
    });
    connect(m_restore, &QPushButton::clicked, this, [this] {
        m_captureController->cancel(this);
        setHotkey(m_shortcut.preferredKeys.value(0));
        m_enabled->setChecked(!m_shortcut.preferredKeys.isEmpty());
        m_status->hide();
    });
}

PortalShortcutUpdate ShortcutEditorRow::update() const
{
    PortalShortcutUpdate result = m_shortcut;
    result.hotkeys = m_hotkey->property("portableHotkey").toString().isEmpty()
            ? QStringList{}
            : QStringList{m_hotkey->property("portableHotkey").toString()};
    result.enabled = m_enabled->isChecked() && !result.hotkeys.isEmpty();
    return result;
}

void ShortcutEditorRow::setInteractionEnabled(bool enabled)
{
    m_enabled->setEnabled(enabled);
    m_record->setEnabled(enabled);
    m_clear->setEnabled(enabled);
    m_restore->setEnabled(enabled && !m_shortcut.preferredKeys.isEmpty());
}

void ShortcutEditorRow::startCapture()
{
    QWidget *topLevel = window();
    if (!topLevel)
        return;
    topLevel->winId();
    QWindow *targetWindow = topLevel->windowHandle();
    m_status->setText(tr("Press the new shortcut now…"));
    m_status->show();
    const bool anotherCaptureActive = m_captureController->isActive();
    if (!m_captureController->capture(
                this,
                targetWindow,
                [this](const QString &shortcut) {
                    setHotkey(shortcut);
                    m_enabled->setChecked(true);
                    m_status->hide();
                },
                [this](const QString &reason) {
                    m_status->setText(reason);
                    m_status->show();
                })) {
        m_status->setText(anotherCaptureActive
                                  ? tr("Another shortcut is being recorded")
                                  : tr("Shortcut recording is unavailable"));
        m_status->show();
    }
}

void ShortcutEditorRow::setHotkey(const QString &hotkey)
{
    m_hotkey->setProperty("portableHotkey", hotkey);
    const QKeySequence sequence = QKeySequence::fromString(hotkey,
                                                            QKeySequence::PortableText);
    m_hotkey->setText(sequence.isEmpty()
                              ? hotkey
                              : sequence.toString(QKeySequence::NativeText));
}

GlobalShortcutsDialog::GlobalShortcutsDialog(
        Mode mode,
        const QString &appId,
        const QString &parentWindow,
        const PortalShortcutUpdateList &shortcuts,
        ShortcutCaptureController *captureController,
        WaylandForeignParent *foreignParent,
        QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_parentWindow(parentWindow)
    , m_captureController(captureController)
    , m_foreignParent(foreignParent)
{
    setWindowTitle(mode == Mode::Bind ? tr("Allow Global Shortcuts")
                                      : tr("Configure Global Shortcuts"));
    resize(620, 480);

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(
            mode == Mode::Bind
                    ? tr("%1 wants to register these global shortcuts").arg(appId)
                    : tr("Global shortcuts for %1").arg(appId),
            this);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setToolTip(Qt::convertFromPlainText(appId));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *explanation = new QLabel(
            mode == Mode::Bind
                    ? tr("These shortcuts work while the application is running. You can leave an action unset.")
                    : tr("Changes affect this application's active shortcut sessions."),
            this);
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    m_rowsContainer = new QWidget(scrollArea);
    auto *rowsLayout = new QVBoxLayout(m_rowsContainer);
    for (const PortalShortcutUpdate &shortcut : shortcuts) {
        auto *row = new ShortcutEditorRow(shortcut, captureController, m_rowsContainer);
        m_rows.append(row);
        rowsLayout->addWidget(row);
    }
    rowsLayout->addStretch();
    scrollArea->setWidget(m_rowsContainer);
    layout->addWidget(scrollArea, 1);

    m_error = new QLabel(this);
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: palette(bright-text);"));
    m_error->hide();
    layout->addWidget(m_error);

    if (mode == Mode::Bind) {
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Allow"));
        connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
            Q_EMIT bindAccepted(updates());
        });
    } else {
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
        connect(m_buttons->button(QDialogButtonBox::Apply),
                &QPushButton::clicked,
                this,
                [this] { Q_EMIT applyRequested(updates()); });
        connect(m_buttons->button(QDialogButtonBox::Close),
                &QPushButton::clicked,
                this,
                &QDialog::reject);
    }
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this] {
        for (ShortcutEditorRow *row : m_rows)
            m_captureController->cancel(row);
        if (m_mode == Mode::Bind && !m_silentClose)
            Q_EMIT bindCancelled();
    });
    layout->addWidget(m_buttons);
}

PortalShortcutUpdateList GlobalShortcutsDialog::updates() const
{
    PortalShortcutUpdateList result;
    result.reserve(m_rows.size());
    for (const ShortcutEditorRow *row : m_rows)
        result.append(row->update());
    return result;
}

void GlobalShortcutsDialog::setBusy(bool busy)
{
    for (ShortcutEditorRow *row : m_rows)
        row->setInteractionEnabled(!busy);
    m_buttons->setEnabled(!busy);
}

void GlobalShortcutsDialog::showError(const QString &message)
{
    m_error->setText(message);
    m_error->setVisible(!message.isEmpty());
}

void GlobalShortcutsDialog::cancelSilently()
{
    m_silentClose = true;
    for (ShortcutEditorRow *row : m_rows)
        m_captureController->cancel(row);
    reject();
}

void GlobalShortcutsDialog::present(const QString &activationToken)
{
    show();
    raise();
    if (!m_parentApplied && !m_parentApplyPending) {
        m_parentApplyPending = true;
        QTimer::singleShot(0, this, &GlobalShortcutsDialog::applyParentWindow);
    }
    if (!activationToken.isEmpty()) {
        QTimer::singleShot(0, this, [this, activationToken] {
            WaylandForeignParent::applyActivationToken(windowHandle(), activationToken);
        });
    } else if (windowHandle()) {
        windowHandle()->requestActivate();
    }
}

void GlobalShortcutsDialog::applyParentWindow()
{
    m_parentApplyPending = false;
    winId();
    QWindow *child = windowHandle();
    if (!child) {
        m_parentApplied = true;
        return;
    }

    if (m_parentWindow.startsWith(QLatin1String("x11:"))) {
        bool ok = false;
        const WId id = QStringView(m_parentWindow).mid(4).toULongLong(&ok, 16);
        if (!ok || id == 0) {
            m_parentApplied = true;
            return;
        }
        QWindow *parent = QWindow::fromWinId(id);
        if (!parent) {
            m_parentApplied = true;
            return;
        }
        connect(this, &QObject::destroyed, parent, &QObject::deleteLater);
        child->setTransientParent(parent);
        m_parentApplied = true;
        return;
    }

    if (m_parentWindow.startsWith(QLatin1String("wayland:")) && m_foreignParent) {
        if (m_foreignParent->apply(child, m_parentWindow.mid(8))) {
            m_parentApplied = true;
            return;
        }
        if (++m_parentApplyAttempts < 20) {
            m_parentApplyPending = true;
            QTimer::singleShot(50, this, &GlobalShortcutsDialog::applyParentWindow);
            return;
        }
    }
    m_parentApplied = true;
}
