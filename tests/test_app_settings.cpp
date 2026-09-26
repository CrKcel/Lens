#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include "AppSettings.hpp"

using namespace lens;

class TestAppSettings : public QObject
{
    Q_OBJECT

private slots:
    void modelsRoundtripThroughSaveLoad();
    void updateProviderWritesModels();
    void legacyProviderJsonWithoutModels();
    void legacyFlatConfigMigration();

private:
    QString writeJson(const QByteArray &json);
    QTemporaryDir m_dir;
};

QString TestAppSettings::writeJson(const QByteArray &json)
{
    static int counter = 0;
    const QString path = m_dir.filePath(QStringLiteral("settings-%1.json").arg(++counter));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(json);
    return path;
}

void TestAppSettings::modelsRoundtripThroughSaveLoad()
{
    const QString path = writeJson("{}");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");
    const QStringList models{QStringLiteral("m-a"), QStringLiteral("m-b")};

    {
        AppSettings settings(path);
        settings.updateProvider(0, QVariantMap{
            {"name", QStringLiteral("测试")},
            {"protocol", QStringLiteral("chat_completions")},
            {"endpoint", QStringLiteral("https://x/v1")},
            {"apiKey", QStringLiteral("k")},
            {"model", QStringLiteral("m-a")},
            {"models", models}});
        settings.save();
    }

    AppSettings reloaded(path);
    const auto providers = reloaded.providers();
    QCOMPARE(providers.size(), 1);
    const QVariantMap map = providers.first().toMap();
    QCOMPARE(map.value(QStringLiteral("models")).toStringList(), models);
    QCOMPARE(map.value(QStringLiteral("model")).toString(), QStringLiteral("m-a"));
    QCOMPARE(reloaded.activeProviderConfig().models, models);
    QCOMPARE(reloaded.model(), QStringLiteral("m-a"));
}

void TestAppSettings::updateProviderWritesModels()
{
    const QString path = writeJson("{}");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");
    AppSettings settings(path);
    QVERIFY(settings.activeProviderConfig().models.isEmpty());

    settings.updateProvider(0, QVariantMap{
        {"name", QStringLiteral("测试")},
        {"protocol", QStringLiteral("anthropic")},
        {"endpoint", QStringLiteral("https://x")},
        {"apiKey", QStringLiteral("k")},
        {"model", QStringLiteral("claude-a")},
        {"models", QStringList{QStringLiteral("claude-a")}}});
    QCOMPARE(settings.activeProviderConfig().models.size(), 1);

    settings.updateProvider(0, QVariantMap{
        {"name", QStringLiteral("测试")},
        {"protocol", QStringLiteral("anthropic")},
        {"endpoint", QStringLiteral("https://x")},
        {"apiKey", QStringLiteral("k")},
        {"model", QStringLiteral("claude-b")}});
    QVERIFY(settings.activeProviderConfig().models.isEmpty());
}

void TestAppSettings::legacyProviderJsonWithoutModels()
{
    const QString path = writeJson(R"({"providers":[
        {"name":"n","protocol":"anthropic","endpoint":"https://h/v1/messages",
         "apiKey":"k","model":"claude-a","serverSearch":true,"inputPrice":1.5}],
        "activeProvider":0})");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");

    AppSettings settings(path);
    const auto config = settings.activeProviderConfig();
    QVERIFY(config.models.isEmpty());
    QCOMPARE(config.model, QStringLiteral("claude-a"));
    QCOMPARE(config.protocol, QStringLiteral("anthropic"));
    QVERIFY(config.serverSearch);
    QCOMPARE(config.inputPrice, 1.5);
}

void TestAppSettings::legacyFlatConfigMigration()
{
    const QString path = writeJson(R"({"endpoint":"https://old/v1/chat/completions",
        "model":"old-model","apiKey":"old-key"})");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");

    AppSettings settings(path);
    const auto providers = settings.providers();
    QCOMPARE(providers.size(), 1);
    const QVariantMap map = providers.first().toMap();
    QCOMPARE(map.value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("https://old/v1/chat/completions"));
    QCOMPARE(map.value(QStringLiteral("model")).toString(), QStringLiteral("old-model"));
    QCOMPARE(map.value(QStringLiteral("apiKey")).toString(), QStringLiteral("old-key"));
    QVERIFY(map.value(QStringLiteral("models")).toStringList().isEmpty());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestAppSettings test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_app_settings.moc"
