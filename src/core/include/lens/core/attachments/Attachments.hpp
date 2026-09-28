#pragma once

#include "lens/core/Conversation.hpp"

#include <QList>
#include <QString>
#include <QVariantList>

namespace lens::attachments {

// loadAttachments 的结果：图片与文本附件分类收集
struct LoadedAttachments {
    QList<ImageAttachment> images;
    QList<TextAttachment> files;
};

// 加载 QML 传入的附件条目：{url, name, isImage}（QVariantMap），兼容旧纯字符串
// 路径/data URL。分类以嗅探为准（QML 的 isImage 只影响预览渲染）：
// data URL 或文件魔数命中 → 图片（单图 5MB 上限，主流 API 的上限）；
// 否则非空、无 NUL 且为合法 UTF-8 → 文本附件（单文件 1MB 上限；空文件拒绝，
// 因为持久化回读会被 filesFromJson 丢弃，会话重启后附件不一致）；
// 其余拒绝。无法识别/越限的条目跳过并告警
LoadedAttachments loadAttachments(const QVariantList &entries);

} // namespace lens::attachments
