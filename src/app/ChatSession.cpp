#include "ChatSession.hpp"

#include "AppSettings.hpp"

#include <QThreadPool>
#include <lens/core/context/AgentDocs.hpp>
#include <lens/core/context/EnvironmentPrompt.hpp>
#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/context/Skills.hpp>
#include <lens/core/storage/SessionStore.hpp>

namespace lens {

ChatSession::ChatSession(SessionStore *store, ToolRegistry *registry, AppSettings *settings,
                         const QString &dataDir, std::unique_ptr<ITransport> transport,
                         QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
    , m_dataDir(dataDir)
    , m_usage(std::make_unique<UsageTracker>(settings))
    , m_messageModel(new MessageListModel(this))
    , m_envPool(std::make_unique<QThreadPool>())
{
    m_envPool->setMaxThreadCount(1);
    // 用量/费用变化（含单价改动触发的重算）透传给 QML 的 usageSummary
    connect(m_usage.get(), &UsageTracker::changed, this, &ChatSession::usageChanged);

    m_agent = std::make_unique<AgentSession>(std::move(transport), registry, this);
    connectAgent();
}

ChatSession::~ChatSession()
{
    m_envPool->waitForDone(); // 等 git 子进程收尾，避免任务触到已析构的 this
}

QVariantList ChatSession::contextSections() const
{
    QVariantList list;
    for (const ContextSectionInfo &section : m_lastSections) {
        list.append(QVariantMap{{QStringLiteral("name"), section.name},
                                {QStringLiteral("source"), section.source},
                                {QStringLiteral("content"), section.content}});
    }
    return list;
}

void ChatSession::loadConversation(qint64 conversationId)
{
    if (conversationId == m_conversationId)
        return;
    for (const Conversation &conversation : m_store->conversations()) {
        if (conversation.id != conversationId)
            continue;
        m_conversationId = conversation.id;
        m_workdir = conversation.workdir;
        m_title = conversation.title;
        const QList<Message> history = m_store->messages(conversation.id);
        m_messageModel->resetFromMessages(history);
        m_agent->setHistory(std::vector<Message>(history.cbegin(), history.cend()));
        m_agent->setWorkdir(m_workdir);
        m_usage->loadFrom(history); // 会话累计与费用从持久化消息重算
        refreshGitLine();           // 环境段 Git 行随工作文件夹重取（后台）
        m_lastSections = collectSections();
        emit currentConversationChanged();
        emit contextChanged();
        return;
    }
}

void ChatSession::setModelOverride(int providerIndex, const QString &modelId)
{
    if (providerIndex < 0 || modelId.isEmpty())
        return;
    m_overrideProviderIndex = providerIndex;
    m_overrideModel = modelId;
    m_usage->setProviderOverride(providerIndex, modelId);
}

void ChatSession::clearModelOverride()
{
    if (!hasModelOverride())
        return;
    m_overrideProviderIndex = -1;
    m_overrideModel.clear();
    m_usage->clearProviderOverride();
}

void ChatSession::connectAgent()
{
    connect(m_agent.get(), &AgentSession::assistantDelta, this, [this](const QString &delta) {
        if (!m_messageModel->hasStreamingRow()) {
            MessageListModel::Item item;
            item.kind = MessageListModel::Assistant;
            item.streaming = true;
            m_messageModel->appendItem(item);
        }
        m_messageModel->appendDelta(delta);
    });
    connect(m_agent.get(), &AgentSession::reasoningDelta, this, [this](const QString &delta) {
        if (!m_messageModel->hasStreamingRow()) {
            MessageListModel::Item item;
            item.kind = MessageListModel::Assistant;
            item.streaming = true;
            m_messageModel->appendItem(item);
        }
        m_messageModel->appendReasoningDelta(delta);
    });
    connect(m_agent.get(), &AgentSession::assistantCompleted, this,
            [this](const Message &message) {
                m_messageModel->finishStreamingRow(message.content, message.reasoning);
                m_messageModel->dropEmptyStreamingRow();
                for (const ToolCall &call : message.toolCalls) {
                    MessageListModel::Item item;
                    item.kind = MessageListModel::ToolCallItem;
                    item.toolName = call.name;
                    item.toolArgs = call.arguments;
                    item.toolCallId = call.id;
                    item.toolPending = true; // 紧随其后的 toolCallFinished 回填结果
                    m_messageModel->appendItem(item);
                }
                m_store->appendMessage(m_conversationId, message);
                m_usage->record(message.usage);
            });
    connect(m_agent.get(), &AgentSession::toolCallStarted, this,
            [this](const QString &id, const QString &name, const QString &args) {
                Q_UNUSED(name);
                Q_UNUSED(args);
                m_messageModel->setToolCallRunning(id);
            });
    connect(m_agent.get(), &AgentSession::toolCallFinished, this,
            [this](const QString &id, const QString &output,
                   const QList<ImageAttachment> &images) {
                m_messageModel->setToolCallResult(id, output, images);
                Message toolMessage;
                toolMessage.role = Role::Tool;
                toolMessage.content = output;
                toolMessage.toolCallId = id;
                toolMessage.images = images;
                m_store->appendMessage(m_conversationId, toolMessage);
            });
    connect(m_agent.get(), &AgentSession::failed, this, [this](const QString &message) {
        m_messageModel->dropEmptyStreamingRow();
        setErrorRow(message);
    });
    connect(m_agent.get(), &AgentSession::idle, this, [this] {
        if (m_streaming) {
            m_streaming = false;
            emit streamingChanged();
        }
        refreshGitLine(); // 回合结束工作区可能变了，刷新环境段的 Git 行
    });
}

// 生效的供应商配置：模型覆盖优先，否则全局激活供应商
ProviderConfig ChatSession::effectiveProvider() const
{
    if (!hasModelOverride())
        return m_settings->activeProviderConfig();
    ProviderConfig provider = m_settings->providerConfigAt(m_overrideProviderIndex);
    if (!provider.name.isEmpty() || !provider.endpoint.isEmpty())
        provider.model = m_overrideModel;
    return provider;
}

QString ChatSession::effectiveModel() const
{
    return hasModelOverride() ? m_overrideModel : m_settings->activeProviderConfig().model;
}

void ChatSession::send(const QString &text, const QVariantList &attachments,
                       const QString &thinkingLevel)
{
    if (m_streaming || text.trimmed().isEmpty() || m_conversationId == 0)
        return;

    const LoadedAttachments loaded = attachments::loadAttachments(attachments);

    Message userMessage;
    userMessage.role = Role::User;
    userMessage.content = text;
    userMessage.images = loaded.images;
    userMessage.files = loaded.files;
    m_store->appendMessage(m_conversationId, userMessage);

    if (m_title == QStringLiteral("新会话")) { // 首条消息作为会话标题
        m_title = text.simplified().left(24);
        m_store->renameConversation(m_conversationId, m_title);
        emit conversationsDirty();
        emit currentConversationChanged();
    }

    MessageListModel::Item item;
    item.kind = MessageListModel::User;
    item.text = text;
    for (const ImageAttachment &image : loaded.images)
        item.images.append(imageDataUrl(image));
    for (const TextAttachment &file : loaded.files)
        item.files.append(QVariantMap{{QStringLiteral("name"), file.fileName}});
    m_messageModel->appendItem(item);

    const ProviderConfig provider = effectiveProvider();
    m_agent->setRequestConfig(provider.endpoint, provider.apiKey, effectiveModel());
    if (const auto protocol = protocolFromString(provider.protocol))
        m_agent->setProtocol(*protocol);
    m_agent->setServerSideSearch(provider.serverSearch);
    // 单模型元数据：输出上限进请求体，图片输入关闭时请求体剥离图片附件
    const ModelConfig modelConfig = modelConfigFor(provider, effectiveModel());
    m_agent->setMaxOutputTokens(modelConfig.maxOutputTokens);
    m_agent->setImagesEnabled(modelConfig.images);
    // 思考强度是聊天区会话内临时状态：每次发送随消息带入，非法值回退关闭
    ThinkingLevel thinking = ThinkingLevel::Disabled;
    if (const auto parsed = thinkingLevelFromString(thinkingLevel))
        thinking = *parsed;
    m_agent->setThinkingLevel(thinking);
    m_lastSections = collectSections();
    PromptAssembler assembler;
    for (const ContextSectionInfo &section : m_lastSections)
        assembler.setSection(section.name, section.content);
    m_agent->setSystemPrompt(assembler.assemble());
    m_agent->setWorkdir(m_workdir);
    m_agent->sendUserMessage(text, loaded.images, loaded.files);

    m_streaming = true;
    emit streamingChanged();
    emit contextChanged();
}

void ChatSession::refreshContext()
{
    // 环境段开启且该工作文件夹还没有 Git 行缓存（如刚重新开启注入）时补取
    if (m_settings->environmentPrompt() && m_gitLineWorkdir != m_workdir)
        refreshGitLine();
    m_lastSections = collectSections();
    emit contextChanged();
}

QVector<ContextSectionInfo> ChatSession::collectSections() const
{
    QVector<ContextSectionInfo> sections;
    auto add = [&sections](const QString &name, const QString &source, const QString &content) {
        sections.append({name, source, content});
    };

    // 用户自定义提示词即 identity 段：未设置时不注入任何身份提示词
    const QString customPrompt = m_settings->systemPrompt().trimmed();
    if (!customPrompt.isEmpty())
        add(QStringLiteral("identity"), QStringLiteral("用户设置"), customPrompt);
    // 环境段：设置可关；Git 行来自后台缓存（未就绪则省略该行，不阻塞主线程）
    if (m_settings->environmentPrompt())
        add(QStringLiteral("environment"), QStringLiteral("自动生成"),
            envprompt::build(m_workdir,
                             m_gitLineWorkdir == m_workdir ? m_gitLine : QString()));

    for (const agentdocs::AgentDoc &doc :
         agentdocs::discover(m_workdir, m_dataDir)) {
        add(doc.scope == QLatin1String("global") ? QStringLiteral("agent_doc_global")
                                                 : QStringLiteral("agent_doc_project"),
            doc.path, doc.content);
    }

    // 技能段正文由 core 的 skills::promptSection 拼装，无技能时该段不注入
    if (const QString skillsSection = skills::promptSection(
             {m_dataDir + QStringLiteral("/skills"),
              m_workdir + QStringLiteral("/.lens/skills")});
        !skillsSection.isEmpty()) {
        add(QStringLiteral("skills"), QStringLiteral("自动发现"), skillsSection);
    }

    return sections;
}

// 环境段的 Git 行异步取得：git 子进程最长数秒，主线程只读缓存，取到后刷新
// 上下文清单（检查器随之更新，下一次发送的提示词也带上该行）
void ChatSession::refreshGitLine()
{
    if (!m_settings->environmentPrompt()) // 环境段关闭时不取 Git 状态
        return;
    const QString workdir = m_workdir.trimmed();
    if (workdir.isEmpty())
        return;
    const quint64 generation = ++m_gitGeneration;
    m_envPool->start([this, workdir, generation] {
        const QString line = envprompt::gitSummary(workdir);
        QMetaObject::invokeMethod(
            this,
            [this, workdir, generation, line] {
                if (generation != m_gitGeneration) // 期间又换了工作文件夹，丢弃
                    return;
                m_gitLine = line;
                m_gitLineWorkdir = workdir;
                m_lastSections = collectSections();
                emit contextChanged();
            },
            Qt::QueuedConnection);
    });
}

void ChatSession::setErrorRow(const QString &text)
{
    MessageListModel::Item item;
    item.kind = MessageListModel::Error;
    item.text = text;
    m_messageModel->appendItem(item);
}

} // namespace lens
