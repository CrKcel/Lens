#include "lens/core/providers/QNetworkTransport.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace lens {

struct QNetworkTransport::Impl {
    QNetworkAccessManager nam;
    QNetworkReply *reply = nullptr;
    StreamCallbacks callbacks;
};

QNetworkTransport::QNetworkTransport(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
}

QNetworkTransport::~QNetworkTransport() = default;

void QNetworkTransport::start(const HttpRequest &request, StreamCallbacks callbacks)
{
    cancel(); // 同一传输同一时刻只承载一条流

    QNetworkRequest netRequest(request.url);
    netRequest.setTransferTimeout(0); // 流式生成可能长时间静默，禁用传输超时
    for (const auto &[name, value] : request.headers)
        netRequest.setRawHeader(name, value);

    d->callbacks = std::move(callbacks);
    d->reply = d->nam.post(netRequest, request.body);
    auto *reply = d->reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (d->reply != reply) // 已被新请求取代的残留分片，直接丢弃
            return;
        if (d->callbacks.onChunk)
            d->callbacks.onChunk(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool active = (d->reply == reply);
        if (active)
            d->reply = nullptr;
        reply->deleteLater();
        if (!active) // 过期请求的收尾信号：不回调任何拥有者
            return;
        if (reply->error() == QNetworkReply::NoError) {
            if (d->callbacks.onFinished)
                d->callbacks.onFinished();
        } else if (reply->error() == QNetworkReply::OperationCanceledError) {
            if (d->callbacks.onCanceled)
                d->callbacks.onCanceled();
        } else {
            if (d->callbacks.onError) {
                QString detail = reply->errorString();
                const QByteArray body = reply->readAll(); // API 错误响应体（JSON 错误信息）
                if (!body.isEmpty())
                    detail += QStringLiteral("\n") + QString::fromUtf8(body.left(2000));
                d->callbacks.onError(detail);
            }
        }
    });
}

void QNetworkTransport::cancel()
{
    if (d->reply)
        d->reply->abort(); // finished 回调负责收尾
}

} // namespace lens
