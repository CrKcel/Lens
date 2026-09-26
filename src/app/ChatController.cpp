#include "ChatController.hpp"

#include "AppSettings.hpp"

#include <QDir>
#include <QTimer>
#include <lens/core/context/AgentDocs.hpp>
#include <lens/core/context/EnvironmentPrompt.hpp>
#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/context/Skills.hpp>
#include <lens/core/mcp/McpTool.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WebSearchTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

namespace lens {

inline const QString kBasePrompt = QStringLiteral(
    "You are a helpful assistant。用户工作文件夹是当前任务的根目录："
    "read/write/edit 工具的相对路径以它为基准；结束后简要说明做了什么、结果如何。"
    "回答使用与用户一致的语言。");

ChatController::ChatController(SessionStore *store, AppSettings *settings, const QString &dataDir,
                               QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
    , m_dataDir(dataDir)
    , m_messageModel(new MessageListModel(this))
    , m_conversationModel(new ConversationListModel(store, this))
{
    registerBuiltinTools();
    // 单价属于激活供应商配置，改动后费用展示需重算
    connect(m_settings, &AppSettings::settingsChanged, this, &ChatController::usageChanged);
    QTimer::singleShot(0, this, [this] {
        loadMcpTools();
        rebuildToolList();
    });

    m_agent = std::make_unique<AgentSession>(std::make_unique<QNetworkTransport>(),
                                             &m_registry, this);
    connectAgent();
}

void ChatController::registerBuiltinTools()
{
    m_registry.registerTool(std::make_shared<ReadTool>());
    m_registry.registerTool(std::make_shared<WriteTool>());
    m_registry.registerTool(std::make_shared<EditTool>());
    m_registry.registerTool(std::make_shared<BashTool>());
    const auto search = std::make_shared<WebSearchTool>();
    search->setConfig(m_settings->webSearchEndpoint(), m_settings->webSearchApiKey());
    m_registry.registerTool(search);
}

void ChatController::loadMcpTools()
{
    for (const McpServerConfig &config : m_settings->mcpServerConfigs()) {
        auto client = std::make_shared<mcp::McpClient>(mcp::ServerConfig{
            config.name, config.command, config.args});
        QString error;
        QVariantMap status{{QStringLiteral("name"), config.name},
                           {QStringLiteral("command"), config.command}};
        if (client->start(&error)) {
            const auto tools = client->listTools(&error);
            if (error.isEmpty()) {
                QStringList toolNames;
                for (const auto &info : tools) {
                    const QString prefixed =
                        QStringLiteral("mcp_%1_%2").arg(config.name, info.name);
                    m_registry.registerTool(
                        std::make_shared<mcp::McpTool>(config.name, info, client));
                    m_mcpToolOrigins.append(
                        {prefixed, QStringLiteral("MCP:%1").arg(config.name)});
                    toolNames.append(prefixed);
                }
                status.insert(QStringLiteral("connected"), true);
                status.insert(QStringLiteral("toolNames"), toolNames);
                status.insert(QStringLiteral("status"),
                              QStringLiteral("已连接，%1 个工具").arg(toolNames.size()));
                m_mcpClients.append(client);
            } else {
                status.insert(QStringLiteral("connected"), false);
                status.insert(QStringLiteral("status"), error);
            }
        } else {
            status.insert(QStringLiteral("connected"), false);
            status.insert(QStringLiteral("status"), error);
        }
        m_mcpStatus.append(status);
    }
}

void ChatController::rebuildToolList()
{
    m_toolList.clear();
    for (const ToolSpec &spec : m_registry.specs()) {
        QString origin = QStringLiteral("内置");
        for (const auto &[toolName, toolOrigin] : m_mcpToolOrigins) {
            if (toolName == spec.name) {
                origin = toolOrigin;
                break;
            }
        }
        m_toolList.append(QVariantMap{{QStringLiteral("name"), spec.name},
                                      {QStringLiteral("description"), spec.description},
                                      {QStringLiteral("origin"), origin}});
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
                recordUsage(message.usage);
            });
    connect(m_agent.get(), &AgentSession::toolCallStarted, this,
            [this](const QString &id, const QString &name, const QString &args) {
                Q_UNUSED(name);
                Q_UNUSED(args);
                m_messageModel->setToolCallRunning(id);
            });
    connect(m_agent.get(), &AgentSession::toolCallFinished, this,
            [this](const QString &id, const QString &output) {
                m_messageModel->setToolCallResult(id, output);
                Message toolMessage;
                toolMessage.role = Role::Tool;
                toolMessage.content = output;
                toolMessage.toolCallId = id;
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
    });
}

void ChatController::newConversation(const QString &workdir)
{
    createAndOpenConversation(workdir);
}

qint64 ChatController::createAndOpenConversation(const QString &workdir)
{
    const qint64 id = m_store->createConversation(
        QStringLiteral("新会话"),
        workdir.trimmed().isEmpty() ? QDir::homePath() : workdir.trimmed());
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
        resetUsage();
        for (const Message &message : history)
            recordUsage(message.usage);
        // 全部无效时 recordUsage 不会发信号，仍需通知 UI 清掉上一会话的残留显示
        emit usageChanged();
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
        resetUsage();
        emit usageChanged();
        emit currentConversationChanged();
    }
}

void ChatController::refreshContext()
{
    m_lastSections = collectSections();
    rebuildToolList();
    emit contextChanged();
}

void ChatController::send(const QString &text, const QString &workdir)
{
    if (m_streaming || text.trimmed().isEmpty())
        return;
    if (m_conversationId == 0)
        createAndOpenConversation(workdir); // 发送时无会话：自动创建

    Message userMessage;
    userMessage.role = Role::User;
    userMessage.content = text;
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
    m_messageModel->appendItem(item);

    const ProviderConfig provider = m_settings->activeProviderConfig();
    m_agent->setRequestConfig(provider.endpoint, provider.apiKey, provider.model);
    if (const auto protocol = protocolFromString(provider.protocol))
        m_agent->setProtocol(*protocol);
    m_agent->setServerSideSearch(provider.serverSearch);
    m_lastSections = collectSections();
    PromptAssembler assembler;
    for (const ContextSectionInfo &section : m_lastSections)
        assembler.setSection(section.name, section.content);
    m_agent->setSystemPrompt(assembler.assemble());
    m_agent->setWorkdir(m_workdir);
    m_agent->sendUserMessage(text);

    m_streaming = true;
    emit streamingChanged();
    emit contextChanged();
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

    add(QStringLiteral("identity"), QStringLiteral("内置"), kBasePrompt);
    add(QStringLiteral("workspace"), QStringLiteral("会话设置"),
        QStringLiteral("当前工作文件夹：%1").arg(m_workdir));
    add(QStringLiteral("environment"), QStringLiteral("自动生成"),
        envprompt::build(m_workdir));

    for (const agentdocs::AgentDoc &doc :
         agentdocs::discover(m_workdir, m_dataDir)) {
        add(doc.scope == QLatin1String("global") ? QStringLiteral("agent_doc_global")
                                                 : QStringLiteral("agent_doc_project"),
            doc.path, doc.content);
    }

    QStringList skillLines;
    for (const QString &dir :
         {m_dataDir + QStringLiteral("/skills"), m_workdir + QStringLiteral("/.lens/skills")}) {
        for (const skills::Skill &skill : skills::discover(dir)) {
            skillLines.append(QStringLiteral("- %1：%2（%3）")
                                  .arg(skill.name, skill.description.isEmpty()
                                                            ? QStringLiteral("（无描述）")
                                                            : skill.description,
                                       skill.path));
        }
    }
    if (!skillLines.isEmpty()) {
        add(QStringLiteral("skills"), QStringLiteral("自动发现"),
            QStringLiteral("以下技能可用，需要时先用 read 工具读取对应 SKILL.md 了解具体做法：\n")
                + skillLines.join(QLatin1Char('\n')));
    }

    if (!m_settings->systemPrompt().trimmed().isEmpty())
        add(QStringLiteral("custom"), QStringLiteral("用户设置"), m_settings->systemPrompt());
    return sections;
}

void ChatController::resetUsage()
{
    m_lastUsage = TokenUsage();
    m_totalPrompt = 0;
    m_totalCompletion = 0;
    m_totalCached = 0;
    m_hasUsage = false;
}

void ChatController::recordUsage(const TokenUsage &usage)
{
    if (!usage.valid)
        return;
    m_lastUsage = usage;
    m_totalPrompt += usage.promptTokens;
    m_totalCompletion += usage.completionTokens;
    m_totalCached += usage.cachedTokens;
    m_hasUsage = true;
    emit usageChanged();
}

QVariantMap ChatController::usageSummary() const
{
    // 费用（每百万 token）：非缓存输入×输入单价 + 输出×输出单价 + 缓存命中×缓存单价，
    // 缓存单价未配置（0）时缓存部分按输入单价计。缓存命中是输入的子集，需先扣除
    const ProviderConfig provider = m_settings->activeProviderConfig();
    const bool hasCost =
        provider.inputPrice > 0.0 || provider.outputPrice > 0.0 || provider.cachedPrice > 0.0;
    const double cachedPrice =
        provider.cachedPrice > 0.0 ? provider.cachedPrice : provider.inputPrice;
    const double cost =
        hasCost ? qMax<qint64>(0, m_totalPrompt - m_totalCached) / 1e6 * provider.inputPrice
                      + m_totalCompletion / 1e6 * provider.outputPrice
                      + m_totalCached / 1e6 * cachedPrice
                : 0.0;
    return {{QStringLiteral("hasUsage"), m_hasUsage},
            {QStringLiteral("contextTokens"), m_lastUsage.promptTokens},
            {QStringLiteral("totalPrompt"), m_totalPrompt},
            {QStringLiteral("totalCompletion"), m_totalCompletion},
            {QStringLiteral("totalCached"), m_totalCached},
            {QStringLiteral("hasCost"), hasCost},
            {QStringLiteral("cost"), cost}};
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
