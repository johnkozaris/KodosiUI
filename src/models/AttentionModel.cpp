#include "models/AttentionModel.hpp"

#include <QSet>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

QDateTime parseRecency(const QString& value)
{
    auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }
    return parsed;
}

QString exactKey(std::initializer_list<QString> values)
{
    QString result;
    for (const auto& value : values) {
        result += QString::number(value.size());
        result += QLatin1Char(':');
        result += value;
        result += QLatin1Char('|');
    }
    return result;
}

} // namespace

AttentionModel::AttentionModel(
    PendingPermissionsModel& pendingPermissions,
    AgentSessionIntelModel& agentIntel,
    SessionCatalogModel& sessions,
    SessionActions& sessionActions,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_pendingPermissions(pendingPermissions)
    , m_agentIntel(agentIntel)
    , m_sessions(sessions)
    , m_sessionActions(sessionActions)
{
    const auto rebuildProjection = [this] { rebuild(); };
    for (auto* model : {
             static_cast<QAbstractItemModel*>(&m_pendingPermissions),
             static_cast<QAbstractItemModel*>(&m_agentIntel),
             static_cast<QAbstractItemModel*>(&m_sessions),
         }) {
        connect(
            model,
            &QAbstractItemModel::modelReset,
            this,
            rebuildProjection);
        connect(
            model,
            &QAbstractItemModel::rowsInserted,
            this,
            rebuildProjection);
        connect(
            model,
            &QAbstractItemModel::rowsRemoved,
            this,
            rebuildProjection);
        connect(
            model,
            &QAbstractItemModel::dataChanged,
            this,
            rebuildProjection);
    }
    connect(
        &m_pendingPermissions,
        &PendingPermissionsModel::authorityStateChanged,
        this,
        rebuildProjection);
    connect(
        &m_agentIntel,
        &AgentSessionIntelModel::hydrationStateChanged,
        this,
        rebuildProjection);
    connect(
        &m_agentIntel,
        &AgentSessionIntelModel::authorityRevisionChanged,
        this,
        rebuildProjection);
    connect(
        &m_sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        rebuildProjection);
    connect(
        &m_sessions,
        &SessionCatalogModel::authorityStateChanged,
        this,
        rebuildProjection);
    rebuild();
}

int AttentionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AttentionModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const auto& item = m_items.at(index.row());
    switch (role) {
    case CategoryRole: return QVariant::fromValue(item.category);
    case SessionIdRole: return item.sessionId;
    case SessionNameRole: return item.sessionName;
    case TitleRole: return item.title;
    case SummaryRole: return item.summary;
    case RiskRole: return QVariant::fromValue(item.risk);
    case ToneRole: return QVariant::fromValue(item.tone);
    case ActionKindRole: return QVariant::fromValue(item.actionKind);
    case CanApproveRole: return item.canApprove;
    case CanDenyRole: return item.canDeny;
    case CanJumpRole: return item.canJump;
    case AttentionTokenRole: return item.attentionToken;
    case StableIdRole: return item.stableId;
    default: return {};
    }
}

QHash<int, QByteArray> AttentionModel::roleNames() const
{
    return {
        {CategoryRole, QByteArrayLiteral("category")},
        {SessionIdRole, QByteArrayLiteral("sessionId")},
        {SessionNameRole, QByteArrayLiteral("sessionName")},
        {TitleRole, QByteArrayLiteral("title")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {RiskRole, QByteArrayLiteral("risk")},
        {ToneRole, QByteArrayLiteral("tone")},
        {ActionKindRole, QByteArrayLiteral("actionKind")},
        {CanApproveRole, QByteArrayLiteral("canApprove")},
        {CanDenyRole, QByteArrayLiteral("canDeny")},
        {CanJumpRole, QByteArrayLiteral("canJump")},
        {AttentionTokenRole, QByteArrayLiteral("attentionToken")},
        {StableIdRole, QByteArrayLiteral("stableId")},
    };
}

int AttentionModel::runningCount() const noexcept
{
    return m_runningCount;
}

AttentionModel::AuthorityState AttentionModel::authorityState() const noexcept
{
    return m_authorityState;
}

QString AttentionModel::authorityError() const
{
    return m_authorityError;
}

QString AttentionModel::operationError() const
{
    return m_operationError;
}

bool AttentionModel::refresh()
{
    return requestAuthorityRefresh(true);
}

bool AttentionModel::review(const QString& attentionToken)
{
    const auto* item = itemForToken(attentionToken);
    if (item == nullptr || item->category != Category::Approval
        || item->actionKind != ActionKind::Review
        || !sourceIsCurrent(*item)) {
        return failStale();
    }
    clearOperationError();
    emit navigationRequested(
        item->sessionId,
        item->approvalIdentityTokens.constFirst());
    return true;
}

bool AttentionModel::jump(const QString& attentionToken)
{
    const auto* item = itemForToken(attentionToken);
    if (item == nullptr || !item->canJump || !sourceIsCurrent(*item)) {
        return failStale();
    }
    clearOperationError();
    emit navigationRequested(
        item->sessionId,
        item->category == Category::Approval
            ? item->approvalIdentityTokens.constFirst()
            : QString {});
    return true;
}

bool AttentionModel::approve(const QString& attentionToken)
{
    const auto* item = itemForToken(attentionToken);
    if (item == nullptr || item->category != Category::Approval
        || !item->canApprove || !sourceIsCurrent(*item)) {
        return failStale();
    }
    if (item->actionKind != ActionKind::Approve) {
        setOperationError(
            tr("Review this request before deciding."));
        return false;
    }
    clearOperationError();
    const auto identity = item->approvalIdentityTokens.constFirst();
    const auto accepted = m_pendingPermissions.approve(identity);
    if (!accepted) {
        setOperationError(
            tr("The approval could not be delivered. The request remains visible."));
    }
    return accepted;
}

bool AttentionModel::deny(const QString& attentionToken)
{
    const auto* item = itemForToken(attentionToken);
    if (item == nullptr || item->category != Category::Approval
        || !item->canDeny || !sourceIsCurrent(*item)) {
        return failStale();
    }
    clearOperationError();
    const auto identity = item->approvalIdentityTokens.constFirst();
    const auto accepted = m_pendingPermissions.deny(identity);
    if (!accepted) {
        setOperationError(
            tr("The denial could not be delivered. The request remains visible."));
    }
    return accepted;
}

bool AttentionModel::approveAll(const QString& attentionToken)
{
    const auto* item = itemForToken(attentionToken);
    if (item == nullptr || item->category != Category::BulkSafe
        || !item->canApprove) {
        return failStale();
    }
    const auto identities = item->approvalIdentityTokens;
    clearOperationError();
    qsizetype acceptedCount = 0;
    qsizetype failedCount = 0;
    for (const auto& identity : identities) {
        const auto request =
            m_pendingPermissions.notificationRequest(identity);
        if (!request || request->risk != QStringLiteral("safe")
            || !request->actionable
            || !eligibleSession(request->sessionId)) {
            ++failedCount;
            continue;
        }
        if (m_pendingPermissions.approve(identity)) {
            ++acceptedCount;
        } else {
            ++failedCount;
        }
    }
    if (failedCount > 0) {
        setOperationError(
            tr("Some safe reads could not be approved. Remaining requests stay visible."));
    }
    rebuild();
    return acceptedCount > 0 && failedCount == 0;
}

void AttentionModel::clearOperationError()
{
    setOperationError({});
}

void AttentionModel::rebuild()
{
    if (!m_sessions.m_hasAuthoritativeSnapshot
        && !m_operationError.isEmpty()) {
        setOperationError({});
    }
    auto authorityState = AuthorityState::Loaded;
    QString authorityError;
    if (m_sessions.m_authorityState
        == SessionCatalogModel::AuthorityState::Failed) {
        authorityState = AuthorityState::Failed;
        authorityError = m_sessions.m_authorityError;
    } else if (
        m_pendingPermissions.m_authenticated
        && m_pendingPermissions.m_authorityState
            == PendingPermissionsModel::AuthorityState::AuthorityFailed) {
        authorityState = AuthorityState::Failed;
        authorityError = m_pendingPermissions.m_authorityError;
    } else if (
        m_agentIntel.m_hasAccountContext
        && m_agentIntel.m_hydrationState
            == AgentSessionIntelModel::HydrationState::Failed) {
        authorityState = AuthorityState::Failed;
        authorityError = m_agentIntel.m_hydrationError;
    } else if (
        m_sessions.m_authorityState
            == SessionCatalogModel::AuthorityState::Loading
        || (m_pendingPermissions.m_authenticated
            && m_pendingPermissions.m_authorityState
                == PendingPermissionsModel::AuthorityState::Loading)
        || (m_agentIntel.m_hasAccountContext
            && m_agentIntel.m_hydrationState
                == AgentSessionIntelModel::HydrationState::Loading)) {
        authorityState = AuthorityState::Loading;
    }

    int runningCount = 0;
    for (const auto& session : m_sessions.m_sessions) {
        if (session.status == QStringLiteral("active")) {
            ++runningCount;
        }
    }

    QSet<QString> liveTokenKeys;
    QSet<QString> sessionsWithApprovals;
    QVector<const PendingPermissionsModel::Request*> approvals;
    approvals.reserve(m_pendingPermissions.m_requests.size());
    for (const auto& request : m_pendingPermissions.m_requests) {
        if (!eligibleSession(request.sessionId)) {
            continue;
        }
        sessionsWithApprovals.insert(request.sessionId);
        approvals.push_back(&request);
    }
    std::ranges::sort(
        approvals,
        [](const auto* lhs, const auto* rhs) {
            const auto lhsSeverity = riskSeverity(lhs->risk);
            const auto rhsSeverity = riskSeverity(rhs->risk);
            if (lhsSeverity != rhsSeverity) {
                return lhsSeverity > rhsSeverity;
            }
            if (lhs->createdAt != rhs->createdAt) {
                return lhs->createdAt < rhs->createdAt;
            }
            return lhs->identityToken < rhs->identityToken;
        });

    QVector<Item> items;
    QVector<QString> safeIdentities;
    QSet<QString> safeSessionNames;
    for (const auto* request : approvals) {
        const auto session = std::ranges::find(
            m_sessions.m_sessions,
            request->sessionId,
            &SessionCatalogModel::Session::id);
        const auto sessionName = session == m_sessions.m_sessions.end()
            ? request->sessionId.left(8)
            : session->name;
        if (request->risk == QStringLiteral("safe")
            && requestActionable(*request)) {
            safeIdentities.push_back(request->identityToken);
            safeSessionNames.insert(sessionName);
            continue;
        }
        const auto canDecide = requestActionable(*request);
        Item item;
        item.category = Category::Approval;
        item.sessionId = request->sessionId;
        item.sessionName = sessionName;
        item.title = tr("%1 approval").arg(request->toolName);
        item.summary = !request->decisionMessage.isEmpty()
            ? request->decisionMessage
            : !request->toolInputSummary.isEmpty()
            ? request->toolInputSummary
            : tr("Permission request waiting");
        item.risk = riskValue(request->risk);
        item.tone = approvalTone(request->risk);
        item.actionKind =
            request->risk == QStringLiteral("safe")
                || request->risk == QStringLiteral("network")
            ? ActionKind::Approve
            : ActionKind::Review;
        item.canApprove = canDecide;
        item.canDeny = canDecide;
        item.canJump = true;
        item.attentionToken = request->identityToken;
        item.stableId = request->identityToken;
        item.sessionIncarnationId = request->sessionIncarnationId;
        item.recency = request->createdAt;
        item.approvalIdentityTokens = {request->identityToken};
        items.push_back(std::move(item));
    }
    if (!safeIdentities.isEmpty()) {
        std::ranges::sort(safeIdentities);
        QStringList memberKeys;
        memberKeys.reserve(safeIdentities.size());
        for (const auto& identity : safeIdentities) {
            memberKeys.push_back(identity);
        }
        const auto key = exactKey({
            QStringLiteral("bulkSafe"),
            memberKeys.join(QLatin1Char(',')),
        });
        const auto sessionSummary = safeSessionNames.size() == 1
            ? *safeSessionNames.constBegin()
            : tr("%1 sessions").arg(safeSessionNames.size());
        Item item;
        item.category = Category::BulkSafe;
        item.sessionName = sessionSummary;
        item.title = safeIdentities.size() == 1
            ? tr("1 safe read waiting")
            : tr("%1 safe reads waiting").arg(safeIdentities.size());
        item.summary = sessionSummary;
        item.risk = Risk::Safe;
        item.tone = Tone::Accent;
        item.actionKind = ActionKind::BulkApprove;
        item.canApprove = true;
        item.attentionToken = tokenForExactKey(key, liveTokenKeys);
        item.stableId = QStringLiteral("bulkSafe");
        item.approvalIdentityTokens = std::move(safeIdentities);
        items.push_back(std::move(item));
    }

    QVector<Item> otherItems;
    QSet<QString> sessionsWithLiveAttention;
    for (const auto& entry : m_agentIntel.m_entries) {
        if (!entry.hasAttention || !eligibleSession(entry.sessionId)
            || (entry.attentionKind == QStringLiteral("permission")
                && sessionsWithApprovals.contains(entry.sessionId))) {
            continue;
        }
        const auto session = std::ranges::find(
            m_sessions.m_sessions,
            entry.sessionId,
            &SessionCatalogModel::Session::id);
        if (session == m_sessions.m_sessions.end()) {
            continue;
        }
        const auto revision = m_agentIntel.m_revision.value_or(0);
        const auto key = exactKey({
            QStringLiteral("agent"),
            entry.sessionId,
            entry.sessionIncarnationId,
            m_agentIntel.m_authorityIncarnationId,
            QString::number(revision),
            entry.attentionKind,
        });
        sessionsWithLiveAttention.insert(entry.sessionId);
        Item item;
        item.category = Category::Agent;
        item.sessionId = entry.sessionId;
        item.sessionName = session->name;
        item.title = attentionTitle(entry.attentionKind);
        item.summary = entry.attentionSummary;
        item.tone = attentionTone(entry.attentionKind);
        item.actionKind = ActionKind::Jump;
        item.canJump = true;
        item.attentionToken = tokenForExactKey(key, liveTokenKeys);
        item.stableId = entry.sessionId;
        item.sessionIncarnationId = entry.sessionIncarnationId;
        item.authorityIncarnationId =
            m_agentIntel.m_authorityIncarnationId;
        item.attentionKind = entry.attentionKind;
        item.sourceRevision = revision;
        item.recency = parseRecency(entry.lastProgressAt);
        otherItems.push_back(std::move(item));
    }

    for (const auto& session : m_sessions.m_sessions) {
        const auto blocked = session.status == QStringLiteral("blocked");
        const auto waiting = session.status == QStringLiteral("waiting");
        if ((!blocked && !waiting) || !eligibleSession(session.id)
            || sessionsWithApprovals.contains(session.id)
            || sessionsWithLiveAttention.contains(session.id)) {
            continue;
        }
        const auto key = exactKey({
            QStringLiteral("session"),
            session.id,
            session.incarnationId,
            session.status,
        });
        Item item;
        item.category = blocked ? Category::Blocked : Category::Waiting;
        item.sessionId = session.id;
        item.sessionName = session.name;
        item.title = blocked ? tr("Blocked") : tr("Waiting");
        item.summary = tr("Open the session to continue.");
        item.tone = Tone::Warning;
        item.actionKind = ActionKind::Jump;
        item.canJump = true;
        item.attentionToken = tokenForExactKey(key, liveTokenKeys);
        item.stableId = session.id;
        item.sessionIncarnationId = session.incarnationId;
        otherItems.push_back(std::move(item));
    }
    std::ranges::sort(
        otherItems,
        [](const Item& lhs, const Item& rhs) {
            const auto lhsSeverity = lhs.category == Category::Agent
                ? attentionSeverity(lhs.attentionKind)
                : lhs.category == Category::Blocked ? 3 : 2;
            const auto rhsSeverity = rhs.category == Category::Agent
                ? attentionSeverity(rhs.attentionKind)
                : rhs.category == Category::Blocked ? 3 : 2;
            if (lhsSeverity != rhsSeverity) {
                return lhsSeverity > rhsSeverity;
            }
            if (lhs.recency.isValid() != rhs.recency.isValid()) {
                return lhs.recency.isValid();
            }
            if (lhs.recency.isValid() && lhs.recency != rhs.recency) {
                return lhs.recency > rhs.recency;
            }
            if (lhs.stableId != rhs.stableId) {
                return lhs.stableId < rhs.stableId;
            }
            return lhs.category < rhs.category;
        });
    items += std::move(otherItems);

    for (auto iterator = m_tokensByExactKey.begin();
         iterator != m_tokensByExactKey.end();) {
        if (liveTokenKeys.contains(iterator.key())) {
            ++iterator;
        } else {
            iterator = m_tokensByExactKey.erase(iterator);
        }
    }

    const auto countChangedValue = items.size() != m_items.size();
    if (items != m_items) {
        beginResetModel();
        m_items = std::move(items);
        endResetModel();
        if (countChangedValue) {
            emit countChanged();
        }
    }
    if (m_runningCount != runningCount
        || m_authorityState != authorityState
        || m_authorityError != authorityError) {
        m_runningCount = runningCount;
        m_authorityState = authorityState;
        m_authorityError = std::move(authorityError);
        emit stateChanged();
    }
}

void AttentionModel::setOperationError(QString error)
{
    if (m_operationError == error) {
        return;
    }
    m_operationError = std::move(error);
    emit operationErrorChanged();
}

bool AttentionModel::requestAuthorityRefresh(const bool clearError)
{
    if (clearError) {
        clearOperationError();
    }
    const auto sessionsAccepted = m_sessionActions.refresh();
    const auto pendingAccepted = m_pendingPermissions.refresh();
    const auto intelAccepted = m_agentIntel.refresh();
    return sessionsAccepted || pendingAccepted || intelAccepted;
}

bool AttentionModel::failStale()
{
    setOperationError(
        tr("This attention item is no longer current. Attention was refreshed."));
    rebuild();
    (void)requestAuthorityRefresh(false);
    return false;
}

bool AttentionModel::eligibleSession(const QString& sessionId) const
{
    const auto session = std::ranges::find(
        m_sessions.m_sessions,
        sessionId,
        &SessionCatalogModel::Session::id);
    if (session == m_sessions.m_sessions.end()
        || session->incarnationId.isEmpty()
        || session->recovery == QStringLiteral("quarantined")
        || !session->commandable) {
        return false;
    }
    return session->kind != QStringLiteral("remote")
        || (session->connectionState == QStringLiteral("connected")
            && session->accessState == QStringLiteral("ready"));
}

bool AttentionModel::sourceIsCurrent(const Item& item) const
{
    if (!eligibleSession(item.sessionId)) {
        return false;
    }
    if (item.category == Category::Approval) {
        if (item.approvalIdentityTokens.size() != 1) {
            return false;
        }
        const auto request = m_pendingPermissions.notificationRequest(
            item.approvalIdentityTokens.constFirst());
        const auto incarnation =
            m_sessions.incarnationForSession(item.sessionId);
        return request && incarnation
            && *incarnation == item.sessionIncarnationId;
    }
    if (item.category == Category::Agent) {
        if (!m_agentIntel.m_revision
            || *m_agentIntel.m_revision != item.sourceRevision
            || m_agentIntel.m_authorityIncarnationId
                != item.authorityIncarnationId) {
            return false;
        }
        const auto entry = std::ranges::find(
            m_agentIntel.m_entries,
            item.sessionId,
            &AgentSessionIntelModel::Entry::sessionId);
        return entry != m_agentIntel.m_entries.end()
            && entry->sessionIncarnationId == item.sessionIncarnationId
            && entry->hasAttention
            && entry->attentionKind == item.attentionKind;
    }
    const auto session = std::ranges::find(
        m_sessions.m_sessions,
        item.sessionId,
        &SessionCatalogModel::Session::id);
    return session != m_sessions.m_sessions.end()
        && session->incarnationId == item.sessionIncarnationId
        && ((item.category == Category::Blocked
                && session->status == QStringLiteral("blocked"))
            || (item.category == Category::Waiting
                && session->status == QStringLiteral("waiting")));
}

const AttentionModel::Item* AttentionModel::itemForToken(
    const QString& token) const
{
    const auto found =
        std::ranges::find(m_items, token, &Item::attentionToken);
    return found == m_items.end() ? nullptr : &*found;
}

QString AttentionModel::tokenForExactKey(
    const QString& exactKeyValue,
    QSet<QString>& liveKeys)
{
    liveKeys.insert(exactKeyValue);
    auto& token = m_tokensByExactKey[exactKeyValue];
    if (token.isEmpty()) {
        token = QStringLiteral("attn-")
            + QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    }
    return token;
}

int AttentionModel::riskSeverity(const QString& risk)
{
    if (risk == QStringLiteral("destructive")) {
        return 4;
    }
    if (risk == QStringLiteral("credential")) {
        return 3;
    }
    if (risk == QStringLiteral("network")) {
        return 2;
    }
    if (risk == QStringLiteral("unknown")) {
        return 1;
    }
    return 0;
}

int AttentionModel::attentionSeverity(const QString& kind)
{
    if (kind == QStringLiteral("failure")
        || kind == QStringLiteral("authentication")) {
        return 5;
    }
    if (kind == QStringLiteral("permission")
        || kind == QStringLiteral("delivery")) {
        return 4;
    }
    if (kind == QStringLiteral("question")
        || kind == QStringLiteral("stalled")
        || kind == QStringLiteral("limit")
        || kind == QStringLiteral("compaction")) {
        return 3;
    }
    if (kind == QStringLiteral("worker")) {
        return 2;
    }
    return 1;
}

bool AttentionModel::requestActionable(
    const PendingPermissionsModel::Request& request)
{
    return request.authoritativePhase
            == PendingPermissionsModel::AuthoritativePhase::Actionable
        && (request.decisionState
                == PendingPermissionsModel::DecisionState::Actionable
            || request.decisionState
                == PendingPermissionsModel::DecisionState::DeliveryFailed);
}

AttentionModel::Risk AttentionModel::riskValue(const QString& risk)
{
    if (risk == QStringLiteral("safe")) {
        return Risk::Safe;
    }
    if (risk == QStringLiteral("network")) {
        return Risk::Network;
    }
    if (risk == QStringLiteral("credential")) {
        return Risk::Credential;
    }
    if (risk == QStringLiteral("destructive")) {
        return Risk::Destructive;
    }
    return Risk::Unknown;
}

AttentionModel::Tone AttentionModel::approvalTone(const QString& risk)
{
    if (risk == QStringLiteral("destructive")) {
        return Tone::Danger;
    }
    if (risk == QStringLiteral("credential")
        || risk == QStringLiteral("unknown")) {
        return Tone::Warning;
    }
    return Tone::Accent;
}

AttentionModel::Tone AttentionModel::attentionTone(const QString& kind)
{
    if (kind == QStringLiteral("failure")
        || kind == QStringLiteral("authentication")) {
        return Tone::Danger;
    }
    if (kind == QStringLiteral("review")) {
        return Tone::Accent;
    }
    return Tone::Warning;
}

QString AttentionModel::attentionTitle(const QString& kind)
{
    if (kind == QStringLiteral("permission")) {
        return tr("Permission needed");
    }
    if (kind == QStringLiteral("question")) {
        return tr("Question waiting");
    }
    if (kind == QStringLiteral("failure")) {
        return tr("Agent failed");
    }
    if (kind == QStringLiteral("stalled")) {
        return tr("Agent stalled");
    }
    if (kind == QStringLiteral("limit")) {
        return tr("Usage limit");
    }
    if (kind == QStringLiteral("authentication")) {
        return tr("Authentication needed");
    }
    if (kind == QStringLiteral("compaction")) {
        return tr("Context compacted");
    }
    if (kind == QStringLiteral("worker")) {
        return tr("Worker needs attention");
    }
    if (kind == QStringLiteral("review")) {
        return tr("Review requested");
    }
    return tr("Delivery interrupted");
}

} // namespace kodosi
