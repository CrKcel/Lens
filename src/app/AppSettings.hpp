#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace lens {

// 模型供应商配置：名称、协议、端点、密钥与模型
struct ProviderConfig {
    QString name;
    QString protocol; // chat_completions | responses | anthropic
    QString endpoint;
    QString apiKey;
    QString model;
    bool serverSearch = false; // 服务端联网搜索（供应商支持时）
};

// 一个 MCP 服务器配置
struct McpServerConfig {
    QString name;
    QString command;
    QStringList args;
};

class AppSettings : public QObject
{
    Q_OBJECT
    // 供应商列表管理走 providers / addProvider 等。
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)
    Q_PROPERTY(QString protocol READ protocol WRITE setProtocol NOTIFY settingsChanged)
    Q_PROPERTY(bool serverSearch READ serverSearch WRITE setServerSearch NOTIFY settingsChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY settingsChanged)
    Q_PROPERTY(int activeProvider READ activeProvider WRITE setActiveProvider NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList mcpServers READ mcpServers NOTIFY settingsChanged)
    Q_PROPERTY(QString webSearchEndpoint READ webSearchEndpoint WRITE setWebSearchEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString webSearchApiKey READ webSearchApiKey WRITE setWebSearchApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(QString sendShortcut READ sendShortcut WRITE setSendShortcut NOTIFY settingsChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY settingsChanged)

public:
    explicit AppSettings(QString filePath, QObject *parent = nullptr);

    QString endpoint() const;
    QString apiKey() const;
    QString model() const;
    QString protocol() const;
    bool serverSearch() const;
    QString systemPrompt() const { return m_systemPrompt; }
    void setEndpoint(const QString &value);
    void setApiKey(const QString &value);
    void setModel(const QString &value);
    void setProtocol(const QString &value);
    void setServerSearch(bool value);
    void setSystemPrompt(const QString &value) { m_systemPrompt = value; emit settingsChanged(); }

    ProviderConfig activeProviderConfig() const;
    QVariantList providers() const;
    int activeProvider() const { return m_activeProvider; }
    void setActiveProvider(int index);

    Q_INVOKABLE void addProvider(const QVariantMap &provider);
    Q_INVOKABLE void updateProvider(int index, const QVariantMap &provider);
    Q_INVOKABLE void removeProvider(int index);

    QList<McpServerConfig> mcpServerConfigs() const;
    QVariantList mcpServers() const;
    Q_INVOKABLE void setMcpServers(const QVariantList &servers);

    QString webSearchEndpoint() const { return m_webSearchEndpoint; }
    QString webSearchApiKey() const { return m_webSearchApiKey; }
    void setWebSearchEndpoint(const QString &value) { m_webSearchEndpoint = value; emit settingsChanged(); }
    void setWebSearchApiKey(const QString &value) { m_webSearchApiKey = value; emit settingsChanged(); }

    QString language() const { return m_language; }
    void setLanguage(const QString &value);
    QString theme() const { return m_theme; }
    void setTheme(const QString &value);
    bool dark() const;
    QString sendShortcut() const { return m_sendShortcut; }
    void setSendShortcut(const QString &value);

    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

signals:
    void settingsChanged();

private:
    void load();
    QVariantMap providerToMap(const ProviderConfig &provider) const;
    ProviderConfig providerFromMap(const QVariantMap &map) const;

    QString m_path;
    QVector<ProviderConfig> m_providers;
    int m_activeProvider = 0;
    QString m_systemPrompt;
    QList<McpServerConfig> m_mcpServers;
    QString m_webSearchEndpoint;
    QString m_webSearchApiKey;
    QString m_language = QStringLiteral("system");   // system | zh | en
    QString m_theme = QStringLiteral("system");      // system | dark | light
    QString m_sendShortcut = QStringLiteral("ctrl_enter"); // ctrl_enter | enter
};

} // namespace lens
