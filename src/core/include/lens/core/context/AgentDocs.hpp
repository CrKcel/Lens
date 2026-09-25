#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

namespace lens::agentdocs {

// 一份注入上下文的 Agent 说明文档
struct AgentDoc {
    QString scope;   // "global"（全局）或 "project"（项目级）
    QString path;    // 文档绝对路径
    QString content; // 文档正文
};

namespace detail {

inline AgentDoc readDoc(const QString &scope, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    AgentDoc doc;
    doc.scope = scope;
    doc.path = path;
    doc.content = QString::fromUtf8(file.readAll());
    return doc;
}

inline AgentDoc findDoc(const QString &scope, const QString &directory,
                        const QStringList &candidates)
{
    if (directory.trimmed().isEmpty())
        return {};
    for (const QString &name : candidates) {
        const QString path = QDir(directory).absoluteFilePath(name);
        if (QFileInfo::exists(path))
            return readDoc(scope, path);
    }
    return {};
}

} // namespace detail

// 发现并读取 AGENT.md / CLAUDE.md：全局目录与项目 workdir 各取第一个存在的
// 候选文件（同级 AGENT.md 优先于 CLAUDE.md），返回按 全局 → 项目 排列的非空文档。
inline QList<AgentDoc> discover(const QString &workdir, const QString &globalDir)
{
    const QStringList kCandidates = {QStringLiteral("AGENT.md"), QStringLiteral("CLAUDE.md")};
    QList<AgentDoc> docs;
    auto appendIfFound = [&docs, &kCandidates](const QString &scope, const QString &dir) {
        AgentDoc doc = detail::findDoc(scope, dir, kCandidates);
        if (!doc.path.isEmpty())
            docs.append(std::move(doc));
    };
    appendIfFound(QStringLiteral("global"), globalDir);
    appendIfFound(QStringLiteral("project"), workdir);
    return docs;
}

} // namespace lens::agentdocs
