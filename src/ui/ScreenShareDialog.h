// SPDX-License-Identifier: MIT
#pragma once

#include "screenshare/ScreenCapturer.h"

#include <QDialog>

class QComboBox;
class QListWidget;
class QPushButton;

namespace lightcord {

// Lets the user pick a screen or a window, plus frame rate and resolution.
class ScreenShareDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenShareDialog(QWidget *parent = nullptr);

    screenshare::CaptureSource selectedSource() const;
    int frameRate() const;
    int maxHeight() const;

private:
    void reload();

    QVector<screenshare::CaptureSource> m_sources;
    QListWidget *m_list = nullptr;
    QComboBox *m_fps = nullptr;
    QComboBox *m_resolution = nullptr;
    QPushButton *m_startButton = nullptr;
};

} // namespace lightcord
