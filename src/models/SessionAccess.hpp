#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/PeopleModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class SessionAccessGrantsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        HandleRole = Qt::UserRole + 1,
        DisplayNameRole,
        AccessLevelRole,
        GrantedAtRole,
        ExpiresAtRole,
    };
    Q_ENUM(Role)

    explicit SessionAccessGrantsModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class SessionAccess;

    struct Grant {
        QString actorUserId;
        QString handle;
        QString displayName;
        QString accessLevel;
        QString grantedAt;
        QString expiresAt;
        std::optional<QDateTime> expiresInstant;
    };

    QVector<Grant> m_rows;
    void replace(QVector<Grant> rows);
};

class SessionAccess final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        kodosi::SessionAccessGrantsModel* grants
        READ grants
        CONSTANT)
    Q_PROPERTY(quint64 stateRevision READ stateRevision NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(
        QString leaveConfirmationSessionId
        READ leaveConfirmationSessionId
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString revokeConfirmationHandle
        READ revokeConfirmationHandle
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString revokeConfirmationSessionId
        READ revokeConfirmationSessionId
        NOTIFY stateChanged)
    Q_PROPERTY(
        QStringList pendingLeaveSessionIds
        READ pendingLeaveSessionIds
        NOTIFY stateChanged)

public:
    struct Timing {
        qint64 resultTimeoutMs = 30'000;
        qint64 projectionTimeoutMs = 30'000;
        qint64 queryTimeoutMs = 30'000;
        qint64 acknowledgmentTimeoutMs = 30'000;
        int maximumAttempts = 3;
    };

    SessionAccess(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        PeopleModel& people,
        QObject* parent = nullptr);
    SessionAccess(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        PeopleModel& people,
        Timing timing,
        QObject* parent = nullptr);

    [[nodiscard]] SessionAccessGrantsModel* grants() noexcept;
    [[nodiscard]] quint64 stateRevision() const noexcept;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] bool stale() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString leaveConfirmationSessionId() const;
    [[nodiscard]] QString revokeConfirmationHandle() const;
    [[nodiscard]] QString revokeConfirmationSessionId() const;
    [[nodiscard]] QStringList pendingLeaveSessionIds() const;

    Q_INVOKABLE [[nodiscard]] bool inspect(const QString& sessionId);
    Q_INVOKABLE void clearInspection();
    Q_INVOKABLE [[nodiscard]] bool refresh(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool canManage(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool grant(
        const QString& sessionId,
        const QString& friendHandle,
        const QString& accessLevel);
    Q_INVOKABLE [[nodiscard]] bool requestRevokeConfirmation(
        const QString& sessionId,
        const QString& friendHandle);
    Q_INVOKABLE [[nodiscard]] bool confirmRevoke(
        const QString& sessionId,
        const QString& friendHandle);
    Q_INVOKABLE void cancelRevokeConfirmation();
    Q_INVOKABLE [[nodiscard]] bool canLeave(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool leaveNeedsRetry(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool requestLeaveConfirmation(
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool confirmLeave(const QString& sessionId);
    Q_INVOKABLE void cancelLeaveConfirmation();
    Q_INVOKABLE [[nodiscard]] bool retryLeave(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool retryMutation(
        const QString& sessionId,
        const QString& friendHandle);
    Q_INVOKABLE [[nodiscard]] bool retryCurrentMutation(
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] QString mutationPhase(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString mutationMessage(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString actorMutationPhase(
        const QString& sessionId,
        const QString& friendHandle) const;
    Q_INVOKABLE [[nodiscard]] QString actorMutationMessage(
        const QString& sessionId,
        const QString& friendHandle) const;

    [[nodiscard]] bool hasPendingLeave(const QString& sessionId) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestSessionEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void presentationContextChanged();

private:
    enum class Kind {
        Grant,
        Revoke,
        Leave,
    };

    enum class Phase {
        Pending,
        Accepted,
        AwaitingProjection,
        Unknown,
        Failed,
        Acknowledging,
        Exhausted,
    };

    enum class Outcome {
        None,
        Applied,
        Rejected,
        Unknown,
    };

    struct Projection {
        QString incarnationId;
        QVector<SessionAccessGrantsModel::Grant> rows;
        QString error;
        quint64 requestedGeneration = 0;
        quint64 inFlightGeneration = 0;
        bool loaded = false;
        bool loading = false;
        bool stale = false;
        bool queued = false;
    };

    struct Mutation {
        QString mutationId;
        QString sessionId;
        QString incarnationId;
        Kind kind = Kind::Leave;
        QString actorUserId;
        QString actorHandle;
        QString accessLevel;
        QString expiresAt;
        std::optional<QDateTime> expiresInstant;
        QString fingerprint;
        QString message;
        Phase phase = Phase::Pending;
        Outcome outcome = Outcome::None;
        QDateTime deadline;
        int attempts = 0;
        bool projectionSatisfied = false;
        bool queryOutstanding = false;
        bool recoveryResponseExpected = false;
        bool acknowledgmentOutstanding = false;
        bool acknowledgmentVerificationIntent = false;
    };

    static constexpr qsizetype maximumGrantRows = 5'000;
    static constexpr qsizetype maximumDurableMutations = 1'024;

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    PeopleModel& m_people;
    Timing m_timing;
    AccountContextFence m_accountFence {256};
    SessionAccessGrantsModel m_grants;
    QHash<QString, Projection> m_projections;
    QHash<QString, Mutation> m_mutations;
    QTimer m_timer;
    QString m_selectedSessionId;
    QString m_selectedIncarnationId;
    QString m_accountUserId;
    QString m_error;
    QString m_leaveConfirmationSessionId;
    QString m_leaveConfirmationIncarnationId;
    QString m_revokeConfirmationSessionId;
    QString m_revokeConfirmationIncarnationId;
    QString m_revokeConfirmationHandle;
    QString m_revokeConfirmationActorUserId;
    quint64 m_stateRevision = 0;
    bool m_authenticated = false;
    bool m_hasAuthoritativeSessionCatalog = false;

    [[nodiscard]] bool send(QJsonObject command, QString* error = nullptr);
    [[nodiscard]] bool selectSession(const QString& sessionId);
    [[nodiscard]] bool dispatchList(const QString& sessionId);
    void finishList(
        const QString& sessionId,
        bool accepted,
        QVector<SessionAccessGrantsModel::Grant> rows,
        QString error);
    void publishSelectedProjection();
    [[nodiscard]] std::optional<SessionCatalogModel::ActionContext>
    accessContext(const QString& sessionId, QString& error) const;
    [[nodiscard]] std::optional<QString> resolveFriend(
        const QString& handle,
        QString& normalizedHandle,
        QString& error) const;
    [[nodiscard]] bool beginActorMutation(
        Kind kind,
        const QString& sessionId,
        const QString& friendHandle,
        const QString& accessLevel);
    [[nodiscard]] bool beginLeave(const QString& sessionId);
    [[nodiscard]] bool dispatchNewMutation(Mutation& mutation);
    [[nodiscard]] bool hasConflict(
        const QString& sessionId,
        const QString& actorUserId,
        Kind kind) const;
    void activateAccount(QString userId, quint64 epoch);
    void applySessionEvent(const QJsonObject& object, QByteArrayView json);
    void applyAccessGrants(const QJsonObject& object);
    void applyMutationEvent(const QJsonObject& object, QByteArrayView json);
    void applySessionError(const QJsonObject& object);
    void reconcileCatalog(bool authoritativeSnapshot);
    void reconcileProjection(const QString& sessionId);
    void refreshMutationAuthority(Mutation& mutation);
    void settleIfAuthoritative(const QString& mutationId);
    [[nodiscard]] bool queryMutation(const QString& mutationId);
    [[nodiscard]] bool acknowledgeMutation(const QString& mutationId);
    void settleMutation(const QString& mutationId, bool success, QString message);
    void markExhausted(Mutation& mutation, QString message);
    void scheduleTimer();
    void expireDeadlines();
    void bumpState();

    [[nodiscard]] const Mutation* latestMutation(
        const QString& sessionId,
        const QString& actorHandle = {}) const;
    [[nodiscard]] Mutation* mutableMutation(
        const QString& sessionId,
        const QString& actorHandle = {});
    [[nodiscard]] static std::optional<QDateTime> parseRfc3339(
        const QString& value);
    [[nodiscard]] static bool knownAccessLevel(const QString& value);
    [[nodiscard]] static bool validFingerprint(const QString& value);
    [[nodiscard]] static bool validMutationId(const QString& value);
    [[nodiscard]] static QString normalizeHandle(const QString& value);
    [[nodiscard]] static QString phaseName(Phase phase);
};

} // namespace kodosi
