// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;

namespace lightcord {

namespace audio {
class AudioPipeline;
}

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

signals:
    void logoutRequested();

private:
    QWidget *buildAppearanceTab();
    QWidget *buildAudioTab();
    QWidget *buildScreenShareTab();
    QWidget *buildAdvancedTab();

    void populateDevices();
    void saveNoiseSettings();
    void toggleMicTest();

    // Audio
    audio::AudioPipeline *m_pipeline = nullptr;
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_outputCombo = nullptr;
    QCheckBox *m_nsEnabled = nullptr;
    QSlider *m_nsStrength = nullptr;
    QLabel *m_nsStrengthLabel = nullptr;
    QCheckBox *m_gateEnabled = nullptr;
    QSlider *m_gateThreshold = nullptr;
    QLabel *m_gateThresholdLabel = nullptr;
    QSpinBox *m_gateHold = nullptr;
    QPushButton *m_micTestButton = nullptr;
    QCheckBox *m_monitorCheck = nullptr;
    QProgressBar *m_inputMeter = nullptr;
    QProgressBar *m_outputMeter = nullptr;
    QProgressBar *m_vadMeter = nullptr;
    QLabel *m_gateLabel = nullptr;
};

} // namespace lightcord
