#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QMutex>
#include <QString>

namespace lens {

// web_search：调用 Tavily /search 兼容的 HTTP 搜索接口（Bearer 鉴权）。
// endpoint/apiKey 由应用层在注册时注入（AppSettings 的 webSearch 配置）；
// 未配置时返回说明性错误，模型据此提示用户去设置。
class WebSearchTool : public IBuiltinTool
{
public:
    void setConfig(const QString &endpoint, const QString &apiKey)
    {
        QMutexLocker locker(&m_mutex);
        m_endpoint = endpoint;
        m_apiKey = apiKey;
    }

    QString name() const override { return QStringLiteral("web_search"); }

    QString description() const override
    {
        return QStringLiteral(
            "搜索互联网以获取实时信息。适用于最新动态、文档查询、事实核对等场景。"
            "query 为搜索关键词，可附上期望的时间范围或限定词。");
    }

    nlohmann::json parametersSchema() const override
    {
        return nlohmann::json{
            {"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}, {"description", "搜索关键词"}}}}},
            {"required", nlohmann::json::array({"query"})}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override;

private:
    QMutex m_mutex;
    QString m_endpoint;
    QString m_apiKey;
};

} // namespace lens
