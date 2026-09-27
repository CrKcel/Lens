#include <QtTest/QtTest>

#include "ConversationListModel.hpp"

#include <lens/core/storage/SessionStore.hpp>

#include <memory>

using namespace lens;

namespace {

// 每个用例独立的临时库
std::unique_ptr<SessionStore> makeStore(QTemporaryDir &dir)
{
    auto store = std::make_unique<SessionStore>(dir.filePath(QStringLiteral("sessions.db")));
    return store;
}

} // namespace

class TestConversationList : public QObject
{
    Q_OBJECT

private slots:
    void groupedByWorkdirNewestFirst();
    void toggleGroupCollapsesOnlyItsRows();
    void searchForcesExpand();
    void expandGroupMakesConversationVisible();
};

void TestConversationList::groupedByWorkdirNewestFirst()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto store = makeStore(dir);
    QVERIFY(store->open());
    store->createConversation(QStringLiteral("beta"), QStringLiteral("/b/work"));
    store->createConversation(QStringLiteral("alpha2"), QStringLiteral("/a/work"));
    const qint64 alpha1Id =
        store->createConversation(QStringLiteral("alpha1"), QStringLiteral("/a/work"));
    store->createConversation(QStringLiteral("delta"), QStringLiteral("/c/work"));
    store->createConversation(QStringLiteral("zeta"), QStringLiteral("/Alpha"));
    // alpha1 追加消息后 updated_at 最新 → 组内排最前
    QVERIFY(store->appendMessage(alpha1Id, Message{}));

    ConversationListModel model(store.get());
    QCOMPARE(model.rowCount(), 4 + 5); // 4 个组头 + 5 条会话

    const auto row = [&model](int r, int role) {
        return model.data(model.index(r, 0), role);
    };
    // 排序不区分大小写：/a/work < /Alpha < /b/work < /c/work
    QCOMPARE(row(0, ConversationListModel::IsHeaderRole).toBool(), true);
    QCOMPARE(row(0, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/a/work"));
    QCOMPARE(row(0, ConversationListModel::GroupCountRole).toInt(), 2);
    QCOMPARE(row(1, ConversationListModel::TitleRole).toString(), QStringLiteral("alpha1"));
    QCOMPARE(row(2, ConversationListModel::TitleRole).toString(), QStringLiteral("alpha2"));
    QCOMPARE(row(3, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/Alpha"));
    QCOMPARE(row(3, ConversationListModel::GroupCountRole).toInt(), 1);
    QCOMPARE(row(4, ConversationListModel::TitleRole).toString(), QStringLiteral("zeta"));
    QCOMPARE(row(5, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/b/work"));
    QCOMPARE(row(7, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/c/work"));
    QCOMPARE(row(8, ConversationListModel::TitleRole).toString(), QStringLiteral("delta"));
    // 组头行不携带会话身份
    QCOMPARE(row(0, ConversationListModel::IdRole).toLongLong(), 0LL);
    QCOMPARE(row(1, ConversationListModel::IsHeaderRole).toBool(), false);
}

void TestConversationList::toggleGroupCollapsesOnlyItsRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto store = makeStore(dir);
    QVERIFY(store->open());
    store->createConversation(QStringLiteral("a1"), QStringLiteral("/a"));
    store->createConversation(QStringLiteral("a2"), QStringLiteral("/a"));
    store->createConversation(QStringLiteral("b1"), QStringLiteral("/b"));

    ConversationListModel model(store.get());
    QCOMPARE(model.rowCount(), 2 + 3);

    model.toggleGroup(QStringLiteral("/a"));
    QVERIFY(model.isGroupCollapsed(QStringLiteral("/a")));
    QCOMPARE(model.rowCount(), 2 + 1); // 两组头 + b1，组头保留
    const auto row = [&model](int r, int role) {
        return model.data(model.index(r, 0), role);
    };
    QCOMPARE(row(0, ConversationListModel::IsHeaderRole).toBool(), true);
    QCOMPARE(row(0, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/a"));
    QCOMPARE(row(1, ConversationListModel::WorkdirRole).toString(), QStringLiteral("/b"));
    QCOMPARE(row(2, ConversationListModel::TitleRole).toString(), QStringLiteral("b1"));

    model.toggleGroup(QStringLiteral("/a"));
    QVERIFY(!model.isGroupCollapsed(QStringLiteral("/a")));
    QCOMPARE(model.rowCount(), 2 + 3);
}

void TestConversationList::searchForcesExpand()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto store = makeStore(dir);
    QVERIFY(store->open());
    store->createConversation(QStringLiteral("needle"), QStringLiteral("/a"));
    store->createConversation(QStringLiteral("other"), QStringLiteral("/b"));

    ConversationListModel model(store.get());
    model.toggleGroup(QStringLiteral("/a"));
    QCOMPARE(model.rowCount(), 2 + 1);

    // 搜索命中的会话不被折叠组藏住
    model.setFilter(QStringLiteral("needle"));
    QCOMPARE(model.rowCount(), 1 + 1);
    QCOMPARE(model.data(model.index(1, 0), ConversationListModel::TitleRole).toString(),
             QStringLiteral("needle"));

    // 清掉过滤恢复折叠
    model.setFilter(QString());
    QCOMPARE(model.rowCount(), 2 + 1);
}

void TestConversationList::expandGroupMakesConversationVisible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto store = makeStore(dir);
    QVERIFY(store->open());
    store->createConversation(QStringLiteral("a1"), QStringLiteral("/a"));

    ConversationListModel model(store.get());
    model.toggleGroup(QStringLiteral("/a"));
    QCOMPARE(model.rowCount(), 1);

    // ChatController 新建会话路径：先 expandGroup 再 reload
    model.expandGroup(QStringLiteral("/a"));
    model.reload();
    QVERIFY(!model.isGroupCollapsed(QStringLiteral("/a")));
    QCOMPARE(model.rowCount(), 1 + 1);
    QCOMPARE(model.data(model.index(1, 0), ConversationListModel::TitleRole).toString(),
             QStringLiteral("a1"));
}

QTEST_GUILESS_MAIN(TestConversationList)
#include "test_conversation_list.moc"
