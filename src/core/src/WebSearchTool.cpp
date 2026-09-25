#include "lens/core/tools/builtins/WebSearchTool.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include "lens/core/tools/ToolArgs.hpp"

namespace lens {
namespace {

constexpr int kTimeoutMs = 15000;
constexpr int kMaxResults = 5;

QString argsErrorMessage(const nlohmann::json &args)
{
    const QString query = argString(args, "query");
    if (query.trimmed().isEmpty())
        return QStringLiteral("web_search 需要 query 参数（搜索关键词）");
    return {};
}

} // namespace

ToolResult WebSearchTool::execute(const nlohmann::json &args, const QString &workdir)
{
    Q_UNUSED(workdir);
    if (const QString invalid = argsErrorMessage(args); !invalid.isEmpty())
        return {false, invalid};

    QString endpoint, apiKey;
    {
        QMutexLocker locker(&m_mutex);
        endpoint = m_endpoint;
        apiKey = m_apiKey;
    }
    if (endpoint.trimmed().isEmpty())
        return {false, QStringLiteral("web_search 未配置：请在设置中填写搜索 API 端点与密钥")};

    const QString query = argString(args, "query");
    QNetworkAccessManager manager;
    manager.setTransferTimeout(kTimeoutMs);

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"), "Bearer " + apiKey.toUtf8());

    const QJsonObject body{{"query", query}, {"max_results", kMaxResults}};
    QNetworkReply *reply =
        manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        // 服务端通常会随错误返回说明，带上便于模型与用户判断原因
        const QString detail = QString::fromUtf8(reply->readAll()).left(300);
        const QString message = QStringLiteral("web_search 请求失败：%1").arg(reply->errorString())
            + (detail.isEmpty() ? QString() : QStringLiteral("\n%1").arg(detail));
        reply->deleteLater();
        return {false, message};
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (!document.isObject()) {
        return {false, QStringLiteral("web_search 返回的不是 JSON 对象")};
    }
    const QJsonArray results = document.object().value(QStringLiteral("results")).toArray();
    if (results.isEmpty())
        return {true, QStringLiteral("没有找到与「%1」相关的结果").arg(query)};

    QStringList lines;
    for (const auto &value : results) {
        const QJsonObject result = value.toObject();
        lines.append(QStringLiteral("%1. %2\n   %3\n   %4")
                         .arg(lines.size() + 1)
                         .arg(result.value(QStringLiteral("title")).toString(),
                              result.value(QStringLiteral("url")).toString(),
                              result.value(QStringLiteral("content")).toString().simplified()));
    }
    return {true, lines.join(QStringLiteral("\n\n"))};
}

} // namespace lens
