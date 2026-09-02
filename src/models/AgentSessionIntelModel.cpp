#include "models/AgentSessionIntelModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identityTextLimit = 1'024;
constexpr qsizetype summaryTextLimit = 240;

bool withinScalarLimit(
    const QStringView value,
    const qsizetype maximum)
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
    const qsizetype maximum = identityTextLimit)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    if (text.isEmpty() || !withinScalarLimit(text, maximum)
        || text.contains(QChar::Null)) {
        return std::nullopt;
    }
    return text;
}

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    QString& destination,
    const qsizetype maximum = identityTextLimit)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    if (!withinScalarLimit(text, maximum)
        || text.contains(QChar::Null)) {
        return false;
    }
    destination = text;
    return true;
}

std::optional<quint32> unsigned32(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto integer = value.toInteger(-1);
    if (integer < 0
        || static_cast<quint64>(integer)
            > std::numeric_limits<quint32>::max()) {
        return std::nullopt;
    }
    return static_cast<quint32>(integer);
}

bool nullableUnsigned32(
    const QJsonObject& object,
    const QString& key,
    quint32& destination,
    bool& present)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination = 0;
        present = false;
        return true;
    }
    const auto decoded = unsigned32(value);
    if (!decoded) {
        return false;
    }
    destination = *decoded;
    present = true;
    return true;
}

bool known(const QString& value, const std::initializer_list<QString> values)
{
    return std::ranges::find(values, value) != values.end();
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid uuid(value);
    return !uuid.isNull()
        && uuid.toString(QUuid::WithoutBraces) == value
        && value.size() == 36 && value.at(14) == QLatin1Char('7');
}

bool nullableObject(
    const QJsonObject& object,
    const QString& key,
    std::optional<QJsonObject>& destination)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination.reset();
        return true;
    }
    if (!value.isObject()) {
        return false;
    }
    destination = value.toObject();
    return true;
}

bool nullableAccountId(
    const QJsonObject& object,
    const QString& key,
    QString& destination)
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
    if (text.isEmpty() || !withinScalarLimit(text, identityTextLimit)
        || text.contains(QChar::Null)) {
        return false;
    }
    destination = text;
    return true;
}

} // namespace

AgentSessionIntelModel::AgentSessionIntelModel(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    const qint64 queryTimeoutMs,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_queryTimeoutMs(queryTimeoutMs)
{
    Q_ASSERT(queryTimeoutMs >= 0);
    const auto reapply = [this] { reapplyLatestSet(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, reapply);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, reapply);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, reapply);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, reapply);
    m_queryTimer.setSingleShot(true);
    connect(&m_queryTimer, &QTimer::timeout, this, [this] {
        if (m_pendingRequestId.isEmpty()) {
            return;
        }
        m_pendingRequestId.clear();
        setHydrationState(
            HydrationState::Failed,
            QStringLiteral(
                "Couldn't load live agent intelligence. The runtime did not respond."));
        scheduleRecovery();
    });
    m_recoveryTimer.setSingleShot(true);
    connect(&m_recoveryTimer, &QTimer::timeout, this, [this] {
        (void)refresh();
    });
}

int AgentSessionIntelModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant AgentSessionIntelModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case SessionIdRole: return entry.sessionId;
    case AgentTypeRole: return entry.agentType;
    case VersionRole: return entry.version;
    case ModelRole: return entry.model;
    case TitleRole: return entry.title;
    case CwdRole: return entry.cwd;
    case LifecycleRole: return entry.lifecycle;
    case HasAttentionRole: return entry.hasAttention;
    case AttentionKindRole: return entry.attentionKind;
    case AttentionSummaryRole: return entry.attentionSummary;
    case AttentionActionableRole: return entry.attentionActionable;
    case HasPendingInteractionRole: return entry.hasPendingInteraction;
    case PendingInteractionKindRole: return entry.pendingInteractionKind;
    case PendingInteractionSummaryRole: return entry.pendingInteractionSummary;
    case PendingToolNameRole: return entry.pendingToolName;
    case CanApproveRole: return entry.canApprove;
    case CanDenyRole: return entry.canDeny;
    case CanAnswerRole: return entry.canAnswer;
    case CanFocusRole: return entry.canFocus;
    case CurrentActivityRole: return entry.currentActivity;
    case LastProgressAtRole: return entry.lastProgressAt;
    case ActiveWorkersRole: return entry.activeWorkers;
    case BlockedWorkersRole: return entry.blockedWorkers;
    case FailedWorkersRole: return entry.failedWorkers;
    case CompletedWorkersRole: return entry.completedWorkers;
    case HasOutcomeRole: return entry.hasOutcome;
    case OutcomeKindRole: return entry.outcomeKind;
    case OutcomeSummaryRole: return entry.outcomeSummary;
    case HasExceptionalStateRole: return entry.hasExceptionalState;
    case ExceptionalKindRole: return entry.exceptionalKind;
    case ExceptionalSummaryRole: return entry.exceptionalSummary;
    case ExceptionalRetryableRole: return entry.exceptionalRetryable;
    case SourceKindRole: return entry.sourceKind;
    case SourceDegradedRole: return entry.sourceDegraded;
    case SourceDetailRole: return entry.sourceDetail;
    default: return {};
    }
}

QHash<int, QByteArray> AgentSessionIntelModel::roleNames() const
{
    return {
        {SessionIdRole, QByteArrayLiteral("sessionId")},
        {AgentTypeRole, QByteArrayLiteral("agentType")},
        {VersionRole, QByteArrayLiteral("version")},
        {ModelRole, QByteArrayLiteral("model")},
        {TitleRole, QByteArrayLiteral("title")},
        {CwdRole, QByteArrayLiteral("cwd")},
        {LifecycleRole, QByteArrayLiteral("lifecycle")},
        {HasAttentionRole, QByteArrayLiteral("hasAttention")},
        {AttentionKindRole, QByteArrayLiteral("attentionKind")},
        {AttentionSummaryRole, QByteArrayLiteral("attentionSummary")},
        {AttentionActionableRole, QByteArrayLiteral("attentionActionable")},
        {HasPendingInteractionRole, QByteArrayLiteral("hasPendingInteraction")},
        {PendingInteractionKindRole, QByteArrayLiteral("pendingInteractionKind")},
        {PendingInteractionSummaryRole, QByteArrayLiteral("pendingInteractionSummary")},
        {PendingToolNameRole, QByteArrayLiteral("pendingToolName")},
        {CanApproveRole, QByteArrayLiteral("canApprove")},
        {CanDenyRole, QByteArrayLiteral("canDeny")},
        {CanAnswerRole, QByteArrayLiteral("canAnswer")},
        {CanFocusRole, QByteArrayLiteral("canFocus")},
        {CurrentActivityRole, QByteArrayLiteral("currentActivity")},
        {LastProgressAtRole, QByteArrayLiteral("lastProgressAt")},
        {ActiveWorkersRole, QByteArrayLiteral("activeWorkers")},
        {BlockedWorkersRole, QByteArrayLiteral("blockedWorkers")},
        {FailedWorkersRole, QByteArrayLiteral("failedWorkers")},
        {CompletedWorkersRole, QByteArrayLiteral("completedWorkers")},
        {HasOutcomeRole, QByteArrayLiteral("hasOutcome")},
        {OutcomeKindRole, QByteArrayLiteral("outcomeKind")},
        {OutcomeSummaryRole, QByteArrayLiteral("outcomeSummary")},
        {HasExceptionalStateRole, QByteArrayLiteral("hasExceptionalState")},
        {ExceptionalKindRole, QByteArrayLiteral("exceptionalKind")},
        {ExceptionalSummaryRole, QByteArrayLiteral("exceptionalSummary")},
        {ExceptionalRetryableRole, QByteArrayLiteral("exceptionalRetryable")},
        {SourceKindRole, QByteArrayLiteral("sourceKind")},
        {SourceDegradedRole, QByteArrayLiteral("sourceDegraded")},
        {SourceDetailRole, QByteArrayLiteral("sourceDetail")},
    };
}

AgentSessionIntelModel::HydrationState
AgentSessionIntelModel::hydrationState() const noexcept
{
    return m_hydrationState;
}

QString AgentSessionIntelModel::hydrationError() const
{
    return m_hydrationError;
}

bool AgentSessionIntelModel::refresh()
{
    if (!m_hasAccountContext) {
        return false;
    }
    if (!m_pendingRequestId.isEmpty()) {
        return true;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_recoveryTimer.stop();
    m_pendingRequestId = requestId;
    if (m_authorityIncarnationId.isEmpty()) {
        setHydrationState(HydrationState::Loading);
    }
    const QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.queryLiveSet")},
        {QStringLiteral("requestId"), requestId},
    };
    const auto json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::System, json)) {
        m_queryTimer.start(static_cast<int>(m_queryTimeoutMs));
        return true;
    }
    if (m_pendingRequestId == requestId) {
        m_pendingRequestId.clear();
        setHydrationState(
            HydrationState::Failed,
            QStringLiteral(
                "Couldn't request live agent intelligence. Try again."));
        scheduleRecovery();
    }
    return false;
}

QVariantMap AgentSessionIntelModel::presentationForSession(
    const QString& sessionId) const
{
    const auto found =
        std::ranges::find(m_entries, sessionId, &Entry::sessionId);
    return found == m_entries.end() ? QVariantMap {} : presentation(*found);
}

std::optional<QString> AgentSessionIntelModel::conversationAgent(
    const QString& sessionId,
    const QString& sessionIncarnationId) const
{
    const auto found =
        std::ranges::find(m_entries, sessionId, &Entry::sessionId);
    if (found == m_entries.end()
        || found->sessionIncarnationId != sessionIncarnationId
        || (found->agentType != QStringLiteral("claude")
            && found->agentType != QStringLiteral("copilot"))) {
        return std::nullopt;
    }
    return found->agentType;
}

std::optional<AgentSessionIntelModel::ProjectMemoryContext>
AgentSessionIntelModel::projectMemoryContext(
    const QString& sessionId,
    const QString& sessionIncarnationId) const
{
    const auto found =
        std::ranges::find(m_entries, sessionId, &Entry::sessionId);
    if (found == m_entries.end()
        || found->sessionIncarnationId != sessionIncarnationId
        || found->agentType.isEmpty() || found->cwd.isEmpty()) {
        return std::nullopt;
    }
    return ProjectMemoryContext {
        .agent = found->agentType,
        .workingDirectory = found->cwd,
    };
}

void AgentSessionIntelModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString userId;
    const auto validUserId = type == QStringLiteral("auth.required")
        || nullableAccountId(
            object,
            QStringLiteral("userId"),
            userId);
    if (!epoch || !validUserId) {
        emit decodeError(
            QStringLiteral("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void AgentSessionIntelModel::ingestAgentIntelEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event lacks account authority."));
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString accountUserId;
    if (!epoch
        || !nullableAccountId(
            object,
            QStringLiteral("accountUserId"),
            accountUserId)) {
        emit decodeError(
            QStringLiteral("Agent-intelligence event has malformed account authority."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = std::move(accountUserId),
            .epoch = *epoch,
        },
        json);
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(
            QStringLiteral("Future live Agent Intel set exceeds the ABI frame limit."));
    }
    if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json, object);
    }
}

void AgentSessionIntelModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_hasAccountContext = false;
    m_queryTimer.stop();
    m_recoveryTimer.stop();
    m_pendingRequestId.clear();
    m_recoveryStep = 0;
    resetAuthority(true);
    setHydrationState(HydrationState::Loading);
}

void AgentSessionIntelModel::reapplyLatestSet()
{
    if (m_latestSet) {
        applyEstablished(*m_latestSet);
    }
}

void AgentSessionIntelModel::activateAccount(
    QString userId,
    const quint64 epoch)
{
    auto activation = m_accountFence.activate({
        .userId = std::move(userId),
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    m_hasAccountContext = true;
    if (activation.changed) {
        m_queryTimer.stop();
        m_recoveryTimer.stop();
        m_pendingRequestId.clear();
        m_recoveryStep = 0;
        resetAuthority(true);
        setHydrationState(HydrationState::Loading);
    }
    for (auto& pending : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(pending));
    }
    if (activation.changed) {
        (void)refresh();
    }
}

void AgentSessionIntelModel::applyAgentIntelEvent(
    const QByteArray& json,
    const QJsonObject& object)
{
    if (object.value(QStringLiteral("type")).toString()
        != QStringLiteral("agent.intel.liveSet")) {
        return;
    }
    auto set = decodeLiveSet(json, object);
    if (!set) {
        protocolFault(
            QStringLiteral("Live Agent Intel authority is malformed."));
        return;
    }
    applyLiveSet(std::move(*set));
}

void AgentSessionIntelModel::applyLiveSet(LiveSet set)
{
    if (m_authorityIncarnationId.isEmpty()) {
        if (!set.requestId.isEmpty()
            && set.requestId == m_pendingRequestId) {
            const auto authority = set.authorityIncarnationId;
            const auto revision = set.revision;
            establish(std::move(set));
            if (m_stagedSet
                && m_stagedSet->authorityIncarnationId == authority
                && m_stagedSet->revision > revision) {
                auto staged = std::move(*m_stagedSet);
                m_stagedSet.reset();
                applyEstablished(std::move(staged));
            } else if (m_stagedSet
                && m_stagedSet->authorityIncarnationId == authority
                && m_stagedSet->revision == revision
                && m_latestSet
                && m_stagedSet->entries != m_latestSet->entries) {
                m_stagedSet.reset();
                protocolFault(
                    QStringLiteral(
                        "Staged live Agent Intel content conflicts with its baseline revision."));
                return;
            } else {
                m_stagedSet.reset();
            }
            settleQuery();
        } else if (set.requestId.isEmpty()) {
            if (m_stagedSet
                && m_stagedSet->authorityIncarnationId
                    == set.authorityIncarnationId) {
                if (set.revision < m_stagedSet->revision) {
                    return;
                }
                if (set.revision == m_stagedSet->revision
                    && set.entries != m_stagedSet->entries) {
                    protocolFault(
                        QStringLiteral(
                            "Staged live Agent Intel content changed without a new revision."));
                    return;
                }
            }
            m_stagedSet = std::move(set);
        }
        return;
    }

    if (set.authorityIncarnationId != m_authorityIncarnationId) {
        if (!set.requestId.isEmpty()
            && set.requestId == m_pendingRequestId) {
            resetAuthority(true);
            establish(std::move(set));
            settleQuery();
            return;
        }
        m_queryTimer.stop();
        m_pendingRequestId.clear();
        resetAuthority(true);
        if (set.requestId.isEmpty()) {
            m_stagedSet = std::move(set);
        }
        (void)refresh();
        return;
    }

    if (!set.requestId.isEmpty()
        && set.requestId == m_pendingRequestId) {
        settleQuery();
    }
    if (!m_revision || set.revision > *m_revision) {
        applyEstablished(std::move(set));
        return;
    }
    if (set.revision == *m_revision && m_latestSet
        && set.entries != m_latestSet->entries) {
        protocolFault(
            QStringLiteral(
                "Live Agent Intel content changed without a new revision."));
    }
}

void AgentSessionIntelModel::establish(LiveSet set)
{
    m_authorityIncarnationId = set.authorityIncarnationId;
    applyEstablished(std::move(set));
}

void AgentSessionIntelModel::applyEstablished(LiveSet set)
{
    m_revision = set.revision;
    m_latestSet = set;
    QVector<Entry> current;
    current.reserve(set.entries.size());
    for (auto& entry : set.entries) {
        const auto incarnation =
            m_sessions.incarnationForSession(entry.sessionId);
        if (incarnation && *incarnation == entry.sessionIncarnationId) {
            current.push_back(std::move(entry));
        }
    }
    replaceEntries(std::move(current));
    emit authorityRevisionChanged();
}

void AgentSessionIntelModel::replaceEntries(QVector<Entry> entries)
{
    std::ranges::sort(entries, {}, &Entry::sessionId);
    const auto countChangedValue = entries.size() != m_entries.size();
    if (entries == m_entries) {
        return;
    }
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

void AgentSessionIntelModel::settleQuery()
{
    m_pendingRequestId.clear();
    m_queryTimer.stop();
    m_recoveryTimer.stop();
    m_recoveryStep = 0;
    setHydrationState(HydrationState::Current);
}

void AgentSessionIntelModel::resetAuthority(const bool clearProjection)
{
    m_authorityIncarnationId.clear();
    m_revision.reset();
    m_latestSet.reset();
    m_stagedSet.reset();
    if (clearProjection) {
        replaceEntries({});
    }
}

void AgentSessionIntelModel::protocolFault(QString message)
{
    emit decodeError(message);
    m_queryTimer.stop();
    m_pendingRequestId.clear();
    resetAuthority(true);
    setHydrationState(HydrationState::Failed, std::move(message));
    scheduleRecovery();
}

void AgentSessionIntelModel::scheduleRecovery()
{
    if (!m_hasAccountContext || m_recoveryTimer.isActive()) {
        return;
    }
    constexpr std::array delays {1'000, 2'000, 5'000, 10'000, 15'000};
    const auto index = std::min<std::size_t>(
        m_recoveryStep,
        delays.size() - 1);
    m_recoveryStep = static_cast<std::uint8_t>(
        std::min<std::size_t>(index + 1, delays.size() - 1));
    m_recoveryTimer.start(delays.at(index));
}

void AgentSessionIntelModel::setHydrationState(
    const HydrationState state,
    QString error)
{
    if (m_hydrationState == state && m_hydrationError == error) {
        return;
    }
    m_hydrationState = state;
    m_hydrationError = std::move(error);
    emit hydrationStateChanged();
}

std::optional<AgentSessionIntelModel::LiveSet>
AgentSessionIntelModel::decodeLiveSet(
    const QByteArray& json,
    const QJsonObject& object)
{
    if (!object.contains(QStringLiteral("requestId"))) {
        return std::nullopt;
    }
    const auto requestValue = object.value(QStringLiteral("requestId"));
    const auto authority =
        requiredString(object, QStringLiteral("authorityIncarnationId"));
    const auto revision =
        exactUnsignedJsonField(json, QByteArrayLiteral("revision"));
    const auto entriesValue = object.value(QStringLiteral("entries"));
    if ((!requestValue.isNull() && !requestValue.isString())
        || (requestValue.isString()
            && !canonicalUuidV7(requestValue.toString()))
        || !authority || !canonicalUuidV7(*authority)
        || !revision || !entriesValue.isArray()) {
        return std::nullopt;
    }

    QVector<Entry> entries;
    QSet<QString> sessionIds;
    for (const auto& value : entriesValue.toArray()) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        auto entry = decodeEntry(value.toObject());
        if (!entry || sessionIds.contains(entry->sessionId)) {
            return std::nullopt;
        }
        sessionIds.insert(entry->sessionId);
        entries.push_back(std::move(*entry));
    }
    return LiveSet {
        .requestId =
            requestValue.isString() ? requestValue.toString() : QString {},
        .authorityIncarnationId = *authority,
        .revision = *revision,
        .entries = std::move(entries),
    };
}

std::optional<AgentSessionIntelModel::Entry>
AgentSessionIntelModel::decodeEntry(const QJsonObject& object)
{
    Entry entry;
    const auto sessionId =
        requiredString(object, QStringLiteral("sessionId"));
    const auto sessionIncarnationId =
        requiredString(object, QStringLiteral("sessionIncarnationId"));
    const auto snapshotValue = object.value(QStringLiteral("snapshot"));
    if (!sessionId || !sessionIncarnationId
        || !canonicalUuidV7(*sessionIncarnationId)
        || !snapshotValue.isObject()) {
        return std::nullopt;
    }
    entry.sessionId = *sessionId;
    entry.sessionIncarnationId = *sessionIncarnationId;
    const auto snapshot = snapshotValue.toObject();

    const auto identityValue = snapshot.value(QStringLiteral("identity"));
    const auto lifecycle =
        requiredString(snapshot, QStringLiteral("lifecycle"), 32);
    const auto workersValue = snapshot.value(QStringLiteral("workers"));
    const auto sourceValue = snapshot.value(QStringLiteral("source"));
    if (!identityValue.isObject() || !lifecycle
        || !known(
            *lifecycle,
            {
                QStringLiteral("starting"),
                QStringLiteral("working"),
                QStringLiteral("idle"),
                QStringLiteral("waiting"),
                QStringLiteral("completed"),
                QStringLiteral("failed"),
                QStringLiteral("stopped"),
                QStringLiteral("offline"),
            })
        || !workersValue.isObject() || !sourceValue.isObject()) {
        return std::nullopt;
    }
    entry.lifecycle = *lifecycle;

    const auto identity = identityValue.toObject();
    const auto agentType =
        requiredString(identity, QStringLiteral("agentType"));
    if (!agentType
        || !nullableString(identity, QStringLiteral("version"), entry.version)
        || !nullableString(identity, QStringLiteral("model"), entry.model)
        || !nullableString(identity, QStringLiteral("title"), entry.title)
        || !nullableString(identity, QStringLiteral("cwd"), entry.cwd)
        || !nullableString(
            identity,
            QStringLiteral("vendorSessionId"),
            entry.vendorSessionId)
        || !nullableUnsigned32(
            identity,
            QStringLiteral("processId"),
            entry.processId,
            entry.hasProcessId)) {
        return std::nullopt;
    }
    entry.agentType = *agentType;

    std::optional<QJsonObject> attention;
    std::optional<QJsonObject> interaction;
    std::optional<QJsonObject> activity;
    std::optional<QJsonObject> outcome;
    std::optional<QJsonObject> exceptional;
    if (!nullableObject(snapshot, QStringLiteral("attention"), attention)
        || !nullableObject(
            snapshot,
            QStringLiteral("pendingInteraction"),
            interaction)
        || !nullableObject(
            snapshot,
            QStringLiteral("currentActivity"),
            activity)
        || !nullableObject(snapshot, QStringLiteral("outcome"), outcome)
        || !nullableObject(
            snapshot,
            QStringLiteral("exceptionalState"),
            exceptional)) {
        return std::nullopt;
    }

    if (attention) {
        const auto kind =
            requiredString(*attention, QStringLiteral("kind"), 32);
        const auto summary =
            requiredString(*attention, QStringLiteral("summary"), summaryTextLimit);
        const auto actionable = attention->value(QStringLiteral("actionable"));
        if (!kind || !summary || !actionable.isBool()
            || !known(
                *kind,
                {
                    QStringLiteral("permission"),
                    QStringLiteral("question"),
                    QStringLiteral("failure"),
                    QStringLiteral("stalled"),
                    QStringLiteral("limit"),
                    QStringLiteral("authentication"),
                    QStringLiteral("compaction"),
                    QStringLiteral("worker"),
                    QStringLiteral("review"),
                    QStringLiteral("delivery"),
                })) {
            return std::nullopt;
        }
        entry.hasAttention = true;
        entry.attentionKind = *kind;
        entry.attentionSummary = *summary;
        entry.attentionActionable = actionable.toBool();
    }

    if (interaction) {
        const auto kind =
            requiredString(*interaction, QStringLiteral("kind"), 32);
        const auto summary = requiredString(
            *interaction,
            QStringLiteral("summary"),
            summaryTextLimit);
        const auto canApprove =
            interaction->value(QStringLiteral("canApprove"));
        const auto canDeny = interaction->value(QStringLiteral("canDeny"));
        const auto canAnswer =
            interaction->value(QStringLiteral("canAnswer"));
        const auto canFocus = interaction->value(QStringLiteral("canFocus"));
        if (!kind || !summary
            || !known(
                *kind,
                {
                    QStringLiteral("permission"),
                    QStringLiteral("question"),
                    QStringLiteral("focus"),
                })
            || !nullableString(
                *interaction,
                QStringLiteral("toolName"),
                entry.pendingToolName,
                summaryTextLimit)
            || !canApprove.isBool() || !canDeny.isBool()
            || !canAnswer.isBool() || !canFocus.isBool()) {
            return std::nullopt;
        }
        entry.hasPendingInteraction = true;
        entry.pendingInteractionKind = *kind;
        entry.pendingInteractionSummary = *summary;
        entry.canApprove = canApprove.toBool();
        entry.canDeny = canDeny.toBool();
        entry.canAnswer = canAnswer.toBool();
        entry.canFocus = canFocus.toBool();
    }

    if (activity) {
        const auto summary =
            requiredString(*activity, QStringLiteral("summary"), summaryTextLimit);
        if (!summary
            || !nullableString(
                *activity,
                QStringLiteral("lastProgressAt"),
                entry.lastProgressAt,
                128)) {
            return std::nullopt;
        }
        entry.currentActivity = *summary;
    }

    const auto workers = workersValue.toObject();
    const auto active = unsigned32(workers.value(QStringLiteral("active")));
    const auto blocked = unsigned32(workers.value(QStringLiteral("blocked")));
    const auto failed = unsigned32(workers.value(QStringLiteral("failed")));
    const auto completed =
        unsigned32(workers.value(QStringLiteral("completed")));
    if (!active || !blocked || !failed || !completed) {
        return std::nullopt;
    }
    entry.activeWorkers = *active;
    entry.blockedWorkers = *blocked;
    entry.failedWorkers = *failed;
    entry.completedWorkers = *completed;

    if (outcome) {
        const auto kind =
            requiredString(*outcome, QStringLiteral("kind"), 32);
        const auto summary =
            requiredString(*outcome, QStringLiteral("summary"), summaryTextLimit);
        if (!kind || !summary
            || !known(
                *kind,
                {
                    QStringLiteral("completed"),
                    QStringLiteral("failed"),
                    QStringLiteral("stopped"),
                })) {
            return std::nullopt;
        }
        entry.hasOutcome = true;
        entry.outcomeKind = *kind;
        entry.outcomeSummary = *summary;
    }

    if (exceptional) {
        const auto kind =
            requiredString(*exceptional, QStringLiteral("kind"), 32);
        const auto summary = requiredString(
            *exceptional,
            QStringLiteral("summary"),
            summaryTextLimit);
        const auto retryable =
            exceptional->value(QStringLiteral("retryable"));
        if (!kind || !summary || !retryable.isBool()
            || !known(
                *kind,
                {
                    QStringLiteral("rateLimit"),
                    QStringLiteral("authentication"),
                    QStringLiteral("quota"),
                    QStringLiteral("contextOverflow"),
                    QStringLiteral("stalled"),
                    QStringLiteral("crash"),
                    QStringLiteral("other"),
                })) {
            return std::nullopt;
        }
        entry.hasExceptionalState = true;
        entry.exceptionalKind = *kind;
        entry.exceptionalSummary = *summary;
        entry.exceptionalRetryable = retryable.toBool();
    }

    const auto source = sourceValue.toObject();
    const auto sourceKind =
        requiredString(source, QStringLiteral("kind"), 32);
    const auto degraded = source.value(QStringLiteral("degraded"));
    if (!sourceKind || !degraded.isBool()
        || !known(
            *sourceKind,
            {
                QStringLiteral("runtime"),
                QStringLiteral("sdk"),
                QStringLiteral("extension"),
                QStringLiteral("telemetry"),
                QStringLiteral("command"),
                QStringLiteral("transcript"),
                QStringLiteral("terminal"),
            })
        || !nullableString(
            source,
            QStringLiteral("detail"),
            entry.sourceDetail)) {
        return std::nullopt;
    }
    entry.sourceKind = *sourceKind;
    entry.sourceDegraded = degraded.toBool();
    return entry;
}

QVariantMap AgentSessionIntelModel::presentation(const Entry& entry)
{
    return {
        {QStringLiteral("sessionId"), entry.sessionId},
        {QStringLiteral("agentType"), entry.agentType},
        {QStringLiteral("version"), entry.version},
        {QStringLiteral("model"), entry.model},
        {QStringLiteral("title"), entry.title},
        {QStringLiteral("cwd"), entry.cwd},
        {QStringLiteral("lifecycle"), entry.lifecycle},
        {QStringLiteral("hasAttention"), entry.hasAttention},
        {QStringLiteral("attentionKind"), entry.attentionKind},
        {QStringLiteral("attentionSummary"), entry.attentionSummary},
        {QStringLiteral("attentionActionable"), entry.attentionActionable},
        {QStringLiteral("hasPendingInteraction"), entry.hasPendingInteraction},
        {QStringLiteral("pendingInteractionKind"), entry.pendingInteractionKind},
        {QStringLiteral("pendingInteractionSummary"), entry.pendingInteractionSummary},
        {QStringLiteral("pendingToolName"), entry.pendingToolName},
        {QStringLiteral("canApprove"), entry.canApprove},
        {QStringLiteral("canDeny"), entry.canDeny},
        {QStringLiteral("canAnswer"), entry.canAnswer},
        {QStringLiteral("canFocus"), entry.canFocus},
        {QStringLiteral("currentActivity"), entry.currentActivity},
        {QStringLiteral("lastProgressAt"), entry.lastProgressAt},
        {QStringLiteral("activeWorkers"), entry.activeWorkers},
        {QStringLiteral("blockedWorkers"), entry.blockedWorkers},
        {QStringLiteral("failedWorkers"), entry.failedWorkers},
        {QStringLiteral("completedWorkers"), entry.completedWorkers},
        {QStringLiteral("hasOutcome"), entry.hasOutcome},
        {QStringLiteral("outcomeKind"), entry.outcomeKind},
        {QStringLiteral("outcomeSummary"), entry.outcomeSummary},
        {QStringLiteral("hasExceptionalState"), entry.hasExceptionalState},
        {QStringLiteral("exceptionalKind"), entry.exceptionalKind},
        {QStringLiteral("exceptionalSummary"), entry.exceptionalSummary},
        {QStringLiteral("exceptionalRetryable"), entry.exceptionalRetryable},
        {QStringLiteral("sourceKind"), entry.sourceKind},
        {QStringLiteral("sourceDegraded"), entry.sourceDegraded},
        {QStringLiteral("sourceDetail"), entry.sourceDetail},
    };
}

} // namespace kodosi
