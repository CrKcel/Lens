#pragma once

#include "AppSettings.hpp"
#include "lens/core/Conversation.hpp"

#include <QList>
#include <QObject>
#include <QVariantMap>

namespace lens {

// 会话用量与费用统计：最近一次上报的输入（= 当前上下文长度）+ 会话累计
// （输入/输出/缓存命中），费用按激活供应商的单价换算。属于设置与持久化之外的
// 一份纯计算状态，从 ChatController 拆出来独立演进
class UsageTracker : public QObject
{
    Q_OBJECT
public:
    explicit UsageTracker(AppSettings *settings, QObject *parent = nullptr);

    // 会话切换 / 清空 / 删除时归零
    void reset();
    // 会话载入：清空后累加整段历史，结束统一发一次 changed——即使没有任何有效
    // 用量也要通知，否则界面会留着上一会话的残留显示
    void loadFrom(const QList<Message> &history);
    // 一次回合结束时的用量累加（无效用量忽略）
    void record(const TokenUsage &usage);

    QVariantMap summary() const;

signals:
    void changed();

private:
    void accumulate(const TokenUsage &usage);
    // 费用/上下文窗口展示依赖的激活供应商数据（单价 + 当前模型）是否变化；
    // settingsChanged 是全域广播（改主题、语言也会发），此处过滤掉无关改动
    bool providerInputsChanged() const;
    void refreshProviderInputs();

    AppSettings *m_settings;
    double m_inputPrice = 0.0;
    double m_outputPrice = 0.0;
    double m_cachedPrice = 0.0;
    QString m_model;
    TokenUsage m_last; // 最近一次上报：其输入侧即当前上下文长度
    qint64 m_totalPrompt = 0;
    qint64 m_totalCompletion = 0;
    qint64 m_totalCached = 0;
    bool m_hasUsage = false;
};

} // namespace lens
