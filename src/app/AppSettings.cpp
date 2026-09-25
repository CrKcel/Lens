#include "AppSettings.hpp"

#include <QFile>
#include <nlohmann/json.hpp>

namespace lens {
namespace {

// QString 常量
inline const QString kDefaultEndpoint = QStringLiteral("https://api.openai.com/v1/chat/completions");
inline const QString kDefaultModel = QStringLiteral("gpt-4o-mini");

std::string readStd(const QString &value) { return value.toStdString(); }
QString readQStr(const nlohmann::json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return {};
    return QString::fromStdString(it->get<std::string>());
}

} // namespace

AppSettings::AppSettings(QString filePath, QObject *parent)
    : QObject(parent)
    , m_path(std::move(filePath))
{
    reset();
    load();
}

void AppSettings::reset()
{
    m_endpoint = kDefaultEndpoint;
    m_apiKey = QString();
    m_model = kDefaultModel;
    m_systemPrompt = QString();
    emit settingsChanged();
}

void AppSettings::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto json = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return;
    const QString endpoint = readQStr(json, "endpoint");
    if (!endpoint.isEmpty())
        m_endpoint = endpoint;
    m_apiKey = readQStr(json, "apiKey");
    const QString model = readQStr(json, "model");
    if (!model.isEmpty())
        m_model = model;
    m_systemPrompt = readQStr(json, "systemPrompt");
    emit settingsChanged();
}

void AppSettings::save()
{
    const nlohmann::json json = {
        {"endpoint", readStd(m_endpoint)},
        {"apiKey", readStd(m_apiKey)},
        {"model", readStd(m_model)},
        {"systemPrompt", readStd(m_systemPrompt)},
    };
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("无法保存设置: %s", qPrintable(m_path));
        return;
    }
    file.write(QByteArray::fromStdString(json.dump(2)));
}

} // namespace lens
