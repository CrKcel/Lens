#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QString>

namespace lens::skills {

// 一个可加载的技能包：提示词为主，必要时配合工具使用。
// 正文不直接注入上下文——把路径交给 Agent，需要时用 read 工具按需读取。
struct Skill {
    QString name;        // 来自 frontmatter 的 name，缺省为目录名
    QString description; // 来自 frontmatter 的 description
    QString path;        // SKILL.md 绝对路径
};

namespace detail {

// 解析 SKILL.md 的 YAML frontmatter（--- 包裹的 name/description 两行），
// 正文与 frontmatter 格式不做严格校验：缺字段时回退到目录名。
inline Skill parseSkillMd(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(file.readAll());

    Skill skill;
    skill.path = path;
    skill.name = QFileInfo(QFileInfo(path).absolutePath()).fileName();

    if (!content.startsWith(QStringLiteral("---\n"))
        && !content.startsWith(QStringLiteral("---\r\n")))
        return skill;
    // frontmatter = 开头 --- 与闭合 --- 之间的行
    const int body = content.indexOf(QLatin1String("---"), 4);
    if (body < 0)
        return skill;
    const QString frontmatter = content.mid(4, body - 4);
    for (const QString &line : frontmatter.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("name") && !value.isEmpty())
            skill.name = value;
        else if (key == QLatin1String("description"))
            skill.description = value;
    }
    return skill;
}

} // namespace detail

// 发现 dir 下每个子目录中的 SKILL.md；dir 不存在时返回空。
inline QList<Skill> discover(const QString &dir)
{
    QList<Skill> skills;
    const QDir root(dir);
    if (!root.exists())
        return skills;
    const QFileInfoList entries =
        root.entryInfoList(QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString skillPath = QDir(entry.absoluteFilePath()).absoluteFilePath(
            QStringLiteral("SKILL.md"));
        if (!QFileInfo::exists(skillPath))
            continue;
        Skill skill = detail::parseSkillMd(skillPath);
        if (!skill.path.isEmpty())
            skills.append(std::move(skill));
    }
    return skills;
}

} // namespace lens::skills
