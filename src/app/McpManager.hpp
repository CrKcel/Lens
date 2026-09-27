#pragma once

#include "AppSettings.hpp"
#include "lens/core/mcp/McpClient.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <memory>

class QThreadPool;

namespace lens {

// MCP 服务器连接管理。stdio 客户端是同步阻塞实现（启动 + initialize 握手 +
// tools/list，单个服务器最坏数十秒），因此连接与断开全部在自己的单线程池里
// 执行，结果经事件循环回灌主线程后再写入 ToolRegistry：主线程不因 MCP 卡顿，
// 注册表也只在工具线程空闲时被改写（流式回合中的重载挂起到回合结束，见
// ChatController 的 requestReload / applyPending 调用点）。
class McpManager : public QObject
{
    Q_OBJECT
public:
    explicit McpManager(ToolRegistry *registry, QObject *parent = nullptr);
    ~McpManager() override;

    // 服务器清单指纹（名称/命令/参数）：不变则无需重连
    static QString configKey(const QList<McpServerConfig> &servers);

    // 配置变化入口。流式回合中（streaming 为 true）工具线程正在遍历注册表，
    // 只记下待办，由 applyPending 在回合结束后补做
    void requestReload(const QList<McpServerConfig> &servers, bool streaming);

    // 回合结束后调用：有待办重载才动手
    void applyPending(const QList<McpServerConfig> &servers);
    bool hasPendingReload() const { return m_pendingReload; }

    QVariantList status() const { return m_status; }
    QVector<QPair<QString, QString>> toolOrigins() const { return m_toolOrigins; }

signals:
    void changed(); // 连接状态或注册表内容变化，调用方据此重建工具清单

private:
    // 一个服务器的连接结果：后台线程填充，主线程消费
    struct Outcome {
        QString name;
        QString command;
        std::shared_ptr<mcp::McpClient> client; // 连接成功时非空
        QList<mcp::McpClient::ToolInfo> tools;
        QString error;
    };

    void reload(const QList<McpServerConfig> &servers); // 主线程：摘旧工具 + 发起后台连接
    void applyOutcomes(quint64 generation, const QList<Outcome> &outcomes);
    void stopInBackground(QList<std::shared_ptr<mcp::McpClient>> clients);

    ToolRegistry *m_registry;
    // 单线程：MCP 工作串行执行，且与工具执行用的全局池隔离（长阻塞不拖慢工具）
    std::unique_ptr<QThreadPool> m_pool;
    // 全部客户端（含已断开的）：生命周期钉在主线程，工具线程只用不销毁。
    // 已断开的只占一个空壳对象，数量随重载次数增长，量级可忽略
    QList<std::shared_ptr<mcp::McpClient>> m_clients;
    QList<std::shared_ptr<mcp::McpClient>> m_live; // 当前已连接（重载时需断开）
    QVector<QPair<QString, QString>> m_toolOrigins; // 工具名 → "MCP:服务器"
    QVariantList m_status;
    QString m_loadedKey;
    bool m_loaded = false; // 首次请求必然加载（配置为空也要初始化状态清单）
    bool m_pendingReload = false;
    quint64 m_generation = 0; // 丢弃过期连接结果（期间配置又变过）
};

} // namespace lens
