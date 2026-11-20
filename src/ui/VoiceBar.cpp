// SPDX-License-Identifier: MIT
#include "VoiceBar.h"

#include "Theme.h"

#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

namespace lightcord {

VoiceBar::VoiceBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("VoiceBar");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- Screen share preview -------------------------------------------
    m_previewPanel = new QWidget(this);
    m_previewPanel->setObjectName("SharePreview");
    m_previewPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto *pv = new QVBoxLayout(m_previewPanel);
    pv->setContentsMargins(10, 8, 10, 8);
    pv->setSpacing(6);
    auto *pvHeader = new QHBoxLayout;
    auto *live = new QLabel(tr("LIVE"), m_previewPanel);
    live->setObjectName("LiveBadge");
    m_previewTitle = new QLabel(m_previewPanel);
    m_previewTitle->setObjectName("UserName");
    m_previewTitle->setTextInteractionFlags(Qt::NoTextInteraction);
    m_previewFps = new QLabel(m_previewPanel);
    m_previewFps->setObjectName("UserStatus");
    m_stopShareButton = makeButton(tr("Stop sharing"), false);
    pvHeader->addWidget(live);
    pvHeader->addWidget(m_previewTitle, 1);
    pvHeader->addWidget(m_previewFps);
    pvHeader->addWidget(m_stopShareButton);
    pv->addLayout(pvHeader);
    m_previewImage = new QLabel(m_previewPanel);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumHeight(120);
    pv->addWidget(m_previewImage);
    m_previewPanel->hide();
    root->addWidget(m_previewPanel);
    connect(m_stopShareButton, &QToolButton::clicked, this, &VoiceBar::stopShareClicked);

    // --- Voice connection row -------------------------------------------
    m_voicePanel = new QWidget(this);
    auto *vr = new QHBoxLayout(m_voicePanel);
    vr->setContentsMargins(10, 6, 6, 0);
    m_voiceLabel = new QLabel(m_voicePanel);
    m_voiceLabel->setObjectName("UserStatus");
    m_voiceLabel->setWordWrap(true);
    m_voiceLabel->setTextFormat(Qt::RichText);
    m_leaveButton = makeButton(tr("Leave voice channel"), false);
    vr->addWidget(m_voiceLabel, 1);
    vr->addWidget(m_leaveButton);
    m_voicePanel->hide();
    root->addWidget(m_voicePanel);
    connect(m_leaveButton, &QToolButton::clicked, this, &VoiceBar::leaveVoiceClicked);

    // --- User row ---------------------------------------------------------
    auto *userRow = new QWidget(this);
    auto *ur = new QHBoxLayout(userRow);
    ur->setContentsMargins(10, 8, 6, 8);
    ur->setSpacing(4);

    m_avatar = new QLabel(userRow);
    m_avatar->setFixedSize(32, 32);
    ur->addWidget(m_avatar);
    ur->addSpacing(4);

    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    m_nameLabel = new QLabel(userRow);
    m_nameLabel->setObjectName("UserName");
    m_statusLabel = new QLabel(userRow);
    m_statusLabel->setObjectName("UserStatus");
    names->addWidget(m_nameLabel);
    names->addWidget(m_statusLabel);
    ur->addLayout(names, 1);

    m_muteButton = makeButton(tr("Mute microphone"), true);
    m_muteButton->setObjectName("DangerToggle");
    m_deafenButton = makeButton(tr("Deafen"), true);
    m_deafenButton->setObjectName("DangerToggle");
    m_noiseButton = makeButton(tr("Noise suppression"), true);
    m_shareButton = makeButton(tr("Share your screen"), false);
    m_settingsButton = makeButton(tr("Settings"), false);
    for (QToolButton *b : {m_muteButton, m_deafenButton, m_noiseButton, m_shareButton, m_settingsButton})
        ur->addWidget(b);
    root->addWidget(userRow);

    connect(m_muteButton, &QToolButton::toggled, this, [this](bool on) {
        refreshIcons();
        emit muteToggled(on);
    });
    connect(m_deafenButton, &QToolButton::toggled, this, [this](bool on) {
        refreshIcons();
        emit deafenToggled(on);
    });
    connect(m_noiseButton, &QToolButton::toggled, this, [this](bool on) {
        m_noiseButton->setToolTip(on ? tr("Noise suppression: on") : tr("Noise suppression: off"));
        refreshIcons();
        emit noiseSuppressionToggled(on);
    });
    connect(m_shareButton, &QToolButton::clicked, this, &VoiceBar::screenShareClicked);
    connect(m_settingsButton, &QToolButton::clicked, this, &VoiceBar::settingsClicked);

    refreshIcons();
}

QToolButton *VoiceBar::makeButton(const QString &tooltip, bool checkable)
{
    auto *b = new QToolButton(this);
    b->setToolTip(tooltip);
    b->setCheckable(checkable);
    b->setIconSize(QSize(18, 18));
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void VoiceBar::refreshIcons()
{
    const ThemePalette &p = Theme::palette();
    m_muteButton->setIcon(m_muteButton->isChecked() ? Theme::icon("mic-off", p.danger) : Theme::icon("mic"));
    m_deafenButton->setIcon(m_deafenButton->isChecked() ? Theme::icon("headphones-off", p.danger)
                                                        : Theme::icon("headphones"));
    m_noiseButton->setIcon(Theme::icon("waves", m_noiseButton->isChecked() ? p.accent : p.textMuted));
    m_shareButton->setIcon(Theme::icon("screen", m_shareButton->property("sharing").toBool() ? p.accent : p.text));
    m_settingsButton->setIcon(Theme::icon("settings"));
    m_stopShareButton->setIcon(Theme::icon("phone-off", p.danger));
    m_leaveButton->setIcon(Theme::icon("phone-off", p.danger));
    updateAvatar(m_userName);
}

void VoiceBar::updateAvatar(const QString &name)
{
    const ThemePalette &p = Theme::palette();
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(QSize(32, 32) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(p.accentSoft);
    painter.drawEllipse(QRectF(0, 0, 32, 32));
    painter.setPen(p.accent);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(15);
    painter.setFont(f);
    const QString initial = name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
    painter.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, initial);
    painter.end();
    m_avatar->setPixmap(pm);
}

void VoiceBar::setUser(const QString &name, const QString &status)
{
    m_userName = name;
    m_nameLabel->setText(name);
    m_statusLabel->setText(status);
    updateAvatar(name);
}

void VoiceBar::setMuted(bool muted)
{
    m_muteButton->setChecked(muted);
}

void VoiceBar::setDeafened(bool deafened)
{
    m_deafenButton->setChecked(deafened);
}

void VoiceBar::setNoiseSuppression(bool enabled)
{
    QSignalBlocker block(m_noiseButton);
    m_noiseButton->setChecked(enabled);
    m_noiseButton->setToolTip(enabled ? tr("Noise suppression: on") : tr("Noise suppression: off"));
    refreshIcons();
}

void VoiceBar::setSharing(bool sharing, const QString &title)
{
    m_shareButton->setProperty("sharing", sharing);
    m_previewPanel->setVisible(sharing);
    m_previewTitle->setText(title);
    if (!sharing)
        m_previewImage->clear();
    refreshIcons();
}

void VoiceBar::setSharePreview(const QImage &frame, double fps)
{
    const int w = qMax(160, m_previewPanel->width() - 20);
    m_previewImage->setPixmap(QPixmap::fromImage(frame.scaledToWidth(w, Qt::FastTransformation)));
    m_previewFps->setText(tr("%1 fps · %2p").arg(fps, 0, 'f', 0).arg(frame.height()));
}

void VoiceBar::setVoiceStatus(const QString &channelName, const QString &status, const QString &security)
{
    m_voicePanel->setVisible(!channelName.isEmpty());
    if (channelName.isEmpty())
        return;
    const ThemePalette &p = Theme::palette();
    QString html = QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>")
                       .arg(p.success.name(), status.toHtmlEscaped());
    html += QStringLiteral("<br><span style=\"color:%1;\">🔊 %2</span>")
                .arg(p.textStrong.name(), channelName.toHtmlEscaped());
    if (!security.isEmpty())
        html += QStringLiteral("<br><span style=\"color:%1;\">%2</span>").arg(p.textMuted.name(), security.toHtmlEscaped());
    m_voiceLabel->setText(html);
}

} // namespace lightcord
