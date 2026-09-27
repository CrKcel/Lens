#include "UsageTracker.hpp"

namespace lens {

UsageTracker::UsageTracker(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // 单价属于激活供应商配置，设置改动后费用展示需重算
    connect(m_settings, &AppSettings::settingsChanged, this, &UsageTracker::changed);
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
