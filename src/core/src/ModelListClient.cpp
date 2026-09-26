#include "lens/core/providers/ModelListClient.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>

#include <optional>

namespace lens {
namespace {

constexpr int kTimeoutMs = 10000;

// 解析失败返回 nullopt；data 为空数组是合法响应，返回空列表
std::optional<QStringList> parseModels(const QByteArray &body)
{
    const auto doc = nlohmann::json::parse(body.toStdString(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        return std::nullopt;
    const auto dataIt = doc.find("data");
    if (dataIt == doc.end() || !dataIt->is_array())
        return std::nullopt;
    QStringList models;
    for (const auto &item : *dataIt) {
        if (!item.is_object())
            continue;
        const auto idIt = item.find("id");
        if (idIt != item.end() && idIt->is_string())
            models << QString::fromStdString(idIt->get<std::string>());
    }
    models.removeDuplicates();
    models.sort();
    return models;
}

} // namespace

ModelListClient::ModelListClient(QObject *parent) : QObject(parent) {}

void ModelListClient::fetch(Protocol protocol, const QString &baseUrl, const QString &apiKey,
                            FetchCallback callback)
{
    const auto adapter = makeProtocolAdapter(protocol);
    QNetworkRequest request(adapter->resolveModelsEndpoint(baseUrl));
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    for (const auto &header : adapter->extraHeaders(apiKey))
        request.setRawHeader(header.first, header.second);

    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback = std::move(callback)] {
                reply->deleteLater();
                const QByteArray body = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    QString message = reply->errorString();
                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status > 0) {
                        const QString snippet = QString::fromUtf8(body.left(200)).simplified();
                        message += QStringLiteral(" (HTTP %1%2)")
                                       .arg(status)
                                       .arg(snippet.isEmpty() ? QString() : QStringLiteral(": %1").arg(snippet));
                    }
                    callback({}, message);
                    return;
                }
                const auto models = parseModels(body);
                if (!models) {
                    callback({}, QStringLiteral("响应中未找到模型清单（data[].id）"));
                    return;
                }
                callback(*models, {});
            });
}

} // namespace lens
