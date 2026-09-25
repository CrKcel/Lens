#pragma once

#include <QObject>
#include <QString>

namespace lens {

class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY settingsChanged)

public:
    explicit AppSettings(QString filePath, QObject *parent = nullptr);

    QString endpoint() const { return m_endpoint; }
    QString apiKey() const { return m_apiKey; }
    QString model() const { return m_model; }
    QString systemPrompt() const { return m_systemPrompt; }
    void setEndpoint(const QString &value) { m_endpoint = value; emit settingsChanged(); }
    void setApiKey(const QString &value) { m_apiKey = value; emit settingsChanged(); }
    void setModel(const QString &value) { m_model = value; emit settingsChanged(); }
    void setSystemPrompt(const QString &value) { m_systemPrompt = value; emit settingsChanged(); }

    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();

signals:
    void settingsChanged();

private:
    void load();

    QString m_path;
    QString m_endpoint;
    QString m_apiKey;
    QString m_model;
    QString m_systemPrompt;
};

} // namespace lens
