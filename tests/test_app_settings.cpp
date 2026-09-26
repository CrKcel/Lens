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
    void fontScaleRoundtripAndNormalization();
    void lineSpacingRoundtripAndNormalization();

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

void TestAppSettings::fontScaleRoundtripAndNormalization()
{
    const QString path = writeJson("{}");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");

    {
        AppSettings settings(path);
        QCOMPARE(settings.fontScale(), 1.0);
        settings.setFontScale(1.3);
        settings.save();
    }
    AppSettings reloaded(path);
    QCOMPARE(reloaded.fontScale(), 1.3);

    // 档位归一化：非法值回到最近档位，缺省回 1.0
    reloaded.setFontScale(0.95);
    QCOMPARE(reloaded.fontScale(), 1.0);
    reloaded.setFontScale(2.0);
    QCOMPARE(reloaded.fontScale(), 1.5);
    reloaded.setFontScale(0.1);
    QCOMPARE(reloaded.fontScale(), 0.85);

    const QString invalidPath = writeJson(R"({"fontScale":"large"})");
    AppSettings invalid(invalidPath);
    QCOMPARE(invalid.fontScale(), 1.0);
}

void TestAppSettings::lineSpacingRoundtripAndNormalization()
{
    const QString path = writeJson("{}");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");

    {
        AppSettings settings(path);
        QCOMPARE(settings.lineSpacing(), 1.3);
        settings.setLineSpacing(1.0);
        settings.save();
    }
    AppSettings reloaded(path);
    QCOMPARE(reloaded.lineSpacing(), 1.0);

    // 档位归一化：非法值回到最近档位，缺省回 1.3
    reloaded.setLineSpacing(1.2);
    QCOMPARE(reloaded.lineSpacing(), 1.15);
    reloaded.setLineSpacing(1.45);
    QCOMPARE(reloaded.lineSpacing(), 1.5);
    reloaded.setLineSpacing(3.0);
    QCOMPARE(reloaded.lineSpacing(), 1.5);

    const QString invalidPath = writeJson(R"({"lineSpacing":"wide"})");
    AppSettings invalid(invalidPath);
    QCOMPARE(invalid.lineSpacing(), 1.3);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestAppSettings test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_app_settings.moc"
