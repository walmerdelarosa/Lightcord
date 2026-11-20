// SPDX-License-Identifier: MIT
#pragma once

#include "core/MessageFormatter.h"
#include "core/Models.h"

#include <QMainWindow>
#include <QSet>
#include <QTimer>

class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSplitter;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace lightcord {

class Session;
class VoiceBar;
class VoiceSession;
namespace screenshare {
class ScreenCapturer;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Logs in with the stored token, or shows the login dialog.
    void startup();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum ItemKind { KindFolder = 0, KindText = 1, KindVoice = 2, KindCategory = 3 };
    enum ItemRole { IdRole = Qt::UserRole + 1, KindRole, GuildRole };

    void buildUi();
    void connectSession();
    void showLogin(const QString &error = QString(), bool bot = false);
    void logout();

    void scheduleTreeRebuild();
    void rebuildTree();
    void applyFilter(const QString &text);
    QTreeWidgetItem *addChannelItem(QTreeWidgetItem *parent, const Channel &c);

    void onItemActivated(QTreeWidgetItem *item);
    void openChannel(const Snowflake &channelId);
    void renderMessages();
    void updateHeader();
    void updateComposer();
    void sendCurrentMessage();

    void joinVoice(const Channel &c);
    void startScreenShare();
    void stopScreenShare(const QString &reason = QString());
    void openSettings();
    void onThemeChanged();

    Session *m_session = nullptr;
    VoiceSession *m_voice = nullptr;
    screenshare::ScreenCapturer *m_capturer = nullptr;
    MessageFormatter m_formatter;

    QSplitter *m_splitter = nullptr;
    QLabel *m_logo = nullptr;
    QLineEdit *m_filter = nullptr;
    QAction *m_filterIcon = nullptr;
    QTreeWidget *m_tree = nullptr;
    VoiceBar *m_voiceBar = nullptr;

    QLabel *m_channelIcon = nullptr;
    QLabel *m_channelTitle = nullptr;
    QLabel *m_channelTopic = nullptr;
    QTextBrowser *m_messages = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_sendButton = nullptr;
    QLabel *m_connectionLabel = nullptr;

    QTimer m_rebuildTimer;
    QSet<QString> m_expanded;
    Snowflake m_currentChannel;
    Snowflake m_restoreChannel;
    bool m_bootstrapped = false;
    bool m_loginOpen = false;
};

} // namespace lightcord
