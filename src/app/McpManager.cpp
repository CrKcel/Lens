#include "McpManager.hpp"

#include "lens/core/mcp/McpTool.hpp"

#include <QMetaObject>
#include <QThreadPool>

namespace lens {

QString McpManager::configKey(const QList<McpServerConfig> &servers)
{
    QStringList parts;
    for (const McpServerConfig &server : servers)
        parts.append(server.name + QLatin1Char('\x1f') + server.command
                     + QLatin1Char('\x1f') + server.args.join(QLatin1Char('\x1e')));
    return parts.join(QLatin1Char('\x1d'));
}

McpManager::McpManager(ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_pool(std::make_unique<QThreadPool>())
{
    m_pool->setMaxThreadCount(1);
}

McpManager::~McpManager()
{
    // 后台任务只持有自己的副本、结果经 invokeMethod 投回本对象，等它们跑完再拆
    // 成员，避免任务触到已析构的 this（投递中的事件由 ~QObject 自行回收）。
    // 取舍：连接进行到一半时关窗，关闭会被拖到握手超时（McpClient 的阻塞等待
    // 不可中断）；代价从原来的"启动即冻结"移到了这里，且只在关窗恰好撞上连接
    // 时出现。要让等待可中断，得给 McpClient 加一个原子取消标志
    m_pool->waitForDone();
}

void McpManager::requestReload(const QList<McpServerConfig> &servers, bool streaming)
{
    if (m_loaded && configKey(servers) == m_loadedKey)
        return;
    if (streaming) {
        m_pendingReload = true;
        return;
    }
    m_pendingReload = false;
    reload(servers);
}

void McpManager::applyPending(const QList<McpServerConfig> &servers)
{
    if (!m_pendingReload)
        return;
    m_pendingReload = false;
    reload(servers);
}

void McpManager::reload(const QList<McpServerConfig> &servers)
{
    // 调用方保证此刻没有工具正在执行：摘掉旧工具、断开旧连接，再把新连接交给后台
    for (const auto &entry : m_toolOrigins)
        m_registry->removeTool(entry.first);
    m_toolOrigins.clear();

    const QList<std::shared_ptr<mcp::McpClient>> obsolete = m_live;
    m_live.clear();
    stopInBackground(obsolete);

    m_status.clear();
    QList<std::shared_ptr<mcp::McpClient>> clients;
    clients.reserve(servers.size());
    for (const McpServerConfig &config : servers) {
        // 客户端在主线程创建（构造不做阻塞 IO），后台任务只负责 start / listTools
        auto client = std::make_shared<mcp::McpClient>(
            mcp::ServerConfig{config.name, config.command, config.args});
        m_clients.append(client);
        clients.append(std::move(client));
        m_status.append(QVariantMap{{QStringLiteral("name"), config.name},
                                    {QStringLiteral("command"), config.command},
                                    {QStringLiteral("pending"), true},
                                    {QStringLiteral("connected"), false},
                                    {QStringLiteral("status"), QStringLiteral("连接中…")}});
    }
    emit changed(); // 先让界面显示"连接中…"，结果到位后由 applyOutcomes 再刷一次

    m_loaded = true;
    m_loadedKey = configKey(servers);
    const quint64 generation = ++m_generation;
    m_pool->start([this, generation, servers, clients] {
        QList<Outcome> outcomes;
        outcomes.reserve(clients.size());
        for (qsizetype i = 0; i < clients.size(); ++i) {
            Outcome outcome;
            outcome.name = servers[i].name;
            outcome.command = servers[i].command;
            outcome.client = clients[i];
            QString error;
            if (clients[i]->start(&error))
                outcome.tools = clients[i]->listTools(&error);
            outcome.error = error;
            outcomes.append(std::move(outcome));
        }
        QMetaObject::invokeMethod(
            this, [this, generation, outcomes] { applyOutcomes(generation, outcomes); },
            Qt::QueuedConnection);
    });
}

void McpManager::applyOutcomes(quint64 generation, const QList<Outcome> &outcomes)
{
    if (generation != m_generation) {
        // 期间配置又变过：这批连接结果作废，已启动的进程要停掉，状态归新一轮管
        QList<std::shared_ptr<mcp::McpClient>> stale;
        for (const Outcome &outcome : outcomes)
            stale.append(outcome.client);
        stopInBackground(std::move(stale));
        return;
    }

    QList<std::shared_ptr<mcp::McpClient>> failed;
    m_status.clear();
    for (const Outcome &outcome : outcomes) {
        QVariantMap status{{QStringLiteral("name"), outcome.name},
                           {QStringLiteral("command"), outcome.command},
                           {QStringLiteral("pending"), false}};
        if (outcome.error.isEmpty()) {
            QStringList toolNames;
            for (const mcp::McpClient::ToolInfo &info : outcome.tools) {
                const QString exposed = mcp::McpTool::exposedName(outcome.name, info.name);
                m_registry->registerTool(
                    std::make_shared<mcp::McpTool>(outcome.name, info, outcome.client));
                m_toolOrigins.append({exposed, QStringLiteral("MCP:%1").arg(outcome.name)});
                toolNames.append(exposed);
            }
            m_live.append(outcome.client);
            status.insert(QStringLiteral("connected"), true);
            status.insert(QStringLiteral("toolNames"), toolNames);
            status.insert(QStringLiteral("status"),
                          QStringLiteral("已连接，%1 个工具").arg(toolNames.size()));
        } else {
            // 连不上的服务器不能留着子进程（客户端由 m_clients 锚定，不会自行析构）
            failed.append(outcome.client);
            status.insert(QStringLiteral("connected"), false);
            status.insert(QStringLiteral("status"), outcome.error);
        }
        m_status.append(status);
    }
    stopInBackground(std::move(failed)); // 空列表时是廉价的无操作
    emit changed();
}

void McpManager::stopInBackground(QList<std::shared_ptr<mcp::McpClient>> clients)
{
    if (clients.isEmpty())
        return;
    m_pool->start([clients = std::move(clients)] {
        for (const auto &client : clients)
            client->stop(); // waitForFinished 阻塞，只能放后台
    });
}

} // namespace lens
