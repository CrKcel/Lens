#include "MessageListModel.hpp"

#include <QSet>

namespace lens {

namespace {

QVariantList imageDataUrls(const QList<ImageAttachment> &images)
{
    QVariantList result;
    for (const ImageAttachment &image : images)
        result.append(imageDataUrl(image));
    return result;
}

} // namespace

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case KindRole: return item.kind;
    case TextRole: return item.text;
    case ToolNameRole: return item.toolName;
    case ToolArgsRole: return item.toolArgs;
    case ToolPendingRole: return item.toolPending;
    case StreamingRole: return item.streaming;
    case ReasoningRole: return item.reasoning;
    case ImagesRole: return item.images;
    }
    return {};
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {{KindRole, "kind"},
            {TextRole, "text"},
            {ToolNameRole, "toolName"},
            {ToolArgsRole, "toolArgs"},
            {ToolPendingRole, "toolPending"},
            {StreamingRole, "streaming"},
            {ReasoningRole, "reasoning"},
            {ImagesRole, "images"}};
}

void MessageListModel::resetFromMessages(const QList<Message> &messages)
{
    beginResetModel();
    m_items.clear();
    m_streamingIndex = -1;

    // 工具结果先建索引，用于把结果回填到对应工具卡片
    QHash<QString, QPair<QString, QVariantList>> toolResults;
    for (const Message &message : messages) {
        if (message.role == Role::Tool)
            toolResults.insert(message.toolCallId,
                               {message.content, imageDataUrls(message.images)});
    }
    QSet<QString> consumedResults;

    for (const Message &message : messages) {
        switch (message.role) {
        case Role::User:
            m_items.append({User, message.content, {}, {}, {}, false, false, {},
                            imageDataUrls(message.images)});
            break;
        case Role::Assistant:
            if (!message.content.isEmpty() || !message.reasoning.isEmpty())
                m_items.append({Assistant, message.content, {}, {}, {}, false, false,
                                message.reasoning, {}});
            for (const ToolCall &call : message.toolCalls) {
                Item item;
                item.kind = ToolCallItem;
                item.toolName = call.name;
                item.toolArgs = call.arguments;
                item.toolCallId = call.id;
                if (toolResults.contains(call.id)) {
                    item.text = toolResults.value(call.id).first;
                    item.images = toolResults.value(call.id).second;
                    consumedResults.insert(call.id);
                } else {
                    item.toolPending = true;
                }
                m_items.append(item);
            }
            break;
        case Role::Tool:
            if (!consumedResults.contains(message.toolCallId)) // 无主结果兜底显示
                m_items.append({ToolCallItem, message.content, QStringLiteral("tool"),
                                message.toolCallId, message.toolCallId, false, false, {},
                                imageDataUrls(message.images)});
            break;
        case Role::System:
            break;
        }
    }
    endResetModel();
}

int MessageListModel::appendItem(const Item &item)
{
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    m_items.append(item);
    endInsertRows();
    if (item.streaming)
        m_streamingIndex = row; // 登记流式行：后续增量都落在这里
    return row;
}

void MessageListModel::appendDelta(const QString &delta)
{
    if (m_streamingIndex < 0 || m_streamingIndex >= m_items.size())
        return;
    m_items[m_streamingIndex].text += delta;
    const QModelIndex index = createIndex(m_streamingIndex, 0);
    emit dataChanged(index, index, {TextRole});
}

void MessageListModel::appendReasoningDelta(const QString &delta)
{
    if (m_streamingIndex < 0 || m_streamingIndex >= m_items.size())
        return;
    m_items[m_streamingIndex].reasoning += delta;
    const QModelIndex index = createIndex(m_streamingIndex, 0);
    emit dataChanged(index, index, {ReasoningRole});
}

void MessageListModel::finishStreamingRow(const QString &finalText,
                                          const QString &finalReasoning)
{
    if (m_streamingIndex < 0 || m_streamingIndex >= m_items.size()) {
        m_streamingIndex = -1;
        return;
    }
    m_items[m_streamingIndex].text = finalText;
    m_items[m_streamingIndex].reasoning = finalReasoning;
    m_items[m_streamingIndex].streaming = false;
    const QModelIndex index = createIndex(m_streamingIndex, 0);
    emit dataChanged(index, index, {TextRole, ReasoningRole, StreamingRole});
    m_lastFinalizedRow = m_streamingIndex;
    m_streamingIndex = -1;
}

void MessageListModel::dropEmptyStreamingRow()
{
    const int row = m_lastFinalizedRow;
    m_lastFinalizedRow = -1;
    if (row < 0 || row >= m_items.size())
        return;
    const Item &item = m_items.at(row);
    if (item.kind == MessageListModel::Assistant && item.text.isEmpty()
        && item.reasoning.isEmpty()) {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
    }
}

void MessageListModel::setToolCallRunning(const QString &callId)
{
    const int row = findIndexByToolCallId(callId);
    if (row < 0)
        return;
    m_items[row].toolPending = true;
    emit dataChanged(createIndex(row, 0), createIndex(row, 0), {ToolPendingRole});
}

void MessageListModel::setToolCallResult(const QString &callId, const QString &output)
{
    setToolCallResult(callId, output, {});
}

void MessageListModel::setToolCallResult(const QString &callId, const QString &output,
                                         const QList<ImageAttachment> &images)
{
    const int row = findIndexByToolCallId(callId);
    if (row < 0)
        return;
    m_items[row].toolPending = false;
    m_items[row].text = output;
    m_items[row].images = imageDataUrls(images);
    emit dataChanged(createIndex(row, 0), createIndex(row, 0),
                     {ToolPendingRole, TextRole, ImagesRole});
}

int MessageListModel::findIndexByToolCallId(const QString &callId) const
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).toolCallId == callId)
            return i;
    }
    return -1;
}

} // namespace lens
