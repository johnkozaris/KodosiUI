#include "models/PendingPermissionsModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
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

std::optional<quint64> unsignedInteger(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto integer = value.toInteger(-1);
    return integer >= 0 ? std::optional<quint64> {static_cast<quint64>(integer)}
                        : std::nullopt;
}

} // namespace

PendingPermissionsModel::PendingPermissionsModel(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    const qint64 deliveryTimeoutMs,
    const qint64 queryTimeoutMs,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_deliveryTimeoutMs(deliveryTimeoutMs)
    , m_queryTimeoutMs(queryTimeoutMs)
{
    Q_ASSERT(deliveryTimeoutMs >= 0);
    Q_ASSERT(queryTimeoutMs >= 0);
    const auto reapply = [this] { reapplyLatestSnapshot(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, reapply);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, reapply);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, reapply);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, reapply);
    m_deliveryTimer.setSingleShot(true);
    connect(&m_deliveryTimer, &QTimer::timeout, this, [this] {
        expireDeliveries();
    });
    m_queryTimer.setSingleShot(true);
    connect(&m_queryTimer, &QTimer::timeout, this, [this] {
        if (m_pendingQueryId.isEmpty()) {
            return;
        }
        m_pendingQueryId.clear();
        settleQueryFailure(
            QStringLiteral(
                "Couldn't load pending approvals. The runtime did not respond."));
    });
}

int PendingPermissionsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_requests.size();
}

QVariant PendingPermissionsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_requests.size()) {
        return {};
    }
    const auto& request = m_requests[index.row()];
    switch (role) {
    case IdentityTokenRole:
        return request.identityToken;
    case SessionIdRole:
        return request.sessionId;
    case SessionIncarnationIdRole:
        return request.sessionIncarnationId;
    case ToolUseIdRole:
        return request.toolUseId;
    case ToolNameRole:
        return request.toolName;
    case ToolInputSummaryRole:
        return request.toolInputSummary;
    case CreatedAtRole:
        return request.createdAt;
    case DeadlineRole:
        return request.deadline;
    case RiskRole:
        return request.risk;
    case DecisionStateRole:
        return QVariant::fromValue(request.decisionState);
    case DecisionMessageRole:
        return request.decisionMessage;
    case ActionableRole:
        return request.authoritativePhase == AuthoritativePhase::Actionable
            && (request.decisionState == DecisionState::Actionable
                || request.decisionState == DecisionState::DeliveryFailed);
    default:
        return {};
    }
}

QHash<int, QByteArray> PendingPermissionsModel::roleNames() const
{
    return {
        {IdentityTokenRole, QByteArrayLiteral("identityToken")},
        {SessionIdRole, QByteArrayLiteral("sessionId")},
        {SessionIncarnationIdRole, QByteArrayLiteral("sessionIncarnationId")},
        {ToolUseIdRole, QByteArrayLiteral("toolUseId")},
        {ToolNameRole, QByteArrayLiteral("toolName")},
        {ToolInputSummaryRole, QByteArrayLiteral("toolInputSummary")},
        {CreatedAtRole, QByteArrayLiteral("createdAt")},
        {DeadlineRole, QByteArrayLiteral("deadline")},
        {RiskRole, QByteArrayLiteral("risk")},
        {DecisionStateRole, QByteArrayLiteral("decisionState")},
        {DecisionMessageRole, QByteArrayLiteral("decisionMessage")},
        {ActionableRole, QByteArrayLiteral("actionable")},
    };
}

PendingPermissionsModel::AuthorityState
PendingPermissionsModel::authorityState() const noexcept
{
    return m_authorityState;
}

QString PendingPermissionsModel::authorityError() const
{
    return m_authorityError;
}

std::optional<PendingPermissionsModel::NotificationRequest>
PendingPermissionsModel::notificationRequest(const QString& token) const
{
    const auto found =
        std::ranges::find(m_requests, token, &Request::identityToken);
    if (found == m_requests.end()) {
        return std::nullopt;
    }
    return NotificationRequest {
        .identityToken = found->identityToken,
        .sessionId = found->sessionId,
        .toolName = found->toolName,
        .toolInputSummary = found->toolInputSummary,
        .risk = found->risk,
        .actionable =
            found->authoritativePhase == AuthoritativePhase::Actionable
            && (found->decisionState == DecisionState::Actionable
                || found->decisionState == DecisionState::DeliveryFailed),
    };
}

int PendingPermissionsModel::rowForIdentityToken(const QString& token) const
{
    const auto found =
        std::ranges::find(m_requests, token, &Request::identityToken);
    return found == m_requests.end()
        ? -1
        : static_cast<int>(std::distance(m_requests.begin(), found));
}

QVariantMap PendingPermissionsModel::presentationForIdentityToken(
    const QString& token) const
{
    const auto found =
        std::ranges::find(m_requests, token, &Request::identityToken);
    return found == m_requests.end()
        ? QVariantMap {}
        : presentation(*found);
}

QVariantMap PendingPermissionsModel::presentationForSession(
    const QString& sessionId) const
{
    const auto actionable = std::ranges::find_if(
        m_requests,
        [&](const Request& request) {
            return request.sessionId == sessionId
                && request.authoritativePhase
                    == AuthoritativePhase::Actionable
                && (request.decisionState == DecisionState::Actionable
                    || request.decisionState
                        == DecisionState::DeliveryFailed);
        });
    if (actionable != m_requests.end()) {
        return presentation(*actionable);
    }
    const auto found =
        std::ranges::find(m_requests, sessionId, &Request::sessionId);
    return found == m_requests.end()
        ? QVariantMap {}
        : presentation(*found);
}

bool PendingPermissionsModel::refresh()
{
    if (!m_authenticated) {
        return false;
    }
    if (!m_pendingQueryId.isEmpty()) {
        return true;
    }
    const auto requestId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_pendingQueryId = requestId;
    if (!m_latestSnapshot) {
        setAuthorityState(AuthorityState::Loading);
    }
    const QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.queryPendingPermissions")},
        {QStringLiteral("requestId"), requestId},
    };
    const auto json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::System, json)) {
        m_queryTimer.start(static_cast<int>(m_queryTimeoutMs));
        return true;
    }
    if (m_pendingQueryId == requestId) {
        m_pendingQueryId.clear();
        m_queryTimer.stop();
        settleQueryFailure(
            QStringLiteral("Couldn't load pending approvals. Try again."));
    }
    return false;
}

bool PendingPermissionsModel::approve(const QString& token)
{
    return decide(token, true, {});
}

bool PendingPermissionsModel::deny(
    const QString& token,
    const QString& reason)
{
    return decide(token, false, reason.trimmed());
}

void PendingPermissionsModel::ingestAuthEvent(QByteArray json)
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
        *epoch,
        type == QStringLiteral("auth.ready"));
}

void PendingPermissionsModel::ingestAgentIntelEvent(QByteArray json)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(QStringLiteral("Agent-intelligence event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(QStringLiteral("Agent-intelligence event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event has no exact account epoch."));
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
            QStringLiteral("Future agent-intelligence event exceeds the ABI frame limit."));
    }
    if (admission != AccountEventAdmission::Current) {
        return;
    }
    applyAgentIntelEvent(object);
}

void PendingPermissionsModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_deliveryTimer.stop();
    m_queryTimer.stop();
    m_latestSnapshot.reset();
    m_snapshotGeneration.reset();
    m_pendingQueryId.clear();
    m_authenticated = false;
    replaceRequests({});
    setAuthorityState(AuthorityState::Loading);
}

void PendingPermissionsModel::activateAccount(
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
        m_deliveryTimer.stop();
        m_queryTimer.stop();
        m_latestSnapshot.reset();
        m_snapshotGeneration.reset();
        m_pendingQueryId.clear();
        m_authenticated = authenticated;
        replaceRequests({});
        setAuthorityState(AuthorityState::Loading);
    } else {
        m_authenticated = authenticated;
    }
    if (activation.changed && authenticated) {
        (void)refresh();
    }
    for (auto& pending : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(pending));
    }
}

void PendingPermissionsModel::applyAgentIntelEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Agent-intelligence event has no type."));
        return;
    }
    if (*type == QStringLiteral("agent.intel.pendingPermissionsSnapshot")) {
        auto snapshot = decodeSnapshot(object);
        if (!snapshot) {
            emit decodeError(QStringLiteral("Pending-approval snapshot is invalid."));
            return;
        }
        if (m_snapshotGeneration
            && snapshot->generation < *m_snapshotGeneration) {
            return;
        }
        m_pendingQueryId.clear();
        m_queryTimer.stop();
        applySnapshot(std::move(*snapshot), false);
        return;
    }
    if (*type == QStringLiteral("agent.intel.reply")) {
        const auto requestId = requiredString(object, QStringLiteral("requestId"));
        if (!requestId || *requestId != m_pendingQueryId) {
            return;
        }
        const auto payload = object.value(QStringLiteral("payload"));
        if (!payload.isObject()) {
            m_pendingQueryId.clear();
            m_queryTimer.stop();
            settleQueryFailure(
                QStringLiteral(
                    "Couldn't load pending approvals. The runtime reply was invalid."));
            emit decodeError(QStringLiteral("Pending-approval reply is invalid."));
            return;
        }
        auto snapshot = decodeSnapshot(payload.toObject());
        if (!snapshot) {
            m_pendingQueryId.clear();
            m_queryTimer.stop();
            settleQueryFailure(
                QStringLiteral("Couldn't load pending approvals. The runtime reply was invalid."));
            emit decodeError(QStringLiteral("Pending-approval reply is invalid."));
            return;
        }
        m_pendingQueryId.clear();
        m_queryTimer.stop();
        applySnapshot(std::move(*snapshot), true);
        return;
    }
    if (*type == QStringLiteral("agent.intel.error")) {
        const auto requestId = requiredString(object, QStringLiteral("requestId"));
        const auto message = requiredString(object, QStringLiteral("message"));
        if (!requestId || !message || *requestId != m_pendingQueryId) {
            return;
        }
        m_pendingQueryId.clear();
        m_queryTimer.stop();
        settleQueryFailure(
            QStringLiteral("Couldn't load pending approvals. ") + *message);
        return;
    }
    if (*type != QStringLiteral("agent.intel.remotePermissionDecisionState")) {
        return;
    }
    const auto sessionId = requiredString(object, QStringLiteral("sessionId"));
    const auto incarnation =
        requiredString(object, QStringLiteral("sessionIncarnationId"));
    const auto toolUseId = requiredString(object, QStringLiteral("toolUseId"));
    const auto generation =
        unsignedInteger(object.value(QStringLiteral("requestGeneration")));
    const auto phase = requiredString(object, QStringLiteral("phase"));
    if (!sessionId || !incarnation || !toolUseId || !generation
        || *generation == 0 || !phase) {
        emit decodeError(QStringLiteral("Remote approval state is invalid."));
        return;
    }
    const auto found = std::ranges::find_if(m_requests, [&](const Request& request) {
        return request.sessionId == *sessionId
            && request.sessionIncarnationId == *incarnation
            && request.toolUseId == *toolUseId
            && request.requestGeneration == *generation;
    });
    if (found == m_requests.end()
        || (found->authoritativePhase == AuthoritativePhase::Actionable
            && found->decisionState == DecisionState::Actionable)) {
        return;
    }
    const auto statusValue = object.value(QStringLiteral("status"));
    const auto messageValue = object.value(QStringLiteral("message"));
    if ((!statusValue.isUndefined() && !statusValue.isNull()
            && !statusValue.isString())
        || (!messageValue.isUndefined() && !messageValue.isNull()
            && !messageValue.isString())) {
        emit decodeError(QStringLiteral("Remote approval state is invalid."));
        return;
    }
    if (*phase == QStringLiteral("pending")) {
        return;
    }
    if (*phase != QStringLiteral("sending")
        && *phase != QStringLiteral("resolved")
        && *phase != QStringLiteral("failed")) {
        found->decisionState = DecisionState::DeliveryUnknown;
        found->decisionMessage =
            QStringLiteral("Unknown remote approval state. Waiting for the runtime snapshot.");
    } else if (*phase == QStringLiteral("failed")) {
        found->authoritativePhase = AuthoritativePhase::Actionable;
        found->decisionState = DecisionState::DeliveryFailed;
        found->decisionMessage = optionalString(object, QStringLiteral("message"));
        if (found->decisionMessage.isEmpty()) {
            found->decisionMessage =
                QStringLiteral("The remote approval failed. You can try again.");
        }
        if (m_latestSnapshot) {
            const auto cached = std::ranges::find_if(
                m_latestSnapshot->requests,
                [&](const Request& request) {
                    return request.identityToken == found->identityToken;
                });
            if (cached != m_latestSnapshot->requests.end()) {
                cached->authoritativePhase = AuthoritativePhase::Actionable;
                cached->decisionState = DecisionState::DeliveryFailed;
                cached->decisionMessage = found->decisionMessage;
                cached->deliveryDeadline = {};
            }
        }
    } else if (*phase == QStringLiteral("sending")) {
        found->decisionMessage = optionalString(object, QStringLiteral("message"));
    }
    found->deliveryDeadline = {};
    const auto row = static_cast<int>(std::distance(m_requests.begin(), found));
    emit dataChanged(
        index(row),
        index(row),
        {DecisionStateRole, DecisionMessageRole, ActionableRole});
    scheduleDeliveryTimeout();
    if (*phase != QStringLiteral("pending")) {
        (void)refresh();
    }
}

void PendingPermissionsModel::applySnapshot(
    Snapshot snapshot,
    const bool authoritativeRefresh)
{
    if (m_snapshotGeneration && snapshot.generation < *m_snapshotGeneration) {
        return;
    }
    m_snapshotGeneration = snapshot.generation;
    m_latestSnapshot = snapshot;

    QVector<Request> desired;
    bool awaitsSessionIdentity = false;
    for (auto request : snapshot.requests) {
        if (request.requestGeneration == 0
            || request.sessionIncarnationId.isEmpty()) {
            continue;
        }
        const auto current =
            m_sessions.incarnationForSession(request.sessionId);
        if (!current) {
            awaitsSessionIdentity = true;
            continue;
        }
        if (*current != request.sessionIncarnationId) {
            continue;
        }
        const auto prior =
            std::ranges::find(m_requests, request.identityToken, &Request::identityToken);
        if (prior != m_requests.end()) {
            if (request.authoritativePhase != AuthoritativePhase::Actionable
                || prior->authoritativePhase == AuthoritativePhase::Actionable) {
                request.decisionMessage = prior->decisionMessage;
            }
            if (prior->authoritativePhase == request.authoritativePhase
                && !authoritativeRefresh) {
                request.decisionState = prior->decisionState;
            }
            if (request.authoritativePhase == AuthoritativePhase::Sending) {
                request.deliveryDeadline = prior->deliveryDeadline;
            }
        } else if (request.authoritativePhase == AuthoritativePhase::Sending) {
            request.decisionState = DecisionState::SendingApprove;
        }
        desired.push_back(std::move(request));
    }
    replaceRequests(std::move(desired));
    setAuthorityState(
        awaitsSessionIdentity ? AuthorityState::Loading : AuthorityState::Loaded);
    scheduleDeliveryTimeout();
}

void PendingPermissionsModel::reapplyLatestSnapshot()
{
    if (m_latestSnapshot) {
        applySnapshot(*m_latestSnapshot, false);
    }
}

void PendingPermissionsModel::replaceRequests(QVector<Request> requests)
{
    std::ranges::sort(requests, [](const Request& lhs, const Request& rhs) {
        return lhs.createdAt == rhs.createdAt
            ? lhs.identityToken < rhs.identityToken
            : lhs.createdAt < rhs.createdAt;
    });
    QVector<QString> removed;
    QVector<QString> added;
    QSet<QString> priorIds;
    QSet<QString> desiredIds;
    for (const auto& request : m_requests) {
        priorIds.insert(request.identityToken);
    }
    for (const auto& request : requests) {
        desiredIds.insert(request.identityToken);
    }
    for (const auto& request : m_requests) {
        if (!desiredIds.contains(request.identityToken)) {
            removed.push_back(request.identityToken);
        }
    }
    for (const auto& request : requests) {
        if (!priorIds.contains(request.identityToken)) {
            added.push_back(request.identityToken);
        }
    }
    beginResetModel();
    const auto changed = m_requests.size() != requests.size();
    m_requests = std::move(requests);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
    for (const auto& token : removed) {
        emit requestRemoved(token);
    }
    for (const auto& token : added) {
        emit requestAdded(token);
    }
}

void PendingPermissionsModel::setAuthorityState(
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

void PendingPermissionsModel::settleQueryFailure(QString error)
{
    if (m_latestSnapshot) {
        applySnapshot(*m_latestSnapshot, false);
        return;
    }
    setAuthorityState(AuthorityState::AuthorityFailed, std::move(error));
}

bool PendingPermissionsModel::decide(
    const QString& token,
    const bool approveDecision,
    const QString& reason)
{
    auto* request = findRequest(token);
    if (request == nullptr
        || request->authoritativePhase != AuthoritativePhase::Actionable
        || (request->decisionState != DecisionState::Actionable
            && request->decisionState != DecisionState::DeliveryFailed)
        || m_sessions.incarnationForSession(request->sessionId)
            != std::optional<QString> {request->sessionIncarnationId}) {
        return false;
    }
    request->decisionState = approveDecision
        ? DecisionState::SendingApprove
        : DecisionState::SendingDeny;
    request->decisionMessage.clear();
    request->deliveryDeadline =
        QDateTime::currentDateTimeUtc().addMSecs(m_deliveryTimeoutMs);
    const auto row = static_cast<int>(request - m_requests.data());
    emit dataChanged(
        index(row),
        index(row),
        {DecisionStateRole, DecisionMessageRole, ActionableRole});
    scheduleDeliveryTimeout();

    QByteArray command = QByteArrayLiteral("{\"type\":");
    command += quoted(
        approveDecision
            ? QStringLiteral("agent.intel.allowPendingPermissionRequest")
            : QStringLiteral("agent.intel.denyPendingPermissionRequest"));
    command += QByteArrayLiteral(",\"sessionId\":");
    command += quoted(request->sessionId);
    command += QByteArrayLiteral(",\"sessionIncarnationId\":");
    command += quoted(request->sessionIncarnationId);
    command += QByteArrayLiteral(",\"toolUseId\":");
    command += quoted(request->toolUseId);
    command += QByteArrayLiteral(",\"requestGeneration\":");
    command += QByteArray::number(request->requestGeneration);
    if (!approveDecision && !reason.isEmpty()) {
        command += QByteArrayLiteral(",\"reason\":");
        command += quoted(reason);
    }
    command += '}';
    if (m_dispatcher.send(CommandLane::System, command)) {
        return true;
    }
    request = findRequest(token);
    if (request != nullptr) {
        request->decisionState = DecisionState::DeliveryFailed;
        request->decisionMessage =
            QStringLiteral("The approval could not be delivered. You can try again.");
        request->deliveryDeadline = {};
        const auto failedRow = static_cast<int>(request - m_requests.data());
        emit dataChanged(
            index(failedRow),
            index(failedRow),
            {DecisionStateRole, DecisionMessageRole, ActionableRole});
        scheduleDeliveryTimeout();
    }
    return false;
}

PendingPermissionsModel::Request* PendingPermissionsModel::findRequest(
    const QString& token)
{
    const auto found =
        std::ranges::find(m_requests, token, &Request::identityToken);
    return found == m_requests.end() ? nullptr : &*found;
}

std::optional<PendingPermissionsModel::Snapshot>
PendingPermissionsModel::decodeSnapshot(const QJsonObject& object)
{
    const auto generation =
        unsignedInteger(object.value(QStringLiteral("generation")));
    const auto values = object.value(QStringLiteral("requests"));
    if (!generation || !values.isArray()) {
        return std::nullopt;
    }
    QVector<Request> requests;
    QSet<QString> identities;
    for (const auto& value : values.toArray()) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        auto request = decodeRequest(value.toObject());
        if (!request) {
            return std::nullopt;
        }
        if (identities.contains(request->identityToken)) {
            return std::nullopt;
        }
        identities.insert(request->identityToken);
        requests.push_back(std::move(*request));
    }
    return Snapshot {
        .generation = *generation,
        .requests = std::move(requests),
    };
}

std::optional<PendingPermissionsModel::Request>
PendingPermissionsModel::decodeRequest(const QJsonObject& object)
{
    const auto sessionId = requiredString(object, QStringLiteral("sessionId"));
    const auto incarnation =
        requiredString(object, QStringLiteral("sessionIncarnationId"));
    const auto requestGeneration =
        unsignedInteger(object.value(QStringLiteral("requestGeneration")));
    const auto toolUseId = requiredString(object, QStringLiteral("toolUseId"));
    const auto toolName = requiredString(object, QStringLiteral("toolName"));
    const auto createdAt =
        unsignedInteger(object.value(QStringLiteral("createdAtMs")));
    const auto deadline =
        unsignedInteger(object.value(QStringLiteral("deadlineAtMs")));
    const auto rawRisk = requiredString(object, QStringLiteral("risk"));
    const auto rawPhase = requiredString(object, QStringLiteral("decisionPhase"));
    const auto toolInput = object.value(QStringLiteral("toolInput"));
    if (!sessionId || !incarnation || !requestGeneration
        || *requestGeneration == 0 || !toolUseId
        || !toolName || !createdAt || !deadline || !rawRisk || !rawPhase
        || toolInput.isUndefined()) {
        return std::nullopt;
    }
    const auto phase = *rawPhase == QStringLiteral("actionable")
        ? std::optional<AuthoritativePhase> {AuthoritativePhase::Actionable}
        : *rawPhase == QStringLiteral("sending")
        ? std::optional<AuthoritativePhase> {AuthoritativePhase::Sending}
        : std::nullopt;
    if (!phase
        || *createdAt > static_cast<quint64>(std::numeric_limits<qint64>::max())
        || *deadline > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    const QSet<QString> knownRisks {
        QStringLiteral("destructive"),
        QStringLiteral("credential"),
        QStringLiteral("network"),
        QStringLiteral("safe"),
        QStringLiteral("unknown"),
    };
    Request request {
        .identityToken = {},
        .sessionId = *sessionId,
        .sessionIncarnationId = *incarnation,
        .toolUseId = *toolUseId,
        .toolName = *toolName,
        .toolInputSummary = inputSummary(toolInput),
        .createdAt = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(*createdAt),
            QTimeZone::UTC),
        .deadline = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(*deadline),
            QTimeZone::UTC),
        .risk = knownRisks.contains(*rawRisk)
            ? *rawRisk
            : QStringLiteral("unknown"),
        .decisionMessage = {},
        .requestGeneration = *requestGeneration,
        .authoritativePhase = *phase,
        .decisionState = *phase == AuthoritativePhase::Actionable
            ? DecisionState::Actionable
            : DecisionState::SendingApprove,
        .deliveryDeadline = {},
    };
    request.identityToken = identityToken(request);
    return request;
}

QString PendingPermissionsModel::identityToken(const Request& request)
{
    const QJsonObject identity {
        {QStringLiteral("sessionId"), request.sessionId},
        {QStringLiteral("sessionIncarnationId"), request.sessionIncarnationId},
        {QStringLiteral("toolUseId"), request.toolUseId},
        {QStringLiteral("requestGeneration"),
         QString::number(request.requestGeneration)},
    };
    return QString::fromLatin1(
        QJsonDocument(identity)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString PendingPermissionsModel::inputSummary(const QJsonValue& value)
{
    if (!value.isObject()) {
        return {};
    }
    const auto object = value.toObject();
    for (const auto& key : {
             QStringLiteral("command"),
             QStringLiteral("path"),
             QStringLiteral("query"),
             QStringLiteral("prompt"),
             QStringLiteral("pattern"),
         }) {
        const auto candidate = object.value(key);
        if (candidate.isString() && !candidate.toString().isEmpty()) {
            return candidate.toString();
        }
    }
    return {};
}

QByteArray PendingPermissionsModel::quoted(const QString& value)
{
    auto json = QJsonDocument(QJsonArray {value}).toJson(QJsonDocument::Compact);
    return json.sliced(1, json.size() - 2);
}

QVariantMap PendingPermissionsModel::presentation(const Request& request)
{
    const auto actionable =
        request.authoritativePhase == AuthoritativePhase::Actionable
        && (request.decisionState == DecisionState::Actionable
            || request.decisionState == DecisionState::DeliveryFailed);
    return {
        {QStringLiteral("identityToken"), request.identityToken},
        {QStringLiteral("sessionId"), request.sessionId},
        {QStringLiteral("toolName"), request.toolName},
        {QStringLiteral("toolInputSummary"), request.toolInputSummary},
        {QStringLiteral("risk"), request.risk},
        {QStringLiteral("decisionMessage"), request.decisionMessage},
        {QStringLiteral("actionable"), actionable},
    };
}

void PendingPermissionsModel::scheduleDeliveryTimeout()
{
    std::optional<QDateTime> nearest;
    for (const auto& request : m_requests) {
        if (!request.deliveryDeadline.isValid()) {
            continue;
        }
        if (!nearest || request.deliveryDeadline < *nearest) {
            nearest = request.deliveryDeadline;
        }
    }
    if (!nearest) {
        m_deliveryTimer.stop();
        return;
    }
    const auto delay = std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().msecsTo(*nearest),
        0,
        std::numeric_limits<int>::max());
    m_deliveryTimer.start(static_cast<int>(delay));
}

void PendingPermissionsModel::expireDeliveries()
{
    const auto now = QDateTime::currentDateTimeUtc();
    bool recoveryNeeded = false;
    for (auto row = 0; row < m_requests.size(); ++row) {
        auto& request = m_requests[row];
        if (!request.deliveryDeadline.isValid()
            || request.deliveryDeadline > now) {
            continue;
        }
        request.deliveryDeadline = {};
        request.decisionState = DecisionState::DeliveryUnknown;
        request.decisionMessage = QStringLiteral(
            "The approval result did not arrive. Waiting for the runtime snapshot.");
        emit dataChanged(
            index(row),
            index(row),
            {DecisionStateRole, DecisionMessageRole, ActionableRole});
        recoveryNeeded = true;
    }
    scheduleDeliveryTimeout();
    if (recoveryNeeded) {
        (void)refresh();
    }
}

} // namespace kodosi
