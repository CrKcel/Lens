#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include <lens/core/context/AgentDocs.hpp>
#include <lens/core/context/EnvironmentPrompt.hpp>
#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/context/Skills.hpp>

using namespace lens;

namespace {

bool writeFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return file.write(content.toUtf8()) >= 0;
}

} // namespace

class TestContext : public QObject
{
    Q_OBJECT

private slots:
    // —— AGENT.md / CLAUDE.md 发现与注入 ——

    void agentDocsProjectLevel();
    void agentDocsGlobalAndProject();
    void agentDocsAgentMdPreferredOverClaudeMd();
    void agentDocsMissingReturnsEmpty();

    // —— 环境提示词 ——

    void environmentPromptBasic();
    void environmentPromptGitRepository();
    void environmentPromptPlainDirectoryHasNoGit();

    // —— Skill 加载 ——

    void skillsDiscover();
    void skillsMissingDirectory();

    // —— PromptAssembler（上下文透明化的分节基础） ——

    void promptAssemblerSections();
};

void TestContext::agentDocsProjectLevel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFile(dir.filePath(QStringLiteral("AGENT.md")),
                      QStringLiteral("# 项目约定\n使用中文注释")));

    const auto docs = agentdocs::discover(dir.path(), QString());
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.first().scope, QStringLiteral("project"));
    QCOMPARE(docs.first().path, dir.filePath(QStringLiteral("AGENT.md")));
    QVERIFY(docs.first().content.contains(QStringLiteral("项目约定")));
}

void TestContext::agentDocsGlobalAndProject()
{
    QTemporaryDir globalDir, projectDir;
    QVERIFY(globalDir.isValid() && projectDir.isValid());
    QVERIFY(writeFile(globalDir.filePath(QStringLiteral("CLAUDE.md")),
                      QStringLiteral("全局风格说明")));
    QVERIFY(writeFile(projectDir.filePath(QStringLiteral("CLAUDE.md")),
                      QStringLiteral("项目说明")));

    const auto docs = agentdocs::discover(projectDir.path(), globalDir.path());
    QCOMPARE(docs.size(), 2);
    QCOMPARE(docs[0].scope, QStringLiteral("global"));
    QVERIFY(docs[0].content.contains(QStringLiteral("全局风格说明")));
    QCOMPARE(docs[1].scope, QStringLiteral("project"));
    QVERIFY(docs[1].content.contains(QStringLiteral("项目说明")));
}

void TestContext::agentDocsAgentMdPreferredOverClaudeMd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeFile(dir.filePath(QStringLiteral("AGENT.md")), QStringLiteral("A")));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("CLAUDE.md")), QStringLiteral("C")));

    const auto docs = agentdocs::discover(dir.path(), QString());
    QCOMPARE(docs.size(), 1);
    QVERIFY(docs.first().content.contains(QStringLiteral("A")));
}

void TestContext::agentDocsMissingReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(agentdocs::discover(dir.path(), dir.path()).isEmpty());
}

void TestContext::environmentPromptBasic()
{
    const QString prompt = envprompt::build(QString());
    QVERIFY(prompt.contains(QStringLiteral("## 运行环境")));
    QVERIFY(prompt.contains(QStringLiteral("操作系统：")));
    QVERIFY(prompt.contains(QStringLiteral("当前日期：")));
}

void TestContext::environmentPromptGitRepository()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString workdir = dir.path();
    auto run = [&workdir](const QStringList &args) {
        QProcess git;
        git.setWorkingDirectory(workdir);
        git.start(QStringLiteral("git"), args);
        git.waitForFinished(10000);
        return git.exitCode() == 0 && git.exitStatus() == QProcess::NormalExit;
    };
    QVERIFY(run({"init", "-q"}));
    // --no-gpg-sign：本机 git 全局可能配置 commit.gpgsign，签名会拖住测试进程
    QVERIFY(run({"-c", "user.email=t@t", "-c", "user.name=t", "commit", "--allow-empty",
                 "--no-gpg-sign", "-q", "-m", "init"}));
    QVERIFY(writeFile(workdir + QStringLiteral("/a.txt"), QStringLiteral("dirty")));

    const QString prompt = envprompt::build(workdir);
    QVERIFY(prompt.contains(QStringLiteral("工作文件夹：")));
    QVERIFY(prompt.contains(QStringLiteral("Git 分支：")));
    QVERIFY(prompt.contains(QStringLiteral("未提交变更")));
}

void TestContext::environmentPromptPlainDirectoryHasNoGit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!envprompt::build(dir.path()).contains(QStringLiteral("Git 分支：")));
    QVERIFY(envprompt::build(dir.path()).contains(QStringLiteral("工作文件夹：")));
    QVERIFY(!envprompt::build(QString()).contains(QStringLiteral("工作文件夹：")));
}

void TestContext::skillsDiscover()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString skillDir = dir.filePath(QStringLiteral("code-review"));
    QVERIFY(QDir().mkpath(skillDir));
    QVERIFY(writeFile(skillDir + QStringLiteral("/SKILL.md"),
                      QStringLiteral("---\nname: code-review\ndescription: 审查代码质量\n---\n"
                                     "# 代码审查\n逐步检查……")));
    // 无 frontmatter 的技能：回退目录名
    const QString bareDir = dir.filePath(QStringLiteral("refactor"));
    QVERIFY(QDir().mkpath(bareDir));
    QVERIFY(writeFile(bareDir + QStringLiteral("/SKILL.md"), QStringLiteral("# 重构")));

    // 无 SKILL.md 的目录被忽略
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("empty"))));

    const auto skills = skills::discover(dir.path());
    QCOMPARE(skills.size(), 2);
    const skills::Skill named = *std::find_if(skills.cbegin(), skills.cend(),
                                              [](const skills::Skill &s) {
                                                  return s.name == QLatin1String("code-review");
                                              });
    QCOMPARE(named.description, QStringLiteral("审查代码质量"));
    QVERIFY(named.path.endsWith(QStringLiteral("SKILL.md")));

    const skills::Skill bare =
        *std::find_if(skills.cbegin(), skills.cend(),
                      [](const skills::Skill &s) { return s.name == QLatin1String("refactor"); });
    QVERIFY(bare.description.isEmpty());
}

void TestContext::skillsMissingDirectory()
{
    QVERIFY(skills::discover(QStringLiteral("/nonexistent-lens-skills")).isEmpty());
}

void TestContext::promptAssemblerSections()
{
    PromptAssembler assembler;
    assembler.setSection(QStringLiteral("identity"), QStringLiteral("身份"));
    assembler.setSection(QStringLiteral("workspace"), QStringLiteral("工作区"));
    // 同名覆盖保持原顺序
    assembler.setSection(QStringLiteral("identity"), QStringLiteral("身份2"));
    assembler.setSection(QStringLiteral("custom"), QString()); // 空段不拼入

    const QStringList names = assembler.sectionNames();
    QCOMPARE(names, QStringList({"identity", "workspace", "custom"}));
    QCOMPARE(assembler.assemble(), QStringLiteral("身份2\n\n工作区"));
}

QTEST_GUILESS_MAIN(TestContext)
#include "test_context.moc"
