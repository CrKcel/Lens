#pragma once

#include "ConversationListModel.hpp"
#include "MessageListModel.hpp"
#include "lens/core/agent/AgentSession.hpp"
#include "lens/core/mcp/McpClient.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace lens {

class AppSettings;
class SessionStore;

// 上下文检查器的一个分节：提示词从哪来、内容是什么
struct ContextSectionInfo {
    QString name; 
    QString source;
    QString content;
};

// UI 与核心层的桥：会话/工作文件夹管理、把 AgentSession 的信号落到
// 显示模型与持久化存储、系统提示词分节组装与上下文透明化。QML 只与此类交互。
class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(qint64 currentConversationId READ currentConversationId NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentWorkdir READ currentWorkdir NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentConversationChanged)
    Q_PROPERTY(QAbstractListModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(QAbstractListModel *messages READ messages CONSTANT)
    Q_PROPERTY(QVariantList contextSections READ contextSections NOTIFY contextChanged)
    Q_PROPERTY(QVariantList contextTools READ contextTools NOTIFY contextChanged)
    Q_PROPERTY(QVariantList mcpStatus READ mcpStatus NOTIFY contextChanged)
    Q_PROPERTY(QVariantMap usageSummary READ usageSummary NOTIFY usageChanged)

public:
    explicit ChatController(SessionStore *store, AppSettings *settings, const QString &dataDir,
                            QObject *parent = nullptr);

    bool streaming() const { return m_streaming; }
    qint64 currentConversationId() const { return m_conversationId; }
    QString currentWorkdir() const { return m_workdir; }
    QString currentTitle() const { return m_title; }
    QAbstractListModel *conversations() const { return m_conversationModel; }
    QAbstractListModel *messages() const { return m_messageModel; }
    QVariantList contextSections() const;
    QVariantList contextTools() const { return m_toolList; }
    QVariantList mcpStatus() const { return m_mcpStatus; }
    QVariantMap usageSummary() const;

    Q_INVOKABLE void newConversation(const QString &workdir);
    Q_INVOKABLE void openConversation(qint64 conversationId);
    Q_INVOKABLE void deleteConversation(qint64 conversationId);
    Q_INVOKABLE void send(const QString &text, const QString &workdir = QString());
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshContext(); // 设置（MCP/工具）变化后重建上下文清单
    Q_INVOKABLE QVariantList skillsList(const QString &workdir) const;

signals:
    void streamingChanged();
    void currentConversationChanged();
    void contextChanged();
    void usageChanged();

private:
    void connectAgent();
    qint64 createAndOpenConversation(const QString &workdir);
    QVector<ContextSectionInfo> collectSections() const;
    void setErrorRow(const QString &text);
    void registerBuiltinTools();
    void loadMcpTools(); // 连接 MCP 服务器并把远程工具桥接进注册表
    void rebuildToolList();
    void resetUsage();                       // 会话切换/清空时归零并重算
    void recordUsage(const TokenUsage &usage); // 累加一次回合用量

    SessionStore *m_store;
    AppSettings *m_settings;
    QString m_dataDir;
    ToolRegistry m_registry;
    QList<std::shared_ptr<mcp::McpClient>> m_mcpClients;
    QVector<QPair<QString, QString>> m_mcpToolOrigins; // 工具名 → "MCP:服务器"
    MessageListModel *m_messageModel;
    ConversationListModel *m_conversationModel;
    std::unique_ptr<AgentSession> m_agent;

    QVector<ContextSectionInfo> m_lastSections;
    QVariantList m_toolList;
    QVariantList m_mcpStatus;

    // 会话用量统计：最近一次输入（= 上下文长度）+ 累计输入/输出/缓存命中
    TokenUsage m_lastUsage;
    qint64 m_totalPrompt = 0;
    qint64 m_totalCompletion = 0;
    qint64 m_totalCached = 0;
    bool m_hasUsage = false;

    qint64 m_conversationId = 0;
    QString m_workdir;
    QString m_title;
    bool m_streaming = false;
};

} // namespace lens
