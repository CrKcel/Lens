#pragma once

#include "MessageListModel.hpp"
#include "UsageTracker.hpp"
#include "lens/core/agent/AgentSession.hpp"
#include "lens/core/attachments/Attachments.hpp"

#include <QObject>
#include <QVariantList>
#include <memory>

class QThreadPool;

namespace lens {

class AppSettings;
class SessionStore;
class ToolRegistry;

// 上下文检查器的一个分节：提示词从哪来、内容是什么
struct ContextSectionInfo {
    QString name;
    QString source;
    QString content;
};

// 一个会话的界面侧状态与执行体：AgentSession、消息显示模型、用量统计与
// 会话游标（id/workdir/title/streaming）。由 ChatEngine 按会话 id 持有，
// ChatController facade 绑定展示——多会话并行生成：同一会话的回合在其
// 所属窗口切走或关闭后照常进行（后台生成），重新绑定即恢复现场。
class ChatSession : public QObject
{
    Q_OBJECT
public:
    // transport 注入便于测试用假传输；registry 由 ChatEngine 共享提供。
    // 生产路径由 ChatEngine 传 QNetworkTransport
    ChatSession(SessionStore *store, ToolRegistry *registry, AppSettings *settings,
                const QString &dataDir, std::unique_ptr<ITransport> transport,
                QObject *parent = nullptr);
    ~ChatSession() override;

    bool streaming() const { return m_streaming; }
    qint64 conversationId() const { return m_conversationId; }
    QString workdir() const { return m_workdir; }
    QString title() const { return m_title; }
    MessageListModel *messages() const { return m_messageModel; }
    QVariantMap usageSummary() const { return m_usage->summary(); }
    QVariantList contextSections() const;

    // 从 store 载入既有会话；已是该会话则原样返回（生成中重入不得重置流式行）
    void loadConversation(qint64 conversationId);

    // 模型覆盖：本会话用“供应商 + 该供应商下的模型”，空（默认）跟随全局
    // 激活供应商。覆盖影响请求与用量计价，不写回设置
    void setModelOverride(int providerIndex, const QString &modelId);
    void clearModelOverride();
    bool hasModelOverride() const { return m_overrideProviderIndex >= 0; }
    int overrideProviderIndex() const { return m_overrideProviderIndex; }
    QString overrideModel() const { return m_overrideModel; }

    void send(const QString &text, const QVariantList &attachments,
              const QString &thinkingLevel);
    void stop() { m_agent->cancel(); }
    void refreshContext(); // 设置（提示词/环境段）变化后重建上下文清单

signals:
    void streamingChanged();
    void currentConversationChanged();
    void contextChanged();
    void usageChanged();
    void conversationsDirty(); // 会话标题在 send 中改名，请求引擎广播列表变化

private:
    using LoadedAttachments = attachments::LoadedAttachments;

    void connectAgent();
    ProviderConfig effectiveProvider() const;
    QString effectiveModel() const;
    QVector<ContextSectionInfo> collectSections() const;
    void setErrorRow(const QString &text);
    // 环境段的 Git 行异步取得（git 子进程最长数秒）：只走后台，主线程拼装时
    // 用缓存，未就绪就省略该行，取到后经 contextChanged 补上
    void refreshGitLine();

    SessionStore *m_store;
    AppSettings *m_settings;
    QString m_dataDir;
    std::unique_ptr<UsageTracker> m_usage;
    MessageListModel *m_messageModel;
    std::unique_ptr<AgentSession> m_agent;

    QVector<ContextSectionInfo> m_lastSections;

    // 单线程池：环境段 git 子进程不占用工具执行用的全局池
    std::unique_ptr<QThreadPool> m_envPool;
    QString m_gitLine;        // 环境段 Git 行缓存
    QString m_gitLineWorkdir; // 缓存对应的 workdir（不等则视为未就绪）
    quint64 m_gitGeneration = 0;

    qint64 m_conversationId = 0;
    QString m_workdir;
    QString m_title;
    bool m_streaming = false;

    int m_overrideProviderIndex = -1;
    QString m_overrideModel;
};

} // namespace lens
