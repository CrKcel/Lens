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
    void legacyStringModelsMigration();
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
    // 新配置：模型条目为对象，带显示名 / 上下文窗口 / 输出上限 / 图片开关
    const QVariantList models{
        QVariantMap{{"id", QStringLiteral("m-a")},
                    {"displayName", QStringLiteral("模型 A")},
                    {"contextWindow", 128000},
                    {"maxOutputTokens", 8192},
                    {"images", false}},
        QVariantMap{{"id", QStringLiteral("m-b")}}};

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
    const ProviderConfig config = reloaded.activeProviderConfig();
    QCOMPARE(config.models.size(), 2);
    QCOMPARE(config.models[0].id, QStringLiteral("m-a"));
    QCOMPARE(config.models[0].displayName, QStringLiteral("模型 A"));
    QCOMPARE(config.models[0].contextWindow, 128000);
    QCOMPARE(config.models[0].maxOutputTokens, 8192);
    QVERIFY(!config.models[0].images);
    // 缺省键回退默认值：无显示名、无上限、图片开启
    QCOMPARE(config.models[1].id, QStringLiteral("m-b"));
    QVERIFY(config.models[1].displayName.isEmpty());
    QCOMPARE(config.models[1].contextWindow, 0);
    QCOMPARE(config.models[1].maxOutputTokens, 0);
    QVERIFY(config.models[1].images);
    QCOMPARE(config.model, QStringLiteral("m-a"));
    QCOMPARE(reloaded.model(), QStringLiteral("m-a"));
    // modelConfigFor 按 id 取元数据，未收录时回退仅含 id 的默认配置
    const ModelConfig fallback = modelConfigFor(config, QStringLiteral("m-x"));
    QCOMPARE(fallback.id, QStringLiteral("m-x"));
    QCOMPARE(fallback.maxOutputTokens, 0);
    QVERIFY(fallback.images);
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

// 老配置的 models 是纯字符串 id：迁移后仅填 id，其余字段取默认（图片开启）
void TestAppSettings::legacyStringModelsMigration()
{
    const QString path = writeJson(R"({"providers":[
        {"name":"n","protocol":"chat_completions","endpoint":"https://h/v1",
         "apiKey":"k","model":"m-a","models":["m-a","m-b"]}],"activeProvider":0})");
    QVERIFY2(!path.isEmpty(), "写入测试配置文件失败");

    AppSettings settings(path);
    const auto config = settings.activeProviderConfig();
    QCOMPARE(config.models.size(), 2);
    QCOMPARE(config.models[0].id, QStringLiteral("m-a"));
    QVERIFY(config.models[0].displayName.isEmpty());
    QCOMPARE(config.models[0].contextWindow, 0);
    QCOMPARE(config.models[0].maxOutputTokens, 0);
    QVERIFY(config.models[0].images);
    QCOMPARE(config.models[1].id, QStringLiteral("m-b"));
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
    QVERIFY(map.value(QStringLiteral("models")).toList().isEmpty());
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
