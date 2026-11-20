// SPDX-License-Identifier: MIT
#include "ScreenShareDialog.h"

#include "Theme.h"
#include "core/Settings.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace lightcord {

using screenshare::CaptureSource;
using screenshare::ScreenCapturer;

ScreenShareDialog::ScreenShareDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Share your screen"));
    resize(720, 520);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(10);

    auto *title = new QLabel(tr("Choose what to share"), this);
    title->setObjectName("Heading");
    v->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(200, 120));
    m_list->setGridSize(QSize(224, 170));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    v->addWidget(m_list, 1);

    auto *options = new QHBoxLayout;
    m_fps = new QComboBox(this);
    for (int f : {5, 15, 30})
        m_fps->addItem(tr("%1 fps").arg(f), f);
    m_fps->setCurrentIndex(qMax(0, m_fps->findData(Settings::instance().screenShareFps())));
    m_resolution = new QComboBox(this);
    m_resolution->addItem(tr("720p"), 720);
    m_resolution->addItem(tr("1080p"), 1080);
    m_resolution->addItem(tr("Native"), 0);
    m_resolution->setCurrentIndex(qMax(0, m_resolution->findData(Settings::instance().screenShareMaxHeight())));
    auto *refresh = new QPushButton(tr("Refresh"), this);
    options->addWidget(new QLabel(tr("Frame rate"), this));
    options->addWidget(m_fps);
    options->addSpacing(12);
    options->addWidget(new QLabel(tr("Resolution"), this));
    options->addWidget(m_resolution);
    options->addStretch();
    options->addWidget(refresh);
    v->addLayout(options);

    auto *note = new QLabel(tr("Preview mode: streaming to other people (Go Live) is planned for Lightcord 0.3. "
                               "For now the capture runs locally so you can check quality and CPU usage."),
                            this);
    note->setObjectName("Muted");
    note->setWordWrap(true);
    v->addWidget(note);

    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(tr("Cancel"), this);
    m_startButton = new QPushButton(Theme::icon("screen", Theme::palette().accentText), tr("Start sharing"), this);
    m_startButton->setObjectName("Primary");
    m_startButton->setEnabled(false);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_startButton);
    v->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        Settings::instance().setScreenShareFps(frameRate());
        Settings::instance().setScreenShareMaxHeight(maxHeight());
        accept();
    });
    connect(refresh, &QPushButton::clicked, this, &ScreenShareDialog::reload);
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int row) { m_startButton->setEnabled(row >= 0 && row < m_sources.size()); });
    connect(m_list, &QListWidget::itemDoubleClicked, m_startButton, &QPushButton::click);

    reload();
}

void ScreenShareDialog::reload()
{
    m_sources = ScreenCapturer::availableSources(200);
    m_list->clear();
    for (const CaptureSource &s : m_sources) {
        auto *item = new QListWidgetItem(m_list);
        item->setText(s.title);
        item->setToolTip(s.title);
        if (!s.thumbnail.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(s.thumbnail)));
        else
            item->setIcon(Theme::icon("screen"));
    }
    if (!m_sources.isEmpty())
        m_list->setCurrentRow(0);
}

CaptureSource ScreenShareDialog::selectedSource() const
{
    const int row = m_list->currentRow();
    return (row >= 0 && row < m_sources.size()) ? m_sources.at(row) : CaptureSource{};
}

int ScreenShareDialog::frameRate() const
{
    return m_fps->currentData().toInt();
}

int ScreenShareDialog::maxHeight() const
{
    return m_resolution->currentData().toInt();
}

} // namespace lightcord
