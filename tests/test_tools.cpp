#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

class TestTools : public QObject
{
    Q_OBJECT

private slots:
    void writeAndReadFile();
    void readRespectsOffsetAndLimit();
    void editReplacesUniqueMatch();
    void editRejectsAmbiguousMatch();
    void editReplaceAll();
    void bashCapturesOutputAndExitCode();
    void unknownToolFails();
};

void TestTools::writeAndReadFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<ReadTool>());

    const auto wrote = registry.execute(
        QStringLiteral("write"),
        nlohmann::json{{"path", "src/main.cpp"}, {"content", "int main() {}"}},
        dir.path());
    QVERIFY(wrote.ok);
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("src/main.cpp")))); // 父目录自动创建

    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "src/main.cpp"}}, dir.path());
    QVERIFY(read.ok);
    QCOMPARE(read.output, QStringLiteral("int main() {}"));

    const auto missing = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "nope.txt"}}, dir.path());
    QVERIFY(!missing.ok);
}

void TestTools::readRespectsOffsetAndLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("lines.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QStringList lines;
    for (int i = 1; i <= 30; ++i)
        lines.append(QStringLiteral("line %1").arg(i));
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.close();

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"),
        nlohmann::json{{"path", "lines.txt"}, {"offset", 20}, {"limit", 5}},
        dir.path());
    QVERIFY(read.ok);
    QVERIFY(read.output.startsWith(QStringLiteral("line 20\n")));
    QVERIFY(read.output.contains(QStringLiteral("\nline 24\n…")));
    QVERIFY(read.output.contains(QStringLiteral("共 30 行")));
}

void TestTools::editReplacesUniqueMatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "a.txt"}, {"content", "foo bar baz"}},
                             dir.path()).ok);

    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "a.txt"}, {"old_string", "bar"}, {"new_string", "quux"}},
        dir.path());
    QVERIFY(edited.ok);

    QFile file(dir.filePath(QStringLiteral("a.txt")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("foo quux baz"));
}

void TestTools::editRejectsAmbiguousMatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "a.txt"}, {"content", "x y x"}},
                             dir.path()).ok);

    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "a.txt"}, {"old_string", "x"}, {"new_string", "z"}},
        dir.path());
    QVERIFY(!edited.ok); 
    QVERIFY(edited.output.contains(QStringLiteral("2 次")));
}

void TestTools::editReplaceAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "a.txt"}, {"content", "x y x"}},
                             dir.path()).ok);
    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "a.txt"}, {"old_string", "x"}, {"new_string", "z"},
                       {"replace_all", true}},
        dir.path());
    QVERIFY(edited.ok);

    QFile file(dir.filePath(QStringLiteral("a.txt")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("z y z"));
}

void TestTools::bashCapturesOutputAndExitCode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<BashTool>());

    const auto echoed = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "printf 'hello %s' \"$PWD\""}},
        dir.path());
    QVERIFY(echoed.ok);
    QVERIFY(echoed.output.startsWith(QStringLiteral("hello ")));
    QVERIFY(echoed.output.contains(QStringLiteral("[退出码: 0]")));

    const auto failed = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "exit 3"}},
        dir.path());
    QVERIFY(!failed.ok);
    QVERIFY(failed.output.contains(QStringLiteral("[退出码: 3]")));
}

void TestTools::unknownToolFails()
{
    ToolRegistry registry;
    const auto result = registry.execute(
        QStringLiteral("nope"), nlohmann::json::object(), QStringLiteral("/tmp"));
    QVERIFY(!result.ok);
    QVERIFY(result.output.contains(QStringLiteral("未知工具")));
}

QTEST_GUILESS_MAIN(TestTools)
#include "test_tools.moc"
