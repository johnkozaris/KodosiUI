#include "models/SessionAccess.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> requiredString(
    const QJsonObject& object,
    const QString& key)
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

bool absent(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull();
}

} // namespace

SessionAccessGrantsModel::SessionAccessGrantsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int SessionAccessGrantsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SessionAccessGrantsModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const auto& row = m_rows[index.row()];
    switch (role) {
    case HandleRole:
        return row.handle;
    case DisplayNameRole:
        return row.displayName;
    case AccessLevelRole:
        return row.accessLevel;
    case GrantedAtRole:
        return row.grantedAt;
    case ExpiresAtRole:
        return row.expiresAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionAccessGrantsModel::roleNames() const
{
    return {
        {HandleRole, QByteArrayLiteral("handle")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {AccessLevelRole, QByteArrayLiteral("accessLevel")},
        {GrantedAtRole, QByteArrayLiteral("grantedAt")},
        {ExpiresAtRole, QByteArrayLiteral("expiresAt")},
    };
}

void SessionAccessGrantsModel::replace(QVector<Grant> rows)
{
    std::ranges::sort(rows, [](const Grant& left, const Grant& right) {
        const auto leftName =
            left.displayName.isEmpty() ? left.handle : left.displayName;
        const auto rightName =
            right.displayName.isEmpty() ? right.handle : right.displayName;
        return leftName.localeAwareCompare(rightName) < 0;
    });
    const auto countChanged = m_rows.size() != rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanged) {
        emit SessionAccessGrantsModel::countChanged();
    }
}

SessionAccess::SessionAccess(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    PeopleModel& people,
    QObject* parent)
    : SessionAccess(dispatcher, sessions, people, Timing {}, parent)
{
}

SessionAccess::SessionAccess(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    PeopleModel& people,
    Timing timing,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_people(people)
    , m_timing(timing)
    , m_grants(this)
{
    Q_ASSERT(m_timing.resultTimeoutMs > 0);
    Q_ASSERT(m_timing.projectionTimeoutMs > 0);
    Q_ASSERT(m_timing.queryTimeoutMs > 0);
    Q_ASSERT(m_timing.acknowledgmentTimeoutMs > 0);
    Q_ASSERT(m_timing.maximumAttempts > 0);
    m_timer.setSingleShot(true);
    connect(
        &m_timer,
        &QTimer::timeout,
        this,
        &SessionAccess::expireDeadlines);
    const auto revalidate = [this] { reconcileCatalog(false); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, revalidate);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, revalidate);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, revalidate);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, revalidate);
    connect(
        &sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        [this] {
            m_hasAuthoritativeSessionCatalog = true;
            reconcileCatalog(true);
        });
}

SessionAccessGrantsModel* SessionAccess::grants() noexcept
{
    return &m_grants;
}

quint64 SessionAccess::stateRevision() const noexcept
{
    return m_stateRevision;
}

bool SessionAccess::loading() const noexcept
{
    const auto projection = m_projections.constFind(m_selectedSessionId);
    return projection != m_projections.cend() && projection->loading;
}

bool SessionAccess::stale() const noexcept
{
    const auto projection = m_projections.constFind(m_selectedSessionId);
    return projection != m_projections.cend() && projection->stale;
}

QString SessionAccess::error() const
{
    const auto projection = m_projections.constFind(m_selectedSessionId);
    if (projection != m_projections.cend() && !projection->error.isEmpty()) {
        return projection->error;
    }
    return m_error;
}

QString SessionAccess::leaveConfirmationSessionId() const
{
    return m_leaveConfirmationSessionId;
}

QString SessionAccess::revokeConfirmationHandle() const
{
    return m_revokeConfirmationHandle;
}

QString SessionAccess::revokeConfirmationSessionId() const
{
    return m_revokeConfirmationSessionId;
}

QStringList SessionAccess::pendingLeaveSessionIds() const
{
    QSet<QString> ids;
    for (const auto& mutation : m_mutations) {
        if (mutation.kind == Kind::Leave) {
            ids.insert(mutation.sessionId);
        }
    }
    auto result = ids.values();
    std::ranges::sort(result);
    return result;
}

bool SessionAccess::inspect(const QString& sessionId)
{
    if (!selectSession(sessionId)) {
        return false;
    }
    return refresh(sessionId);
}

void SessionAccess::clearInspection()
{
    if (m_selectedSessionId.isEmpty()) {
        return;
    }
    m_selectedSessionId.clear();
    m_selectedIncarnationId.clear();
    m_grants.replace({});
    emit presentationContextChanged();
    bumpState();
}

bool SessionAccess::refresh(const QString& sessionId)
{
    if (!selectSession(sessionId)) {
        return false;
    }
    return dispatchList(sessionId);
}

bool SessionAccess::canManage(const QString& sessionId) const
{
    QString ignored;
    return accessContext(sessionId, ignored).has_value();
}

bool SessionAccess::grant(
    const QString& sessionId,
    const QString& friendHandle,
    const QString& accessLevel)
{
    return beginActorMutation(
        Kind::Grant,
        sessionId,
        friendHandle,
        accessLevel);
}

bool SessionAccess::requestRevokeConfirmation(
    const QString& sessionId,
    const QString& friendHandle)
{
    QString error;
    const auto context = accessContext(sessionId, error);
    QString normalized;
    const auto actor = resolveFriend(friendHandle, normalized, error);
    const auto projection = m_projections.constFind(sessionId);
    const auto hasExactRow =
        projection != m_projections.cend() && projection->loaded
        && std::ranges::any_of(
            projection->rows,
            [&](const SessionAccessGrantsModel::Grant& grant) {
                return normalizeHandle(grant.handle) == normalized
                    && actor && grant.actorUserId == *actor;
            });
    if (!context || !actor || projection == m_projections.cend()
        || !hasExactRow || hasConflict(sessionId, *actor, Kind::Revoke)) {
        m_error = error.isEmpty()
            ? QStringLiteral("That access grant can no longer be revoked.")
            : std::move(error);
        bumpState();
        return false;
    }
    m_revokeConfirmationSessionId = sessionId;
    m_revokeConfirmationIncarnationId = context->incarnationId;
    m_revokeConfirmationHandle = normalized;
    m_revokeConfirmationActorUserId = *actor;
    m_error.clear();
    bumpState();
    return true;
}

bool SessionAccess::confirmRevoke(
    const QString& sessionId,
    const QString& friendHandle)
{
    QString normalized;
    QString error;
    const auto actor = resolveFriend(friendHandle, normalized, error);
    const auto context = accessContext(sessionId, error);
    if (!actor || !context
        || sessionId != m_revokeConfirmationSessionId
        || context->incarnationId != m_revokeConfirmationIncarnationId
        || normalized != m_revokeConfirmationHandle
        || *actor != m_revokeConfirmationActorUserId) {
        cancelRevokeConfirmation();
        m_error = QStringLiteral(
            "The access grant changed before revoke was confirmed.");
        bumpState();
        return false;
    }
    cancelRevokeConfirmation();
    return beginActorMutation(Kind::Revoke, sessionId, normalized, {});
}

void SessionAccess::cancelRevokeConfirmation()
{
    if (m_revokeConfirmationSessionId.isEmpty()
        && m_revokeConfirmationIncarnationId.isEmpty()
        && m_revokeConfirmationHandle.isEmpty()
        && m_revokeConfirmationActorUserId.isEmpty()) {
        return;
    }
    m_revokeConfirmationSessionId.clear();
    m_revokeConfirmationIncarnationId.clear();
    m_revokeConfirmationHandle.clear();
    m_revokeConfirmationActorUserId.clear();
    bumpState();
}

bool SessionAccess::canLeave(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    const auto blocked = context
        && (context->accessState == QStringLiteral("accessDenied")
            || context->accessState == QStringLiteral("failed"));
    return m_authenticated && context
        && context->kind == QStringLiteral("remote")
        && !context->owner.isEmpty() && context->commandable
        && !context->incarnationId.isEmpty() && !blocked
        && !hasPendingLeave(sessionId);
}

bool SessionAccess::leaveNeedsRetry(const QString& sessionId) const
{
    const auto mutation = latestMutation(sessionId);
    return mutation && mutation->kind == Kind::Leave
        && mutation->phase == Phase::Exhausted;
}

bool SessionAccess::requestLeaveConfirmation(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canLeave(sessionId)) {
        m_error = QStringLiteral(
            "That shared session can no longer be left.");
        bumpState();
        return false;
    }
    m_leaveConfirmationSessionId = sessionId;
    m_leaveConfirmationIncarnationId = context->incarnationId;
    m_error.clear();
    bumpState();
    return true;
}

bool SessionAccess::confirmLeave(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || sessionId != m_leaveConfirmationSessionId
        || context->incarnationId != m_leaveConfirmationIncarnationId
        || !canLeave(sessionId)) {
        cancelLeaveConfirmation();
        m_error = QStringLiteral(
            "The shared session changed before leave was confirmed.");
        bumpState();
        return false;
    }
    cancelLeaveConfirmation();
    return beginLeave(sessionId);
}

void SessionAccess::cancelLeaveConfirmation()
{
    if (m_leaveConfirmationSessionId.isEmpty()
        && m_leaveConfirmationIncarnationId.isEmpty()) {
        return;
    }
    m_leaveConfirmationSessionId.clear();
    m_leaveConfirmationIncarnationId.clear();
    bumpState();
}

bool SessionAccess::retryLeave(const QString& sessionId)
{
    auto mutation = mutableMutation(sessionId);
    if (!mutation || mutation->kind != Kind::Leave
        || mutation->phase != Phase::Exhausted) {
        return false;
    }
    mutation->attempts = 0;
    mutation->phase = Phase::Unknown;
    mutation->message.clear();
    m_error.clear();
    const auto accepted = queryMutation(mutation->mutationId);
    bumpState();
    return accepted;
}

bool SessionAccess::retryMutation(
    const QString& sessionId,
    const QString& friendHandle)
{
    auto mutation = mutableMutation(sessionId, friendHandle);
    if (!mutation || mutation->phase != Phase::Exhausted) {
        return false;
    }
    mutation->attempts = 0;
    mutation->phase = Phase::Unknown;
    mutation->message.clear();
    m_error.clear();
    const auto accepted = queryMutation(mutation->mutationId);
    bumpState();
    return accepted;
}

bool SessionAccess::retryCurrentMutation(const QString& sessionId)
{
    auto mutation = mutableMutation(sessionId);
    if (!mutation || mutation->phase != Phase::Exhausted) {
        return false;
    }
    mutation->attempts = 0;
    mutation->phase = Phase::Unknown;
    mutation->message.clear();
    m_error.clear();
    const auto accepted = queryMutation(mutation->mutationId);
    bumpState();
    return accepted;
}

QString SessionAccess::mutationPhase(const QString& sessionId) const
{
    const auto mutation = latestMutation(sessionId);
    return mutation ? phaseName(mutation->phase) : QStringLiteral("idle");
}

QString SessionAccess::mutationMessage(const QString& sessionId) const
{
    const auto mutation = latestMutation(sessionId);
    return mutation ? mutation->message : QString {};
}

QString SessionAccess::actorMutationPhase(
    const QString& sessionId,
    const QString& friendHandle) const
{
    const auto mutation = latestMutation(sessionId, friendHandle);
    return mutation ? phaseName(mutation->phase) : QStringLiteral("idle");
}

QString SessionAccess::actorMutationMessage(
    const QString& sessionId,
    const QString& friendHandle) const
{
    const auto mutation = latestMutation(sessionId, friendHandle);
    return mutation ? mutation->message : QString {};
}

bool SessionAccess::hasPendingLeave(const QString& sessionId) const
{
    return std::ranges::any_of(m_mutations, [&](const Mutation& mutation) {
        return mutation.kind == Kind::Leave
            && mutation.sessionId == sessionId;
    });
}

void SessionAccess::ingestAuthEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = requiredString(object, QStringLiteral("type"));
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!type || !epoch
        || (*type != QStringLiteral("auth.ready")
            && *type != QStringLiteral("auth.required"))) {
        return;
    }
    activateAccount(
        *type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        *epoch);
}

void SessionAccess::ingestSessionEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId =
                optionalString(object, QStringLiteral("accountUserId")),
            .epoch = *epoch,
        },
        json);
    if (admission == AccountEventAdmission::Current) {
        applySessionEvent(object, json);
    }
}

void SessionAccess::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_timer.stop();
    m_projections.clear();
    m_mutations.clear();
    m_selectedSessionId.clear();
    m_selectedIncarnationId.clear();
    m_accountUserId.clear();
    m_error.clear();
    m_leaveConfirmationSessionId.clear();
    m_leaveConfirmationIncarnationId.clear();
    m_revokeConfirmationSessionId.clear();
    m_revokeConfirmationIncarnationId.clear();
    m_revokeConfirmationHandle.clear();
    m_revokeConfirmationActorUserId.clear();
    m_authenticated = false;
    m_hasAuthoritativeSessionCatalog = false;
    m_grants.replace({});
    emit presentationContextChanged();
    bumpState();
}

bool SessionAccess::send(QJsonObject command, QString* error)
{
    const auto payload =
        QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (const auto result =
            m_dispatcher.send(CommandLane::Sessions, payload);
        result) {
        return true;
    } else if (error) {
        *error = result.error().message;
    }
    return false;
}

bool SessionAccess::selectSession(const QString& sessionId)
{
    if (sessionId.isEmpty()) {
        m_error = QStringLiteral("Select a local session first.");
        bumpState();
        return false;
    }
    const auto context = m_sessions.actionContext(sessionId);
    if (!m_authenticated || !context
        || context->kind != QStringLiteral("local")
        || context->incarnationId.isEmpty()) {
        m_error = QStringLiteral(
            "Access grants are unavailable for this session.");
        bumpState();
        return false;
    }
    const auto changed = m_selectedSessionId != sessionId
        || m_selectedIncarnationId != context->incarnationId;
    m_selectedSessionId = sessionId;
    m_selectedIncarnationId = context->incarnationId;
    auto& projection = m_projections[sessionId];
    if (projection.incarnationId != context->incarnationId) {
        projection = Projection {};
        projection.incarnationId = context->incarnationId;
    }
    m_error.clear();
    publishSelectedProjection();
    if (changed) {
        emit presentationContextChanged();
    }
    bumpState();
    return true;
}

bool SessionAccess::dispatchList(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    auto projection = m_projections.find(sessionId);
    if (!m_authenticated || !context
        || context->kind != QStringLiteral("local")
        || context->incarnationId.isEmpty()
        || projection == m_projections.end()
        || projection->incarnationId != context->incarnationId) {
        finishList(
            sessionId,
            false,
            {},
            QStringLiteral(
                "The session changed before access grants could refresh."));
        return false;
    }
    ++projection->requestedGeneration;
    projection->stale = projection->loaded;
    projection->error.clear();
    if (projection->inFlightGeneration != 0) {
        projection->queued = true;
        projection->loading = true;
        publishSelectedProjection();
        bumpState();
        return true;
    }
    projection->inFlightGeneration = projection->requestedGeneration;
    projection->loading = true;
    projection->queued = false;
    QString error;
    if (!send({
            {QStringLiteral("type"), QStringLiteral("session.listAccess")},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("expectedRuntimeIncarnationId"),
             context->incarnationId},
        },
        &error)) {
        projection->inFlightGeneration = 0;
        projection->loading = false;
        projection->error = error.isEmpty()
            ? QStringLiteral("The runtime did not accept the access refresh.")
            : std::move(error);
        projection->stale = projection->loaded;
        publishSelectedProjection();
        bumpState();
        return false;
    }
    publishSelectedProjection();
    bumpState();
    return true;
}

void SessionAccess::finishList(
    const QString& sessionId,
    const bool accepted,
    QVector<SessionAccessGrantsModel::Grant> rows,
    QString error)
{
    auto projection = m_projections.find(sessionId);
    if (projection == m_projections.end()) {
        return;
    }
    const auto completedGeneration = projection->inFlightGeneration;
    projection->inFlightGeneration = 0;
    projection->loading = false;
    if (accepted) {
        projection->rows = std::move(rows);
        projection->loaded = true;
        projection->stale = false;
        projection->error.clear();
    } else {
        projection->stale = projection->loaded;
        projection->error = std::move(error);
    }
    const auto redispatch =
        projection->queued
        || projection->requestedGeneration > completedGeneration;
    projection->queued = false;
    publishSelectedProjection();
    reconcileProjection(sessionId);
    bumpState();
    if (redispatch) {
        (void)dispatchList(sessionId);
    }
}

void SessionAccess::publishSelectedProjection()
{
    const auto projection = m_projections.constFind(m_selectedSessionId);
    m_grants.replace(
        projection == m_projections.cend() || !projection->loaded
            ? QVector<SessionAccessGrantsModel::Grant> {}
            : projection->rows);
}

std::optional<SessionCatalogModel::ActionContext>
SessionAccess::accessContext(
    const QString& sessionId,
    QString& error) const
{
    if (!m_authenticated) {
        error = QStringLiteral("Sign in before changing access.");
        return std::nullopt;
    }
    const auto context = m_sessions.actionContext(sessionId);
    if (!context) {
        error = QStringLiteral("The session is no longer available.");
        return std::nullopt;
    }
    if (context->kind != QStringLiteral("local")) {
        error = QStringLiteral(
            "Access can only be changed on the host session.");
        return std::nullopt;
    }
    if (!context->commandable || context->incarnationId.isEmpty()
        || context->status == QStringLiteral("stopping")
        || context->status == QStringLiteral("stopped")) {
        error = QStringLiteral("This session cannot change access right now.");
        return std::nullopt;
    }
    if (context->scope == QStringLiteral("justMe")) {
        error = QStringLiteral(
            "Share the session before granting explicit access.");
        return std::nullopt;
    }
    return context;
}

std::optional<QString> SessionAccess::resolveFriend(
    const QString& handle,
    QString& normalizedHandle,
    QString& error) const
{
    normalizedHandle = normalizeHandle(handle);
    if (normalizedHandle.isEmpty() || normalizedHandle.toUtf8().size() > 128) {
        error = QStringLiteral("Enter a valid friend handle.");
        return std::nullopt;
    }
    const auto actor = m_people.friendUserIdForHandle(normalizedHandle);
    if (!actor || actor->isEmpty()) {
        error = QStringLiteral("That handle is not a current friend.");
        return std::nullopt;
    }
    if (*actor == m_accountUserId) {
        error = QStringLiteral("You cannot grant access to yourself.");
        return std::nullopt;
    }
    return actor;
}

bool SessionAccess::beginActorMutation(
    const Kind kind,
    const QString& sessionId,
    const QString& friendHandle,
    const QString& accessLevel)
{
    QString error;
    const auto context = accessContext(sessionId, error);
    QString normalized;
    const auto actor = resolveFriend(friendHandle, normalized, error);
    if (!context || !actor
        || (kind == Kind::Grant && !knownAccessLevel(accessLevel))) {
        m_error = error.isEmpty()
            ? QStringLiteral("Choose a supported access level.")
            : std::move(error);
        bumpState();
        return false;
    }
    if (hasConflict(sessionId, *actor, kind)) {
        m_error =
            QStringLiteral("Finish checking this friend's access change first.");
        bumpState();
        return false;
    }
    if (m_mutations.size() >= maximumDurableMutations) {
        m_error = QStringLiteral(
            "Too many access changes are awaiting reconciliation.");
        bumpState();
        return false;
    }
    if (kind == Kind::Revoke) {
        const auto projection = m_projections.constFind(sessionId);
        if (projection == m_projections.cend() || !projection->loaded
            || std::ranges::none_of(
                projection->rows,
                [&](const SessionAccessGrantsModel::Grant& grant) {
                    return grant.actorUserId == *actor
                        && normalizeHandle(grant.handle) == normalized;
                })) {
            m_error =
                QStringLiteral("That access grant is no longer present.");
            bumpState();
            return false;
        }
    }

    Mutation mutation;
    mutation.mutationId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    mutation.sessionId = sessionId;
    mutation.incarnationId = context->incarnationId;
    mutation.kind = kind;
    mutation.actorUserId = *actor;
    mutation.actorHandle = normalized;
    mutation.accessLevel =
        kind == Kind::Grant ? accessLevel : QString {};
    if (kind == Kind::Grant) {
        const auto expiry =
            QDateTime::currentDateTimeUtc().addSecs(24 * 60 * 60);
        mutation.expiresAt =
            expiry.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
        mutation.expiresInstant = expiry;
    }
    m_mutations.insert(mutation.mutationId, mutation);
    auto inserted = m_mutations.find(mutation.mutationId);
    if (!dispatchNewMutation(*inserted)) {
        m_mutations.erase(inserted);
        bumpState();
        return false;
    }
    m_error.clear();
    bumpState();
    return true;
}

bool SessionAccess::beginLeave(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canLeave(sessionId)
        || std::ranges::any_of(
            m_mutations,
            [&](const Mutation& mutation) {
                return mutation.sessionId == sessionId;
            })) {
        m_error =
            QStringLiteral("That shared session can no longer be left.");
        bumpState();
        return false;
    }
    if (m_mutations.size() >= maximumDurableMutations) {
        m_error = QStringLiteral(
            "Too many access changes are awaiting reconciliation.");
        bumpState();
        return false;
    }
    Mutation mutation;
    mutation.mutationId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    mutation.sessionId = sessionId;
    mutation.incarnationId = context->incarnationId;
    mutation.kind = Kind::Leave;
    m_mutations.insert(mutation.mutationId, mutation);
    auto inserted = m_mutations.find(mutation.mutationId);
    if (!dispatchNewMutation(*inserted)) {
        m_mutations.erase(inserted);
        bumpState();
        return false;
    }
    m_error.clear();
    bumpState();
    return true;
}

bool SessionAccess::dispatchNewMutation(Mutation& mutation)
{
    const auto context = m_sessions.actionContext(mutation.sessionId);
    if (!m_authenticated || !context
        || context->incarnationId != mutation.incarnationId) {
        m_error =
            QStringLiteral("The session changed before the access request started.");
        return false;
    }
    if (mutation.kind == Kind::Leave) {
        const auto blocked =
            context->accessState == QStringLiteral("accessDenied")
            || context->accessState == QStringLiteral("failed");
        if (context->kind != QStringLiteral("remote")
            || context->owner.isEmpty() || !context->commandable
            || blocked) {
            m_error = QStringLiteral(
                "That shared session can no longer be left.");
            return false;
        }
    } else {
        QString validationError;
        const auto validated =
            accessContext(mutation.sessionId, validationError);
        QString normalized;
        const auto actor =
            resolveFriend(mutation.actorHandle, normalized, validationError);
        if (!validated || validated->incarnationId != mutation.incarnationId
            || !actor || *actor != mutation.actorUserId
            || normalized != mutation.actorHandle) {
            m_error = validationError.isEmpty()
                ? QStringLiteral(
                      "The access target changed before dispatch.")
                : std::move(validationError);
            return false;
        }
        if (mutation.kind == Kind::Revoke) {
            const auto projection =
                m_projections.constFind(mutation.sessionId);
            if (projection == m_projections.cend()
                || !projection->loaded
                || std::ranges::none_of(
                    projection->rows,
                    [&](const SessionAccessGrantsModel::Grant& grant) {
                        return grant.actorUserId == mutation.actorUserId
                            && normalizeHandle(grant.handle)
                                == mutation.actorHandle;
                    })) {
                m_error =
                    QStringLiteral("That access grant is no longer present.");
                return false;
            }
        }
    }
    QJsonObject command {
        {QStringLiteral("mutationId"), mutation.mutationId},
        {QStringLiteral("sessionId"), mutation.sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"),
         mutation.incarnationId},
    };
    switch (mutation.kind) {
    case Kind::Grant:
        command.insert(
            QStringLiteral("type"),
            QStringLiteral("session.grantAccess"));
        command.insert(QStringLiteral("actorUserId"), mutation.actorUserId);
        command.insert(QStringLiteral("accessLevel"), mutation.accessLevel);
        command.insert(QStringLiteral("expiresAt"), mutation.expiresAt);
        break;
    case Kind::Revoke:
        command.insert(
            QStringLiteral("type"),
            QStringLiteral("session.revokeAccess"));
        command.insert(QStringLiteral("actorUserId"), mutation.actorUserId);
        break;
    case Kind::Leave:
        command.insert(
            QStringLiteral("type"),
            QStringLiteral("session.leave"));
        break;
    }
    QString error;
    if (!send(std::move(command), &error)) {
        m_error = error.isEmpty()
            ? QStringLiteral("The runtime did not accept the access request.")
            : std::move(error);
        return false;
    }
    mutation.phase = Phase::Pending;
    mutation.deadline =
        QDateTime::currentDateTimeUtc().addMSecs(m_timing.resultTimeoutMs);
    scheduleTimer();
    return true;
}

bool SessionAccess::hasConflict(
    const QString& sessionId,
    const QString& actorUserId,
    const Kind kind) const
{
    return std::ranges::any_of(m_mutations, [&](const Mutation& mutation) {
        if (mutation.sessionId != sessionId) {
            return false;
        }
        if (kind == Kind::Leave || mutation.kind == Kind::Leave) {
            return true;
        }
        return mutation.actorUserId == actorUserId;
    });
}

void SessionAccess::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        m_timer.stop();
        m_projections.clear();
        m_mutations.clear();
        m_selectedSessionId.clear();
        m_selectedIncarnationId.clear();
        m_accountUserId = std::move(userId);
        m_authenticated = !m_accountUserId.isEmpty();
        m_hasAuthoritativeSessionCatalog = false;
        m_error.clear();
        m_leaveConfirmationSessionId.clear();
        m_leaveConfirmationIncarnationId.clear();
        m_revokeConfirmationSessionId.clear();
        m_revokeConfirmationIncarnationId.clear();
        m_revokeConfirmationHandle.clear();
        m_revokeConfirmationActorUserId.clear();
        m_grants.replace({});
        emit presentationContextChanged();
        bumpState();
    }
    for (auto& pending : activation.pendingEvents) {
        ingestSessionEvent(std::move(pending));
    }
    if (activation.changed && m_authenticated) {
        QString ignored;
        if (!send({
                {QStringLiteral("type"),
                 QStringLiteral("session.accessMutationsRecover")},
            },
            &ignored)) {
            m_error =
                QStringLiteral("Could not recover pending access changes.");
            bumpState();
        }
    }
}

void SessionAccess::applySessionEvent(
    const QJsonObject& object,
    const QByteArrayView json)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        return;
    }
    if (*type == QStringLiteral("session.accessGrants")) {
        applyAccessGrants(object);
    } else if (*type == QStringLiteral("session.accessMutationAccepted")
        || *type == QStringLiteral("session.accessMutationResult")
        || *type == QStringLiteral("session.accessMutationRecovered")
        || *type == QStringLiteral("session.accessMutationReconciled")
        || *type == QStringLiteral("session.accessMutationAcknowledged")) {
        applyMutationEvent(object, json);
    } else if (*type == QStringLiteral("session.error")) {
        applySessionError(object);
    } else if (*type == QStringLiteral("session.removed")
        || *type == QStringLiteral("session.upsert")
        || *type == QStringLiteral("session.list")) {
        reconcileCatalog(false);
    }
}

void SessionAccess::applyAccessGrants(const QJsonObject& object)
{
    const auto sessionId =
        requiredString(object, QStringLiteral("sessionId"));
    const auto incarnation =
        requiredString(object, QStringLiteral("runtimeIncarnationId"));
    const auto accountUserId =
        requiredString(object, QStringLiteral("accountUserId"));
    if (!sessionId || !incarnation || !accountUserId
        || *accountUserId != m_accountUserId) {
        return;
    }
    auto projection = m_projections.find(*sessionId);
    const auto context = m_sessions.actionContext(*sessionId);
    if (projection == m_projections.end() || !context
        || context->kind != QStringLiteral("local")
        || projection->inFlightGeneration == 0
        || projection->incarnationId != *incarnation
        || context->incarnationId != *incarnation) {
        return;
    }
    const auto grants = object.value(QStringLiteral("grants"));
    if (!grants.isArray() || grants.toArray().size() > maximumGrantRows) {
        finishList(
            *sessionId,
            false,
            {},
            QStringLiteral("The access grant inventory is invalid."));
        return;
    }

    QVector<SessionAccessGrantsModel::Grant> rows;
    rows.reserve(grants.toArray().size());
    QSet<QString> actors;
    for (const auto& value : grants.toArray()) {
        if (!value.isObject()) {
            finishList(
                *sessionId,
                false,
                {},
                QStringLiteral("The access grant inventory is invalid."));
            return;
        }
        const auto row = value.toObject();
        const auto actor =
            requiredString(row, QStringLiteral("actorUserId"));
        const auto handle = requiredString(row, QStringLiteral("handle"));
        const auto displayNameValue =
            row.value(QStringLiteral("displayName"));
        const auto level =
            requiredString(row, QStringLiteral("accessLevel"));
        const auto grantedAt =
            requiredString(row, QStringLiteral("grantedAt"));
        const auto expiresValue = row.value(QStringLiteral("expiresAt"));
        const auto grantedInstant =
            grantedAt ? parseRfc3339(*grantedAt) : std::nullopt;
        const auto expiresAt = expiresValue.isString()
            ? expiresValue.toString()
            : QString {};
        const auto expiresInstant = expiresAt.isEmpty()
            ? std::optional<QDateTime> {}
            : parseRfc3339(expiresAt);
        const auto normalizedHandle =
            handle ? normalizeHandle(*handle) : QString {};
        if (!actor || !handle || !displayNameValue.isString()
            || !level || !grantedAt
            || actors.contains(*actor)
            || normalizedHandle.isEmpty()
            || actor->toUtf8().size() > 256
            || normalizedHandle.toUtf8().size() > 128
            || displayNameValue.toString().toUtf8().size() > 256
            || grantedAt->size() > 64 || expiresAt.size() > 64
            || !knownAccessLevel(*level) || !grantedInstant
            || (!absent(expiresValue) && !expiresValue.isString())
            || (!expiresAt.isEmpty() && !expiresInstant)) {
            finishList(
                *sessionId,
                false,
                {},
                QStringLiteral("The access grant inventory is invalid."));
            return;
        }
        actors.insert(*actor);
        rows.push_back({
            .actorUserId = *actor,
            .handle = normalizedHandle,
            .displayName = displayNameValue.toString(),
            .accessLevel = *level,
            .grantedAt = *grantedAt,
            .expiresAt = expiresAt,
            .expiresInstant = expiresInstant,
        });
    }
    finishList(*sessionId, true, std::move(rows), {});
}

void SessionAccess::applyMutationEvent(
    const QJsonObject& object,
    const QByteArrayView json)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    const auto mutationId =
        requiredString(object, QStringLiteral("mutationId"));
    if (!type || !mutationId || !validMutationId(*mutationId)) {
        return;
    }
    if (*type == QStringLiteral("session.accessMutationReconciled")) {
        const auto present = object.value(QStringLiteral("present"));
        auto mutation = m_mutations.find(*mutationId);
        if (mutation == m_mutations.end() || !present.isBool()) {
            return;
        }
        mutation->queryOutstanding = false;
        if (present.toBool()) {
            mutation->recoveryResponseExpected = true;
            mutation->phase = Phase::Unknown;
            mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
                m_timing.queryTimeoutMs);
        } else {
            mutation->recoveryResponseExpected = false;
            if (mutation->acknowledgmentVerificationIntent) {
                const auto rejected =
                    mutation->outcome == Outcome::Rejected;
                const auto message = mutation->message;
                settleMutation(
                    *mutationId,
                    !rejected,
                    rejected ? message : QString {});
                return;
            }
            if (mutation->outcome == Outcome::Applied
                && mutation->projectionSatisfied) {
                settleMutation(*mutationId, true, {});
                return;
            }
            const auto message = !mutation->message.isEmpty()
                ? mutation->message
                : QStringLiteral("The access change did not start.");
            settleMutation(*mutationId, false, message);
            return;
        }
        scheduleTimer();
        bumpState();
        return;
    }
    if (*type == QStringLiteral("session.accessMutationAcknowledged")) {
        const auto fingerprint =
            requiredString(object, QStringLiteral("fingerprint"));
        auto mutation = m_mutations.find(*mutationId);
        if (!fingerprint || mutation == m_mutations.end()
            || !mutation->acknowledgmentVerificationIntent
            || mutation->fingerprint != *fingerprint
            || !validFingerprint(*fingerprint)) {
            return;
        }
        const auto rejected =
            mutation->outcome == Outcome::Rejected;
        const auto message = mutation->message;
        settleMutation(
            *mutationId,
            !rejected,
            rejected ? message : QString {});
        return;
    }

    const auto sessionId =
        requiredString(object, QStringLiteral("sessionId"));
    const auto incarnation = requiredString(
        object,
        QStringLiteral("expectedRuntimeIncarnationId"));
    const auto kindValue = requiredString(object, QStringLiteral("kind"));
    if (!sessionId || !incarnation || !kindValue) {
        return;
    }
    Kind kind;
    if (*kindValue == QStringLiteral("grant")) {
        kind = Kind::Grant;
    } else if (*kindValue == QStringLiteral("revoke")) {
        kind = Kind::Revoke;
    } else if (*kindValue == QStringLiteral("leave")) {
        kind = Kind::Leave;
    } else {
        return;
    }
    const auto actor = optionalString(object, QStringLiteral("actorUserId"));
    const auto level = optionalString(object, QStringLiteral("accessLevel"));
    const auto expires = optionalString(object, QStringLiteral("expiresAt"));
    const auto expiresInstant = kind == Kind::Grant
        ? parseRfc3339(expires)
        : std::optional<QDateTime> {};
    const auto exactTarget =
        (kind == Kind::Grant && !actor.isEmpty()
            && knownAccessLevel(level) && expiresInstant)
        || (kind == Kind::Revoke && !actor.isEmpty()
            && absent(object.value(QStringLiteral("accessLevel")))
            && absent(object.value(QStringLiteral("expiresAt"))))
        || (kind == Kind::Leave
            && absent(object.value(QStringLiteral("actorUserId")))
            && absent(object.value(QStringLiteral("accessLevel")))
            && absent(object.value(QStringLiteral("expiresAt"))));
    if (!exactTarget) {
        return;
    }

    auto mutation = m_mutations.find(*mutationId);
    const auto recovered =
        *type == QStringLiteral("session.accessMutationRecovered");
    QString fingerprint;
    if (recovered) {
        const auto originatingEpoch = exactUnsignedJsonField(
            json,
            QByteArrayLiteral("originatingAccountEpoch"));
        fingerprint =
            optionalString(object, QStringLiteral("fingerprint"));
        if (!originatingEpoch || !validFingerprint(fingerprint)) {
            return;
        }
        if (mutation == m_mutations.end()) {
            if (m_mutations.size() >= maximumDurableMutations) {
                m_error = QStringLiteral(
                    "Too many recovered access changes await reconciliation.");
                bumpState();
                return;
            }
            Mutation recoveredMutation;
            recoveredMutation.mutationId = *mutationId;
            recoveredMutation.sessionId = *sessionId;
            recoveredMutation.incarnationId = *incarnation;
            recoveredMutation.kind = kind;
            recoveredMutation.actorUserId = actor;
            recoveredMutation.accessLevel = level;
            recoveredMutation.expiresAt = expires;
            recoveredMutation.expiresInstant =
                kind == Kind::Grant
                ? parseRfc3339(expires)
                : std::optional<QDateTime> {};
            recoveredMutation.fingerprint = fingerprint;
            recoveredMutation.phase = Phase::Unknown;
            recoveredMutation.deadline =
                QDateTime::currentDateTimeUtc().addMSecs(
                    m_timing.queryTimeoutMs);
            m_mutations.insert(*mutationId, recoveredMutation);
            mutation = m_mutations.find(*mutationId);
        }
    }
    if (mutation == m_mutations.end()) {
        return;
    }
    const auto mismatch =
        mutation->sessionId != *sessionId
        || mutation->incarnationId != *incarnation
        || mutation->kind != kind
        || mutation->actorUserId != actor
        || mutation->accessLevel != level
        || (kind == Kind::Grant
            && (!mutation->expiresInstant || !expiresInstant
                || mutation->expiresInstant->toMSecsSinceEpoch()
                    != expiresInstant->toMSecsSinceEpoch()))
        || (recovered && !mutation->fingerprint.isEmpty()
            && mutation->fingerprint != fingerprint);
    if (mismatch) {
        markExhausted(
            *mutation,
            QStringLiteral(
                "The recovered access result did not match its original target."));
        return;
    }
    if (recovered) {
        mutation->fingerprint = fingerprint;
    }
    const auto wasExpectedRecovery =
        recovered && mutation->recoveryResponseExpected;
    if (recovered) {
        mutation->recoveryResponseExpected = false;
    }
    mutation->queryOutstanding = false;

    if (*type == QStringLiteral("session.accessMutationAccepted")) {
        const auto currentContext =
            m_sessions.actionContext(mutation->sessionId);
        if (mutation->kind != Kind::Leave
            && (!currentContext
                || currentContext->incarnationId
                    != mutation->incarnationId)) {
            markExhausted(
                *mutation,
                QStringLiteral(
                    "The session restarted before the access change finished."));
            return;
        }
        mutation->attempts = 0;
        mutation->phase = Phase::Accepted;
        mutation->message.clear();
        mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
            m_timing.resultTimeoutMs);
        scheduleTimer();
        bumpState();
        return;
    }

    const auto outcomeValue = object.value(QStringLiteral("outcome"));
    QString outcome;
    if (outcomeValue.isString()) {
        outcome = outcomeValue.toString();
    } else if (!absent(outcomeValue)) {
        return;
    }
    if (!outcome.isEmpty() && outcome != QStringLiteral("applied")
        && outcome != QStringLiteral("rejected")
        && outcome != QStringLiteral("unknown")) {
        return;
    }
    const auto currentContext =
        m_sessions.actionContext(mutation->sessionId);
    if (mutation->kind != Kind::Leave
        && outcome != QStringLiteral("applied")
        && outcome != QStringLiteral("rejected")
        && (!currentContext
            || currentContext->incarnationId != mutation->incarnationId)) {
        markExhausted(
            *mutation,
            QStringLiteral(
                "The session restarted before the access change finished."));
        return;
    }
    mutation->message = optionalString(object, QStringLiteral("message"));
    if (outcome == QStringLiteral("applied")) {
        mutation->attempts = 0;
        mutation->outcome = Outcome::Applied;
        mutation->phase = Phase::AwaitingProjection;
        mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
            m_timing.projectionTimeoutMs);
        refreshMutationAuthority(*mutation);
        settleIfAuthoritative(*mutationId);
    } else if (outcome == QStringLiteral("rejected")) {
        mutation->attempts = 0;
        mutation->outcome = Outcome::Rejected;
        mutation->phase = Phase::Failed;
        mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
            m_timing.queryTimeoutMs);
        if (!mutation->fingerprint.isEmpty()) {
            (void)acknowledgeMutation(*mutationId);
        } else {
            (void)queryMutation(*mutationId);
        }
    } else {
        mutation->outcome = outcome == QStringLiteral("unknown")
            ? Outcome::Unknown
            : mutation->outcome;
        mutation->phase = Phase::Unknown;
        mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
            m_timing.queryTimeoutMs);
        if (!wasExpectedRecovery) {
            (void)queryMutation(*mutationId);
        }
    }
    scheduleTimer();
    bumpState();
}

void SessionAccess::applySessionError(const QJsonObject& object)
{
    const auto operation =
        requiredString(object, QStringLiteral("operation"));
    const auto message =
        requiredString(object, QStringLiteral("message"));
    if (!operation || !message) {
        return;
    }
    if (*operation == QStringLiteral("session.listAccess")) {
        const auto sessionId =
            requiredString(object, QStringLiteral("sessionId"));
        if (!sessionId) {
            return;
        }
        const auto projection = m_projections.constFind(*sessionId);
        if (projection == m_projections.cend()
            || projection->inFlightGeneration == 0) {
            return;
        }
        finishList(*sessionId, false, {}, *message);
        return;
    }
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"));
    if (!requestId) {
        return;
    }
    auto mutation = m_mutations.find(*requestId);
    if (mutation == m_mutations.end()) {
        return;
    }
    if (*operation == QStringLiteral("session.accessMutationAck")
        && mutation->acknowledgmentOutstanding) {
        mutation->acknowledgmentOutstanding = false;
        mutation->acknowledgmentVerificationIntent = true;
        mutation->phase = Phase::Unknown;
        mutation->message = *message;
        m_error = *message;
        (void)queryMutation(*requestId);
        bumpState();
        return;
    }
    if (*operation == QStringLiteral("session.accessMutationReconcile")
        && mutation->queryOutstanding) {
        mutation->queryOutstanding = false;
        mutation->recoveryResponseExpected = false;
        markExhausted(*mutation, *message);
        return;
    }
    const auto expectedOperation = mutation->kind == Kind::Grant
        ? QStringLiteral("session.grantAccess")
        : mutation->kind == Kind::Revoke
        ? QStringLiteral("session.revokeAccess")
        : QStringLiteral("session.leave");
    if (*operation == expectedOperation) {
        mutation->outcome = Outcome::Rejected;
        mutation->phase = Phase::Failed;
        mutation->message = *message;
        m_error = *message;
        (void)queryMutation(*requestId);
        bumpState();
    }
}

void SessionAccess::reconcileCatalog(const bool authoritativeSnapshot)
{
    if (authoritativeSnapshot) {
        m_hasAuthoritativeSessionCatalog = true;
    }
    const auto projectionIds = m_projections.keys();
    for (const auto& sessionId : projectionIds) {
        auto projection = m_projections.find(sessionId);
        const auto context = m_sessions.actionContext(sessionId);
        if (!context || context->kind != QStringLiteral("local")
            || context->incarnationId != projection->incarnationId) {
            projection->inFlightGeneration = 0;
            projection->loading = false;
            projection->loaded = false;
            projection->stale = false;
            projection->rows.clear();
            projection->error = QStringLiteral(
                "The session changed; refresh access for the current session.");
            if (sessionId == m_selectedSessionId) {
                const auto currentIncarnation =
                    context && context->kind == QStringLiteral("local")
                    ? context->incarnationId
                    : QString {};
                if (m_selectedIncarnationId != currentIncarnation) {
                    m_selectedIncarnationId = currentIncarnation;
                    emit presentationContextChanged();
                }
            }
        }
    }
    if (!m_leaveConfirmationSessionId.isEmpty()) {
        const auto context =
            m_sessions.actionContext(m_leaveConfirmationSessionId);
        if (!context
            || context->incarnationId != m_leaveConfirmationIncarnationId
            || !canLeave(m_leaveConfirmationSessionId)) {
            m_leaveConfirmationSessionId.clear();
            m_leaveConfirmationIncarnationId.clear();
        }
    }
    if (!m_revokeConfirmationSessionId.isEmpty()) {
        const auto context =
            m_sessions.actionContext(m_revokeConfirmationSessionId);
        if (!context
            || context->incarnationId != m_revokeConfirmationIncarnationId) {
            m_revokeConfirmationSessionId.clear();
            m_revokeConfirmationIncarnationId.clear();
            m_revokeConfirmationHandle.clear();
            m_revokeConfirmationActorUserId.clear();
        }
    }

    const auto mutationIds = m_mutations.keys();
    for (const auto& mutationId : mutationIds) {
        auto mutation = m_mutations.find(mutationId);
        if (mutation == m_mutations.end()) {
            continue;
        }
        const auto context = m_sessions.actionContext(mutation->sessionId);
        if (mutation->kind == Kind::Leave) {
            mutation->projectionSatisfied =
                (context
                    && context->incarnationId != mutation->incarnationId)
                || (!context && m_hasAuthoritativeSessionCatalog);
        } else if (context
            && context->incarnationId != mutation->incarnationId) {
            if (mutation->outcome == Outcome::Applied) {
                mutation->projectionSatisfied = true;
                settleIfAuthoritative(mutationId);
                continue;
            }
            if (mutation->outcome == Outcome::Rejected) {
                if (mutation->fingerprint.isEmpty()) {
                    (void)queryMutation(mutationId);
                } else {
                    (void)acknowledgeMutation(mutationId);
                }
                continue;
            }
            markExhausted(
                *mutation,
                QStringLiteral(
                    "The session restarted before the access change finished."));
            continue;
        }
        settleIfAuthoritative(mutationId);
    }
    publishSelectedProjection();
    scheduleTimer();
    bumpState();
}

void SessionAccess::reconcileProjection(const QString& sessionId)
{
    const auto projection = m_projections.constFind(sessionId);
    if (projection == m_projections.cend() || !projection->loaded) {
        return;
    }
    const auto mutationIds = m_mutations.keys();
    for (const auto& mutationId : mutationIds) {
        auto mutation = m_mutations.find(mutationId);
        if (mutation == m_mutations.end()
            || mutation->sessionId != sessionId
            || mutation->kind == Kind::Leave) {
            continue;
        }
        if (mutation->kind == Kind::Grant) {
            mutation->projectionSatisfied =
                std::ranges::any_of(
                    projection->rows,
                    [&](const SessionAccessGrantsModel::Grant& grant) {
                        return grant.actorUserId == mutation->actorUserId
                            && grant.accessLevel == mutation->accessLevel
                            && grant.expiresInstant
                            && mutation->expiresInstant
                            && grant.expiresInstant->toMSecsSinceEpoch()
                                == mutation->expiresInstant->toMSecsSinceEpoch();
                    });
        } else {
            mutation->projectionSatisfied =
                std::ranges::none_of(
                    projection->rows,
                    [&](const SessionAccessGrantsModel::Grant& grant) {
                        return grant.actorUserId == mutation->actorUserId;
                    });
        }
        settleIfAuthoritative(mutationId);
    }
}

void SessionAccess::refreshMutationAuthority(Mutation& mutation)
{
    if (mutation.kind == Kind::Leave) {
        const auto context = m_sessions.actionContext(mutation.sessionId);
        mutation.projectionSatisfied =
            (context && context->incarnationId != mutation.incarnationId)
            || (!context && m_hasAuthoritativeSessionCatalog);
        if (!mutation.projectionSatisfied) {
            QString ignored;
            (void)send({
                {QStringLiteral("type"), QStringLiteral("session.list")},
            },
            &ignored);
        }
        return;
    }
    const auto context = m_sessions.actionContext(mutation.sessionId);
    if ((context
            && context->incarnationId != mutation.incarnationId)
        || (!context && m_hasAuthoritativeSessionCatalog)) {
        mutation.projectionSatisfied = true;
        return;
    }
    auto& projection = m_projections[mutation.sessionId];
    if (projection.incarnationId != mutation.incarnationId) {
        projection = Projection {};
        projection.incarnationId = mutation.incarnationId;
    }
    (void)dispatchList(mutation.sessionId);
}

void SessionAccess::settleIfAuthoritative(const QString& mutationId)
{
    auto mutation = m_mutations.find(mutationId);
    if (mutation == m_mutations.end()
        || mutation->outcome != Outcome::Applied
        || !mutation->projectionSatisfied) {
        return;
    }
    if (mutation->fingerprint.isEmpty()) {
        if (!mutation->queryOutstanding
            && !mutation->recoveryResponseExpected) {
            (void)queryMutation(mutationId);
        }
        return;
    }
    (void)acknowledgeMutation(mutationId);
}

bool SessionAccess::queryMutation(const QString& mutationId)
{
    auto mutation = m_mutations.find(mutationId);
    if (mutation == m_mutations.end() || mutation->queryOutstanding) {
        return false;
    }
    QString error;
    const auto accepted = send({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconcile")},
        {QStringLiteral("mutationId"), mutationId},
    },
        &error);
    mutation = m_mutations.find(mutationId);
    if (mutation == m_mutations.end()) {
        return false;
    }
    mutation->queryOutstanding = accepted;
    mutation->recoveryResponseExpected = false;
    mutation->phase = Phase::Unknown;
    mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
        m_timing.queryTimeoutMs);
    if (!accepted) {
        mutation->message = error.isEmpty()
            ? QStringLiteral("Could not check the access change.")
            : std::move(error);
    }
    scheduleTimer();
    return accepted;
}

bool SessionAccess::acknowledgeMutation(const QString& mutationId)
{
    auto mutation = m_mutations.find(mutationId);
    if (mutation == m_mutations.end()
        || mutation->fingerprint.isEmpty()
        || mutation->acknowledgmentOutstanding) {
        return false;
    }
    mutation->acknowledgmentVerificationIntent = true;
    QString error;
    const auto accepted = send({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationAck")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("fingerprint"), mutation->fingerprint},
    },
        &error);
    mutation = m_mutations.find(mutationId);
    if (mutation == m_mutations.end()) {
        return false;
    }
    mutation->acknowledgmentOutstanding = accepted;
    mutation->phase = accepted ? Phase::Acknowledging : Phase::Unknown;
    mutation->deadline = QDateTime::currentDateTimeUtc().addMSecs(
        accepted ? m_timing.acknowledgmentTimeoutMs : m_timing.queryTimeoutMs);
    if (!accepted) {
        mutation->message = error.isEmpty()
            ? QStringLiteral("Could not acknowledge the access change.")
            : std::move(error);
        (void)queryMutation(mutationId);
    }
    scheduleTimer();
    return accepted;
}

void SessionAccess::settleMutation(
    const QString& mutationId,
    const bool success,
    QString message)
{
    const auto found = m_mutations.constFind(mutationId);
    if (found == m_mutations.cend()) {
        return;
    }
    const auto sessionId = found->sessionId;
    m_mutations.remove(mutationId);
    m_error = success
        ? QString {}
        : message.isEmpty()
        ? QStringLiteral("The access change failed.")
        : std::move(message);
    scheduleTimer();
    bumpState();
    if (success && sessionId == m_selectedSessionId) {
        publishSelectedProjection();
    }
}

void SessionAccess::markExhausted(Mutation& mutation, QString message)
{
    mutation.phase = Phase::Exhausted;
    mutation.message = std::move(message);
    mutation.deadline = {};
    mutation.queryOutstanding = false;
    mutation.recoveryResponseExpected = false;
    mutation.acknowledgmentOutstanding = false;
    m_error = mutation.message;
    scheduleTimer();
    bumpState();
}

void SessionAccess::scheduleTimer()
{
    std::optional<QDateTime> nearest;
    for (const auto& mutation : m_mutations) {
        if (!mutation.deadline.isValid()) {
            continue;
        }
        nearest = !nearest || mutation.deadline < *nearest
            ? std::optional<QDateTime> {mutation.deadline}
            : nearest;
    }
    if (!nearest) {
        m_timer.stop();
        return;
    }
    const auto delay = std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().msecsTo(*nearest),
        0,
        std::numeric_limits<int>::max());
    m_timer.start(static_cast<int>(delay));
}

void SessionAccess::expireDeadlines()
{
    const auto now = QDateTime::currentDateTimeUtc();
    const auto mutationIds = m_mutations.keys();
    for (const auto& mutationId : mutationIds) {
        auto mutation = m_mutations.find(mutationId);
        if (mutation == m_mutations.end()
            || !mutation->deadline.isValid()
            || mutation->deadline > now) {
            continue;
        }
        if (mutation->attempts >= m_timing.maximumAttempts) {
            markExhausted(
                *mutation,
                QStringLiteral(
                    "The access change remains unconfirmed. Check its outcome before trying another change."));
            continue;
        }
        ++mutation->attempts;
        mutation->acknowledgmentOutstanding = false;
        mutation->queryOutstanding = false;
        mutation->recoveryResponseExpected = false;
        if (mutation->outcome == Outcome::Applied
            && !mutation->projectionSatisfied) {
            refreshMutationAuthority(*mutation);
        }
        (void)queryMutation(mutationId);
    }
    scheduleTimer();
    bumpState();
}

void SessionAccess::bumpState()
{
    ++m_stateRevision;
    emit stateChanged();
}

const SessionAccess::Mutation* SessionAccess::latestMutation(
    const QString& sessionId,
    const QString& actorHandle) const
{
    const auto normalized = normalizeHandle(actorHandle);
    const Mutation* result = nullptr;
    for (const auto& mutation : m_mutations) {
        if (mutation.sessionId != sessionId
            || (!normalized.isEmpty()
                && mutation.actorHandle != normalized)) {
            continue;
        }
        if (!result || mutation.mutationId > result->mutationId) {
            result = &mutation;
        }
    }
    return result;
}

SessionAccess::Mutation* SessionAccess::mutableMutation(
    const QString& sessionId,
    const QString& actorHandle)
{
    const auto selected = latestMutation(sessionId, actorHandle);
    return selected
        ? &m_mutations[selected->mutationId]
        : nullptr;
}

std::optional<QDateTime> SessionAccess::parseRfc3339(const QString& value)
{
    static const QRegularExpression completeRfc3339(
        QStringLiteral(
            R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+-]\d{2}:\d{2})$)"));
    if (!completeRfc3339.match(value).hasMatch()) {
        return std::nullopt;
    }
    auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(value, Qt::ISODate);
    }
    return date.isValid()
        ? std::optional<QDateTime> {date.toUTC()}
        : std::nullopt;
}

bool SessionAccess::knownAccessLevel(const QString& value)
{
    return value == QStringLiteral("view")
        || value == QStringLiteral("suggest")
        || value == QStringLiteral("inject")
        || value == QStringLiteral("approve");
}

bool SessionAccess::validFingerprint(const QString& value)
{
    return value.size() == 64
        && std::ranges::all_of(value, [](const QChar character) {
               return (character >= u'0' && character <= u'9')
                   || (character >= u'a' && character <= u'f')
                   || (character >= u'A' && character <= u'F');
           });
}

bool SessionAccess::validMutationId(const QString& value)
{
    const QUuid parsed(value);
    return !parsed.isNull()
        && parsed.version() == QUuid::UnixEpoch
        && parsed.toString(QUuid::WithoutBraces) == value;
}

QString SessionAccess::normalizeHandle(const QString& value)
{
    auto normalized = value.trimmed();
    if (normalized.startsWith(u'@')) {
        normalized.removeFirst();
    }
    return normalized.trimmed().toLower();
}

QString SessionAccess::phaseName(const Phase phase)
{
    switch (phase) {
    case Phase::Pending:
        return QStringLiteral("pending");
    case Phase::Accepted:
        return QStringLiteral("accepted");
    case Phase::AwaitingProjection:
        return QStringLiteral("reconciling");
    case Phase::Unknown:
        return QStringLiteral("unknown");
    case Phase::Failed:
        return QStringLiteral("error");
    case Phase::Acknowledging:
        return QStringLiteral("acknowledging");
    case Phase::Exhausted:
        return QStringLiteral("exhausted");
    }
    return QStringLiteral("unknown");
}

} // namespace kodosi
