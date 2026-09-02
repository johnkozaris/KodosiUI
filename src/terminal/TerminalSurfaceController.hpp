#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/SessionCatalogModel.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalNotificationSink.hpp"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include <cstdint>

namespace kodosi {

class TerminalView;

class TerminalSurfaceController final : public QObject {
    Q_OBJECT

public:
    TerminalSurfaceController(
        TerminalSessionRegistry& registry,
        RuntimeBridge& runtime,
        SessionCatalogModel& sessions,
        QObject* parent = nullptr);

    Q_INVOKABLE [[nodiscard]] bool bind(
        kodosi::TerminalView* view,
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool retry(
        kodosi::TerminalView* view,
        const QString& sessionId);
    Q_INVOKABLE void detach(kodosi::TerminalView* view);
    void setNotificationSink(TerminalNotificationSink* sink) noexcept;

signals:
    void attachmentRejected(QString sessionId, QString reason);
    void attachmentReady(QString sessionId);

private:
    struct Binding {
        QPointer<TerminalView> view;
        QString sessionId;
        QString runtimeIncarnationId;
        QMetaObject::Connection notificationConnection;
        int retryAttempts = 0;
        bool retryPending = false;
        bool retryExhausted = false;
        bool attached = false;
    };

    TerminalSessionRegistry& m_registry;
    RuntimeBridge& m_runtime;
    SessionCatalogModel& m_sessions;
    TerminalNotificationSink* m_notificationSink = nullptr;
    QVector<Binding> m_bindings;
    std::uint64_t m_nextSubscriptionGeneration = 0;
    QTimer m_attachmentRetryTimer;
    bool m_waitingForFreshCatalog = true;

    void runtimeChanged(bool running);
    void catalogSnapshotApplied();
    void reconcile();
    void tryAttach(Binding& binding);
    void scheduleRemoteRetry(Binding& binding, QString reason);
    void retryPendingAttachments();
    void forwardNotification(
        TerminalView* view,
        QString title,
        QString body);
};

} // namespace kodosi
