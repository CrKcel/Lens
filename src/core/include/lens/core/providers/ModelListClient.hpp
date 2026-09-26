#pragma once

#include "lens/core/providers/ProtocolAdapter.hpp"

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

#include <functional>

namespace lens {

// 从供应商端点拉取可用模型清单（GET /models），供设置页自动补全模型列表。
// 纯异步实现（QNetworkAccessManager），可在主线程使用，回调经事件循环触发。
// OpenAI 系与 Anthropic 的响应均为 {"data":[{"id":...}]}，统一解析。
class ModelListClient : public QObject
{
    Q_OBJECT
public:
    explicit ModelListClient(QObject *parent = nullptr);

    // 回调参数 (models, error)：成功时 models 已排序去重，error 为空；失败时 models 为空
    using FetchCallback = std::function<void(QStringList models, QString error)>;

    // 按协议解析模型清单端点并发起 GET。同一时间只跟踪最后一次请求，
    // 过期回复的回调仍会触发，由调用方按需忽略（如 ChatController 的代次计数）。
    void fetch(Protocol protocol, const QString &baseUrl, const QString &apiKey,
               FetchCallback callback);

private:
    QNetworkAccessManager m_nam;
};

} // namespace lens
