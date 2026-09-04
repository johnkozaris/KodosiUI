#include "models/SessionCatalogModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr quint32 viewPermission = 1U << 0U;
constexpr quint32 sendInputPermission = 1U << 1U;
constexpr quint32 resizePermission = 1U << 2U;
constexpr quint32 focusBlurPermission = 1U << 3U;

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

bool known(const QString& value, const std::initializer_list<QString> allowed)
{
    return std::ranges::find(allowed, value) != allowed.end();
}

} // namespace

SessionCatalogModel::SessionCatalogModel(QObject* parent)
    : SessionCatalogModel(15'000, parent)
{
}

SessionCatalogModel::SessionCatalogModel(
    const qint64 refreshTimeoutMs,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_refreshTimeoutMs(refreshTimeoutMs)
{
    Q_ASSERT(refreshTimeoutMs >= 0);
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (!m_refreshPending) {
            return;
        }
        settleRefreshFailure(
            QStringLiteral(
                "Couldn't load sessions. The runtime did not respond."));
    });
}

int SessionCatalogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_sessions.size();
}

QVariant SessionCatalogModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size()) {
        return {};
    }
    const auto& session = m_sessions[index.row()];
    switch (role) {
    case SessionIdRole:
        return session.id;
    case IncarnationIdRole:
        return session.incarnationId;
    case KindRole:
        return session.kind;
    case NameRole:
        return session.name;
    case ProjectRole:
        return session.project;
    case ModeRole:
        return session.mode;
    case StatusRole:
        return session.status;
    case RecoveryRole:
        return session.recovery;
    case ScopeRole:
        return session.scope;
    case AccessRole:
        return session.access;
    case RoomNameRole:
        return session.roomName;
    case OwnerRole:
        return session.owner;
    case PermissionsRole:
        return session.permissions;
    case CommandableRole:
        return session.commandable;
    case CanQueueRole:
        return session.commandable && session.canQueue;
    case CanSteerRole:
        return session.commandable && session.canSteer;
    case CanStopAndSendRole:
        return session.commandable && session.canStopAndSend;
    case CanRetainPresentationRole:
        return projectPresentation(session).canRetainPresentation;
    case IsStageReadyRole:
        return projectPresentation(session).isStageReady;
    case IsRemoteConnectableRole:
        return projectPresentation(session).isRemoteConnectable;
    case CanSendInputRole:
        return projectPresentation(session).canSendInput;
    case CanRetainFocusRole:
        return projectPresentation(session).canRetainFocus;
    case CanSendFocusRole:
        return projectPresentation(session).canSendFocus;
    case CanResizeRole:
        return projectPresentation(session).canResize;
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionCatalogModel::roleNames() const
{
    return {
        {SessionIdRole, QByteArrayLiteral("sessionId")},
        {KindRole, QByteArrayLiteral("kind")},
        {NameRole, QByteArrayLiteral("name")},
        {ProjectRole, QByteArrayLiteral("project")},
        {ModeRole, QByteArrayLiteral("mode")},
        {StatusRole, QByteArrayLiteral("status")},
        {RecoveryRole, QByteArrayLiteral("recovery")},
        {ScopeRole, QByteArrayLiteral("scope")},
        {AccessRole, QByteArrayLiteral("access")},
        {RoomNameRole, QByteArrayLiteral("roomName")},
        {OwnerRole, QByteArrayLiteral("owner")},
        {PermissionsRole, QByteArrayLiteral("permissions")},
        {CommandableRole, QByteArrayLiteral("commandable")},
        {CanQueueRole, QByteArrayLiteral("canQueue")},
        {CanSteerRole, QByteArrayLiteral("canSteer")},
        {CanStopAndSendRole, QByteArrayLiteral("canStopAndSend")},
        {CanRetainPresentationRole, QByteArrayLiteral("canRetainPresentation")},
        {IsStageReadyRole, QByteArrayLiteral("isStageReady")},
        {IsRemoteConnectableRole, QByteArrayLiteral("isRemoteConnectable")},
        {CanSendInputRole, QByteArrayLiteral("canSendInput")},
        {CanRetainFocusRole, QByteArrayLiteral("canRetainFocus")},
        {CanSendFocusRole, QByteArrayLiteral("canSendFocus")},
        {CanResizeRole, QByteArrayLiteral("canResize")},
    };
}

SessionCatalogModel::AuthorityState
SessionCatalogModel::authorityState() const noexcept
{
    return m_authorityState;
}

QString SessionCatalogModel::authorityError() const
{
    return m_authorityError;
}

bool SessionCatalogModel::hasAuthoritativeSnapshot() const noexcept
{
    return m_hasAuthoritativeSnapshot;
}

std::optional<QString> SessionCatalogModel::incarnationForSession(
    const QString& sessionId) const
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end() || found->incarnationId.isEmpty()) {
        return std::nullopt;
    }
    return found->incarnationId;
}

std::optional<SessionCatalogModel::ActionContext>
SessionCatalogModel::actionContext(const QString& sessionId) const
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end() || found->incarnationId.isEmpty()) {
        return std::nullopt;
    }
    return ActionContext {
        .incarnationId = found->incarnationId,
        .kind = found->kind,
        .mode = found->mode,
        .status = found->status,
        .recovery = found->recovery,
        .owner = found->owner,
        .ownerUserId = found->ownerUserId,
        .scope = found->scope,
        .roomId = found->roomId,
        .assignmentSessionId = found->assignmentSessionId,
        .assignmentIncarnationId = found->assignmentIncarnationId,
        .connectionState = found->connectionState,
        .accessState = found->accessState,
        .permissions = found->permissions,
        .commandable = found->commandable,
        .canQueue = found->canQueue,
        .canSteer = found->canSteer,
        .canStopAndSend = found->canStopAndSend,
    };
}

std::optional<SessionCatalogModel::ConversationContext>
SessionCatalogModel::conversationContext(const QString& sessionId) const
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end()) {
        return std::nullopt;
    }
    return ConversationContext {
        .incarnationId = found->incarnationId,
        .kind = found->kind,
        .workingDirectory = found->project,
    };
}

QVector<SessionCatalogModel::PresentationSession>
SessionCatalogModel::presentationSessions() const
{
    QVector<PresentationSession> result;
    result.reserve(m_sessions.size());
    for (const auto& session : m_sessions) {
        result.append(projectPresentation(session));
    }
    return result;
}

std::optional<SessionCatalogModel::PresentationSession>
SessionCatalogModel::presentationSession(const QString& sessionId) const
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end()) {
        return std::nullopt;
    }
    return projectPresentation(*found);
}

bool SessionCatalogModel::containsSession(const QString& sessionId) const
{
    return std::ranges::find(m_sessions, sessionId, &Session::id)
        != m_sessions.end();
}

QVariantMap SessionCatalogModel::presentationForSession(
    const QString& sessionId) const
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end()) {
        return {};
    }
    const auto capabilities = projectPresentation(*found);
    return {
        {QStringLiteral("sessionId"), found->id},
        {QStringLiteral("name"), found->name},
        {QStringLiteral("project"), found->project},
        {QStringLiteral("status"), found->status},
        {QStringLiteral("mode"), found->mode},
        {QStringLiteral("kind"), found->kind},
        {QStringLiteral("canRetainPresentation"),
         capabilities.canRetainPresentation},
        {QStringLiteral("isStageReady"), capabilities.isStageReady},
        {QStringLiteral("isRemoteConnectable"),
         capabilities.isRemoteConnectable},
        {QStringLiteral("canSendInput"), capabilities.canSendInput},
        {QStringLiteral("canRetainFocus"), capabilities.canRetainFocus},
        {QStringLiteral("canSendFocus"), capabilities.canSendFocus},
        {QStringLiteral("canResize"), capabilities.canResize},
    };
}

void SessionCatalogModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Auth event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    const auto type = requiredString(object, QStringLiteral("type"));
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!type || !epoch) {
        emit decodeError(QStringLiteral("Auth event lacks account context."));
        return;
    }
    if (*type == QStringLiteral("auth.ready")) {
        activateAccount(optionalString(object, QStringLiteral("userId")), *epoch);
    } else if (*type == QStringLiteral("auth.required")) {
        activateAccount({}, *epoch);
    }
}

void SessionCatalogModel::ingestSessionEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Session event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Session event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(QStringLiteral("Session event has no exact account epoch."));
        return;
    }
    const auto userId = optionalString(object, QStringLiteral("accountUserId"));
    const auto admission =
        m_accountFence.admit({.userId = userId, .epoch = *epoch}, std::move(json));
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(QStringLiteral("Future session event exceeds the ABI frame limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    applySessionEvent(object);
}

void SessionCatalogModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_refreshTimer.stop();
    m_refreshPending = false;
    m_hasAuthoritativeSnapshot = false;
    m_runtimeSessionIds.clear();
    replaceSnapshot({});
    setAuthorityState(AuthorityState::Loading);
}

void SessionCatalogModel::applySessionEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Session event has no valid type."));
        return;
    }
    if (*type == QStringLiteral("session.list")) {
        const auto value = object.value(QStringLiteral("sessions"));
        if (!value.isArray()) {
            settleRefreshFailure(
                QStringLiteral(
                    "Couldn't load sessions. The runtime reply was invalid."));
            emit decodeError(QStringLiteral("Session snapshot has no session array."));
            return;
        }

        QVector<Session> decoded;
        bool complete = true;
        for (const auto& entry : value.toArray()) {
            if (!entry.isObject()) {
                complete = false;
                continue;
            }
            auto session = decodeSession(entry.toObject());
            if (!session) {
                complete = false;
                continue;
            }
            decoded.push_back(std::move(*session));
        }
        if (complete) {
            m_runtimeSessionIds.clear();
            QVector<QPair<QString, QString>> inactiveLocals;
            QVector<Session> live;
            live.reserve(decoded.size());
            for (auto& session : decoded) {
                m_runtimeSessionIds.insert(session.id);
                if (belongsToLiveCatalog(session)) {
                    live.push_back(std::move(session));
                } else if (session.kind == QStringLiteral("local")) {
                    inactiveLocals.push_back({
                        session.id,
                        session.incarnationId,
                    });
                }
            }
            m_refreshTimer.stop();
            m_refreshPending = false;
            m_hasAuthoritativeSnapshot = true;
            replaceSnapshot(std::move(live));
            setAuthorityState(AuthorityState::Loaded);
            for (const auto& [sessionId, incarnationId] : inactiveLocals) {
                emit inactiveLocalObserved(sessionId, incarnationId);
            }
            emit authoritativeSnapshotApplied();
        } else {
            for (auto& session : decoded) {
                m_runtimeSessionIds.insert(session.id);
                if (belongsToLiveCatalog(session)) {
                    upsert(std::move(session));
                } else {
                    remove(session.id);
                    if (session.kind == QStringLiteral("local")) {
                        emit inactiveLocalObserved(
                            session.id,
                            session.incarnationId);
                    }
                }
            }
            settleRefreshFailure(
                QStringLiteral(
                    "Couldn't load sessions. The runtime reply was invalid."));
            emit decodeError(
                QStringLiteral("Session snapshot was partial; prior rows were retained."));
        }
    } else if (*type == QStringLiteral("session.upsert")) {
        const auto value = object.value(QStringLiteral("session"));
        if (value.isObject()) {
            if (auto session = decodeSession(value.toObject())) {
                m_runtimeSessionIds.insert(session->id);
                if (belongsToLiveCatalog(*session)) {
                    upsert(std::move(*session));
                } else {
                    remove(session->id);
                    if (session->kind == QStringLiteral("local")) {
                        emit inactiveLocalObserved(
                            session->id,
                            session->incarnationId);
                    }
                }
                return;
            }
        }
        emit decodeError(QStringLiteral("Session upsert is invalid."));
    } else if (*type == QStringLiteral("session.removed")) {
        if (const auto sessionId = requiredString(object, QStringLiteral("sessionId"))) {
            m_runtimeSessionIds.remove(*sessionId);
            remove(*sessionId);
        } else {
            emit decodeError(QStringLiteral("Session removal has no valid sessionId."));
        }
    } else if (*type == QStringLiteral("session.error")) {
        const auto operation = object.value(QStringLiteral("operation"));
        const auto message = object.value(QStringLiteral("message"));
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto requestId = object.value(QStringLiteral("requestId"));
        if (operation.toString() == QStringLiteral("session.list")
            && message.isString() && !message.toString().isEmpty()
            && (sessionId.isUndefined() || sessionId.isNull())
            && (requestId.isUndefined() || requestId.isNull())) {
            settleRefreshFailure(
                QStringLiteral("Couldn't load sessions. ")
                + message.toString());
        }
    }
}

void SessionCatalogModel::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = std::move(userId), .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        m_refreshTimer.stop();
        m_refreshPending = false;
        m_hasAuthoritativeSnapshot = false;
        m_runtimeSessionIds.clear();
        replaceSnapshot({});
        setAuthorityState(AuthorityState::Loading);
    }
    for (auto& json : activation.pendingEvents) {
        ingestSessionEvent(std::move(json));
    }
}

bool SessionCatalogModel::beginRefresh()
{
    if (m_refreshPending) {
        return false;
    }
    m_refreshPending = true;
    if (!m_hasAuthoritativeSnapshot) {
        setAuthorityState(AuthorityState::Loading);
    }
    return true;
}

void SessionCatalogModel::refreshDispatched()
{
    if (m_refreshPending) {
        m_refreshTimer.start(static_cast<int>(m_refreshTimeoutMs));
    }
}

void SessionCatalogModel::refreshDispatchFailed()
{
    if (m_refreshPending) {
        settleRefreshFailure(
            QStringLiteral("Couldn't load sessions. Try again."));
    }
}

void SessionCatalogModel::settleRefreshFailure(QString error)
{
    m_refreshTimer.stop();
    m_refreshPending = false;
    setAuthorityState(AuthorityState::Failed, std::move(error));
}

void SessionCatalogModel::setAuthorityState(
    const AuthorityState state,
    QString error)
{
    if (m_authorityState == state && m_authorityError == error) {
        return;
    }
    m_authorityState = state;
    m_authorityError = std::move(error);
    emit authorityStateChanged();
}

std::optional<SessionCatalogModel::Session> SessionCatalogModel::decodeSession(
    const QJsonObject& object)
{
    const auto id = requiredString(object, QStringLiteral("id"));
    const auto kind = requiredString(object, QStringLiteral("kind"));
    const auto name = requiredString(object, QStringLiteral("name"));
    const auto project = requiredString(object, QStringLiteral("project"));
    const auto mode = requiredString(object, QStringLiteral("mode"));
    const auto status = requiredString(object, QStringLiteral("status"));
    const auto scope = requiredString(object, QStringLiteral("scope"));
    const auto access = requiredString(object, QStringLiteral("access"));
    if (!id || !kind || !name || !project || !mode || !status || !scope || !access
        || !known(*kind, {QStringLiteral("local"), QStringLiteral("remote")})) {
        return std::nullopt;
    }

    const auto incarnationId = optionalString(object, QStringLiteral("incarnationId"));
    const auto roomId = optionalString(object, QStringLiteral("roomId"));
    const auto backendSessionId =
        optionalString(object, QStringLiteral("backendSessionId"));
    const auto backendIncarnationId =
        optionalString(object, QStringLiteral("backendIncarnationId"));
    const auto localAssignmentPair =
        !backendSessionId.isEmpty() && !backendIncarnationId.isEmpty();
    const auto assignmentSessionId = *kind == QStringLiteral("remote")
        ? *id
        : localAssignmentPair ? backendSessionId : QString {};
    const auto assignmentIncarnationId = *kind == QStringLiteral("remote")
        ? incarnationId
        : localAssignmentPair ? backendIncarnationId : QString {};
    const auto recovery = optionalString(object, QStringLiteral("recovery"));
    const auto connectionState = optionalString(object, QStringLiteral("connectionState"));
    const auto accessState = optionalString(object, QStringLiteral("accessState"));
    const auto accessIssue = optionalString(object, QStringLiteral("accessIssue"));
    quint32 permissions = 0;
    if (*kind == QStringLiteral("remote")) {
        const auto value = object.value(QStringLiteral("permissions"));
        if (!value.isDouble() || value.toDouble() < 0
            || value.toDouble() > std::numeric_limits<quint32>::max()
            || std::floor(value.toDouble()) != value.toDouble()) {
            return std::nullopt;
        }
        permissions = static_cast<quint32>(value.toDouble());
    } else {
        permissions = 0x1ff;
    }

    const auto knownProjection = known(
        *status,
        {QStringLiteral("active"), QStringLiteral("waiting"), QStringLiteral("blocked"),
         QStringLiteral("reconnecting"), QStringLiteral("stopping"),
         QStringLiteral("stopped")})
        && known(*scope, {QStringLiteral("justMe"), QStringLiteral("myDevices"),
                          QStringLiteral("friends"),
                          QStringLiteral("room")})
        && known(*access, {QStringLiteral("view"), QStringLiteral("suggest"),
                           QStringLiteral("inject"), QStringLiteral("approve")})
        && known(*mode, {QStringLiteral("normal"), QStringLiteral("plan"),
                         QStringLiteral("autopilot")});
    const auto knownRemoteProjection = *kind == QStringLiteral("local")
        || ((connectionState.isEmpty()
                || known(connectionState, {QStringLiteral("connecting"),
                     QStringLiteral("connected"), QStringLiteral("reconnecting"),
                     QStringLiteral("offline")}))
            && (accessState.isEmpty()
                || known(accessState, {QStringLiteral("registeringDevice"),
                     QStringLiteral("awaitingKey"), QStringLiteral("ready"),
                     QStringLiteral("accessDenied"), QStringLiteral("failed")}))
            && (accessIssue.isEmpty()
                || accessIssue == QStringLiteral("peerIdentityChanged")));
    const auto liveRecovery = *kind == QStringLiteral("remote")
        || recovery == QStringLiteral("live");
    bool canQueue = false;
    bool canSteer = false;
    bool canStopAndSend = false;
    const auto semanticActions = object.value(QStringLiteral("semanticActions"));
    if (!semanticActions.isUndefined()) {
        if (!semanticActions.isObject()) {
            return std::nullopt;
        }
        const auto actions = semanticActions.toObject();
        const auto queue = actions.value(QStringLiteral("queue"));
        const auto steer = actions.value(QStringLiteral("steer"));
        const auto stopAndSend = actions.value(QStringLiteral("stopAndSend"));
        if (!queue.isBool() || !steer.isBool() || !stopAndSend.isBool()) {
            return std::nullopt;
        }
        canQueue = queue.toBool();
        canSteer = steer.toBool();
        canStopAndSend = stopAndSend.toBool();
    }
    return Session {
        .id = *id,
        .incarnationId = incarnationId,
        .createRequestId =
            optionalString(object, QStringLiteral("createRequestId")),
        .kind = *kind,
        .name = *name,
        .project = *project,
        .mode = *mode,
        .status = *status,
        .recovery = recovery,
        .scope = *scope,
        .access = *access,
        .roomName = optionalString(object, QStringLiteral("roomName")),
        .roomId = roomId,
        .assignmentSessionId = assignmentSessionId,
        .assignmentIncarnationId = assignmentIncarnationId,
        .owner = optionalString(object, QStringLiteral("owner")),
        .ownerUserId =
            optionalString(object, QStringLiteral("ownerUserId")),
        .connectionState = connectionState,
        .accessState = accessState,
        .accessIssue = accessIssue,
        .permissions = permissions,
        .commandable = knownProjection && knownRemoteProjection && liveRecovery
            && !incarnationId.isEmpty(),
        .canQueue = canQueue,
        .canSteer = canSteer,
        .canStopAndSend = canStopAndSend,
    };
}

SessionCatalogModel::PresentationSession
SessionCatalogModel::projectPresentation(const Session& session)
{
    const auto isLocal = session.kind == QStringLiteral("local");
    const auto needsLiveTerminal =
        session.status == QStringLiteral("active")
        || session.status == QStringLiteral("waiting")
        || session.status == QStringLiteral("blocked")
        || session.status == QStringLiteral("reconnecting");
    const auto accessBlocked =
        session.accessState == QStringLiteral("accessDenied")
        || session.accessState == QStringLiteral("failed")
        || session.accessIssue == QStringLiteral("peerIdentityChanged");
    const auto hasRuntimeIncarnation = !session.incarnationId.isEmpty();
    const auto hasViewPermission =
        isLocal || (session.permissions & viewPermission) != 0;
    const auto canRetainPresentation =
        session.commandable && needsLiveTerminal && hasRuntimeIncarnation
        && !accessBlocked && hasViewPermission;
    const auto isStageReady =
        canRetainPresentation
        && (isLocal
            || (session.connectionState == QStringLiteral("connected")
                && session.accessState == QStringLiteral("ready")));
    const auto canRetainFocus =
        session.commandable && needsLiveTerminal && hasRuntimeIncarnation
        && !accessBlocked
        && (isLocal || (session.permissions & focusBlurPermission) != 0);
    const auto ownerControlled = isLocal || session.owner.isEmpty();

    return {
        .id = session.id,
        .kind = session.kind,
        .canRetainPresentation = canRetainPresentation,
        .isStageReady = isStageReady,
        .isRemoteConnectable = !isLocal && canRetainPresentation,
        .canSendInput =
            session.commandable && needsLiveTerminal
            && hasRuntimeIncarnation && !accessBlocked
            && (isLocal || isStageReady)
            && (isLocal
                || (session.permissions & sendInputPermission) != 0),
        .canRetainFocus = canRetainFocus,
        .canSendFocus =
            canRetainFocus && (isLocal || isStageReady),
        .canResize =
            ownerControlled && session.commandable && needsLiveTerminal
            && hasRuntimeIncarnation && !accessBlocked
            && (isLocal || isStageReady)
            && (isLocal || (session.permissions & resizePermission) != 0),
    };
}

bool SessionCatalogModel::belongsToLiveCatalog(const Session& session)
{
    if (session.status == QStringLiteral("stopped")) {
        return false;
    }
    return session.kind == QStringLiteral("local")
        ? session.recovery == QStringLiteral("live")
        : session.recovery != QStringLiteral("quarantined");
}

bool SessionCatalogModel::containsRuntimeSession(
    const QString& sessionId) const
{
    return m_runtimeSessionIds.contains(sessionId);
}

void SessionCatalogModel::replaceSnapshot(QVector<Session> sessions)
{
    std::ranges::sort(sessions, {}, &Session::name);
    beginResetModel();
    const auto countChangedValue = m_sessions.size() != sessions.size();
    m_sessions = std::move(sessions);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

void SessionCatalogModel::upsert(Session session)
{
    const auto found = std::ranges::find(m_sessions, session.id, &Session::id);
    if (found == m_sessions.end()) {
        const auto row = m_sessions.size();
        beginInsertRows({}, row, row);
        m_sessions.push_back(std::move(session));
        endInsertRows();
        emit countChanged();
        return;
    }
    const auto row = static_cast<int>(std::distance(m_sessions.begin(), found));
    *found = std::move(session);
    emit dataChanged(index(row), index(row));
}

void SessionCatalogModel::remove(const QString& sessionId)
{
    const auto found = std::ranges::find(m_sessions, sessionId, &Session::id);
    if (found == m_sessions.end()) {
        return;
    }
    const auto row = static_cast<int>(std::distance(m_sessions.begin(), found));
    beginRemoveRows({}, row, row);
    m_sessions.erase(found);
    endRemoveRows();
    emit countChanged();
}

} // namespace kodosi
