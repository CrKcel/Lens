#pragma once

#include "ITransport.hpp"

#include <QObject>
#include <memory>

namespace lens {

// 基于 QNetworkAccessManager 的流式传输：原生运行在 Qt 事件循环上，
// 通过 readyRead 增量读取响应分片，无需额外线程。
class QNetworkTransport final : public QObject, public ITransport
{
    Q_OBJECT

public:
    explicit QNetworkTransport(QObject *parent = nullptr);
    ~QNetworkTransport() override;

    void start(const HttpRequest &request, StreamCallbacks callbacks) override;
    void cancel() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace lens
