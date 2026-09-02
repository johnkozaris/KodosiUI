#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/MissionDirectoryModel.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

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
        qint64 sequence;
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
        qint64 revision;
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
        QString assignedSessionId;
        QString assignedSessionIncarnationId;
        QString result;
        qint64 revision;
        QDateTime dueAt;
        QDateTime updatedAt;
    };
    QVector<Task> m_tasks;
    void replace(QVector<Task> tasks);
    void upsert(Task task);
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

public:
    MissionDetailModel(
        CommandDispatcher& dispatcher,
        MissionDirectoryModel& directory,
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

    Q_INVOKABLE [[nodiscard]] bool openMission(const QString& missionId);
    Q_INVOKABLE void closeMission();
    [[nodiscard]] bool refreshTasks();

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestRoomEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void decodeError(QString message);

private:
    struct TaskLoad {
        QString token;
        qsizetype nextOffset = 0;
        int pageCount = 0;
        QVector<MissionTasksModel::Task> tasks;
    };

    static constexpr qsizetype taskPageSize = 100;
    static constexpr qsizetype maximumTasks = 2'000;
    static constexpr int maximumTaskPages = 20;

    CommandDispatcher& m_dispatcher;
    MissionDirectoryModel& m_directory;
    AccountContextFence m_accountFence {256};
    MissionMembersModel m_members;
    MissionMessagesModel m_messages;
    MissionTasksModel m_tasks;
    QString m_missionId;
    QString m_memberHydrationId;
    QString m_chatHydrationId;
    std::optional<TaskLoad> m_taskLoad;
    QString m_lastError;
    QTimer m_hydrationTimer;
    qint64 m_hydrationTimeoutMs;
    bool m_membersLoading = false;
    bool m_membersAuthoritative = false;
    bool m_chatLoading = false;
    bool m_tasksLoading = false;
    bool m_tasksAuthoritative = false;
    bool m_authenticated = false;

    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void applyRoomEvent(const QJsonObject& object);
    void clearDetail();
    void updateLoading();
    void hydrationTimedOut();
    [[nodiscard]] bool requestTaskPage();

    [[nodiscard]] static std::optional<MissionMembersModel::Member> decodeMember(
        const QJsonObject& object,
        const QString& missionId);
    [[nodiscard]] static std::optional<MissionMessagesModel::Message> decodeMessage(
        const QJsonObject& object,
        const QString& missionId);
    [[nodiscard]] static std::optional<MissionTasksModel::Task> decodeTask(
        const QJsonObject& object,
        const QString& missionId);
};

} // namespace kodosi
