#pragma once

#include "app/DeepLinkRouter.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include <optional>

namespace kodosi {

class DeepLinkController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusCode READ statusCode NOTIFY statusChanged)

public:
    explicit DeepLinkController(
        SessionCatalogModel& sessions,
        PendingPermissionsModel& permissions,
        QObject* parent = nullptr);

    [[nodiscard]] QString statusCode() const;
    [[nodiscard]] qsizetype pendingCount() const noexcept;

    void enqueue(DeepLinkDestination destination);
    void reject(DeepLinkParseError error);

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
        QDeadlineTimer deadline;
    };

    static constexpr qsizetype MaximumPendingRoutes = 16;

    SessionCatalogModel& m_sessions;
    PendingPermissionsModel& m_permissions;
    QQueue<PendingRoute> m_pending;
    QTimer m_pendingTimer;
    std::optional<AccountIdentity> m_account;
    QString m_statusCode;
    bool m_processing = false;

    void process();
    void schedulePendingTimeout();
    void setStatus(QString code);
    void cancelBoundRoutes(QString code);
};

} // namespace kodosi
