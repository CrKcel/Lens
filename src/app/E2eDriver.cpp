#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include "AppSettings.hpp"
#include "ChatController.hpp"
#include "MessageListModel.hpp"

namespace lens {
namespace {

void writeReport(QQmlApplicationEngine &engine, ChatController *chat)
{
    QJsonObject report;

    // ① C++ 数据层：MessageListModel 的真实内容
    auto *model = qobject_cast<MessageListModel *>(chat->messages());
    report["modelRows"] = model ? model->rowCount() : -1;
    QJsonArray rows;
    if (model) {
        for (int i = 0; i < model->rowCount(); ++i) {
            const QModelIndex idx = model->index(i, 0);
            QJsonObject row;
            row["kind"] = model->data(idx, MessageListModel::KindRole).toInt();
            row["streaming"] = model->data(idx, MessageListModel::StreamingRole).toBool();
            row["text"] = model->data(idx, MessageListModel::TextRole).toString().left(300);
            row["reasoning"] =
                model->data(idx, MessageListModel::ReasoningRole).toString().left(200);
            row["toolName"] = model->data(idx, MessageListModel::ToolNameRole).toString();
            rows.append(row);
        }
    }
    report["modelData"] = rows;

    // ② QML 渲染层：ListView 实际实例化的委托与标签内容
    QQuickItem *list = nullptr;
    for (QObject *rootObj : engine.rootObjects()) {
        if ((list = rootObj->findChild<QQuickItem *>(QStringLiteral("messageListView"))))
            break;
    }
    report["qmlListFound"] = bool(list);
    if (list) {
        report["qmlCount"] = list->property("count").toInt();
        QJsonArray rendered;
        const int count = list->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, i));
            QJsonObject renderedRow;
            renderedRow["index"] = i;
            renderedRow["instantiated"] = bool(item);
            if (item) {
                renderedRow["height"] = item->height();
                const auto content = item->findChild<QObject *>(QStringLiteral("assistantContent"));
                const auto reasoning =
                    item->findChild<QObject *>(QStringLiteral("assistantReasoning"));
                renderedRow["assistantText"] = content
                    ? content->property("text").toString().left(300)
                    : QString();
                renderedRow["assistantReasoning"] = reasoning
                    ? reasoning->property("text").toString().left(200)
                    : QString();
            }
            rendered.append(renderedRow);
        }
        report["qmlRendered"] = rendered;
    }

    const QString outPath = qEnvironmentVariable("LENS_E2E_OUT");
    QFile out(outPath.isEmpty() ? QStringLiteral("/tmp/lens-e2e.json") : outPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(report).toJson());
}

void runE2e(QQmlApplicationEngine &engine, ChatController *chat, AppSettings *settings)
{
    const QStringList args = QCoreApplication::arguments();
    QString message = QStringLiteral("请用一句话介绍你自己");
    const int msgIdx = args.indexOf(QStringLiteral("--e2e-chat"));
    if (msgIdx >= 0 && args.size() > msgIdx + 1)
        message = args.at(msgIdx + 1);

    // 设置：优先环境变量（含自动探测模型名），否则用已保存的 settings.json
    const QString envEndpoint = qEnvironmentVariable("LENS_E2E_ENDPOINT");
    if (!envEndpoint.isEmpty()) {
        settings->setEndpoint(envEndpoint);
        settings->setApiKey(qEnvironmentVariable("LENS_E2E_API_KEY"));
        QString model = qEnvironmentVariable("LENS_E2E_MODEL");
        if (model.isEmpty()) {
            QNetworkAccessManager nam;
            QUrl modelsUrl(envEndpoint);
            modelsUrl.setPath(QStringLiteral("/v1/models"));
            auto *reply = nam.get(QNetworkRequest(modelsUrl));
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            reply->deleteLater();
            model = doc.object().value("data").toArray().at(0).toObject()
                        .value("id").toString();
        }
        settings->setModel(model);
        settings->save();
    }

    // 走 UI 路径发送：设置输入框文本后调用 sendAction()。
    // LENS_E2E_WORKDIR：先创建绑定该目录的会话（工具操作的根目录）。
    QTimer::singleShot(300, chat, [chat, &engine, message] {
        QObject *root = engine.rootObjects().value(0);
        if (!root) {
            QCoreApplication::exit(3);
            return;
        }
        const QString workdir = qEnvironmentVariable("LENS_E2E_WORKDIR");
        if (!workdir.isEmpty())
            chat->newConversation(workdir);
        QObject *input = root->findChild<QObject *>(QStringLiteral("chatInput"));
        if (input) {
            input->setProperty("text", message);
            input->setProperty("cursorPosition", message.length());
        }
    if (qEnvironmentVariableIsEmpty("LENS_E2E_NOSEND"))
        QMetaObject::invokeMethod(root, "sendAction");
    else
        QTimer::singleShot(1200, chat, [chat, &engine] {
            writeReport(engine, chat);
            if (qEnvironmentVariableIsEmpty("LENS_E2E_NOEXIT"))
                QCoreApplication::exit(0);
        });
    });

    // streaming true→false 即回合结束，导出报告
    auto dumpOnce = [chat, &engine]() {
        if (chat->streaming())
            return;
        QTimer::singleShot(800, chat, [chat, &engine]() {
            writeReport(engine, chat);
            QCoreApplication::exit(0);
        });
    };
    QObject::connect(chat, &ChatController::streamingChanged, dumpOnce);

    // 整体超时保护：同样导出完整状态，用于区分“没发出去”与“发出后挂住”
    QTimer::singleShot(240000, chat, [chat, &engine] {
        qWarning("e2e 超时，导出当前状态");
        writeReport(engine, chat);
        QCoreApplication::exit(2);
    });
}

} // namespace

void runE2eIfNeeded(QQmlApplicationEngine &engine, ChatController *chat, AppSettings *settings)
{
    if (QCoreApplication::arguments().contains(QStringLiteral("--e2e-chat")))
        runE2e(engine, chat, settings);
}

} // namespace lens
