#pragma once

#include <QByteArray>
#include <functional>

namespace lens {

// 增量 SSE 解析：把传输层交付的原始字节流按空行切成事件，
// 提取每个事件的 data 负载（去掉前缀，合并多行 data）交给回调。
// 纯增量实现，分片边界可以落在任意位置。
class SseParser
{
public:
    using EventCallback = std::function<void(const QByteArray &data)>;

    void feed(const QByteArray &bytes, const EventCallback &onEvent)
    {
        m_buffer += bytes;
        m_buffer.replace("\r\n", "\n");
        int index;
        while ((index = m_buffer.indexOf("\n\n")) >= 0) {
            const QByteArray block = m_buffer.left(index);
            m_buffer.remove(0, index + 2);
            emitEvent(block, onEvent);
        }
    }

private:
    static void emitEvent(const QByteArray &block, const EventCallback &onEvent)
    {
        QByteArray data;
        const QList<QByteArray> lines = block.split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("data:"))
                continue; 
            QByteArray payload = line.mid(5);
            if (payload.startsWith(' '))
                payload.remove(0, 1);
            if (!data.isEmpty())
                data += '\n';
            data += payload;
        }
        if (!data.isEmpty())
            onEvent(data);
    }

    QByteArray m_buffer;
};

} // namespace lens
