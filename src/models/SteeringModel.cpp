#include "models/SteeringModel.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <charconv>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace kodosi {
namespace {

bool withinScalarLimit(const QStringView value, const qsizetype maximum)
{
    qsizetype count = 0;
    for (qsizetype offset = 0; offset < value.size();) {
        const auto first = value.at(offset++);
        if (first.isHighSurrogate() && offset < value.size()
            && value.at(offset).isLowSurrogate()) {
            ++offset;
        }
        if (++count > maximum) {
            return false;
        }
    }
    return true;
}

std::optional<QString> requiredString(
    const QJsonObject& object,
    const QString& key,
    const qsizetype maximum)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    if (text.isEmpty() || text.contains(QChar::Null)
        || !withinScalarLimit(text, maximum)) {
        return std::nullopt;
    }
    return text;
}

bool optionalString(
    const QJsonObject& object,
    const QString& key,
    QString& destination,
    const qsizetype maximum)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        destination.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    if (text.contains(QChar::Null) || !withinScalarLimit(text, maximum)) {
        return false;
    }
    destination = text;
    return true;
}

QString optionalAccountId(const QJsonObject& object, bool& valid)
{
    const auto value = object.value(QStringLiteral("accountUserId"));
    if (value.isUndefined() || value.isNull()) {
        valid = true;
        return {};
    }
    if (!value.isString()) {
        valid = false;
        return {};
    }
    const auto text = value.toString();
    valid = !text.isEmpty() && !text.contains(QChar::Null)
        && withinScalarLimit(text, 1'024);
    return valid ? text : QString {};
}

std::optional<QVector<quint64>> exactUnsignedFields(
    const QByteArrayView json,
    const QByteArrayView field)
{
    const auto expected = QString::fromUtf8(field);
    QVector<quint64> values;
    for (qsizetype cursor = 0; cursor < json.size(); ++cursor) {
        if (json[cursor] != '"') {
            continue;
        }
        const auto tokenStart = cursor;
        bool escaped = false;
        for (++cursor; cursor < json.size(); ++cursor) {
            if (escaped) {
                escaped = false;
            } else if (json[cursor] == '\\') {
                escaped = true;
            } else if (json[cursor] == '"') {
                break;
            }
        }
        if (cursor >= json.size()) {
            return std::nullopt;
        }
        QJsonParseError parseError;
        const auto token = QJsonDocument::fromJson(
            QByteArrayLiteral("[")
                + json.sliced(tokenStart, cursor - tokenStart + 1).toByteArray()
                + QByteArrayLiteral("]"),
            &parseError);
        if (parseError.error != QJsonParseError::NoError || !token.isArray()
            || token.array().size() != 1 || !token.array().at(0).isString()
            || token.array().at(0).toString() != expected) {
            continue;
        }
        auto valueCursor = cursor + 1;
        while (valueCursor < json.size()
            && (json[valueCursor] == ' ' || json[valueCursor] == '\t'
                || json[valueCursor] == '\r' || json[valueCursor] == '\n')) {
            ++valueCursor;
        }
        if (valueCursor >= json.size() || json[valueCursor] != ':') {
            continue;
        }
        ++valueCursor;
        while (valueCursor < json.size()
            && (json[valueCursor] == ' ' || json[valueCursor] == '\t'
                || json[valueCursor] == '\r' || json[valueCursor] == '\n')) {
            ++valueCursor;
        }
        const auto start = valueCursor;
        while (valueCursor < json.size() && json[valueCursor] >= '0'
            && json[valueCursor] <= '9') {
            ++valueCursor;
        }
        if (valueCursor == start) {
            return std::nullopt;
        }
        quint64 value = 0;
        const auto parsed = std::from_chars(
            json.data() + start,
            json.data() + valueCursor,
            value);
        if (parsed.ec != std::errc {} || parsed.ptr != json.data() + valueCursor) {
            return std::nullopt;
        }
        values.push_back(value);
    }
    return values;
}

std::optional<SteeringModel::State> deliveryState(const QString& value)
{
    if (value == QStringLiteral("preparing")) {
        return SteeringModel::State::Preparing;
    }
    if (value == QStringLiteral("queued")) {
        return SteeringModel::State::Queued;
    }
    if (value == QStringLiteral("deliveryUnknown")) {
        return SteeringModel::State::DeliveryUnknown;
    }
    if (value == QStringLiteral("injected")) {
        return SteeringModel::State::Injected;
    }
    if (value == QStringLiteral("cancelled")) {
        return SteeringModel::State::Cancelled;
    }
    return std::nullopt;
}

std::optional<SteeringModel::State> transitionState(const QString& value)
{
    if (value == QStringLiteral("queued")) {
        return SteeringModel::State::Queued;
    }
    if (value == QStringLiteral("sending")) {
        return SteeringModel::State::Sending;
    }
    if (value == QStringLiteral("injected")) {
        return SteeringModel::State::Injected;
    }
    if (value == QStringLiteral("failed")) {
        return SteeringModel::State::Failed;
    }
    if (value == QStringLiteral("cancelled")) {
        return SteeringModel::State::Cancelled;
    }
    if (value == QStringLiteral("deliveryUnknown")) {
        return SteeringModel::State::DeliveryUnknown;
    }
    return std::nullopt;
}

bool terminalState(const SteeringModel::State state)
{
    return state == SteeringModel::State::Injected
        || state == SteeringModel::State::Cancelled
        || state == SteeringModel::State::Failed;
}

} // namespace

bool SteeringModel::RequestKey::operator<(const RequestKey& other) const
{
    return std::tie(accountUserId, requestId, sessionId, incarnationId)
        < std::tie(
            other.accountUserId,
            other.requestId,
            other.sessionId,
            other.incarnationId);
}

bool SteeringModel::HydrationKey::operator<(const HydrationKey& other) const
{
    return std::tie(
               accountUserId,
               sessionId,
               incarnationId,
               canQueue,
               canSteer,
               canStopAndSend)
        < std::tie(
               other.accountUserId,
               other.sessionId,
               other.incarnationId,
               other.canQueue,
               other.canSteer,
               other.canStopAndSend);
}

SteeringModel::SteeringModel(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    QObject* parent)
    : SteeringModel(dispatcher, sessions, Timing {}, parent)
{
}

SteeringModel::SteeringModel(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    Timing timing,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_timing(timing)
{
    Q_ASSERT(timing.replyTimeoutMs > 0);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SteeringModel::expireOperations);
    const auto reconcile = [this] { reconcileCatalog(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, reconcile);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, reconcile);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, reconcile);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, reconcile);
}

quint64 SteeringModel::stateRevision() const noexcept
{
    return m_stateRevision;
}

QString SteeringModel::draftText() const
{
    return m_drafts.value(m_selectedSessionId).text;
}

QString SteeringModel::draftMode() const
{
    const auto preferred = m_drafts.value(m_selectedSessionId).mode;
    QString ignored;
    const auto context = currentContext(m_selectedSessionId, ignored);
    const auto mode = context ? effectiveMode(preferred, *context)
                              : std::optional<Mode> {};
    return modeName(mode.value_or(preferred));
}

QStringList SteeringModel::availableModes() const
{
    QString ignored;
    const auto context = currentContext(m_selectedSessionId, ignored);
    if (!context) {
        return {};
    }
    QStringList modes;
    if (context->canQueue) {
        modes.push_back(QStringLiteral("queue"));
    }
    if (context->canSteer) {
        modes.push_back(QStringLiteral("steer"));
    }
    if (context->canStopAndSend) {
        modes.push_back(QStringLiteral("stopAndSend"));
    }
    return modes;
}

QString SteeringModel::statusText() const
{
    const auto* selected = selectedRequest(m_selectedSessionId);
    return selected == nullptr ? QString {} : stateMessage(*selected);
}

QString SteeringModel::error() const
{
    return m_errors.value(m_selectedSessionId);
}

bool SteeringModel::busy() const
{
    return blockingRequestCount(m_selectedSessionId) > 0;
}

bool SteeringModel::canSend() const
{
    return canSendTo(m_selectedSessionId);
}

bool SteeringModel::canSendTo(const QString& sessionId) const
{
    QString ignored;
    const auto context = currentContext(sessionId, ignored);
    if (!context || blockingRequestCount(sessionId) > 0) {
        return false;
    }
    const auto hydration = currentHydrationKey(sessionId, ignored);
    if (!hydration || !m_hydratedAuthorities.contains(*hydration)) {
        return false;
    }
    const auto draft = m_drafts.value(sessionId);
    const auto payload = draft.text.trimmed();
    return effectiveMode(draft.mode, *context).has_value() && !payload.isEmpty()
        && payload.toUtf8().size() <= maximumSteerTextBytes;
}

bool SteeringModel::canCancel() const
{
    return canCancelFor(m_selectedSessionId);
}

bool SteeringModel::canCancelFor(const QString& sessionId) const
{
    const auto* selected = selectedRequest(sessionId);
    return selected != nullptr && cancellable(*selected)
        && std::ranges::none_of(m_operations, [&](const Operation& operation) {
               return operation.kind == OperationKind::Cancel
                   && operation.target
                   && *operation.target == selected->key
                   && operation.deadlineMs > 0;
           });
}

bool SteeringModel::canRetry() const
{
    return canRetryFor(m_selectedSessionId);
}

bool SteeringModel::canRetryFor(const QString& sessionId) const
{
    const auto* selected = selectedRequest(sessionId);
    const auto exactQueryActive = selected != nullptr
        && std::ranges::any_of(m_operations, [&](const Operation& operation) {
               return operation.kind == OperationKind::QueryExact
                   && operation.target && *operation.target == selected->key
                   && operation.deadlineMs > 0;
           });
    QString ignored;
    const auto hydration = currentHydrationKey(sessionId, ignored);
    return (selected != nullptr
               && (selected->state == State::Reconciling
                   || selected->state == State::DeliveryUnknown)
               && !exactQueryActive)
        || (hydration && m_fullQueryRetryAuthorities.contains(*hydration));
}

int SteeringModel::retainedCount() const
{
    return retainedRequestCount(m_selectedSessionId);
}

QVariantMap SteeringModel::presentationForSession(const QString& sessionId) const
{
    const auto* request = selectedRequest(sessionId);
    return {
        {QStringLiteral("draftText"), m_drafts.value(sessionId).text},
        {QStringLiteral("statusText"), request == nullptr ? QString {} : stateMessage(*request)},
        {QStringLiteral("error"), m_errors.value(sessionId)},
        {QStringLiteral("canSend"), canSendTo(sessionId)},
        {QStringLiteral("canCancel"), canCancelFor(sessionId)},
        {QStringLiteral("canRetry"), canRetryFor(sessionId)},
    };
}

bool SteeringModel::inspect(const QString& sessionId)
{
    QString message;
    const auto context = currentContext(sessionId, message);
    if (!context) {
        const auto catalogContext = m_sessions.actionContext(sessionId);
        const auto incarnation =
            catalogContext ? catalogContext->incarnationId : QString {};
        const auto changed = m_selectedSessionId != sessionId
            || m_selectedIncarnationId != incarnation;
        m_selectedSessionId = sessionId;
        m_selectedIncarnationId = incarnation;
        m_errors[sessionId] = std::move(message);
        if (changed) {
            bumpState();
        }
        return false;
    }
    const auto changed = m_selectedSessionId != sessionId
        || m_selectedIncarnationId != context->incarnationId;
    m_selectedSessionId = sessionId;
    m_selectedIncarnationId = context->incarnationId;
    if (changed) {
        bumpState();
    }
    return rehydrate(sessionId);
}

void SteeringModel::clearInspection()
{
    if (m_selectedSessionId.isEmpty()) {
        return;
    }
    m_selectedSessionId.clear();
    m_selectedIncarnationId.clear();
    bumpState();
}

bool SteeringModel::rehydrate(const QString& sessionId)
{
    QString message;
    const auto hydration = currentHydrationKey(sessionId, message);
    if (!hydration) {
        m_errors[sessionId] = std::move(message);
        bumpState();
        return false;
    }
    if (m_hydratedAuthorities.contains(*hydration)) {
        return true;
    }
    const auto active = std::ranges::any_of(
        m_operations,
        [&](const Operation& operation) {
            return operation.kind == OperationKind::QueryFull
                && operation.hydration == hydration
                && operation.deadlineMs > 0;
        });
    if (active) {
        return true;
    }
    return dispatchFullQuery(*hydration);
}

bool SteeringModel::saveDraft(
    const QString& sessionId,
    const QString& text,
    const QString& mode)
{
    if (text.contains(QChar::Null) || text.toUtf8().size() > maximumDraftBytes) {
        m_errors[sessionId] =
            QStringLiteral("This draft is too large to keep in the steering footer.");
        bumpState();
        return false;
    }
    const auto decodedMode = decodeMode(mode);
    if (!decodedMode) {
        m_errors[sessionId] =
            QStringLiteral("That steering mode is not available.");
        bumpState();
        return false;
    }
    auto savedMode = *decodedMode;
    QString ignored;
    if (const auto context = currentContext(sessionId, ignored)) {
        savedMode = effectiveMode(savedMode, *context).value_or(savedMode);
    }
    auto& draft = m_drafts[sessionId];
    if (draft.text == text && draft.mode == savedMode) {
        return true;
    }
    draft.text = text;
    draft.mode = savedMode;
    ++draft.revision;
    m_errors.remove(sessionId);
    bumpState();
    return true;
}

bool SteeringModel::send(const QString& sessionId)
{
    QString message;
    const auto context = currentContext(sessionId, message);
    if (!context) {
        m_errors[sessionId] = std::move(message);
        bumpState();
        return false;
    }
    if (blockingRequestCount(sessionId) > 0) {
        m_errors[sessionId] =
            QStringLiteral("A previous message is still runtime-owned. Reconcile or cancel it first.");
        bumpState();
        return false;
    }
    const auto hydration = currentHydrationKey(sessionId, message);
    if (!hydration || !m_hydratedAuthorities.contains(*hydration)) {
        m_errors[sessionId] = QStringLiteral(
            "Wait for the runtime to finish loading authoritative steering state.");
        bumpState();
        return false;
    }
    auto draft = m_drafts.value(sessionId);
    const auto selectedMode = effectiveMode(draft.mode, *context);
    const auto payload = draft.text.trimmed();
    if (!selectedMode) {
        m_errors[sessionId] =
            QStringLiteral("The runtime no longer advertises that steering mode.");
        bumpState();
        return false;
    }
    if (draft.mode != *selectedMode) {
        auto& storedDraft = m_drafts[sessionId];
        storedDraft.mode = *selectedMode;
        ++storedDraft.revision;
        draft = storedDraft;
    }
    const auto mode = modeName(*selectedMode);
    if (payload.isEmpty()) {
        m_errors[sessionId] = QStringLiteral("Enter a message before sending.");
        bumpState();
        return false;
    }
    if (payload.toUtf8().size() > maximumSteerTextBytes) {
        m_errors[sessionId] =
            QStringLiteral("This message exceeds the runtime's 16 KiB steering limit.");
        bumpState();
        return false;
    }
    if (!ensureBlockingCapacity(1)) {
        m_errors[sessionId] =
            QStringLiteral("Too many session messages are awaiting runtime results.");
        bumpState();
        return false;
    }

    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    Request requestValue {
        .key = {
            .accountUserId = semanticAccountUserId(),
            .requestId = requestId,
            .sessionId = sessionId,
            .incarnationId = context->incarnationId,
        },
        .mode = draft.mode,
        .payload = payload,
        .submittedDraft = draft.text,
        .draftRevision = draft.revision,
        .version = ++m_nextVersion,
        .order = ++m_nextOrder,
        .state = State::Sending,
        .message = {},
        .runtime = std::nullopt,
        .locallyOwned = true,
    };
    m_requests[requestValue.key] = requestValue;
    const QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.semanticSend")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("incarnationId"), context->incarnationId},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("text"), payload},
    };
    Operation operation {
        .requestId = requestId,
        .kind = OperationKind::Send,
        .sessionId = sessionId,
        .incarnationId = context->incarnationId,
        .target = requestValue.key,
        .snapshot = {},
        .hydration = std::nullopt,
    };
    if (!dispatch(std::move(operation), command)) {
        auto* stored = mutableRequest(requestValue.key);
        if (stored != nullptr) {
            markFailed(
                *stored,
                QStringLiteral("The runtime did not accept this message. Your draft was kept."));
        }
        bumpState();
        return false;
    }
    m_errors.remove(sessionId);
    bumpState();
    return true;
}

bool SteeringModel::cancel(const QString& sessionId)
{
    const auto* selected = selectedRequest(sessionId);
    if (selected == nullptr || !cancellable(*selected)
        || !selected->runtime) {
        m_errors[sessionId] =
            QStringLiteral("The selected steering request can no longer be cancelled.");
        bumpState();
        return false;
    }
    QString message;
    const auto context = currentContext(sessionId, message);
    if (!context || context->incarnationId != selected->key.incarnationId) {
        m_errors[sessionId] = QStringLiteral(
            "The session restarted before this steering request could be cancelled.");
        bumpState();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto target = selected->key;
    const QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.cancelSteer")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("steerId"), selected->runtime->steerId},
    };
    Operation operation {
        .requestId = requestId,
        .kind = OperationKind::Cancel,
        .sessionId = sessionId,
        .incarnationId = context->incarnationId,
        .target = target,
        .snapshot = {},
        .hydration = std::nullopt,
    };
    if (!dispatch(std::move(operation), command)) {
        m_errors[sessionId] =
            QStringLiteral("The runtime did not accept the cancellation. The request remains queued.");
        bumpState();
        return false;
    }
    m_errors.remove(sessionId);
    bumpState();
    return true;
}

bool SteeringModel::retry(const QString& sessionId)
{
    QString ignored;
    const auto hydration = currentHydrationKey(sessionId, ignored);
    if (hydration && m_fullQueryRetryAuthorities.contains(*hydration)) {
        return rehydrate(sessionId);
    }
    const auto* selected = selectedRequest(sessionId);
    if (selected != nullptr
        && (selected->state == State::Reconciling
            || selected->state == State::DeliveryUnknown)) {
        return dispatchExactQuery(
            selected->key,
            QStringLiteral("The runtime still could not confirm this message."));
    }
    return false;
}

void SteeringModel::clearError(const QString& sessionId)
{
    if (m_errors.remove(sessionId) > 0) {
        bumpState();
    }
}

int SteeringModel::retainedRequestCount(const QString& sessionId) const
{
    return static_cast<int>(std::ranges::count_if(
        m_requests,
        [&](const auto& pair) {
            return pair.second.key.sessionId == sessionId
                && pair.second.key.incarnationId
                    == m_sessions.incarnationForSession(sessionId).value_or(QString {});
        }));
}

int SteeringModel::blockingRequestCount(const QString& sessionId) const
{
    const auto incarnation =
        m_sessions.incarnationForSession(sessionId).value_or(QString {});
    return static_cast<int>(std::ranges::count_if(
        m_requests,
        [&](const auto& pair) {
            return pair.second.key.sessionId == sessionId
                && pair.second.key.incarnationId == incarnation
                && blocks(pair.second);
        }));
}

void SteeringModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type =
        requiredString(object, QStringLiteral("type"), maximumIdentityScalars);
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!type || !epoch
        || (*type != QStringLiteral("auth.ready")
            && *type != QStringLiteral("auth.required"))) {
        return;
    }
    QString userId;
    if (*type == QStringLiteral("auth.ready")) {
        const auto value = object.value(QStringLiteral("userId"));
        if (!value.isNull() && !value.isString()) {
            return;
        }
        if (value.isString()) {
            userId = value.toString();
            if (userId.isEmpty() || userId.contains(QChar::Null)
                || !withinScalarLimit(userId, maximumIdentityScalars)) {
                return;
            }
        }
    }
    activateAccount(
        std::move(userId),
        *epoch,
        *type == QStringLiteral("auth.ready"));
}

void SteeringModel::ingestAgentIntelEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Steering event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Steering event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    bool accountValid = false;
    const auto accountUserId = optionalAccountId(object, accountValid);
    if (!epoch || !accountValid) {
        emit decodeError(QStringLiteral("Steering event has malformed account authority."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = accountUserId, .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(QStringLiteral("Steering event exceeds the account backlog limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    const auto type =
        requiredString(object, QStringLiteral("type"), maximumIdentityScalars);
    if (!type) {
        emit decodeError(QStringLiteral("Steering event has no valid type."));
        return;
    }
    if (*type == QStringLiteral("agent.intel.reply")) {
        applyReply(json, object);
    } else if (*type == QStringLiteral("agent.intel.error")) {
        applyError(object);
    } else if (*type == QStringLiteral("agent.intel.steerState")) {
        applyRuntimeEvent(json, object);
    }
}

void SteeringModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_timer.stop();
    m_requests.clear();
    m_drafts.clear();
    m_errors.clear();
    m_operations.clear();
    m_hydratedAuthorities.clear();
    m_fullQueryRetryAuthorities.clear();
    m_selectedSessionId.clear();
    m_selectedIncarnationId.clear();
    m_accountUserId.clear();
    m_authenticated = false;
    m_hasAccountActivation = false;
    bumpState();
}

std::optional<SessionCatalogModel::ActionContext> SteeringModel::currentContext(
    const QString& sessionId,
    QString& message) const
{
    if (!m_hasAccountActivation) {
        message = QStringLiteral("Wait for the runtime account context before steering.");
        return std::nullopt;
    }
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || context->incarnationId.isEmpty()) {
        message = QStringLiteral("Wait for this session to finish connecting.");
        return std::nullopt;
    }
    if (!m_authenticated && context->kind != QStringLiteral("local")) {
        message = QStringLiteral("Sign in before steering a remote session.");
        return std::nullopt;
    }
    if (!context->commandable
        || (!context->canQueue && !context->canSteer
            && !context->canStopAndSend)) {
        message = QStringLiteral("The runtime does not advertise steering for this session.");
        return std::nullopt;
    }
    if (!isUuidV7(context->incarnationId)) {
        message = QStringLiteral("The session incarnation is not valid for steering.");
        return std::nullopt;
    }
    return context;
}

std::optional<SteeringModel::HydrationKey>
SteeringModel::currentHydrationKey(
    const QString& sessionId,
    QString& message) const
{
    const auto context = currentContext(sessionId, message);
    if (!context) {
        return std::nullopt;
    }
    return HydrationKey {
        .accountUserId = semanticAccountUserId(),
        .sessionId = sessionId,
        .incarnationId = context->incarnationId,
        .canQueue = context->canQueue,
        .canSteer = context->canSteer,
        .canStopAndSend = context->canStopAndSend,
    };
}

QString SteeringModel::semanticAccountUserId() const
{
    return m_accountUserId.isEmpty() ? QStringLiteral("local") : m_accountUserId;
}

const SteeringModel::Request* SteeringModel::selectedRequest(
    const QString& sessionId) const
{
    const auto incarnation =
        m_sessions.incarnationForSession(sessionId).value_or(QString {});
    const Request* selected = nullptr;
    auto rank = [](const Request& requestValue) {
        switch (requestValue.state) {
        case State::Reconciling: return 0;
        case State::Sending:
        case State::Preparing: return 1;
        case State::Queued: return 2;
        case State::DeliveryUnknown:
        case State::Injected:
        case State::Cancelled: return 3;
        case State::Failed: return 4;
        }
        return 5;
    };
    for (const auto& [key, candidate] : m_requests) {
        if (key.sessionId != sessionId || key.incarnationId != incarnation) {
            continue;
        }
        if (selected == nullptr
            || (blocks(candidate) && !blocks(*selected))
            || (blocks(candidate) == blocks(*selected)
                && (rank(candidate) < rank(*selected)
                    || (rank(candidate) == rank(*selected)
                        && std::tuple(
                               candidate.runtime.has_value(),
                               candidate.order)
                            > std::tuple(
                                selected->runtime.has_value(),
                                selected->order))))) {
            selected = &candidate;
        }
    }
    return selected;
}

SteeringModel::Request* SteeringModel::mutableRequest(const RequestKey& key)
{
    const auto found = m_requests.find(key);
    return found == m_requests.end() ? nullptr : &found->second;
}

bool SteeringModel::dispatch(Operation operation, QJsonObject command)
{
    if (operation.target
        && std::ranges::any_of(m_operations, [&](const Operation& existing) {
               return existing.target == operation.target
                   && existing.deadlineMs > 0;
           })) {
        m_errors[operation.sessionId] =
            QStringLiteral("A steering operation is already awaiting a runtime reply.");
        return false;
    }
    const auto activeOperations = std::ranges::count_if(
        m_operations,
        [](const Operation& existing) { return existing.deadlineMs > 0; });
    if (activeOperations >= maximumBlockingRequests) {
        m_errors[operation.sessionId] =
            QStringLiteral("Too many steering operations are awaiting runtime replies.");
        return false;
    }
    const auto json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    const auto result = m_dispatcher.send(CommandLane::System, json);
    if (!result) {
        m_errors[operation.sessionId] = result.error().message;
        return false;
    }
    operation.deadlineMs =
        QDateTime::currentMSecsSinceEpoch() + m_timing.replyTimeoutMs;
    m_operations.insert(operation.requestId, std::move(operation));
    scheduleTimer();
    return true;
}

bool SteeringModel::dispatchFullQuery(const HydrationKey& hydration)
{
    m_hydratedAuthorities.erase(hydration);
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        if (iterator->kind == OperationKind::QueryFull
            && iterator->hydration == hydration
            && iterator->deadlineMs == 0) {
            iterator = m_operations.erase(iterator);
        } else {
            ++iterator;
        }
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    Operation operation {
        .requestId = requestId,
        .kind = OperationKind::QueryFull,
        .sessionId = hydration.sessionId,
        .incarnationId = hydration.incarnationId,
        .target = std::nullopt,
        .snapshot = {},
        .hydration = hydration,
    };
    for (const auto& [key, requestValue] : m_requests) {
        if (key.sessionId == hydration.sessionId
            && key.incarnationId == hydration.incarnationId) {
            operation.snapshot.emplace(key, requestValue.version);
        }
    }
    const QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.querySteer")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), hydration.sessionId},
    };
    if (!dispatch(std::move(operation), command)) {
        m_fullQueryRetryAuthorities.insert(hydration);
        if (blockingRequestCount(hydration.sessionId) > 0) {
            for (auto& [key, requestValue] : m_requests) {
                if (key.sessionId == hydration.sessionId
                    && key.incarnationId == hydration.incarnationId
                    && requestValue.state == State::Sending) {
                    markReconciling(
                        requestValue,
                        QStringLiteral("The runtime could not reconcile this message."));
                }
            }
        }
        bumpState();
        return false;
    }
    bumpState();
    return true;
}

bool SteeringModel::dispatchExactQuery(
    const RequestKey& key,
    QString failureMessage)
{
    auto* target = mutableRequest(key);
    if (target == nullptr) {
        return false;
    }
    const auto targetsExactRequest = [&](const Operation& operation) {
        return operation.target && *operation.target == key
            && (operation.kind == OperationKind::Send
                || operation.kind == OperationKind::Cancel
                || operation.kind == OperationKind::QueryExact);
    };
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        if (targetsExactRequest(*iterator) && iterator->deadlineMs == 0) {
            iterator = m_operations.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (std::ranges::any_of(m_operations, [&](const Operation& operation) {
            return targetsExactRequest(operation) && operation.deadlineMs > 0;
        })) {
        return true;
    }
    const auto current = m_sessions.incarnationForSession(key.sessionId);
    if (!current || *current != key.incarnationId) {
        markFailed(
            *target,
            QStringLiteral("The session restarted before steering reconciliation finished."));
        bumpState();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    Operation operation {
        .requestId = requestId,
        .kind = OperationKind::QueryExact,
        .sessionId = key.sessionId,
        .incarnationId = key.incarnationId,
        .target = key,
        .snapshot = {{key, target->version}},
        .hydration = std::nullopt,
    };
    const QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("agent.intel.querySteer")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), key.sessionId},
        {QStringLiteral("semanticRequestId"), key.requestId},
    };
    if (!dispatch(std::move(operation), command)) {
        markReconciling(*target, std::move(failureMessage));
        m_errors[key.sessionId] = target->message;
        bumpState();
        return false;
    }
    markReconciling(
        *target,
        QStringLiteral("Checking whether the runtime admitted this message…"));
    m_errors.remove(key.sessionId);
    bumpState();
    return true;
}

bool SteeringModel::applyEntry(
    const DecodedEntry& entry,
    const std::optional<State> transition,
    QString message,
    const bool prunePresentation)
{
    if (entry.key.accountUserId != semanticAccountUserId()) {
        return false;
    }
    const auto current = m_sessions.incarnationForSession(entry.key.sessionId);
    if (!current || *current != entry.key.incarnationId) {
        return false;
    }

    auto effective = entry.deliveryState;
    if (transition == State::Injected || transition == State::Cancelled) {
        effective = *transition;
    } else if (entry.deliveryState == State::Injected
        || entry.deliveryState == State::Cancelled) {
        effective = entry.deliveryState;
    } else if (transition) {
        effective = *transition;
    }

    auto* existing = mutableRequest(entry.key);
    if (existing != nullptr) {
        if (existing->mode != entry.mode
            || (!existing->payload.isEmpty() && !entry.text.isEmpty()
                && existing->payload != entry.text)) {
            return false;
        }
        if (terminal(*existing)) {
            return true;
        }
        if (!terminalState(effective) && existing->runtime
            && existing->runtime->steerId != entry.steerId) {
            return false;
        }
    } else if (!terminalState(effective) && !ensureBlockingCapacity(1)) {
        m_errors[entry.key.sessionId] =
            QStringLiteral("The runtime reported more active steering work than this client can retain.");
        return false;
    }
    if (!terminalState(effective)
        && (existing == nullptr || !existing->runtime)) {
        const auto runtimePending = std::ranges::count_if(
            m_requests,
            [&](const auto& pair) {
                return pair.first.sessionId == entry.key.sessionId
                    && pair.first.incarnationId == entry.key.incarnationId
                    && pair.second.runtime
                    && (pair.second.runtime->deliveryState == State::Preparing
                        || pair.second.runtime->deliveryState == State::Queued);
            });
        if (runtimePending >= maximumRuntimePendingPerSession) {
            m_errors[entry.key.sessionId] =
                QStringLiteral("The runtime steering snapshot exceeds its per-session pending limit.");
            return false;
        }
    }

    Request updated = existing == nullptr
        ? Request {
              .key = entry.key,
              .mode = entry.mode,
              .payload = entry.text,
              .submittedDraft = {},
              .draftRevision = 0,
              .version = ++m_nextVersion,
              .order = ++m_nextOrder,
              .state = effective,
              .message = {},
              .runtime = std::nullopt,
              .locallyOwned = false,
          }
        : *existing;
    if (updated.payload.isEmpty()) {
        updated.payload = entry.text;
    }
    updated.state = effective;
    updated.runtime = RuntimeEntry {
        .steerId = entry.steerId,
        .text = entry.text,
        .atToolUseId = entry.atToolUseId,
        .queuedAtMs = entry.queuedAtMs,
        .deliveryState = entry.deliveryState,
    };
    if (transition == State::Failed
        && (entry.deliveryState == State::Queued
            || entry.deliveryState == State::Preparing)) {
        updated.state = entry.deliveryState;
        updated.message = message.isEmpty()
            ? QStringLiteral("Runtime delivery failed; the request remains queued.")
            : std::move(message);
    } else {
        updated.message = std::move(message);
    }
    updated.version = ++m_nextVersion;
    m_requests[entry.key] = updated;
    if (updated.state == State::Injected) {
        clearMatchingDraft(updated);
    }
    if (updated.state != State::Reconciling && updated.state != State::Failed) {
        m_errors.remove(entry.key.sessionId);
    }
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        if (iterator->target && *iterator->target == entry.key) {
            iterator = m_operations.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (prunePresentation) {
        pruneTerminalHistory();
    }
    scheduleTimer();
    return true;
}

bool SteeringModel::applyQuery(
    const Operation& operation,
    const QVector<DecodedEntry>& entries)
{
    if (operation.kind == OperationKind::QueryExact) {
        if (!operation.target || entries.size() > 1) {
            return false;
        }
        auto* target = mutableRequest(*operation.target);
        if (target == nullptr) {
            return true;
        }
        if (entries.isEmpty()) {
            const auto failure = QStringLiteral(
                "The runtime did not durably admit this message. Your draft was kept.");
            markFailed(*target, failure);
            m_errors[operation.sessionId] = failure;
            return true;
        }
        if (entries.constFirst().key != *operation.target) {
            return false;
        }
        return applyEntry(entries.constFirst(), std::nullopt, {});
    }
    if (!operation.hydration) {
        return false;
    }

    auto backupRequests = m_requests;
    const auto backupErrors = m_errors;
    const auto backupDrafts = m_drafts;
    const auto backupVersion = m_nextVersion;
    const auto backupOrder = m_nextOrder;
    const auto backupOperations = m_operations;
    const auto backupFullQueryRetries = m_fullQueryRetryAuthorities;
    QSet<QString> returned;
    for (const auto& entry : entries) {
        if (entry.key.sessionId != operation.sessionId
            || entry.key.incarnationId != operation.incarnationId
            || !applyEntry(entry, std::nullopt, {}, false)) {
            m_requests = std::move(backupRequests);
            m_errors = backupErrors;
            m_drafts = backupDrafts;
            m_nextVersion = backupVersion;
            m_nextOrder = backupOrder;
            m_operations = backupOperations;
            m_fullQueryRetryAuthorities = backupFullQueryRetries;
            return false;
        }
        returned.insert(
            entry.key.accountUserId + QChar::Null + entry.key.requestId
            + QChar::Null + entry.key.sessionId + QChar::Null
            + entry.key.incarnationId);
    }
    for (const auto& [key, version] : operation.snapshot) {
        const auto token = key.accountUserId + QChar::Null + key.requestId
            + QChar::Null + key.sessionId + QChar::Null + key.incarnationId;
        if (returned.contains(token)) {
            continue;
        }
        auto found = m_requests.find(key);
        if (found == m_requests.end() || found->second.version != version
            || terminal(found->second)) {
            continue;
        }
        if (found->second.locallyOwned) {
            markFailed(
                found->second,
                QStringLiteral("The runtime no longer reports this message. Your draft was kept."));
            m_errors[key.sessionId] = found->second.message;
        } else {
            m_requests.erase(found);
        }
    }
    m_hydratedAuthorities.insert(*operation.hydration);
    m_fullQueryRetryAuthorities.erase(*operation.hydration);
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        if (iterator->kind == OperationKind::QueryFull
            && iterator->sessionId == operation.sessionId
            && iterator->incarnationId == operation.incarnationId) {
            iterator = m_operations.erase(iterator);
        } else {
            ++iterator;
        }
    }
    pruneTerminalHistory();
    return true;
}

bool SteeringModel::decodeReplyEntries(
    const QByteArray& json,
    const QJsonValue& payload,
    QVector<DecodedEntry>& entries,
    QString& message) const
{
    QJsonArray array;
    if (payload.isArray()) {
        array = payload.toArray();
    } else if (payload.isObject()) {
        array = QJsonArray {payload};
    } else {
        message = QStringLiteral("The steering reply payload has the wrong shape.");
        return false;
    }
    if (array.size() > maximumQueryEntries) {
        message = QStringLiteral("The steering reply exceeds the bounded query result.");
        return false;
    }
    const auto exactTimes =
        exactUnsignedFields(json, QByteArrayLiteral("queuedAtMs"));
    if (!exactTimes || exactTimes->size() != array.size()) {
        message = QStringLiteral("The steering reply has an inexact queue timestamp.");
        return false;
    }
    std::map<RequestKey, bool> identities;
    entries.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            message = QStringLiteral("The steering reply contains a malformed entry.");
            return false;
        }
        auto decoded = decodeEntry(array.at(index).toObject(), exactTimes->at(index));
        if (!decoded || !identities.emplace(decoded->key, true).second) {
            message = QStringLiteral("The steering reply contains a malformed or duplicate entry.");
            return false;
        }
        entries.push_back(std::move(*decoded));
    }
    return true;
}

std::optional<SteeringModel::DecodedEntry> SteeringModel::decodeEntry(
    const QJsonObject& object,
    const quint64 queuedAtMs) const
{
    const auto steerId =
        requiredString(object, QStringLiteral("steerId"), maximumIdentityScalars);
    const auto accountUserId = requiredString(
        object,
        QStringLiteral("accountUserId"),
        maximumIdentityScalars);
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"), maximumIdentityScalars);
    const auto sessionId =
        requiredString(object, QStringLiteral("sessionId"), maximumIdentityScalars);
    const auto incarnationId = requiredString(
        object,
        QStringLiteral("sessionIncarnationId"),
        maximumIdentityScalars);
    const auto modeText =
        requiredString(object, QStringLiteral("mode"), maximumIdentityScalars);
    const auto deliveryText = requiredString(
        object,
        QStringLiteral("deliveryState"),
        maximumIdentityScalars);
    const auto textValue = object.value(QStringLiteral("text"));
    QString atToolUseId;
    if (!steerId || !accountUserId || !requestId || !sessionId
        || !incarnationId || !modeText || !deliveryText
        || !textValue.isString() || textValue.toString().contains(QChar::Null)
        || textValue.toString().toUtf8().size() > maximumSteerTextBytes
        || !optionalString(
            object,
            QStringLiteral("atToolUseId"),
            atToolUseId,
            maximumIdentityScalars)
        || !isUuidV7(*requestId) || !isUuidV7(*incarnationId)) {
        return std::nullopt;
    }
    const auto mode = decodeMode(*modeText);
    const auto state = deliveryState(*deliveryText);
    if (!mode || !state) {
        return std::nullopt;
    }
    return DecodedEntry {
        .key = {
            .accountUserId = *accountUserId,
            .requestId = *requestId,
            .sessionId = *sessionId,
            .incarnationId = *incarnationId,
        },
        .mode = *mode,
        .text = textValue.toString(),
        .steerId = *steerId,
        .atToolUseId = atToolUseId,
        .queuedAtMs = queuedAtMs,
        .deliveryState = *state,
    };
}

bool SteeringModel::ensureBlockingCapacity(const qsizetype additional)
{
    const auto blocking = std::ranges::count_if(
        m_requests,
        [&](const auto& pair) { return blocks(pair.second); });
    return additional >= 0
        && blocking <= maximumBlockingRequests - additional;
}

bool SteeringModel::blocks(const Request& requestValue) const
{
    if (terminal(requestValue)) {
        return false;
    }
    if (requestValue.runtime) {
        switch (requestValue.runtime->deliveryState) {
        case State::Injected:
        case State::Cancelled:
            return false;
        case State::Preparing:
        case State::Queued:
        case State::DeliveryUnknown:
            return true;
        case State::Sending:
        case State::Reconciling:
        case State::Failed:
            break;
        }
    }
    return requestValue.state == State::Sending
        || requestValue.state == State::Reconciling
        || requestValue.state == State::Preparing
        || requestValue.state == State::Queued
        || requestValue.state == State::DeliveryUnknown;
}

bool SteeringModel::terminal(const Request& requestValue) const
{
    return terminalState(requestValue.state);
}

bool SteeringModel::cancellable(const Request& requestValue) const
{
    return requestValue.runtime
        && !requestValue.runtime->steerId.isEmpty()
        && (requestValue.state == State::Preparing
            || requestValue.state == State::Queued);
}

void SteeringModel::applyReply(
    const QByteArray& json,
    const QJsonObject& object)
{
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"), maximumIdentityScalars);
    if (!requestId) {
        emit decodeError(QStringLiteral("Steering reply has no valid request ID."));
        return;
    }
    auto operationIterator = m_operations.find(*requestId);
    if (operationIterator == m_operations.end()) {
        const auto local = std::ranges::find_if(
            m_requests,
            [&](const auto& pair) {
                return pair.first.requestId == *requestId
                    && pair.second.state == State::Reconciling;
            });
        if (local == m_requests.end()) {
            return;
        }
        Operation late {
            .requestId = *requestId,
            .kind = OperationKind::Send,
            .sessionId = local->first.sessionId,
            .incarnationId = local->first.incarnationId,
            .target = local->first,
            .snapshot = {},
            .hydration = std::nullopt,
        };
        operationIterator = m_operations.insert(*requestId, late);
    }
    const auto operation = *operationIterator;
    const auto current = m_sessions.incarnationForSession(operation.sessionId);
    if (!current || *current != operation.incarnationId) {
        return;
    }
    if (operation.kind == OperationKind::QueryFull) {
        QString ignored;
        const auto hydration =
            currentHydrationKey(operation.sessionId, ignored);
        if (!operation.hydration || hydration != operation.hydration) {
            m_operations.remove(*requestId);
            scheduleTimer();
            return;
        }
    }
    QVector<DecodedEntry> entries;
    QString message;
    const auto payload = object.value(QStringLiteral("payload"));
    const auto expectsArray =
        operation.kind == OperationKind::QueryExact
        || operation.kind == OperationKind::QueryFull;
    if ((expectsArray && !payload.isArray())
        || (!expectsArray && !payload.isObject())) {
        m_operations.remove(*requestId);
        const auto failure =
            QStringLiteral("The steering reply payload has the wrong shape.");
        if (operation.target) {
            if (auto* target = mutableRequest(*operation.target)) {
                markReconciling(*target, failure);
            }
        } else if (operation.hydration) {
            m_hydratedAuthorities.erase(*operation.hydration);
            m_fullQueryRetryAuthorities.insert(*operation.hydration);
        }
        m_errors[operation.sessionId] = failure;
        emit decodeError(failure);
        if ((operation.kind == OperationKind::Send
                || operation.kind == OperationKind::Cancel)
            && operation.target) {
            (void)dispatchExactQuery(*operation.target, failure);
        }
        bumpState();
        return;
    }
    if (!decodeReplyEntries(
            json,
            payload,
            entries,
            message)) {
        m_operations.remove(*requestId);
        if (operation.target) {
            if (auto* target = mutableRequest(*operation.target)) {
                markReconciling(*target, message);
            }
        } else if (operation.hydration) {
            m_hydratedAuthorities.erase(*operation.hydration);
            m_fullQueryRetryAuthorities.insert(*operation.hydration);
        }
        m_errors[operation.sessionId] = message;
        emit decodeError(message);
        if ((operation.kind == OperationKind::Send
                || operation.kind == OperationKind::Cancel)
            && operation.target) {
            (void)dispatchExactQuery(*operation.target, message);
        }
        bumpState();
        return;
    }
    const auto hasInvalidScope =
        std::ranges::any_of(entries, [&](const DecodedEntry& entry) {
            return entry.key.accountUserId != semanticAccountUserId()
                || entry.key.sessionId != operation.sessionId;
        });
    QVector<DecodedEntry> currentIncarnationEntries;
    const QVector<DecodedEntry>* applicableEntries = &entries;
    if (!hasInvalidScope && operation.kind == OperationKind::QueryFull) {
        currentIncarnationEntries.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.key.incarnationId == operation.incarnationId) {
                currentIncarnationEntries.push_back(entry);
            }
        }
        applicableEntries = &currentIncarnationEntries;
    }

    bool applied = false;
    if (!hasInvalidScope) {
        if (operation.kind == OperationKind::Send
            || operation.kind == OperationKind::Cancel) {
            applied = operation.target && applicableEntries->size() == 1
                && applicableEntries->constFirst().key == *operation.target
                && applyEntry(
                    applicableEntries->constFirst(),
                    std::nullopt,
                    {});
        } else {
            applied = applyQuery(operation, *applicableEntries);
        }
    }
    m_operations.remove(*requestId);
    if (!applied) {
        const auto failure =
            QStringLiteral("The steering reply did not match the requested operation.");
        if (operation.target) {
            if (auto* target = mutableRequest(*operation.target)) {
                markReconciling(*target, failure);
            }
        } else if (operation.hydration) {
            m_hydratedAuthorities.erase(*operation.hydration);
            m_fullQueryRetryAuthorities.insert(*operation.hydration);
        }
        m_errors[operation.sessionId] = failure;
        emit decodeError(failure);
        if ((operation.kind == OperationKind::Send
                || operation.kind == OperationKind::Cancel)
            && operation.target) {
            (void)dispatchExactQuery(*operation.target, failure);
        }
    } else if (operation.kind == OperationKind::QueryFull) {
        m_errors.remove(operation.sessionId);
    }
    scheduleTimer();
    bumpState();
}

void SteeringModel::applyRuntimeEvent(
    const QByteArray& json,
    const QJsonObject& object)
{
    const auto entryValue = object.value(QStringLiteral("entry"));
    const auto transitionText =
        requiredString(object, QStringLiteral("transition"), maximumIdentityScalars);
    QString message;
    const auto validMessage = optionalString(
        object,
        QStringLiteral("message"),
        message,
        maximumStatusScalars);
    const auto times =
        exactUnsignedFields(json, QByteArrayLiteral("queuedAtMs"));
    if (!entryValue.isObject() || !transitionText || !validMessage || !times
        || times->size() != 1) {
        emit decodeError(QStringLiteral("Steering transition is malformed."));
        return;
    }
    const auto entry = decodeEntry(entryValue.toObject(), times->constFirst());
    const auto transition = transitionState(*transitionText);
    if (!entry || !transition) {
        emit decodeError(QStringLiteral("Steering transition is malformed."));
        return;
    }
    const auto current = m_sessions.incarnationForSession(entry->key.sessionId);
    if (!current || *current != entry->key.incarnationId
        || entry->key.accountUserId != semanticAccountUserId()) {
        return;
    }
    if (!applyEntry(*entry, transition, std::move(message))) {
        emit decodeError(QStringLiteral("Steering transition conflicts with retained authority."));
        return;
    }
    bumpState();
}

void SteeringModel::applyError(const QJsonObject& object)
{
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"), maximumIdentityScalars);
    const auto message =
        requiredString(object, QStringLiteral("message"), maximumStatusScalars);
    if (!requestId || !message) {
        emit decodeError(QStringLiteral("Steering error event is malformed."));
        return;
    }
    const auto iterator = m_operations.find(*requestId);
    if (iterator == m_operations.end()) {
        return;
    }
    const auto operation = *iterator;
    const auto current = m_sessions.incarnationForSession(operation.sessionId);
    if (!current || *current != operation.incarnationId) {
        return;
    }
    m_operations.erase(iterator);
    handleOperationFailure(operation, *message);
    scheduleTimer();
    bumpState();
}

void SteeringModel::handleOperationFailure(
    Operation operation,
    QString message)
{
    if (operation.kind == OperationKind::QueryFull) {
        if (operation.hydration) {
            m_hydratedAuthorities.erase(*operation.hydration);
            m_fullQueryRetryAuthorities.insert(*operation.hydration);
        }
        m_errors[operation.sessionId] = std::move(message);
        return;
    }
    if (!operation.target) {
        return;
    }
    auto* target = mutableRequest(*operation.target);
    if (target == nullptr) {
        return;
    }
    if (operation.kind == OperationKind::QueryExact) {
        markReconciling(*target, std::move(message));
        m_errors[operation.sessionId] = target->message;
        return;
    }
    markReconciling(
        *target,
        message.isEmpty()
            ? QStringLiteral("The runtime could not confirm this message.")
            : std::move(message));
    m_errors[operation.sessionId] = target->message;
    (void)dispatchExactQuery(
        target->key,
        QStringLiteral("The runtime could not reconcile this message."));
}

void SteeringModel::markFailed(Request& requestValue, QString message)
{
    const auto key = requestValue.key;
    requestValue.state = State::Failed;
    requestValue.runtime.reset();
    requestValue.message = std::move(message);
    requestValue.version = ++m_nextVersion;
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        if (iterator->target && *iterator->target == key) {
            iterator = m_operations.erase(iterator);
        } else {
            ++iterator;
        }
    }
    pruneTerminalHistory();
}

void SteeringModel::markReconciling(Request& requestValue, QString message)
{
    requestValue.state = State::Reconciling;
    requestValue.message = std::move(message);
    requestValue.version = ++m_nextVersion;
}

void SteeringModel::clearMatchingDraft(const Request& requestValue)
{
    if (!requestValue.locallyOwned) {
        return;
    }
    auto found = m_drafts.find(requestValue.key.sessionId);
    if (found == m_drafts.end()
        || found->revision != requestValue.draftRevision
        || found->text != requestValue.submittedDraft
        || found->mode != requestValue.mode) {
        return;
    }
    found->text.clear();
    ++found->revision;
}

void SteeringModel::pruneTerminalHistory()
{
    QVector<RequestKey> terminalKeys;
    for (const auto& [key, requestValue] : m_requests) {
        if (!blocks(requestValue)) {
            terminalKeys.push_back(key);
        }
    }
    if (terminalKeys.size() <= maximumTerminalHistory) {
        return;
    }
    std::ranges::sort(terminalKeys, [&](const RequestKey& left, const RequestKey& right) {
        const auto& lhs = m_requests.at(left);
        const auto& rhs = m_requests.at(right);
        const auto lhsTime = lhs.runtime ? lhs.runtime->queuedAtMs : lhs.order;
        const auto rhsTime = rhs.runtime ? rhs.runtime->queuedAtMs : rhs.order;
        return std::tie(lhsTime, lhs.order) < std::tie(rhsTime, rhs.order);
    });
    const auto excess = terminalKeys.size() - maximumTerminalHistory;
    for (qsizetype index = 0; index < excess; ++index) {
        m_requests.erase(terminalKeys.at(index));
    }
}

void SteeringModel::reconcileCatalog()
{
    bool changed = false;
    for (auto iterator = m_requests.begin(); iterator != m_requests.end();) {
        const auto current =
            m_sessions.incarnationForSession(iterator->first.sessionId);
        if (!current || *current != iterator->first.incarnationId) {
            iterator = m_requests.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_operations.begin(); iterator != m_operations.end();) {
        const auto current =
            m_sessions.incarnationForSession(iterator->sessionId);
        QString ignored;
        const auto hydration = iterator->kind == OperationKind::QueryFull
            ? currentHydrationKey(iterator->sessionId, ignored)
            : std::optional<HydrationKey> {};
        if (!current || *current != iterator->incarnationId
            || (iterator->kind == OperationKind::QueryFull
                && (!iterator->hydration
                    || hydration != iterator->hydration))) {
            iterator = m_operations.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_hydratedAuthorities.begin();
         iterator != m_hydratedAuthorities.end();) {
        QString ignored;
        const auto current = currentHydrationKey(iterator->sessionId, ignored);
        if (!current || *current != *iterator) {
            iterator = m_hydratedAuthorities.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_fullQueryRetryAuthorities.begin();
         iterator != m_fullQueryRetryAuthorities.end();) {
        QString ignored;
        const auto current = currentHydrationKey(iterator->sessionId, ignored);
        if (!current || *current != *iterator) {
            iterator = m_fullQueryRetryAuthorities.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_errors.begin(); iterator != m_errors.end();) {
        if (!m_sessions.containsSession(iterator.key())) {
            iterator = m_errors.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    if (!m_selectedSessionId.isEmpty()) {
        const auto current =
            m_sessions.incarnationForSession(m_selectedSessionId);
        const auto incarnation = current.value_or(QString {});
        if (incarnation != m_selectedIncarnationId) {
            m_selectedIncarnationId = incarnation;
        }
        changed = true;
    }
    scheduleTimer();
    if (changed) {
        bumpState();
    }
    if (!m_selectedSessionId.isEmpty()) {
        QString ignored;
        const auto hydration =
            currentHydrationKey(m_selectedSessionId, ignored);
        const auto active = hydration
            && std::ranges::any_of(
                m_operations,
                [&](const Operation& operation) {
                    return operation.kind == OperationKind::QueryFull
                        && operation.hydration == hydration
                        && operation.deadlineMs > 0;
                });
        if (hydration && !m_hydratedAuthorities.contains(*hydration)
            && !active) {
            (void)dispatchFullQuery(*hydration);
        }
    }
}

void SteeringModel::activateAccount(
    QString userId,
    const quint64 epoch,
    const bool authenticated)
{
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    const auto authenticationChanged = m_authenticated != authenticated;
    const auto accountIdentityChanged =
        !m_hasAccountActivation || m_accountUserId != userId;
    if (activation.changed) {
        m_timer.stop();
        m_requests.clear();
        if (accountIdentityChanged) {
            m_drafts.clear();
        }
        m_errors.clear();
        m_operations.clear();
        m_hydratedAuthorities.clear();
        m_fullQueryRetryAuthorities.clear();
        m_selectedIncarnationId.clear();
        m_accountUserId = std::move(userId);
        m_authenticated = authenticated;
        m_hasAccountActivation = true;
        bumpState();
    } else if (authenticationChanged) {
        for (auto iterator = m_operations.begin();
             iterator != m_operations.end();) {
            if (iterator->kind == OperationKind::QueryFull) {
                iterator = m_operations.erase(iterator);
            } else {
                ++iterator;
            }
        }
        m_hydratedAuthorities.clear();
        m_fullQueryRetryAuthorities.clear();
        m_authenticated = authenticated;
        bumpState();
    }
    for (auto& event : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(event));
    }
    if (!m_selectedSessionId.isEmpty()) {
        (void)rehydrate(m_selectedSessionId);
    }
}

void SteeringModel::scheduleTimer()
{
    qint64 nextDeadline = std::numeric_limits<qint64>::max();
    for (const auto& operation : m_operations) {
        if (operation.deadlineMs > 0) {
            nextDeadline = std::min(nextDeadline, operation.deadlineMs);
        }
    }
    if (nextDeadline == std::numeric_limits<qint64>::max()) {
        m_timer.stop();
        return;
    }
    const auto delay = std::max<qint64>(
        0,
        nextDeadline - QDateTime::currentMSecsSinceEpoch());
    m_timer.start(static_cast<int>(
        std::min<qint64>(delay, std::numeric_limits<int>::max())));
}

void SteeringModel::expireOperations()
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QVector<QString> expired;
    for (auto iterator = m_operations.begin(); iterator != m_operations.end(); ++iterator) {
        if (iterator->deadlineMs > 0 && iterator->deadlineMs <= now) {
            expired.push_back(iterator.key());
        }
    }
    for (const auto& requestId : expired) {
        auto iterator = m_operations.find(requestId);
        if (iterator == m_operations.end()) {
            continue;
        }
        iterator->deadlineMs = 0;
        handleOperationFailure(
            *iterator,
            QStringLiteral("The runtime did not answer before the steering timeout."));
    }
    scheduleTimer();
    if (!expired.isEmpty()) {
        bumpState();
    }
}

void SteeringModel::bumpState()
{
    ++m_stateRevision;
    emit stateChanged();
}

QString SteeringModel::modeName(const Mode mode)
{
    switch (mode) {
    case Mode::Queue: return QStringLiteral("queue");
    case Mode::Steer: return QStringLiteral("steer");
    case Mode::StopAndSend: return QStringLiteral("stopAndSend");
    }
    return {};
}

std::optional<SteeringModel::Mode> SteeringModel::decodeMode(
    const QString& mode)
{
    if (mode == QStringLiteral("queue")) {
        return Mode::Queue;
    }
    if (mode == QStringLiteral("steer")) {
        return Mode::Steer;
    }
    if (mode == QStringLiteral("stopAndSend")) {
        return Mode::StopAndSend;
    }
    return std::nullopt;
}

bool SteeringModel::supportsMode(
    const Mode mode,
    const SessionCatalogModel::ActionContext& context)
{
    switch (mode) {
    case Mode::Queue: return context.canQueue;
    case Mode::Steer: return context.canSteer;
    case Mode::StopAndSend: return context.canStopAndSend;
    }
    return false;
}

std::optional<SteeringModel::Mode> SteeringModel::effectiveMode(
    const Mode preferred,
    const SessionCatalogModel::ActionContext& context)
{
    if (supportsMode(preferred, context)) {
        return preferred;
    }
    if (context.canQueue) {
        return Mode::Queue;
    }
    if (context.canSteer) {
        return Mode::Steer;
    }
    if (context.canStopAndSend) {
        return Mode::StopAndSend;
    }
    return std::nullopt;
}

QString SteeringModel::stateMessage(const Request& requestValue)
{
    if (!requestValue.message.isEmpty()) {
        return requestValue.message;
    }
    switch (requestValue.state) {
    case State::Sending:
        return QStringLiteral("Waiting for the runtime to queue this message…");
    case State::Reconciling:
        return QStringLiteral("Checking whether the runtime admitted this message…");
    case State::Preparing:
        return QStringLiteral("The runtime is preparing this message…");
    case State::Queued:
        return QStringLiteral("Queued by the runtime for the next safe boundary.");
    case State::Injected:
        return QStringLiteral("Injected at the tool boundary.");
    case State::Cancelled:
        return QStringLiteral("Cancelled. Your draft was kept.");
    case State::DeliveryUnknown:
        return QStringLiteral("The runtime cannot prove whether this message was delivered.");
    case State::Failed:
        return QStringLiteral("The message was not admitted. Your draft was kept.");
    }
    return {};
}

bool SteeringModel::isUuidV7(const QString& value)
{
    const QUuid uuid(value);
    return !uuid.isNull() && uuid.version() == QUuid::UnixEpoch
        && uuid.toString(QUuid::WithoutBraces) == value;
}

} // namespace kodosi
