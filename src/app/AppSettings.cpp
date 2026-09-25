#include "AppSettings.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QPalette>
#include <QStyleHints>
#include <nlohmann/json.hpp>

namespace lens {
namespace {

inline const QString kDefaultEndpoint = QStringLiteral("https://api.openai.com/v1/chat/completions");
inline const QString kDefaultModel = QStringLiteral("gpt-4o-mini");
inline const QString kDefaultProtocol = QStringLiteral("chat_completions");

std::string readStd(const QString &value) { return value.toStdString(); }

QString readQStr(const nlohmann::json &j, const char *key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return {};
    return QString::fromStdString(it->get<std::string>());
}

QString normalizeChoice(const QString &value, std::initializer_list<const char *> allowed)
{
    for (const char *choice : allowed) {
        if (value == QLatin1String(choice))
            return value;
    }
    return QStringLiteral("system");
}

} // namespace

AppSettings::AppSettings(QString filePath, QObject *parent)
    : QObject(parent)
    , m_path(std::move(filePath))
{
    reset();
    load();

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { emit settingsChanged(); });
}

namespace {
ProviderConfig defaultProvider()
{
    return {QStringLiteral("默认"), kDefaultProtocol, kDefaultEndpoint, QString(), kDefaultModel};
}
} // namespace

void AppSettings::reset()
{
    m_providers = {defaultProvider()};
    m_activeProvider = 0;
    m_systemPrompt = QString();
    m_mcpServers.clear();
    m_webSearchEndpoint = QString();
    m_webSearchApiKey = QString();
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

    if (json.contains("providers") && json.at("providers").is_array()) {
        m_providers.clear();
        for (const auto &entry : json.at("providers")) {
            if (!entry.is_object())
                continue;
            ProviderConfig provider;
            provider.name = readQStr(entry, "name");
            provider.protocol = readQStr(entry, "protocol");
            provider.endpoint = readQStr(entry, "endpoint");
            provider.apiKey = readQStr(entry, "apiKey");
            provider.model = readQStr(entry, "model");
            if (entry.contains("serverSearch") && entry.at("serverSearch").is_boolean())
                provider.serverSearch = entry.at("serverSearch").get<bool>();
            if (provider.name.isEmpty())
                provider.name = QStringLiteral("供应商%1").arg(m_providers.size() + 1);
            if (provider.protocol.isEmpty())
                provider.protocol = kDefaultProtocol;
            m_providers.append(provider);
        }
        if (m_providers.isEmpty())
            m_providers.append(defaultProvider());
    } else {
        ProviderConfig legacy = defaultProvider();
        const QString endpoint = readQStr(json, "endpoint");
        if (!endpoint.isEmpty())
            legacy.endpoint = endpoint;
        const QString model = readQStr(json, "model");
        if (!model.isEmpty())
            legacy.model = model;
        legacy.apiKey = readQStr(json, "apiKey");
        m_providers = {legacy};
    }

    const auto activeIt = json.find("activeProvider");
    m_activeProvider = activeIt != json.end() && activeIt->is_number_integer()
                           ? activeIt->get<int>()
                           : 0;
    m_activeProvider = qBound(0, m_activeProvider, m_providers.size() - 1);

    m_systemPrompt = readQStr(json, "systemPrompt");

    if (json.contains("mcpServers") && json.at("mcpServers").is_array()) {
        m_mcpServers.clear();
        for (const auto &entry : json.at("mcpServers")) {
            if (!entry.is_object())
                continue;
            McpServerConfig server;
            server.name = readQStr(entry, "name");
            server.command = readQStr(entry, "command");
            if (entry.contains("args") && entry.at("args").is_array()) {
                for (const auto &arg : entry.at("args")) {
                    if (arg.is_string())
                        server.args.append(
                            QString::fromStdString(arg.get<std::string>()));
                }
            }
            if (!server.name.isEmpty() && !server.command.isEmpty())
                m_mcpServers.append(server);
        }
    }

    m_webSearchEndpoint = readQStr(json, "webSearchEndpoint");
    m_webSearchApiKey = readQStr(json, "webSearchApiKey");
    m_language = normalizeChoice(readQStr(json, "language"), {"zh", "en"});
    m_theme = normalizeChoice(readQStr(json, "theme"), {"dark", "light"});
    m_sendShortcut = readQStr(json, "sendShortcut") == QLatin1String("enter")
                         ? QStringLiteral("enter")
                         : QStringLiteral("ctrl_enter");
    emit settingsChanged();
}

void AppSettings::save()
{
    nlohmann::json json = {
        {"activeProvider", m_activeProvider},
        {"systemPrompt", readStd(m_systemPrompt)},
        {"webSearchEndpoint", readStd(m_webSearchEndpoint)},
        {"webSearchApiKey", readStd(m_webSearchApiKey)},
        {"language", readStd(m_language)},
        {"theme", readStd(m_theme)},
        {"sendShortcut", readStd(m_sendShortcut)},
    };
    auto providers = nlohmann::json::array();
    for (const ProviderConfig &provider : m_providers) {
        providers.push_back({{"name", readStd(provider.name)},
                             {"protocol", readStd(provider.protocol)},
                             {"endpoint", readStd(provider.endpoint)},
                             {"apiKey", readStd(provider.apiKey)},
                             {"model", readStd(provider.model)},
                             {"serverSearch", provider.serverSearch}});
    }
    json["providers"] = std::move(providers);
    auto servers = nlohmann::json::array();
    for (const McpServerConfig &server : m_mcpServers) {
        auto args = nlohmann::json::array();
        for (const QString &arg : server.args)
            args.push_back(readStd(arg));
        servers.push_back({{"name", readStd(server.name)},
                           {"command", readStd(server.command)},
                           {"args", std::move(args)}});
    }
    json["mcpServers"] = std::move(servers);

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("无法保存设置: %s", qPrintable(m_path));
        return;
    }
    file.write(QByteArray::fromStdString(json.dump(2)));
}

ProviderConfig AppSettings::activeProviderConfig() const
{
    return m_providers.isEmpty() ? defaultProvider()
                                 : m_providers.value(qBound(0, m_activeProvider,
                                                            m_providers.size() - 1));
}

QString AppSettings::endpoint() const { return activeProviderConfig().endpoint; }
QString AppSettings::apiKey() const { return activeProviderConfig().apiKey; }
QString AppSettings::model() const { return activeProviderConfig().model; }
QString AppSettings::protocol() const { return activeProviderConfig().protocol; }
bool AppSettings::serverSearch() const { return activeProviderConfig().serverSearch; }

void AppSettings::setEndpoint(const QString &value)
{
    if (!m_providers.isEmpty())
        m_providers[qBound(0, m_activeProvider, m_providers.size() - 1)].endpoint = value;
    emit settingsChanged();
}

void AppSettings::setApiKey(const QString &value)
{
    if (!m_providers.isEmpty())
        m_providers[qBound(0, m_activeProvider, m_providers.size() - 1)].apiKey = value;
    emit settingsChanged();
}

void AppSettings::setModel(const QString &value)
{
    if (!m_providers.isEmpty())
        m_providers[qBound(0, m_activeProvider, m_providers.size() - 1)].model = value;
    emit settingsChanged();
}

void AppSettings::setProtocol(const QString &value)
{
    if (!m_providers.isEmpty())
        m_providers[qBound(0, m_activeProvider, m_providers.size() - 1)].protocol =
            value.isEmpty() ? kDefaultProtocol : value;
    emit settingsChanged();
}

void AppSettings::setServerSearch(bool value)
{
    if (!m_providers.isEmpty())
        m_providers[qBound(0, m_activeProvider, m_providers.size() - 1)].serverSearch = value;
    emit settingsChanged();
}

void AppSettings::setActiveProvider(int index)
{
    m_activeProvider = qBound(0, index, qMax(0, m_providers.size() - 1));
    emit settingsChanged();
}

QVariantMap AppSettings::providerToMap(const ProviderConfig &provider) const
{
    return {{QStringLiteral("name"), provider.name},
            {QStringLiteral("protocol"), provider.protocol},
            {QStringLiteral("endpoint"), provider.endpoint},
            {QStringLiteral("apiKey"), provider.apiKey},
            {QStringLiteral("model"), provider.model},
            {QStringLiteral("serverSearch"), provider.serverSearch}};
}

ProviderConfig AppSettings::providerFromMap(const QVariantMap &map) const
{
    ProviderConfig provider;
    provider.name = map.value(QStringLiteral("name")).toString();
    provider.protocol = map.value(QStringLiteral("protocol")).toString();
    provider.endpoint = map.value(QStringLiteral("endpoint")).toString();
    provider.apiKey = map.value(QStringLiteral("apiKey")).toString();
    provider.model = map.value(QStringLiteral("model")).toString();
    provider.serverSearch = map.value(QStringLiteral("serverSearch")).toBool();
    if (provider.protocol.isEmpty())
        provider.protocol = kDefaultProtocol;
    return provider;
}

QVariantList AppSettings::providers() const
{
    QVariantList list;
    for (const ProviderConfig &provider : m_providers)
        list.append(providerToMap(provider));
    return list;
}

void AppSettings::addProvider(const QVariantMap &provider)
{
    ProviderConfig config = providerFromMap(provider);
    if (config.name.isEmpty())
        config.name = QStringLiteral("供应商%1").arg(m_providers.size() + 1);
    m_providers.append(config);
    m_activeProvider = m_providers.size() - 1;
    emit settingsChanged();
}

void AppSettings::updateProvider(int index, const QVariantMap &provider)
{
    if (index < 0 || index >= m_providers.size())
        return;
    const QString name = provider.value(QStringLiteral("name")).toString();
    ProviderConfig config = providerFromMap(provider);
    if (name.isEmpty())
        config.name = m_providers[index].name;
    m_providers[index] = config;
    emit settingsChanged();
}

void AppSettings::removeProvider(int index)
{
    if (index < 0 || index >= m_providers.size() || m_providers.size() <= 1)
        return;
    m_providers.remove(index);
    m_activeProvider = qBound(0, m_activeProvider, m_providers.size() - 1);
    emit settingsChanged();
}

QList<McpServerConfig> AppSettings::mcpServerConfigs() const { return m_mcpServers; }

QVariantList AppSettings::mcpServers() const
{
    QVariantList list;
    for (const McpServerConfig &server : m_mcpServers) {
        list.append(QVariantMap{{QStringLiteral("name"), server.name},
                                {QStringLiteral("command"), server.command},
                                {QStringLiteral("args"), server.args}});
    }
    return list;
}

void AppSettings::setMcpServers(const QVariantList &servers)
{    m_mcpServers.clear();
    for (const QVariant &entry : servers) {
        const QVariantMap map = entry.toMap();
        McpServerConfig server;
        server.name = map.value(QStringLiteral("name")).toString().trimmed();
        server.command = map.value(QStringLiteral("command")).toString().trimmed();
        server.args = map.value(QStringLiteral("args")).toStringList();
        if (!server.name.isEmpty() && !server.command.isEmpty())
            m_mcpServers.append(server);
    }
    emit settingsChanged();
}

void AppSettings::setLanguage(const QString &value)
{
    m_language = normalizeChoice(value, {"zh", "en"});
    emit settingsChanged();
}

void AppSettings::setTheme(const QString &value)
{
    m_theme = normalizeChoice(value, {"dark", "light"});
    emit settingsChanged();
}

bool AppSettings::dark() const
{
    if (m_theme == QLatin1String("dark"))
        return true;
    if (m_theme == QLatin1String("light"))
        return false;
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return true;
    case Qt::ColorScheme::Light:
        return false;
    case Qt::ColorScheme::Unknown:
        break;
    }
    return QGuiApplication::palette().window().color().lightness() < 128;
}

void AppSettings::setSendShortcut(const QString &value)
{
    m_sendShortcut = value == QLatin1String("enter")
                         ? QStringLiteral("enter")
                         : QStringLiteral("ctrl_enter");
    emit settingsChanged();
}

} // namespace lens
