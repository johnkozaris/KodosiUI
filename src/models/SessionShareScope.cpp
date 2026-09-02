#include "models/SessionShareScope.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <limits>
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

SessionShareScope::SessionShareScope(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    MissionDirectoryModel& missions,
    QObject* parent)
    : SessionShareScope(
          dispatcher,
          sessions,
          missions,
          Timing {},
          parent)
{
}

SessionShareScope::SessionShareScope(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    MissionDirectoryModel& missions,
    Timing timing,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_missions(missions)
    , m_timing(timing)
{
    Q_ASSERT(m_timing.receiptTimeoutMs > 0);
    Q_ASSERT(m_timing.maximumAcceptedBudgetMs > 0);
    m_clock.start();
    m_timer.setSingleShot(true);
    connect(
        &m_timer,
        &QTimer::timeout,
        this,
        &SessionShareScope::expireDeadlines);
    const auto revalidate = [this] { reconcileCatalog(false); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, revalidate);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, revalidate);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, revalidate);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, revalidate);
    connect(
        &sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        [this] { reconcileCatalog(true); });
}

quint64 SessionShareScope::stateRevision() const noexcept
{
    return m_stateRevision;
}

bool SessionShareScope::canChange(const QString& sessionId) const
{
    if (!m_authenticated) {
        return false;
    }
    const auto context = m_sessions.actionContext(sessionId);
    return context && context->kind == QStringLiteral("local")
        && context->commandable
        && context->status != QStringLiteral("stopping")
        && context->status != QStringLiteral("stopped")
        && context->incarnationId.size() > 0;
}

QString SessionShareScope::currentScope(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context ? context->scope : QString {};
}

QString SessionShareScope::currentMissionId(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context ? context->roomId : QString {};
}

QString SessionShareScope::phase(const QString& sessionId) const
{
    const auto found = m_mutations.constFind(sessionId);
    return found == m_mutations.cend()
        ? QStringLiteral("idle")
        : phaseName(found->phase);
}

QString SessionShareScope::message(const QString& sessionId) const
{
    const auto found = m_mutations.constFind(sessionId);
    return found == m_mutations.cend() ? QString {} : found->message;
}

QString SessionShareScope::requestId(const QString& sessionId) const
{
    const auto found = m_mutations.constFind(sessionId);
    return found == m_mutations.cend() ? QString {} : found->requestId;
}

bool SessionShareScope::setScope(
    const QString& sessionId,
    const QString& scope,
    const QString& missionId)
{
    const auto target = decodeTarget(scope, missionId);
    auto existing = m_mutations.find(sessionId);
    if (existing != m_mutations.end()
        && (existing->phase == Phase::Pending
            || existing->phase == Phase::Accepted
            || existing->phase == Phase::Unknown)) {
        if (target && existing->phase == Phase::Unknown
            && existing->target == *target) {
            return retry(sessionId);
        }
        existing->message =
            QStringLiteral("Finish or retry the current sharing change first.");
        bumpState();
        return false;
    }
    if (!target) {
        recordFailure(
            sessionId,
            {.scope = scope, .missionId = missionId},
            scope == QStringLiteral("friends")
                ? QStringLiteral("Friends sharing cannot be selected.")
                : QStringLiteral("That sharing audience is invalid."));
        return false;
    }

    QString error;
    const auto context = commandContext(sessionId, *target, error);
    if (!context) {
        recordFailure(sessionId, *target, std::move(error));
        return false;
    }
    if (projectedTargetMatches(*context, *target)) {
        recordFailure(
            sessionId,
            *target,
            QStringLiteral("The session already uses that audience."));
        return false;
    }
    if (!ensureCapacity(sessionId)) {
        return false;
    }

    Mutation mutation {
        .requestId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .sessionId = sessionId,
        .incarnationId = context->incarnationId,
        .target = *target,
        .phase = Phase::Pending,
        .message = {},
        .deadlineMs = 0,
    };
    m_mutations.insert(sessionId, mutation);
    m_order.removeAll(sessionId);
    m_order.push_back(sessionId);
    auto inserted = m_mutations.find(sessionId);
    if (!dispatch(*inserted, false)) {
        return false;
    }
    bumpState();
    return true;
}

bool SessionShareScope::retry(const QString& sessionId)
{
    auto found = m_mutations.find(sessionId);
    if (found == m_mutations.end()) {
        return false;
    }
    if (found->phase == Phase::Unknown) {
        const auto context = m_sessions.actionContext(sessionId);
        if (!m_authenticated || !context
            || context->incarnationId != found->incarnationId) {
            failMutation(
                sessionId,
                QStringLiteral(
                    "The session restarted before this sharing change finished."));
            return false;
        }
        found->phase = Phase::Pending;
        found->message.clear();
        if (!dispatch(*found, true)) {
            return false;
        }
        bumpState();
        return true;
    }
    if (found->phase != Phase::Failed) {
        return false;
    }
    const auto target = found->target;
    m_mutations.remove(sessionId);
    m_order.removeAll(sessionId);
    return setScope(sessionId, target.scope, target.missionId);
}

void SessionShareScope::ingestAuthEvent(QByteArray json)
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

void SessionShareScope::ingestSessionEvent(QByteArray json)
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

void SessionShareScope::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_timer.stop();
    m_mutations.clear();
    m_order.clear();
    m_authenticated = false;
    bumpState();
}

std::optional<SessionShareScope::Target> SessionShareScope::decodeTarget(
    const QString& scope,
    const QString& missionId) const
{
    if (scope == QStringLiteral("justMe")
        || scope == QStringLiteral("myDevices")) {
        if (!missionId.isEmpty()) {
            return std::nullopt;
        }
        return Target {.scope = scope, .missionId = {}};
    }
    if (scope == QStringLiteral("room") && !missionId.isEmpty()) {
        return Target {.scope = scope, .missionId = missionId};
    }
    return std::nullopt;
}

std::optional<SessionCatalogModel::ActionContext>
SessionShareScope::commandContext(
    const QString& sessionId,
    const Target& target,
    QString& error) const
{
    if (!m_authenticated) {
        error = QStringLiteral("Sign in before changing the session audience.");
        return std::nullopt;
    }
    const auto context = m_sessions.actionContext(sessionId);
    if (!context) {
        error = QStringLiteral("The session is no longer available.");
        return std::nullopt;
    }
    if (context->kind != QStringLiteral("local")) {
        error = QStringLiteral("Only local sessions can change audience.");
        return std::nullopt;
    }
    if (!context->commandable || context->incarnationId.isEmpty()
        || context->status == QStringLiteral("stopping")
        || context->status == QStringLiteral("stopped")) {
        error =
            QStringLiteral("This session cannot change audience right now.");
        return std::nullopt;
    }
    if (target.scope == QStringLiteral("room")
        && !m_missions.containsMission(target.missionId)) {
        error = QStringLiteral("That Mission is no longer available.");
        return std::nullopt;
    }
    return context;
}

bool SessionShareScope::dispatch(Mutation& mutation, const bool retainedRetry)
{
    QString error;
    const auto context = retainedRetry
        ? m_sessions.actionContext(mutation.sessionId)
        : commandContext(mutation.sessionId, mutation.target, error);
    if (!context || context->incarnationId != mutation.incarnationId) {
        if (retainedRetry) {
            mutation.phase = Phase::Failed;
            mutation.message = QStringLiteral(
                "The session restarted before this sharing change finished.");
        } else {
            mutation.phase = Phase::Failed;
            mutation.message = context
                ? QStringLiteral(
                      "The session restarted before this sharing change started.")
                : std::move(error);
        }
        mutation.deadlineMs = 0;
        scheduleTimer();
        bumpState();
        return false;
    }

    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("session.scope")},
        {QStringLiteral("requestId"), mutation.requestId},
        {QStringLiteral("sessionId"), mutation.sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"),
         mutation.incarnationId},
        {QStringLiteral("scope"), mutation.target.scope},
    };
    if (mutation.target.scope == QStringLiteral("room")) {
        command.insert(
            QStringLiteral("roomId"),
            mutation.target.missionId);
    }
    const auto payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    if (const auto result =
            m_dispatcher.send(CommandLane::Sessions, payload);
        !result) {
        mutation.phase = retainedRetry ? Phase::Unknown : Phase::Failed;
        mutation.message = retainedRetry
            ? QStringLiteral(
                  "The sharing outcome is still unknown. Retry when the runtime is available.")
            : result.error().message;
        mutation.deadlineMs = 0;
        scheduleTimer();
        bumpState();
        return false;
    }
    mutation.phase = Phase::Pending;
    mutation.message.clear();
    mutation.deadlineMs = m_clock.elapsed() + m_timing.receiptTimeoutMs;
    scheduleTimer();
    return true;
}

bool SessionShareScope::projectedTargetMatches(
    const SessionCatalogModel::ActionContext& context,
    const Target& target) const
{
    return context.scope == target.scope
        && (target.scope != QStringLiteral("room")
            ? context.roomId.isEmpty()
            : context.roomId == target.missionId);
}

bool SessionShareScope::ensureCapacity(const QString& sessionId)
{
    if (m_mutations.contains(sessionId)
        || m_mutations.size() < maximumRetainedStates) {
        return true;
    }
    for (auto entry = m_order.begin();
         entry != m_order.end()
         && m_mutations.size() >= maximumRetainedStates;) {
        const auto found = m_mutations.constFind(*entry);
        if (found == m_mutations.cend()
            || found->phase == Phase::Failed) {
            m_mutations.remove(*entry);
            entry = m_order.erase(entry);
        } else {
            ++entry;
        }
    }
    return m_mutations.size() < maximumRetainedStates;
}

void SessionShareScope::recordFailure(
    const QString& sessionId,
    Target target,
    QString message)
{
    if (sessionId.isEmpty() || !ensureCapacity(sessionId)) {
        return;
    }
    m_mutations.insert(
        sessionId,
        {
            .requestId = {},
            .sessionId = sessionId,
            .incarnationId = {},
            .target = std::move(target),
            .phase = Phase::Failed,
            .message = std::move(message),
            .deadlineMs = 0,
        });
    m_order.removeAll(sessionId);
    m_order.push_back(sessionId);
    scheduleTimer();
    bumpState();
}

void SessionShareScope::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        m_timer.stop();
        m_mutations.clear();
        m_order.clear();
        m_authenticated = !userId.isEmpty();
        bumpState();
    }
    for (auto& pending : activation.pendingEvents) {
        ingestSessionEvent(std::move(pending));
    }
}

void SessionShareScope::applySessionEvent(
    const QJsonObject& object,
    const QByteArrayView json)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        return;
    }
    if (*type == QStringLiteral("session.scopeAccepted")) {
        const auto request = requiredString(
            object, QStringLiteral("requestId"));
        const auto session = requiredString(
            object, QStringLiteral("sessionId"));
        const auto incarnation = requiredString(
            object, QStringLiteral("expectedRuntimeIncarnationId"));
        const auto scope = requiredString(object, QStringLiteral("scope"));
        const auto budget =
            exactUnsignedJsonField(json, QByteArrayLiteral("budgetMs"));
        if (!request || !session || !incarnation || !scope || !budget) {
            return;
        }
        auto mutation = m_mutations.find(*session);
        if (mutation == m_mutations.end()
            || mutation->phase == Phase::Failed
            || mutation->requestId != *request
            || mutation->incarnationId != *incarnation
            || mutation->target.scope != *scope
            || (*scope == QStringLiteral("room")
                ? optionalString(object, QStringLiteral("roomId"))
                    != mutation->target.missionId
                : !absent(object.value(QStringLiteral("roomId"))))) {
            return;
        }
        mutation->phase = Phase::Accepted;
        mutation->message.clear();
        if (*budget > 0) {
            const auto bounded = std::min<quint64>(
                *budget,
                static_cast<quint64>(
                    m_timing.maximumAcceptedBudgetMs));
            mutation->deadlineMs =
                m_clock.elapsed() + static_cast<qint64>(bounded);
        }
        scheduleTimer();
        bumpState();
        return;
    }
    if (*type == QStringLiteral("session.scopeChanged")) {
        const auto request = requiredString(
            object, QStringLiteral("requestId"));
        const auto session = requiredString(
            object, QStringLiteral("sessionId"));
        const auto incarnation = requiredString(
            object, QStringLiteral("expectedRuntimeIncarnationId"));
        const auto scope = requiredString(object, QStringLiteral("scope"));
        if (!request || !session || !incarnation || !scope) {
            return;
        }
        const auto mutation = m_mutations.constFind(*session);
        if (mutation == m_mutations.cend()
            || mutation->phase == Phase::Failed
            || mutation->requestId != *request
            || mutation->incarnationId != *incarnation
            || mutation->target.scope != *scope
            || (*scope == QStringLiteral("room")
                ? optionalString(object, QStringLiteral("roomId"))
                    != mutation->target.missionId
                : !absent(object.value(QStringLiteral("roomId"))))) {
            return;
        }
        settleSuccess(*session);
        return;
    }
    if (*type != QStringLiteral("session.error")) {
        return;
    }
    const auto operation = requiredString(
        object, QStringLiteral("operation"));
    const auto request = requiredString(
        object, QStringLiteral("requestId"));
    const auto session = requiredString(
        object, QStringLiteral("sessionId"));
    const auto errorMessage = requiredString(
        object, QStringLiteral("message"));
    if (!operation || *operation != QStringLiteral("session.scope")
        || !request || !session || !errorMessage) {
        return;
    }
    const auto mutation = m_mutations.constFind(*session);
    if (mutation == m_mutations.cend()
        || mutation->requestId != *request) {
        return;
    }
    failMutation(*session, *errorMessage);
}

void SessionShareScope::reconcileCatalog(const bool authoritativeSnapshot)
{
    const auto sessionIds = m_mutations.keys();
    for (const auto& sessionId : sessionIds) {
        auto mutation = m_mutations.find(sessionId);
        if (mutation == m_mutations.end()
            || mutation->phase == Phase::Failed) {
            continue;
        }
        const auto context = m_sessions.actionContext(sessionId);
        if (!context) {
            failMutation(
                sessionId,
                QStringLiteral(
                    "The session disappeared before this sharing change finished."));
            continue;
        }
        if (context->incarnationId != mutation->incarnationId) {
            failMutation(
                sessionId,
                QStringLiteral(
                    "The session restarted before this sharing change finished."));
            continue;
        }
        if (authoritativeSnapshot
            && projectedTargetMatches(*context, mutation->target)) {
            settleSuccess(sessionId);
        }
    }
    bumpState();
}

void SessionShareScope::settleSuccess(const QString& sessionId)
{
    if (m_mutations.remove(sessionId) == 0) {
        return;
    }
    m_order.removeAll(sessionId);
    scheduleTimer();
    bumpState();
    emit transitionSucceeded(sessionId);
}

void SessionShareScope::failMutation(
    const QString& sessionId,
    QString message)
{
    auto found = m_mutations.find(sessionId);
    if (found == m_mutations.end()) {
        return;
    }
    found->phase = Phase::Failed;
    found->message = std::move(message);
    found->deadlineMs = 0;
    scheduleTimer();
    bumpState();
}

void SessionShareScope::scheduleTimer()
{
    qint64 nearest = std::numeric_limits<qint64>::max();
    const auto now = m_clock.elapsed();
    for (auto mutation = m_mutations.cbegin();
         mutation != m_mutations.cend();
         ++mutation) {
        if (mutation->deadlineMs > 0) {
            nearest = std::min(nearest, mutation->deadlineMs);
        }
    }
    if (nearest == std::numeric_limits<qint64>::max()) {
        m_timer.stop();
        return;
    }
    const auto delay = std::clamp<qint64>(
        nearest - now,
        0,
        std::numeric_limits<int>::max());
    m_timer.start(static_cast<int>(delay));
}

void SessionShareScope::expireDeadlines()
{
    const auto now = m_clock.elapsed();
    bool changed = false;
    for (auto mutation = m_mutations.begin();
         mutation != m_mutations.end();
         ++mutation) {
        if (mutation->deadlineMs <= 0 || mutation->deadlineMs > now) {
            continue;
        }
        mutation->phase = Phase::Unknown;
        mutation->message = QStringLiteral(
            "The sharing change may still have been applied. Retry to check it safely.");
        mutation->deadlineMs = 0;
        changed = true;
    }
    scheduleTimer();
    if (changed) {
        bumpState();
    }
}

void SessionShareScope::bumpState()
{
    ++m_stateRevision;
    emit stateChanged();
}

QString SessionShareScope::phaseName(const Phase phase)
{
    switch (phase) {
    case Phase::Pending:
        return QStringLiteral("pending");
    case Phase::Accepted:
        return QStringLiteral("accepted");
    case Phase::Unknown:
        return QStringLiteral("unknown");
    case Phase::Failed:
        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

} // namespace kodosi
