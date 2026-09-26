#include "lens/core/storage/SessionStore.hpp"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <atomic>
#include <nlohmann/json.hpp>

namespace lens {
namespace {

std::atomic<int> g_instanceCounter{0};

// null QString 会被 Qt SQL 绑定为 SQL NULL，撞上文本列的 NOT NULL 约束；
// 存储层统一把 null 归一化为空串
QString notNull(QString text)
{
    return text.isNull() ? QStringLiteral("") : text;
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString toolCallsToJson(const QList<ToolCall> &toolCalls)
{
    if (toolCalls.isEmpty())
        return {};
    auto array = nlohmann::json::array();
    for (const ToolCall &call : toolCalls) {
        array.push_back({{"id", call.id.toStdString()},
                         {"name", call.name.toStdString()},
                         {"arguments", call.arguments.toStdString()}});
    }
    return QString::fromStdString(array.dump());
}

QList<ToolCall> toolCallsFromJson(const QString &text)
{
    QList<ToolCall> result;
    if (text.isEmpty())
        return result;
    const auto array = nlohmann::json::parse(text.toStdString(), nullptr, false);
    if (array.is_discarded() || !array.is_array())
        return result;
    for (const auto &entry : array) {
        if (!entry.is_object())
            continue;
        ToolCall call;
        if (entry.contains("id") && entry.at("id").is_string())
            call.id = QString::fromStdString(entry.at("id").get<std::string>());
        if (entry.contains("name") && entry.at("name").is_string())
            call.name = QString::fromStdString(entry.at("name").get<std::string>());
        if (entry.contains("arguments") && entry.at("arguments").is_string())
            call.arguments = QString::fromStdString(entry.at("arguments").get<std::string>());
        result.append(call);
    }
    return result;
}

// 老库升级：messages 表补充 tool_calls / tool_call_id 列
bool ensureColumn(QSqlDatabase &db, const QString &table, const QString &column,
                  const QString &definition, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        *error = query.lastError().text();
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column)
            return true;
    }
    if (!query.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                        .arg(table, column, definition))) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

} // namespace

SessionStore::SessionStore(QString databasePath)
    : m_path(std::move(databasePath))
    , m_connection(QStringLiteral("lens_session_%1").arg(g_instanceCounter.fetch_add(1)))
{
}

SessionStore::~SessionStore()
{
    if (m_db.isValid()) {
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

bool SessionStore::open()
{
    m_lastError.clear();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        m_db = QSqlDatabase();
        return false;
    }

    QSqlQuery query(m_db);
    query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conversations ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT NOT NULL, "
            "workdir TEXT NOT NULL DEFAULT '', "
            "created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL)"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE, "
            "role TEXT NOT NULL, "
            "content TEXT NOT NULL, "
            "created_at TEXT NOT NULL)"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (!ensureColumn(m_db, QStringLiteral("messages"), QStringLiteral("tool_calls"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''"), &m_lastError))
        return false;
    if (!ensureColumn(m_db, QStringLiteral("messages"), QStringLiteral("tool_call_id"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''"), &m_lastError))
        return false;
    if (!ensureColumn(m_db, QStringLiteral("messages"), QStringLiteral("reasoning"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''"), &m_lastError))
        return false;
    if (!ensureColumn(m_db, QStringLiteral("messages"), QStringLiteral("usage_json"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''"), &m_lastError))
        return false;
    return true;
}

QString SessionStore::lastError() const
{
    return m_lastError;
}

QString SessionStore::databasePath() const
{
    return m_path;
}

qint64 SessionStore::createConversation(const QString &title, const QString &workdir)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO conversations (title, workdir, created_at, updated_at) "
        "VALUES (?, ?, ?, ?)"));
    const QString stamp = nowIso();
    query.addBindValue(notNull(title));
    query.addBindValue(notNull(workdir));
    query.addBindValue(stamp);
    query.addBindValue(stamp);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

void SessionStore::renameConversation(qint64 conversationId, const QString &title)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE conversations SET title = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(notNull(title));
    query.addBindValue(nowIso());
    query.addBindValue(conversationId);
    if (!query.exec())
        m_lastError = query.lastError().text();
}

void SessionStore::deleteConversation(qint64 conversationId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM conversations WHERE id = ?"));
    query.addBindValue(conversationId);
    if (!query.exec())
        m_lastError = query.lastError().text(); // messages 经外键级联删除
}

bool SessionStore::appendMessage(qint64 conversationId, const Message &message)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO messages (conversation_id, role, content, tool_calls, tool_call_id, reasoning, usage_json, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(conversationId);
    query.addBindValue(roleToString(message.role));
    query.addBindValue(notNull(message.content));
    query.addBindValue(notNull(toolCallsToJson(message.toolCalls)));
    query.addBindValue(notNull(message.toolCallId));
    query.addBindValue(notNull(message.reasoning));
    query.addBindValue(notNull(QString::fromStdString(usageToJson(message.usage).dump())));
    query.addBindValue(message.createdAt.isValid() ? message.createdAt.toString(Qt::ISODateWithMs)
                                                   : nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    QSqlQuery touch(m_db);
    touch.prepare(QStringLiteral("UPDATE conversations SET updated_at = ? WHERE id = ?"));
    touch.addBindValue(nowIso());
    touch.addBindValue(conversationId);
    touch.exec();
    return true;
}

QList<Conversation> SessionStore::conversations() const
{
    QList<Conversation> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral(
        "SELECT id, title, workdir, created_at, updated_at "
        "FROM conversations ORDER BY updated_at DESC"));
    while (query.next()) {
        Conversation conversation;
        conversation.id = query.value(0).toLongLong();
        conversation.title = query.value(1).toString();
        conversation.workdir = query.value(2).toString();
        conversation.createdAt =
            QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        conversation.updatedAt =
            QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        result.append(conversation);
    }
    return result;
}

QList<Message> SessionStore::messages(qint64 conversationId) const
{
    QList<Message> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT role, content, tool_calls, tool_call_id, reasoning, usage_json, created_at FROM messages "
        "WHERE conversation_id = ? ORDER BY id"));
    query.addBindValue(conversationId);
    query.exec();
    while (query.next()) {
        Message message;
        message.role = roleFromString(query.value(0).toString()).value_or(Role::User);
        message.content = query.value(1).toString();
        message.toolCalls = toolCallsFromJson(query.value(2).toString());
        message.toolCallId = query.value(3).toString();
        message.reasoning = query.value(4).toString();
        message.usage =
            usageFromJson(nlohmann::json::parse(query.value(5).toString().toStdString(),
                                                nullptr, false));
        message.createdAt =
            QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        result.append(message);
    }
    return result;
}

} // namespace lens
