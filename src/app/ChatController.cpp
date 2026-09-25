#include "ChatController.hpp"

#include "AppSettings.hpp"

#include <QDir>
#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

namespace lens {

inline const QString kBasePrompt = QStringLiteral(
    "你是 Lens，一个轻量的编程 AI Agent。用户工作文件夹是当前任务的根目录："
    "read/write/edit 工具的相对路径以它为基准。完成任务时优先使用工具读写文件、"
    "执行命令来获取真实状态，而不是凭空假设；结束后简要说明做了什么、结果如何。"
    "回答使用与用户一致的语言。");

ChatController::ChatController(SessionStore *store, AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
    , m_messageModel(new MessageListModel(this))
    , m_conversationModel(new ConversationListModel(store, this))
{
    m_registry.registerTool(std::make_shared<ReadTool>());
    m_registry.registerTool(std::make_shared<WriteTool>());
    m_registry.registerTool(std::make_shared<EditTool>());
    m_registry.registerTool(std::make_shared<BashTool>());

    m_agent = std::make_unique<AgentSession>(std::make_unique<QNetworkTransport>(),
                                             &m_registry, this);
    connectAgent();
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
        emit currentConversationChanged();
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
        emit currentConversationChanged();
    }
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

    m_agent->setRequestConfig(m_settings->endpoint(), m_settings->apiKey(), m_settings->model());
    m_agent->setSystemPrompt(assembleSystemPrompt());
    m_agent->setWorkdir(m_workdir);
    m_agent->sendUserMessage(text);

    m_streaming = true;
    emit streamingChanged();
}

void ChatController::stop()
{
    m_agent->cancel();
}

QString ChatController::assembleSystemPrompt() const
{
    PromptAssembler assembler;
    assembler.setSection(QStringLiteral("identity"), kBasePrompt);
    assembler.setSection(QStringLiteral("workspace"),
                         QStringLiteral("当前工作文件夹：%1").arg(m_workdir));
    if (!m_settings->systemPrompt().trimmed().isEmpty())
        assembler.setSection(QStringLiteral("custom"), m_settings->systemPrompt());
    return assembler.assemble();
}

void ChatController::setErrorRow(const QString &text)
{
    MessageListModel::Item item;
    item.kind = MessageListModel::Error;
    item.text = text;
    m_messageModel->appendItem(item);
}

} // namespace lens
