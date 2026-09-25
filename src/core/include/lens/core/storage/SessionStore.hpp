#pragma once

#include "lens/core/Conversation.hpp"

#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace lens {

// 会话持久化（SQLite，经 Qt SQL 驱动）。messages 表保存工具调用与结果，
class SessionStore
{
public:
    explicit SessionStore(QString databasePath);
    ~SessionStore();
    SessionStore(const SessionStore &) = delete;
    SessionStore &operator=(const SessionStore &) = delete;

    bool open();
    QString lastError() const;
    QString databasePath() const;

    qint64 createConversation(const QString &title, const QString &workdir);
    void renameConversation(qint64 conversationId, const QString &title);
    void deleteConversation(qint64 conversationId);
    bool appendMessage(qint64 conversationId, const Message &message);
    QList<Conversation> conversations() const;
    QList<Message> messages(qint64 conversationId) const;

private:
    QString m_path;
    QString m_connection;
    QSqlDatabase m_db;
    QString m_lastError;
};

} // namespace lens
