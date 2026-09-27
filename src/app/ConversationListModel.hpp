#pragma once

#include "lens/core/Conversation.hpp"

#include <QAbstractListModel>
#include <QList>
#include <QSet>

namespace lens {

class SessionStore;

// 会话列表模型（conversations 表的只读视图），增删改后由 ChatController 调 reload()。
// 展示为按工作文件夹分组的扁平结构：每个分组先输出一行组头（isHeader=true，
// 带 workdir 与组内会话数），随后是组内会话行。组可折叠（toggleGroup，状态只在
// 内存），搜索过滤期间强制全部展开避免结果被藏住
class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        WorkdirRole,
        IsHeaderRole,
        GroupCountRole, // 仅组头行：组内会话数
    };

    explicit ConversationListModel(SessionStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 搜索关键词：非空时只显示标题或消息内容命中的会话
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);

    void reload();

    // 折叠/展开某工作文件夹的分组
    Q_INVOKABLE void toggleGroup(const QString &workdir);
    Q_INVOKABLE bool isGroupCollapsed(const QString &workdir) const;
    // 展开分组（无副作用可重复调用）：新建会话落入折叠组时保持可见
    void expandGroup(const QString &workdir);

signals:
    void filterChanged();

private:
    // 扁平化后的一行：组头或会话；组头行只有 workdir 有效
    struct Entry {
        bool isHeader = false;
        Conversation conversation = {};
        int groupCount = 0;
    };

    SessionStore *m_store;
    QString m_filter;
    QSet<QString> m_collapsed;
    QList<Entry> m_entries;
};

} // namespace lens
