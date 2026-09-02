#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <cstdint>
#include <optional>

namespace kodosi {

class AttentionModel;

class AgentSessionIntelModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(
        HydrationState hydrationState
        READ hydrationState
        NOTIFY hydrationStateChanged)
    Q_PROPERTY(
        QString hydrationError
        READ hydrationError
        NOTIFY hydrationStateChanged)

public:
    struct ProjectMemoryContext {
        QString agent;
        QString workingDirectory;
    };

    enum class HydrationState {
        Loading,
        Current,
        Failed,
    };
    Q_ENUM(HydrationState)

    enum Role {
        SessionIdRole = Qt::UserRole + 1,
        AgentTypeRole,
        VersionRole,
        ModelRole,
        TitleRole,
        CwdRole,
        LifecycleRole,
        HasAttentionRole,
        AttentionKindRole,
        AttentionSummaryRole,
        AttentionActionableRole,
        HasPendingInteractionRole,
        PendingInteractionKindRole,
        PendingInteractionSummaryRole,
        PendingToolNameRole,
        CanApproveRole,
        CanDenyRole,
        CanAnswerRole,
        CanFocusRole,
        CurrentActivityRole,
        LastProgressAtRole,
        ActiveWorkersRole,
        BlockedWorkersRole,
        FailedWorkersRole,
        CompletedWorkersRole,
        HasOutcomeRole,
        OutcomeKindRole,
        OutcomeSummaryRole,
        HasExceptionalStateRole,
        ExceptionalKindRole,
        ExceptionalSummaryRole,
        ExceptionalRetryableRole,
        SourceKindRole,
        SourceDegradedRole,
        SourceDetailRole,
    };
    Q_ENUM(Role)

    AgentSessionIntelModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        qint64 queryTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] HydrationState hydrationState() const noexcept;
    [[nodiscard]] QString hydrationError() const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] QVariantMap presentationForSession(
        const QString& sessionId) const;
    [[nodiscard]] std::optional<QString> conversationAgent(
        const QString& sessionId,
        const QString& sessionIncarnationId) const;
    [[nodiscard]] std::optional<ProjectMemoryContext> projectMemoryContext(
        const QString& sessionId,
        const QString& sessionIncarnationId) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();
    void reapplyLatestSet();

signals:
    void countChanged();
    void hydrationStateChanged();
    void authorityRevisionChanged();
    void decodeError(QString message);

private:
    friend class AttentionModel;

    struct Entry {
        QString sessionId;
        QString sessionIncarnationId;
        QString agentType;
        QString version;
        QString model;
        QString title;
        QString cwd;
        QString vendorSessionId;
        quint32 processId = 0;
        bool hasProcessId = false;
        QString lifecycle;
        bool hasAttention = false;
        QString attentionKind;
        QString attentionSummary;
        bool attentionActionable = false;
        bool hasPendingInteraction = false;
        QString pendingInteractionKind;
        QString pendingInteractionSummary;
        QString pendingToolName;
        bool canApprove = false;
        bool canDeny = false;
        bool canAnswer = false;
        bool canFocus = false;
        QString currentActivity;
        QString lastProgressAt;
        quint32 activeWorkers = 0;
        quint32 blockedWorkers = 0;
        quint32 failedWorkers = 0;
        quint32 completedWorkers = 0;
        bool hasOutcome = false;
        QString outcomeKind;
        QString outcomeSummary;
        bool hasExceptionalState = false;
        QString exceptionalKind;
        QString exceptionalSummary;
        bool exceptionalRetryable = false;
        QString sourceKind;
        bool sourceDegraded = false;
        QString sourceDetail;

        bool operator==(const Entry&) const = default;
    };

    struct LiveSet {
        QString requestId;
        QString authorityIncarnationId;
        quint64 revision;
        QVector<Entry> entries;
    };

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    AccountContextFence m_accountFence {256};
    QVector<Entry> m_entries;
    std::optional<LiveSet> m_latestSet;
    std::optional<LiveSet> m_stagedSet;
    QString m_authorityIncarnationId;
    std::optional<quint64> m_revision;
    QString m_pendingRequestId;
    QString m_hydrationError;
    QTimer m_queryTimer;
    QTimer m_recoveryTimer;
    qint64 m_queryTimeoutMs;
    std::uint8_t m_recoveryStep = 0;
    HydrationState m_hydrationState = HydrationState::Loading;
    bool m_hasAccountContext = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(
        const QByteArray& json,
        const QJsonObject& object);
    void applyLiveSet(LiveSet set);
    void establish(LiveSet set);
    void applyEstablished(LiveSet set);
    void replaceEntries(QVector<Entry> entries);
    void settleQuery();
    void resetAuthority(bool clearProjection);
    void protocolFault(QString message);
    void scheduleRecovery();
    void setHydrationState(HydrationState state, QString error = {});
    [[nodiscard]] static std::optional<LiveSet> decodeLiveSet(
        const QByteArray& json,
        const QJsonObject& object);
    [[nodiscard]] static std::optional<Entry> decodeEntry(
        const QJsonObject& object);
    [[nodiscard]] static QVariantMap presentation(const Entry& entry);
};

} // namespace kodosi
