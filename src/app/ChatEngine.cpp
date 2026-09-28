#include "ChatEngine.hpp"

#include "AppSettings.hpp"
#include "ChatController.hpp"

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

namespace lens {

ChatEngine::ChatEngine(SessionStore *store, AppSettings *settings, const QString &dataDir,
                       QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
    , m_dataDir(dataDir)
    , m_mcp(std::make_unique<McpManager>(&m_registry))
{
    registerBuiltinTools();
    applyToolSettings();
    // MCP 连接在后台线程完成，结果到位后重建工具清单（内部会发 contextChanged）
    connect(m_mcp.get(), &McpManager::changed, this, [this] { rebuildToolList(); });
    QTimer::singleShot(0, this, [this] {
        // 事件循环启动后才连 MCP：窗口先出来，连接过程也不阻塞主线程
        m_mcp->requestReload(m_settings->mcpServerConfigs(), anyStreaming());
        rebuildToolList();
    });
}

ChatEngine::~ChatEngine()
{
    stopAll();
}

void ChatEngine::registerBuiltinTools()
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
void ChatEngine::applyToolSettings()
{
    QStringList customTools;
    for (const QVariant &entry : m_settings->customTools())
        customTools.append(entry.toString());
    m_registry.setDisabledTools(disabledToolsForPreset(m_settings->toolPreset(),
                                                       m_builtinToolNames, customTools));
}

void ChatEngine::rebuildToolList()
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

ChatSession *ChatEngine::createSession(qint64 conversationId)
{
    auto transport = m_transportFactory ? m_transportFactory()
                                        : std::make_unique<QNetworkTransport>();
    auto *session = new ChatSession(m_store, &m_registry, m_settings, m_dataDir,
                                    std::move(transport), this);
    connect(session, &ChatSession::streamingChanged, this,
            [this, session] { onSessionStreaming(session->conversationId(),
                                                 session->streaming()); });
    connect(session, &ChatSession::conversationsDirty, this,
            &ChatEngine::notifyConversationsChanged);
    m_sessions.insert(conversationId, session);
    return session;
}

ChatSession *ChatEngine::sessionFor(qint64 conversationId)
{
    if (const auto it = m_sessions.constFind(conversationId); it != m_sessions.constEnd())
        return it.value();
    for (const Conversation &conversation : m_store->conversations()) {
        if (conversation.id == conversationId) {
            auto *session = createSession(conversationId);
            session->loadConversation(conversationId);
            return session;
        }
    }
    return nullptr; // 会话不存在（store 已删等）
}

ChatSession *ChatEngine::createConversation(const QString &workdir)
{
    // 与 store 的归一规则一致
    const QString dir = workdir.trimmed().isEmpty() ? QDir::homePath() : workdir.trimmed();
    const qint64 id = m_store->createConversation(QStringLiteral("新会话"), dir);
    auto *session = createSession(id);
    session->loadConversation(id);
    emit conversationsChanged();
    return session;
}

void ChatEngine::deleteConversation(qint64 conversationId)
{
    if (auto it = m_sessions.constFind(conversationId); it != m_sessions.constEnd()) {
        ChatSession *session = it.value();
        if (session->streaming())
            onSessionStreaming(conversationId, false);
        session->stop();
        m_sessions.erase(it);
        session->deleteLater();
    }
    m_store->deleteConversation(conversationId);
    emit sessionClosed(conversationId);
    emit conversationsChanged();
}

void ChatEngine::stopAll()
{
    for (ChatSession *session : std::as_const(m_sessions)) {
        if (session->streaming()) {
            onSessionStreaming(session->conversationId(), false);
            session->stop();
        }
    }
}

void ChatEngine::onSessionStreaming(qint64 conversationId, bool streaming)
{
    if (streaming)
        m_streamingIds.insert(conversationId);
    else
        m_streamingIds.remove(conversationId);
    // MCP 重载在流式期间挂起（工具线程可能正在遍历注册表），全部回合结束后补做
    if (!streaming && m_streamingIds.isEmpty() && m_mcp->hasPendingReload())
        m_mcp->applyPending(m_settings->mcpServerConfigs());
    emit sessionStreamingChanged(conversationId, streaming);
}

void ChatEngine::refreshContext()
{
    applyToolSettings();
    m_mcp->requestReload(m_settings->mcpServerConfigs(), anyStreaming());
    for (ChatSession *session : std::as_const(m_sessions))
        session->refreshContext();
    rebuildToolList();
}

void ChatEngine::fetchModels(const QString &protocol, const QString &endpoint,
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

void ChatEngine::createWindow(qint64 conversationId)
{
    if (!m_qmlEngine)
        return;
    auto *controller = new ChatController(this, this);
    auto *context = new QQmlContext(m_qmlEngine->rootContext(), controller);
    context->setContextProperty(QStringLiteral("chat"), controller);

    QQmlComponent component(m_qmlEngine, QUrl(QStringLiteral("qrc:/qt/qml/Lens/app/Main.qml")));
    QObject *window = component.create(context);
    if (!window) {
        qWarning() << "创建窗口失败:" << component.errorString();
        delete controller;
        return;
    }
    // 窗口对象与其上下文一起析构（上下文随窗口销毁，避免悬空的 chat 属性）
    context->setParent(window);
    window->setParent(controller);
    m_windows.append(controller);
    connect(window, &QObject::destroyed, controller, [this, controller] {
        m_windows.removeOne(controller);
        controller->deleteLater();
    });
    if (conversationId != 0)
        controller->openConversation(conversationId);
}

} // namespace lens
