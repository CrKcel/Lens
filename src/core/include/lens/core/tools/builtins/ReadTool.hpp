#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QFile>
#include <QStringList>

namespace lens {

// 读取文本文件。offset/limit 按 1 起始的行号选取，默认读前 2000 行；
// 选中的内容超过 50KB 时再按字节截断（对齐主流实现的 2000 行 / 50KB 双上限）。
// 图片与二进制文件（魔数 / NUL 字节嗅探）拒绝读取，避免把乱码灌进上下文。
// 输出为纯文本，便于 edit 工具用原文片段匹配。
class ReadTool final : public IBuiltinTool
{
public:
    QString name() const override { return QStringLiteral("read"); }
    QString description() const override
    {
        return QStringLiteral("读取工作文件夹中的文本文件内容（图片和二进制文件不支持）");
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "文件路径，相对路径以工作文件夹为基准"}}},
                  {"offset", {{"type", "integer"}, {"description", "起始行号（从 1 开始），默认 1"}}},
                  {"limit", {{"type", "integer"}, {"description", "最多读取行数，默认 2000"}}}}},
                {"required", {"path"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString path = argString(args, "path");
        if (path.isEmpty())
            return {false, QStringLiteral("缺少 path 参数")};
        const QString resolved = resolveWorkdirPath(workdir, path);

        QFile file(resolved);
        if (!file.open(QIODevice::ReadOnly))
            return {false, QStringLiteral("无法读取文件 %1：%2").arg(resolved, file.errorString())};

        if (const QString binaryNote = binaryKind(file.peek(kSniffBytes)); !binaryNote.isEmpty())
            return {false,
                    QStringLiteral("无法读取 %1：%2。read 工具仅支持文本内容，"
                                   "可用 bash 工具检查该文件（如 file、xxd、ls -la）")
                        .arg(resolved, binaryNote)};

        // 为支持 offset 选取，最多扫描前 2MB；更大的文件只暴露前 2MB 并给出提示
        const QByteArray raw = file.read(kMaxScanBytes);
        const bool scanTruncated = file.bytesAvailable() > 0;
        file.close();

        if (raw.isEmpty())
            return {true, QStringLiteral("(空文件)")};

        QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
        const int totalLines = lines.size();

        const int requestedOffset = argInt(args, "offset", 1);
        if (requestedOffset > totalLines) {
            return {false,
                    QStringLiteral("offset %1 超出文件末尾（可读范围共 %2 行）")
                        .arg(requestedOffset)
                        .arg(totalLines)};
        }
        const int offset = qBound(1, requestedOffset, totalLines);
        const int limit = qBound(1, argInt(args, "limit", 2000), 5000);
        const int end = qMin(offset - 1 + limit, totalLines);

        QStringList picked;
        qint64 bytes = 0;
        bool byteCut = false;
        bool singleLineCut = false;
        for (int i = offset - 1; i < end; ++i) {
            const QByteArray lineBytes = lines.at(i).toUtf8();
            if (bytes + lineBytes.size() + 1 > kMaxOutputBytes) {
                byteCut = true;
                if (picked.isEmpty()) { // 单行超限：截取该行前 50KB
                    picked.append(QString::fromUtf8(lineBytes.left(kMaxOutputBytes)));
                    singleLineCut = true;
                }
                break;
            }
            picked.append(lines.at(i));
            bytes += lineBytes.size() + 1;
        }

        QString output = picked.join(QLatin1Char('\n'));
        if (singleLineCut)
            output += QStringLiteral("\n…[第 %1 行超过 %2KB，已截断；可用 bash 工具分段查看（如 head -c）]")
                          .arg(offset)
                          .arg(kMaxOutputBytes / 1024);
        else if (byteCut)
            output += QStringLiteral("\n…[输出超过 %1KB，已截断]").arg(kMaxOutputBytes / 1024);
        if (offset - 1 + limit < totalLines && !byteCut)
            output += QStringLiteral("\n…[仅显示第 %1~%2 行，共 %3 行，可用 offset/limit 继续读取]")
                          .arg(offset).arg(offset - 1 + limit).arg(totalLines);
        if (scanTruncated)
            output += QStringLiteral("\n…[文件超过 %1MB，仅扫描前 %1MB]")
                          .arg(kMaxScanBytes / (1024 * 1024));
        return {true, output};
    }

private:
    static constexpr qint64 kMaxOutputBytes = 50 * 1024;
    static constexpr qint64 kMaxScanBytes = 2 * 1024 * 1024;
    static constexpr int kSniffBytes = 8192;

    // 返回人类可读的文件类型描述；文本文件返回空
    static QString binaryKind(const QByteArray &head)
    {
        if (head.startsWith("\xFF\xD8\xFF"))
            return QStringLiteral("JPEG 图片");
        if (head.startsWith("\x89PNG\r\n\x1A\n"))
            return QStringLiteral("PNG 图片");
        if (head.startsWith("GIF87a") || head.startsWith("GIF89a"))
            return QStringLiteral("GIF 图片");
        if (head.size() >= 12 && head.startsWith("RIFF") && head.mid(8, 4) == "WEBP")
            return QStringLiteral("WebP 图片");
        if (head.startsWith("BM"))
            return QStringLiteral("BMP 图片");
        if (head.contains('\0'))
            return QStringLiteral("二进制文件");
        return {};
    }
};

} // namespace lens
