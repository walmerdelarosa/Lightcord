// SPDX-License-Identifier: MIT
#include "SettingsDialog.h"

#include "Theme.h"
#include "audio/AudioPipeline.h"
#include "core/Settings.h"

#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lightcord {

namespace {
QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName("Muted");
    l->setWordWrap(true);
    return l;
}

QProgressBar *makeMeter(QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setRange(0, 100);
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    return bar;
}

QAudioDevice findDevice(const QList<QAudioDevice> &devices, const QByteArray &id)
{
    for (const QAudioDevice &d : devices) {
        if (d.id() == id)
            return d;
    }
    return {};
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lightcord Settings"));
    resize(560, 600);

    m_pipeline = new audio::AudioPipeline(this);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildAppearanceTab(), tr("Appearance"));
    tabs->addTab(buildAudioTab(), tr("Voice && Audio"));
    tabs->addTab(buildScreenShareTab(), tr("Screen share"));
    tabs->addTab(buildAdvancedTab(), tr("Advanced"));
    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_pipeline, &audio::AudioPipeline::levels, this,
            [this](float in, float out, float vad, bool gateOpen) {
                m_inputMeter->setValue(static_cast<int>(in * 100));
                m_outputMeter->setValue(static_cast<int>(out * 100));
                m_vadMeter->setValue(static_cast<int>(vad * 100));
                m_gateLabel->setText(gateOpen ? tr("Gate: open (voice detected)") : tr("Gate: closed (silence)"));
            });
    connect(m_pipeline, &audio::AudioPipeline::errorOccurred, this,
            [this](const QString &msg) { m_gateLabel->setText(msg); });
}

SettingsDialog::~SettingsDialog()
{
    m_pipeline->stop();
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(12);

    auto *themeCombo = new QComboBox(page);
    themeCombo->addItem(tr("Night (dark)"), "dark");
    themeCombo->addItem(tr("Day (light)"), "light");
    themeCombo->setCurrentIndex(Settings::instance().theme() == QLatin1String("light") ? 1 : 0);
    form->addRow(tr("Theme"), themeCombo);

    auto *fontSpin = new QSpinBox(page);
    fontSpin->setRange(8, 16);
    fontSpin->setSuffix(tr(" pt"));
    fontSpin->setValue(Settings::instance().fontPointSize());
    form->addRow(tr("Font size"), fontSpin);

    form->addRow(mutedLabel(tr("Lightcord keeps a compact, desktop-style layout: no animations, no avatars "
                               "downloaded by default and no embedded browser engine."),
                            page));

    connect(themeCombo, &QComboBox::currentIndexChanged, this, [themeCombo, fontSpin](int) {
        Theme::apply(themeCombo->currentData().toString(), fontSpin->value());
        Settings::instance().setTheme(themeCombo->currentData().toString());
    });
    connect(fontSpin, &QSpinBox::valueChanged, this, [themeCombo](int pt) {
        Theme::apply(themeCombo->currentData().toString(), pt);
        Settings::instance().setFontPointSize(pt);
    });
    return page;
}

QWidget *SettingsDialog::buildAudioTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    // Devices
    auto *devices = new QGroupBox(tr("Devices"), page);
    auto *df = new QFormLayout(devices);
    m_inputCombo = new QComboBox(devices);
    m_outputCombo = new QComboBox(devices);
    df->addRow(tr("Microphone"), m_inputCombo);
    df->addRow(tr("Output"), m_outputCombo);
    v->addWidget(devices);
    populateDevices();

    // Noise suppression
    const auto ns = Settings::instance().noiseSuppression();
    auto *nsBox = new QGroupBox(tr("Noise suppression (RNNoise)"), page);
    auto *nf = new QFormLayout(nsBox);
    m_nsEnabled = new QCheckBox(tr("Remove background noise (fans, keyboards, traffic…)"), nsBox);
    m_nsEnabled->setChecked(ns.enabled);
    nf->addRow(m_nsEnabled);

    auto *strengthRow = new QHBoxLayout;
    m_nsStrength = new QSlider(Qt::Horizontal, nsBox);
    m_nsStrength->setRange(0, 100);
    m_nsStrength->setValue(static_cast<int>(ns.strength * 100));
    m_nsStrengthLabel = new QLabel(nsBox);
    m_nsStrengthLabel->setMinimumWidth(40);
    strengthRow->addWidget(m_nsStrength, 1);
    strengthRow->addWidget(m_nsStrengthLabel);
    nf->addRow(tr("Strength"), strengthRow);

    m_gateEnabled = new QCheckBox(tr("Voice gate: mute the microphone between sentences"), nsBox);
    m_gateEnabled->setChecked(ns.voiceGate);
    nf->addRow(m_gateEnabled);

    auto *thresholdRow = new QHBoxLayout;
    m_gateThreshold = new QSlider(Qt::Horizontal, nsBox);
    m_gateThreshold->setRange(5, 95);
    m_gateThreshold->setValue(static_cast<int>(ns.gateThreshold * 100));
    m_gateThresholdLabel = new QLabel(nsBox);
    m_gateThresholdLabel->setMinimumWidth(40);
    thresholdRow->addWidget(m_gateThreshold, 1);
    thresholdRow->addWidget(m_gateThresholdLabel);
    nf->addRow(tr("Gate sensitivity"), thresholdRow);

    m_gateHold = new QSpinBox(nsBox);
    m_gateHold->setRange(0, 1000);
    m_gateHold->setSingleStep(50);
    m_gateHold->setSuffix(tr(" ms"));
    m_gateHold->setValue(ns.gateHoldMs);
    nf->addRow(tr("Gate hold time"), m_gateHold);
    v->addWidget(nsBox);

    // Mic test
    auto *testBox = new QGroupBox(tr("Microphone test"), page);
    auto *tf = new QFormLayout(testBox);
    auto *testRow = new QHBoxLayout;
    m_micTestButton = new QPushButton(tr("Start test"), testBox);
    m_micTestButton->setObjectName("Primary");
    m_monitorCheck = new QCheckBox(tr("Hear myself (use headphones)"), testBox);
    testRow->addWidget(m_micTestButton);
    testRow->addWidget(m_monitorCheck);
    testRow->addStretch();
    tf->addRow(testRow);
    m_inputMeter = makeMeter(testBox);
    m_outputMeter = makeMeter(testBox);
    m_vadMeter = makeMeter(testBox);
    m_vadMeter->setObjectName("VadMeter");
    tf->addRow(tr("Raw input"), m_inputMeter);
    tf->addRow(tr("After suppression"), m_outputMeter);
    tf->addRow(tr("Voice probability"), m_vadMeter);
    m_gateLabel = mutedLabel(tr("Start the test and speak to see the effect."), testBox);
    tf->addRow(m_gateLabel);
    v->addWidget(testBox);
    v->addStretch();

    auto updateLabels = [this]() {
        m_nsStrengthLabel->setText(QStringLiteral("%1%").arg(m_nsStrength->value()));
        m_gateThresholdLabel->setText(QStringLiteral("%1%").arg(m_gateThreshold->value()));
        const bool on = m_nsEnabled->isChecked();
        m_nsStrength->setEnabled(on);
        m_gateEnabled->setEnabled(on);
        m_gateThreshold->setEnabled(on && m_gateEnabled->isChecked());
        m_gateHold->setEnabled(on && m_gateEnabled->isChecked());
    };
    updateLabels();

    auto onChange = [this, updateLabels]() {
        updateLabels();
        saveNoiseSettings();
    };
    connect(m_nsEnabled, &QCheckBox::toggled, this, onChange);
    connect(m_gateEnabled, &QCheckBox::toggled, this, onChange);
    connect(m_nsStrength, &QSlider::valueChanged, this, onChange);
    connect(m_gateThreshold, &QSlider::valueChanged, this, onChange);
    connect(m_gateHold, &QSpinBox::valueChanged, this, onChange);

    connect(m_inputCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QByteArray id = m_inputCombo->currentData().toByteArray();
        Settings::instance().setInputDeviceId(id);
        m_pipeline->setInputDevice(findDevice(QMediaDevices::audioInputs(), id));
    });
    connect(m_outputCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QByteArray id = m_outputCombo->currentData().toByteArray();
        Settings::instance().setOutputDeviceId(id);
        m_pipeline->setOutputDevice(findDevice(QMediaDevices::audioOutputs(), id));
    });
    connect(m_micTestButton, &QPushButton::clicked, this, &SettingsDialog::toggleMicTest);
    connect(m_monitorCheck, &QCheckBox::toggled, m_pipeline, &audio::AudioPipeline::setMonitorEnabled);

    m_pipeline->setInputDevice(findDevice(QMediaDevices::audioInputs(), Settings::instance().inputDeviceId()));
    m_pipeline->setOutputDevice(findDevice(QMediaDevices::audioOutputs(), Settings::instance().outputDeviceId()));
    m_pipeline->setSuppressorSettings(ns);
    return page;
}

void SettingsDialog::populateDevices()
{
    const QByteArray inId = Settings::instance().inputDeviceId();
    const QByteArray outId = Settings::instance().outputDeviceId();

    QSignalBlocker b1(m_inputCombo);
    QSignalBlocker b2(m_outputCombo);
    m_inputCombo->clear();
    m_outputCombo->clear();
    m_inputCombo->addItem(tr("System default"), QByteArray());
    m_outputCombo->addItem(tr("System default"), QByteArray());
    for (const QAudioDevice &d : QMediaDevices::audioInputs()) {
        m_inputCombo->addItem(d.description(), d.id());
        if (d.id() == inId)
            m_inputCombo->setCurrentIndex(m_inputCombo->count() - 1);
    }
    for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
        m_outputCombo->addItem(d.description(), d.id());
        if (d.id() == outId)
            m_outputCombo->setCurrentIndex(m_outputCombo->count() - 1);
    }
}

void SettingsDialog::saveNoiseSettings()
{
    audio::NoiseSuppressor::Settings s;
    s.enabled = m_nsEnabled->isChecked();
    s.strength = m_nsStrength->value() / 100.0f;
    s.voiceGate = m_gateEnabled->isChecked();
    s.gateThreshold = m_gateThreshold->value() / 100.0f;
    s.gateHoldMs = m_gateHold->value();
    Settings::instance().setNoiseSuppression(s);
    m_pipeline->setSuppressorSettings(s);
}

void SettingsDialog::toggleMicTest()
{
    if (m_pipeline->isRunning()) {
        m_pipeline->stop();
        m_micTestButton->setText(tr("Start test"));
        m_inputMeter->setValue(0);
        m_outputMeter->setValue(0);
        m_vadMeter->setValue(0);
        m_gateLabel->setText(tr("Test stopped."));
        return;
    }
    m_pipeline->setMonitorEnabled(m_monitorCheck->isChecked());
    if (m_pipeline->start())
        m_micTestButton->setText(tr("Stop test"));
}

QWidget *SettingsDialog::buildScreenShareTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(12);

    auto *fps = new QComboBox(page);
    for (int f : {5, 15, 30})
        fps->addItem(tr("%1 fps").arg(f), f);
    fps->setCurrentIndex(qMax(0, fps->findData(Settings::instance().screenShareFps())));
    form->addRow(tr("Frame rate"), fps);

    auto *res = new QComboBox(page);
    res->addItem(tr("720p (recommended)"), 720);
    res->addItem(tr("1080p"), 1080);
    res->addItem(tr("Native resolution"), 0);
    res->setCurrentIndex(qMax(0, res->findData(Settings::instance().screenShareMaxHeight())));
    form->addRow(tr("Resolution"), res);

    form->addRow(mutedLabel(tr("Lower values use less CPU. Streaming your screen to other people (Go Live) is planned "
                               "for Lightcord 0.3; until then sharing shows a local preview."),
                            page));

    connect(fps, &QComboBox::currentIndexChanged, this,
            [fps](int) { Settings::instance().setScreenShareFps(fps->currentData().toInt()); });
    connect(res, &QComboBox::currentIndexChanged, this,
            [res](int) { Settings::instance().setScreenShareMaxHeight(res->currentData().toInt()); });
    return page;
}

QWidget *SettingsDialog::buildAdvancedTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    v->addWidget(mutedLabel(tr("Voice calls use Discord's DAVE end-to-end encryption: audio is encrypted on "
                               "your computer and can only be decrypted by the people in the call."),
                            page));
    v->addSpacing(8);
    auto *logs = new QPushButton(tr("Open log folder"), page);
    v->addWidget(logs, 0, Qt::AlignLeft);
    v->addWidget(mutedLabel(tr("If something does not work (for example, voice), the file lightcord.log in this "
                               "folder helps to find out why. It never contains your token or messages."),
                            page));
    connect(logs, &QPushButton::clicked, this, []() {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    v->addSpacing(16);
    auto *logout = new QPushButton(Theme::icon("logout", Theme::palette().danger), tr("Log out and forget token"), page);
    logout->setObjectName("Danger");
    v->addWidget(logout, 0, Qt::AlignLeft);
    connect(logout, &QPushButton::clicked, this, [this]() {
        emit logoutRequested();
        accept();
    });

    v->addStretch();
    v->addWidget(mutedLabel(tr("Lightcord %1 · MIT License · RNNoise (BSD-3-Clause) · libdave (MIT) · Opus (BSD) · libsodium (ISC)")
                                .arg(QStringLiteral(LIGHTCORD_VERSION)),
                            page));
    return page;
}

} // namespace lightcord
