#include <QtTest/QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include "AppSettings.hpp"
#include "McpManager.hpp"

using namespace lens;

// McpManager 的异步连接管线：阻塞的 stdio 客户端全部跑在后台线程，主线程只
// 收状态。真机连接用例需要 LENS_MCP_COMMAND（与 test_mcp 同一约定），未设置
// 时打警告跳过
class TestMcpManager : public QObject
{
    Q_OBJECT

private slots:
    void emptyServerListKeepsStatusEmpty();
    void failedServerIsReportedAsynchronously();
    void connectsAndRegistersTools();

private:
    static QList<McpServerConfig> envCommand();
    static bool waitUntil(const std::function<bool()> &condition, int timeoutMs);
    QTemporaryDir m_dir;
};

QList<McpServerConfig> TestMcpManager::envCommand()
{
    const QString command = qEnvironmentVariable("LENS_MCP_COMMAND");
    if (command.trimmed().isEmpty())
        return {};
    const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return {{QStringLiteral("everything"), parts.first(), parts.mid(1)}};
}

bool TestMcpManager::waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

void TestMcpManager::emptyServerListKeepsStatusEmpty()
{
    ToolRegistry registry;
    McpManager manager(&registry);
    QSignalSpy spy(&manager, &McpManager::changed);

    manager.requestReload({}, false);
    QCOMPARE(spy.count(), 1); // 空配置也要初始化状态清单
    QVERIFY(manager.status().isEmpty());
    QVERIFY(manager.toolOrigins().isEmpty());
    QVERIFY(!manager.hasPendingReload());
}

void TestMcpManager::failedServerIsReportedAsynchronously()
{
    ToolRegistry registry;
    McpManager manager(&registry);

    const QList<McpServerConfig> servers{{QStringLiteral("bad"),
                                          QStringLiteral("/nonexistent/lens-mcp-server"), {}}};
    manager.requestReload(servers, false);
    // 请求立即返回、状态先落在"连接中"：连接本身在后台线程做
    QCOMPARE(manager.status().size(), 1);
    QVERIFY(manager.status().first().toMap().value(QStringLiteral("pending")).toBool());

    QSignalSpy spy(&manager, &McpManager::changed);
    manager.requestReload(servers, false); // 指纹未变：不重连，也不再发信号
    QCOMPARE(spy.count(), 0);

    // 连接在后台进行：启动失败的结果经事件循环回灌，状态从"连接中"变为错误
    QVERIFY(waitUntil([&manager] {
                const QVariantMap status = manager.status().first().toMap();
                return !status.value(QStringLiteral("pending")).toBool();
            },
            30000));
    const QVariantMap status = manager.status().first().toMap();
    QCOMPARE(status.value(QStringLiteral("connected")).toBool(), false);
    QVERIFY(!status.value(QStringLiteral("status")).toString().isEmpty());
    QVERIFY(manager.toolOrigins().isEmpty());
    QVERIFY(spy.count() > 0); // 结果到位后还会通知一次，界面据此刷新

    // 配置变化时旧连接被摘掉、重新连接（仍然连不上，但不留残留工具）
    manager.requestReload({{QStringLiteral("bad2"), QStringLiteral("/nonexistent/other"), {}}},
                          false);
    QCOMPARE(manager.status().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("bad2"));
}

void TestMcpManager::connectsAndRegistersTools()
{
    const QList<McpServerConfig> servers = envCommand();
    if (servers.isEmpty()) {
        QSKIP("未设置 LENS_MCP_COMMAND（MCP 服务器启动命令），跳过真机连接用例");
    }

    ToolRegistry registry;
    McpManager manager(&registry);

    QElapsedTimer elapsed;
    elapsed.start();
    manager.requestReload(servers, false);
    const qint64 requestMs = elapsed.elapsed();
    // 关键：requestReload 不在主线程等待服务器启动/握手（旧实现会阻塞数秒）
    QVERIFY2(requestMs < 2000, qPrintable(QStringLiteral("requestReload 阻塞了 %1ms")
                                             .arg(requestMs)));
    qInfo("requestReload 立即返回，耗时 %lldms", static_cast<long long>(requestMs));
    QVERIFY(manager.status().first().toMap().value(QStringLiteral("pending")).toBool());

    QVERIFY2(waitUntil([&manager] {
                 return manager.status().first().toMap()
                     .value(QStringLiteral("connected")).toBool();
             },
             120000),
             qPrintable(manager.status().first().toMap()
                            .value(QStringLiteral("status")).toString()));

    const QVariantMap status = manager.status().first().toMap();
    QVERIFY(!status.value(QStringLiteral("pending")).toBool());
    QVERIFY(status.value(QStringLiteral("toolNames")).toStringList().size() > 0);

    // 远程工具已桥接进注册表，且能在工具线程外按名字查到
    QVERIFY(!manager.toolOrigins().isEmpty());
    for (const auto &[toolName, origin] : manager.toolOrigins()) {
        QVERIFY(toolName.startsWith(QStringLiteral("mcp_")));
        QCOMPARE(origin, QStringLiteral("MCP:everything"));
    }
    // 注册表里的工具名必须与 toolOrigins 的键一致（否则检查器会把它们标成内置）
    const QString lastName = manager.toolOrigins().last().first;
    bool found = false;
    for (const ToolSpec &spec : registry.specs()) {
        if (spec.name == lastName)
            found = true;
    }
    QVERIFY2(found, qPrintable(lastName));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestMcpManager test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_mcp_manager.moc"
