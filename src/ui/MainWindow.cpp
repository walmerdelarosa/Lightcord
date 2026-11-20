// SPDX-License-Identifier: MIT
#include "MainWindow.h"

#include "LoginDialog.h"
#include "ScreenShareDialog.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "VoiceBar.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "core/TokenStore.h"
#include "screenshare/ScreenCapturer.h"
#include "voice/VoiceSession.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace lightcord {

namespace {
constexpr int kGroupWindowSecs = 7 * 60;

bool channelLess(const Channel &a, const Channel &b)
{
    // Text-like channels before voice channels, then by position, then by id.
    if (a.isVoice() != b.isVoice())
        return !a.isVoice();
    if (a.position != b.position)
        return a.position < b.position;
    return a.id < b.id;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_session(new Session(this))
    , m_voice(nullptr)
    , m_capturer(new screenshare::ScreenCapturer(this))
    , m_formatter(m_session)
{
    m_voice = new VoiceSession(m_session->gateway(), this);

    setWindowTitle(QStringLiteral("Lightcord"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/lightcord.svg")));
    resize(1100, 700);

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(60);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &MainWindow::rebuildTree);

    buildUi();
    connectSession();

    if (!Settings::instance().windowGeometry().isEmpty())
        restoreGeometry(Settings::instance().windowGeometry());
    if (!Settings::instance().splitterState().isEmpty())
        m_splitter->restoreState(Settings::instance().splitterState());
    m_restoreChannel = Settings::instance().lastChannelId();

    connect(&Settings::instance(), &Settings::themeChanged, this, &MainWindow::onThemeChanged);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    // ---------------- Sidebar -----------------------------------------------
    auto *sidebar = new QWidget(m_splitter);
    sidebar->setObjectName("Sidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setMinimumWidth(220);
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);

    auto *header = new QWidget(sidebar);
    header->setObjectName("SidebarHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 10, 12, 10);
    hl->setSpacing(8);
    m_logo = new QLabel(header);
    m_logo->setPixmap(Theme::logo(24));
    auto *wordmark = new QLabel(QStringLiteral("Lightcord"), header);
    wordmark->setObjectName("Wordmark");
    hl->addWidget(m_logo);
    hl->addWidget(wordmark);
    hl->addStretch();
    sv->addWidget(header);

    m_filter = new QLineEdit(sidebar);
    m_filter->setPlaceholderText(tr("Find a server or channel"));
    m_filter->setClearButtonEnabled(true);
    auto *filterWrap = new QWidget(sidebar);
    auto *fw = new QVBoxLayout(filterWrap);
    fw->setContentsMargins(8, 8, 8, 4);
    fw->addWidget(m_filter);
    sv->addWidget(filterWrap);

    m_tree = new QTreeWidget(sidebar);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(12);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(false);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sv->addWidget(m_tree, 1);

    m_voiceBar = new VoiceBar(sidebar);
    sv->addWidget(m_voiceBar);

    // ---------------- Chat pane ---------------------------------------------
    auto *chat = new QWidget(m_splitter);
    auto *cv = new QVBoxLayout(chat);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    auto *channelHeader = new QWidget(chat);
    channelHeader->setObjectName("ChannelHeader");
    channelHeader->setAttribute(Qt::WA_StyledBackground, true);
    auto *chl = new QHBoxLayout(channelHeader);
    chl->setContentsMargins(14, 10, 14, 10);
    chl->setSpacing(8);
    m_channelIcon = new QLabel(channelHeader);
    m_channelTitle = new QLabel(channelHeader);
    m_channelTitle->setObjectName("ChannelTitle");
    m_channelTopic = new QLabel(channelHeader);
    m_channelTopic->setObjectName("ChannelTopic");
    m_channelTopic->setTextFormat(Qt::PlainText);
    m_channelTopic->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    chl->addWidget(m_channelIcon);
    chl->addWidget(m_channelTitle);
    chl->addWidget(m_channelTopic, 1);
    cv->addWidget(channelHeader);

    m_messages = new QTextBrowser(chat);
    m_messages->setObjectName("MessageView");
    m_messages->setOpenExternalLinks(true);
    m_messages->setUndoRedoEnabled(false);
    m_messages->document()->setDocumentMargin(8);
    cv->addWidget(m_messages, 1);

    auto *composer = new QWidget(chat);
    composer->setObjectName("Composer");
    auto *col = new QHBoxLayout(composer);
    col->setContentsMargins(14, 8, 14, 12);
    col->setSpacing(8);
    m_input = new QPlainTextEdit(composer);
    m_input->setTabChangesFocus(true);
    m_input->setFixedHeight(64);
    m_input->installEventFilter(this);
    m_sendButton = new QToolButton(composer);
    m_sendButton->setToolTip(tr("Send (Enter)"));
    m_sendButton->setIconSize(QSize(20, 20));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    col->addWidget(m_input, 1);
    col->addWidget(m_sendButton, 0, Qt::AlignBottom);
    cv->addWidget(composer);

    m_splitter->addWidget(sidebar);
    m_splitter->addWidget(chat);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({270, 830});
    setCentralWidget(m_splitter);

    m_connectionLabel = new QLabel(tr("Offline"), this);
    statusBar()->addPermanentWidget(m_connectionLabel);
    statusBar()->setSizeGripEnabled(false);

    connect(m_filter, &QLineEdit::textChanged, this, &MainWindow::applyFilter);
    // Single click opens text channels and toggles folders; double click joins voice.
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) { onItemActivated(item); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item && item->data(0, KindRole).toInt() == KindVoice) {
            if (const Channel *c = m_session->channel(item->data(0, IdRole).toString()))
                joinVoice(*c);
        }
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (item && item->data(0, KindRole).toInt() == KindText)
            openChannel(item->data(0, IdRole).toString());
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        const QString id = item->data(0, IdRole).toString();
        if (item->data(0, KindRole).toInt() == KindCategory)
            m_expanded.remove(QStringLiteral("collapsed:") + id);
        else
            m_expanded.insert(id);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        const QString id = item->data(0, IdRole).toString();
        if (item->data(0, KindRole).toInt() == KindCategory)
            m_expanded.insert(QStringLiteral("collapsed:") + id);
        else
            m_expanded.remove(id);
    });
    connect(m_sendButton, &QToolButton::clicked, this, &MainWindow::sendCurrentMessage);

    // Voice bar
    m_voiceBar->setNoiseSuppression(Settings::instance().noiseSuppression().enabled);
    connect(m_voiceBar, &VoiceBar::noiseSuppressionToggled, this, [this](bool on) {
        auto s = Settings::instance().noiseSuppression();
        s.enabled = on;
        Settings::instance().setNoiseSuppression(s);
        statusBar()->showMessage(on ? tr("Noise suppression enabled") : tr("Noise suppression disabled"), 3000);
    });
    connect(m_voiceBar, &VoiceBar::muteToggled, m_voice, &VoiceSession::setSelfMute);
    connect(m_voiceBar, &VoiceBar::deafenToggled, m_voice, &VoiceSession::setSelfDeaf);
    connect(m_voiceBar, &VoiceBar::settingsClicked, this, &MainWindow::openSettings);
    connect(m_voiceBar, &VoiceBar::screenShareClicked, this, [this]() {
        if (m_capturer->isActive())
            stopScreenShare();
        else
            startScreenShare();
    });
    connect(m_voiceBar, &VoiceBar::stopShareClicked, this, [this]() { stopScreenShare(); });
    connect(m_voiceBar, &VoiceBar::leaveVoiceClicked, this, [this]() { m_voice->leave(); });
    connect(&Settings::instance(), &Settings::noiseSuppressionChanged, this,
            [this]() { m_voiceBar->setNoiseSuppression(Settings::instance().noiseSuppression().enabled); });

    // Screen capture
    connect(m_capturer, &screenshare::ScreenCapturer::frameReady, this,
            [this](const QImage &frame) { m_voiceBar->setSharePreview(frame, m_capturer->measuredFps()); });
    connect(m_capturer, &screenshare::ScreenCapturer::stopped, this, [this](const QString &reason) {
        m_voiceBar->setSharing(false);
        if (!reason.isEmpty())
            statusBar()->showMessage(reason, 5000);
    });

    // Voice signalling
    connect(m_voice, &VoiceSession::stateChanged, this, [this](VoiceSession::State s) {
        if (s == VoiceSession::State::Idle) {
            m_voiceBar->setVoiceChannel(QString());
            return;
        }
        const Channel *c = m_session->channel(m_voice->channelId());
        m_voiceBar->setVoiceChannel(c ? c->name : tr("Voice"));
    });
    connect(m_voice, &VoiceSession::transportInfoReady, this, [this](const VoiceSession::TransportInfo &info) {
        statusBar()->showMessage(
            tr("Voice server assigned (%1). Audio transport with DAVE encryption arrives in Lightcord 0.2.")
                .arg(info.endpoint),
            8000);
    });

    onThemeChanged();
    updateHeader();
    updateComposer();
}

void MainWindow::connectSession()
{
    connect(m_session, &Session::connectionStateChanged, m_connectionLabel, &QLabel::setText);
    connect(m_session, &Session::errorOccurred, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 8000); });
    connect(m_session, &Session::authenticationFailed, this, [this](const QString &reason) {
        const bool wasBot = m_session->isBot();
        TokenStore::clear();
        m_session->stop();
        QTimer::singleShot(0, this, [this, reason, wasBot]() { showLogin(reason, wasBot); });
    });
    connect(m_session, &Session::ready, this, [this]() {
        const User &me = m_session->me();
        m_voiceBar->setUser(me.displayName(), m_session->isBot() ? tr("Bot account") : QStringLiteral("@") + me.username);
        m_bootstrapped = true;
    });
    connect(m_session, &Session::guildsChanged, this, &MainWindow::scheduleTreeRebuild);
    connect(m_session, &Session::messagesLoaded, this, [this](const Snowflake &channelId) {
        if (channelId == m_currentChannel)
            renderMessages();
    });
    connect(m_session, &Session::messageCreated, this, [this](const Message &m) {
        if (m.channelId == m_currentChannel)
            renderMessages();
    });
    connect(m_session, &Session::messageUpdated, this, [this](const Message &m) {
        if (m.channelId == m_currentChannel)
            renderMessages();
    });
    connect(m_session, &Session::messageDeleted, this, [this](const Snowflake &channelId, const Snowflake &) {
        if (channelId == m_currentChannel)
            renderMessages();
    });
    connect(m_session, &Session::voiceStateUpdate, m_voice, &VoiceSession::handleVoiceStateUpdate);
    connect(m_session, &Session::voiceServerUpdate, m_voice, &VoiceSession::handleVoiceServerUpdate);
}

void MainWindow::startup()
{
    const TokenStore::Credentials creds = TokenStore::load();
    if (creds.valid()) {
        m_voiceBar->setUser(tr("Connecting…"), QString());
        m_session->start(creds.token, creds.isBot);
    } else {
        showLogin();
    }
}

void MainWindow::showLogin(const QString &error, bool bot)
{
    if (m_loginOpen)
        return;
    m_loginOpen = true;

    LoginDialog dlg(this);
    dlg.setAccountType(bot);
    dlg.setError(error);
    const int result = dlg.exec();
    m_loginOpen = false;

    if (result != QDialog::Accepted) {
        if (!m_session->isRunning())
            QTimer::singleShot(0, qApp, []() { QApplication::quit(); });
        return;
    }
    if (dlg.remember())
        TokenStore::save({dlg.token(), dlg.isBot()});
    m_voiceBar->setUser(tr("Connecting…"), QString());
    m_session->start(dlg.token(), dlg.isBot());
}

void MainWindow::logout()
{
    stopScreenShare();
    m_voice->leave();
    TokenStore::clear();
    m_session->stop();
    m_currentChannel.clear();
    m_restoreChannel.clear();
    m_tree->clear();
    m_messages->clear();
    m_bootstrapped = false;
    updateHeader();
    updateComposer();
    m_voiceBar->setUser(QString(), QString());
    QTimer::singleShot(0, this, [this]() { showLogin(); });
}

// ---------------------------------------------------------------------------
// Navigation tree
// ---------------------------------------------------------------------------

void MainWindow::scheduleTreeRebuild()
{
    m_rebuildTimer.start();
}

QTreeWidgetItem *MainWindow::addChannelItem(QTreeWidgetItem *parent, const Channel &c)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, m_session->channelDisplayName(c));
    item->setData(0, IdRole, c.id);
    item->setData(0, GuildRole, c.guildId);
    if (c.isVoice()) {
        item->setData(0, KindRole, KindVoice);
        item->setIcon(0, Theme::icon("speaker", Theme::palette().textMuted));
    } else if (c.isPrivate()) {
        item->setData(0, KindRole, KindText);
        item->setIcon(0, Theme::icon("at", Theme::palette().textMuted));
    } else {
        item->setData(0, KindRole, KindText);
        item->setIcon(0, Theme::icon("hash", Theme::palette().textMuted));
    }
    if (c.id == m_currentChannel)
        m_tree->setCurrentItem(item);
    return item;
}

void MainWindow::rebuildTree()
{
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();

    QFont bold = m_tree->font();
    bold.setBold(true);

    // Direct messages
    if (!m_session->privateChannels().isEmpty()) {
        auto *dms = new QTreeWidgetItem(m_tree);
        dms->setText(0, tr("Direct Messages"));
        dms->setData(0, IdRole, QStringLiteral("@dms"));
        dms->setData(0, KindRole, KindFolder);
        dms->setFont(0, bold);
        int count = 0;
        for (const Channel &c : m_session->privateChannels()) {
            if (++count > 150)
                break;
            addChannelItem(dms, c);
        }
        dms->setExpanded(m_expanded.contains(QStringLiteral("@dms")));
    }

    // Guilds
    for (const Guild &g : m_session->guilds()) {
        if (g.unavailable && g.name.isEmpty())
            continue;
        auto *gi = new QTreeWidgetItem(m_tree);
        gi->setText(0, g.name);
        gi->setData(0, IdRole, g.id);
        gi->setData(0, KindRole, KindFolder);
        gi->setFont(0, bold);

        QVector<Channel> categories;
        QVector<Channel> loose;
        QHash<Snowflake, QVector<Channel>> byParent;
        for (const Channel &c : g.channels) {
            if (c.isCategory()) {
                categories.push_back(c);
            } else if ((c.isText() || c.isVoice()) && m_session->canView(c)) {
                if (c.parentId.isEmpty())
                    loose.push_back(c);
                else
                    byParent[c.parentId].push_back(c);
            }
        }
        std::sort(loose.begin(), loose.end(), channelLess);
        std::sort(categories.begin(), categories.end(), [](const Channel &a, const Channel &b) {
            return a.position != b.position ? a.position < b.position : a.id < b.id;
        });

        for (const Channel &c : loose)
            addChannelItem(gi, c);
        for (const Channel &cat : categories) {
            QVector<Channel> children = byParent.value(cat.id);
            if (children.isEmpty())
                continue;
            std::sort(children.begin(), children.end(), channelLess);
            auto *ci = new QTreeWidgetItem(gi);
            ci->setText(0, cat.name.toUpper());
            ci->setData(0, IdRole, cat.id);
            ci->setData(0, KindRole, KindCategory);
            ci->setForeground(0, Theme::palette().textMuted);
            QFont small = m_tree->font();
            small.setPointSizeF(small.pointSizeF() * 0.85);
            small.setBold(true);
            ci->setFont(0, small);
            for (const Channel &c : children)
                addChannelItem(ci, c);
            ci->setExpanded(!m_expanded.contains(QStringLiteral("collapsed:") + cat.id));
        }
        gi->setExpanded(m_expanded.contains(g.id));
    }

    applyFilter(m_filter->text());
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(scroll);

    // Re-open the channel from the previous run once it is known.
    if (!m_restoreChannel.isEmpty() && m_currentChannel.isEmpty()) {
        if (const Channel *c = m_session->channel(m_restoreChannel)) {
            if (c->isText()) {
                const Snowflake id = m_restoreChannel;
                m_restoreChannel.clear();
                openChannel(id);
                // Make sure the restored channel is visible in the tree.
                const auto matches = m_tree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
                for (QTreeWidgetItem *it : matches) {
                    if (it->data(0, IdRole).toString() == id) {
                        for (QTreeWidgetItem *p = it->parent(); p; p = p->parent())
                            p->setExpanded(true);
                        m_tree->setCurrentItem(it);
                        break;
                    }
                }
            }
        }
    }
}

void MainWindow::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        const bool topMatches = needle.isEmpty() || top->text(0).contains(needle, Qt::CaseInsensitive);
        bool anyChild = false;
        // Walk channels (direct children and children of categories).
        for (int j = 0; j < top->childCount(); ++j) {
            QTreeWidgetItem *child = top->child(j);
            if (child->data(0, KindRole).toInt() == KindCategory) {
                bool anyInCategory = false;
                for (int k = 0; k < child->childCount(); ++k) {
                    QTreeWidgetItem *leaf = child->child(k);
                    const bool m = topMatches || leaf->text(0).contains(needle, Qt::CaseInsensitive);
                    leaf->setHidden(!m);
                    anyInCategory |= m;
                }
                child->setHidden(!anyInCategory);
                if (!needle.isEmpty() && anyInCategory)
                    child->setExpanded(true);
                anyChild |= anyInCategory;
            } else {
                const bool m = topMatches || child->text(0).contains(needle, Qt::CaseInsensitive);
                child->setHidden(!m);
                anyChild |= m;
            }
        }
        top->setHidden(!(topMatches || anyChild));
        if (!needle.isEmpty() && anyChild && !topMatches)
            top->setExpanded(true);
    }
}

void MainWindow::onItemActivated(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const int kind = item->data(0, KindRole).toInt();
    const QString id = item->data(0, IdRole).toString();
    switch (kind) {
    case KindFolder:
        item->setExpanded(!item->isExpanded());
        break;
    case KindCategory:
        item->setExpanded(!item->isExpanded());
        break;
    case KindText:
        openChannel(id);
        break;
    case KindVoice:
        statusBar()->showMessage(tr("Double-click a voice channel to join it."), 3000);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void MainWindow::openChannel(const Snowflake &channelId)
{
    if (channelId == m_currentChannel)
        return;
    m_currentChannel = channelId;
    Settings::instance().setLastChannelId(channelId);
    updateHeader();
    updateComposer();
    m_messages->clear();
    if (m_session->hasLoadedMessages(channelId)) {
        renderMessages();
    } else {
        m_messages->setHtml(QStringLiteral("<p style=\"color:%1\">%2</p>")
                                .arg(Theme::palette().textMuted.name(), tr("Loading messages…")));
        m_session->loadMessages(channelId);
    }
    m_input->setFocus();
}

void MainWindow::renderMessages()
{
    if (m_currentChannel.isEmpty())
        return;
    QScrollBar *bar = m_messages->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    const int previous = bar->value();

    const QVector<Message> &list = m_session->messages(m_currentChannel);
    QString html;
    html.reserve(list.size() * 300);
    if (list.isEmpty()) {
        html = QStringLiteral("<p class=\"system\">%1</p>").arg(tr("No messages yet. Say hi!"));
    } else {
        const Message *prev = nullptr;
        for (const Message &m : list) {
            const bool grouped = prev && prev->author.id == m.author.id && m.replyAuthor.isEmpty() &&
                                 prev->timestamp.secsTo(m.timestamp) < kGroupWindowSecs;
            html += m_formatter.messageToHtml(m, grouped);
            prev = &m;
        }
    }
    m_messages->document()->setDefaultStyleSheet(MessageFormatter::styleSheet());
    m_messages->setHtml(html);
    if (atBottom)
        bar->setValue(bar->maximum());
    else
        bar->setValue(previous);
}

void MainWindow::updateHeader()
{
    const Channel *c = m_currentChannel.isEmpty() ? nullptr : m_session->channel(m_currentChannel);
    if (!c) {
        m_channelIcon->setPixmap(Theme::logo(18));
        m_channelTitle->setText(tr("Lightcord"));
        m_channelTopic->setText(tr("Pick a channel on the left to start chatting."));
        setWindowTitle(QStringLiteral("Lightcord"));
        return;
    }
    const QString icon = c->isPrivate() ? QStringLiteral("at") : QStringLiteral("hash");
    m_channelIcon->setPixmap(Theme::icon(icon, Theme::palette().textMuted).pixmap(18, 18));
    const QString name = m_session->channelDisplayName(*c);
    m_channelTitle->setText(name);
    m_channelTopic->setText(c->topic.simplified());
    const Guild *g = m_session->guild(c->guildId);
    setWindowTitle(g ? QStringLiteral("%1 · %2 — Lightcord").arg(name, g->name)
                     : QStringLiteral("%1 — Lightcord").arg(name));
}

void MainWindow::updateComposer()
{
    const Channel *c = m_currentChannel.isEmpty() ? nullptr : m_session->channel(m_currentChannel);
    const bool canSend = c && m_session->canSend(*c);
    m_input->setEnabled(canSend);
    m_sendButton->setEnabled(canSend);
    if (!c)
        m_input->setPlaceholderText(QString());
    else if (!canSend)
        m_input->setPlaceholderText(tr("You do not have permission to send messages in this channel."));
    else
        m_input->setPlaceholderText(tr("Message %1%2  (Enter to send, Shift+Enter for a new line)")
                                        .arg(c->isPrivate() ? QStringLiteral("@") : QStringLiteral("#"),
                                             m_session->channelDisplayName(*c)));
}

void MainWindow::sendCurrentMessage()
{
    if (m_currentChannel.isEmpty())
        return;
    const QString text = m_input->toPlainText();
    if (text.trimmed().isEmpty())
        return;
    m_session->sendMessage(m_currentChannel, text);
    m_input->clear();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & Qt::ShiftModifier)) {
            sendCurrentMessage();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Voice, screen share, settings
// ---------------------------------------------------------------------------

void MainWindow::joinVoice(const Channel &c)
{
    if (!Settings::instance().experimentalVoiceSignaling()) {
        statusBar()->showMessage(tr("Voice chat (with Discord's DAVE end-to-end encryption) is coming in Lightcord "
                                    "0.2. You can test the signalling in Settings › Advanced."),
                                 8000);
        return;
    }
    if (m_voice->channelId() == c.id) {
        m_voice->leave();
        return;
    }
    m_voice->join(c.guildId, c.id, m_session->me().id);
    statusBar()->showMessage(tr("Joining %1 (signalling only)…").arg(c.name), 4000);
}

void MainWindow::startScreenShare()
{
    ScreenShareDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const screenshare::CaptureSource source = dlg.selectedSource();
    if (!source.isValid())
        return;
    m_capturer->setSource(source);
    m_capturer->setFrameRate(dlg.frameRate());
    m_capturer->setMaxHeight(dlg.maxHeight());
    if (m_capturer->start()) {
        m_voiceBar->setSharing(true, source.title);
        statusBar()->showMessage(tr("Sharing \"%1\" (local preview)").arg(source.title), 4000);
    }
}

void MainWindow::stopScreenShare(const QString &reason)
{
    if (m_capturer->isActive())
        m_capturer->stop();
    m_voiceBar->setSharing(false);
    if (!reason.isEmpty())
        statusBar()->showMessage(reason, 5000);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::logoutRequested, this, [this]() { QTimer::singleShot(0, this, &MainWindow::logout); });
    dlg.exec();
}

void MainWindow::onThemeChanged()
{
    const ThemePalette &p = Theme::palette();
    m_logo->setPixmap(Theme::logo(24));
    m_sendButton->setIcon(Theme::icon("send", p.accent));
    if (!m_filterIcon)
        m_filterIcon = m_filter->addAction(Theme::icon("hash", p.textMuted), QLineEdit::LeadingPosition);
    else
        m_filterIcon->setIcon(Theme::icon("hash", p.textMuted));
    m_voiceBar->refreshIcons();
    updateHeader();
    scheduleTreeRebuild();
    renderMessages();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Settings::instance().setWindowGeometry(saveGeometry());
    Settings::instance().setSplitterState(m_splitter->saveState());
    stopScreenShare();
    m_voice->leave();
    m_session->stop();
    event->accept();
}

} // namespace lightcord
