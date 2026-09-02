#pragma once

#include <QString>

namespace kodosi {

struct TerminalNotificationEvent {
    QString sessionId;
    QString runtimeIncarnationId;
    QString title;
    QString body;
};

class TerminalNotificationSink {
public:
    virtual ~TerminalNotificationSink() = default;
    virtual void receiveTerminalNotification(
        TerminalNotificationEvent notification) = 0;
};

} // namespace kodosi
