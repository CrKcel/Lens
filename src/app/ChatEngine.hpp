#pragma once

#include "ChatSession.hpp"
#include "McpManager.hpp"
#include "lens/core/providers/ModelListClient.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <memory>

class QThreadPool;

namespace lens {

class AppSettings;
class ChatController;
class SessionStore;

// 全局共享层：会话注册表（按会话 id 持有 ChatSession）、工具注册表与 MCP
// 连接（全应用一份，MCP 子进程不重复）、模型清单拉取。会话在窗口切走后
// 照常生成（后台生成）。全部状态只在主线程访问（AgentSession/QNetworkTransport
// 的事件循环模型，SessionStore 的线程亲和性都不允许其他线程）。
class ChatEngine : public QObject
{
    Q_OBJECT
public:
    ChatEngine(SessionStore *store, AppSettings *settings, const QString &dataDir,
               QObject *parent = nullptr);
    ~ChatEngine() override;

    // 测试注入口：会话创建时用假传输替代 QNetworkTransport
    void setTransportFactory(std::function<std::unique_ptr<ITransport>()> factory)
    {
        m_transportFactory = std::move(factory);
    }

    SessionStore *store() const { return m_store; }
    AppSettings *settings() const { return m_settings; }
    const QString &dataDir() const { return m_dataDir; }

    // 会话注册表：惰性创建并从 store 载入
    ChatSession *sessionFor(qint64 conversationId);
    // 新建会话（store 落库 + 注册表登记），返回绑定窗口用的会话对象
    ChatSession *createConversation(const QString &workdir);
    void deleteConversation(qint64 conversationId);
    // 退出前停掉全部在跑的回合（后台会话不再有归属）
    void stopAll();

    bool anyStreaming() const { return !m_streamingIds.isEmpty(); }
    QSet<qint64> streamingIds() const { return m_streamingIds; }

    // 工具/MCP（facade 转发给上下文检查器）
    QVariantList toolList() const { return m_toolList; }
    QVariantList mcpStatus() const { return m_mcp->status(); }
    // 设置变化后的整体刷新：工具禁用集合 + MCP 重载（任一会话流式则挂起）
    // + 各会话提示词分节重建
    void refreshContext();

    void notifyConversationsChanged() { emit conversationsChanged(); }

    // 模型清单拉取（设置页用；同一时间仅一次，结果经信号返回）
    void fetchModels(const QString &protocol, const QString &endpoint, const QString &apiKey);
    bool fetchingModels() const { return m_fetchingModels; }

signals:
    void sessionStreamingChanged(qint64 conversationId, bool streaming);
    void conversationsChanged(); // 会话增删/改名，facade 据此重载各自的列表模型
    void sessionClosed(qint64 conversationId);
    void contextChanged(); // 工具清单 / MCP 状态变化
    void fetchingModelsChanged();
    void modelsFetched(const QStringList &models);
    void modelsFetchFailed(const QString &error);

private:
    ChatSession *createSession(qint64 conversationId);
    void registerBuiltinTools();
    void applyToolSettings();
    void rebuildToolList();
    void onSessionStreaming(qint64 conversationId, bool streaming);

    SessionStore *m_store;
    AppSettings *m_settings;
    QString m_dataDir;
    ToolRegistry m_registry;
    QStringList m_builtinToolNames; // 注册时的内置工具名（bash 工具名随 shell 变化）
    std::unique_ptr<McpManager> m_mcp;
    QHash<qint64, ChatSession *> m_sessions;
    QSet<qint64> m_streamingIds;
    QVariantList m_toolList;
    ModelListClient m_modelListClient;
    bool m_fetchingModels = false;

    std::function<std::unique_ptr<ITransport>()> m_transportFactory;
};

} // namespace lens
