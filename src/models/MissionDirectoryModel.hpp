#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace kodosi {

class MissionInvitationsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int incomingCount READ incomingCount NOTIFY countChanged)

public:
    enum class Direction {
        Incoming,
        Outgoing,
    };
    Q_ENUM(Direction)

    enum Role {
        InvitationIdRole = Qt::UserRole + 1,
        RoomIdRole,
        RoomNameRole,
        RoomSlugRole,
        DirectionRole,
        CounterpartyHandleRole,
        CounterpartyDisplayNameRole,
        StatusRole,
        CreatedAtRole,
    };
    Q_ENUM(Role)

    explicit MissionInvitationsModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int incomingCount() const noexcept;

signals:
    void countChanged();

private:
    friend class MissionDirectoryModel;

    struct Invitation {
        QString id;
        QString roomId;
        QString roomName;
        QString roomSlug;
        Direction direction;
        QString counterpartyHandle;
        QString counterpartyDisplayName;
        QString status;
        QDateTime createdAt;
        QString inviteeUserId;
        qint64 baseRosterGeneration;
    };

    QVector<Invitation> m_invitations;
    void replace(QVector<Invitation> invitations);
};

class MissionDirectoryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool invitationsReady READ invitationsReady NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(
        kodosi::MissionInvitationsModel* invitations
        READ invitations
        CONSTANT)

public:
    struct MissionActionContext {
        QString ownerUserId;
        qint64 rosterGeneration;
    };

    struct InvitationActionContext {
        QString roomId;
        QString inviteeUserId;
        MissionInvitationsModel::Direction direction;
        qint64 baseRosterGeneration;
    };

    enum Role {
        MissionIdRole = Qt::UserRole + 1,
        NameRole,
        SlugRole,
        OwnerUserIdRole,
        RosterGenerationRole,
    };
    Q_ENUM(Role)

    explicit MissionDirectoryModel(
        CommandDispatcher& dispatcher,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] bool invitationsReady() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] MissionInvitationsModel* invitations() noexcept;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool containsMission(const QString& missionId) const;
    [[nodiscard]] std::optional<MissionActionContext> actionContext(
        const QString& missionId) const;
    [[nodiscard]] std::optional<InvitationActionContext>
    invitationActionContext(const QString& invitationId) const;
    [[nodiscard]] bool hasOutgoingInvitation(
        const QString& missionId,
        const QString& inviteeUserId) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestRoomEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void decodeError(QString message);

private:
    struct Mission {
        QString id;
        QString name;
        QString slug;
        QString ownerUserId;
        qint64 rosterGeneration;
    };

    CommandDispatcher& m_dispatcher;
    AccountContextFence m_accountFence {256};
    MissionInvitationsModel m_invitations;
    QVector<Mission> m_missions;
    QString m_lastError;
    bool m_loading = false;
    bool m_authenticated = false;
    bool m_invitationsReady = false;

    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void applyRoomEvent(const QJsonObject& object);
    void clearAccountState();
    void replaceMissions(QVector<Mission> missions);

    [[nodiscard]] static std::optional<Mission> decodeMission(
        const QJsonObject& object);
    [[nodiscard]] static std::optional<MissionInvitationsModel::Invitation>
    decodeInvitation(
        const QJsonObject& object,
        MissionInvitationsModel::Direction direction);
};

} // namespace kodosi
