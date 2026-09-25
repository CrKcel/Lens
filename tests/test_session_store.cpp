#include <QtTest/QtTest>

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
