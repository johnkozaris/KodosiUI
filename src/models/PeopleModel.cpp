#include "models/PeopleModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <optional>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

std::optional<QString> requiredString(const QJsonObject& object, const QString& key)
{
    const auto value = optionalString(object, key);
    return value.isEmpty() ? std::nullopt : std::optional<QString> {value};
}

} // namespace

PeopleModel::PeopleModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PeopleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_people.size();
}

QVariant PeopleModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_people.size()) {
        return {};
    }
    const auto& person = m_people[index.row()];
    switch (role) {
    case UserIdRole:
        return person.userId;
    case HandleRole:
        return person.handle;
    case DisplayNameRole:
        return person.displayName;
    case AvatarUrlRole:
        return person.avatarUrl;
    case RelationshipRole:
        return QVariant::fromValue(person.relationship);
    case RelationshipNameRole:
        switch (person.relationship) {
        case Relationship::Friend:
            return QStringLiteral("friend");
        case Relationship::IncomingRequest:
            return QStringLiteral("incoming");
        case Relationship::OutgoingRequest:
            return QStringLiteral("outgoing");
        }
        return {};
    case CreatedAtRole:
        return person.createdAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const
{
    return {
        {UserIdRole, QByteArrayLiteral("userId")},
        {HandleRole, QByteArrayLiteral("handle")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {AvatarUrlRole, QByteArrayLiteral("avatarUrl")},
        {RelationshipRole, QByteArrayLiteral("relationship")},
        {RelationshipNameRole, QByteArrayLiteral("relationshipName")},
        {CreatedAtRole, QByteArrayLiteral("createdAt")},
    };
}

int PeopleModel::friendCount() const noexcept
{
    return static_cast<int>(std::ranges::count(
        m_people,
        Relationship::Friend,
        &Person::relationship));
}

int PeopleModel::incomingCount() const noexcept
{
    return static_cast<int>(std::ranges::count(
        m_people,
        Relationship::IncomingRequest,
        &Person::relationship));
}

int PeopleModel::outgoingCount() const noexcept
{
    return static_cast<int>(std::ranges::count(
        m_people,
        Relationship::OutgoingRequest,
        &Person::relationship));
}

bool PeopleModel::ready() const noexcept
{
    return m_ready;
}

QVariantList PeopleModel::friendPresentations() const
{
    QVariantList result;
    for (const auto& person : m_people) {
        if (person.relationship != Relationship::Friend) {
            continue;
        }
        result.push_back(QVariantMap {
            {QStringLiteral("handle"), person.handle},
            {QStringLiteral("displayName"),
             person.displayName.isEmpty()
                 ? person.handle
                 : person.displayName},
        });
    }
    return result;
}

std::optional<PeopleModel::Relationship> PeopleModel::relationshipForHandle(
    const QString& handle) const
{
    const auto normalized = handle.trimmed().toLower();
    const auto found = std::ranges::find_if(m_people, [&](const Person& person) {
        return person.handle.toLower() == normalized;
    });
    return found == m_people.end()
        ? std::nullopt
        : std::optional<Relationship> {found->relationship};
}

std::optional<QString> PeopleModel::friendUserIdForHandle(
    const QString& handle) const
{
    auto normalized = handle.trimmed();
    if (normalized.startsWith(u'@')) {
        normalized.removeFirst();
    }
    normalized = normalized.trimmed().toLower();
    const auto found = std::ranges::find_if(m_people, [&](const Person& person) {
        return person.handle.toLower() == normalized
            && person.relationship == Relationship::Friend;
    });
    return found == m_people.end()
        ? std::nullopt
        : std::optional<QString> {found->userId};
}

void PeopleModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = optionalString(object, QStringLiteral("type"));
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Authentication context has no exact account epoch."));
        return;
    }
    activateAccount(
        type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        *epoch);
}

void PeopleModel::ingestFriendsEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Friends event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Friends event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Friends event has no exact account epoch."));
        return;
    }
    const auto userId = optionalString(object, QStringLiteral("accountUserId"));
    const auto admission =
        m_accountFence.admit({.userId = userId, .epoch = *epoch}, std::move(json));
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(QStringLiteral("Future friends event exceeds the ABI frame limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    applyFriendsEvent(object);
}

void PeopleModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    replaceSnapshot({});
    setReady(false);
}

void PeopleModel::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = std::move(userId), .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        replaceSnapshot({});
        setReady(false);
    }
    for (auto& json : activation.pendingEvents) {
        ingestFriendsEvent(std::move(json));
    }
}

void PeopleModel::applyFriendsEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Friends event has no type."));
        return;
    }
    if (*type == QStringLiteral("friends.error")) {
        const auto operation = requiredString(object, QStringLiteral("operation"));
        const auto message = requiredString(object, QStringLiteral("message"));
        if (!operation || !message) {
            emit decodeError(QStringLiteral("Friends error event is invalid."));
            return;
        }
        emit operationError(
            *operation,
            *message,
            optionalString(object, QStringLiteral("requestId")));
        return;
    }
    if (*type != QStringLiteral("friends.snapshot")) {
        return;
    }

    QVector<Person> people;
    const auto decode = [&](const QString& key, const Relationship relationship) {
        const auto value = object.value(key);
        if (!value.isArray()) {
            return false;
        }
        for (const auto& row : value.toArray()) {
            if (!row.isObject()) {
                return false;
            }
            auto person = decodePerson(row.toObject(), relationship);
            if (!person) {
                return false;
            }
            people.push_back(std::move(*person));
        }
        return true;
    };
    if (!decode(QStringLiteral("friends"), Relationship::Friend)
        || !decode(QStringLiteral("incoming"), Relationship::IncomingRequest)
        || !decode(QStringLiteral("outgoing"), Relationship::OutgoingRequest)) {
        emit decodeError(QStringLiteral("Friends snapshot is incomplete or invalid."));
        return;
    }
    replaceSnapshot(std::move(people));
    setReady(true);
}

std::optional<PeopleModel::Person> PeopleModel::decodePerson(
    const QJsonObject& object,
    const Relationship relationship)
{
    const auto userId = requiredString(object, QStringLiteral("userId"));
    const auto handle = requiredString(object, QStringLiteral("handle"));
    const auto displayName = requiredString(object, QStringLiteral("displayName"));
    if (!userId || !handle || !displayName) {
        return std::nullopt;
    }
    QUrl avatarUrl;
    const auto avatar = optionalString(object, QStringLiteral("avatarUrl"));
    if (!avatar.isEmpty()) {
        avatarUrl = QUrl(avatar, QUrl::StrictMode);
        if (!avatarUrl.isValid() || avatarUrl.scheme() != QStringLiteral("https")
            || avatarUrl.host().isEmpty()) {
            return std::nullopt;
        }
    }
    const auto createdAt = optionalString(object, QStringLiteral("createdAt"));
    if (relationship != Relationship::Friend && createdAt.isEmpty()) {
        return std::nullopt;
    }
    return Person {
        .userId = *userId,
        .handle = *handle,
        .displayName = *displayName,
        .avatarUrl = avatarUrl,
        .relationship = relationship,
        .createdAt = createdAt,
    };
}

void PeopleModel::replaceSnapshot(QVector<Person> people)
{
    std::ranges::sort(people, {}, &Person::displayName);
    beginResetModel();
    const auto changed = m_people.size() != people.size();
    m_people = std::move(people);
    endResetModel();
    emit relationshipsChanged();
    if (changed) {
        emit countChanged();
    }
}

void PeopleModel::setReady(const bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readinessChanged();
}

} // namespace kodosi
