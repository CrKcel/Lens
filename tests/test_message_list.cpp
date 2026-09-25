#include <QtTest/QtTest>

#include "MessageListModel.hpp"

using namespace lens;

class TestMessageList : public QObject
{
    Q_OBJECT

private slots:
    void streamingDeltasAccumulateInOneRow();
    void reasoningStreamsIntoSameRow();
    void emptyStreamingRowDropped();
    void resetFromMessagesBuildsReasoningAndToolRows();
};

void TestMessageList::streamingDeltasAccumulateInOneRow()
{
    MessageListModel model;
    MessageListModel::Item item;
    item.kind = MessageListModel::Assistant;
    item.streaming = true;
    model.appendItem(item);

    QVERIFY(model.hasStreamingRow());
    model.appendDelta(QStringLiteral("你好"));
    model.appendDelta(QStringLiteral("，世界"));
    QCOMPARE(model.rowCount(), 1); 
    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, MessageListModel::TextRole).toString(),
             QStringLiteral("你好，世界"));
    QVERIFY(model.data(idx, MessageListModel::StreamingRole).toBool());

    model.finishStreamingRow(QStringLiteral("你好，世界"), {});
    QVERIFY(!model.hasStreamingRow());
    QVERIFY(!model.data(model.index(0, 0), MessageListModel::StreamingRole).toBool());
}

void TestMessageList::reasoningStreamsIntoSameRow()
{
    MessageListModel model;
    MessageListModel::Item item;
    item.kind = MessageListModel::Assistant;
    item.streaming = true;
    model.appendItem(item);

    model.appendReasoningDelta(QStringLiteral("思考"));
    model.appendReasoningDelta(QStringLiteral("中"));
    model.appendDelta(QStringLiteral("答案"));
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, MessageListModel::ReasoningRole).toString(),
             QStringLiteral("思考中"));
    QCOMPARE(model.data(idx, MessageListModel::TextRole).toString(),
             QStringLiteral("答案"));

    model.finishStreamingRow(QStringLiteral("答案"), QStringLiteral("思考中"));
    QCOMPARE(model.data(model.index(0, 0), MessageListModel::ReasoningRole).toString(),
             QStringLiteral("思考中"));
}

void TestMessageList::emptyStreamingRowDropped()
{
    MessageListModel model;
    MessageListModel::Item item;
    item.kind = MessageListModel::Assistant;
    item.streaming = true;
    model.appendItem(item);
    model.finishStreamingRow(QString(), QString());
    model.dropEmptyStreamingRow();
    QCOMPARE(model.rowCount(), 0);
}

void TestMessageList::resetFromMessagesBuildsReasoningAndToolRows()
{
    MessageListModel model;
    QList<Message> history;

    Message user;
    user.role = Role::User;
    user.content = QStringLiteral("hi");
    history.append(user);

    Message assistant;
    assistant.role = Role::Assistant;
    assistant.reasoning = QStringLiteral("思考内容");
    assistant.toolCalls.append({QStringLiteral("call_1"), QStringLiteral("read"),
                                QStringLiteral("{}")});
    history.append(assistant);

    Message tool;
    tool.role = Role::Tool;
    tool.content = QStringLiteral("结果");
    tool.toolCallId = QStringLiteral("call_1");
    history.append(tool);

    Message final_;
    final_.role = Role::Assistant;
    final_.content = QStringLiteral("完成");
    history.append(final_);

    model.resetFromMessages(history);

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(1, 0), MessageListModel::ReasoningRole).toString(),
             QStringLiteral("思考内容"));
    QCOMPARE(model.data(model.index(1, 0), MessageListModel::TextRole).toString(), QString());
    QCOMPARE(model.data(model.index(2, 0), MessageListModel::TextRole).toString(),
             QStringLiteral("结果"));
    QVERIFY(!model.data(model.index(2, 0), MessageListModel::ToolPendingRole).toBool());
    QCOMPARE(model.data(model.index(3, 0), MessageListModel::TextRole).toString(),
             QStringLiteral("完成"));
}

QTEST_GUILESS_MAIN(TestMessageList)
#include "test_message_list.moc"
