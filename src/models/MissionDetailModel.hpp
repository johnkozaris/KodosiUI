#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/MissionDirectoryModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class AttentionModel;
class DesktopStateModel;
class MissionActions;
class PeopleModel;
class SessionActions;
class SessionCatalogModel;
class SteeringModel;

class MissionMembersModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        UserIdRole = Qt::UserRole + 1,
        MemberRoleRole,
        UsernameRole,
        DisplayNameRole,
        ResolvedNameRole,
    };
    Q_ENUM(Role)

    explicit MissionMembersModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool containsUser(const QString& userId) const;

signals:
    void countChanged();

private:
    friend class MissionDetailModel;
    struct Member {
        QString userId;
        QString role;
        QString username;
        QString displayName;
    };
    QVector<Member> m_members;
    void replace(QVector<Member> members);
};

class MissionMessagesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        AuthorUserIdRole,
        AuthorSessionIdRole,
        AuthorKindRole,
        BodyRole,
        SequenceRole,
        PostedAtRole,
        BroadcastRole,
        AuthorDisplayRole,
        AudienceSummaryRole,
        RecipientPresentationIdsRole,
        RecipientCountRole,
    };
    Q_ENUM(Role)

    explicit MissionMessagesModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class MissionDetailModel;
    struct Message {
        QString id;
        QString authorUserId;
        QString authorSessionId;
        QString authorKind;
        QString body;
        QStringList recipientSessionIds;
        QStringList recipientUserIds;
        QString authorDisplay;
        QString audienceSummary;
        QStringList recipientPresentationIds;
        qint64 sequence = 0;
        QDateTime postedAt;
    };
    QVector<Message> m_messages;
    void replace(QVector<Message> messages);
    void upsert(Message message);
};

class MissionTasksModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    struct ActionContext {
        QString status;
        QString assignedSessionId;
        QString assignedSessionIncarnationId;
        qint64 revision = 0;
    };

    enum Role {
        TaskIdRole = Qt::UserRole + 1,
        TitleRole,
        DescriptionRole,
        StatusRole,
        AssignedSessionIdRole,
        DueAtRole,
        UpdatedAtRole,
        ResultRole,
        KnownStatusRole,
        StatusLabelRole,
        AssignmentDisplayRole,
        CreatedAtRole,
        CompletedAtRole,
        ResultEvidenceRole,
        ResultAuthorDisplayRole,
        UnknownStatusTitleRole,
        UnknownStatusMessageRole,
    };
    Q_ENUM(Role)

    explicit MissionTasksModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] std::optional<ActionContext> actionContext(
        const QString& taskId) const;

signals:
    void countChanged();

private:
    friend class MissionDetailModel;
    struct Task {
        QString id;
        QString title;
        QString description;
        QString status;
        QString statusLabel;
        QString assignedSessionId;
        QString assignedSessionIncarnationId;
        QString assignmentDisplay;
        QString result;
        QString resultAuthorUserId;
        QString resultAuthorDisplay;
        qint64 revision = 0;
        bool knownStatus = false;
        QDateTime createdAt;
        QDateTime dueAt;
        QDateTime completedAt;
        QDateTime updatedAt;
    };
    QVector<Task> m_tasks;
    void replace(QVector<Task> tasks);
    void upsert(Task task);
};

class MissionCrewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(
        int dispatchableAgentCount
        READ dispatchableAgentCount
        NOTIFY dispatchableAgentCountChanged)

public:
    enum class Kind {
        Agent,
        Member,
    };
    Q_ENUM(Kind)

    enum Role {
        PresentationIdRole = Qt::UserRole + 1,
        KindRole,
        DisplayNameRole,
        SecondaryLabelRole,
        StatusRole,
        CanOpenFullTerminalRole,
        CanDispatchRole,
        CanAssignTaskRole,
        CanSteerRole,
        CanInterruptRole,
        DispatchSelectedRole,
        AttentionCountRole,
        DeliveryStateRole,
        DeliveryDetailRole,
    };
    Q_ENUM(Role)

    explicit MissionCrewModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int dispatchableAgentCount() const noexcept;
    Q_INVOKABLE [[nodiscard]] bool containsPresentationId(
        const QString& presentationId) const;

signals:
    void countChanged();
    void dispatchableAgentCountChanged();

private:
    friend class MissionActions;
    friend class MissionDetailModel;

    struct RecipientContext {
        Kind kind = Kind::Member;
        QString rawId;
        QString localSessionId;
        QString sessionIncarnationId;
        QString assignmentSessionId;
        QString assignmentIncarnationId;
        QString displayName;
        bool canDispatch = false;
        bool canAssignTask = false;
    };

    struct Entry {
        QString authorityKey;
        QString presentationId;
        Kind kind = Kind::Member;
        QString displayName;
        QString secondaryLabel;
        QString status;
        QString localSessionId;
        QString sessionIncarnationId;
        QString rawRecipientId;
        QString assignmentSessionId;
        QString assignmentIncarnationId;
        QString deliveryState;
        QString deliveryDetail;
        bool canOpenFullTerminal = false;
        bool canDispatch = false;
        bool canAssignTask = false;
        bool canSteer = false;
        bool canInterrupt = false;
        bool dispatchSelected = false;
        int attentionCount = 0;
    };

    QVector<Entry> m_entries;
    QHash<QString, QString> m_tokensByKey;
    QStringList m_tokenOrder;
    void replace(QVector<Entry> entries);
    [[nodiscard]] std::optional<RecipientContext> recipientContext(
        const QString& presentationId) const;
    [[nodiscard]] QString presentationForSession(
        const QString& sessionId,
        const QString& incarnationId = {}) const;
    [[nodiscard]] QString presentationForUser(const QString& userId) const;
};

class MissionScopedAttentionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY countChanged)
    Q_PROPERTY(int pageOffset READ pageOffset NOTIFY countChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY countChanged)
    Q_PROPERTY(bool canLoadPrevious READ canLoadPrevious NOTIFY countChanged)

public:
    enum class Category {
        Approval,
        BulkSafe,
        Agent,
        Blocked,
        Waiting,
    };
    Q_ENUM(Category)

    enum class Risk {
        None,
        Safe,
        Unknown,
        Network,
        Credential,
        Destructive,
    };
    Q_ENUM(Risk)

    enum class Tone {
        Muted,
        Accent,
        Warning,
        Danger,
    };
    Q_ENUM(Tone)

    enum class ActionKind {
        None,
        Jump,
        Review,
        Approve,
        BulkApprove,
    };
    Q_ENUM(ActionKind)

    enum Role {
        ActionPresentationIdRole = Qt::UserRole + 1,
        CategoryRole,
        SessionPresentationIdRole,
        TitleRole,
        SummaryRole,
        RiskRole,
        ToneRole,
        ActionKindRole,
        CanApproveRole,
        CanDenyRole,
        CanJumpRole,
        ItemCountRole,
    };
    Q_ENUM(Role)

    explicit MissionScopedAttentionModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int totalCount() const noexcept;
    [[nodiscard]] bool truncated() const noexcept;
    [[nodiscard]] int pageOffset() const noexcept;
    [[nodiscard]] bool canLoadMore() const noexcept;
    [[nodiscard]] bool canLoadPrevious() const noexcept;
    Q_INVOKABLE [[nodiscard]] bool loadMore();
    Q_INVOKABLE [[nodiscard]] bool loadPrevious();
    Q_INVOKABLE [[nodiscard]] bool review(const QString& actionPresentationId);
    Q_INVOKABLE [[nodiscard]] bool jump(const QString& actionPresentationId);
    Q_INVOKABLE [[nodiscard]] bool approve(const QString& actionPresentationId);
    Q_INVOKABLE [[nodiscard]] bool deny(const QString& actionPresentationId);
    Q_INVOKABLE [[nodiscard]] bool approveAll(
        const QString& actionPresentationId);

signals:
    void countChanged();

private:
    friend class MissionDetailModel;
    struct Entry {
        QString authorityKey;
        QString actionPresentationId;
        QString sourceToken;
        QString sessionPresentationId;
        QStringList sourceSessionIds;
        QHash<QString, int> sourceSessionCounts;
        Category category = Category::Agent;
        Risk risk = Risk::None;
        Tone tone = Tone::Muted;
        ActionKind actionKind = ActionKind::None;
        QString title;
        QString summary;
        bool canApprove = false;
        bool canDeny = false;
        bool canJump = false;
        int itemCount = 1;
    };

    static constexpr int maximumVisibleItems = 64;
    QVector<Entry> m_entries;
    QHash<QString, QString> m_tokensByKey;
    QHash<QString, int> m_sessionCounts;
    class MissionDetailModel* m_owner = nullptr;
    int m_totalCount = 0;
    int m_pageOffset = 0;
    void replace(
        QVector<Entry> entries,
        QHash<QString, int> sessionCounts,
        int totalCount,
        int pageOffset);
    [[nodiscard]] bool perform(
        const QString& presentationId,
        ActionKind actionKind);
    [[nodiscard]] int countForSession(const QString& sessionId) const;
};

class MissionDetailModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString missionId READ missionId NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool membersReady READ membersReady NOTIFY stateChanged)
    Q_PROPERTY(bool tasksReady READ tasksReady NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(kodosi::MissionMembersModel* members READ members CONSTANT)
    Q_PROPERTY(kodosi::MissionMessagesModel* messages READ messages CONSTANT)
    Q_PROPERTY(kodosi::MissionTasksModel* tasks READ tasks CONSTANT)
    Q_PROPERTY(kodosi::MissionCrewModel* crew READ crew CONSTANT)
    Q_PROPERTY(
        kodosi::MissionScopedAttentionModel* attention
        READ attention
        CONSTANT)
    Q_PROPERTY(
        QString selectedCrewPresentationId
        READ selectedCrewPresentationId
        NOTIFY focusChanged)
    Q_PROPERTY(
        QString selectedSessionDisplayName
        READ selectedSessionDisplayName
        NOTIFY focusChanged)
    Q_PROPERTY(
        QString selectedSessionStatus
        READ selectedSessionStatus
        NOTIFY focusChanged)
    Q_PROPERTY(
        QString selectedTerminalSessionId
        READ selectedTerminalSessionId
        NOTIFY focusChanged)
    Q_PROPERTY(
        bool selectedCanOpenFullTerminal
        READ selectedCanOpenFullTerminal
        NOTIFY focusChanged)
    Q_PROPERTY(
        bool selectedCanToggleDispatch
        READ selectedCanToggleDispatch
        NOTIFY focusChanged)
    Q_PROPERTY(
        bool selectedCanSteer
        READ selectedCanSteer
        NOTIFY focusChanged)
    Q_PROPERTY(
        bool selectedCanInterrupt
        READ selectedCanInterrupt
        NOTIFY focusChanged)

public:
    struct Dependencies {
        PeopleModel* people = nullptr;
        SessionCatalogModel* sessions = nullptr;
        AttentionModel* attention = nullptr;
        DesktopStateModel* desktopState = nullptr;
        SteeringModel* steering = nullptr;
        SessionActions* sessionActions = nullptr;
    };

    MissionDetailModel(
        CommandDispatcher& dispatcher,
        MissionDirectoryModel& directory,
        qint64 hydrationTimeoutMs = 45'000,
        QObject* parent = nullptr);
    MissionDetailModel(
        CommandDispatcher& dispatcher,
        MissionDirectoryModel& directory,
        Dependencies dependencies,
        qint64 hydrationTimeoutMs = 45'000,
        QObject* parent = nullptr);

    [[nodiscard]] QString missionId() const;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] bool membersReady() const noexcept;
    [[nodiscard]] bool tasksReady() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] MissionMembersModel* members() noexcept;
    [[nodiscard]] MissionMessagesModel* messages() noexcept;
    [[nodiscard]] MissionTasksModel* tasks() noexcept;
    [[nodiscard]] MissionCrewModel* crew() noexcept;
    [[nodiscard]] MissionScopedAttentionModel* attention() noexcept;
    [[nodiscard]] QString selectedCrewPresentationId() const;
    [[nodiscard]] QString selectedSessionDisplayName() const;
    [[nodiscard]] QString selectedSessionStatus() const;
    [[nodiscard]] QString selectedTerminalSessionId() const;
    [[nodiscard]] bool selectedCanOpenFullTerminal() const;
    [[nodiscard]] bool selectedCanToggleDispatch() const;
    [[nodiscard]] bool selectedCanSteer() const;
    [[nodiscard]] bool selectedCanInterrupt() const;
    [[nodiscard]] static bool isCompleteMessageEntity(
        const QJsonObject& object,
        const QString& missionId);
    [[nodiscard]] static bool isCompleteMemberEntity(
        const QJsonObject& object,
        const QString& missionId);
    [[nodiscard]] static bool isCompleteTaskEntity(
        const QJsonObject& object,
        const QString& missionId);

    Q_INVOKABLE [[nodiscard]] bool openMission(const QString& missionId);
    Q_INVOKABLE void closeMission();
    Q_INVOKABLE [[nodiscard]] bool selectCrew(
        const QString& presentationId);
    Q_INVOKABLE void clearFocus();
    Q_INVOKABLE [[nodiscard]] bool openFocusedFullTerminal();
    Q_INVOKABLE [[nodiscard]] bool toggleFocusedDispatch();
    Q_INVOKABLE [[nodiscard]] bool steerFocused(const QString& text);
    Q_INVOKABLE [[nodiscard]] bool interruptFocused();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool refreshTasks();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestRoomEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void focusChanged();
    void decodeError(QString message);

private:
    friend class MissionActions;
    friend class MissionScopedAttentionModel;

    struct TaskLoad {
        QString token;
        qsizetype nextOffset = 0;
        int pageCount = 0;
        QVector<MissionTasksModel::Task> tasks;
    };

    struct Delivery {
        QString sessionId;
        QString incarnationId;
        QString state;
        QString detail;
    };

    static constexpr qsizetype taskPageSize = 500;
    static constexpr qsizetype maximumTasks = 10'000;
    static constexpr int maximumTaskPages = 20;
    static constexpr qsizetype maximumMessages = 1'000;
    static constexpr qsizetype maximumDeliveries = 256;

    CommandDispatcher& m_dispatcher;
    MissionDirectoryModel& m_directory;
    Dependencies m_dependencies;
    AccountContextFence m_accountFence {256};
    MissionMembersModel m_members;
    MissionMessagesModel m_messages;
    MissionTasksModel m_tasks;
    MissionCrewModel m_crew;
    MissionScopedAttentionModel m_attention;
    MissionActions* m_actions = nullptr;
    QString m_accountUserId;
    QString m_missionId;
    QString m_memberHydrationId;
    QString m_chatHydrationId;
    std::optional<TaskLoad> m_taskLoad;
    QString m_lastError;
    QString m_selectedCrewPresentationId;
    QString m_selectedSessionId;
    QString m_selectedSessionIncarnationId;
    QHash<QString, Delivery> m_deliveries;
    QStringList m_deliveryOrder;
    QSet<QString> m_dispatchSelection;
    QSet<QString> m_staleMessageIds;
    QSet<QString> m_staleTaskIds;
    QTimer m_hydrationTimer;
    qint64 m_hydrationTimeoutMs;
    bool m_membersLoading = false;
    bool m_membersAuthoritative = false;
    bool m_chatLoading = false;
    bool m_chatAuthoritative = false;
    bool m_tasksLoading = false;
    bool m_tasksAuthoritative = false;
    bool m_authenticated = false;

    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void applyRoomEvent(const QJsonObject& object);
    void clearDetail();
    void updateLoading();
    void hydrationTimedOut();
    [[nodiscard]] bool beginFullHydration();
    [[nodiscard]] bool requestTaskPage();
    void rebuildProjections();
    void rebuildAttention();
    [[nodiscard]] bool setAttentionPageOffset(int offset);
    void rebuildCrew();
    void rebuildMessagePresentation();
    void rebuildTaskPresentation();
    void reconcileFocus();
    void setDispatchSelection(QSet<QString> presentationIds);
    [[nodiscard]] QSet<QString> currentMissionSessionIds() const;
    [[nodiscard]] QString displayForUser(const QString& userId) const;
    [[nodiscard]] QString displayForSession(
        const QString& sessionId,
        const QString& incarnationId = {}) const;
    [[nodiscard]] QString statusLabel(const QString& status) const;
    [[nodiscard]] std::optional<MissionCrewModel::Entry> selectedCrewEntry()
        const;
    [[nodiscard]] bool performAttentionAction(
        const MissionScopedAttentionModel::Entry& entry,
        MissionScopedAttentionModel::ActionKind actionKind,
        bool deny = false);

    [[nodiscard]] static std::optional<MissionMembersModel::Member> decodeMember(
        const QJsonObject& object,
        const QString& missionId);
    [[nodiscard]] static std::optional<MissionMessagesModel::Message> decodeMessage(
        const QJsonObject& object,
        const QString& missionId,
        bool requireRecipientArrays = false);
    [[nodiscard]] static std::optional<MissionTasksModel::Task> decodeTask(
        const QJsonObject& object,
        const QString& missionId);
};

} // namespace kodosi
