#include "ConversationListModel.hpp"

#include <lens/core/storage/SessionStore.hpp>

#include <algorithm>

namespace lens {

ConversationListModel::ConversationListModel(SessionStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    reload();
}

int ConversationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ConversationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole: return entry.isHeader ? 0 : entry.conversation.id;
    case TitleRole: return entry.isHeader ? QString() : entry.conversation.title;
    case WorkdirRole: return entry.conversation.workdir;
    case IsHeaderRole: return entry.isHeader;
    case GroupCountRole: return entry.isHeader ? entry.groupCount : 0;
    }
    return {};
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {{IdRole, "conversationId"},
            {TitleRole, "title"},
            {WorkdirRole, "workdir"},
            {IsHeaderRole, "isHeader"},
            {GroupCountRole, "groupCount"}};
}

void ConversationListModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    reload();
}

void ConversationListModel::toggleGroup(const QString &workdir)
{
    if (!m_collapsed.remove(workdir))
        m_collapsed.insert(workdir);
    reload();
}

bool ConversationListModel::isGroupCollapsed(const QString &workdir) const
{
    return m_collapsed.contains(workdir);
}

void ConversationListModel::expandGroup(const QString &workdir)
{
    m_collapsed.remove(workdir);
}

void ConversationListModel::reload()
{
    beginResetModel();
    m_entries.clear();
    QList<Conversation> items = m_filter.trimmed().isEmpty()
                                    ? m_store->conversations()
                                    : m_store->searchConversations(m_filter);
    // 分组展示须按 workdir 排序（store 按 updated_at 返回）；
    // 稳定排序保持 store 返回的组内 updated_at 倒序
    std::stable_sort(items.begin(), items.end(),
                     [](const Conversation &a, const Conversation &b) {
                         return a.workdir.compare(b.workdir, Qt::CaseInsensitive) < 0;
                     });
    const bool searching = !m_filter.trimmed().isEmpty();
    for (int start = 0; start < items.size();) {
        const QString &workdir = items[start].workdir;
        int end = start + 1;
        while (end < items.size() && items[end].workdir == workdir)
            ++end;
        Entry header;
        header.isHeader = true;
        header.conversation.workdir = workdir;
        header.groupCount = end - start;
        m_entries.push_back(header);
        // 搜索期间强制展开，避免命中的会话被折叠组藏住
        if (searching || !m_collapsed.contains(workdir)) {
            for (int i = start; i < end; ++i)
                m_entries.push_back(Entry{false, items[i], 0});
        }
        start = end;
    }
    endResetModel();
}

} // namespace lens
