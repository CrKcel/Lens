#pragma once

#include "ChatEngine.hpp"
#include "ConversationListModel.hpp"
#include "MessageListModel.hpp"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace lens {

class ChatSession;

// 每窗口 facade：QML 只与此类交互（context property `chat`）。会话相关属性
// 转发到当前绑定的 ChatSession（无会话时空占位），会话列表/搜索/删除与工具、
// MCP、模型清单拉取转发到 ChatEngine（全应用共享一份）。会话切换只重绑视图
// 不中断生成：原会话的回合继续在后台进行，切回即恢复现场（后台生成）。
class ChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(qint64 currentConversationId READ currentConversationId NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentWorkdir READ currentWorkdir NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentConversationChanged)
    Q_PROPERTY(QAbstractListModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(QAbstractListModel *messages READ messages NOTIFY currentConversationChanged)
    Q_PROPERTY(QVariantList contextSections READ contextSections NOTIFY contextChanged)
    Q_PROPERTY(QVariantList contextTools READ contextTools NOTIFY contextChanged)
    Q_PROPERTY(QVariantList mcpStatus READ mcpStatus NOTIFY contextChanged)
    Q_PROPERTY(QVariantMap usageSummary READ usageSummary NOTIFY usageChanged)
    Q_PROPERTY(bool fetchingModels READ fetchingModels NOTIFY fetchingModelsChanged)
    // 生效模型（会话覆盖 ?: 全局激活供应商）——模型弹层与按钮展示用
    Q_PROPERTY(int currentProviderIndex READ currentProviderIndex NOTIFY modelSelectionChanged)
    Q_PROPERTY(QString currentModelId READ currentModelId NOTIFY modelSelectionChanged)
    Q_PROPERTY(bool modelOverridden READ modelOverridden NOTIFY modelSelectionChanged)

public:
    explicit ChatController(ChatEngine *engine, QObject *parent = nullptr);

    bool streaming() const { return m_session && m_session->streaming(); }
    qint64 currentConversationId() const
    {
        return m_session ? m_session->conversationId() : 0;
    }
    QString currentWorkdir() const { return m_session ? m_session->workdir() : QString(); }
    QString currentTitle() const { return m_session ? m_session->title() : QString(); }
    QAbstractListModel *conversations() const { return m_conversationModel; }
    QAbstractListModel *messages() const
    {
        return m_session ? m_session->messages() : m_emptyMessages;
    }
    QVariantList contextSections() const
    {
        return m_session ? m_session->contextSections() : QVariantList();
    }
    QVariantList contextTools() const { return m_engine->toolList(); }
    QVariantList mcpStatus() const { return m_engine->mcpStatus(); }
    // 无会话时返回同构的空用量（键齐全），QML 取 hasUsage 等键不会得到 undefined
    QVariantMap usageSummary() const;
    bool fetchingModels() const { return m_engine->fetchingModels(); }

    int currentProviderIndex() const;
    QString currentModelId() const;
    bool modelOverridden() const { return m_session && m_session->hasModelOverride(); }

    Q_INVOKABLE void newConversation(const QString &workdir);
    Q_INVOKABLE void openConversation(qint64 conversationId);
    Q_INVOKABLE void deleteConversation(qint64 conversationId);
    // 会话列表搜索：标题或消息内容命中（空串恢复全量）；列表模型每窗口独立
    Q_INVOKABLE void searchConversations(const QString &query);
    Q_INVOKABLE void send(const QString &text, const QString &workdir = QString());
    // 带附件的发送：attachments 每项为 {url, name, isImage}（兼容旧纯字符串路径/data URL）；
    // 图片与文本文件由嗅探分类（文本文件内容随消息发给模型）；
    // thinkingLevel 为会话内临时状态（disabled/low/medium/high/max），不持久化
    Q_INVOKABLE void send(const QString &text, const QString &workdir,
                          const QVariantList &attachments,
                          const QString &thinkingLevel = QStringLiteral("disabled"));
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshContext(); // 设置（MCP/工具/提示词）变化后全量重建上下文
    Q_INVOKABLE QVariantList skillsList(const QString &workdir) const;
    Q_INVOKABLE bool clipboardHasImage() const;      // 剪贴板是否携带图片（粘贴转附件）
    Q_INVOKABLE QString clipboardImageDataUrl() const; // 剪贴板图片转 data URL，无图片返回空
    // 从端点拉取可用模型清单。参数取设置页当前表单值（未保存的修改也可拉取）；
    // 结果经 modelsFetched / modelsFetchFailed 信号返回。
    Q_INVOKABLE void fetchModels(const QString &protocol, const QString &endpoint,
                                 const QString &apiKey);
    // 聊天区模型切换：写本窗口会话的模型覆盖（供应商 + 模型），下次发送生效；
    // 全局激活供应商不受影响。越界索引/空模型名忽略
    Q_INVOKABLE void selectModel(int providerIndex, const QString &model);
    Q_INVOKABLE void clearModelOverride(); // 清除覆盖，回退跟随全局
    // 模型显示名：providerIndex 对应供应商的 models 清单里按 id 找 displayName，
    // 未配置显示名、清单为空或未收录时回退模型 id（聊天按钮与模型弹层共用）
    Q_INVOKABLE QString modelDisplayName(int providerIndex, const QString &modelId) const;
    // 多窗口：新开一个窗口（可带会话）；本窗口不切换
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE void openConversationInNewWindow(qint64 conversationId);

signals:
    void streamingChanged();
    void currentConversationChanged();
    void contextChanged();
    void usageChanged();
    void fetchingModelsChanged();
    void modelSelectionChanged();
    void modelsFetched(const QStringList &models);
    void modelsFetchFailed(const QString &error);

private:
    void rebindSession(ChatSession *session);
    // 新会话在列表中立即可见：清过滤 + 展开所在分组 + 重载
    void revealNewConversation(const QString &workdir);

    ChatEngine *m_engine;
    QPointer<ChatSession> m_session;
    QVector<QMetaObject::Connection> m_sessionConnections;
    ConversationListModel *m_conversationModel;
    MessageListModel *m_emptyMessages; // 无会话时的占位显示模型
};

} // namespace lens
