#pragma once

#include "ConversationListModel.hpp"
#include "MessageListModel.hpp"
#include "lens/core/agent/AgentSession.hpp"
#include "lens/core/mcp/McpClient.hpp"
#include "lens/core/providers/ModelListClient.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace lens {

class AppSettings;
class SessionStore;
class WebSearchTool;

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
    Q_PROPERTY(bool fetchingModels READ fetchingModels NOTIFY fetchingModelsChanged)

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
    // 带图片附件的发送：attachments 每项为图片文件路径或 data URL；
    // thinkingLevel 为会话内临时状态（disabled/low/medium/high/max），不持久化
    Q_INVOKABLE void send(const QString &text, const QString &workdir,
                          const QVariantList &attachments,
                          const QString &thinkingLevel = QStringLiteral("disabled"));
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshContext(); // 设置（MCP/工具）变化后重建上下文清单
    Q_INVOKABLE QVariantList skillsList(const QString &workdir) const;
    Q_INVOKABLE bool clipboardHasImage() const;      // 剪贴板是否携带图片（粘贴转附件）
    Q_INVOKABLE QString clipboardImageDataUrl() const; // 剪贴板图片转 data URL，无图片返回空
    // 从端点拉取可用模型清单。参数取设置页当前表单值（未保存的修改也可拉取）；
    // 结果经 modelsFetched / modelsFetchFailed 信号返回。
    Q_INVOKABLE void fetchModels(const QString &protocol, const QString &endpoint,
                                 const QString &apiKey);
    // 聊天区模型切换：切激活供应商并写回其模型，立即持久化（越界索引/空模型名忽略）
    Q_INVOKABLE void selectModel(int providerIndex, const QString &model);
    bool fetchingModels() const { return m_fetchingModels; }

signals:
    void streamingChanged();
    void currentConversationChanged();
    void contextChanged();
    void usageChanged();
    void fetchingModelsChanged();
    void modelsFetched(const QStringList &models);
    void modelsFetchFailed(const QString &error);

private:
    void connectAgent();
    QList<ImageAttachment> loadAttachments(const QVariantList &attachments) const;
    qint64 createAndOpenConversation(const QString &workdir);
    QVector<ContextSectionInfo> collectSections() const;
    void setErrorRow(const QString &text);
    void registerBuiltinTools();
    void applyToolSettings(); // 按设置（预设/自定义清单）计算禁用集合并写入注册表
    void loadMcpTools(); // 连接 MCP 服务器并把远程工具桥接进注册表
    void rebuildToolList();
    void resetUsage();                       // 会话切换/清空时归零并重算
    void recordUsage(const TokenUsage &usage); // 累加一次回合用量

    SessionStore *m_store;
    AppSettings *m_settings;
    QString m_dataDir;
    ToolRegistry m_registry;
    QStringList m_builtinToolNames; // 注册时的内置工具名（bash 工具名随 shell 变化）
    std::shared_ptr<WebSearchTool> m_webSearchTool; // 保留指针：设置变更后重设端点/密钥
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

    ModelListClient m_modelListClient;
    bool m_fetchingModels = false;
};

} // namespace lens
