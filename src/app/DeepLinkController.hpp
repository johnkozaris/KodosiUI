#pragma once

#include "app/ApplicationLifecycleModel.hpp"
#include "app/DeepLinkRouter.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include <chrono>
#include <functional>
#include <optional>

namespace kodosi {

class DeepLinkController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusCode READ statusCode NOTIFY statusChanged)

public:
    using MonotonicTime = std::chrono::steady_clock::time_point;
    using MonotonicClock = std::function<MonotonicTime()>;

    struct RuntimeReadiness {
        ApplicationLifecycleModel::State state =
            ApplicationLifecycleModel::State::Starting;
        quint64 generation = 0;
    };

    explicit DeepLinkController(
        SessionCatalogModel& sessions,
        PendingPermissionsModel& permissions,
        ApplicationLifecycleModel& lifecycle,
        QObject* parent = nullptr);
    DeepLinkController(
        SessionCatalogModel& sessions,
        PendingPermissionsModel& permissions,
        ApplicationLifecycleModel& lifecycle,
        MonotonicClock clock,
        QObject* parent = nullptr);
    DeepLinkController(
        SessionCatalogModel& sessions,
        PendingPermissionsModel& permissions,
        RuntimeReadiness readiness,
        QObject* parent = nullptr);
    DeepLinkController(
        SessionCatalogModel& sessions,
        PendingPermissionsModel& permissions,
        RuntimeReadiness readiness,
        MonotonicClock clock,
        QObject* parent = nullptr);

    [[nodiscard]] QString statusCode() const;
    [[nodiscard]] qsizetype pendingCount() const noexcept;

    void enqueue(DeepLinkDestination destination);
    void reject(DeepLinkParseError error);
    void updateRuntimeReadiness(RuntimeReadiness readiness);

    Q_INVOKABLE void clearStatus();
    Q_INVOKABLE void reportNavigationResult(bool succeeded);

public slots:
    void ingestAuthEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void statusChanged();
    void openSessionRequested(QString sessionId);
    void reviewApprovalRequested(
        QString sessionId,
        QString identityToken);
    void missingApprovalRequested(QString sessionId);
    void activationRequested();

private:
    struct AccountIdentity {
        QString userId;
        quint64 epoch = 0;

        bool operator==(const AccountIdentity&) const = default;
    };

    struct PendingRoute {
        DeepLinkDestination destination;
        std::optional<AccountIdentity> account;
        std::optional<QString> incarnationId;
        std::chrono::milliseconds remainingAuthorityTime;
        std::optional<MonotonicTime> deadline;
    };

    static constexpr qsizetype MaximumPendingRoutes = 16;
    static constexpr std::chrono::milliseconds PendingRouteLifetime {
        30'000};

    SessionCatalogModel& m_sessions;
    PendingPermissionsModel& m_permissions;
    QQueue<PendingRoute> m_pending;
    QTimer m_pendingTimer;
    RuntimeReadiness m_runtimeReadiness;
    MonotonicClock m_clock;
    std::optional<AccountIdentity> m_account;
    QString m_statusCode;
    bool m_processing = false;

    [[nodiscard]] bool runtimeReady() const noexcept;
    void process();
    void schedulePendingTimeout();
    void setStatus(QString code);
    void cancelBoundRoutes(QString code);
    void pausePendingDeadlines();
    void resumePendingDeadlines();
    void clearRuntimeAuthority();
};

} // namespace kodosi
