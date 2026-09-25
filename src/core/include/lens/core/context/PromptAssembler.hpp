#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace lens {

class PromptAssembler
{
public:
    // 同名段落覆盖内容并保持原顺序（Agent.md 热重载等场景依赖此语义）
    void setSection(const QString &name, const QString &content)
    {
        for (auto &[sectionName, sectionContent] : m_sections) {
            if (sectionName == name) {
                sectionContent = content;
                return;
            }
        }
        m_sections.append({name, content});
    }

    // 拼装最终系统提示词：非空段落按插入顺序以空行连接
    QString assemble() const
    {
        QStringList parts;
        for (const auto &[sectionName, sectionContent] : m_sections) {
            const QString trimmed = sectionContent.trimmed();
            if (!trimmed.isEmpty())
                parts.append(trimmed);
        }
        return parts.join(QStringLiteral("\n\n"));
    }

    // 全部段落名（含空段落）——上下文透明化时逐段展示
    QStringList sectionNames() const
    {
        QStringList names;
        for (const auto &[sectionName, sectionContent] : m_sections)
            names.append(sectionName);
        return names;
    }

private:
    QList<QPair<QString, QString>> m_sections;
};

} // namespace lens
