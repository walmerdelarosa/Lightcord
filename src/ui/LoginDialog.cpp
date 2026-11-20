// SPDX-License-Identifier: MIT
#include "LoginDialog.h"

#include "Theme.h"
#include "core/TokenStore.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace lightcord {

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lightcord — Log in"));
    setModal(true);
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(10);

    auto *logo = new QLabel(this);
    logo->setPixmap(Theme::logo(72));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto *title = new QLabel(tr("Welcome to Lightcord"), this);
    title->setObjectName("Heading");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *subtitle = new QLabel(tr("A lightweight, native desktop client for Discord."), this);
    subtitle->setObjectName("Muted");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);
    layout->addSpacing(8);

    auto *typeRow = new QHBoxLayout;
    m_userRadio = new QRadioButton(tr("User token"), this);
    m_botRadio = new QRadioButton(tr("Bot token"), this);
    auto *group = new QButtonGroup(this);
    group->addButton(m_userRadio);
    group->addButton(m_botRadio);
    m_userRadio->setChecked(true);
    typeRow->addWidget(m_userRadio);
    typeRow->addWidget(m_botRadio);
    typeRow->addStretch();
    layout->addLayout(typeRow);

    m_tokenEdit = new QLineEdit(this);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setPlaceholderText(tr("Paste your token here"));
    m_tokenEdit->setClearButtonEnabled(true);
    QAction *reveal = m_tokenEdit->addAction(Theme::icon("eye"), QLineEdit::TrailingPosition);
    reveal->setToolTip(tr("Show / hide token"));
    connect(reveal, &QAction::triggered, this, [this]() {
        m_tokenEdit->setEchoMode(m_tokenEdit->echoMode() == QLineEdit::Password ? QLineEdit::Normal
                                                                                : QLineEdit::Password);
    });
    layout->addWidget(m_tokenEdit);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName("Muted");
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setOpenExternalLinks(true);
    layout->addWidget(m_hintLabel);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setObjectName("Warning");
    m_warningLabel->setWordWrap(true);
    layout->addWidget(m_warningLabel);

    m_rememberCheck = new QCheckBox(TokenStore::isSecure() ? tr("Remember me (stored in Windows Credential Manager)")
                                                           : tr("Remember me (stored unencrypted in the settings file)"),
                                    this);
    m_rememberCheck->setChecked(true);
    layout->addWidget(m_rememberCheck);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("Warning");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    layout->addSpacing(6);
    auto *buttons = new QHBoxLayout;
    auto *quit = new QPushButton(tr("Quit"), this);
    auto *login = new QPushButton(tr("Log in"), this);
    login->setObjectName("Primary");
    login->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(quit);
    buttons->addWidget(login);
    layout->addLayout(buttons);

    connect(quit, &QPushButton::clicked, this, &QDialog::reject);
    connect(login, &QPushButton::clicked, this, [this]() {
        if (token().isEmpty()) {
            setError(tr("Please paste a token first."));
            return;
        }
        accept();
    });
    connect(m_userRadio, &QRadioButton::toggled, this, &LoginDialog::updateHints);
    updateHints();
}

QString LoginDialog::token() const
{
    QString t = m_tokenEdit->text().trimmed();
    if (t.startsWith(QLatin1String("Bot "), Qt::CaseInsensitive))
        t = t.mid(4).trimmed();
    if (t.size() >= 2 && t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')))
        t = t.mid(1, t.size() - 2);
    return t;
}

bool LoginDialog::isBot() const
{
    return m_botRadio->isChecked();
}

bool LoginDialog::remember() const
{
    return m_rememberCheck->isChecked();
}

void LoginDialog::setError(const QString &error)
{
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!error.isEmpty());
}

void LoginDialog::setAccountType(bool bot)
{
    (bot ? m_botRadio : m_userRadio)->setChecked(true);
}

void LoginDialog::updateHints()
{
    if (isBot()) {
        m_hintLabel->setText(
            tr("Create an application in the <a href=\"https://discord.com/developers/applications\">Discord "
               "Developer Portal</a>, add a bot, copy its token and enable the <b>Message Content</b> intent."));
        m_warningLabel->setText(QString());
        m_warningLabel->hide();
    } else {
        m_hintLabel->setText(tr("Lightcord never asks for your password. Your token is only sent to Discord."));
        m_warningLabel->setText(
            tr("Heads-up: third-party clients are not allowed by Discord's Terms of Service. Using a user token "
               "here may get your account restricted or suspended. Use at your own risk."));
        m_warningLabel->show();
    }
}

} // namespace lightcord
