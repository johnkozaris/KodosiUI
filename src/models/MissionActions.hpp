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
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
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
    Q_PROPERTY(
        QVariantList inviteCandidates
        READ inviteCandidates
        NOTIFY stateChanged)

    Q_PROPERTY(QString createMissionName READ createMissionName NOTIFY draftsChanged)
    Q_PROPERTY(QString createMissionSlug READ createMissionSlug NOTIFY draftsChanged)
    Q_PROPERTY(
        bool hasCreateMissionDraft
        READ hasCreateMissionDraft
        NOTIFY draftsChanged)
    Q_PROPERTY(Outcome createMissionOutcome READ createMissionOutcome NOTIFY draftsChanged)
    Q_PROPERTY(QString createMissionError READ createMissionError NOTIFY draftsChanged)
    Q_PROPERTY(bool createMissionCanCheck READ createMissionCanCheck NOTIFY draftsChanged)
    Q_PROPERTY(bool createMissionCanRetry READ createMissionCanRetry NOTIFY draftsChanged)
    Q_PROPERTY(bool createMissionCanDiscard READ createMissionCanDiscard NOTIFY draftsChanged)
    Q_PROPERTY(bool createMissionCanSubmit READ createMissionCanSubmit NOTIFY draftsChanged)

    Q_PROPERTY(QString chatDraftBody READ chatDraftBody NOTIFY draftsChanged)
    Q_PROPERTY(
        QStringList chatRecipientPresentationIds
        READ chatRecipientPresentationIds
        NOTIFY draftsChanged)
    Q_PROPERTY(
        QString chatRecipientSummary
        READ chatRecipientSummary
        NOTIFY draftsChanged)
    Q_PROPERTY(
        QVariantList chatUnavailableRecipients
        READ chatUnavailableRecipients
        NOTIFY draftsChanged)
    Q_PROPERTY(
        bool chatHasUnavailableRecipients
        READ chatHasUnavailableRecipients
        NOTIFY draftsChanged)
    Q_PROPERTY(Outcome chatOutcome READ chatOutcome NOTIFY draftsChanged)
    Q_PROPERTY(QString chatError READ chatError NOTIFY draftsChanged)
    Q_PROPERTY(bool chatCanCheck READ chatCanCheck NOTIFY draftsChanged)
    Q_PROPERTY(bool chatCanRetry READ chatCanRetry NOTIFY draftsChanged)
    Q_PROPERTY(bool chatCanDiscard READ chatCanDiscard NOTIFY draftsChanged)
    Q_PROPERTY(bool chatCanSubmit READ chatCanSubmit NOTIFY draftsChanged)

    Q_PROPERTY(QString taskDraftTitle READ taskDraftTitle NOTIFY draftsChanged)
    Q_PROPERTY(
        QString taskDraftDescription
        READ taskDraftDescription
        NOTIFY draftsChanged)
    Q_PROPERTY(
        QString taskDraftAssignmentPresentationId
        READ taskDraftAssignmentPresentationId
        NOTIFY draftsChanged)
    Q_PROPERTY(
        bool taskDraftHasDueAt
        READ taskDraftHasDueAt
        NOTIFY draftsChanged)
    Q_PROPERTY(QDateTime taskDraftDueAt READ taskDraftDueAt NOTIFY draftsChanged)
    Q_PROPERTY(
        QVariantList taskDraftAssignmentOptions
        READ taskDraftAssignmentOptions
        NOTIFY assignmentChanged)
    Q_PROPERTY(Outcome taskCreateOutcome READ taskCreateOutcome NOTIFY draftsChanged)
    Q_PROPERTY(QString taskCreateError READ taskCreateError NOTIFY draftsChanged)
    Q_PROPERTY(bool taskCreateCanCheck READ taskCreateCanCheck NOTIFY draftsChanged)
    Q_PROPERTY(bool taskCreateCanRetry READ taskCreateCanRetry NOTIFY draftsChanged)
    Q_PROPERTY(bool taskCreateCanDiscard READ taskCreateCanDiscard NOTIFY draftsChanged)
    Q_PROPERTY(bool taskCreateCanSubmit READ taskCreateCanSubmit NOTIFY draftsChanged)

    Q_PROPERTY(Outcome ledgerOutcome READ ledgerOutcome NOTIFY stateChanged)
    Q_PROPERTY(QString ledgerError READ ledgerError NOTIFY stateChanged)
    Q_PROPERTY(bool ledgerCanCheck READ ledgerCanCheck NOTIFY stateChanged)
    Q_PROPERTY(bool ledgerCanRetry READ ledgerCanRetry NOTIFY stateChanged)
    Q_PROPERTY(bool ledgerCanDiscard READ ledgerCanDiscard NOTIFY stateChanged)

public:
    enum class Outcome {
        Idle,
        Pending,
        AcceptedAwaitingProjection,
        Unknown,
        Reconciling,
        Failed,
        Conflict,
        Succeeded,
    };
    Q_ENUM(Outcome)

    enum class SyntheticPresentation {
        Pending,
        Unknown,
    };

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
    [[nodiscard]] QVariantList inviteCandidates() const;

    [[nodiscard]] QString createMissionName() const;
    [[nodiscard]] QString createMissionSlug() const;
    [[nodiscard]] bool hasCreateMissionDraft() const noexcept;
    [[nodiscard]] Outcome createMissionOutcome() const noexcept;
    [[nodiscard]] QString createMissionError() const;
    [[nodiscard]] bool createMissionCanCheck() const noexcept;
    [[nodiscard]] bool createMissionCanRetry() const noexcept;
    [[nodiscard]] bool createMissionCanDiscard() const noexcept;
    [[nodiscard]] bool createMissionCanSubmit() const;

    [[nodiscard]] QString chatDraftBody() const;
    [[nodiscard]] QStringList chatRecipientPresentationIds() const;
    [[nodiscard]] QString chatRecipientSummary() const;
    [[nodiscard]] QVariantList chatUnavailableRecipients() const;
    [[nodiscard]] bool chatHasUnavailableRecipients() const;
    [[nodiscard]] Outcome chatOutcome() const;
    [[nodiscard]] QString chatError() const;
    [[nodiscard]] bool chatCanCheck() const;
    [[nodiscard]] bool chatCanRetry() const;
    [[nodiscard]] bool chatCanDiscard() const;
    [[nodiscard]] bool chatCanSubmit() const;

    [[nodiscard]] QString taskDraftTitle() const;
    [[nodiscard]] QString taskDraftDescription() const;
    [[nodiscard]] QString taskDraftAssignmentPresentationId() const;
    [[nodiscard]] bool taskDraftHasDueAt() const;
    [[nodiscard]] QDateTime taskDraftDueAt() const;
    [[nodiscard]] QVariantList taskDraftAssignmentOptions() const;
    [[nodiscard]] Outcome taskCreateOutcome() const;
    [[nodiscard]] QString taskCreateError() const;
    [[nodiscard]] bool taskCreateCanCheck() const;
    [[nodiscard]] bool taskCreateCanRetry() const;
    [[nodiscard]] bool taskCreateCanDiscard() const;
    [[nodiscard]] bool taskCreateCanSubmit() const;

    [[nodiscard]] Outcome ledgerOutcome() const;
    [[nodiscard]] QString ledgerError() const;
    [[nodiscard]] bool ledgerCanCheck() const;
    [[nodiscard]] bool ledgerCanRetry() const;
    [[nodiscard]] bool ledgerCanDiscard() const noexcept;

    Q_INVOKABLE void setCreateMissionName(const QString& name);
    Q_INVOKABLE void setCreateMissionSlug(const QString& slug);
    Q_INVOKABLE [[nodiscard]] bool createMission();
    Q_INVOKABLE [[nodiscard]] bool checkCreateMission();
    Q_INVOKABLE [[nodiscard]] bool retryCreateMission();
    Q_INVOKABLE [[nodiscard]] bool discardCreateMission();

    Q_INVOKABLE void setChatDraftBody(const QString& body);
    Q_INVOKABLE [[nodiscard]] bool toggleChatRecipient(
        const QString& presentationId);
    Q_INVOKABLE [[nodiscard]] bool removeChatRecipient(
        const QString& presentationId);
    Q_INVOKABLE void selectChatBroadcast();
    Q_INVOKABLE [[nodiscard]] bool sendChat();
    Q_INVOKABLE [[nodiscard]] bool checkChat();
    Q_INVOKABLE [[nodiscard]] bool retryChat();
    Q_INVOKABLE [[nodiscard]] bool discardChat();

    Q_INVOKABLE void setTaskDraftTitle(const QString& title);
    Q_INVOKABLE void setTaskDraftDescription(const QString& description);
    Q_INVOKABLE [[nodiscard]] bool setTaskDraftAssignment(
        const QString& presentationId);
    Q_INVOKABLE void setTaskDraftHasDueAt(bool enabled);
    Q_INVOKABLE void setTaskDraftDueAt(const QDateTime& dueAt);
    Q_INVOKABLE [[nodiscard]] bool submitTaskCreate();
    Q_INVOKABLE [[nodiscard]] bool checkTaskCreate();
    Q_INVOKABLE [[nodiscard]] bool retryTaskCreate();
    Q_INVOKABLE [[nodiscard]] bool discardTaskCreate();

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
    Q_INVOKABLE [[nodiscard]] bool inviteFriendCandidate(
        const QString& candidateId);
    Q_INVOKABLE [[nodiscard]] bool acceptInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool declineInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool cancelInvitation(
        const QString& invitationId);
    Q_INVOKABLE [[nodiscard]] bool removeMember(
        const QString& missionId,
        const QString& userId);
    Q_INVOKABLE [[nodiscard]] bool removeMemberByPresentationId(
        const QString& presentationId);
    Q_INVOKABLE [[nodiscard]] bool transitionTask(
        const QString& missionId,
        const QString& taskId,
        const QString& toStatus,
        const QString& result);
    Q_INVOKABLE [[nodiscard]] bool assignTask(
        const QString& missionId,
        const QString& taskId,
        const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool assignTaskByPresentationId(
        const QString& taskId,
        const QString& presentationId);
    Q_INVOKABLE [[nodiscard]] QString assignmentForTask(
        const QString& taskId) const;
    Q_INVOKABLE [[nodiscard]] QString assignmentPresentationForTask(
        const QString& taskId) const;
    Q_INVOKABLE [[nodiscard]] QVariantList transitionOptionsForTask(
        const QString& taskId) const;
    Q_INVOKABLE [[nodiscard]] bool canRemoveMember(
        const QString& missionId,
        const QString& userId) const;
    Q_INVOKABLE [[nodiscard]] bool canRemoveMemberByPresentationId(
        const QString& presentationId) const;
    Q_INVOKABLE [[nodiscard]] bool checkLedger();
    Q_INVOKABLE [[nodiscard]] bool retryLedger();
    Q_INVOKABLE [[nodiscard]] bool discardLedger();
    Q_INVOKABLE [[nodiscard]] bool discardUnknown();
    Q_INVOKABLE [[nodiscard]] bool retryUnknown();
    Q_INVOKABLE void clearError();
    void installSyntheticPresentation(SyntheticPresentation presentation);

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestRoomEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void draftsChanged();
    void assignmentChanged();
    void missionCreated(QString missionId);
    void chatCompleted(QString missionId);
    void taskCompleted(QString missionId);
    void invitationSent(QString missionId);

private:
    struct DraftOutcome {
        Outcome outcome = Outcome::Idle;
        QString error;
        QString requestId;
        quint64 submittedRevision = 0;
    };

    struct CreateDraft {
        QString name;
        QString slug;
        QString submittedName;
        QString submittedSlug;
        quint64 revision = 0;
        DraftOutcome state;
    };

    struct ChatDraft {
        QString body;
        QSet<QString> recipientPresentationIds;
        QHash<QString, QString> recipientLabels;
        QString submittedBody;
        QStringList submittedRecipientSessionIds;
        QStringList submittedRecipientUserIds;
        quint64 revision = 0;
        DraftOutcome state;
    };

    struct TaskDraft {
        QString title;
        QString description;
        QString assignmentPresentationId;
        QString submittedTitle;
        QString submittedDescription;
        QString submittedAssignedSessionId;
        QString submittedAssignedSessionIncarnationId;
        QString submittedDueAt;
        QDateTime dueAt;
        quint64 revision = 0;
        bool hasDueAt = false;
        DraftOutcome state;
    };

    struct LedgerPresentation {
        Outcome outcome = Outcome::Idle;
        QString error;
    };

    enum class IntentKnowledge {
        ExactLocalIntent,
        RecoveredWithoutIntent,
    };

    struct Pending {
        QString requestId;
        QString operation;
        QString missionId;
        QString submittedName;
        QString submittedSlug;
        QString submittedBody;
        QStringList submittedRecipientSessionIds;
        QStringList submittedRecipientUserIds;
        QString submittedTitle;
        QString submittedDescription;
        QString submittedAssignedSessionId;
        QString submittedAssignedSessionIncarnationId;
        QString submittedDueAt;
        QString targetId;
        QString inviteeUserId;
        QString expectedEntityId;
        QString expectedFingerprint;
        QString reconciliationHydrationId;
        QString expectedTaskStatus;
        QString expectedTaskResult;
        QString expectedAssignedSessionId;
        QString expectedAssignedSessionIncarnationId;
        QString terminalStatus;
        QString terminalMessage;
        QDateTime deadline;
        qint64 expectedRevision = -1;
        qsizetype reconciliationNextOffset = 0;
        int reconciliationAttempts = 0;
        int reconciliationPageCount = 0;
        int expectedInvitationDirection = -1;
        bool exhausted = false;
        bool receiptAuthoritative = false;
        bool terminalAwaitingProjection = false;
        bool projectionRevisionMatched = false;
        bool projectionEntityMatched = false;
        bool retiredReceiptReplay = false;
        IntentKnowledge intentKnowledge = IntentKnowledge::ExactLocalIntent;
    };

    struct RetiredReceipt {
        Pending pending;
        QDateTime expiresAt;
    };

    static constexpr int maximumPending = 256;
    static constexpr int maximumRetiredReceipts = 256;
    static constexpr qint64 retiredReceiptLifetimeMs = 5 * 60 * 1'000;
    static constexpr int maximumReconciliationAttempts = 3;
    static constexpr qsizetype maximumDrafts = 128;

    CommandDispatcher& m_dispatcher;
    MissionDirectoryModel& m_directory;
    MissionDetailModel& m_detail;
    PeopleModel& m_people;
    SessionCatalogModel& m_sessions;
    AccountContextFence m_accountFence {128};
    QHash<QString, Pending> m_pending;
    QHash<QString, RetiredReceipt> m_retiredReceipts;
    QStringList m_retiredReceiptOrder;
    QHash<QString, ChatDraft> m_chatDrafts;
    QHash<QString, TaskDraft> m_taskDrafts;
    QHash<QString, LedgerPresentation> m_ledgerPresentations;
    mutable QHash<QString, QString> m_inviteCandidateTokensByUserId;
    mutable QHash<QString, QString> m_inviteCandidateUserIdsByToken;
    QStringList m_chatDraftOrder;
    QStringList m_taskDraftOrder;
    CreateDraft m_createDraft;
    QTimer m_receiptTimer;
    QString m_lastError;
    QString m_accountUserId;
    QString m_retiredAccountUserId;
    quint64 m_assignmentRevision = 0;
    qint64 m_receiptTimeoutMs;

    [[nodiscard]] bool dispatchMutation(Pending pending, QJsonObject command);
    [[nodiscard]] bool dispatchInvitationAction(
        const QString& invitationId,
        MissionInvitationsModel::Direction expectedDirection,
        const QString& operation,
        const QString& commandType);
    [[nodiscard]] bool hasReceiptPendingForMission(
        const QString& missionId) const;
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
        const QStringList& recipientSessionIds,
        const QStringList& recipientUserIds,
        const QString& title,
        const QString& description,
        const QString& assignedSessionId,
        const QString& assignedSessionIncarnationId,
        const QString& dueAt);
    void reconcileRoomSnapshot(const QJsonArray& rooms);
    void reconcileInvitation(const QJsonObject& invitation, bool outgoing);
    [[nodiscard]] bool requestReceiptRetirement(const QString& requestId);
    [[nodiscard]] bool refreshReceiptProjections(Pending& pending);
    [[nodiscard]] bool refreshDirectProjection(Pending& pending);
    void finalizeReceipt(
        const QString& requestId,
        const Pending& pending);
    void retireReceipt(const Pending& pending);
    void pruneRetiredReceipts();
    void finalizeDirectProjection(const QString& requestId);
    void completeReceiptProjection(const QString& requestId);
    void failProjection(const QString& requestId, QString message);
    [[nodiscard]] bool taskProjectionMatches(
        const Pending& pending,
        const QJsonObject& task) const;
    void rejectCorrelation(
        const QString& requestId,
        const QString& message);

    [[nodiscard]] ChatDraft* currentChatDraft(bool create);
    [[nodiscard]] const ChatDraft* currentChatDraft() const;
    [[nodiscard]] TaskDraft* currentTaskDraft(bool create);
    [[nodiscard]] const TaskDraft* currentTaskDraft() const;
    [[nodiscard]] QString currentMissionId() const;
    void touchChatDraft(const QString& missionId);
    void touchTaskDraft(const QString& missionId);
    void pruneDraftCaches();
    void reconcileDraftAuthorities();
    void publishCurrentDraftSelection();
    void completeDirectDraft(
        const Pending& pending,
        Outcome outcome,
        QString error);
    void setLedgerPresentation(
        const QString& missionId,
        Outcome outcome,
        QString error = {});
    [[nodiscard]] bool checkDirect(
        const QString& operation,
        const QString& missionId);
    [[nodiscard]] bool submitChatDraft(const QString& missionId);
    [[nodiscard]] bool discardDirect(
        const QString& operation,
        const QString& missionId);
    [[nodiscard]] static bool canCheck(Outcome outcome) noexcept;
    [[nodiscard]] static bool canRetry(Outcome outcome) noexcept;
    [[nodiscard]] static bool canDiscard(Outcome outcome) noexcept;
};

} // namespace kodosi
