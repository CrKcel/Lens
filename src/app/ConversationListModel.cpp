#include "ConversationListModel.hpp"

#include <lens/core/storage/SessionStore.hpp>

namespace lens {

ConversationListModel::ConversationListModel(SessionStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    reload();
}

int ConversationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant ConversationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Conversation &conversation = m_items.at(index.row());
    switch (role) {
    case IdRole: return conversation.id;
    case TitleRole: return conversation.title;
    case WorkdirRole: return conversation.workdir;
    }
    return {};
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {{IdRole, "conversationId"},
            {TitleRole, "title"},
            {WorkdirRole, "workdir"}};
}

void ConversationListModel::reload()
{
    beginResetModel();
    m_items = m_store->conversations();
    endResetModel();
}

} // namespace lens
