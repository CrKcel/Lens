#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <lens/core/tools/ToolArgs.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/EditTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

namespace {

bool writeFileBytes(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

QByteArray readFileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestTools : public QObject
{
    Q_OBJECT

private slots:
    void writeAndReadFile();
    void writeRejectsMissingContent();
    void writeCoercesScalarContent();
    void readRespectsOffsetAndLimit();
    void readRejectsBinaryFile();
    void readReturnsImageFile();
    void readRejectsOversizedImage();
    void readTextFileStartingWithBm();
    void readOffsetBeyondEofFails();
    void readEmptyFilePlaceholder();
    void readTruncatesHugeSingleLine();
    void editReplacesUniqueMatch();
    void editRejectsAmbiguousMatch();
    void editReplaceAll();
    void editPreservesCrlfLineEndings();
    void editPreservesBom();
    void editRejectsNoOpReplacement();
    void editRejectsMissingNewString();
    void bashCapturesOutputAndExitCode();
    void bashTimesOutAndReports();
    void bashMissingWorkdirFails();
    void bashEmptyOutputPlaceholder();
    void bashKeepsTailOfHugeOutput();
    void resolveWorkdirExpandsTilde();
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

void TestTools::writeRejectsMissingContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());

    // content 缺失时必须报错，而不是把已有文件静默清空
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("a.txt")), "keep"));
    const auto wrote = registry.execute(
        QStringLiteral("write"), nlohmann::json{{"path", "a.txt"}}, dir.path());
    QVERIFY(!wrote.ok);
    QVERIFY(wrote.output.contains(QStringLiteral("content")));
    QCOMPARE(readFileBytes(dir.filePath(QStringLiteral("a.txt"))), QByteArray("keep"));
}

void TestTools::writeCoercesScalarContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<ReadTool>());

    const auto wrote = registry.execute(
        QStringLiteral("write"),
        nlohmann::json{{"path", "n.txt"}, {"content", 42}},
        dir.path());
    QVERIFY(wrote.ok);
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "n.txt"}}, dir.path());
    QVERIFY(read.ok);
    QCOMPARE(read.output, QStringLiteral("42"));
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

void TestTools::readRejectsBinaryFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("blob.bin")), QByteArray("abc\0def", 7)));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "blob.bin"}}, dir.path());
    QVERIFY(!read.ok);
    QVERIFY(read.output.contains(QStringLiteral("二进制")));
}

void TestTools::readReturnsImageFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray png = QByteArray("\x89PNG\r\n\x1A\n\x00\x00", 10) + QByteArray("pixels");
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("logo.png")), png));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "logo.png"}}, dir.path());
    QVERIFY(read.ok);
    QCOMPARE(read.images.size(), 1);
    QCOMPARE(read.images.first().mimeType, QStringLiteral("image/png"));
    QCOMPARE(read.images.first().data, png);
    QVERIFY(read.output.contains(QStringLiteral("image/png")));
}

void TestTools::readRejectsOversizedImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 5MB + 若干字节的 PNG 魔数数据，超出单图上限
    QByteArray huge(5 * 1024 * 1024 + 16, 'x');
    huge.prepend(QByteArray("\x89PNG\r\n\x1A\n", 8));
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("huge.png")), huge));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "huge.png"}}, dir.path());
    QVERIFY(!read.ok);
    QVERIFY(read.output.contains(QStringLiteral("压缩")));
}

void TestTools::readTextFileStartingWithBm()
{
    // "BM" 前缀 + 声明大小与实际不符：不是 BMP，按文本读取
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("bmw.txt")),
                           QByteArray("BMW models of 2026\nline two\n")));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "bmw.txt"}}, dir.path());
    QVERIFY(read.ok);
    QVERIFY(read.images.isEmpty());
    QVERIFY(read.output.startsWith(QStringLiteral("BMW models of 2026")));
}

void TestTools::readOffsetBeyondEofFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("tiny.txt")), "one\ntwo"));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"),
        nlohmann::json{{"path", "tiny.txt"}, {"offset", 999}},
        dir.path());
    QVERIFY(!read.ok);
    QVERIFY(read.output.contains(QStringLiteral("超出")));
}

void TestTools::readEmptyFilePlaceholder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("empty.txt")), ""));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "empty.txt"}}, dir.path());
    QVERIFY(read.ok);
    QCOMPARE(read.output, QStringLiteral("(空文件)"));
}

void TestTools::readTruncatesHugeSingleLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFileBytes(dir.filePath(QStringLiteral("wide.txt")), QByteArray(60 * 1024, 'x')));

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto read = registry.execute(
        QStringLiteral("read"), nlohmann::json{{"path", "wide.txt"}}, dir.path());
    QVERIFY(read.ok);
    QVERIFY(read.output.contains(QStringLiteral("第 1 行超过")));
    QVERIFY(read.output.size() > 50 * 1024);
    QVERIFY(read.output.size() < 52 * 1024);
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

void TestTools::editPreservesCrlfLineEndings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "crlf.txt"},
                                            {"content", "int a;\r\nint b;\r\nint c;\r\n"}},
                             dir.path()).ok);

    // old_string 用 \n 书写也能匹配 CRLF 文件，写回时保留 \r\n
    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "crlf.txt"},
                       {"old_string", "int a;\nint b;"}, {"new_string", "int a;\nint B;"}},
        dir.path());
    QVERIFY(edited.ok);

    QFile file(dir.filePath(QStringLiteral("crlf.txt")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()),
             QStringLiteral("int a;\r\nint B;\r\nint c;\r\n"));
}

void TestTools::editPreservesBom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "bom.txt"}, {"content", "\uFEFFhello world"}},
                             dir.path()).ok);
    QVERIFY(readFileBytes(dir.filePath(QStringLiteral("bom.txt"))).startsWith("\xEF\xBB\xBF"));

    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "bom.txt"}, {"old_string", "hello"}, {"new_string", "goodbye"}},
        dir.path());
    QVERIFY(edited.ok);

    const QByteArray raw = readFileBytes(dir.filePath(QStringLiteral("bom.txt")));
    QVERIFY(raw.startsWith("\xEF\xBB\xBF")); // BOM 原样保留
    QVERIFY(raw.contains("goodbye"));
}

void TestTools::editRejectsNoOpReplacement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "a.txt"}, {"content", "same text"}},
                             dir.path()).ok);
    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "a.txt"}, {"old_string", "same"}, {"new_string", "same"}},
        dir.path());
    QVERIFY(!edited.ok);
    QVERIFY(edited.output.contains(QStringLiteral("相同")));
}

void TestTools::editRejectsMissingNewString()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<EditTool>());

    QVERIFY(registry.execute(QStringLiteral("write"),
                             nlohmann::json{{"path", "a.txt"}, {"content", "keep me"}},
                             dir.path()).ok);
    const auto edited = registry.execute(
        QStringLiteral("edit"),
        nlohmann::json{{"path", "a.txt"}, {"old_string", "keep"}},
        dir.path());
    QVERIFY(!edited.ok); // new_string 缺失必须报错，而不是当作删除
    QCOMPARE(readFileBytes(dir.filePath(QStringLiteral("a.txt"))), QByteArray("keep me"));
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

void TestTools::bashTimesOutAndReports()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<BashTool>());

    const auto timedOut = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "sleep 5; echo finished"},
                       {"timeout_ms", 1000}},
        dir.path());
    QVERIFY(!timedOut.ok);
    QVERIFY(timedOut.output.contains(QStringLiteral("命令超时")));
    QVERIFY(!timedOut.output.contains(QStringLiteral("finished"))); // 命令确实被终止
}

void TestTools::bashMissingWorkdirFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<BashTool>());

    const auto failed = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "echo hi"}},
        dir.filePath(QStringLiteral("no-such-dir")));
    QVERIFY(!failed.ok);
    QVERIFY(failed.output.contains(QStringLiteral("工作目录不存在")));
}

void TestTools::bashEmptyOutputPlaceholder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<BashTool>());

    const auto silent = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "true"}},
        dir.path());
    QVERIFY(silent.ok);
    QVERIFY(silent.output.contains(QStringLiteral("(无输出)")));
}

void TestTools::bashKeepsTailOfHugeOutput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.registerTool(std::make_shared<BashTool>());

    const auto huge = registry.execute(
        QStringLiteral("bash"),
        nlohmann::json{{"command", "awk 'BEGIN{for(i=1;i<=20000;i++)print i}'"}},
        dir.path());
    QVERIFY(huge.ok);
    // 只保留末尾 50KB：末行可见，总长度有界，并给出完整输出转存路径
    QVERIFY(huge.output.contains(QStringLiteral("\n20000\n[输出共 ")));
    QVERIFY(huge.output.contains(QStringLiteral("完整输出：")));
    QVERIFY(huge.output.size() > 50 * 1024);
    QVERIFY(huge.output.size() < 54 * 1024);

    // 清理转存的临时文件
    const int begin = huge.output.indexOf(QStringLiteral("完整输出：")) 
                          + int(QStringLiteral("完整输出：").size());
    const int end = huge.output.indexOf(QLatin1Char(']'), begin);
    QVERIFY(begin > int(QStringLiteral("完整输出：").size()) && end > begin);
    QFile::remove(huge.output.mid(begin, end - begin));
}

void TestTools::resolveWorkdirExpandsTilde()
{
    QCOMPARE(resolveWorkdirPath(QStringLiteral("/w"), QStringLiteral("a.txt")),
             QDir(QStringLiteral("/w")).absoluteFilePath(QStringLiteral("a.txt")));
    QCOMPARE(resolveWorkdirPath(QStringLiteral("/w"), QStringLiteral("~")),
             QDir::homePath());
    QVERIFY(resolveWorkdirPath(QStringLiteral("/w"), QStringLiteral("~/x/y"))
                .startsWith(QDir::homePath()));
    QVERIFY(resolveWorkdirPath(QStringLiteral("/w"), QStringLiteral("~/x/y"))
                .endsWith(QStringLiteral("x/y")));
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
