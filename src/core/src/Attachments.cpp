#include "lens/core/attachments/Attachments.hpp"

#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QUrl>

namespace lens::attachments {
namespace {

constexpr qint64 kMaxImageBytes = 5 * 1024 * 1024; // 主流 API 的单图上限
constexpr qint64 kMaxTextBytes = 1024 * 1024;      // 单文本附件上限

bool loadImageDataUrl(const QString &value, QList<ImageAttachment> &out)
{
    // data:<mime>;base64,<payload>
    ImageAttachment image;
    image.mimeType = value.mid(5, value.indexOf(QLatin1Char(';')) - 5);
    image.data =
        QByteArray::fromBase64(value.section(QLatin1String("base64,"), 1).toLatin1());
    if (image.mimeType.isEmpty() || image.data.isEmpty()) {
        qWarning("附件 %s 不是受支持的图片（png/jpg/gif/webp/bmp），已跳过", qPrintable(value));
        return false;
    }
    if (image.data.size() > kMaxImageBytes) {
        qWarning("附件 %s 超过 %lldMB 上限，已跳过", qPrintable(value),
                 kMaxImageBytes / (1024 * 1024));
        return false;
    }
    out.append(std::move(image));
    return true;
}

} // namespace

LoadedAttachments loadAttachments(const QVariantList &entries)
{
    LoadedAttachments result;
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        const QString value =
            map.isEmpty() ? entry.toString() : map.value(QStringLiteral("url")).toString();
        if (value.isEmpty())
            continue;
        if (value.startsWith(QLatin1String("data:"))) {
            loadImageDataUrl(value, result.images);
            continue;
        }
        const QString path =
            value.startsWith(QLatin1String("file:")) ? QUrl(value).toLocalFile() : value;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("附件 %s 无法打开，已跳过", qPrintable(path));
            continue;
        }
        const QByteArray head = file.peek(8192);
        if (const QString mime = sniffImageMime(head, file.size()); !mime.isEmpty()) {
            ImageAttachment image;
            image.mimeType = mime;
            image.data = file.read(kMaxImageBytes + 1);
            if (image.data.isEmpty()) {
                qWarning("附件 %s 无法读取，已跳过", qPrintable(path));
                continue;
            }
            if (image.data.size() > kMaxImageBytes) {
                qWarning("附件 %s 超过 %lldMB 上限，已跳过", qPrintable(path),
                         kMaxImageBytes / (1024 * 1024));
                continue;
            }
            result.images.append(std::move(image));
            continue;
        }
        if (file.size() > kMaxTextBytes) {
            qWarning("附件 %s 超过 1MB 文本上限，已跳过", qPrintable(path));
            continue;
        }
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) {
            qWarning("附件 %s 是空文件，已跳过", qPrintable(path));
            continue;
        }
        if (bytes.contains('\0')) {
            qWarning("附件 %s 是二进制文件，已跳过", qPrintable(path));
            continue;
        }
        QStringDecoder decoder(QStringConverter::Utf8);
        const QString content = decoder.decode(bytes);
        if (decoder.hasError()) {
            qWarning("附件 %s 不是合法 UTF-8 文本，已跳过", qPrintable(path));
            continue;
        }
        QString fileName = map.value(QStringLiteral("name")).toString();
        if (fileName.isEmpty())
            fileName = QFileInfo(path).fileName();
        result.files.append(TextAttachment{fileName, content});
    }
    return result;
}

} // namespace lens::attachments
