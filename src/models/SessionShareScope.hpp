#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/MissionDirectoryModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace kodosi {

class SessionShareScope final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        quint64 stateRevision
        READ stateRevision
        NOTIFY stateChanged)

public:
    struct Timing {
        qint64 receiptTimeoutMs = 30'000;
        qint64 maximumAcceptedBudgetMs = 735'000;
    };

    SessionShareScope(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        MissionDirectoryModel& missions,
        QObject* parent = nullptr);
    SessionShareScope(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        MissionDirectoryModel& missions,
        Timing timing,
        QObject* parent = nullptr);

    [[nodiscard]] quint64 stateRevision() const noexcept;
    Q_INVOKABLE [[nodiscard]] bool canChange(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString currentScope(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString currentMissionId(
        const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString phase(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] QString message(const QString& sessionId) const;
    [[nodiscard]] QString requestId(const QString& sessionId) const;
    Q_INVOKABLE [[nodiscard]] bool setScope(
        const QString& sessionId,
        const QString& scope,
        const QString& missionId);
    Q_INVOKABLE [[nodiscard]] bool retry(const QString& sessionId);

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestSessionEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void transitionSucceeded(QString sessionId);

private:
    enum class Phase {
        Pending,
        Accepted,
        Unknown,
        Failed,
    };

    struct Target {
        QString scope;
        QString missionId;

        bool operator==(const Target&) const = default;
    };

    struct Mutation {
        QString requestId;
        QString sessionId;
        QString incarnationId;
        Target target;
        Phase phase = Phase::Pending;
        QString message;
        qint64 deadlineMs = 0;
    };

    static constexpr qsizetype maximumRetainedStates = 256;

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    MissionDirectoryModel& m_missions;
    Timing m_timing;
    AccountContextFence m_accountFence {256};
    QHash<QString, Mutation> m_mutations;
    QStringList m_order;
    QElapsedTimer m_clock;
    QTimer m_timer;
    quint64 m_stateRevision = 0;
    bool m_authenticated = false;

    [[nodiscard]] std::optional<Target> decodeTarget(
        const QString& scope,
        const QString& missionId) const;
    [[nodiscard]] std::optional<SessionCatalogModel::ActionContext>
    commandContext(
        const QString& sessionId,
        const Target& target,
        QString& error) const;
    [[nodiscard]] bool dispatch(Mutation& mutation, bool retainedRetry);
    [[nodiscard]] bool projectedTargetMatches(
        const SessionCatalogModel::ActionContext& context,
        const Target& target) const;
    [[nodiscard]] bool ensureCapacity(const QString& sessionId);
    void recordFailure(
        const QString& sessionId,
        Target target,
        QString message);
    void activateAccount(QString userId, quint64 epoch);
    void applySessionEvent(const QJsonObject& object, QByteArrayView json);
    void reconcileCatalog(bool authoritativeSnapshot);
    void settleSuccess(const QString& sessionId);
    void failMutation(const QString& sessionId, QString message);
    void scheduleTimer();
    void expireDeadlines();
    void bumpState();

    [[nodiscard]] static QString phaseName(Phase phase);
};

} // namespace kodosi
