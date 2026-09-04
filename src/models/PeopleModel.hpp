#pragma once

#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace kodosi {

class PeopleModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int friendCount READ friendCount NOTIFY relationshipsChanged)
    Q_PROPERTY(
        int incomingCount
        READ incomingCount
        NOTIFY relationshipsChanged)
    Q_PROPERTY(
        int outgoingCount
        READ outgoingCount
        NOTIFY relationshipsChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readinessChanged)
    Q_PROPERTY(
        QVariantList friendPresentations
        READ friendPresentations
        NOTIFY relationshipsChanged)

public:
    enum class Relationship {
        Friend,
        IncomingRequest,
        OutgoingRequest,
    };
    Q_ENUM(Relationship)

    enum Role {
        UserIdRole = Qt::UserRole + 1,
        HandleRole,
        DisplayNameRole,
        AvatarUrlRole,
        RelationshipRole,
        RelationshipNameRole,
        CreatedAtRole,
    };
    Q_ENUM(Role)

    explicit PeopleModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int friendCount() const noexcept;
    [[nodiscard]] int incomingCount() const noexcept;
    [[nodiscard]] int outgoingCount() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] QVariantList friendPresentations() const;
    [[nodiscard]] std::optional<Relationship> relationshipForHandle(
        const QString& handle) const;
    [[nodiscard]] std::optional<QString> friendUserIdForHandle(
        const QString& handle) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestFriendsEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void relationshipsChanged();
    void readinessChanged();
    void decodeError(QString message);
    void operationError(QString operation, QString message, QString requestId);

private:
    struct Person {
        QString userId;
        QString handle;
        QString displayName;
        QUrl avatarUrl;
        Relationship relationship;
        QString createdAt;
    };

    QVector<Person> m_people;
    AccountContextFence m_accountFence {128};
    bool m_ready = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyFriendsEvent(const QJsonObject& object);
    static std::optional<Person> decodePerson(
        const QJsonObject& object,
        Relationship relationship);
    void replaceSnapshot(QVector<Person> people);
    void setReady(bool ready);
};

} // namespace kodosi
