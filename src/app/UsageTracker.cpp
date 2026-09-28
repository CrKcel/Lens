#include "UsageTracker.hpp"

namespace lens {

UsageTracker::UsageTracker(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    refreshProviderInputs();
    // 设置改动全域广播：只有激活供应商的单价/模型（影响费用与上下文窗口展示）
    // 真的变了才重发 changed，改主题、语言等不触发用量条刷新
    connect(m_settings, &AppSettings::settingsChanged, this, [this] {
        if (providerInputsChanged()) {
            refreshProviderInputs();
            emit changed();
        }
    });
}

void UsageTracker::setProviderOverride(int providerIndex, const QString &modelId)
{
    if (m_overrideIndex == providerIndex && m_overrideModel == modelId)
        return;
    m_overrideIndex = providerIndex;
    m_overrideModel = modelId;
    if (providerInputsChanged()) {
        refreshProviderInputs();
        emit changed();
    }
}

ProviderConfig UsageTracker::effectiveProvider() const
{
    if (m_overrideIndex < 0)
        return m_settings->activeProviderConfig();
    ProviderConfig provider = m_settings->providerConfigAt(m_overrideIndex);
    if (!m_overrideModel.isEmpty())
        provider.model = m_overrideModel;
    return provider;
}

bool UsageTracker::providerInputsChanged() const
{
    const ProviderConfig provider = effectiveProvider();
    return provider.inputPrice != m_inputPrice || provider.outputPrice != m_outputPrice
        || provider.cachedPrice != m_cachedPrice || provider.model != m_model;
}

void UsageTracker::refreshProviderInputs()
{
    const ProviderConfig provider = effectiveProvider();
    m_inputPrice = provider.inputPrice;
    m_outputPrice = provider.outputPrice;
    m_cachedPrice = provider.cachedPrice;
    m_model = provider.model;
}

void UsageTracker::reset()
{
    m_last = TokenUsage();
    m_totalPrompt = 0;
    m_totalCompletion = 0;
    m_totalCached = 0;
    m_hasUsage = false;
    emit changed();
}

void UsageTracker::loadFrom(const QList<Message> &history)
{
    m_last = TokenUsage();
    m_totalPrompt = 0;
    m_totalCompletion = 0;
    m_totalCached = 0;
    m_hasUsage = false;
    for (const Message &message : history)
        accumulate(message.usage);
    emit changed();
}

void UsageTracker::record(const TokenUsage &usage)
{
    if (!usage.valid)
        return;
    accumulate(usage);
    emit changed();
}

void UsageTracker::accumulate(const TokenUsage &usage)
{
    if (!usage.valid)
        return;
    m_last = usage;
    m_totalPrompt += usage.promptTokens;
    m_totalCompletion += usage.completionTokens;
    m_totalCached += usage.cachedTokens;
    m_hasUsage = true;
}

QVariantMap UsageTracker::summary() const
{
    // 费用（每百万 token）：非缓存输入×输入单价 + 输出×输出单价 + 缓存命中×缓存单价，
    // 缓存单价未配置（0）时缓存部分按输入单价计。缓存命中是输入的子集，需先扣除
    const ProviderConfig provider = effectiveProvider();
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
            {QStringLiteral("contextTokens"), m_last.promptTokens},
            {QStringLiteral("contextWindow"),
             modelConfigFor(provider, provider.model).contextWindow},
            {QStringLiteral("totalPrompt"), m_totalPrompt},
            {QStringLiteral("totalCompletion"), m_totalCompletion},
            {QStringLiteral("totalCached"), m_totalCached},
            {QStringLiteral("hasCost"), hasCost},
            {QStringLiteral("cost"), cost}};
}

} // namespace lens
