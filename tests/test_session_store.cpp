#include <QtTest/QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <lens/core/storage/SessionStore.hpp>

using namespace lens;

class TestSessionStore : public QObject
{
    Q_OBJECT

private slots:
    void roundtripPersistsAcrossReopen();
    void messagesAreOrderedPerConversation();
    void toolCallsAndResultsRoundtrip();
    void imagesRoundtrip();
    void filesRoundtrip();
    void usageRoundtripAndLegacyMigration();
    void renameAndDeleteConversation();
};

void TestSessionStore::roundtripPersistsAcrossReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("sessions.db"));

    qint64 conversationId = 0;
    {
        SessionStore store(dbPath);
        QVERIFY(store.open());
        QVERIFY(store.lastError().isEmpty());

        conversationId = store.createConversation(
            QStringLiteral("demo"), QStringLiteral("/tmp/proj"));
        QVERIFY(conversationId > 0);

        Message user;
        user.role = Role::User;
        user.content = QStringLiteral("hello");
        QVERIFY(store.appendMessage(conversationId, user));

    Message assistant;
    assistant.role = Role::Assistant;
    assistant.content = QStringLiteral("world");
    assistant.reasoning = QStringLiteral("思考过程");
    QVERIFY(store.appendMessage(conversationId, assistant));

        QCOMPARE(store.messages(conversationId).size(), 2);
        QCOMPARE(store.conversations().size(), 1);
        QCOMPARE(store.conversations().first().title, QStringLiteral("demo"));
        QCOMPARE(store.conversations().first().workdir, QStringLiteral("/tmp/proj"));
    }
    {
        SessionStore store(dbPath);
        QVERIFY(store.open());

        QCOMPARE(store.conversations().size(), 1);
        const auto messages = store.messages(conversationId);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].role, Role::User);
        QCOMPARE(messages[0].content, QStringLiteral("hello"));
        QCOMPARE(messages[1].role, Role::Assistant);
        QCOMPARE(messages[1].content, QStringLiteral("world"));
        QCOMPARE(messages[1].reasoning, QStringLiteral("思考过程"));
    }
}

void TestSessionStore::messagesAreOrderedPerConversation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionStore store(dir.filePath(QStringLiteral("sessions.db")));
    QVERIFY(store.open());

    const qint64 first = store.createConversation(QStringLiteral("a"), QString());
    const qint64 second = store.createConversation(QStringLiteral("b"), QString());
    if (first <= 0 || second <= 0)
        QFAIL(qPrintable(QStringLiteral(
            "createConversation failed (first=%1, second=%2): %3")
            .arg(first).arg(second).arg(store.lastError())));
    QVERIFY(first != second);

    Message q1; q1.role = Role::User; q1.content = QStringLiteral("q1");
    Message q2; q2.role = Role::User; q2.content = QStringLiteral("q2");
    Message a1; a1.role = Role::Assistant; a1.content = QStringLiteral("a1");
    QVERIFY(store.appendMessage(first, q1));
    QVERIFY(store.appendMessage(second, q2));
    QVERIFY(store.appendMessage(first, a1));

    const auto firstMessages = store.messages(first);
    QCOMPARE(firstMessages.size(), 2);
    QCOMPARE(firstMessages[0].content, QStringLiteral("q1"));
    QCOMPARE(firstMessages[1].content, QStringLiteral("a1"));
    QCOMPARE(store.messages(second).size(), 1);
}

void TestSessionStore::toolCallsAndResultsRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionStore store(dir.filePath(QStringLiteral("sessions.db")));
    QVERIFY(store.open());

    const qint64 conversationId = store.createConversation(QStringLiteral("tools"), QString());
    QVERIFY(conversationId > 0);

    Message assistant;
    assistant.role = Role::Assistant;
    assistant.toolCalls.append({QStringLiteral("call_1"), QStringLiteral("read"),
                                QStringLiteral("{\"path\":\"a.txt\"}")});
    assistant.toolCalls.append({QStringLiteral("call_2"), QStringLiteral("bash"),
                                QStringLiteral("{\"command\":\"ls\"}")});
    QVERIFY(store.appendMessage(conversationId, assistant));

    Message toolResult;
    toolResult.role = Role::Tool;
    toolResult.content = QStringLiteral("file contents");
    toolResult.toolCallId = QStringLiteral("call_1");
    QVERIFY(store.appendMessage(conversationId, toolResult));

    const auto messages = store.messages(conversationId);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0].toolCalls.size(), 2);
    QCOMPARE(messages[0].toolCalls[0].name, QStringLiteral("read"));
    QCOMPARE(messages[0].toolCalls[1].id, QStringLiteral("call_2"));
    QCOMPARE(messages[1].role, Role::Tool);
    QCOMPARE(messages[1].toolCallId, QStringLiteral("call_1"));
    QCOMPARE(messages[1].content, QStringLiteral("file contents"));
}

void TestSessionStore::imagesRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("sessions.db"));

    const QByteArray png = QByteArray("\x89PNG\r\n\x1A\n", 8) + QByteArray("pixels");
    const QByteArray jpeg = QByteArray("\xFF\xD8\xFF", 3) + QByteArray("data");
    qint64 conversationId = 0;
    {
        SessionStore store(dbPath);
        QVERIFY(store.open());
        conversationId = store.createConversation(QStringLiteral("images"), QString());
        QVERIFY(conversationId > 0);
        Message user;
        user.role = Role::User;
        user.content = QStringLiteral("看这两张图");
        user.images.append({QStringLiteral("image/png"), png});
        user.images.append({QStringLiteral("image/jpeg"), jpeg});
        QVERIFY(store.appendMessage(conversationId, user));
    }

    // 重开后回读：字节与 MIME 均无损
    SessionStore store(dbPath);
    QVERIFY(store.open());
    const auto messages = store.messages(conversationId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages[0].images.size(), 2);
    QCOMPARE(messages[0].images[0].mimeType, QStringLiteral("image/png"));
    QCOMPARE(messages[0].images[0].data, png);
    QCOMPARE(messages[0].images[1].mimeType, QStringLiteral("image/jpeg"));
    QCOMPARE(messages[0].images[1].data, jpeg);
}

void TestSessionStore::filesRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("sessions.db"));

    qint64 conversationId = 0;
    {
        SessionStore store(dbPath);
        QVERIFY(store.open());
        conversationId = store.createConversation(QStringLiteral("files"), QString());
        QVERIFY(conversationId > 0);
        Message user;
        user.role = Role::User;
        user.content = QStringLiteral("看这两个文件");
        user.files.append({QStringLiteral("a.txt"), QStringLiteral("第一行\n第二行")});
        user.files.append({QStringLiteral("b.md"), QStringLiteral("# 标题")});
        QVERIFY(store.appendMessage(conversationId, user));
    }

    // 重开后回读：文件名与内容均无损
    SessionStore store(dbPath);
    QVERIFY(store.open());
    const auto messages = store.messages(conversationId);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages[0].files.size(), 2);
    QCOMPARE(messages[0].files[0].fileName, QStringLiteral("a.txt"));
    QCOMPARE(messages[0].files[0].content, QStringLiteral("第一行\n第二行"));
    QCOMPARE(messages[0].files[1].fileName, QStringLiteral("b.md"));
    QCOMPARE(messages[0].files[1].content, QStringLiteral("# 标题"));
}

void TestSessionStore::usageRoundtripAndLegacyMigration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("sessions.db"));

    qint64 conversationId = 0;
    {
        SessionStore store(dbPath);
        QVERIFY(store.open());
        conversationId = store.createConversation(QStringLiteral("usage"), QString());
        QVERIFY(conversationId > 0);
        Message assistant;
        assistant.role = Role::Assistant;
        assistant.content = QStringLiteral("ok");
        assistant.usage.valid = true;
        assistant.usage.promptTokens = 321;
        assistant.usage.completionTokens = 45;
        assistant.usage.cachedTokens = 200;
        QVERIFY(store.appendMessage(conversationId, assistant));
    }

    // 重开（ensureColumn 走已存在分支）后 usage 完整回读
    SessionStore store(dbPath);
    QVERIFY(store.open());
    const auto messages = store.messages(conversationId);
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages[0].usage.valid);
    QCOMPARE(messages[0].usage.promptTokens, 321);
    QCOMPARE(messages[0].usage.completionTokens, 45);
    QCOMPARE(messages[0].usage.cachedTokens, 200);

    // 老库（无 usage_json 列）迁移：手工建一张旧 schema 的库再打开
    QTemporaryDir legacyDir;
    QVERIFY(legacyDir.isValid());
    const QString legacyDb = legacyDir.filePath(QStringLiteral("legacy.db"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("legacy_migration"));
        db.setDatabaseName(legacyDb);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(
            "CREATE TABLE conversations (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT NOT NULL, workdir TEXT NOT NULL DEFAULT '', "
            "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"));
        QVERIFY(query.exec(
            "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE, "
            "role TEXT NOT NULL, content TEXT NOT NULL, created_at TEXT NOT NULL)"));
        QVERIFY(query.exec(
            "INSERT INTO conversations (title, workdir, created_at, updated_at) "
            "VALUES ('old', '', '2026-01-01T00:00:00.000', '2026-01-01T00:00:00.000')"));
        QVERIFY(query.exec(
            "INSERT INTO messages (conversation_id, role, content, created_at) "
            "VALUES (1, 'assistant', '旧数据', '2026-01-01T00:00:00.000')"));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("legacy_migration"));

    SessionStore legacyStore(legacyDb);
    QVERIFY(legacyStore.open());
    const auto legacyMessages = legacyStore.messages(1);
    QCOMPARE(legacyMessages.size(), 1);
    QCOMPARE(legacyMessages[0].content, QStringLiteral("旧数据"));
    QVERIFY(!legacyMessages[0].usage.valid); // 无 usage 历史视为未上报
}

void TestSessionStore::renameAndDeleteConversation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionStore store(dir.filePath(QStringLiteral("sessions.db")));
    QVERIFY(store.open());

    const qint64 id = store.createConversation(QStringLiteral("old"), QStringLiteral("/w"));
    QVERIFY(id > 0);
    Message m; m.role = Role::User; m.content = QStringLiteral("x");
    QVERIFY(store.appendMessage(id, m));

    store.renameConversation(id, QStringLiteral("new"));
    QCOMPARE(store.conversations().first().title, QStringLiteral("new"));

    store.deleteConversation(id);
    QVERIFY(store.conversations().isEmpty());
    QVERIFY(store.messages(id).isEmpty()); // 外键级联删除
}

QTEST_GUILESS_MAIN(TestSessionStore)
#include "test_session_store.moc"
