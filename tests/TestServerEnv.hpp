// 真实服务器测试公共设施。
// 所有依赖网络的测试统一指向测试服务器（默认 http://127.0.0.1:8081，环境变量
// LENS_TEST_SERVER 可覆盖）。服务器离线或目标协议端点不受支持时，相关用例
// 跳过并打警告，不判失败——测试不再内嵌任何 mock 服务器。
#pragma once

#include <QtTest/QtTest>

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <lens/core/providers/ProtocolAdapter.hpp>

namespace lens::test {

// 测试服务器 base 端点。LENS_TEST_SERVER 允许给完整 chat/completions 路径或
// 仅 base：统一退回 base，再由各协议适配器补全自家路径
inline QString serverBaseEndpoint()
{
    QString base = qEnvironmentVariable("LENS_TEST_SERVER").trimmed();
    if (base.isEmpty())
        base = QStringLiteral("http://127.0.0.1:8081");
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/chat/completions")))
        base.chop(QStringView(u"/chat/completions").size());
    return base;
}

struct TestServer
{
    QString endpoint;          // OpenAI 系 base（chat completions / responses / models）
    QString anthropicEndpoint; // Anthropic 兼容端点与 OpenAI 系不同前缀的供应商可覆盖
    QString apiKey;
    QString model;
    bool online = false;
};

// 同步 GET /v1/models：探测在线并取默认模型（LENS_TEST_MODEL 优先）。
// 响应兼容两种形态：OpenAI {"data":[{"id":..}]} 与 llama.cpp {"models":[{"name":..}]}
inline TestServer probeTestServer()
{
    TestServer server;
    server.endpoint = serverBaseEndpoint();
    const QString anthropicOverride =
        qEnvironmentVariable("LENS_TEST_ANTHROPIC_ENDPOINT").trimmed();
    server.anthropicEndpoint =
        anthropicOverride.isEmpty() ? server.endpoint : anthropicOverride;
    const QString key = qEnvironmentVariable("LENS_TEST_API_KEY").trimmed();
    server.apiKey = key.isEmpty() ? QStringLiteral("no-key-needed") : key;
    server.model = qEnvironmentVariable("LENS_TEST_MODEL").trimmed();

    QNetworkAccessManager nam;
    QNetworkReply *reply =
        nam.get(QNetworkRequest(QUrl(server.endpoint + QStringLiteral("/v1/models"))));
    QEventLoop loop;
    bool finished = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&] {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    if (finished && reply->error() == QNetworkReply::NoError) {
        server.online = true;
        if (server.model.isEmpty()) {
            const QByteArray body = reply->readAll();
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            const QJsonArray data = root.value("data").toArray();
            if (!data.isEmpty()) {
                server.model = data.at(0).toObject().value("id").toString();
            } else {
                const QJsonArray models = root.value("models").toArray();
                if (!models.isEmpty())
                    server.model = models.at(0).toObject().value("name").toString();
            }
        }
    } else {
        reply->abort();
    }
    reply->deleteLater();
    if (server.model.isEmpty())
        server.model = QStringLiteral("default");
    return server;
}

// 探测服务器是否提供某协议端点：发一个最小请求，连接层失败或 404/405/501
// 视为不支持（其余 HTTP 错误说明路由存在，只是参数或服务端问题）
inline bool protocolSupported(const TestServer &server, Protocol protocol)
{
    const QString base = protocol == Protocol::Anthropic ? server.anthropicEndpoint
                                                         : server.endpoint;
    QUrl url(base);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    QByteArray body;
    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (protocol == Protocol::Anthropic) {
        url.setPath(path + QStringLiteral("/v1/messages"));
        request.setRawHeader("x-api-key", server.apiKey.toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");
        body = QByteArrayLiteral("{\"model\":\"probe\",\"max_tokens\":1,\"stream\":false,"
                                 "\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}]}");
    } else if (protocol == Protocol::Responses) {
        url.setPath(path + (path.endsWith(QLatin1String("/v1")) ? QStringLiteral("/responses")
                                                                : QStringLiteral("/v1/responses")));
        request.setRawHeader("Authorization", "Bearer " + server.apiKey.toUtf8());
        body = QByteArrayLiteral("{\"model\":\"probe\",\"input\":\"ping\",\"stream\":false}");
    } else {
        url.setPath(path + (path.endsWith(QLatin1String("/v1"))
                                ? QStringLiteral("/chat/completions")
                                : QStringLiteral("/v1/chat/completions")));
        request.setRawHeader("Authorization", "Bearer " + server.apiKey.toUtf8());
        body = QByteArrayLiteral("{\"model\":\"probe\",\"max_tokens\":1,\"stream\":false,"
                                 "\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}]}");
    }
    request.setUrl(url);

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.post(request, body);
    QEventLoop loop;
    bool finished = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&] {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    loop.exec();
    bool supported = false;
    if (finished && reply->error() != QNetworkReply::OperationCanceledError) {
        if (reply->error() == QNetworkReply::NoError) {
            supported = true;
        } else {
            const QVariant status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            supported = status.isValid() && status.toInt() != 404 && status.toInt() != 405
                        && status.toInt() != 501;
        }
    } else {
        reply->abort();
    }
    reply->deleteLater();
    return supported;
}

// 测试图片：LENS_TEST_IMAGE 优先，否则从当前目录向上找 packaging/icons/lens-256.png
inline QString findTestImage()
{
    const QString fromEnv = qEnvironmentVariable("LENS_TEST_IMAGE");
    if (!fromEnv.isEmpty())
        return fromEnv;
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 4; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("packaging/icons/lens-256.png"));
        if (QFileInfo::exists(candidate))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return {};
}

// 测试工作目录：LENS_TEST_WORKDIR 优先，默认临时目录下的 lens-real-test
inline QString testWorkdir()
{
    const QString fromEnv = qEnvironmentVariable("LENS_TEST_WORKDIR");
    const QString path =
        fromEnv.isEmpty() ? QDir::temp().filePath(QStringLiteral("lens-real-test")) : fromEnv;
    QDir().mkpath(path);
    return path;
}

} // namespace lens::test

// 在 initTestCase / 测试槽内使用：探测测试服务器，离线则警告并跳过
#define LENS_REQUIRE_SERVER(var)                                                          \
    (var) = lens::test::probeTestServer();                                                \
    if (!(var).online) {                                                                  \
        qWarning("测试服务器 %s 离线，跳过（LENS_TEST_SERVER 可指定其它地址）",             \
                 qPrintable((var).endpoint));                                             \
        QSKIP("测试服务器离线");                                                          \
    }

// 需要特定协议端点时使用：不支持则警告并跳过
#define LENS_REQUIRE_PROTOCOL(server, protocol, label)                                    \
    if (!lens::test::protocolSupported((server), (protocol))) {                           \
        qWarning("测试服务器 %s 不支持 %s 协议端点，跳过",                                 \
                 qPrintable((server).endpoint), label);                                   \
        QSKIP("协议端点不受支持");                                                        \
    }
