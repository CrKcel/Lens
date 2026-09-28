#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>
#include <functional>

namespace lens {

struct HttpRequest {
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
};

// 传输层错误的载体。httpStatus 为 HTTP 响应状态码（请求没到服务器时为 0）；
// networkError 为 QNetworkReply::NetworkError 枚举值；retryAfterSeconds 取自
// Retry-After 响应头（未提供为 0），供上层退避策略参考。
struct TransportError {
    QString message;
    int httpStatus = 0;
    int networkError = 0;
    int retryAfterSeconds = 0;
};

// 流式传输回调。onChunk 交付增量字节（SSE 原始分片，按事件拆分交给协议层）；
// onFinished 表示传输正常结束；onError 携带传输层错误（网络、HTTP 状态等）；
// onCanceled 表示请求被 cancel() 中止，不计为错误。
struct StreamCallbacks {
    std::function<void(const QByteArray &)> onChunk;
    std::function<void()> onFinished;
    std::function<void(const TransportError &)> onError;
    std::function<void()> onCanceled;
};

// 传输层抽象：把“HTTP + 流式响应”从协议适配（chat completions / responses /
// anthropic…）中隔离出来。
class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual void start(const HttpRequest &request, StreamCallbacks callbacks) = 0;
    virtual void cancel() = 0;
};

} // namespace lens
