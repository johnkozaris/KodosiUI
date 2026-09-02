#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/MissionDirectoryModel.hpp"
#include "models/MissionDetailModel.hpp"
#include "models/PeopleModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace kodosi {

class MissionActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(
        QStringList pendingMissionIds
        READ pendingMissionIds
        NOTIFY stateChanged)
    Q_PROPERTY(bool canDiscardUnknown READ canDiscardUnknown NOTIFY stateChanged)
    Q_PROPERTY(bool canRetryUnknown READ canRetryUnknown NOTIFY stateChanged)
    Q_PROPERTY(
        bool canManageSelectedMission
        READ canManageSelectedMission
        NOTIFY stateChanged)
    Q_PROPERTY(
        bool canCreateSelectedTasks
        READ canCreateSelectedTasks
        NOTIFY stateChanged)
    Q_PROPERTY(
        bool canActOnSelectedTasks
        READ canActOnSelectedTasks
        NOTIFY stateChanged)
    Q_PROPERTY(
        QVariantList assignmentOptions
        READ assignmentOptions
        NOTIFY assignmentChanged)
    Q_PROPERTY(
        quint64 assignmentRevision
        READ assignmentRevision
        NOTIFY assignmentChanged)

public:
    MissionActions(
        CommandDispatcher& dispatcher,
        MissionDirectoryModel& directory,
        MissionDetailModel& detail,
        PeopleModel& people,
        SessionCatalogModel& sessions,
        qint64 receiptTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] QString lastError() const;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QStringList pendingMissionIds() const;
    [[nodiscard]] bool canDiscardUnknown() const noexcept;
    [[nodiscard]] bool canRetryUnknown() const noexcept;
    [[nodiscard]] bool canManageSelectedMission() const;
    [[nodiscard]] bool canCreateSelectedTasks() const;
    [[nodiscard]] bool canActOnSelectedTasks() const;
    [[nodiscard]] QVariantList assignmentOptions() const;
    [[nodiscard]] quint64 assignmentRevision() const noexcept;

    Q_INVOKABLE [[nodiscard]] bool postBroadcast(
        const QString& missionId,
        const QString& body);
    Q_INVOKABLE [[nodiscard]] bool createTask(
        const QString& missionId,
        const QString& title,
        const QString& description);
    Q_INVOKABLE [[nodiscard]] bool inviteFriend(
        const QString& missionId,
        const QString& handle);
    Q_INVOKABLE [[nodiscard]] bool acceptInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool declineInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool cancelInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool removeMember(
        const QString& missionId,
        const QString& userId);
    Q_INVOKABLE [[nodiscard]] bool transitionTask(
        const QString& missionId,
        const QString& taskId,
        const QString& toStatus,
        const QString& result);
    Q_INVOKABLE [[nodiscard]] bool assignTask(
        const QString& missionId,
        const QString& taskId,
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] QString assignmentForTask(
        const QString& taskId) const;
    Q_INVOKABLE [[nodiscard]] bool canRemoveMember(
        const QString& missionId,
        const QString& userId) const;
    Q_INVOKABLE [[nodiscard]] bool discardUnknown();
    Q_INVOKABLE [[nodiscard]] bool retryUnknown();
    Q_INVOKABLE void clearError();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestRoomEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void assignmentChanged();
    void chatCompleted(QString missionId);
    void taskCompleted(QString missionId);
    void invitationSent(QString missionId);

private:
    struct Pending {
        QString operation {};
        QString missionId {};
        QString submittedBody {};
        QString submittedTitle {};
        QString submittedDescription {};
        QString targetId {};
        QString inviteeUserId {};
        QString expectedFingerprint {};
        QString reconciliationHydrationId {};
        QString terminalStatus {};
        QString terminalMessage {};
        QDateTime deadline {};
        qsizetype reconciliationNextOffset = 0;
        int reconciliationAttempts = 0;
        int reconciliationPageCount = 0;
        bool exhausted = false;
        bool receiptAuthoritative = false;
        bool terminalAwaitingProjection = false;
        bool terminalAwaitingAcknowledgement = false;
        bool requiresMemberProjectionRefresh = false;
        bool requiresTaskProjectionRefresh = false;
    };

    static constexpr int maximumPending = 64;
    static constexpr int maximumReconciliationAttempts = 3;

    CommandDispatcher& m_dispatcher;
    MissionDirectoryModel& m_directory;
    MissionDetailModel& m_detail;
    PeopleModel& m_people;
    SessionCatalogModel& m_sessions;
    AccountContextFence m_accountFence {128};
    QHash<QString, Pending> m_pending;
    QTimer m_receiptTimer;
    QString m_lastError;
    QString m_accountUserId;
    quint64 m_assignmentRevision = 0;
    qint64 m_receiptTimeoutMs;

    [[nodiscard]] bool dispatchMutation(
        Pending pending,
        QJsonObject command);
    [[nodiscard]] bool dispatchInvitationAction(
        const QString& invitationId,
        MissionInvitationsModel::Direction expectedDirection,
        const QString& operation,
        const QString& commandType);
    [[nodiscard]] bool hasPendingForMission(const QString& missionId) const;
    void activateAccount(QString userId, quint64 epoch);
    void applyRoomEvent(const QJsonObject& object);
    void scheduleReceiptTimeout();
    void reconcileExpired();
    void settle(
        const QString& requestId,
        const QString& status,
        const QString& message,
        const QString& entityId);
    void reconcileEntity(
        const QString& requestId,
        const QString& missionId,
        const QString& body,
        const QString& title,
        const QString& description);
    void reconcileInvitation(const QJsonObject& invitation, bool outgoing);
    [[nodiscard]] bool acknowledgeReceipt(const QString& requestId);
    [[nodiscard]] bool refreshReceiptProjections(const Pending& pending);
    [[nodiscard]] bool finalizeReceipt(
        const QString& requestId,
        const Pending& pending);
    void rejectCorrelation(
        const QString& requestId,
        const QString& message);
};

} // namespace kodosi
