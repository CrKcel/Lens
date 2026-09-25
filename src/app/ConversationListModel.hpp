#pragma once

#include "lens/core/Conversation.hpp"

#include <QAbstractListModel>
#include <QList>

namespace lens {

class SessionStore;

// 会话列表模型（ conversations 表的只读视图），增删改后由 ChatController 调 reload()
class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        WorkdirRole,
    };

    explicit ConversationListModel(SessionStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reload();

private:
    SessionStore *m_store;
    QList<Conversation> m_items;
};

} // namespace lens
