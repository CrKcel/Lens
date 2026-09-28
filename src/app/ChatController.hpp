#pragma once

#include "ConversationListModel.hpp"
#include "McpManager.hpp"
#include "MessageListModel.hpp"
#include "UsageTracker.hpp"
#include "lens/core/agent/AgentSession.hpp"
#include "lens/core/providers/ModelListClient.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class QThreadPool;

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
    Q_PROPERTY(bool fetchingModels READ fetchingModels NOTIFY fetchingModelsChanged)

public:
    explicit ChatController(SessionStore *store, AppSettings *settings, const QString &dataDir,
                            QObject *parent = nullptr);
    ~ChatController() override;

    bool streaming() const { return m_streaming; }
    qint64 currentConversationId() const { return m_conversationId; }
    QString currentWorkdir() const { return m_workdir; }
    QString currentTitle() const { return m_title; }
    QAbstractListModel *conversations() const { return m_conversationModel; }
    QAbstractListModel *messages() const { return m_messageModel; }
    QVariantList contextSections() const;
    QVariantList contextTools() const { return m_toolList; }
    QVariantList mcpStatus() const { return m_mcp->status(); }
    QVariantMap usageSummary() const { return m_usage->summary(); }

    Q_INVOKABLE void newConversation(const QString &workdir);
    Q_INVOKABLE void openConversation(qint64 conversationId);
    Q_INVOKABLE void deleteConversation(qint64 conversationId);
    // 会话列表搜索：标题或消息内容命中（空串恢复全量）
    Q_INVOKABLE void searchConversations(const QString &query);
    Q_INVOKABLE void send(const QString &text, const QString &workdir = QString());
    // 带附件的发送：attachments 每项为 {url, name, isImage}（兼容旧纯字符串路径/data URL）；
    // 图片与文本文件由嗅探分类（文本文件内容随消息发给模型）；
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
    // loadAttachments 的结果：图片与文本附件分类收集
    struct LoadedAttachments {
        QList<ImageAttachment> images;
        QList<TextAttachment> files;
    };

    void connectAgent();
    LoadedAttachments loadAttachments(const QVariantList &attachments) const;
    qint64 createAndOpenConversation(const QString &workdir);
    QVector<ContextSectionInfo> collectSections() const;
    void setErrorRow(const QString &text);
    void registerBuiltinTools();
    void applyToolSettings(); // 按设置（预设/自定义清单）计算禁用集合并写入注册表
    void rebuildToolList();
    // 环境段的 Git 行异步取得（git 子进程最长数秒）：只走后台，主线程拼装时
    // 用缓存，未就绪就省略该行，取到后经 contextChanged 补上
    void refreshGitLine();

    SessionStore *m_store;
    AppSettings *m_settings;
    QString m_dataDir;
    ToolRegistry m_registry;
    QStringList m_builtinToolNames; // 注册时的内置工具名（bash 工具名随 shell 变化）
    std::unique_ptr<McpManager> m_mcp;
    std::unique_ptr<UsageTracker> m_usage;
    MessageListModel *m_messageModel;
    ConversationListModel *m_conversationModel;
    std::unique_ptr<AgentSession> m_agent;

    QVector<ContextSectionInfo> m_lastSections;
    QVariantList m_toolList;

    // 单线程池：环境段 git 子进程不占用工具执行用的全局池
    std::unique_ptr<QThreadPool> m_envPool;
    QString m_gitLine;        // 环境段 Git 行缓存
    QString m_gitLineWorkdir; // 缓存对应的 workdir（不等则视为未就绪）
    quint64 m_gitGeneration = 0;

    qint64 m_conversationId = 0;
    QString m_workdir;
    QString m_title;
    bool m_streaming = false;

    ModelListClient m_modelListClient;
    bool m_fetchingModels = false;
};

} // namespace lens
