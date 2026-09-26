#pragma once

#include "lens/core/Conversation.hpp"

#include <QAbstractListModel>
#include <QList>

namespace lens {

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Kind { User, Assistant, ToolCallItem, Error };
    Q_ENUM(Kind)

    enum Roles {
        KindRole = Qt::UserRole + 1,
        TextRole,
        ToolNameRole,
        ToolArgsRole,
        ToolPendingRole,
        StreamingRole,
        ReasoningRole,
        ImagesRole, // QVariantList：data URL 字符串，供 QML Image 显示
        FilesRole,  // QVariantList：{name} map，供 QML 渲染文本附件 chip
    };

    struct Item {
        Kind kind = Assistant;
        QString text;
        QString toolName;
        QString toolArgs;
        QString toolCallId;
        bool toolPending = false;
        bool streaming = false;
        QString reasoning;  // Assistant：思考过程
        QVariantList images = {}; // 随消息展示的图片（data URL）
        QVariantList files = {};  // 随消息展示的文本附件（{name}）
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void resetFromMessages(const QList<Message> &messages);
    int appendItem(const Item &item);
    void appendDelta(const QString &delta);          // 落在最后一个流式行
    void appendReasoningDelta(const QString &delta); // 落在最后一个流式行
    void finishStreamingRow(const QString &finalText, const QString &finalReasoning);
    void setToolCallRunning(const QString &callId);
    void setToolCallResult(const QString &callId, const QString &output);
    void setToolCallResult(const QString &callId, const QString &output,
                           const QList<ImageAttachment> &images);
    bool hasStreamingRow() const { return m_streamingIndex >= 0; }
    void dropEmptyStreamingRow();

private:
    int findIndexByToolCallId(const QString &callId) const;

    QList<Item> m_items;
    int m_streamingIndex = -1;
    int m_lastFinalizedRow = -1; // finishStreamingRow 后供 dropEmptyStreamingRow 使用
};

} // namespace lens
