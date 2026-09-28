#include "ChatController.hpp"

#include "AppSettings.hpp"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>
#include <lens/core/attachments/Attachments.hpp>
#include <lens/core/context/AgentDocs.hpp>
#include <lens/core/context/EnvironmentPrompt.hpp>
#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/context/Skills.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

namespace lens {

ChatController::ChatController(SessionStore *store, AppSettings *settings, const QString &dataDir,
                               QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
    , m_dataDir(dataDir)
    , m_mcp(std::make_unique<McpManager>(&m_registry))
    , m_usage(std::make_unique<UsageTracker>(settings))
    , m_messageModel(new MessageListModel(this))
    , m_conversationModel(new ConversationListModel(store, this))
    , m_envPool(std::make_unique<QThreadPool>())
{
    m_envPool->setMaxThreadCount(1);
    registerBuiltinTools();
    applyToolSettings();
    // 用量/费用变化（含单价改动触发的重算）透传给 QML 的 usageSummary
    connect(m_usage.get(), &UsageTracker::changed, this, &ChatController::usageChanged);
    // MCP 连接在后台线程完成，结果到位后重建工具清单（内部会发 contextChanged）
    connect(m_mcp.get(), &McpManager::changed, this, [this] { rebuildToolList(); });
    QTimer::singleShot(0, this, [this] {
        // 事件循环启动后才连 MCP：窗口先出来，连接过程也不阻塞主线程
        m_mcp->requestReload(m_settings->mcpServerConfigs(), m_streaming);
        rebuildToolList();
    });

    m_agent = std::make_unique<AgentSession>(std::make_unique<QNetworkTransport>(),
                                             &m_registry, this);
    connectAgent();
}

ChatController::~ChatController()
{
    m_envPool->waitForDone(); // 等 git 子进程收尾，避免任务触到已析构的 this
}

void ChatController::registerBuiltinTools()
{
    const auto registerBuiltin = [this](std::shared_ptr<IBuiltinTool> tool) {
        m_builtinToolNames.append(tool->name());
        m_registry.registerTool(std::move(tool));
    };
    registerBuiltin(std::make_shared<ReadTool>());
    registerBuiltin(std::make_shared<WriteTool>());
    registerBuiltin(std::make_shared<EditTool>());
    registerBuiltin(std::make_shared<BashTool>());
}

// 预设语义（full/chat/read_only/custom）与换算规则在 core 的
// disabledToolsForPreset；MCP 工具不在内置名单里，不受预设影响
void ChatController::applyToolSettings()
{
    QStringList customTools;
    for (const QVariant &entry : m_settings->customTools())
        customTools.append(entry.toString());
    m_registry.setDisabledTools(disabledToolsForPreset(m_settings->toolPreset(),
                                                       m_builtinToolNames, customTools));
}

void ChatController::rebuildToolList()
{
    m_toolList.clear();
    for (const ToolSpec &spec : m_registry.specs()) {
        QString origin = QStringLiteral("内置");
        for (const auto &[toolName, toolOrigin] : m_mcp->toolOrigins()) {
            if (toolName == spec.name) {
                origin = toolOrigin;
                break;
            }
        }
        m_toolList.append(QVariantMap{{QStringLiteral("name"), spec.name},
                                      {QStringLiteral("description"), spec.description},
                                      {QStringLiteral("origin"), origin},
                                      {QStringLiteral("enabled"),
                                       m_registry.isEnabled(spec.name)}});
    }
    emit contextChanged();
}

void ChatController::connectAgent()
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
        m_mcp->applyPending(m_settings->mcpServerConfigs()); // 流式期间挂起的 MCP 重载
        refreshGitLine(); // 回合结束工作区可能变了，刷新环境段的 Git 行
    });
}

void ChatController::newConversation(const QString &workdir)
{
    createAndOpenConversation(workdir);
}

void ChatController::searchConversations(const QString &query)
{
    m_conversationModel->setFilter(query);
}

qint64 ChatController::createAndOpenConversation(const QString &workdir)
{
    // 与 store 的归一规则一致，折叠组展开键须对上组名
    const QString dir = workdir.trimmed().isEmpty() ? QDir::homePath() : workdir.trimmed();
    const qint64 id = m_store->createConversation(QStringLiteral("新会话"), dir);
    m_conversationModel->setFilter(QString()); // 新会话要在列表可见：清掉搜索过滤
    m_conversationModel->expandGroup(dir);     // 落在折叠分组里也要立即可见
    m_conversationModel->reload();
    openConversation(id);
    return id;
}

void ChatController::openConversation(qint64 conversationId)
{
    if (m_streaming)
        stop(); 
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

void ChatController::deleteConversation(qint64 conversationId)
{
    if (conversationId == m_conversationId && m_streaming)
        stop();
    m_store->deleteConversation(conversationId);
    m_conversationModel->reload();
    if (conversationId == m_conversationId) {
        m_conversationId = 0;
        m_title.clear();
        m_messageModel->resetFromMessages({});
        m_usage->reset();
        emit currentConversationChanged();
    }
}

void ChatController::refreshContext()
{
    applyToolSettings();
    // 环境段开启且该工作文件夹还没有 Git 行缓存（如刚重新开启注入）时补取
    if (m_settings->environmentPrompt() && m_gitLineWorkdir != m_workdir)
        refreshGitLine();
    m_mcp->requestReload(m_settings->mcpServerConfigs(), m_streaming);
    m_lastSections = collectSections();
    rebuildToolList();
    emit contextChanged();
}

void ChatController::send(const QString &text, const QString &workdir)
{
    send(text, workdir, {});
}

// 附件加载与分类规则在 core 的 attachments::loadAttachments，这里只做转发
ChatController::LoadedAttachments ChatController::loadAttachments(
    const QVariantList &attachments) const
{
    return attachments::loadAttachments(attachments);
}

void ChatController::send(const QString &text, const QString &workdir,
                          const QVariantList &attachments, const QString &thinkingLevel)
{
    if (m_streaming || text.trimmed().isEmpty())
        return;
    if (m_conversationId == 0)
        createAndOpenConversation(workdir); // 发送时无会话：自动创建

    const LoadedAttachments loaded = loadAttachments(attachments);

    Message userMessage;
    userMessage.role = Role::User;
    userMessage.content = text;
    userMessage.images = loaded.images;
    userMessage.files = loaded.files;
    m_store->appendMessage(m_conversationId, userMessage);

    if (m_title == QStringLiteral("新会话")) { // 首条消息作为会话标题
        m_title = text.simplified().left(24);
        m_store->renameConversation(m_conversationId, m_title);
        m_conversationModel->reload();
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

    const ProviderConfig provider = m_settings->activeProviderConfig();
    m_agent->setRequestConfig(provider.endpoint, provider.apiKey, provider.model);
    if (const auto protocol = protocolFromString(provider.protocol))
        m_agent->setProtocol(*protocol);
    m_agent->setServerSideSearch(provider.serverSearch);
    // 单模型元数据：输出上限进请求体，图片输入关闭时请求体剥离图片附件
    const ModelConfig modelConfig = modelConfigFor(provider, provider.model);
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

bool ChatController::clipboardHasImage() const
{
    return !QGuiApplication::clipboard()->image().isNull();
}

QString ChatController::clipboardImageDataUrl() const
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull())
        return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    ImageAttachment attachment{QStringLiteral("image/png"), png};
    return imageDataUrl(attachment);
}

// 模型选择的持久化下沉到 AppSettings（settings.json 的字段归它管），
// 这里只保留 QML 的调用入口
void ChatController::selectModel(int providerIndex, const QString &model)
{
    m_settings->selectActiveModel(providerIndex, model);
}

// 显示名解析从 QML 表达式收拢到这里（此前在 ChatView 两处重复）
QString ChatController::modelDisplayName(int providerIndex, const QString &modelId) const
{
    if (modelId.isEmpty())
        return {};
    const QVariantMap provider =
        m_settings->providers().value(providerIndex).toMap();
    const QVariantList models = provider.value(QStringLiteral("models")).toList();
    for (const QVariant &entry : models) {
        const QVariantMap model = entry.toMap();
        if (model.value(QStringLiteral("id")).toString() == modelId) {
            const QString displayName = model.value(QStringLiteral("displayName")).toString();
            return displayName.isEmpty() ? modelId : displayName;
        }
    }
    return modelId; // 清单为空或未收录：回退模型 id
}

// 参数取设置页当前表单值（未保存的修改也可拉取）；同一时间仅允许一次拉取，
// 结果原样经信号返回，“用户已切走”等过期判断由界面侧比对表单快照完成
void ChatController::fetchModels(const QString &protocol, const QString &endpoint,
                                 const QString &apiKey)
{
    const auto parsed = protocolFromString(protocol);
    if (!parsed || endpoint.trimmed().isEmpty() || m_fetchingModels)
        return;
    m_fetchingModels = true;
    emit fetchingModelsChanged();
    m_modelListClient.fetch(*parsed, endpoint.trimmed(), apiKey,
                            [this](QStringList models, QString error) {
                                m_fetchingModels = false;
                                emit fetchingModelsChanged();
                                if (error.isEmpty())
                                    emit modelsFetched(models);
                                else
                                    emit modelsFetchFailed(error);
                            });
}

void ChatController::stop()
{
    m_agent->cancel();
}

QVector<ContextSectionInfo> ChatController::collectSections() const
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
void ChatController::refreshGitLine()
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

QVariantList ChatController::contextSections() const
{
    QVariantList list;
    for (const ContextSectionInfo &section : m_lastSections) {
        list.append(QVariantMap{{QStringLiteral("name"), section.name},
                                {QStringLiteral("source"), section.source},
                                {QStringLiteral("content"), section.content}});
    }
    return list;
}

QVariantList ChatController::skillsList(const QString &workdir) const
{
    QVariantList list;
    const QString effectiveWorkdir =
        workdir.isEmpty() ? m_workdir : workdir;
    const QList<QPair<QString, QString>> dirs = {
        {m_dataDir + QStringLiteral("/skills"), QStringLiteral("global")},
        {effectiveWorkdir + QStringLiteral("/.lens/skills"), QStringLiteral("project")},
    };
    for (const auto &[dir, origin] : dirs) {
        for (const skills::Skill &skill : skills::discover(dir)) {
            list.append(QVariantMap{{QStringLiteral("name"), skill.name},
                                    {QStringLiteral("description"), skill.description},
                                    {QStringLiteral("path"), skill.path},
                                    {QStringLiteral("origin"), origin}});
        }
    }
    return list;
}

void ChatController::setErrorRow(const QString &text)
{
    MessageListModel::Item item;
    item.kind = MessageListModel::Error;
    item.text = text;
    m_messageModel->appendItem(item);
}

} // namespace lens
