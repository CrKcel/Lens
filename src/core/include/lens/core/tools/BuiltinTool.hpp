#pragma once

#include "lens/core/Conversation.hpp"
#include "lens/core/tools/ToolArgs.hpp"
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>
#include <nlohmann/json.hpp>

namespace lens {

struct ToolResult {
    bool ok = true;
    QString output;
    QList<ImageAttachment> images = {}; // 多模态工具（如 read）返回的图片，随工具结果发给模型
};

// 工具描述符：name 与 JSON Schema 会被拼进请求的 tools 字段，
struct ToolSpec {
    QString name;
    QString description;
    nlohmann::json parameters; // JSON Schema 对象
};

// 内置工具接口：spec() 提供给请求的 tools 字段与上下文透明化展示，
// execute() 在工作线程被调用（实现需保证线程安全，不要触碰 UI）。
class IBuiltinTool
{
public:
    virtual ~IBuiltinTool() = default;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual nlohmann::json parametersSchema() const = 0;
    virtual ToolResult execute(const nlohmann::json &args, const QString &workdir) = 0;

    ToolSpec spec() const
    {
        return {name(), description(), parametersSchema()};
    }
};

} // namespace lens
