#include "models/MissionDetailModel.hpp"

#include "models/AttentionModel.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/MissionActions.hpp"
#include "models/PeopleModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"
#include "models/SteeringModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
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

std::optional<qint64> integer(const QJsonValue& value, const bool nonnegative)
{
    constexpr auto maximumExactJsonInteger = 9'007'199'254'740'991.0;
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < -maximumExactJsonInteger
        || number > maximumExactJsonInteger || std::floor(number) != number
        || (nonnegative && number < 0)) {
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

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    QString& result,
    const bool nonempty = false)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        result.clear();
        return true;
    }
    if (!value.isString()
        || (nonempty && value.toString().isEmpty())) {
        return false;
    }
    result = value.toString();
    return true;
}

std::optional<QStringList> stringArray(
    const QJsonObject& object,
    const QString& key,
    const bool required = false)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        return required
            ? std::nullopt
            : std::optional<QStringList> {QStringList {}};
    }
    if (!value.isArray()) {
        return std::nullopt;
    }
    QStringList strings;
    QSet<QString> unique;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()
            || unique.contains(entry.toString())) {
            return std::nullopt;
        }
        unique.insert(entry.toString());
        strings.push_back(entry.toString());
    }
    return strings;
}

QByteArray commandJson(QJsonObject command)
{
    return QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
}

bool liveMissionSession(const QString& status)
{
    return status == QStringLiteral("active")
        || status == QStringLiteral("waiting")
        || status == QStringLiteral("blocked")
        || status == QStringLiteral("reconnecting");
}

bool knownTaskStatus(const QString& status)
{
    return status == QStringLiteral("Open")
        || status == QStringLiteral("InProgress")
        || status == QStringLiteral("Review")
        || status == QStringLiteral("Done")
        || status == QStringLiteral("Archived");
}

bool knownDeliveryState(const QString& state)
{
    return state == QStringLiteral("waitingForAdapter")
        || state == QStringLiteral("ready")
        || state == QStringLiteral("offered")
        || state == QStringLiteral("acceptedByTransport")
        || state == QStringLiteral("actedOn")
        || state == QStringLiteral("failed");
}

} // namespace

MissionMembersModel::MissionMembersModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionMembersModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_members.size();
}

QVariant MissionMembersModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_members.size()) {
        return {};
    }
    const auto& member = m_members[index.row()];
    switch (role) {
    case UserIdRole:
        return member.userId;
    case MemberRoleRole:
        return member.role;
    case UsernameRole:
        return member.username;
    case DisplayNameRole:
        return member.displayName;
    case ResolvedNameRole:
        return !member.displayName.isEmpty()
            ? member.displayName
            : !member.username.isEmpty() ? member.username : member.userId;
    default:
        return {};
    }
}

QHash<int, QByteArray> MissionMembersModel::roleNames() const
{
    return {
        {UserIdRole, QByteArrayLiteral("userId")},
        {MemberRoleRole, QByteArrayLiteral("memberRole")},
        {UsernameRole, QByteArrayLiteral("username")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {ResolvedNameRole, QByteArrayLiteral("resolvedName")},
    };
}

bool MissionMembersModel::containsUser(const QString& userId) const
{
    return std::ranges::find(m_members, userId, &Member::userId)
        != m_members.end();
}

void MissionMembersModel::replace(QVector<Member> members)
{
    std::ranges::sort(members, [](const Member& lhs, const Member& rhs) {
        const auto lhsName = !lhs.displayName.isEmpty() ? lhs.displayName
            : !lhs.username.isEmpty()                    ? lhs.username
                                                        : lhs.userId;
        const auto rhsName = !rhs.displayName.isEmpty() ? rhs.displayName
            : !rhs.username.isEmpty()                    ? rhs.username
                                                        : rhs.userId;
        return lhsName < rhsName;
    });
    beginResetModel();
    const auto changed = m_members.size() != members.size();
    m_members = std::move(members);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

MissionMessagesModel::MissionMessagesModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionMessagesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant MissionMessagesModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }
    const auto& message = m_messages[index.row()];
    switch (role) {
    case MessageIdRole:
        return message.id;
    case AuthorUserIdRole:
        return message.authorUserId;
    case AuthorSessionIdRole:
        return message.authorSessionId;
    case AuthorKindRole:
        return message.authorKind;
    case BodyRole:
        return message.body;
    case SequenceRole:
        return message.sequence;
    case PostedAtRole:
        return message.postedAt;
    case BroadcastRole:
        return message.recipientSessionIds.isEmpty()
            && message.recipientUserIds.isEmpty();
    case AuthorDisplayRole:
        return message.authorDisplay;
    case AudienceSummaryRole:
        return message.audienceSummary;
    case RecipientPresentationIdsRole:
        return message.recipientPresentationIds;
    case RecipientCountRole:
        return message.recipientSessionIds.size()
            + message.recipientUserIds.size();
    default:
        return {};
    }
}

QHash<int, QByteArray> MissionMessagesModel::roleNames() const
{
    return {
        {MessageIdRole, QByteArrayLiteral("messageId")},
        {AuthorUserIdRole, QByteArrayLiteral("authorUserId")},
        {AuthorSessionIdRole, QByteArrayLiteral("authorSessionId")},
        {AuthorKindRole, QByteArrayLiteral("authorKind")},
        {BodyRole, QByteArrayLiteral("body")},
        {SequenceRole, QByteArrayLiteral("sequence")},
        {PostedAtRole, QByteArrayLiteral("postedAt")},
        {BroadcastRole, QByteArrayLiteral("broadcast")},
        {AuthorDisplayRole, QByteArrayLiteral("authorDisplay")},
        {AudienceSummaryRole, QByteArrayLiteral("audienceSummary")},
        {RecipientPresentationIdsRole,
         QByteArrayLiteral("recipientPresentationIds")},
        {RecipientCountRole, QByteArrayLiteral("recipientCount")},
    };
}

void MissionMessagesModel::replace(QVector<Message> messages)
{
    std::ranges::sort(messages, {}, &Message::sequence);
    constexpr qsizetype maximumMessages = 1'000;
    if (messages.size() > maximumMessages) {
        messages = messages.sliced(messages.size() - maximumMessages);
    }
    beginResetModel();
    const auto changed = m_messages.size() != messages.size();
    m_messages = std::move(messages);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

void MissionMessagesModel::upsert(Message message)
{
    auto messages = m_messages;
    const auto found = std::ranges::find(messages, message.id, &Message::id);
    if (found == messages.end()) {
        messages.push_back(std::move(message));
    } else {
        *found = std::move(message);
    }
    replace(std::move(messages));
}

MissionTasksModel::MissionTasksModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionTasksModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_tasks.size();
}

QVariant MissionTasksModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
        return {};
    }
    const auto& task = m_tasks[index.row()];
    switch (role) {
    case TaskIdRole:
        return task.id;
    case TitleRole:
        return task.title;
    case DescriptionRole:
        return task.description;
    case StatusRole:
        return task.status;
    case AssignedSessionIdRole:
        return task.assignedSessionId;
    case DueAtRole:
        return task.dueAt;
    case UpdatedAtRole:
        return task.updatedAt;
    case ResultRole:
        return task.result;
    case KnownStatusRole:
        return task.knownStatus;
    case StatusLabelRole:
        return task.statusLabel;
    case AssignmentDisplayRole:
        return task.assignmentDisplay;
    case CreatedAtRole:
        return task.createdAt;
    case CompletedAtRole:
        return task.completedAt;
    case ResultEvidenceRole:
        return task.result;
    case ResultAuthorDisplayRole:
        return task.resultAuthorDisplay;
    case UnknownStatusTitleRole:
        return task.knownStatus
            ? QString {}
            : tr("Unknown task state");
    case UnknownStatusMessageRole:
        return task.knownStatus
            ? QString {}
            : tr("This task uses a status this client does not recognize. It remains visible and cannot be changed here.");
    default:
        return {};
    }
}

QHash<int, QByteArray> MissionTasksModel::roleNames() const
{
    return {
        {TaskIdRole, QByteArrayLiteral("taskId")},
        {TitleRole, QByteArrayLiteral("title")},
        {DescriptionRole, QByteArrayLiteral("description")},
        {StatusRole, QByteArrayLiteral("status")},
        {AssignedSessionIdRole, QByteArrayLiteral("assignedSessionId")},
        {DueAtRole, QByteArrayLiteral("dueAt")},
        {UpdatedAtRole, QByteArrayLiteral("updatedAt")},
        {ResultRole, QByteArrayLiteral("result")},
        {KnownStatusRole, QByteArrayLiteral("knownStatus")},
        {StatusLabelRole, QByteArrayLiteral("statusLabel")},
        {AssignmentDisplayRole, QByteArrayLiteral("assignmentDisplay")},
        {CreatedAtRole, QByteArrayLiteral("createdAt")},
        {CompletedAtRole, QByteArrayLiteral("completedAt")},
        {ResultEvidenceRole, QByteArrayLiteral("resultEvidence")},
        {ResultAuthorDisplayRole, QByteArrayLiteral("resultAuthorDisplay")},
        {UnknownStatusTitleRole, QByteArrayLiteral("unknownStatusTitle")},
        {UnknownStatusMessageRole, QByteArrayLiteral("unknownStatusMessage")},
    };
}

std::optional<MissionTasksModel::ActionContext>
MissionTasksModel::actionContext(const QString& taskId) const
{
    const auto found = std::ranges::find(m_tasks, taskId, &Task::id);
    return found == m_tasks.end()
        ? std::nullopt
        : std::optional<ActionContext> {{
              .status = found->status,
              .assignedSessionId = found->assignedSessionId,
              .assignedSessionIncarnationId =
                  found->assignedSessionIncarnationId,
              .revision = found->revision,
          }};
}

void MissionTasksModel::replace(QVector<Task> tasks)
{
    std::ranges::sort(tasks, [](const Task& lhs, const Task& rhs) {
        return lhs.updatedAt == rhs.updatedAt
            ? lhs.id > rhs.id
            : lhs.updatedAt > rhs.updatedAt;
    });
    beginResetModel();
    const auto changed = m_tasks.size() != tasks.size();
    m_tasks = std::move(tasks);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

void MissionTasksModel::upsert(Task task)
{
    auto tasks = m_tasks;
    const auto found = std::ranges::find(tasks, task.id, &Task::id);
    if (found != tasks.end() && task.revision < found->revision) {
        return;
    }
    if (found == tasks.end()) {
        tasks.push_back(std::move(task));
    } else {
        *found = std::move(task);
    }
    replace(std::move(tasks));
}

MissionCrewModel::MissionCrewModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionCrewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant MissionCrewModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case PresentationIdRole: return entry.presentationId;
    case KindRole: return QVariant::fromValue(entry.kind);
    case DisplayNameRole: return entry.displayName;
    case SecondaryLabelRole: return entry.secondaryLabel;
    case StatusRole: return entry.status;
    case CanOpenFullTerminalRole: return entry.canOpenFullTerminal;
    case CanDispatchRole: return entry.canDispatch;
    case CanAssignTaskRole: return entry.canAssignTask;
    case CanSteerRole: return entry.canSteer;
    case CanInterruptRole: return entry.canInterrupt;
    case DispatchSelectedRole: return entry.dispatchSelected;
    case AttentionCountRole: return entry.attentionCount;
    case DeliveryStateRole: return entry.deliveryState;
    case DeliveryDetailRole: return entry.deliveryDetail;
    default: return {};
    }
}

QHash<int, QByteArray> MissionCrewModel::roleNames() const
{
    return {
        {PresentationIdRole, QByteArrayLiteral("presentationId")},
        {KindRole, QByteArrayLiteral("kind")},
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {SecondaryLabelRole, QByteArrayLiteral("secondaryLabel")},
        {StatusRole, QByteArrayLiteral("status")},
        {CanOpenFullTerminalRole, QByteArrayLiteral("canOpenFullTerminal")},
        {CanDispatchRole, QByteArrayLiteral("canDispatch")},
        {CanAssignTaskRole, QByteArrayLiteral("canAssignTask")},
        {CanSteerRole, QByteArrayLiteral("canSteer")},
        {CanInterruptRole, QByteArrayLiteral("canInterrupt")},
        {DispatchSelectedRole, QByteArrayLiteral("dispatchSelected")},
        {AttentionCountRole, QByteArrayLiteral("attentionCount")},
        {DeliveryStateRole, QByteArrayLiteral("deliveryState")},
        {DeliveryDetailRole, QByteArrayLiteral("deliveryDetail")},
    };
}

int MissionCrewModel::dispatchableAgentCount() const noexcept
{
    return static_cast<int>(std::ranges::count_if(
        m_entries,
        [](const Entry& entry) {
            return entry.kind == Kind::Agent && entry.canDispatch;
        }));
}

bool MissionCrewModel::containsPresentationId(
    const QString& presentationId) const
{
    return std::ranges::find(
        m_entries,
        presentationId,
        &Entry::presentationId) != m_entries.end();
}

void MissionCrewModel::replace(QVector<Entry> entries)
{
    std::ranges::sort(entries, [](const Entry& left, const Entry& right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        const auto compared = left.displayName.localeAwareCompare(
            right.displayName);
        return compared == 0 ? left.presentationId < right.presentationId
                             : compared < 0;
    });
    const auto countChangedValue = m_entries.size() != entries.size();
    const auto dispatchableAgentCountValue =
        static_cast<int>(std::ranges::count_if(
            entries,
            [](const Entry& entry) {
                return entry.kind == Kind::Agent && entry.canDispatch;
            }));
    const auto dispatchableAgentCountChangedValue =
        dispatchableAgentCount() != dispatchableAgentCountValue;
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
    if (dispatchableAgentCountChangedValue) {
        emit dispatchableAgentCountChanged();
    }
}

std::optional<MissionCrewModel::RecipientContext>
MissionCrewModel::recipientContext(const QString& presentationId) const
{
    const auto found = std::ranges::find(
        m_entries,
        presentationId,
        &Entry::presentationId);
    if (found == m_entries.end()) {
        return std::nullopt;
    }
    return RecipientContext {
        .kind = found->kind,
        .rawId = found->rawRecipientId,
        .localSessionId = found->localSessionId,
        .sessionIncarnationId = found->sessionIncarnationId,
        .assignmentSessionId = found->assignmentSessionId,
        .assignmentIncarnationId = found->assignmentIncarnationId,
        .displayName = found->displayName,
        .canDispatch = found->canDispatch,
        .canAssignTask = found->canAssignTask,
    };
}

QString MissionCrewModel::presentationForSession(
    const QString& sessionId,
    const QString& incarnationId) const
{
    const auto found = std::ranges::find_if(
        m_entries,
        [&](const Entry& entry) {
            return entry.kind == Kind::Agent
                && entry.localSessionId == sessionId
                && (incarnationId.isEmpty()
                    || entry.sessionIncarnationId == incarnationId);
        });
    return found == m_entries.end() ? QString {} : found->presentationId;
}

QString MissionCrewModel::presentationForUser(const QString& userId) const
{
    const auto found = std::ranges::find_if(
        m_entries,
        [&](const Entry& entry) {
            return entry.kind == Kind::Member
                && entry.rawRecipientId == userId;
        });
    return found == m_entries.end() ? QString {} : found->presentationId;
}

MissionScopedAttentionModel::MissionScopedAttentionModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MissionScopedAttentionModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant MissionScopedAttentionModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case ActionPresentationIdRole: return entry.actionPresentationId;
    case CategoryRole: return QVariant::fromValue(entry.category);
    case SessionPresentationIdRole: return entry.sessionPresentationId;
    case TitleRole: return entry.title;
    case SummaryRole: return entry.summary;
    case RiskRole: return QVariant::fromValue(entry.risk);
    case ToneRole: return QVariant::fromValue(entry.tone);
    case ActionKindRole: return QVariant::fromValue(entry.actionKind);
    case CanApproveRole: return entry.canApprove;
    case CanDenyRole: return entry.canDeny;
    case CanJumpRole: return entry.canJump;
    case ItemCountRole: return entry.itemCount;
    default: return {};
    }
}

QHash<int, QByteArray> MissionScopedAttentionModel::roleNames() const
{
    return {
        {ActionPresentationIdRole, QByteArrayLiteral("actionPresentationId")},
        {CategoryRole, QByteArrayLiteral("category")},
        {SessionPresentationIdRole,
         QByteArrayLiteral("sessionPresentationId")},
        {TitleRole, QByteArrayLiteral("title")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {RiskRole, QByteArrayLiteral("risk")},
        {ToneRole, QByteArrayLiteral("tone")},
        {ActionKindRole, QByteArrayLiteral("actionKind")},
        {CanApproveRole, QByteArrayLiteral("canApprove")},
        {CanDenyRole, QByteArrayLiteral("canDeny")},
        {CanJumpRole, QByteArrayLiteral("canJump")},
        {ItemCountRole, QByteArrayLiteral("itemCount")},
    };
}

int MissionScopedAttentionModel::totalCount() const noexcept
{
    return m_totalCount;
}

bool MissionScopedAttentionModel::truncated() const noexcept
{
    return m_totalCount > m_entries.size();
}

int MissionScopedAttentionModel::pageOffset() const noexcept
{
    return m_pageOffset;
}

bool MissionScopedAttentionModel::canLoadMore() const noexcept
{
    return m_pageOffset + m_entries.size() < m_totalCount;
}

bool MissionScopedAttentionModel::canLoadPrevious() const noexcept
{
    return m_pageOffset > 0;
}

bool MissionScopedAttentionModel::loadMore()
{
    return canLoadMore() && m_owner != nullptr
        && m_owner->setAttentionPageOffset(
            m_pageOffset + maximumVisibleItems);
}

bool MissionScopedAttentionModel::loadPrevious()
{
    return canLoadPrevious() && m_owner != nullptr
        && m_owner->setAttentionPageOffset(
            std::max(0, m_pageOffset - maximumVisibleItems));
}

bool MissionScopedAttentionModel::review(const QString& presentationId)
{
    return perform(
        presentationId,
        ActionKind::Review);
}

bool MissionScopedAttentionModel::jump(const QString& presentationId)
{
    return perform(
        presentationId,
        ActionKind::Jump);
}

bool MissionScopedAttentionModel::approve(const QString& presentationId)
{
    return perform(
        presentationId,
        ActionKind::Approve);
}

bool MissionScopedAttentionModel::deny(const QString& presentationId)
{
    const auto found = std::ranges::find(
        m_entries,
        presentationId,
        &Entry::actionPresentationId);
    if (found == m_entries.end() || m_owner == nullptr || !found->canDeny) {
        return false;
    }
    return m_owner->performAttentionAction(
        *found,
        ActionKind::Approve,
        true);
}

bool MissionScopedAttentionModel::approveAll(const QString& presentationId)
{
    return perform(
        presentationId,
        ActionKind::BulkApprove);
}

void MissionScopedAttentionModel::replace(
    QVector<Entry> entries,
    QHash<QString, int> sessionCounts,
    const int totalCount,
    const int pageOffset)
{
    QSet<QString> liveKeys;
    for (const auto& entry : entries) {
        liveKeys.insert(entry.authorityKey);
    }
    for (auto token = m_tokensByKey.begin(); token != m_tokensByKey.end();) {
        if (liveKeys.contains(token.key())) {
            ++token;
        } else {
            token = m_tokensByKey.erase(token);
        }
    }
    const auto countChangedValue =
        m_entries.size() != entries.size() || m_totalCount != totalCount
        || m_pageOffset != pageOffset;
    beginResetModel();
    m_entries = std::move(entries);
    m_sessionCounts = std::move(sessionCounts);
    m_totalCount = totalCount;
    m_pageOffset = pageOffset;
    endResetModel();
    if (countChangedValue) {
        emit countChanged();
    }
}

bool MissionScopedAttentionModel::perform(
    const QString& presentationId,
    const ActionKind actionKind)
{
    const auto found = std::ranges::find(
        m_entries,
        presentationId,
        &Entry::actionPresentationId);
    return found != m_entries.end() && m_owner != nullptr
        && found->actionKind == actionKind
        && m_owner->performAttentionAction(*found, actionKind);
}

int MissionScopedAttentionModel::countForSession(const QString& sessionId) const
{
    return m_sessionCounts.value(sessionId);
}

MissionDetailModel::MissionDetailModel(
    CommandDispatcher& dispatcher,
    MissionDirectoryModel& directory,
    const qint64 hydrationTimeoutMs,
    QObject* parent)
    : MissionDetailModel(
        dispatcher,
        directory,
        Dependencies {},
        hydrationTimeoutMs,
        parent)
{
}

MissionDetailModel::MissionDetailModel(
    CommandDispatcher& dispatcher,
    MissionDirectoryModel& directory,
    Dependencies dependencies,
    const qint64 hydrationTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_directory(directory)
    , m_dependencies(dependencies)
    , m_members(this)
    , m_messages(this)
    , m_tasks(this)
    , m_crew(this)
    , m_attention(this)
    , m_hydrationTimeoutMs(hydrationTimeoutMs)
{
    Q_ASSERT(hydrationTimeoutMs >= 0);
    m_attention.m_owner = this;
    m_hydrationTimer.setSingleShot(true);
    connect(&m_hydrationTimer, &QTimer::timeout, this, [this] {
        hydrationTimedOut();
    });
    connect(&directory, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_missionId.isEmpty() && !m_directory.containsMission(m_missionId)) {
            closeMission();
        }
    });
    const auto rebuild = [this] {
        if (!m_missionId.isEmpty()) {
            rebuildProjections();
        }
    };
    for (auto* model : {
             static_cast<QAbstractItemModel*>(m_dependencies.people),
             static_cast<QAbstractItemModel*>(m_dependencies.sessions),
             static_cast<QAbstractItemModel*>(m_dependencies.attention),
         }) {
        if (model == nullptr) {
            continue;
        }
        connect(model, &QAbstractItemModel::modelReset, this, rebuild);
        connect(model, &QAbstractItemModel::rowsInserted, this, rebuild);
        connect(model, &QAbstractItemModel::rowsRemoved, this, rebuild);
        connect(model, &QAbstractItemModel::dataChanged, this, rebuild);
    }
    if (m_dependencies.attention != nullptr) {
        connect(
            m_dependencies.attention,
            &AttentionModel::stateChanged,
            this,
            rebuild);
    }
    if (m_dependencies.sessions != nullptr) {
        connect(
            m_dependencies.sessions,
            &SessionCatalogModel::authorityStateChanged,
            this,
            rebuild);
    }
    if (m_dependencies.sessionActions != nullptr) {
        connect(
            m_dependencies.sessionActions,
            &SessionActions::availabilityChanged,
            this,
            rebuild);
    }
}

QString MissionDetailModel::missionId() const
{
    return m_missionId;
}

bool MissionDetailModel::loading() const noexcept
{
    return m_membersLoading || m_chatLoading || m_tasksLoading;
}

bool MissionDetailModel::membersReady() const noexcept
{
    return !m_missionId.isEmpty() && m_membersAuthoritative;
}

bool MissionDetailModel::tasksReady() const noexcept
{
    return !m_missionId.isEmpty() && m_tasksAuthoritative;
}

QString MissionDetailModel::lastError() const
{
    return m_lastError;
}

MissionMembersModel* MissionDetailModel::members() noexcept
{
    return &m_members;
}

MissionMessagesModel* MissionDetailModel::messages() noexcept
{
    return &m_messages;
}

MissionTasksModel* MissionDetailModel::tasks() noexcept
{
    return &m_tasks;
}

MissionCrewModel* MissionDetailModel::crew() noexcept
{
    return &m_crew;
}

MissionScopedAttentionModel* MissionDetailModel::attention() noexcept
{
    return &m_attention;
}

QString MissionDetailModel::selectedCrewPresentationId() const
{
    return m_selectedCrewPresentationId;
}

QString MissionDetailModel::selectedSessionDisplayName() const
{
    const auto entry = selectedCrewEntry();
    return entry ? entry->displayName : QString {};
}

QString MissionDetailModel::selectedSessionStatus() const
{
    const auto entry = selectedCrewEntry();
    return entry ? entry->status : QString {};
}

QString MissionDetailModel::selectedTerminalSessionId() const
{
    const auto entry = selectedCrewEntry();
    return entry && entry->kind == MissionCrewModel::Kind::Agent
        ? entry->localSessionId
        : QString {};
}

bool MissionDetailModel::selectedCanOpenFullTerminal() const
{
    const auto entry = selectedCrewEntry();
    return entry && entry->kind == MissionCrewModel::Kind::Agent
        && entry->canOpenFullTerminal;
}

bool MissionDetailModel::selectedCanToggleDispatch() const
{
    const auto entry = selectedCrewEntry();
    return entry && entry->kind == MissionCrewModel::Kind::Agent
        && entry->canDispatch;
}

bool MissionDetailModel::selectedCanSteer() const
{
    const auto entry = selectedCrewEntry();
    return entry && entry->kind == MissionCrewModel::Kind::Agent
        && entry->canSteer;
}

bool MissionDetailModel::selectedCanInterrupt() const
{
    const auto entry = selectedCrewEntry();
    return entry && entry->kind == MissionCrewModel::Kind::Agent
        && entry->canInterrupt;
}

bool MissionDetailModel::openMission(const QString& missionId)
{
    if (!m_authenticated || !m_directory.containsMission(missionId)) {
        return false;
    }
    clearDetail();
    m_missionId = missionId;
    rebuildProjections();
    (void)beginFullHydration();
    return true;
}

bool MissionDetailModel::retry()
{
    if (!m_authenticated || m_missionId.isEmpty()
        || !m_directory.containsMission(m_missionId)) {
        return false;
    }
    return beginFullHydration();
}

bool MissionDetailModel::beginFullHydration()
{
    m_hydrationTimer.stop();
    m_staleMessageIds.clear();
    for (const auto& message : std::as_const(m_messages.m_messages)) {
        m_staleMessageIds.insert(message.id);
    }
    m_staleTaskIds.clear();
    for (const auto& task : std::as_const(m_tasks.m_tasks)) {
        m_staleTaskIds.insert(task.id);
    }
    m_memberHydrationId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_chatHydrationId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_taskLoad = TaskLoad {
        .token = QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .nextOffset = 0,
        .pageCount = 0,
        .tasks = {},
    };
    m_membersLoading = true;
    m_membersAuthoritative = false;
    m_chatLoading = true;
    m_chatAuthoritative = false;
    m_tasksLoading = true;
    m_tasksAuthoritative = false;
    rebuildProjections();
    emit stateChanged();

    const auto membersAccepted = m_dispatcher.send(
        CommandLane::Rooms,
        commandJson({
            {QStringLiteral("type"), QStringLiteral("room.refreshMembers")},
            {QStringLiteral("room_id"), m_missionId},
            {QStringLiteral("hydration_id"), m_memberHydrationId},
        }));
    const auto chatAccepted = m_dispatcher.send(
        CommandLane::Rooms,
        commandJson({
            {QStringLiteral("type"), QStringLiteral("room.chat.list")},
            {QStringLiteral("room_id"), m_missionId},
            {QStringLiteral("limit"), static_cast<qint64>(maximumMessages)},
            {QStringLiteral("tail"), true},
            {QStringLiteral("hydration_id"), m_chatHydrationId},
        }));
    const auto tasksAccepted = requestTaskPage();
    if (!membersAccepted) {
        m_memberHydrationId.clear();
        m_membersLoading = false;
    }
    if (!chatAccepted) {
        m_chatHydrationId.clear();
        m_chatLoading = false;
    }
    if (!tasksAccepted) {
        m_taskLoad.reset();
        m_tasksLoading = false;
    }
    if (membersAccepted || chatAccepted || tasksAccepted) {
        m_hydrationTimer.start(static_cast<int>(m_hydrationTimeoutMs));
    }
    const auto accepted =
        membersAccepted && chatAccepted && tasksAccepted;
    if (!accepted) {
        m_lastError = QStringLiteral(
            "The runtime did not accept all Mission detail hydration requests.");
    }
    updateLoading();
    return accepted;
}

bool MissionDetailModel::refreshTasks()
{
    if (!m_authenticated || m_missionId.isEmpty()
        || !m_directory.containsMission(m_missionId)) {
        return false;
    }
    m_taskLoad = TaskLoad {
        .token = QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .nextOffset = 0,
        .pageCount = 0,
        .tasks = {},
    };
    m_staleTaskIds.clear();
    for (const auto& task : std::as_const(m_tasks.m_tasks)) {
        m_staleTaskIds.insert(task.id);
    }
    m_tasksLoading = true;
    m_tasksAuthoritative = false;
    emit stateChanged();
    if (requestTaskPage()) {
        m_hydrationTimer.start(static_cast<int>(m_hydrationTimeoutMs));
        return true;
    }
    m_taskLoad.reset();
    m_tasksLoading = false;
    m_lastError =
        QStringLiteral("The runtime did not accept Mission task hydration.");
    emit stateChanged();
    return false;
}

void MissionDetailModel::closeMission()
{
    clearDetail();
    emit stateChanged();
}

bool MissionDetailModel::selectCrew(const QString& presentationId)
{
    const auto context = m_crew.recipientContext(presentationId);
    if (!context || context->kind != MissionCrewModel::Kind::Agent
        || context->localSessionId.isEmpty()
        || context->sessionIncarnationId.isEmpty()) {
        return false;
    }
    m_selectedCrewPresentationId = presentationId;
    m_selectedSessionId = context->localSessionId;
    m_selectedSessionIncarnationId = context->sessionIncarnationId;
    if (m_dependencies.steering != nullptr) {
        (void)m_dependencies.steering->inspect(context->localSessionId);
    }
    emit focusChanged();
    return true;
}

void MissionDetailModel::clearFocus()
{
    if (m_selectedCrewPresentationId.isEmpty()) {
        return;
    }
    m_selectedCrewPresentationId.clear();
    m_selectedSessionId.clear();
    m_selectedSessionIncarnationId.clear();
    if (m_dependencies.steering != nullptr) {
        m_dependencies.steering->clearInspection();
    }
    emit focusChanged();
}

bool MissionDetailModel::openFocusedFullTerminal()
{
    const auto entry = selectedCrewEntry();
    if (!entry || !entry->canOpenFullTerminal
        || m_dependencies.desktopState == nullptr
        || !m_dependencies.desktopState->selectSession(
            entry->localSessionId)) {
        return false;
    }
    m_dependencies.desktopState->setActiveView(0);
    return true;
}

bool MissionDetailModel::toggleFocusedDispatch()
{
    const auto entry = selectedCrewEntry();
    return entry && entry->canDispatch && m_actions != nullptr
        && m_actions->toggleChatRecipient(entry->presentationId);
}

bool MissionDetailModel::steerFocused(const QString& text)
{
    const auto entry = selectedCrewEntry();
    if (!entry || !entry->canSteer || m_dependencies.steering == nullptr
        || text.trimmed().isEmpty()) {
        return false;
    }
    auto* steering = m_dependencies.steering;
    if (!steering->inspect(entry->localSessionId)
        || !steering->saveDraft(
            entry->localSessionId,
            text,
            QStringLiteral("steer"))) {
        return false;
    }
    return steering->send(entry->localSessionId);
}

bool MissionDetailModel::interruptFocused()
{
    const auto entry = selectedCrewEntry();
    return entry && entry->canInterrupt
        && m_dependencies.sessionActions != nullptr
        && m_dependencies.sessionActions->interrupt(entry->localSessionId);
}

void MissionDetailModel::ingestAuthEvent(QByteArray json)
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

void MissionDetailModel::ingestRoomEvent(QByteArray json)
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

void MissionDetailModel::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_authenticated = false;
    m_accountUserId.clear();
    m_deliveries.clear();
    m_deliveryOrder.clear();
    m_crew.m_tokensByKey.clear();
    m_crew.m_tokenOrder.clear();
    closeMission();
}

void MissionDetailModel::activateAccount(
    QString userId,
    const quint64 epoch,
    const bool authenticated)
{
    auto activation = m_accountFence.activate({
        .userId = userId,
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    if (activation.changed || m_authenticated != authenticated) {
        clearDetail();
        m_deliveries.clear();
        m_deliveryOrder.clear();
        m_crew.m_tokensByKey.clear();
        m_crew.m_tokenOrder.clear();
    }
    m_authenticated = authenticated;
    m_accountUserId = authenticated ? std::move(userId) : QString {};
    for (auto& pending : activation.pendingEvents) {
        ingestRoomEvent(std::move(pending));
    }
    emit stateChanged();
}

void MissionDetailModel::applyRoomEvent(const QJsonObject& object)
{
    const auto type = requiredString(object, QStringLiteral("type"));
    if (!type) {
        emit decodeError(QStringLiteral("Mission event has no type."));
        return;
    }
    if (*type == QStringLiteral("room.agent.delivery")) {
        const auto sessionId =
            requiredString(object, QStringLiteral("session_id"));
        const auto incarnationId =
            requiredString(object, QStringLiteral("session_incarnation_id"));
        const auto state = requiredString(object, QStringLiteral("state"));
        if (!sessionId || !incarnationId || !state
            || !knownDeliveryState(*state)
            || m_dependencies.sessions == nullptr) {
            emit decodeError(QStringLiteral("Mission delivery event is invalid."));
            return;
        }
        const auto context =
            m_dependencies.sessions->actionContext(*sessionId);
        if (!context || context->incarnationId != *incarnationId
            || context->scope != QStringLiteral("room")
            || context->roomId != m_missionId
            || !liveMissionSession(context->status)) {
            return;
        }
        for (auto delivery = m_deliveries.begin();
             delivery != m_deliveries.end();) {
            if (delivery->sessionId == *sessionId) {
                m_deliveryOrder.removeAll(delivery.key());
                delivery = m_deliveries.erase(delivery);
            } else {
                ++delivery;
            }
        }
        const auto key = *sessionId + QChar::Null + *incarnationId;
        m_deliveries.insert(key, {
            .sessionId = *sessionId,
            .incarnationId = *incarnationId,
            .state = *state,
            .detail = optionalString(object, QStringLiteral("detail")),
        });
        m_deliveryOrder.removeAll(key);
        m_deliveryOrder.push_back(key);
        while (m_deliveryOrder.size() > maximumDeliveries) {
            m_deliveries.remove(m_deliveryOrder.takeFirst());
        }
        rebuildCrew();
        reconcileFocus();
        return;
    }
    const auto roomId = optionalString(object, QStringLiteral("room_id"));
    if ((*type == QStringLiteral("room.members")
            || *type == QStringLiteral("room.chat.snapshot")
            || *type == QStringLiteral("room.chat.posted")
            || *type == QStringLiteral("room.tasks.page")
            || *type == QStringLiteral("room.tasks.snapshot")
            || *type == QStringLiteral("room.tasks.upserted"))
        && (roomId.isEmpty() || roomId != m_missionId)) {
        return;
    }
    if (*type == QStringLiteral("room.members")) {
        const auto values = object.value(QStringLiteral("members"));
        const auto hydration =
            optionalString(object, QStringLiteral("hydration_id"));
        if ((!m_memberHydrationId.isEmpty() && hydration != m_memberHydrationId)
            || (m_memberHydrationId.isEmpty() && !hydration.isEmpty())) {
            return;
        }
        if (!values.isArray()) {
            emit decodeError(QStringLiteral("Mission roster is invalid."));
            return;
        }
        QVector<MissionMembersModel::Member> members;
        QSet<QString> ids;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()) {
                emit decodeError(QStringLiteral("Mission roster is invalid."));
                return;
            }
            auto member = decodeMember(value.toObject(), m_missionId);
            if (!member || ids.contains(member->userId)) {
                emit decodeError(QStringLiteral("Mission roster is invalid."));
                return;
            }
            ids.insert(member->userId);
            members.push_back(std::move(*member));
        }
        m_members.replace(std::move(members));
        m_membersAuthoritative = true;
        if (!hydration.isEmpty()) {
            m_memberHydrationId.clear();
            m_membersLoading = false;
        }
        rebuildProjections();
        updateLoading();
        return;
    }
    if (*type == QStringLiteral("room.chat.snapshot")) {
        const auto values = object.value(QStringLiteral("messages"));
        const auto hydration =
            optionalString(object, QStringLiteral("hydration_id"));
        if ((!m_chatHydrationId.isEmpty() && hydration != m_chatHydrationId)
            || (m_chatHydrationId.isEmpty() && !hydration.isEmpty())) {
            return;
        }
        if (!values.isArray()) {
            emit decodeError(QStringLiteral("Mission chat is invalid."));
            return;
        }
        QVector<MissionMessagesModel::Message> messages;
        QSet<QString> ids;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()) {
                emit decodeError(QStringLiteral("Mission chat is invalid."));
                return;
            }
            auto message = decodeMessage(value.toObject(), m_missionId);
            if (!message || ids.contains(message->id)) {
                emit decodeError(QStringLiteral("Mission chat is invalid."));
                return;
            }
            ids.insert(message->id);
            messages.push_back(std::move(*message));
        }
        for (const auto& live : m_messages.m_messages) {
            if (m_staleMessageIds.contains(live.id)) {
                continue;
            }
            const auto found =
                std::ranges::find(messages, live.id, &MissionMessagesModel::Message::id);
            if (found == messages.end()) {
                messages.push_back(live);
            } else if (live.sequence >= found->sequence) {
                *found = live;
            }
        }
        m_messages.replace(std::move(messages));
        m_chatAuthoritative = true;
        m_staleMessageIds.clear();
        if (!hydration.isEmpty()) {
            m_chatHydrationId.clear();
            m_chatLoading = false;
        }
        rebuildMessagePresentation();
        updateLoading();
        return;
    }
    if (*type == QStringLiteral("room.chat.posted")) {
        const auto value = object.value(QStringLiteral("message"));
        if (!value.isObject()) {
            emit decodeError(QStringLiteral("Mission chat update is invalid."));
            return;
        }
        auto message = decodeMessage(value.toObject(), m_missionId);
        if (!message) {
            emit decodeError(QStringLiteral("Mission chat update is invalid."));
            return;
        }
        m_staleMessageIds.remove(message->id);
        m_messages.upsert(std::move(*message));
        rebuildMessagePresentation();
        return;
    }
    if (*type == QStringLiteral("room.tasks.page")) {
        if (!m_taskLoad) {
            return;
        }
        const auto hydration =
            optionalString(object, QStringLiteral("hydration_id"));
        const auto requestOffset =
            integer(object.value(QStringLiteral("request_offset")), true);
        if (hydration != m_taskLoad->token || !requestOffset
            || *requestOffset != m_taskLoad->nextOffset) {
            return;
        }
        const auto hasMore = object.value(QStringLiteral("has_more"));
        const auto values = object.value(QStringLiteral("tasks"));
        if (!hasMore.isBool() || !values.isArray()
            || values.toArray().size() > taskPageSize
            || m_taskLoad->pageCount >= maximumTaskPages
            || m_taskLoad->tasks.size()
                > maximumTasks - values.toArray().size()) {
            emit decodeError(QStringLiteral("Mission task page is invalid."));
            m_tasksLoading = false;
            m_taskLoad.reset();
            updateLoading();
            return;
        }
        QVector<MissionTasksModel::Task> decodedPage;
        QSet<QString> pageIds;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()) {
                emit decodeError(QStringLiteral("Mission task page is invalid."));
                return;
            }
            auto task = decodeTask(value.toObject(), m_missionId);
            if (!task || pageIds.contains(task->id)) {
                emit decodeError(QStringLiteral("Mission task page is invalid."));
                return;
            }
            pageIds.insert(task->id);
            decodedPage.push_back(std::move(*task));
        }
        auto accumulated = m_taskLoad->tasks;
        for (auto& task : decodedPage) {
            const auto prior = std::ranges::find(
                accumulated,
                task.id,
                &MissionTasksModel::Task::id);
            if (prior == accumulated.end()) {
                accumulated.push_back(std::move(task));
            } else if (task.revision >= prior->revision) {
                *prior = std::move(task);
            }
        }
        m_taskLoad->tasks = std::move(accumulated);
        ++m_taskLoad->pageCount;
        if (hasMore.toBool()) {
            if (m_taskLoad->pageCount >= maximumTaskPages
                || m_taskLoad->tasks.size() >= maximumTasks) {
                emit decodeError(
                    QStringLiteral("Mission task pagination exceeds its bound."));
                m_lastError =
                    QStringLiteral("Mission task history exceeds the supported bound.");
                m_tasksLoading = false;
                m_taskLoad.reset();
                updateLoading();
                return;
            }
            const auto nextOffset =
                integer(object.value(QStringLiteral("next_offset")), true);
            if (!nextOffset || *nextOffset <= m_taskLoad->nextOffset
                || *nextOffset
                    != m_taskLoad->nextOffset + values.toArray().size()) {
                emit decodeError(QStringLiteral("Mission task pagination is invalid."));
                m_tasksLoading = false;
                m_taskLoad.reset();
                updateLoading();
                return;
            }
            m_taskLoad->nextOffset = *nextOffset;
            if (!requestTaskPage()) {
                m_lastError =
                    QStringLiteral("The runtime rejected Mission task pagination.");
                m_tasksLoading = false;
                m_taskLoad.reset();
                updateLoading();
            }
            return;
        }
        if (!object.value(QStringLiteral("next_offset")).isUndefined()
            && !object.value(QStringLiteral("next_offset")).isNull()) {
            emit decodeError(QStringLiteral("Mission task pagination is invalid."));
            return;
        }
        auto tasks = std::move(m_taskLoad->tasks);
        for (const auto& live : m_tasks.m_tasks) {
            if (m_staleTaskIds.contains(live.id)) {
                continue;
            }
            const auto found =
                std::ranges::find(tasks, live.id, &MissionTasksModel::Task::id);
            if (found == tasks.end()) {
                tasks.push_back(live);
            } else if (live.revision > found->revision) {
                *found = live;
            }
        }
        m_tasks.replace(std::move(tasks));
        m_staleTaskIds.clear();
        m_taskLoad.reset();
        m_tasksLoading = false;
        m_tasksAuthoritative = true;
        rebuildTaskPresentation();
        updateLoading();
        return;
    }
    if (*type == QStringLiteral("room.tasks.snapshot")) {
        const auto values = object.value(QStringLiteral("tasks"));
        if (m_taskLoad) {
            return;
        }
        if (!values.isArray() || values.toArray().size() > maximumTasks) {
            emit decodeError(QStringLiteral("Mission task snapshot is invalid."));
            return;
        }
        QVector<MissionTasksModel::Task> tasks;
        QSet<QString> ids;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()) {
                emit decodeError(QStringLiteral("Mission task snapshot is invalid."));
                return;
            }
            auto task = decodeTask(value.toObject(), m_missionId);
            if (!task || ids.contains(task->id)) {
                emit decodeError(QStringLiteral("Mission task snapshot is invalid."));
                return;
            }
            ids.insert(task->id);
            tasks.push_back(std::move(*task));
        }
        for (const auto& live : m_tasks.m_tasks) {
            const auto found =
                std::ranges::find(tasks, live.id, &MissionTasksModel::Task::id);
            if (found != tasks.end() && live.revision > found->revision) {
                *found = live;
            }
        }
        m_tasks.replace(std::move(tasks));
        m_staleTaskIds.clear();
        m_tasksAuthoritative = true;
        rebuildTaskPresentation();
        updateLoading();
        return;
    }
    if (*type == QStringLiteral("room.tasks.upserted")) {
        const auto value = object.value(QStringLiteral("task"));
        if (!value.isObject()) {
            emit decodeError(QStringLiteral("Mission task update is invalid."));
            return;
        }
        auto task = decodeTask(value.toObject(), m_missionId);
        if (!task) {
            emit decodeError(QStringLiteral("Mission task update is invalid."));
            return;
        }
        m_staleTaskIds.remove(task->id);
        m_tasks.upsert(std::move(*task));
        rebuildTaskPresentation();
        return;
    }
    if (*type == QStringLiteral("room.error")) {
        const auto errorRoomId = optionalString(object, QStringLiteral("room_id"));
        const auto operation = requiredString(object, QStringLiteral("operation"));
        const auto message = requiredString(object, QStringLiteral("message"));
        if (errorRoomId != m_missionId || !operation || !message) {
            return;
        }
        if (*operation != QStringLiteral("refreshMembers")
            && *operation != QStringLiteral("chat.list")
            && *operation != QStringLiteral("tasks.list")) {
            return;
        }
        m_lastError = *message;
        emit stateChanged();
    }
}

void MissionDetailModel::clearDetail()
{
    m_hydrationTimer.stop();
    m_missionId.clear();
    m_memberHydrationId.clear();
    m_chatHydrationId.clear();
    m_taskLoad.reset();
    m_lastError.clear();
    m_membersLoading = false;
    m_membersAuthoritative = false;
    m_chatLoading = false;
    m_chatAuthoritative = false;
    m_tasksLoading = false;
    m_tasksAuthoritative = false;
    m_dispatchSelection.clear();
    m_staleMessageIds.clear();
    m_staleTaskIds.clear();
    m_members.replace({});
    m_messages.replace({});
    m_tasks.replace({});
    m_crew.replace({});
    m_attention.replace({}, {}, 0, 0);
    clearFocus();
}

void MissionDetailModel::updateLoading()
{
    if (!loading()) {
        m_hydrationTimer.stop();
        if (m_membersAuthoritative && m_chatAuthoritative
            && m_tasksAuthoritative) {
            m_lastError.clear();
        }
    }
    emit stateChanged();
}

void MissionDetailModel::hydrationTimedOut()
{
    if (!loading()) {
        return;
    }
    m_memberHydrationId.clear();
    m_chatHydrationId.clear();
    m_taskLoad.reset();
    m_membersLoading = false;
    m_chatLoading = false;
    m_tasksLoading = false;
    m_staleMessageIds.clear();
    m_staleTaskIds.clear();
    m_lastError =
        QStringLiteral("Mission detail synchronization timed out. Retry the Mission.");
    emit stateChanged();
}

bool MissionDetailModel::requestTaskPage()
{
    if (!m_taskLoad) {
        return false;
    }
    return m_dispatcher.send(
        CommandLane::Rooms,
        commandJson({
            {QStringLiteral("type"), QStringLiteral("room.tasks.list")},
            {QStringLiteral("room_id"), m_missionId},
            {QStringLiteral("offset"),
             static_cast<qint64>(m_taskLoad->nextOffset)},
            {QStringLiteral("limit"), static_cast<qint64>(taskPageSize)},
            {QStringLiteral("hydration_id"), m_taskLoad->token},
        })).has_value();
}

void MissionDetailModel::rebuildProjections()
{
    if (m_missionId.isEmpty()) {
        return;
    }
    rebuildCrew();
    rebuildAttention();
    rebuildCrew();
    rebuildMessagePresentation();
    rebuildTaskPresentation();
    reconcileFocus();
}

void MissionDetailModel::rebuildAttention()
{
    if (m_dependencies.attention == nullptr || m_missionId.isEmpty()) {
        m_attention.replace({}, {}, 0, 0);
        return;
    }
    const auto sessionIds = currentMissionSessionIds();
    auto scoped = m_dependencies.attention->scopedItemPage(
        sessionIds,
        m_attention.m_pageOffset,
        MissionScopedAttentionModel::maximumVisibleItems);
    if (scoped.totalCount > 0
        && scoped.items.isEmpty()
        && m_attention.m_pageOffset >= scoped.totalCount) {
        m_attention.m_pageOffset =
            ((scoped.totalCount - 1)
                / MissionScopedAttentionModel::maximumVisibleItems)
            * MissionScopedAttentionModel::maximumVisibleItems;
        scoped = m_dependencies.attention->scopedItemPage(
            sessionIds,
            m_attention.m_pageOffset,
            MissionScopedAttentionModel::maximumVisibleItems);
    }
    QVector<MissionScopedAttentionModel::Entry> entries;
    entries.reserve(std::min(
        scoped.items.size(),
        static_cast<qsizetype>(
            MissionScopedAttentionModel::maximumVisibleItems)));
    for (const auto& item : scoped.items) {
        if (entries.size()
            >= MissionScopedAttentionModel::maximumVisibleItems) {
            break;
        }
        const auto key = item.sourceToken + QChar::Null
            + item.sessionIds.join(QChar::Null);
        auto& token = m_attention.m_tokensByKey[key];
        if (token.isEmpty()) {
            token = QStringLiteral("mission-attention-")
                + QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        }
        entries.push_back({
            .authorityKey = key,
            .actionPresentationId = token,
            .sourceToken = item.sourceToken,
            .sessionPresentationId = item.sessionId.isEmpty()
                ? QString {}
                : m_crew.presentationForSession(item.sessionId),
            .sourceSessionIds = item.sessionIds,
            .sourceSessionCounts = item.sessionCounts,
            .category =
                static_cast<MissionScopedAttentionModel::Category>(
                    item.category),
            .risk = static_cast<MissionScopedAttentionModel::Risk>(
                item.risk),
            .tone = static_cast<MissionScopedAttentionModel::Tone>(
                item.tone),
            .actionKind =
                static_cast<MissionScopedAttentionModel::ActionKind>(
                    item.actionKind),
            .title = item.title,
            .summary = item.summary,
            .canApprove = item.canApprove,
            .canDeny = item.canDeny,
            .canJump = item.canJump,
            .itemCount = item.itemCount,
        });
    }
    m_attention.replace(
        std::move(entries),
        std::move(scoped.sessionCounts),
        scoped.totalCount,
        m_attention.m_pageOffset);
}

bool MissionDetailModel::setAttentionPageOffset(const int offset)
{
    if (offset < 0 || m_dependencies.attention == nullptr
        || m_missionId.isEmpty()
        || offset == m_attention.m_pageOffset) {
        return false;
    }
    m_attention.m_pageOffset = offset;
    rebuildAttention();
    rebuildCrew();
    return true;
}

void MissionDetailModel::rebuildCrew()
{
    if (m_missionId.isEmpty()) {
        m_crew.replace({});
        return;
    }
    QVector<MissionCrewModel::Entry> entries;
    if (m_dependencies.sessions != nullptr) {
        for (auto row = 0; row < m_dependencies.sessions->rowCount(); ++row) {
            const auto index = m_dependencies.sessions->index(row);
            const auto sessionId = m_dependencies.sessions
                                       ->data(
                                           index,
                                           SessionCatalogModel::SessionIdRole)
                                       .toString();
            const auto context =
                m_dependencies.sessions->actionContext(sessionId);
            if (!context || context->scope != QStringLiteral("room")
                || context->roomId != m_missionId
                || context->incarnationId.isEmpty()
                || !liveMissionSession(context->status)) {
                continue;
            }
            const auto key = m_missionId + QChar::Null
                + QStringLiteral("agent") + QChar::Null + sessionId
                + QChar::Null + context->incarnationId;
            auto& presentationId = m_crew.m_tokensByKey[key];
            if (presentationId.isEmpty()) {
                presentationId = QStringLiteral("crew-")
                    + QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            }
            m_crew.m_tokenOrder.removeAll(key);
            m_crew.m_tokenOrder.push_back(key);
            const auto delivery = m_deliveries.constFind(
                sessionId + QChar::Null + context->incarnationId);
            const auto presentation =
                m_dependencies.sessions->presentationSession(sessionId);
            entries.push_back({
                .authorityKey = key,
                .presentationId = presentationId,
                .kind = MissionCrewModel::Kind::Agent,
                .displayName = m_dependencies.sessions
                                   ->data(
                                       index,
                                       SessionCatalogModel::NameRole)
                                   .toString(),
                .secondaryLabel = m_dependencies.sessions
                                      ->data(
                                          index,
                                          SessionCatalogModel::ModeRole)
                                      .toString(),
                .status = context->status,
                .localSessionId = sessionId,
                .sessionIncarnationId = context->incarnationId,
                .rawRecipientId = sessionId,
                .assignmentSessionId = context->assignmentSessionId,
                .assignmentIncarnationId =
                    context->assignmentIncarnationId,
                .deliveryState = delivery == m_deliveries.cend()
                    ? QString {}
                    : delivery->state,
                .deliveryDetail = delivery == m_deliveries.cend()
                    ? QString {}
                    : delivery->detail,
                .canOpenFullTerminal =
                    presentation && presentation->canRetainPresentation,
                .canDispatch = context->commandable,
                .canAssignTask = context->commandable
                    && !context->assignmentSessionId.isEmpty()
                    && !context->assignmentIncarnationId.isEmpty(),
                .canSteer = context->commandable && context->canSteer,
                .canInterrupt = m_dependencies.sessionActions != nullptr
                    && m_dependencies.sessionActions->canInterrupt(sessionId),
                .dispatchSelected =
                    m_dispatchSelection.contains(presentationId),
                .attentionCount = m_attention.countForSession(sessionId),
            });
        }
    }
    if (m_membersAuthoritative) {
        for (const auto& member : std::as_const(m_members.m_members)) {
            const auto key = m_missionId + QChar::Null
                + QStringLiteral("member") + QChar::Null + member.userId;
            auto& presentationId = m_crew.m_tokensByKey[key];
            if (presentationId.isEmpty()) {
                presentationId = QStringLiteral("crew-")
                    + QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            }
            m_crew.m_tokenOrder.removeAll(key);
            m_crew.m_tokenOrder.push_back(key);
            entries.push_back({
                .authorityKey = key,
                .presentationId = presentationId,
                .kind = MissionCrewModel::Kind::Member,
                .displayName = displayForUser(member.userId),
                .secondaryLabel = member.role,
                .status = QStringLiteral("member"),
                .localSessionId = {},
                .sessionIncarnationId = {},
                .rawRecipientId = member.userId,
                .assignmentSessionId = {},
                .assignmentIncarnationId = {},
                .deliveryState = {},
                .deliveryDetail = {},
                .canOpenFullTerminal = false,
                .canDispatch = member.userId != m_accountUserId,
                .canAssignTask = false,
                .canSteer = false,
                .canInterrupt = false,
                .dispatchSelected =
                    m_dispatchSelection.contains(presentationId),
                .attentionCount = 0,
            });
        }
    }
    constexpr qsizetype maximumCrewPresentationTokens = 8'192;
    while (m_crew.m_tokenOrder.size() > maximumCrewPresentationTokens) {
        m_crew.m_tokensByKey.remove(m_crew.m_tokenOrder.takeFirst());
    }
    m_crew.replace(std::move(entries));
}

void MissionDetailModel::rebuildMessagePresentation()
{
    auto messages = m_messages.m_messages;
    for (auto& message : messages) {
        message.authorDisplay = !message.authorSessionId.isEmpty()
            ? displayForSession(message.authorSessionId)
            : displayForUser(message.authorUserId);
        if (message.authorDisplay.isEmpty()) {
            message.authorDisplay =
                message.authorKind == QStringLiteral("Agent")
                ? tr("Mission agent")
                : tr("Mission member");
        }
        message.recipientPresentationIds.clear();
        QStringList recipientNames;
        int unavailable = 0;
        const auto addRecipient =
            [&](const QString& presentationId, const QString& displayName) {
                if (presentationId.isEmpty()) {
                    ++unavailable;
                    return;
                }
                if (!message.recipientPresentationIds.contains(
                        presentationId)) {
                    message.recipientPresentationIds.push_back(
                        presentationId);
                }
                if (!displayName.isEmpty()
                    && !recipientNames.contains(displayName)) {
                    recipientNames.push_back(displayName);
                }
            };
        for (const auto& sessionId : message.recipientSessionIds) {
            auto found = std::ranges::find_if(
                m_crew.m_entries,
                [&](const MissionCrewModel::Entry& entry) {
                    return entry.kind == MissionCrewModel::Kind::Agent
                        && (entry.rawRecipientId == sessionId
                            || entry.assignmentSessionId == sessionId);
                });
            addRecipient(
                found == m_crew.m_entries.end()
                    ? QString {}
                    : found->presentationId,
                found == m_crew.m_entries.end()
                    ? QString {}
                    : found->displayName);
        }
        for (const auto& userId : message.recipientUserIds) {
            const auto presentationId =
                m_crew.presentationForUser(userId);
            addRecipient(presentationId, displayForUser(userId));
        }
        if (message.recipientSessionIds.isEmpty()
            && message.recipientUserIds.isEmpty()) {
            message.audienceSummary = tr("Everyone in this Mission");
        } else {
            std::ranges::sort(recipientNames);
            const auto shown = recipientNames.sliced(
                0,
                std::min<qsizetype>(recipientNames.size(), 3));
            message.audienceSummary = shown.join(QStringLiteral(", "));
            const auto remaining =
                recipientNames.size() - shown.size() + unavailable;
            if (remaining > 0) {
                if (!message.audienceSummary.isEmpty()) {
                    message.audienceSummary += QStringLiteral(" + ");
                }
                message.audienceSummary += tr("%1 unavailable or additional")
                                               .arg(remaining);
            }
        }
    }
    m_messages.replace(std::move(messages));
}

void MissionDetailModel::rebuildTaskPresentation()
{
    auto tasks = m_tasks.m_tasks;
    for (auto& task : tasks) {
        task.knownStatus = knownTaskStatus(task.status);
        task.statusLabel = statusLabel(task.status);
        task.assignmentDisplay.clear();
        if (!task.assignedSessionId.isEmpty()
            && !task.assignedSessionIncarnationId.isEmpty()) {
            const auto found = std::ranges::find_if(
                m_crew.m_entries,
                [&](const MissionCrewModel::Entry& entry) {
                    return entry.kind == MissionCrewModel::Kind::Agent
                        && entry.assignmentSessionId
                            == task.assignedSessionId
                        && entry.assignmentIncarnationId
                            == task.assignedSessionIncarnationId;
                });
            task.assignmentDisplay =
                found == m_crew.m_entries.end()
                ? tr("Unavailable Mission agent")
                : found->displayName;
        } else {
            task.assignmentDisplay = tr("Unassigned");
        }
        task.resultAuthorDisplay =
            displayForUser(task.resultAuthorUserId);
    }
    m_tasks.replace(std::move(tasks));
}

void MissionDetailModel::reconcileFocus()
{
    if (m_selectedCrewPresentationId.isEmpty()) {
        return;
    }
    const auto context =
        m_crew.recipientContext(m_selectedCrewPresentationId);
    if (!context || context->kind != MissionCrewModel::Kind::Agent
        || context->localSessionId != m_selectedSessionId
        || context->sessionIncarnationId != m_selectedSessionIncarnationId) {
        clearFocus();
        return;
    }
    emit focusChanged();
}

void MissionDetailModel::setDispatchSelection(
    QSet<QString> presentationIds)
{
    for (auto id = presentationIds.begin(); id != presentationIds.end();) {
        const auto context = m_crew.recipientContext(*id);
        if (!context || !context->canDispatch) {
            id = presentationIds.erase(id);
        } else {
            ++id;
        }
    }
    if (m_dispatchSelection == presentationIds) {
        return;
    }
    m_dispatchSelection = std::move(presentationIds);
    rebuildCrew();
}

QSet<QString> MissionDetailModel::currentMissionSessionIds() const
{
    QSet<QString> result;
    if (m_dependencies.sessions == nullptr || m_missionId.isEmpty()) {
        return result;
    }
    for (auto row = 0; row < m_dependencies.sessions->rowCount(); ++row) {
        const auto index = m_dependencies.sessions->index(row);
        const auto sessionId = m_dependencies.sessions
                                   ->data(
                                       index,
                                       SessionCatalogModel::SessionIdRole)
                                   .toString();
        const auto context =
            m_dependencies.sessions->actionContext(sessionId);
        if (context && context->scope == QStringLiteral("room")
            && context->roomId == m_missionId
            && !context->incarnationId.isEmpty()
            && liveMissionSession(context->status)) {
            result.insert(sessionId);
        }
    }
    return result;
}

QString MissionDetailModel::displayForUser(const QString& userId) const
{
    if (userId.isEmpty()) {
        return {};
    }
    if (userId == m_accountUserId) {
        return tr("You");
    }
    const auto member =
        std::ranges::find(m_members.m_members, userId, &MissionMembersModel::Member::userId);
    if (member != m_members.m_members.end()) {
        return !member->displayName.isEmpty()
            ? member->displayName
            : !member->username.isEmpty() ? member->username : tr("Mission member");
    }
    if (m_dependencies.people != nullptr) {
        for (auto row = 0; row < m_dependencies.people->rowCount(); ++row) {
            const auto index = m_dependencies.people->index(row);
            if (m_dependencies.people
                    ->data(index, PeopleModel::UserIdRole)
                    .toString()
                != userId) {
                continue;
            }
            const auto display = m_dependencies.people
                                     ->data(index, PeopleModel::DisplayNameRole)
                                     .toString();
            return !display.isEmpty()
                ? display
                : m_dependencies.people
                      ->data(index, PeopleModel::HandleRole)
                      .toString();
        }
    }
    return {};
}

QString MissionDetailModel::displayForSession(
    const QString& sessionId,
    const QString& incarnationId) const
{
    if (m_dependencies.sessions == nullptr || sessionId.isEmpty()) {
        return {};
    }
    for (auto row = 0; row < m_dependencies.sessions->rowCount(); ++row) {
        const auto index = m_dependencies.sessions->index(row);
        const auto candidate = m_dependencies.sessions
                                   ->data(
                                       index,
                                       SessionCatalogModel::SessionIdRole)
                                   .toString();
        const auto context =
            m_dependencies.sessions->actionContext(candidate);
        if (!context || context->roomId != m_missionId
            || (candidate != sessionId
                && context->assignmentSessionId != sessionId)
            || (!incarnationId.isEmpty()
                && context->incarnationId != incarnationId
                && context->assignmentIncarnationId != incarnationId)) {
            continue;
        }
        return m_dependencies.sessions
            ->data(index, SessionCatalogModel::NameRole)
            .toString();
    }
    return {};
}

QString MissionDetailModel::statusLabel(const QString& status) const
{
    if (status == QStringLiteral("InProgress")) {
        return tr("In progress");
    }
    if (knownTaskStatus(status)) {
        return status;
    }
    return tr("Unknown: %1").arg(status);
}

std::optional<MissionCrewModel::Entry>
MissionDetailModel::selectedCrewEntry() const
{
    const auto found = std::ranges::find(
        m_crew.m_entries,
        m_selectedCrewPresentationId,
        &MissionCrewModel::Entry::presentationId);
    if (found == m_crew.m_entries.end()
        || found->kind != MissionCrewModel::Kind::Agent
        || found->localSessionId != m_selectedSessionId
        || found->sessionIncarnationId != m_selectedSessionIncarnationId) {
        return std::nullopt;
    }
    return *found;
}

bool MissionDetailModel::performAttentionAction(
    const MissionScopedAttentionModel::Entry& entry,
    const MissionScopedAttentionModel::ActionKind actionKind,
    const bool deny)
{
    if (m_dependencies.attention == nullptr) {
        return false;
    }
    const auto sessionIds = currentMissionSessionIds();
    for (const auto& sessionId : entry.sourceSessionIds) {
        if (!sessionIds.contains(sessionId)) {
            rebuildProjections();
            return false;
        }
    }
    const auto accepted = deny
        ? m_dependencies.attention->denyScopedItem(
              entry.sourceToken,
              sessionIds)
        : m_dependencies.attention->actOnScopedItem(
              entry.sourceToken,
              sessionIds,
              static_cast<AttentionModel::ActionKind>(actionKind));
    if (accepted
        && actionKind == MissionScopedAttentionModel::ActionKind::Jump
        && entry.sourceSessionIds.size() == 1) {
        const auto presentationId =
            m_crew.presentationForSession(entry.sourceSessionIds.constFirst());
        if (!presentationId.isEmpty()) {
            (void)selectCrew(presentationId);
        }
    }
    rebuildProjections();
    return accepted;
}

std::optional<MissionMembersModel::Member> MissionDetailModel::decodeMember(
    const QJsonObject& object,
    const QString& missionId)
{
    const auto roomId = requiredString(object, QStringLiteral("roomId"));
    const auto userId = requiredString(object, QStringLiteral("userId"));
    const auto role = requiredString(object, QStringLiteral("role"));
    if (!roomId || *roomId != missionId || !userId || !role) {
        return std::nullopt;
    }
    return MissionMembersModel::Member {
        .userId = *userId,
        .role = *role,
        .username = optionalString(object, QStringLiteral("username")),
        .displayName = optionalString(object, QStringLiteral("displayName")),
    };
}

std::optional<MissionMessagesModel::Message> MissionDetailModel::decodeMessage(
    const QJsonObject& object,
    const QString& missionId,
    const bool requireRecipientArrays)
{
    const auto id = requiredString(object, QStringLiteral("id"));
    const auto roomId = requiredString(object, QStringLiteral("roomId"));
    const auto author = requiredString(object, QStringLiteral("authorUserId"));
    const auto kind = requiredString(object, QStringLiteral("authorKind"));
    const auto body = requiredString(object, QStringLiteral("body"));
    const auto sequence = integer(object.value(QStringLiteral("seq")), true);
    const auto posted = requiredString(object, QStringLiteral("postedAt"));
    const auto postedAt = posted ? rfc3339(*posted) : std::nullopt;
    const auto recipientSessions =
        stringArray(
            object,
            QStringLiteral("recipientSessionIds"),
            requireRecipientArrays);
    const auto recipientUsers =
        stringArray(
            object,
            QStringLiteral("recipientUserIds"),
            requireRecipientArrays);
    if (!id || !roomId || *roomId != missionId || !author || !kind || !body
        || !sequence || !postedAt || !recipientSessions || !recipientUsers
        || (*kind != QStringLiteral("Human")
            && *kind != QStringLiteral("Agent"))) {
        return std::nullopt;
    }
    QString authorSessionId;
    if (!nullableString(
            object,
            QStringLiteral("authorSessionId"),
            authorSessionId,
            true)) {
        return std::nullopt;
    }
    return MissionMessagesModel::Message {
        .id = *id,
        .authorUserId = *author,
        .authorSessionId = authorSessionId,
        .authorKind = *kind,
        .body = *body,
        .recipientSessionIds = *recipientSessions,
        .recipientUserIds = *recipientUsers,
        .authorDisplay = {},
        .audienceSummary = {},
        .recipientPresentationIds = {},
        .sequence = *sequence,
        .postedAt = *postedAt,
    };
}

std::optional<MissionTasksModel::Task> MissionDetailModel::decodeTask(
    const QJsonObject& object,
    const QString& missionId)
{
    const auto id = requiredString(object, QStringLiteral("id"));
    const auto roomId = requiredString(object, QStringLiteral("roomId"));
    const auto creator = requiredString(object, QStringLiteral("createdByUserId"));
    const auto title = requiredString(object, QStringLiteral("title"));
    const auto status = requiredString(object, QStringLiteral("status"));
    const auto revision = integer(object.value(QStringLiteral("revision")), true);
    const auto created = requiredString(object, QStringLiteral("createdAt"));
    const auto updated = requiredString(object, QStringLiteral("updatedAt"));
    const auto createdAt = created ? rfc3339(*created) : std::nullopt;
    const auto updatedAt = updated ? rfc3339(*updated) : std::nullopt;
    if (!id || !roomId || *roomId != missionId || !creator || !title || !status
        || !revision || !createdAt || !updatedAt) {
        return std::nullopt;
    }
    QString description;
    QString assignedSessionId;
    QString assignedSessionIncarnationId;
    QString due;
    QString completed;
    QString result;
    QString resultAuthorUserId;
    if (!nullableString(
            object,
            QStringLiteral("description"),
            description)
        || !nullableString(
            object,
            QStringLiteral("assignedSessionId"),
            assignedSessionId,
            true)
        || !nullableString(
            object,
            QStringLiteral("assignedSessionIncarnationId"),
            assignedSessionIncarnationId,
            true)
        || !nullableString(
            object,
            QStringLiteral("dueAt"),
            due,
            true)
        || !nullableString(
            object,
            QStringLiteral("completedAt"),
            completed,
            true)
        || !nullableString(
            object,
            QStringLiteral("result"),
            result)
        || !nullableString(
            object,
            QStringLiteral("resultAuthorUserId"),
            resultAuthorUserId,
            true)) {
        return std::nullopt;
    }
    QDateTime dueAt;
    if (!due.isEmpty()) {
        const auto parsed = rfc3339(due);
        if (!parsed) {
            return std::nullopt;
        }
        dueAt = *parsed;
    }
    QDateTime completedAt;
    if (!completed.isEmpty()) {
        const auto parsed = rfc3339(completed);
        if (!parsed) {
            return std::nullopt;
        }
        completedAt = *parsed;
    }
    if (assignedSessionId.isEmpty()
        != assignedSessionIncarnationId.isEmpty()) {
        return std::nullopt;
    }
    return MissionTasksModel::Task {
        .id = *id,
        .title = *title,
        .description = description,
        .status = *status,
        .statusLabel = *status,
        .assignedSessionId = assignedSessionId,
        .assignedSessionIncarnationId = assignedSessionIncarnationId,
        .assignmentDisplay = {},
        .result = result,
        .resultAuthorUserId = resultAuthorUserId,
        .resultAuthorDisplay = {},
        .revision = *revision,
        .knownStatus = knownTaskStatus(*status),
        .createdAt = *createdAt,
        .dueAt = dueAt,
        .completedAt = completedAt,
        .updatedAt = *updatedAt,
    };
}

bool MissionDetailModel::isCompleteMessageEntity(
    const QJsonObject& object,
    const QString& missionId)
{
    return decodeMessage(object, missionId, true).has_value();
}

bool MissionDetailModel::isCompleteMemberEntity(
    const QJsonObject& object,
    const QString& missionId)
{
    return decodeMember(object, missionId).has_value();
}

bool MissionDetailModel::isCompleteTaskEntity(
    const QJsonObject& object,
    const QString& missionId)
{
    return decodeTask(object, missionId).has_value();
}

} // namespace kodosi
