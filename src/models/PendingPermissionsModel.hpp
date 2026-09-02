#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace kodosi {

class AttentionModel;

class PendingPermissionsModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(AuthorityState authorityState READ authorityState NOTIFY authorityStateChanged)
    Q_PROPERTY(QString authorityError READ authorityError NOTIFY authorityStateChanged)

public:
    struct NotificationRequest {
        QString identityToken;
        QString sessionId;
        QString toolName;
        QString toolInputSummary;
        QString risk;
        bool actionable;
    };

    enum class AuthorityState {
        Loading,
        Loaded,
        AuthorityFailed,
    };
    Q_ENUM(AuthorityState)

    enum class DecisionState {
        Actionable,
        SendingApprove,
        SendingDeny,
        DeliveryUnknown,
        DeliveryFailed,
    };
    Q_ENUM(DecisionState)

    enum Role {
        IdentityTokenRole = Qt::UserRole + 1,
        SessionIdRole,
        SessionIncarnationIdRole,
        ToolUseIdRole,
        ToolNameRole,
        ToolInputSummaryRole,
        CreatedAtRole,
        DeadlineRole,
        RiskRole,
        DecisionStateRole,
        DecisionMessageRole,
        ActionableRole,
    };
    Q_ENUM(Role)

    PendingPermissionsModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        qint64 deliveryTimeoutMs = 15'000,
        qint64 queryTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] AuthorityState authorityState() const noexcept;
    [[nodiscard]] QString authorityError() const;
    [[nodiscard]] std::optional<NotificationRequest> notificationRequest(
        const QString& identityToken) const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] int rowForIdentityToken(
        const QString& identityToken) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap presentationForIdentityToken(
        const QString& identityToken) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap presentationForSession(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool approve(const QString& identityToken);
    Q_INVOKABLE [[nodiscard]] bool deny(
        const QString& identityToken,
        const QString& reason = {});

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void reapplyLatestSnapshot();
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void authorityStateChanged();
    void decodeError(QString message);
    void requestAdded(QString identityToken);
    void requestRemoved(QString identityToken);

private:
    friend class AttentionModel;

    enum class AuthoritativePhase {
        Actionable,
        Sending,
    };

    struct Request {
        QString identityToken;
        QString sessionId;
        QString sessionIncarnationId;
        QString toolUseId;
        QString toolName;
        QString toolInputSummary;
        QDateTime createdAt;
        QDateTime deadline;
        QString risk;
        QString decisionMessage;
        quint64 requestGeneration;
        AuthoritativePhase authoritativePhase;
        DecisionState decisionState;
        QDateTime deliveryDeadline;
    };

    struct Snapshot {
        quint64 generation;
        QVector<Request> requests;
    };

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    AccountContextFence m_accountFence {256};
    QVector<Request> m_requests;
    std::optional<Snapshot> m_latestSnapshot;
    std::optional<quint64> m_snapshotGeneration;
    QString m_pendingQueryId;
    QString m_authorityError;
    QTimer m_deliveryTimer;
    QTimer m_queryTimer;
    qint64 m_deliveryTimeoutMs;
    qint64 m_queryTimeoutMs;
    AuthorityState m_authorityState = AuthorityState::Loading;
    bool m_authenticated = false;

    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void applyAgentIntelEvent(const QJsonObject& object);
    void applySnapshot(Snapshot snapshot, bool authoritativeRefresh);
    void replaceRequests(QVector<Request> requests);
    void setAuthorityState(AuthorityState state, QString error = {});
    void settleQueryFailure(QString error);
    void scheduleDeliveryTimeout();
    void expireDeliveries();
    [[nodiscard]] bool decide(
        const QString& identityToken,
        bool approve,
        const QString& reason);
    [[nodiscard]] Request* findRequest(const QString& identityToken);
    [[nodiscard]] static std::optional<Snapshot> decodeSnapshot(
        const QJsonObject& object);
    [[nodiscard]] static std::optional<Request> decodeRequest(
        const QJsonObject& object);
    [[nodiscard]] static QString identityToken(const Request& request);
    [[nodiscard]] static QString inputSummary(const QJsonValue& value);
    [[nodiscard]] static QByteArray quoted(const QString& value);
    [[nodiscard]] static QVariantMap presentation(const Request& request);
};

} // namespace kodosi
