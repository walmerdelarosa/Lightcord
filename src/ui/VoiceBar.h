// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace lightcord {

// Bottom-left panel: current user, voice controls (mute, deafen, noise
// suppression), screen share button, settings, plus a small live preview
// while a screen or window is being shared.
class VoiceBar : public QWidget {
    Q_OBJECT
public:
    explicit VoiceBar(QWidget *parent = nullptr);

    void setUser(const QString &name, const QString &status);
    void setMuted(bool muted);
    void setDeafened(bool deafened);
    void setNoiseSuppression(bool enabled);
    void setSharing(bool sharing, const QString &title = QString());
    void setSharePreview(const QImage &frame, double fps);
    void setVoiceChannel(const QString &channelName); // empty = not in voice
    void refreshIcons();

signals:
    void muteToggled(bool muted);
    void deafenToggled(bool deafened);
    void noiseSuppressionToggled(bool enabled);
    void screenShareClicked();
    void stopShareClicked();
    void settingsClicked();
    void leaveVoiceClicked();

private:
    QToolButton *makeButton(const QString &tooltip, bool checkable);
    void updateAvatar(const QString &name);

    QWidget *m_previewPanel = nullptr;
    QLabel *m_previewImage = nullptr;
    QLabel *m_previewTitle = nullptr;
    QLabel *m_previewFps = nullptr;
    QToolButton *m_stopShareButton = nullptr;

    QWidget *m_voicePanel = nullptr;
    QLabel *m_voiceLabel = nullptr;
    QToolButton *m_leaveButton = nullptr;

    QLabel *m_avatar = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QToolButton *m_muteButton = nullptr;
    QToolButton *m_deafenButton = nullptr;
    QToolButton *m_noiseButton = nullptr;
    QToolButton *m_shareButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    QString m_userName;
};

} // namespace lightcord
