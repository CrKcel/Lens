#include "ChatController.hpp"

#include "ChatEngine.hpp"
#include "ChatSession.hpp"
#include "UsageTracker.hpp"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <lens/core/context/Skills.hpp>

namespace lens {

ChatController::ChatController(ChatEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_conversationModel(new ConversationListModel(engine->store(), this))
    , m_emptyMessages(new MessageListModel(this))
{
    // 引擎侧广播 → 本窗口视图：会话列表重载、生成中角标、上下文/工具清单
    connect(m_engine, &ChatEngine::conversationsChanged, this,
            [this] { m_conversationModel->reload(); });
    connect(m_engine, &ChatEngine::sessionStreamingChanged, this,
            [this](qint64 conversationId, bool streaming) {
                m_conversationModel->setStreaming(conversationId, streaming);
            });
    connect(m_engine, &ChatEngine::sessionClosed, this,
            [this](qint64 conversationId) {
                if (m_session && m_session->conversationId() == conversationId)
                    rebindSession(nullptr);
                m_conversationModel->setStreaming(conversationId, false);
            });
    connect(m_engine, &ChatEngine::contextChanged, this, &ChatController::contextChanged);
    connect(m_engine, &ChatEngine::fetchingModelsChanged, this,
            &ChatController::fetchingModelsChanged);
    connect(m_engine, &ChatEngine::modelsFetched, this, &ChatController::modelsFetched);
    connect(m_engine, &ChatEngine::modelsFetchFailed, this, &ChatController::modelsFetchFailed);
    // 全局激活供应商/模型变化影响“跟随全局”的生效模型展示
    connect(m_engine->settings(), &AppSettings::settingsChanged, this,
            &ChatController::modelSelectionChanged);
    // 引擎里可能已有后台生成中的会话（本窗口创建于生成中途），同步角标
    for (qint64 id : m_engine->streamingIds())
        m_conversationModel->setStreaming(id, true);
}

void ChatController::rebindSession(ChatSession *session)
{
    if (m_session == session)
        return;
    for (const QMetaObject::Connection &connection : std::as_const(m_sessionConnections))
        disconnect(connection);
    m_sessionConnections.clear();
    m_session = session;
    if (m_session) {
        m_sessionConnections = {
            connect(m_session, &ChatSession::streamingChanged, this,
                    &ChatController::streamingChanged),
            connect(m_session, &ChatSession::retryNoticeChanged, this,
                    &ChatController::retryNoticeChanged),
            connect(m_session, &ChatSession::currentConversationChanged, this,
                    &ChatController::currentConversationChanged),
            connect(m_session, &ChatSession::contextChanged, this,
                    &ChatController::contextChanged),
            connect(m_session, &ChatSession::usageChanged, this, &ChatController::usageChanged),
            connect(m_session, &ChatSession::conversationsDirty, this,
                    [this] { m_engine->notifyConversationsChanged(); }),
        };
    }
    // 属性族全部重读（messages 模型指针、会话游标、上下文、用量、流式态）
    emit currentConversationChanged();
    emit contextChanged();
    emit usageChanged();
    emit streamingChanged();
    emit retryNoticeChanged();
    emit modelSelectionChanged();
}

// 无会话（窗口刚开、会话被删）时也要返回键齐全的空用量：
// QML 直接取 hasUsage / contextTokens 等键，缺键会得到 undefined
QVariantMap ChatController::usageSummary() const
{
    return m_session ? m_session->usageSummary() : UsageTracker::emptySummary();
}

void ChatController::revealNewConversation(const QString &workdir)
{
    const QString dir = workdir.trimmed().isEmpty() ? QDir::homePath() : workdir.trimmed();
    m_conversationModel->setFilter(QString()); // 新会话要在列表可见：清掉搜索过滤
    m_conversationModel->expandGroup(dir);     // 落在折叠分组里也要立即可见
    m_conversationModel->reload();
}

void ChatController::newConversation(const QString &workdir)
{
    rebindSession(m_engine->createConversation(workdir));
    revealNewConversation(workdir);
}

void ChatController::openConversation(qint64 conversationId)
{
    // 只重绑不 stop：原会话生成中的回合继续后台进行
    if (ChatSession *session = m_engine->sessionFor(conversationId))
        rebindSession(session);
}

void ChatController::deleteConversation(qint64 conversationId)
{
    // 引擎停止并销毁会话；sessionClosed 广播里本窗口已绑定时自动重绑空视图
    m_engine->deleteConversation(conversationId);
}

void ChatController::searchConversations(const QString &query)
{
    m_conversationModel->setFilter(query);
}

void ChatController::send(const QString &text, const QString &workdir,
                          const QVariantList &attachments, const QString &thinkingLevel)
{
    // 无会话时自动创建（工作文件夹取侧栏输入）；会话已在生成中则忽略
    if (!m_session || m_session->conversationId() == 0) {
        if (m_session && m_session->streaming())
            return;
        rebindSession(m_engine->createConversation(workdir));
        revealNewConversation(workdir);
    }
    m_session->send(text, attachments, thinkingLevel);
}

void ChatController::stop()
{
    if (m_session)
        m_session->stop();
}

void ChatController::refreshContext()
{
    // 全量重建：工具禁用集合 + MCP 重载 + 各会话（含后台）提示词分节
    m_engine->refreshContext();
}

QVariantList ChatController::skillsList(const QString &workdir) const
{
    QVariantList list;
    const QString effectiveWorkdir =
        workdir.isEmpty() && m_session ? m_session->workdir() : workdir;
    const QList<QPair<QString, QString>> dirs = {
        {m_engine->dataDir() + QStringLiteral("/skills"), QStringLiteral("global")},
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

void ChatController::fetchModels(const QString &protocol, const QString &endpoint,
                                 const QString &apiKey)
{
    m_engine->fetchModels(protocol, endpoint, apiKey);
}

int ChatController::currentProviderIndex() const
{
    if (m_session && m_session->hasModelOverride())
        return m_session->overrideProviderIndex();
    return m_engine->settings()->activeProvider();
}

QString ChatController::currentModelId() const
{
    if (m_session && m_session->hasModelOverride())
        return m_session->overrideModel();
    return m_engine->settings()->model();
}

// 覆盖的持久化归会话（内存态，不写 settings.json）：切换只影响本窗口的后续发送
void ChatController::selectModel(int providerIndex, const QString &model)
{
    if (!m_session)
        return;
    m_session->setModelOverride(providerIndex, model);
    emit modelSelectionChanged();
}

void ChatController::clearModelOverride()
{
    if (!m_session)
        return;
    m_session->clearModelOverride();
    emit modelSelectionChanged();
}

// 显示名解析从 QML 表达式收拢到这里（此前在 ChatView 两处重复）
QString ChatController::modelDisplayName(int providerIndex, const QString &modelId) const
{
    if (modelId.isEmpty())
        return {};
    const ProviderConfig config = m_engine->settings()->providerConfigAt(providerIndex);
    const QString displayName = modelConfigFor(config, modelId).displayName;
    return displayName.isEmpty() ? modelId : displayName;
}

} // namespace lens
