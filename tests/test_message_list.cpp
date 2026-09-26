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
    void imagesRoleRoundtrip();
    void filesRoleRoundtrip();
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

void TestMessageList::imagesRoleRoundtrip()
{
    // resetFromMessages：Message.images → ImagesRole data URL，user 行与工具卡片都带
    MessageListModel model;
    QList<Message> history;

    Message user;
    user.role = Role::User;
    user.content = QStringLiteral("看图");
    ImageAttachment attachment;
    attachment.mimeType = QStringLiteral("image/png");
    attachment.data = QByteArray("\x89PNG\r\n\x1A\n", 8);
    user.images.append(attachment);
    history.append(user);

    Message assistant;
    assistant.role = Role::Assistant;
    assistant.toolCalls.append({QStringLiteral("call_1"), QStringLiteral("read"),
                                QStringLiteral("{}")});
    history.append(assistant);

    Message tool;
    tool.role = Role::Tool;
    tool.content = QStringLiteral("图片已读");
    tool.toolCallId = QStringLiteral("call_1");
    tool.images.append(attachment);
    history.append(tool);

    model.resetFromMessages(history);

    // data URL 前缀 + base64 载荷
    const QVariantList userImages =
        model.data(model.index(0, 0), MessageListModel::ImagesRole).toList();
    QCOMPARE(userImages.size(), 1);
    QVERIFY(userImages.first().toString().startsWith(
        QStringLiteral("data:image/png;base64,")));

    // 行序：user(0) → 工具卡片(1)（assistant 无文本不占行）；无主 tool 行不重复
    const QVariantList toolImages =
        model.data(model.index(1, 0), MessageListModel::ImagesRole).toList();
    QCOMPARE(toolImages.size(), 1);
    QVERIFY(toolImages.first().toString() == userImages.first().toString());
    QCOMPARE(model.rowCount(), 2);

    // 运行中工具卡片经 setToolCallResult 回填结果图片
    MessageListModel live;
    MessageListModel::Item call;
    call.kind = MessageListModel::ToolCallItem;
    call.toolCallId = QStringLiteral("call_9");
    call.toolPending = true;
    live.appendItem(call);
    live.setToolCallResult(QStringLiteral("call_9"), QStringLiteral("ok"), {attachment});
    const QModelIndex idx = live.index(0, 0);
    QVERIFY(!live.data(idx, MessageListModel::ToolPendingRole).toBool());
    QCOMPARE(live.data(idx, MessageListModel::TextRole).toString(), QStringLiteral("ok"));
    QCOMPARE(live.data(idx, MessageListModel::ImagesRole).toList().size(), 1);
}

void TestMessageList::filesRoleRoundtrip()
{
    // resetFromMessages：Message.files → FilesRole {name} map，仅 user 行携带
    MessageListModel model;
    QList<Message> history;

    Message user;
    user.role = Role::User;
    user.content = QStringLiteral("看文件");
    user.files.append({QStringLiteral("a.txt"), QStringLiteral("内容")});
    user.files.append({QStringLiteral("b.md"), QStringLiteral("# 标题")});
    history.append(user);

    Message assistant;
    assistant.role = Role::Assistant;
    assistant.content = QStringLiteral("done");
    history.append(assistant);

    model.resetFromMessages(history);

    const QVariantList userFiles =
        model.data(model.index(0, 0), MessageListModel::FilesRole).toList();
    QCOMPARE(userFiles.size(), 2);
    QCOMPARE(userFiles[0].toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("a.txt"));
    QCOMPARE(userFiles[1].toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("b.md"));
    // assistant 行不带文件
    QVERIFY(model.data(model.index(1, 0), MessageListModel::FilesRole).toList().isEmpty());
}

QTEST_GUILESS_MAIN(TestMessageList)
#include "test_message_list.moc"
