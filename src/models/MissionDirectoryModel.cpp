#include "models/MissionDirectoryModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> requiredString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    if (!value.isString() || value.toString().isEmpty()) {
        return std::nullopt;
    }
    return value.toString();
}

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

std::optional<qint64> exactSignedInteger(const QJsonValue& value)
{
    constexpr auto maximumExactJsonInteger = 9'007'199'254'740'991.0;
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < -maximumExactJsonInteger
        || number > maximumExactJsonInteger || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

std::optional<QDateTime> rfc3339(const QString& value)
{
    static const QRegularExpression grammar(
        QStringLiteral(
            R"(^\d{4}-\d{2}-\d{2}T(?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d(?:\.\d+)?(?:Z|[+-](?:[01]\d|2[0-3]):[0-5]\d)$)"));
    if (!grammar.match(value).hasMatch()) {
        return std::nullopt;
    }
    auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(value, Qt::ISODate);
    }
    return date.isValid() && date.timeSpec() != Qt::LocalTime
        ? std::optional<QDateTime> {date.toUTC()}
        : std::nullopt;
}

} // namespace

MissionInvitationsModel::MissionInvitationsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionInvitationsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_invitations.size();
}

QVariant MissionInvitationsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_invitations.size()) {
        return {};
    }
    const auto& invitation = m_invitations[index.row()];
    switch (role) {
    case InvitationIdRole:
        return invitation.id;
    case RoomIdRole:
        return invitation.roomId;
    case RoomNameRole:
        return invitation.roomName;
    case RoomSlugRole:
        return invitation.roomSlug;
    case DirectionRole:
        return QVariant::fromValue(invitation.direction);
    case CounterpartyHandleRole:
        return invitation.counterpartyHandle;
    case CounterpartyDisplayNameRole:
        return invitation.counterpartyDisplayName;
    case StatusRole:
        return invitation.status;
    case CreatedAtRole:
        return invitation.createdAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> MissionInvitationsModel::roleNames() const
{
    return {
        {InvitationIdRole, QByteArrayLiteral("invitationId")},
        {RoomIdRole, QByteArrayLiteral("roomId")},
        {RoomNameRole, QByteArrayLiteral("roomName")},
        {RoomSlugRole, QByteArrayLiteral("roomSlug")},
        {DirectionRole, QByteArrayLiteral("direction")},
        {CounterpartyHandleRole, QByteArrayLiteral("counterpartyHandle")},
        {CounterpartyDisplayNameRole, QByteArrayLiteral("counterpartyDisplayName")},
        {StatusRole, QByteArrayLiteral("status")},
        {CreatedAtRole, QByteArrayLiteral("createdAt")},
    };
}

int MissionInvitationsModel::incomingCount() const noexcept
{
    return static_cast<int>(std::ranges::count(
        m_invitations,
        Direction::Incoming,
        &Invitation::direction));
}

void MissionInvitationsModel::replace(QVector<Invitation> invitations)
{
    std::ranges::sort(invitations, [](const Invitation& lhs, const Invitation& rhs) {
        return lhs.createdAt == rhs.createdAt
            ? lhs.id < rhs.id
            : lhs.createdAt > rhs.createdAt;
    });
    beginResetModel();
    const auto countChangedValue =
        m_invitations.size() != invitations.size()
        || incomingCount()
            != static_cast<int>(std::ranges::count(
                invitations,
                Direction::Incoming,
                &Invitation::direction));
    m_invitations = std::move(invitations);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

MissionDirectoryModel::MissionDirectoryModel(
    CommandDispatcher& dispatcher,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_invitations(this)
{
}

int MissionDirectoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_missions.size();
}

QVariant MissionDirectoryModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_missions.size()) {
        return {};
    }
    const auto& mission = m_missions[index.row()];
    switch (role) {
    case MissionIdRole:
        return mission.id;
    case NameRole:
        return mission.name;
    case SlugRole:
        return mission.slug;
    default:
        return {};
    }
}

QHash<int, QByteArray> MissionDirectoryModel::roleNames() const
{
    return {
        {MissionIdRole, QByteArrayLiteral("missionId")},
        {NameRole, QByteArrayLiteral("name")},
        {SlugRole, QByteArrayLiteral("slug")},
    };
}

bool MissionDirectoryModel::loading() const noexcept
{
    return m_loading;
}

MissionDirectoryModel::AuthorityState
MissionDirectoryModel::authorityState() const noexcept
{
    return m_authorityState;
}

bool MissionDirectoryModel::staleDataVisible() const noexcept
{
    return m_hasAuthoritativeSnapshot
        && m_authorityState != AuthorityState::Loaded;
}

bool MissionDirectoryModel::canRefresh() const noexcept
{
    return m_authenticated && !loading();
}

bool MissionDirectoryModel::canRetry() const noexcept
{
    return m_authenticated && !m_loading
        && m_authorityState == AuthorityState::Failed;
}

bool MissionDirectoryModel::invitationsReady() const noexcept
{
    return m_invitationsReady;
}

QString MissionDirectoryModel::lastError() const
{
    return m_lastError;
}

MissionInvitationsModel* MissionDirectoryModel::invitations() noexcept
{
    return &m_invitations;
}

bool MissionDirectoryModel::hasAuthoritativeSnapshot() const noexcept
{
    return m_hasAuthoritativeSnapshot;
}

bool MissionDirectoryModel::refresh()
{
    if (!m_authenticated) {
        return false;
    }
    if (m_loading) {
        if (m_authorityState == AuthorityState::Failed) {
            return false;
        }
        m_refreshQueued = true;
        return true;
    }
    if (m_refreshGeneration == std::numeric_limits<quint64>::max()) {
        setAuthorityState(
            AuthorityState::Failed,
            QStringLiteral(
                "The Mission refresh generation is exhausted."));
        return false;
    }
    ++m_refreshGeneration;
    m_loading = true;
    m_invitationsReady = false;
    m_roomRefreshState = RefreshHalfState::Pending;
    m_invitationRefreshState = RefreshHalfState::Pending;
    setAuthorityState(
        m_authorityState == AuthorityState::Failed
            ? AuthorityState::Recovering
            : m_hasAuthoritativeSnapshot ? AuthorityState::Stale
                                         : AuthorityState::Loading);
    const auto rooms = QByteArrayLiteral("{\"type\":\"room.refresh\"}");
    const auto invitations =
        QByteArrayLiteral("{\"type\":\"room.refreshInvitations\"}");
    const auto roomsAccepted = m_dispatcher.send(CommandLane::Rooms, rooms);
    const auto invitationsAccepted =
        m_dispatcher.send(CommandLane::Rooms, invitations);
    if (roomsAccepted && invitationsAccepted) {
        return true;
    }
    if (!roomsAccepted) {
        m_roomRefreshState = RefreshHalfState::Failed;
    }
    if (!invitationsAccepted) {
        m_invitationRefreshState = RefreshHalfState::Failed;
    }
    failRefreshHalf(
        !roomsAccepted ? m_roomRefreshState : m_invitationRefreshState,
        QStringLiteral("The runtime did not accept the Mission refresh."));
    return false;
}

bool MissionDirectoryModel::containsMission(const QString& missionId) const
{
    return std::ranges::find(m_missions, missionId, &Mission::id)
        != m_missions.end();
}

std::optional<MissionDirectoryModel::MissionActionContext>
MissionDirectoryModel::actionContext(const QString& missionId) const
{
    const auto found = std::ranges::find(m_missions, missionId, &Mission::id);
    return found == m_missions.end()
        ? std::nullopt
        : std::optional<MissionActionContext> {{
              .ownerUserId = found->ownerUserId,
              .rosterGeneration = found->rosterGeneration,
          }};
}

std::optional<MissionDirectoryModel::InvitationActionContext>
MissionDirectoryModel::invitationActionContext(
    const QString& invitationId) const
{
    const auto found = std::ranges::find(
        m_invitations.m_invitations,
        invitationId,
        &MissionInvitationsModel::Invitation::id);
    return found == m_invitations.m_invitations.end()
        ? std::nullopt
        : std::optional<InvitationActionContext> {{
              .roomId = found->roomId,
              .inviteeUserId = found->inviteeUserId,
              .direction = found->direction,
              .baseRosterGeneration = found->baseRosterGeneration,
          }};
}

bool MissionDirectoryModel::hasOutgoingInvitation(
    const QString& missionId,
    const QString& inviteeUserId) const
{
    return std::ranges::any_of(
        m_invitations.m_invitations,
        [&](const MissionInvitationsModel::Invitation& invitation) {
            return invitation.direction
                    == MissionInvitationsModel::Direction::Outgoing
                && invitation.roomId == missionId
                && invitation.inviteeUserId == inviteeUserId;
        });
}

void MissionDirectoryModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
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
        emit decodeError(
            QStringLiteral("Authentication context has no exact account epoch."));
        return;
    }
    activateAccount(
        type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        *epoch,
        type == QStringLiteral("auth.ready"));
}

void MissionDirectoryModel::ingestRoomEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Mission event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Mission event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Mission event has no exact account epoch."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = optionalString(object, QStringLiteral("accountUserId")),
            .epoch = *epoch,
        },
        std::move(json));
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(
            QStringLiteral("Future Mission event exceeds the ABI frame limit."));
    }
    if (admission == AccountEventAdmission::Current) {
        applyRoomEvent(object);
    }
}

void MissionDirectoryModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    clearAccountState();
}

void MissionDirectoryModel::activateAccount(
    QString userId,
    const quint64 epoch,
    const bool authenticated)
{
    auto activation = m_accountFence.activate({
        .userId = std::move(userId),
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    if (activation.changed || m_authenticated != authenticated) {
        clearAccountState();
    }
    m_authenticated = authenticated;
    if (activation.changed && authenticated) {
        (void)refresh();
    }
    for (auto& pending : activation.pendingEvents) {
        ingestRoomEvent(std::move(pending));
    }
}

void MissionDirectoryModel::applyRoomEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Mission event has no type."));
        return;
    }
    if (*type == QStringLiteral("room.snapshot")) {
        const auto values = object.value(QStringLiteral("rooms"));
        if (!values.isArray()) {
            emit decodeError(QStringLiteral("Mission directory is incomplete."));
            failRefreshHalf(
                m_roomRefreshState,
                QStringLiteral("The Mission directory response was incomplete."));
            return;
        }
        QVector<Mission> missions;
        QSet<QString> ids;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()) {
                emit decodeError(QStringLiteral("Mission directory has an invalid row."));
                failRefreshHalf(
                    m_roomRefreshState,
                    QStringLiteral("The Mission directory response was invalid."));
                return;
            }
            auto mission = decodeMission(value.toObject());
            if (!mission || ids.contains(mission->id)) {
                emit decodeError(QStringLiteral("Mission directory has an invalid row."));
                failRefreshHalf(
                    m_roomRefreshState,
                    QStringLiteral("The Mission directory response was invalid."));
                return;
            }
            ids.insert(mission->id);
            missions.push_back(std::move(*mission));
        }
        replaceMissions(std::move(missions));
        m_hasAuthoritativeSnapshot = true;
        completeRefreshHalf(m_roomRefreshState);
        return;
    }
    if (*type == QStringLiteral("room.invitations")) {
        const auto incoming = object.value(QStringLiteral("incoming"));
        const auto outgoing = object.value(QStringLiteral("outgoing"));
        if (!incoming.isArray() || !outgoing.isArray()) {
            emit decodeError(QStringLiteral("Mission invitations are incomplete."));
            failRefreshHalf(
                m_invitationRefreshState,
                QStringLiteral("The Mission invitation response was incomplete."));
            return;
        }
        QVector<MissionInvitationsModel::Invitation> invitations;
        QSet<QString> ids;
        const auto decode = [&](const QJsonArray& values, const auto direction) {
            for (const auto& value : values) {
                if (!value.isObject()) {
                    return false;
                }
                auto invitation = decodeInvitation(value.toObject(), direction);
                if (!invitation || ids.contains(invitation->id)) {
                    return false;
                }
                ids.insert(invitation->id);
                invitations.push_back(std::move(*invitation));
            }
            return true;
        };
        if (!decode(incoming.toArray(), MissionInvitationsModel::Direction::Incoming)
            || !decode(
                outgoing.toArray(),
                MissionInvitationsModel::Direction::Outgoing)) {
            emit decodeError(QStringLiteral("Mission invitations have an invalid row."));
            failRefreshHalf(
                m_invitationRefreshState,
                QStringLiteral("The Mission invitation response was invalid."));
            return;
        }
        m_invitations.replace(std::move(invitations));
        m_invitationsReady = true;
        completeRefreshHalf(m_invitationRefreshState);
        return;
    }
    if (*type == QStringLiteral("room.error")) {
        const auto operation = requiredString(object, QStringLiteral("operation"));
        const auto message = requiredString(object, QStringLiteral("message"));
        if (!operation || !message) {
            emit decodeError(QStringLiteral("Mission error event is invalid."));
            return;
        }
        if (*operation == QStringLiteral("refresh")) {
            failRefreshHalf(m_roomRefreshState, *message);
            return;
        }
        if (*operation == QStringLiteral("refreshInvitations")) {
            m_invitationsReady = false;
            failRefreshHalf(m_invitationRefreshState, *message);
            return;
        }
        m_lastError = *message;
        emit stateChanged();
    }
}

void MissionDirectoryModel::clearAccountState()
{
    m_authenticated = false;
    m_loading = false;
    m_invitationsReady = false;
    m_hasAuthoritativeSnapshot = false;
    m_refreshQueued = false;
    m_refreshGeneration = 0;
    m_roomRefreshState = RefreshHalfState::Idle;
    m_invitationRefreshState = RefreshHalfState::Idle;
    m_lastError.clear();
    m_authorityState = AuthorityState::Loading;
    replaceMissions({});
    m_invitations.replace({});
    emit stateChanged();
}

void MissionDirectoryModel::completeRefreshHalf(RefreshHalfState& half)
{
    if (half == RefreshHalfState::Pending) {
        half = RefreshHalfState::Complete;
    }
    finishRefreshIfReady();
}

void MissionDirectoryModel::failRefreshHalf(
    RefreshHalfState& half,
    QString error)
{
    half = RefreshHalfState::Failed;
    m_refreshQueued = false;
    setAuthorityState(AuthorityState::Failed, std::move(error));
    finishRefreshIfReady();
}

void MissionDirectoryModel::finishRefreshIfReady()
{
    if (m_roomRefreshState == RefreshHalfState::Failed
        || m_invitationRefreshState == RefreshHalfState::Failed) {
        if (m_roomRefreshState != RefreshHalfState::Pending
            && m_invitationRefreshState != RefreshHalfState::Pending) {
            m_loading = false;
        }
        emit stateChanged();
        return;
    }
    if (m_roomRefreshState != RefreshHalfState::Complete
        || m_invitationRefreshState != RefreshHalfState::Complete) {
        emit stateChanged();
        return;
    }
    m_loading = false;
    const auto refreshQueued = std::exchange(m_refreshQueued, false);
    setAuthorityState(AuthorityState::Loaded);
    if (refreshQueued) {
        (void)refresh();
    }
}

void MissionDirectoryModel::setAuthorityState(
    const AuthorityState state,
    QString error)
{
    if (m_authorityState == state && m_lastError == error) {
        return;
    }
    m_authorityState = state;
    m_lastError = std::move(error);
    emit stateChanged();
}

void MissionDirectoryModel::replaceMissions(QVector<Mission> missions)
{
    std::ranges::sort(missions, {}, &Mission::name);
    beginResetModel();
    const auto changed = m_missions.size() != missions.size();
    m_missions = std::move(missions);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

bool MissionDirectoryModel::isCompleteRoomEntity(
    const QJsonObject& object)
{
    return decodeMission(object).has_value();
}

bool MissionDirectoryModel::isCompleteInvitationEntity(
    const QJsonObject& object,
    const MissionInvitationsModel::Direction direction)
{
    return decodeInvitation(object, direction).has_value();
}

std::optional<MissionDirectoryModel::Mission>
MissionDirectoryModel::decodeMission(const QJsonObject& object)
{
    const auto id = requiredString(object, QStringLiteral("id"));
    const auto name = requiredString(object, QStringLiteral("name"));
    const auto slug = requiredString(object, QStringLiteral("slug"));
    const auto owner = requiredString(object, QStringLiteral("ownerUserId"));
    const auto generation =
        exactSignedInteger(object.value(QStringLiteral("rosterGeneration")));
    if (!id || !name || !slug || !owner || !generation || *generation < 0) {
        return std::nullopt;
    }
    return Mission {
        .id = *id,
        .name = *name,
        .slug = *slug,
        .ownerUserId = *owner,
        .rosterGeneration = *generation,
    };
}

std::optional<MissionInvitationsModel::Invitation>
MissionDirectoryModel::decodeInvitation(
    const QJsonObject& object,
    const MissionInvitationsModel::Direction direction)
{
    const auto id = requiredString(object, QStringLiteral("id"));
    const auto roomId = requiredString(object, QStringLiteral("roomId"));
    const auto roomName = requiredString(object, QStringLiteral("roomName"));
    const auto roomSlug = requiredString(object, QStringLiteral("roomSlug"));
    const auto status = requiredString(object, QStringLiteral("status"));
    const auto inviteeUserId =
        requiredString(object, QStringLiteral("inviteeUserId"));
    const auto invitedByUserId =
        requiredString(object, QStringLiteral("invitedByUserId"));
    const auto rawCreatedAt = requiredString(object, QStringLiteral("createdAt"));
    const auto createdAt = rawCreatedAt ? rfc3339(*rawCreatedAt) : std::nullopt;
    const auto inviteeHandle =
        requiredString(object, QStringLiteral("inviteeHandle"));
    const auto invitedByHandle =
        requiredString(object, QStringLiteral("invitedByHandle"));
    const auto displayNameKey =
        direction == MissionInvitationsModel::Direction::Incoming
        ? QStringLiteral("invitedByDisplayName")
        : QStringLiteral("inviteeDisplayName");
    const auto baseGeneration =
        exactSignedInteger(object.value(QStringLiteral("baseRosterGeneration")));
    const auto proposedGeneration = exactSignedInteger(
        object.value(QStringLiteral("proposedRosterGeneration")));
    if (!id || !roomId || !roomName || !roomSlug || !status || !inviteeUserId
        || !invitedByUserId || !createdAt || !inviteeHandle || !invitedByHandle
        || !baseGeneration || !proposedGeneration
        || *baseGeneration < 0 || *proposedGeneration < 0) {
        return std::nullopt;
    }
    return MissionInvitationsModel::Invitation {
        .id = *id,
        .roomId = *roomId,
        .roomName = *roomName,
        .roomSlug = *roomSlug,
        .direction = direction,
        .counterpartyHandle =
            direction == MissionInvitationsModel::Direction::Incoming
            ? *invitedByHandle
            : *inviteeHandle,
        .counterpartyDisplayName = optionalString(object, displayNameKey),
        .status = *status,
        .createdAt = *createdAt,
        .inviteeUserId = *inviteeUserId,
        .baseRosterGeneration = *baseGeneration,
    };
}

} // namespace kodosi
