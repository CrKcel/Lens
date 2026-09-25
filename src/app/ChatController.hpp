#pragma once

#include "ConversationListModel.hpp"
#include "MessageListModel.hpp"
#include "lens/core/agent/AgentSession.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QObject>
#include <memory>

namespace lens {

class AppSettings;
class SessionStore;

// UI 与核心层的桥：会话/工作文件夹管理、把 AgentSession 的信号落到
// 显示模型与持久化存储。QML 只与此类交互。
class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(qint64 currentConversationId READ currentConversationId NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentWorkdir READ currentWorkdir NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentConversationChanged)
    Q_PROPERTY(QAbstractListModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(QAbstractListModel *messages READ messages CONSTANT)

public:
    explicit ChatController(SessionStore *store, AppSettings *settings,
                            QObject *parent = nullptr);

    bool streaming() const { return m_streaming; }
    qint64 currentConversationId() const { return m_conversationId; }
    QString currentWorkdir() const { return m_workdir; }
    QString currentTitle() const { return m_title; }
    QAbstractListModel *conversations() const { return m_conversationModel; }
    QAbstractListModel *messages() const { return m_messageModel; }

    Q_INVOKABLE void newConversation(const QString &workdir);
    Q_INVOKABLE void openConversation(qint64 conversationId);
    Q_INVOKABLE void deleteConversation(qint64 conversationId);
    Q_INVOKABLE void send(const QString &text, const QString &workdir = QString());
    Q_INVOKABLE void stop();

signals:
    void streamingChanged();
    void currentConversationChanged();

private:
    void connectAgent();
    qint64 createAndOpenConversation(const QString &workdir);
    QString assembleSystemPrompt() const;
    void setErrorRow(const QString &text);

    SessionStore *m_store;
    AppSettings *m_settings;
    ToolRegistry m_registry;
    MessageListModel *m_messageModel;
    ConversationListModel *m_conversationModel;
    std::unique_ptr<AgentSession> m_agent;

    qint64 m_conversationId = 0;
    QString m_workdir;
    QString m_title;
    bool m_streaming = false;
};

} // namespace lens
