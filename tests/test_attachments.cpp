#include <lens/core/attachments/Attachments.hpp>

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace lens;
using namespace lens::attachments;

namespace {

QVariantMap entry(const QString &url, const QString &name = {})
{
    QVariantMap map{{QStringLiteral("url"), url}};
    if (!name.isEmpty())
        map.insert(QStringLiteral("name"), name);
    return map;
}

// 最小 PNG：魔数命中 sniffImageMime 即可，内容本身不重要
QByteArray pngHead()
{
    return QByteArrayLiteral("\x89PNG\r\n\x1a\n")
        + QByteArray(64, '\0');
}

} // namespace

class TestAttachments : public QObject
{
    Q_OBJECT

private slots:
    void dataUrlImage();
    void fileImageByMagic();
    void textFile();
    void textFileExplicitName();
    void rejectsEmptyBinaryAndInvalidUtf8();
    void rejectsOversize();
    void skipsMissingAndEmptyEntries();
};

void TestAttachments::dataUrlImage()
{
    const QByteArray bytes = pngHead();
    const QString url = QStringLiteral("data:image/png;base64,")
        + QString::fromLatin1(bytes.toBase64());
    const LoadedAttachments loaded = loadAttachments({entry(url)});
    QCOMPARE(loaded.images.size(), 1);
    QCOMPARE(loaded.images.first().mimeType, QStringLiteral("image/png"));
    QCOMPARE(loaded.images.first().data, bytes);
    QVERIFY(loaded.files.isEmpty());
}

void TestAttachments::fileImageByMagic()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("pic.bin")); // 扩展名不参与分类
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(pngHead());
    file.close();

    const LoadedAttachments loaded = loadAttachments({entry(path)});
    QCOMPARE(loaded.images.size(), 1);
    QCOMPARE(loaded.images.first().mimeType, QStringLiteral("image/png"));
    QVERIFY(loaded.files.isEmpty());
}

void TestAttachments::textFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("note.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("你好，附件\nsecond line\n");
    file.close();

    const LoadedAttachments loaded = loadAttachments({entry(path)});
    QVERIFY(loaded.images.isEmpty());
    QCOMPARE(loaded.files.size(), 1);
    QCOMPARE(loaded.files.first().fileName, QStringLiteral("note.txt"));
    QCOMPARE(loaded.files.first().content, QStringLiteral("你好，附件\nsecond line\n"));
}

void TestAttachments::textFileExplicitName()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    const LoadedAttachments loaded =
        loadAttachments({entry(path, QStringLiteral("自定义.txt"))});
    QCOMPARE(loaded.files.size(), 1);
    QCOMPARE(loaded.files.first().fileName, QStringLiteral("自定义.txt"));
}

void TestAttachments::rejectsEmptyBinaryAndInvalidUtf8()
{
    QTemporaryDir dir;
    const QString empty = dir.filePath(QStringLiteral("empty.txt"));
    {
        QFile file(empty);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    const QString binary = dir.filePath(QStringLiteral("blob.dat"));
    {
        QFile file(binary);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("a\0b", 3);
    }

    const QString invalid = dir.filePath(QStringLiteral("bad.txt"));
    {
        QFile file(invalid);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\xff\xfe\xfa");
    }

    const LoadedAttachments loaded = loadAttachments(
        {entry(empty), entry(binary), entry(invalid)});
    QVERIFY(loaded.images.isEmpty());
    QVERIFY(loaded.files.isEmpty());
}

void TestAttachments::rejectsOversize()
{
    QTemporaryDir dir;
    // 文本 1MB 上限
    const QString bigText = dir.filePath(QStringLiteral("big.txt"));
    {
        QFile file(bigText);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1024 * 1024 + 1, 'a'));
    }
    const LoadedAttachments loaded = loadAttachments({entry(bigText)});
    QVERIFY(loaded.images.isEmpty());
    QVERIFY(loaded.files.isEmpty());

    // 图片 5MB 上限：数据 URL 形态（文件形态读入 kMaxImageBytes+1 后同样拒绝，
    // 属同一分支，这里只验 data URL 路径的完整链路）
    const QByteArray bigPng = QByteArrayLiteral("\x89PNG\r\n\x1a\n")
        + QByteArray(5 * 1024 * 1024 + 1, '\0');
    const QString url = QStringLiteral("data:image/png;base64,")
        + QString::fromLatin1(bigPng.toBase64());
    const LoadedAttachments loadedImage = loadAttachments({entry(url)});
    QVERIFY(loadedImage.images.isEmpty());
}

void TestAttachments::skipsMissingAndEmptyEntries()
{
    const LoadedAttachments loaded = loadAttachments(
        {entry(QStringLiteral("/nonexistent/path/x.txt")), entry(QStringLiteral("")),
         QVariant() // 旧形态之外的空条目
        });
    QVERIFY(loaded.images.isEmpty());
    QVERIFY(loaded.files.isEmpty());
}

QTEST_MAIN(TestAttachments)
#include "test_attachments.moc"
