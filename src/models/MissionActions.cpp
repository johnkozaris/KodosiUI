#include "models/MissionActions.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kodosi {
namespace {

QString optionalString(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    return value.isString() ? value.toString() : QString {};
}

QByteArray json(QJsonObject command)
{
    return QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
}

bool receiptOperation(const QString& operation)
{
    static const QSet<QString> operations {
        QStringLiteral("removeMember"),
        QStringLiteral("acceptInvitation"),
        QStringLiteral("declineInvitation"),
        QStringLiteral("cancelInvitation"),
        QStringLiteral("tasks.transition"),
        QStringLiteral("tasks.assign"),
    };
    return operations.contains(operation);
}

bool directCreateOperation(const QString& operation)
{
    return operation == QStringLiteral("create")
        || operation == QStringLiteral("chat.post")
        || operation == QStringLiteral("tasks.create");
}

bool knownTaskStatus(const QString& status)
{
    static const QSet<QString> statuses {
        QStringLiteral("Open"),
        QStringLiteral("InProgress"),
        QStringLiteral("Review"),
        QStringLiteral("Done"),
        QStringLiteral("Archived"),
    };
    return statuses.contains(status);
}

bool liveTaskSession(const QString& status)
{
    return status == QStringLiteral("active")
        || status == QStringLiteral("waiting")
        || status == QStringLiteral("blocked")
        || status == QStringLiteral("reconnecting");
}

bool validFingerprint(const QString& fingerprint)
{
    return fingerprint.size() == 64
        && std::ranges::all_of(fingerprint, [](const QChar character) {
               return (character >= u'0' && character <= u'9')
                   || (character >= u'a' && character <= u'f')
                   || (character >= u'A' && character <= u'F');
           });
}

bool validStatus(const QString& status)
{
    return status == QStringLiteral("succeeded")
        || status == QStringLiteral("failed")
        || status == QStringLiteral("unknown")
        || status == QStringLiteral("conflict");
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid uuid(value);
    return !uuid.isNull() && uuid.version() == QUuid::UnixEpoch
        && uuid.toString(QUuid::WithoutBraces) == value;
}

std::optional<qsizetype> nonnegativeInteger(const QJsonValue& value)
{
    constexpr auto maximumExactJsonInteger = 9'007'199'254'740'991.0;
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0
        || number > maximumExactJsonInteger || std::floor(number) != number
        || number > static_cast<double>(
            std::numeric_limits<qsizetype>::max())) {
        return std::nullopt;
    }
    return static_cast<qsizetype>(number);
}

std::optional<QStringList> sortedUniqueStrings(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) {
        return QStringList {};
    }
    if (!value.isArray()) {
        return std::nullopt;
    }
    QSet<QString> unique;
    QStringList result;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()
            || unique.contains(entry.toString())) {
            return std::nullopt;
        }
        unique.insert(entry.toString());
        result.push_back(entry.toString());
    }
    std::ranges::sort(result);
    return result;
}

bool validSlug(const QString& value)
{
    static const QRegularExpression grammar(
        QStringLiteral(R"(^[a-z0-9-]{3,64}$)"));
    return grammar.match(value).hasMatch();
}

QString rfc3339(const QDateTime& value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

std::optional<QDateTime> rfc3339Instant(const QString& value)
{
    static const QRegularExpression zoneSuffix(
        QStringLiteral(R"((?:Z|[+-]\d{2}:\d{2})$)"));
    if (value.isEmpty() || !zoneSuffix.match(value).hasMatch()) {
        return std::nullopt;
    }
    const auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        return std::nullopt;
    }
    return parsed.toUTC();
}

bool sameOptionalRfc3339Instant(
    const QString& left,
    const QString& right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return left.isEmpty() && right.isEmpty();
    }
    const auto leftInstant = rfc3339Instant(left);
    const auto rightInstant = rfc3339Instant(right);
    return leftInstant && rightInstant
        && leftInstant->toMSecsSinceEpoch()
            == rightInstant->toMSecsSinceEpoch();
}

} // namespace

MissionActions::MissionActions(
    CommandDispatcher& dispatcher,
    MissionDirectoryModel& directory,
    MissionDetailModel& detail,
    PeopleModel& people,
    SessionCatalogModel& sessions,
    const qint64 receiptTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_directory(directory)
    , m_detail(detail)
    , m_people(people)
    , m_sessions(sessions)
    , m_receiptTimeoutMs(receiptTimeoutMs)
{
    Q_ASSERT(receiptTimeoutMs >= 0);
    m_detail.m_actions = this;
    m_receiptTimer.setSingleShot(true);
    connect(&m_receiptTimer, &QTimer::timeout, this, [this] {
        reconcileExpired();
    });
    const auto availabilityChanged = [this] {
        emit draftsChanged();
        emit stateChanged();
    };
    const auto assignmentsChanged = [this] {
        ++m_assignmentRevision;
        publishCurrentDraftSelection();
        emit assignmentChanged();
        emit draftsChanged();
        emit stateChanged();
    };
    connect(
        &directory,
        &QAbstractItemModel::modelReset,
        this,
        availabilityChanged);
    connect(
        directory.invitations(),
        &QAbstractItemModel::modelReset,
        this,
        availabilityChanged);
    connect(
        &detail,
        &MissionDetailModel::stateChanged,
        this,
        assignmentsChanged);
    connect(
        detail.members(),
        &QAbstractItemModel::modelReset,
        this,
        availabilityChanged);
    connect(
        detail.tasks(),
        &QAbstractItemModel::modelReset,
        this,
        assignmentsChanged);
    connect(
        detail.crew(),
        &QAbstractItemModel::modelReset,
        this,
        [this] {
            reconcileDraftAuthorities();
            ++m_assignmentRevision;
            emit assignmentChanged();
            emit draftsChanged();
            emit stateChanged();
        });
    connect(
        &people,
        &QAbstractItemModel::modelReset,
        this,
        availabilityChanged);
    connect(
        &sessions,
        &QAbstractItemModel::modelReset,
        this,
        assignmentsChanged);
    connect(
        &sessions,
        &QAbstractItemModel::rowsInserted,
        this,
        assignmentsChanged);
    connect(
        &sessions,
        &QAbstractItemModel::rowsRemoved,
        this,
        assignmentsChanged);
    connect(
        &sessions,
        &QAbstractItemModel::dataChanged,
        this,
        assignmentsChanged);
}

QString MissionActions::lastError() const
{
    return m_lastError;
}

bool MissionActions::busy() const noexcept
{
    return !m_pending.isEmpty();
}

QStringList MissionActions::pendingMissionIds() const
{
    QSet<QString> ids;
    for (auto pending = m_pending.cbegin(); pending != m_pending.cend(); ++pending) {
        ids.insert(pending->missionId);
    }
    auto result = ids.values();
    std::ranges::sort(result);
    return result;
}

bool MissionActions::canDiscardUnknown() const noexcept
{
    return std::ranges::any_of(m_pending, [](const Pending& pending) {
        return pending.exhausted && !pending.receiptAuthoritative;
    });
}

bool MissionActions::canRetryUnknown() const noexcept
{
    return std::ranges::any_of(m_pending, [](const Pending& pending) {
        return pending.exhausted && pending.receiptAuthoritative;
    });
}

bool MissionActions::canManageSelectedMission() const
{
    const auto context = m_directory.actionContext(m_detail.missionId());
    return context && !m_accountUserId.isEmpty()
        && context->ownerUserId == m_accountUserId;
}

bool MissionActions::canActOnSelectedTasks() const
{
    return canCreateSelectedTasks() && m_detail.tasksReady();
}

bool MissionActions::canCreateSelectedTasks() const
{
    return !m_accountUserId.isEmpty() && m_detail.membersReady()
        && m_directory.containsMission(m_detail.missionId())
        && m_detail.members()->containsUser(m_accountUserId);
}

QVariantList MissionActions::assignmentOptions() const
{
    QVariantList options;
    if (!canActOnSelectedTasks()) {
        return options;
    }
    options.push_back(QVariantMap {
        {QStringLiteral("sessionId"), QString {}},
        {QStringLiteral("name"), QStringLiteral("Unassigned")},
    });
    for (auto row = 0; row < m_sessions.rowCount(); ++row) {
        const auto index = m_sessions.index(row);
        const auto sessionId =
            m_sessions.data(index, SessionCatalogModel::SessionIdRole).toString();
        const auto context = m_sessions.actionContext(sessionId);
        if (!context || !context->commandable
            || context->roomId != m_detail.missionId()
            || !liveTaskSession(context->status)
            || context->assignmentSessionId.isEmpty()
            || context->assignmentIncarnationId.isEmpty()) {
            continue;
        }
        options.push_back(QVariantMap {
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("name"),
             m_sessions.data(index, SessionCatalogModel::NameRole)},
        });
    }
    return options;
}

quint64 MissionActions::assignmentRevision() const noexcept
{
    return m_assignmentRevision;
}

QString MissionActions::createMissionName() const
{
    return m_createDraft.name;
}

QString MissionActions::createMissionSlug() const
{
    return m_createDraft.slug;
}

bool MissionActions::hasCreateMissionDraft() const noexcept
{
    return !m_createDraft.name.isEmpty() || !m_createDraft.slug.isEmpty()
        || !m_createDraft.state.requestId.isEmpty();
}

MissionActions::Outcome MissionActions::createMissionOutcome() const noexcept
{
    return m_createDraft.state.outcome;
}

QString MissionActions::createMissionError() const
{
    return m_createDraft.state.error;
}

bool MissionActions::createMissionCanCheck() const noexcept
{
    return canCheck(m_createDraft.state.outcome);
}

bool MissionActions::createMissionCanRetry() const noexcept
{
    return canRetry(m_createDraft.state.outcome);
}

bool MissionActions::createMissionCanDiscard() const noexcept
{
    if (!m_createDraft.state.requestId.isEmpty()) {
        return canDiscard(m_createDraft.state.outcome);
    }
    return canDiscard(m_createDraft.state.outcome)
        || !m_createDraft.name.isEmpty() || !m_createDraft.slug.isEmpty();
}

bool MissionActions::createMissionCanSubmit() const
{
    const auto name = m_createDraft.name.trimmed();
    return !m_accountUserId.isEmpty()
        && m_createDraft.state.requestId.isEmpty()
        && !name.isEmpty() && name.size() <= 128
        && validSlug(m_createDraft.slug);
}

QString MissionActions::chatDraftBody() const
{
    const auto* draft = currentChatDraft();
    return draft == nullptr ? QString {} : draft->body;
}

QStringList MissionActions::chatRecipientPresentationIds() const
{
    const auto* draft = currentChatDraft();
    if (draft == nullptr) {
        return {};
    }
    auto ids = draft->recipientPresentationIds.values();
    std::ranges::sort(ids);
    return ids;
}

QString MissionActions::chatRecipientSummary() const
{
    const auto* draft = currentChatDraft();
    if (draft == nullptr || draft->recipientPresentationIds.isEmpty()) {
        return tr("Everyone in this Mission");
    }
    QStringList names;
    for (const auto& presentationId : draft->recipientPresentationIds) {
        const auto context =
            m_detail.crew()->recipientContext(presentationId);
        if (context && !context->displayName.isEmpty()) {
            names.push_back(context->displayName);
        }
    }
    std::ranges::sort(names);
    return names.isEmpty()
        ? tr("Selected recipients are unavailable")
        : names.join(QStringLiteral(", "));
}

MissionActions::Outcome MissionActions::chatOutcome() const
{
    const auto* draft = currentChatDraft();
    return draft == nullptr ? Outcome::Idle : draft->state.outcome;
}

QString MissionActions::chatError() const
{
    const auto* draft = currentChatDraft();
    return draft == nullptr ? QString {} : draft->state.error;
}

bool MissionActions::chatCanCheck() const
{
    return canCheck(chatOutcome());
}

bool MissionActions::chatCanRetry() const
{
    return canRetry(chatOutcome());
}

bool MissionActions::chatCanDiscard() const
{
    const auto* draft = currentChatDraft();
    if (draft == nullptr) {
        return false;
    }
    if (!draft->state.requestId.isEmpty()) {
        return canDiscard(draft->state.outcome);
    }
    return canDiscard(draft->state.outcome)
        || !draft->body.isEmpty()
        || !draft->recipientPresentationIds.isEmpty();
}

bool MissionActions::chatCanSubmit() const
{
    const auto missionId = currentMissionId();
    const auto* draft = currentChatDraft();
    if (draft == nullptr || m_accountUserId.isEmpty()
        || !m_directory.containsMission(missionId)
        || !draft->state.requestId.isEmpty()) {
        return false;
    }
    const auto body = draft->body.trimmed();
    if (body.isEmpty() || body.size() > 4'000
        || draft->recipientPresentationIds.size() > 32) {
        return false;
    }
    return std::ranges::all_of(
        draft->recipientPresentationIds,
        [this](const QString& presentationId) {
            const auto context =
                m_detail.crew()->recipientContext(presentationId);
            return context && context->canDispatch;
        });
}

QString MissionActions::taskDraftTitle() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? QString {} : draft->title;
}

QString MissionActions::taskDraftDescription() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? QString {} : draft->description;
}

QString MissionActions::taskDraftAssignmentPresentationId() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? QString {} : draft->assignmentPresentationId;
}

bool MissionActions::taskDraftHasDueAt() const
{
    const auto* draft = currentTaskDraft();
    return draft != nullptr && draft->hasDueAt;
}

QDateTime MissionActions::taskDraftDueAt() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? QDateTime {} : draft->dueAt;
}

QVariantList MissionActions::taskDraftAssignmentOptions() const
{
    QVariantList result;
    if (!canCreateSelectedTasks()) {
        return result;
    }
    result.push_back(QVariantMap {
        {QStringLiteral("presentationId"), QString {}},
        {QStringLiteral("name"), tr("Unassigned")},
    });
    for (const auto& entry : m_detail.crew()->m_entries) {
        if (entry.kind != MissionCrewModel::Kind::Agent
            || !entry.canAssignTask) {
            continue;
        }
        result.push_back(QVariantMap {
            {QStringLiteral("presentationId"), entry.presentationId},
            {QStringLiteral("name"), entry.displayName},
        });
    }
    return result;
}

MissionActions::Outcome MissionActions::taskCreateOutcome() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? Outcome::Idle : draft->state.outcome;
}

QString MissionActions::taskCreateError() const
{
    const auto* draft = currentTaskDraft();
    return draft == nullptr ? QString {} : draft->state.error;
}

bool MissionActions::taskCreateCanCheck() const
{
    return canCheck(taskCreateOutcome());
}

bool MissionActions::taskCreateCanRetry() const
{
    return canRetry(taskCreateOutcome());
}

bool MissionActions::taskCreateCanDiscard() const
{
    const auto* draft = currentTaskDraft();
    if (draft == nullptr) {
        return false;
    }
    if (!draft->state.requestId.isEmpty()) {
        return canDiscard(draft->state.outcome);
    }
    return canDiscard(draft->state.outcome)
        || !draft->title.isEmpty() || !draft->description.isEmpty()
        || !draft->assignmentPresentationId.isEmpty()
        || draft->hasDueAt;
}

bool MissionActions::taskCreateCanSubmit() const
{
    const auto* draft = currentTaskDraft();
    if (draft == nullptr || !canCreateSelectedTasks()
        || !draft->state.requestId.isEmpty()) {
        return false;
    }
    const auto title = draft->title.trimmed();
    const auto description = draft->description.trimmed();
    if (title.isEmpty() || title.size() > 200
        || description.size() > 4'000) {
        return false;
    }
    if (!draft->assignmentPresentationId.isEmpty()) {
        const auto context = m_detail.crew()->recipientContext(
            draft->assignmentPresentationId);
        if (!context || context->kind != MissionCrewModel::Kind::Agent
            || !context->canAssignTask
            || context->assignmentSessionId.isEmpty()
            || context->assignmentIncarnationId.isEmpty()) {
            return false;
        }
    }
    return !draft->hasDueAt
        || (draft->dueAt.isValid()
            && draft->dueAt.toUTC() > QDateTime::currentDateTimeUtc());
}

MissionActions::Outcome MissionActions::ledgerOutcome() const
{
    return m_ledgerPresentations
        .value(currentMissionId())
        .outcome;
}

QString MissionActions::ledgerError() const
{
    return m_ledgerPresentations
        .value(currentMissionId())
        .error;
}

bool MissionActions::ledgerCanCheck() const
{
    const auto outcome = ledgerOutcome();
    return outcome == Outcome::Unknown
        || outcome == Outcome::AcceptedAwaitingProjection;
}

bool MissionActions::ledgerCanRetry() const
{
    return ledgerOutcome() == Outcome::Unknown;
}

bool MissionActions::ledgerCanDiscard() const noexcept
{
    return false;
}

void MissionActions::setCreateMissionName(const QString& name)
{
    if (m_createDraft.name == name) {
        return;
    }
    m_createDraft.name = name;
    ++m_createDraft.revision;
    if (canRetry(m_createDraft.state.outcome)
        || m_createDraft.state.outcome == Outcome::Succeeded) {
        m_createDraft.state.outcome = Outcome::Idle;
        m_createDraft.state.error.clear();
    }
    emit draftsChanged();
}

void MissionActions::setCreateMissionSlug(const QString& slug)
{
    if (m_createDraft.slug == slug) {
        return;
    }
    m_createDraft.slug = slug;
    ++m_createDraft.revision;
    if (canRetry(m_createDraft.state.outcome)
        || m_createDraft.state.outcome == Outcome::Succeeded) {
        m_createDraft.state.outcome = Outcome::Idle;
        m_createDraft.state.error.clear();
    }
    emit draftsChanged();
}

bool MissionActions::createMission()
{
    if (!m_createDraft.state.requestId.isEmpty()) {
        return false;
    }
    const auto name = m_createDraft.name.trimmed();
    const auto slug = m_createDraft.slug;
    if (m_accountUserId.isEmpty() || name.isEmpty() || name.size() > 128
        || !validSlug(slug)) {
        m_createDraft.state.outcome = Outcome::Failed;
        m_createDraft.state.error = tr(
            "Enter a Mission name of at most 128 characters and an exact 3–64 character lowercase slug.");
        emit draftsChanged();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    Pending pending;
    pending.requestId = requestId;
    pending.operation = QStringLiteral("create");
    pending.submittedName = name;
    pending.submittedSlug = slug;
    if (!dispatchMutation(
            std::move(pending),
            {
                {QStringLiteral("type"), QStringLiteral("room.create")},
                {QStringLiteral("name"), name},
                {QStringLiteral("slug"), slug},
            })) {
        m_createDraft.state.outcome = Outcome::Failed;
        m_createDraft.state.error = m_lastError;
        emit draftsChanged();
        return false;
    }
    m_createDraft.submittedName = name;
    m_createDraft.submittedSlug = slug;
    m_createDraft.state = {};
    m_createDraft.state.outcome = Outcome::Pending;
    m_createDraft.state.requestId = requestId;
    m_createDraft.state.submittedRevision = m_createDraft.revision;
    emit draftsChanged();
    return true;
}

bool MissionActions::checkCreateMission()
{
    return checkDirect(QStringLiteral("create"), {});
}

bool MissionActions::retryCreateMission()
{
    if (!createMissionCanRetry()) {
        return false;
    }
    m_createDraft.state = {};
    return createMission();
}

bool MissionActions::discardCreateMission()
{
    if (!createMissionCanDiscard()) {
        return false;
    }
    if (!m_createDraft.state.requestId.isEmpty()) {
        m_pending.remove(m_createDraft.state.requestId);
    }
    m_createDraft = {};
    scheduleReceiptTimeout();
    emit draftsChanged();
    emit stateChanged();
    return true;
}

void MissionActions::setChatDraftBody(const QString& body)
{
    auto* draft = currentChatDraft(true);
    if (draft == nullptr || draft->body == body) {
        return;
    }
    draft->body = body;
    ++draft->revision;
    if (canRetry(draft->state.outcome)
        || draft->state.outcome == Outcome::Succeeded) {
        draft->state.outcome = Outcome::Idle;
        draft->state.error.clear();
    }
    emit draftsChanged();
}

bool MissionActions::toggleChatRecipient(const QString& presentationId)
{
    auto* draft = currentChatDraft(true);
    const auto context = m_detail.crew()->recipientContext(presentationId);
    if (draft == nullptr || !context || !context->canDispatch) {
        return false;
    }
    if (draft->recipientPresentationIds.remove(presentationId) == 0) {
        if (draft->recipientPresentationIds.size() >= 32) {
            draft->state.error =
                tr("A directed Mission message can include at most 32 recipients.");
            emit draftsChanged();
            return false;
        }
        draft->recipientPresentationIds.insert(presentationId);
    }
    ++draft->revision;
    publishCurrentDraftSelection();
    emit draftsChanged();
    return true;
}

bool MissionActions::removeChatRecipient(const QString& presentationId)
{
    auto* draft = currentChatDraft(false);
    if (draft == nullptr
        || draft->recipientPresentationIds.remove(presentationId) == 0) {
        return false;
    }
    ++draft->revision;
    publishCurrentDraftSelection();
    emit draftsChanged();
    return true;
}

void MissionActions::selectChatBroadcast()
{
    auto* draft = currentChatDraft(false);
    if (draft == nullptr || draft->recipientPresentationIds.isEmpty()) {
        return;
    }
    draft->recipientPresentationIds.clear();
    ++draft->revision;
    publishCurrentDraftSelection();
    emit draftsChanged();
}

bool MissionActions::sendChat()
{
    return submitChatDraft(currentMissionId());
}

bool MissionActions::submitChatDraft(const QString& missionId)
{
    if (missionId.isEmpty()) {
        return false;
    }
    (void)m_chatDrafts[missionId];
    touchChatDraft(missionId);
    auto* draft = &m_chatDrafts[missionId];
    if (draft == nullptr || !m_directory.containsMission(missionId)
        || !draft->state.requestId.isEmpty()) {
        return false;
    }
    const auto body = draft->body.trimmed();
    if (body.isEmpty() || body.size() > 4'000) {
        draft->state.outcome = Outcome::Failed;
        draft->state.error =
            tr("Enter a Mission message of at most 4000 characters.");
        emit draftsChanged();
        return false;
    }

    QStringList sessionIds;
    QStringList userIds;
    QSet<QString> stale;
    for (const auto& presentationId : draft->recipientPresentationIds) {
        const auto context =
            m_detail.crew()->recipientContext(presentationId);
        if (!context || !context->canDispatch) {
            stale.insert(presentationId);
            continue;
        }
        if (context->kind == MissionCrewModel::Kind::Agent) {
            sessionIds.push_back(context->rawId);
        } else {
            userIds.push_back(context->rawId);
        }
    }
    if (!stale.isEmpty()) {
        draft->recipientPresentationIds.subtract(stale);
        ++draft->revision;
        draft->state.outcome = Outcome::Failed;
        draft->state.error =
            tr("Unavailable recipients were removed. Review the audience and send again.");
        publishCurrentDraftSelection();
        emit draftsChanged();
        return false;
    }
    std::ranges::sort(sessionIds);
    std::ranges::sort(userIds);
    if (sessionIds.size() + userIds.size() > 32) {
        draft->state.outcome = Outcome::Failed;
        draft->state.error =
            tr("A directed Mission message can include at most 32 recipients.");
        emit draftsChanged();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("room.chat.post")},
        {QStringLiteral("room_id"), missionId},
        {QStringLiteral("body"), body},
    };
    if (!sessionIds.isEmpty()) {
        command.insert(
            QStringLiteral("recipient_session_ids"),
            QJsonArray::fromStringList(sessionIds));
    }
    if (!userIds.isEmpty()) {
        command.insert(
            QStringLiteral("recipient_user_ids"),
            QJsonArray::fromStringList(userIds));
    }
    Pending pending;
    pending.requestId = requestId;
    pending.operation = QStringLiteral("chat.post");
    pending.missionId = missionId;
    pending.submittedBody = body;
    pending.submittedRecipientSessionIds = sessionIds;
    pending.submittedRecipientUserIds = userIds;
    if (!dispatchMutation(std::move(pending), std::move(command))) {
        draft->state.outcome = Outcome::Failed;
        draft->state.error = m_lastError;
        emit draftsChanged();
        return false;
    }
    draft->submittedBody = body;
    draft->submittedRecipientSessionIds = sessionIds;
    draft->submittedRecipientUserIds = userIds;
    draft->state = {};
    draft->state.outcome = Outcome::Pending;
    draft->state.requestId = requestId;
    draft->state.submittedRevision = draft->revision;
    emit draftsChanged();
    return true;
}

bool MissionActions::checkChat()
{
    return checkDirect(QStringLiteral("chat.post"), currentMissionId());
}

bool MissionActions::retryChat()
{
    auto* draft = currentChatDraft(false);
    if (draft == nullptr || !canRetry(draft->state.outcome)) {
        return false;
    }
    draft->state = {};
    return sendChat();
}

bool MissionActions::discardChat()
{
    return discardDirect(QStringLiteral("chat.post"), currentMissionId());
}

void MissionActions::setTaskDraftTitle(const QString& title)
{
    auto* draft = currentTaskDraft(true);
    if (draft == nullptr || draft->title == title) {
        return;
    }
    draft->title = title;
    ++draft->revision;
    if (canRetry(draft->state.outcome)
        || draft->state.outcome == Outcome::Succeeded) {
        draft->state.outcome = Outcome::Idle;
        draft->state.error.clear();
    }
    emit draftsChanged();
}

void MissionActions::setTaskDraftDescription(const QString& description)
{
    auto* draft = currentTaskDraft(true);
    if (draft == nullptr || draft->description == description) {
        return;
    }
    draft->description = description;
    ++draft->revision;
    if (canRetry(draft->state.outcome)
        || draft->state.outcome == Outcome::Succeeded) {
        draft->state.outcome = Outcome::Idle;
        draft->state.error.clear();
    }
    emit draftsChanged();
}

bool MissionActions::setTaskDraftAssignment(const QString& presentationId)
{
    auto* draft = currentTaskDraft(true);
    if (draft == nullptr) {
        return false;
    }
    if (!presentationId.isEmpty()) {
        const auto context =
            m_detail.crew()->recipientContext(presentationId);
        if (!context || context->kind != MissionCrewModel::Kind::Agent
            || !context->canAssignTask) {
            return false;
        }
    }
    if (draft->assignmentPresentationId == presentationId) {
        return true;
    }
    draft->assignmentPresentationId = presentationId;
    ++draft->revision;
    emit draftsChanged();
    return true;
}

void MissionActions::setTaskDraftHasDueAt(const bool enabled)
{
    auto* draft = currentTaskDraft(true);
    if (draft == nullptr || draft->hasDueAt == enabled) {
        return;
    }
    draft->hasDueAt = enabled;
    if (enabled && !draft->dueAt.isValid()) {
        draft->dueAt = QDateTime::currentDateTimeUtc().addDays(1);
    }
    ++draft->revision;
    emit draftsChanged();
}

void MissionActions::setTaskDraftDueAt(const QDateTime& dueAt)
{
    auto* draft = currentTaskDraft(true);
    if (draft == nullptr || draft->dueAt == dueAt) {
        return;
    }
    draft->dueAt = dueAt;
    ++draft->revision;
    emit draftsChanged();
}

bool MissionActions::submitTaskCreate()
{
    const auto missionId = currentMissionId();
    auto* draft = currentTaskDraft(true);
    if (draft != nullptr && !draft->state.requestId.isEmpty()) {
        return false;
    }
    const auto title = draft == nullptr ? QString {} : draft->title.trimmed();
    const auto description =
        draft == nullptr ? QString {} : draft->description.trimmed();
    if (draft == nullptr || !m_directory.containsMission(missionId)
        || !canCreateSelectedTasks() || title.isEmpty() || title.size() > 200
        || description.size() > 4'000) {
        if (draft != nullptr) {
            draft->state.outcome = Outcome::Failed;
            draft->state.error = tr(
                "Enter a task title of at most 200 characters and description of at most 4000.");
            emit draftsChanged();
        }
        return false;
    }

    QString assignmentSessionId;
    QString assignmentIncarnationId;
    if (!draft->assignmentPresentationId.isEmpty()) {
        const auto context = m_detail.crew()->recipientContext(
            draft->assignmentPresentationId);
        if (!context || context->kind != MissionCrewModel::Kind::Agent
            || !context->canAssignTask
            || context->assignmentSessionId.isEmpty()
            || context->assignmentIncarnationId.isEmpty()) {
            draft->assignmentPresentationId.clear();
            ++draft->revision;
            draft->state.outcome = Outcome::Failed;
            draft->state.error = tr(
                "The selected Mission agent is no longer eligible. Choose an assignee again.");
            emit draftsChanged();
            return false;
        }
        assignmentSessionId = context->assignmentSessionId;
        assignmentIncarnationId = context->assignmentIncarnationId;
    }
    QString dueAt;
    if (draft->hasDueAt) {
        if (!draft->dueAt.isValid()
            || draft->dueAt.toUTC()
                <= QDateTime::currentDateTimeUtc()) {
            draft->state.outcome = Outcome::Failed;
            draft->state.error =
                tr("The task due date must be in the future.");
            emit draftsChanged();
            return false;
        }
        dueAt = rfc3339(draft->dueAt);
    }

    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("room.tasks.create")},
        {QStringLiteral("room_id"), missionId},
        {QStringLiteral("title"), title},
    };
    if (!description.isEmpty()) {
        command.insert(QStringLiteral("description"), description);
    }
    if (!assignmentSessionId.isEmpty()) {
        command.insert(
            QStringLiteral("assigned_session_id"),
            assignmentSessionId);
        command.insert(
            QStringLiteral("assigned_session_incarnation_id"),
            assignmentIncarnationId);
    }
    if (!dueAt.isEmpty()) {
        command.insert(QStringLiteral("due_at"), dueAt);
    }
    Pending pending;
    pending.requestId = requestId;
    pending.operation = QStringLiteral("tasks.create");
    pending.missionId = missionId;
    pending.submittedTitle = title;
    pending.submittedDescription = description;
    pending.submittedAssignedSessionId = assignmentSessionId;
    pending.submittedAssignedSessionIncarnationId =
        assignmentIncarnationId;
    pending.submittedDueAt = dueAt;
    if (!dispatchMutation(std::move(pending), std::move(command))) {
        draft->state.outcome = Outcome::Failed;
        draft->state.error = m_lastError;
        emit draftsChanged();
        return false;
    }
    draft->submittedTitle = title;
    draft->submittedDescription = description;
    draft->submittedAssignedSessionId = assignmentSessionId;
    draft->submittedAssignedSessionIncarnationId =
        assignmentIncarnationId;
    draft->submittedDueAt = dueAt;
    draft->state = {};
    draft->state.outcome = Outcome::Pending;
    draft->state.requestId = requestId;
    draft->state.submittedRevision = draft->revision;
    emit draftsChanged();
    return true;
}

bool MissionActions::checkTaskCreate()
{
    return checkDirect(QStringLiteral("tasks.create"), currentMissionId());
}

bool MissionActions::retryTaskCreate()
{
    auto* draft = currentTaskDraft(false);
    if (draft == nullptr || !canRetry(draft->state.outcome)) {
        return false;
    }
    draft->state = {};
    return submitTaskCreate();
}

bool MissionActions::discardTaskCreate()
{
    return discardDirect(QStringLiteral("tasks.create"), currentMissionId());
}

bool MissionActions::postBroadcast(
    const QString& missionId,
    const QString& body)
{
    if (!m_directory.containsMission(missionId)) {
        return false;
    }
    auto& draft = m_chatDrafts[missionId];
    draft.body = body;
    draft.recipientPresentationIds.clear();
    ++draft.revision;
    touchChatDraft(missionId);
    if (missionId == currentMissionId()) {
        publishCurrentDraftSelection();
        emit draftsChanged();
    }
    return submitChatDraft(missionId);
}

bool MissionActions::createTask(
    const QString& missionId,
    const QString& title,
    const QString& description)
{
    if (missionId != currentMissionId()) {
        return false;
    }
    setTaskDraftTitle(title);
    setTaskDraftDescription(description);
    (void)setTaskDraftAssignment({});
    setTaskDraftHasDueAt(false);
    return submitTaskCreate();
}

bool MissionActions::inviteFriend(
    const QString& missionId,
    const QString& handle)
{
    const auto mission = m_directory.actionContext(missionId);
    const auto friendId = m_people.friendUserIdForHandle(handle);
    if (!mission || !friendId || m_accountUserId.isEmpty()
        || mission->ownerUserId != m_accountUserId
        || m_detail.missionId() != missionId
        || !m_detail.membersReady() || !m_directory.invitationsReady()
        || !m_people.ready()
        || m_detail.members()->containsUser(*friendId)
        || m_directory.hasOutgoingInvitation(missionId, *friendId)) {
        m_lastError =
            QStringLiteral("That friend can no longer be invited to this Mission.");
        emit stateChanged();
        return false;
    }
    Pending pending;
    pending.operation = QStringLiteral("invite");
    pending.missionId = missionId;
    pending.inviteeUserId = *friendId;
    return dispatchMutation(
        std::move(pending),
        {
            {QStringLiteral("type"), QStringLiteral("room.invite")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("invitee_user_id"), *friendId},
        });
}

bool MissionActions::acceptInvitation(const QString& invitationId)
{
    return dispatchInvitationAction(
        invitationId,
        MissionInvitationsModel::Direction::Incoming,
        QStringLiteral("acceptInvitation"),
        QStringLiteral("room.acceptInvitation"));
}

bool MissionActions::declineInvitation(const QString& invitationId)
{
    return dispatchInvitationAction(
        invitationId,
        MissionInvitationsModel::Direction::Incoming,
        QStringLiteral("declineInvitation"),
        QStringLiteral("room.declineInvitation"));
}

bool MissionActions::cancelInvitation(const QString& invitationId)
{
    return dispatchInvitationAction(
        invitationId,
        MissionInvitationsModel::Direction::Outgoing,
        QStringLiteral("cancelInvitation"),
        QStringLiteral("room.cancelInvitation"));
}

bool MissionActions::removeMember(
    const QString& missionId,
    const QString& userId)
{
    const auto mission = m_directory.actionContext(missionId);
    if (!mission || !canRemoveMember(missionId, userId)) {
        m_lastError =
            QStringLiteral("That member can no longer be removed from this Mission.");
        emit stateChanged();
        return false;
    }
    Pending pending;
    pending.operation = QStringLiteral("removeMember");
    pending.missionId = missionId;
    pending.targetId = userId;
    pending.expectedRevision = mission->rosterGeneration;
    pending.receiptAuthoritative = true;
    return dispatchMutation(
        std::move(pending),
        {
            {QStringLiteral("type"), QStringLiteral("room.removeMember")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("user_id"), userId},
            {QStringLiteral("expected_roster_generation"),
             mission->rosterGeneration},
        });
}

bool MissionActions::removeMemberByPresentationId(
    const QString& presentationId)
{
    const auto context =
        m_detail.crew()->recipientContext(presentationId);
    if (!context || context->kind != MissionCrewModel::Kind::Member) {
        m_lastError =
            QStringLiteral("That Mission member is no longer available.");
        emit stateChanged();
        return false;
    }
    return removeMember(currentMissionId(), context->rawId);
}

bool MissionActions::canRemoveMember(
    const QString& missionId,
    const QString& userId) const
{
    const auto mission = m_directory.actionContext(missionId);
    return mission && !m_accountUserId.isEmpty()
        && mission->ownerUserId == m_accountUserId
        && mission->ownerUserId != userId && m_accountUserId != userId
        && m_detail.missionId() == missionId
        && m_detail.membersReady()
        && m_detail.members()->containsUser(userId);
}

bool MissionActions::canRemoveMemberByPresentationId(
    const QString& presentationId) const
{
    const auto context =
        m_detail.crew()->recipientContext(presentationId);
    return context && context->kind == MissionCrewModel::Kind::Member
        && canRemoveMember(currentMissionId(), context->rawId);
}

bool MissionActions::transitionTask(
    const QString& missionId,
    const QString& taskId,
    const QString& toStatus,
    const QString& result)
{
    const auto task = m_detail.tasks()->actionContext(taskId);
    const auto trimmedResult = result.trimmed();
    const auto requiresEvidence =
        toStatus == QStringLiteral("Review")
        || toStatus == QStringLiteral("Done");
    if (!task || m_detail.missionId() != missionId
        || !canManageSelectedMission() || !knownTaskStatus(task->status)
        || !m_detail.tasksReady()
        || !knownTaskStatus(toStatus) || task->status == toStatus
        || trimmedResult.size() > 4'000
        || (requiresEvidence
            && trimmedResult.isEmpty())) {
        m_lastError =
            QStringLiteral("That task transition is no longer available.");
        emit stateChanged();
        return false;
    }
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("room.tasks.transition")},
        {QStringLiteral("room_id"), missionId},
        {QStringLiteral("task_id"), taskId},
        {QStringLiteral("expected_task_revision"), task->revision},
        {QStringLiteral("to_status"), toStatus},
    };
    if (!trimmedResult.isEmpty()) {
        command.insert(QStringLiteral("result"), trimmedResult);
    }
    Pending pending;
    pending.operation = QStringLiteral("tasks.transition");
    pending.missionId = missionId;
    pending.targetId = taskId;
    pending.expectedRevision = task->revision;
    pending.expectedTaskStatus = toStatus;
    pending.expectedTaskResult = trimmedResult;
    pending.receiptAuthoritative = true;
    return dispatchMutation(std::move(pending), std::move(command));
}

bool MissionActions::assignTask(
    const QString& missionId,
    const QString& taskId,
    const QString& sessionId)
{
    const auto task = m_detail.tasks()->actionContext(taskId);
    if (!task || m_detail.missionId() != missionId
        || !canActOnSelectedTasks() || !knownTaskStatus(task->status)) {
        m_lastError =
            QStringLiteral("That task assignment is no longer available.");
        emit stateChanged();
        return false;
    }

    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("room.tasks.assign")},
        {QStringLiteral("room_id"), missionId},
        {QStringLiteral("task_id"), taskId},
        {QStringLiteral("expected_task_revision"), task->revision},
    };
    if (sessionId.isEmpty()) {
        if (task->assignedSessionId.isEmpty()
            && task->assignedSessionIncarnationId.isEmpty()) {
            m_lastError =
                QStringLiteral("That task is already unassigned.");
            emit stateChanged();
            return false;
        }
    } else {
        const auto session = m_sessions.actionContext(sessionId);
        if (!session || !session->commandable
            || session->roomId != missionId
            || !liveTaskSession(session->status)
            || session->assignmentSessionId.isEmpty()
            || session->assignmentIncarnationId.isEmpty()
            || (task->assignedSessionId == session->assignmentSessionId
                && task->assignedSessionIncarnationId
                    == session->assignmentIncarnationId)) {
            m_lastError =
                QStringLiteral("That Mission agent can no longer receive this task.");
            emit stateChanged();
            return false;
        }
        command.insert(
            QStringLiteral("session_id"),
            session->assignmentSessionId);
        command.insert(
            QStringLiteral("session_incarnation_id"),
            session->assignmentIncarnationId);
    }
    Pending pending;
    pending.operation = QStringLiteral("tasks.assign");
    pending.missionId = missionId;
    pending.targetId = taskId;
    pending.expectedRevision = task->revision;
    pending.expectedAssignedSessionId =
        optionalString(command, QStringLiteral("session_id"));
    pending.expectedAssignedSessionIncarnationId =
        optionalString(
            command,
            QStringLiteral("session_incarnation_id"));
    pending.receiptAuthoritative = true;
    return dispatchMutation(std::move(pending), std::move(command));
}

QString MissionActions::assignmentForTask(const QString& taskId) const
{
    const auto task = m_detail.tasks()->actionContext(taskId);
    if (!task || task->assignedSessionId.isEmpty()
        || task->assignedSessionIncarnationId.isEmpty()) {
        return {};
    }
    for (auto row = 0; row < m_sessions.rowCount(); ++row) {
        const auto index = m_sessions.index(row);
        const auto sessionId =
            m_sessions.data(index, SessionCatalogModel::SessionIdRole).toString();
        const auto session = m_sessions.actionContext(sessionId);
        if (session
            && session->assignmentSessionId == task->assignedSessionId
            && session->assignmentIncarnationId
                == task->assignedSessionIncarnationId) {
            return sessionId;
        }
    }
    return {};
}

bool MissionActions::discardUnknown()
{
    bool removed = false;
    for (auto pending = m_pending.begin(); pending != m_pending.end();) {
        if (pending->exhausted && !pending->receiptAuthoritative) {
            if (pending->operation == QStringLiteral("create")
                && m_createDraft.state.requestId == pending.key()) {
                m_createDraft.state = {};
            } else if (
                pending->operation == QStringLiteral("chat.post")) {
                auto draft = m_chatDrafts.find(pending->missionId);
                if (draft != m_chatDrafts.end()
                    && draft->state.requestId == pending.key()) {
                    draft->state = {};
                }
            } else if (
                pending->operation == QStringLiteral("tasks.create")) {
                auto draft = m_taskDrafts.find(pending->missionId);
                if (draft != m_taskDrafts.end()
                    && draft->state.requestId == pending.key()) {
                    draft->state = {};
                }
            }
            pending = m_pending.erase(pending);
            removed = true;
        } else {
            ++pending;
        }
    }
    if (removed) {
        m_lastError.clear();
        publishCurrentDraftSelection();
        scheduleReceiptTimeout();
        emit draftsChanged();
        emit stateChanged();
    }
    return removed;
}

bool MissionActions::retryUnknown()
{
    bool retried = false;
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto pending = m_pending.begin(); pending != m_pending.end(); ++pending) {
        if (!pending->exhausted || !pending->receiptAuthoritative) {
            continue;
        }
        pending->deadline = now;
        pending->reconciliationAttempts = 0;
        pending->exhausted = false;
        setLedgerPresentation(
            pending->missionId,
            Outcome::Reconciling);
        retried = true;
    }
    if (retried) {
        m_lastError.clear();
        scheduleReceiptTimeout();
        emit stateChanged();
    }
    return retried;
}

void MissionActions::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    emit stateChanged();
}

void MissionActions::installSyntheticPresentation(
    const SyntheticPresentation presentation)
{
    static const QStringList syntheticRequestIds {
        QStringLiteral("synthetic-unknown-chat"),
        QStringLiteral("synthetic-unknown-task"),
        QStringLiteral("synthetic-unknown-create"),
        QStringLiteral("synthetic-unknown-ledger"),
    };
    for (const auto& requestId : syntheticRequestIds) {
        m_pending.remove(requestId);
    }

    const auto missionId = currentMissionId();
    if (missionId.isEmpty()) {
        return;
    }

    auto& chat = m_chatDrafts[missionId];
    chat.body = tr(
        "Confirm the release notes and report any remaining Linux parity gaps.");
    chat.recipientPresentationIds.clear();
    ++chat.revision;
    touchChatDraft(missionId);

    auto& task = m_taskDrafts[missionId];
    task.title = tr("Verify release evidence");
    task.description = tr(
        "Check the packaged desktop flow and attach the final verification note.");
    ++task.revision;
    touchTaskDraft(missionId);

    if (presentation == SyntheticPresentation::Pending) {
        chat.state = {
            .outcome = Outcome::Pending,
            .error = {},
            .requestId = QStringLiteral("synthetic-pending-chat"),
            .submittedRevision = chat.revision,
        };
        task.state = {
            .outcome = Outcome::AcceptedAwaitingProjection,
            .error = {},
            .requestId = QStringLiteral("synthetic-pending-task"),
            .submittedRevision = task.revision,
        };
        setLedgerPresentation(missionId, Outcome::Reconciling);
    } else {
        const auto error = tr(
            "The Mission change outcome is still unknown. Check authoritative state before retrying.");
        chat.state = {
            .outcome = Outcome::Unknown,
            .error = error,
            .requestId = QStringLiteral("synthetic-unknown-chat"),
            .submittedRevision = chat.revision,
        };
        task.state = {
            .outcome = Outcome::Unknown,
            .error = error,
            .requestId = QStringLiteral("synthetic-unknown-task"),
            .submittedRevision = task.revision,
        };
        m_createDraft.name = tr("Release follow-up");
        m_createDraft.slug = QStringLiteral("release-follow-up");
        ++m_createDraft.revision;
        m_createDraft.state = {
            .outcome = Outcome::Unknown,
            .error = error,
            .requestId = QStringLiteral("synthetic-unknown-create"),
            .submittedRevision = m_createDraft.revision,
        };
        setLedgerPresentation(missionId, Outcome::Unknown, error);

        const auto installPending =
            [this](Pending pending) {
                pending.exhausted = true;
                m_pending.insert(pending.requestId, std::move(pending));
            };
        Pending syntheticChat;
        syntheticChat.requestId = chat.state.requestId;
        syntheticChat.operation = QStringLiteral("chat.post");
        syntheticChat.missionId = missionId;
        syntheticChat.submittedBody = chat.body;
        installPending(std::move(syntheticChat));

        Pending syntheticTask;
        syntheticTask.requestId = task.state.requestId;
        syntheticTask.operation = QStringLiteral("tasks.create");
        syntheticTask.missionId = missionId;
        syntheticTask.submittedTitle = task.title;
        syntheticTask.submittedDescription = task.description;
        installPending(std::move(syntheticTask));

        Pending syntheticCreate;
        syntheticCreate.requestId = m_createDraft.state.requestId;
        syntheticCreate.operation = QStringLiteral("create");
        syntheticCreate.submittedName = m_createDraft.name;
        syntheticCreate.submittedSlug = m_createDraft.slug;
        installPending(std::move(syntheticCreate));

        Pending syntheticLedger;
        syntheticLedger.requestId =
            QStringLiteral("synthetic-unknown-ledger");
        syntheticLedger.operation = QStringLiteral("tasks.transition");
        syntheticLedger.missionId = missionId;
        syntheticLedger.receiptAuthoritative = true;
        installPending(std::move(syntheticLedger));
    }

    scheduleReceiptTimeout();
    publishCurrentDraftSelection();
    emit assignmentChanged();
    emit draftsChanged();
    emit stateChanged();
}

void MissionActions::ingestAuthEvent(QByteArray jsonBytes)
{
    const auto document = QJsonDocument::fromJson(jsonBytes);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(jsonBytes, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    activateAccount(
        type == QStringLiteral("auth.ready")
            ? optionalString(object, QStringLiteral("userId"))
            : QString {},
        *epoch);
}

void MissionActions::ingestRoomEvent(QByteArray jsonBytes)
{
    const auto document = QJsonDocument::fromJson(jsonBytes);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(jsonBytes, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = optionalString(object, QStringLiteral("accountUserId")),
            .epoch = *epoch,
        },
        std::move(jsonBytes));
    if (admission == AccountEventAdmission::Current) {
        applyRoomEvent(object);
    }
}

void MissionActions::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_receiptTimer.stop();
    pruneRetiredReceipts();
    const auto recoveryMessage =
        QStringLiteral(
            "The runtime restarted before this Mission action was confirmed. Check its outcome before retrying.");
    for (auto pending = m_pending.begin(); pending != m_pending.end();
         ++pending) {
        pending->deadline = {};
        pending->exhausted = false;
        pending->reconciliationHydrationId.clear();
        if (pending->receiptAuthoritative) {
            setLedgerPresentation(
                pending->missionId,
                Outcome::Unknown,
                recoveryMessage);
            continue;
        }
        if (pending->operation == QStringLiteral("create")) {
            m_createDraft.state.outcome = Outcome::Unknown;
            m_createDraft.state.error = recoveryMessage;
        } else if (pending->operation == QStringLiteral("chat.post")) {
            auto draft = m_chatDrafts.find(pending->missionId);
            if (draft != m_chatDrafts.end()) {
                draft->state.outcome = Outcome::Unknown;
                draft->state.error = recoveryMessage;
            }
        } else if (pending->operation == QStringLiteral("tasks.create")) {
            auto draft = m_taskDrafts.find(pending->missionId);
            if (draft != m_taskDrafts.end()) {
                draft->state.outcome = Outcome::Unknown;
                draft->state.error = recoveryMessage;
            }
        }
    }
    m_lastError = m_pending.isEmpty() ? QString {} : recoveryMessage;
    publishCurrentDraftSelection();
    ++m_assignmentRevision;
    emit assignmentChanged();
    emit draftsChanged();
    emit stateChanged();
}

bool MissionActions::dispatchMutation(Pending pending, QJsonObject command)
{
    pruneRetiredReceipts();
    if (m_pending.size() >= maximumPending) {
        m_lastError =
            QStringLiteral("Too many Mission changes are awaiting confirmation.");
        emit stateChanged();
        return false;
    }
    if (pending.receiptAuthoritative
        && hasReceiptPendingForMission(pending.missionId)) {
        m_lastError =
            QStringLiteral("Another durable change for this Mission is still pending.");
        emit stateChanged();
        return false;
    }
    const auto requestId = pending.requestId.isEmpty()
        ? QUuid::createUuidV7().toString(QUuid::WithoutBraces)
        : pending.requestId;
    pending.requestId = requestId;
    if (pending.submittedBody.isEmpty()) {
        pending.submittedBody =
            optionalString(command, QStringLiteral("body"));
    }
    if (pending.submittedTitle.isEmpty()) {
        pending.submittedTitle =
            optionalString(command, QStringLiteral("title"));
    }
    if (pending.submittedDescription.isEmpty()) {
        pending.submittedDescription =
            optionalString(command, QStringLiteral("description"));
    }
    command.insert(QStringLiteral("requestId"), requestId);
    if (!m_dispatcher.send(CommandLane::Rooms, json(std::move(command)))) {
        m_lastError =
            QStringLiteral("The runtime did not accept the Mission change.");
        emit stateChanged();
        return false;
    }
    m_lastError.clear();
    pending.deadline =
        QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
    m_pending.insert(requestId, std::move(pending));
    const auto inserted = m_pending.constFind(requestId);
    if (inserted != m_pending.cend() && inserted->receiptAuthoritative) {
        setLedgerPresentation(
            inserted->missionId,
            Outcome::Pending);
    }
    scheduleReceiptTimeout();
    emit stateChanged();
    return true;
}

bool MissionActions::dispatchInvitationAction(
    const QString& invitationId,
    const MissionInvitationsModel::Direction expectedDirection,
    const QString& operation,
    const QString& commandType)
{
    const auto invitation =
        m_directory.invitationActionContext(invitationId);
    const auto mission = invitation
        ? m_directory.actionContext(invitation->roomId)
        : std::optional<MissionDirectoryModel::MissionActionContext> {};
    const auto ownsOutgoing =
        expectedDirection != MissionInvitationsModel::Direction::Outgoing
        || (mission && mission->ownerUserId == m_accountUserId);
    const auto isIncomingInvitee =
        expectedDirection != MissionInvitationsModel::Direction::Incoming
        || (invitation && invitation->inviteeUserId == m_accountUserId);
    if (!invitation || !m_directory.invitationsReady()
        || m_accountUserId.isEmpty()
        || invitation->direction != expectedDirection || !ownsOutgoing
        || !isIncomingInvitee) {
        m_lastError =
            QStringLiteral("That Mission invitation action is no longer available.");
        emit stateChanged();
        return false;
    }
    Pending pending;
    pending.operation = operation;
    pending.missionId = invitation->roomId;
    pending.targetId = invitationId;
    pending.expectedRevision = invitation->baseRosterGeneration;
    pending.expectedInvitationDirection =
        static_cast<int>(invitation->direction);
    pending.receiptAuthoritative = true;
    return dispatchMutation(
        std::move(pending),
        {
            {QStringLiteral("type"), commandType},
            {QStringLiteral("invitation_id"), invitationId},
            {QStringLiteral("room_id"), invitation->roomId},
            {QStringLiteral("expected_roster_generation"),
             invitation->baseRosterGeneration},
        });
}

bool MissionActions::hasReceiptPendingForMission(
    const QString& missionId) const
{
    return std::ranges::any_of(
        m_pending,
        [&](const Pending& pending) {
            return pending.receiptAuthoritative
                && pending.missionId == missionId;
        });
}

void MissionActions::activateAccount(QString userId, const quint64 epoch)
{
    const auto sameAccount =
        !userId.isEmpty() && !m_accountUserId.isEmpty()
        && userId == m_accountUserId;
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        pruneRetiredReceipts();
        if (userId.isEmpty()
            || (!m_retiredAccountUserId.isEmpty()
                && m_retiredAccountUserId != userId)) {
            m_retiredReceipts.clear();
            m_retiredReceiptOrder.clear();
            m_retiredAccountUserId.clear();
        } else {
            m_retiredAccountUserId = userId;
        }
        m_receiptTimer.stop();
        if (!sameAccount) {
            m_pending.clear();
            m_chatDrafts.clear();
            m_taskDrafts.clear();
            m_ledgerPresentations.clear();
            m_chatDraftOrder.clear();
            m_taskDraftOrder.clear();
            m_createDraft = {};
            m_lastError.clear();
        }
        m_accountUserId = std::move(userId);
        publishCurrentDraftSelection();
        ++m_assignmentRevision;
        emit assignmentChanged();
        emit draftsChanged();
        emit stateChanged();
        if (!m_accountUserId.isEmpty()
            && !m_dispatcher.send(
                CommandLane::Rooms,
                QByteArrayLiteral("{\"type\":\"room.mutations.recover\"}"))) {
            m_lastError =
                QStringLiteral("The runtime did not accept Mission recovery.");
            emit stateChanged();
        }
        if (sameAccount) {
            for (auto pending = m_pending.begin();
                 pending != m_pending.end();
                 ++pending) {
                if (pending->receiptAuthoritative) {
                    continue;
                }
                if (refreshDirectProjection(*pending)) {
                    pending->deadline =
                        QDateTime::currentDateTimeUtc()
                            .addMSecs(m_receiptTimeoutMs);
                    if (pending->operation == QStringLiteral("create")) {
                        m_createDraft.state.outcome = Outcome::Reconciling;
                    } else if (
                        pending->operation == QStringLiteral("chat.post")) {
                        m_chatDrafts[pending->missionId]
                            .state.outcome = Outcome::Reconciling;
                    } else if (
                        pending->operation == QStringLiteral("tasks.create")) {
                        m_taskDrafts[pending->missionId]
                            .state.outcome = Outcome::Reconciling;
                    }
                }
            }
            scheduleReceiptTimeout();
            emit draftsChanged();
            emit stateChanged();
        }
    }
    for (auto& pending : activation.pendingEvents) {
        ingestRoomEvent(std::move(pending));
    }
}

void MissionActions::applyRoomEvent(const QJsonObject& object)
{
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("room.error")) {
        const auto operation =
            optionalString(object, QStringLiteral("operation"));
        const auto message =
            optionalString(object, QStringLiteral("message"));
        const auto roomId =
            optionalString(object, QStringLiteral("room_id"));
        if (operation.isEmpty() || message.isEmpty()) {
            return;
        }
        QStringList failedRequests;
        for (auto pending = m_pending.begin();
             pending != m_pending.end();
             ++pending) {
            if (!pending->terminalAwaitingProjection
                || (!roomId.isEmpty()
                    && pending->missionId != roomId)) {
                continue;
            }
            const auto matches =
                (pending->operation == QStringLiteral("create")
                    && operation == QStringLiteral("refresh"))
                || (pending->operation == QStringLiteral("chat.post")
                    && operation == QStringLiteral("chat.list"))
                || ((pending->operation == QStringLiteral("tasks.create")
                        || pending->operation
                            == QStringLiteral("tasks.transition")
                        || pending->operation
                            == QStringLiteral("tasks.assign"))
                    && operation == QStringLiteral("tasks.list"))
                || (pending->operation == QStringLiteral("removeMember")
                    && operation
                        == QStringLiteral("refreshMembers"))
                || (pending->operation
                        == QStringLiteral("acceptInvitation")
                    && operation == QStringLiteral("refresh"))
                || ((pending->operation
                        == QStringLiteral("acceptInvitation")
                        || pending->operation
                            == QStringLiteral("declineInvitation")
                        || pending->operation
                            == QStringLiteral("cancelInvitation"))
                    && operation
                        == QStringLiteral("refreshInvitations"));
            if (matches) {
                failedRequests.push_back(pending.key());
            }
        }
        for (const auto& requestId : failedRequests) {
            failProjection(requestId, message);
        }
        return;
    }
    const auto reconcileMessage = [this, &object](const QJsonObject& message) {
        const auto roomId =
            optionalString(object, QStringLiteral("room_id"));
        if (!MissionDetailModel::isCompleteMessageEntity(
                message,
                roomId)) {
            return;
        }
        const auto recipientSessions = sortedUniqueStrings(
            message.value(QStringLiteral("recipientSessionIds")));
        const auto recipientUsers = sortedUniqueStrings(
            message.value(QStringLiteral("recipientUserIds")));
        if (!recipientSessions || !recipientUsers) {
            return;
        }
        reconcileEntity(
            optionalString(message, QStringLiteral("id")),
            roomId,
            optionalString(message, QStringLiteral("body")),
            *recipientSessions,
            *recipientUsers,
            {},
            {},
            {},
            {},
            {});
    };
    if (type == QStringLiteral("room.snapshot")) {
        const auto rooms = object.value(QStringLiteral("rooms"));
        if (!rooms.isArray()) {
            return;
        }
        const auto roomValues = rooms.toArray();
        for (const auto& room : roomValues) {
            if (!room.isObject()
                || !MissionDirectoryModel::isCompleteRoomEntity(
                    room.toObject())) {
                return;
            }
        }
        reconcileRoomSnapshot(roomValues);
        QStringList completed;
        for (auto pending = m_pending.begin();
             pending != m_pending.end();
             ++pending) {
            if (!pending->receiptAuthoritative
                || !pending->terminalAwaitingProjection
                || pending->terminalStatus
                    != QStringLiteral("succeeded")
                || (pending->operation
                        != QStringLiteral("removeMember")
                    && pending->operation
                        != QStringLiteral("acceptInvitation"))) {
                continue;
            }
            const auto room = std::ranges::find_if(
                roomValues,
                [&](const QJsonValue& value) {
                    return value.isObject()
                        && optionalString(
                            value.toObject(),
                            QStringLiteral("id"))
                            == pending->missionId;
                });
            if (room == roomValues.end()) {
                continue;
            }
            if (pending->operation
                == QStringLiteral("acceptInvitation")) {
                pending->projectionRevisionMatched = true;
                if (pending->projectionEntityMatched) {
                    completed.push_back(pending.key());
                }
                continue;
            }
            const auto generation = nonnegativeInteger(
                (*room).toObject().value(
                    QStringLiteral("rosterGeneration")));
            if (generation
                && (pending->expectedRevision < 0
                    || *generation > pending->expectedRevision)) {
                pending->projectionRevisionMatched = true;
                if (pending->projectionEntityMatched) {
                    completed.push_back(pending.key());
                }
            }
        }
        for (const auto& requestId : completed) {
            completeReceiptProjection(requestId);
        }
        return;
    }
    if (type == QStringLiteral("room.members")) {
        const auto roomId =
            optionalString(object, QStringLiteral("room_id"));
        const auto hydrationId =
            optionalString(object, QStringLiteral("hydration_id"));
        const auto values = object.value(QStringLiteral("members"));
        auto pending = std::ranges::find_if(
            m_pending,
            [&](const Pending& candidate) {
                return candidate.receiptAuthoritative
                    && candidate.terminalAwaitingProjection
                    && candidate.terminalStatus
                        == QStringLiteral("succeeded")
                    && candidate.operation
                        == QStringLiteral("removeMember")
                    && candidate.missionId == roomId
                    && !candidate.reconciliationHydrationId.isEmpty()
                    && candidate.reconciliationHydrationId
                        == hydrationId;
            });
        if (pending == m_pending.end()) {
            return;
        }
        if (!values.isArray()) {
            failProjection(
                pending.key(),
                QStringLiteral(
                    "The refreshed Mission member projection was incomplete."));
            return;
        }
        QSet<QString> userIds;
        for (const auto& value : values.toArray()) {
            if (!value.isObject()
                || !MissionDetailModel::isCompleteMemberEntity(
                    value.toObject(),
                    roomId)) {
                failProjection(
                    pending.key(),
                    QStringLiteral(
                        "The refreshed Mission member projection was invalid."));
                return;
            }
            const auto userId = optionalString(
                value.toObject(),
                QStringLiteral("userId"));
            if (userIds.contains(userId)) {
                failProjection(
                    pending.key(),
                    QStringLiteral(
                        "The refreshed Mission member projection was ambiguous."));
                return;
            }
            userIds.insert(userId);
        }
        if (userIds.contains(pending->targetId)) {
            failProjection(
                pending.key(),
                QStringLiteral(
                    "The refreshed Mission members do not confirm the completed change."));
            return;
        }
        pending->projectionEntityMatched = true;
        const auto requestId = pending.key();
        if (pending->projectionRevisionMatched
            || pending->expectedRevision < 0) {
            completeReceiptProjection(requestId);
        }
        return;
    }
    if (type == QStringLiteral("room.chat.posted")) {
        const auto message = object.value(QStringLiteral("message"));
        if (!message.isObject()) {
            return;
        }
        reconcileMessage(message.toObject());
        return;
    }
    if (type == QStringLiteral("room.chat.snapshot")) {
        const auto messages = object.value(QStringLiteral("messages"));
        if (!messages.isArray()) {
            return;
        }
        for (const auto& message : messages.toArray()) {
            if (!message.isObject()
                || !MissionDetailModel::isCompleteMessageEntity(
                    message.toObject(),
                    optionalString(object, QStringLiteral("room_id")))) {
                return;
            }
        }
        for (const auto& message : messages.toArray()) {
            reconcileMessage(message.toObject());
        }
        return;
    }
    if (type == QStringLiteral("room.invitations")) {
        const auto incoming = object.value(QStringLiteral("incoming"));
        const auto outgoing = object.value(QStringLiteral("outgoing"));
        if (!incoming.isArray() || !outgoing.isArray()) {
            return;
        }
        QSet<QString> invitationIds;
        const auto validate =
            [&](const QJsonArray& invitations,
                const MissionInvitationsModel::Direction direction) {
                for (const auto& invitation : invitations) {
                    if (!invitation.isObject()
                        || !MissionDirectoryModel::
                            isCompleteInvitationEntity(
                                invitation.toObject(),
                                direction)) {
                        return false;
                    }
                    const auto invitationId = optionalString(
                        invitation.toObject(),
                        QStringLiteral("id"));
                    if (invitationIds.contains(invitationId)) {
                        return false;
                    }
                    invitationIds.insert(invitationId);
                }
                return true;
            };
        if (!validate(
                incoming.toArray(),
                MissionInvitationsModel::Direction::Incoming)
            || !validate(
                outgoing.toArray(),
                MissionInvitationsModel::Direction::Outgoing)) {
            return;
        }
        for (const auto& invitation : outgoing.toArray()) {
            if (invitation.isObject()) {
                reconcileInvitation(invitation.toObject(), true);
            }
        }
        QStringList completed;
        QStringList contradicted;
        for (auto pending = m_pending.begin();
             pending != m_pending.end();
             ++pending) {
            if (!pending->receiptAuthoritative
                || !pending->terminalAwaitingProjection
                || pending->terminalStatus
                    != QStringLiteral("succeeded")
                || (pending->operation
                        != QStringLiteral("acceptInvitation")
                    && pending->operation
                        != QStringLiteral("declineInvitation")
                    && pending->operation
                        != QStringLiteral("cancelInvitation"))) {
                continue;
            }
            if (invitationIds.contains(pending->targetId)) {
                contradicted.push_back(pending.key());
            } else if (
                pending->operation
                == QStringLiteral("acceptInvitation")) {
                pending->projectionEntityMatched = true;
                if (pending->projectionRevisionMatched) {
                    completed.push_back(pending.key());
                }
            } else {
                completed.push_back(pending.key());
            }
        }
        for (const auto& requestId : contradicted) {
            failProjection(
                requestId,
                QStringLiteral(
                    "The refreshed Mission invitations do not confirm the completed change."));
        }
        for (const auto& requestId : completed) {
            completeReceiptProjection(requestId);
        }
        return;
    }
    if (type == QStringLiteral("room.tasks.upserted")) {
        const auto roomId = optionalString(object, QStringLiteral("room_id"));
        const auto task = object.value(QStringLiteral("task"));
        if (task.isObject()
            && MissionDetailModel::isCompleteTaskEntity(
                task.toObject(),
                roomId)) {
            const auto value = task.toObject();
            reconcileEntity(
                optionalString(value, QStringLiteral("id")),
                roomId,
                {},
                {},
                {},
                optionalString(value, QStringLiteral("title")),
                optionalString(value, QStringLiteral("description")),
                optionalString(
                    value,
                    QStringLiteral("assignedSessionId")),
                optionalString(
                    value,
                    QStringLiteral("assignedSessionIncarnationId")),
                optionalString(value, QStringLiteral("dueAt")));
        }
        if (!task.isObject()
            || !MissionDetailModel::isCompleteTaskEntity(
                task.toObject(),
                roomId)) {
            return;
        }
        const auto taskId =
            optionalString(task.toObject(), QStringLiteral("id"));
        auto pending = std::ranges::find_if(
            m_pending,
            [&](const Pending& candidate) {
                return candidate.receiptAuthoritative
                    && candidate.terminalAwaitingProjection
                    && candidate.terminalStatus
                        == QStringLiteral("succeeded")
                    && (candidate.operation
                            == QStringLiteral("tasks.transition")
                        || candidate.operation
                            == QStringLiteral("tasks.assign"))
                    && candidate.missionId == roomId
                    && candidate.targetId == taskId;
            });
        if (pending != m_pending.end()) {
            const auto requestId = pending.key();
            if (taskProjectionMatches(*pending, task.toObject())) {
                completeReceiptProjection(requestId);
            } else {
                failProjection(
                    requestId,
                    QStringLiteral(
                        "The refreshed Mission task does not match the completed change."));
            }
        }
        return;
    }
    if (type == QStringLiteral("room.tasks.snapshot")
        || type == QStringLiteral("room.tasks.page")) {
        const auto roomId = optionalString(object, QStringLiteral("room_id"));
        const auto hydrationId =
            optionalString(object, QStringLiteral("hydration_id"));
        const auto receiptCandidate = std::ranges::find_if(
            m_pending,
            [&](const Pending& candidate) {
                return candidate.receiptAuthoritative
                    && candidate.terminalAwaitingProjection
                    && candidate.terminalStatus
                        == QStringLiteral("succeeded")
                    && (candidate.operation
                            == QStringLiteral("tasks.transition")
                        || candidate.operation
                            == QStringLiteral("tasks.assign"))
                    && candidate.missionId == roomId
                    && (type != QStringLiteral("room.tasks.page")
                        || (!candidate
                                .reconciliationHydrationId
                                .isEmpty()
                            && candidate
                                    .reconciliationHydrationId
                                == hydrationId));
            });
        const auto receiptRequestId =
            receiptCandidate == m_pending.end()
            ? QString {}
            : receiptCandidate.key();
        const auto tasks = object.value(QStringLiteral("tasks"));
        if (!tasks.isArray()) {
            if (!receiptRequestId.isEmpty()) {
                failProjection(
                    receiptRequestId,
                    QStringLiteral(
                        "The refreshed Mission task projection was incomplete."));
            }
            return;
        }
        const auto taskValues = tasks.toArray();
        for (const auto& task : taskValues) {
            if (!task.isObject()
                || !MissionDetailModel::isCompleteTaskEntity(
                    task.toObject(),
                    roomId)) {
                if (!receiptRequestId.isEmpty()) {
                    failProjection(
                        receiptRequestId,
                        QStringLiteral(
                            "The refreshed Mission task projection was invalid."));
                }
                return;
            }
        }
        for (const auto& task : taskValues) {
            const auto value = task.toObject();
            reconcileEntity(
                optionalString(value, QStringLiteral("id")),
                roomId,
                {},
                {},
                {},
                optionalString(value, QStringLiteral("title")),
                optionalString(value, QStringLiteral("description")),
                optionalString(
                    value,
                    QStringLiteral("assignedSessionId")),
                optionalString(
                    value,
                    QStringLiteral("assignedSessionIncarnationId")),
                optionalString(value, QStringLiteral("dueAt")));
        }
        const auto requestOffset =
            nonnegativeInteger(object.value(QStringLiteral("request_offset")));
        const auto hasMore = object.value(QStringLiteral("has_more"));
        auto directPending = std::ranges::find_if(
            m_pending,
            [&](const Pending& candidate) {
                return candidate.operation == QStringLiteral("tasks.create")
                    && candidate.missionId == roomId
                    && !candidate.reconciliationHydrationId.isEmpty()
                    && candidate.reconciliationHydrationId == hydrationId;
            });
        const auto continuePage =
            [&](Pending& pending) {
                if (!requestOffset
                    || *requestOffset
                        != pending.reconciliationNextOffset
                    || !hasMore.isBool()) {
                    return false;
                }
                ++pending.reconciliationPageCount;
                if (!hasMore.toBool()) {
                    pending.reconciliationHydrationId.clear();
                    return true;
                }
                const auto nextOffset = nonnegativeInteger(
                    object.value(QStringLiteral("next_offset")));
                if (!nextOffset || *nextOffset <= *requestOffset
                    || pending.reconciliationPageCount >= 20
                    || *nextOffset > 10'000) {
                    pending.reconciliationHydrationId.clear();
                    return false;
                }
                pending.reconciliationNextOffset = *nextOffset;
                const auto accepted = m_dispatcher.send(
                    CommandLane::Rooms,
                    json({
                        {QStringLiteral("type"),
                         QStringLiteral("room.tasks.list")},
                        {QStringLiteral("room_id"), roomId},
                        {QStringLiteral("offset"),
                         static_cast<qint64>(*nextOffset)},
                        {QStringLiteral("limit"), 500},
                        {QStringLiteral("hydration_id"),
                         hydrationId},
                    }));
                if (accepted) {
                    pending.deadline =
                        QDateTime::currentDateTimeUtc()
                            .addMSecs(m_receiptTimeoutMs);
                }
                return static_cast<bool>(accepted);
            };
        if (type == QStringLiteral("room.tasks.page")
            && directPending != m_pending.end()) {
            (void)continuePage(*directPending);
        }

        auto receiptPending = m_pending.find(receiptRequestId);
        if (receiptPending == m_pending.end()) {
            return;
        }
        const auto target = std::ranges::find_if(
            taskValues,
            [&](const QJsonValue& value) {
                return value.isObject()
                    && optionalString(
                        value.toObject(),
                        QStringLiteral("id"))
                        == receiptPending->targetId;
            });
        if (target != taskValues.end()) {
            const auto requestId = receiptPending.key();
            if (taskProjectionMatches(
                    *receiptPending,
                    (*target).toObject())) {
                completeReceiptProjection(requestId);
            } else {
                failProjection(
                    requestId,
                    QStringLiteral(
                        "The refreshed Mission task does not match the completed change."));
            }
            return;
        }
        if (type != QStringLiteral("room.tasks.page")) {
            failProjection(
                receiptPending.key(),
                QStringLiteral(
                    "The refreshed Mission tasks did not contain the completed change."));
            return;
        }
        const auto requestId = receiptPending.key();
        if (!continuePage(*receiptPending)
            || !hasMore.toBool()) {
            failProjection(
                requestId,
                QStringLiteral(
                    "The refreshed Mission tasks did not confirm the completed change."));
        }
        return;
    }
    const auto requestId = optionalString(object, QStringLiteral("requestId"));
    if (requestId.isEmpty()) {
        return;
    }
    const auto operation = optionalString(object, QStringLiteral("operation"));
    const auto roomId = optionalString(object, QStringLiteral("roomId"));
    if (type == QStringLiteral("room.mutation.recovered")) {
        const auto fingerprint =
            optionalString(object, QStringLiteral("fingerprint"));
        const auto statusValue = object.value(QStringLiteral("status"));
        const auto status = optionalString(object, QStringLiteral("status"));
        if (!canonicalUuidV7(requestId) || !receiptOperation(operation)
            || roomId.isEmpty() || !validFingerprint(fingerprint)
            || (!statusValue.isUndefined() && !statusValue.isNull()
                && !validStatus(status))) {
            return;
        }
        pruneRetiredReceipts();
        bool restoredRetiredReceipt = false;
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end()) {
            if (m_pending.size() >= maximumPending) {
                m_lastError = QStringLiteral(
                    "Too many recovered Mission changes await confirmation.");
                emit stateChanged();
                return;
            }
            Pending recovered;
            const auto retired = m_retiredReceipts.constFind(requestId);
            if (retired != m_retiredReceipts.cend()) {
                if (retired->pending.operation != operation
                    || retired->pending.missionId != roomId
                    || (!retired->pending.expectedFingerprint.isEmpty()
                        && retired->pending.expectedFingerprint
                            != fingerprint)) {
                    m_lastError = QStringLiteral(
                        "The recovered Mission receipt did not match its retired correlation.");
                    setLedgerPresentation(
                        roomId,
                        Outcome::Conflict,
                        m_lastError);
                    emit stateChanged();
                    return;
                }
                recovered = retired->pending;
                m_retiredReceipts.remove(requestId);
                m_retiredReceiptOrder.removeAll(requestId);
                restoredRetiredReceipt = true;
                recovered.terminalStatus.clear();
                recovered.terminalMessage.clear();
                recovered.terminalAwaitingProjection = false;
                recovered.projectionRevisionMatched = false;
                recovered.projectionEntityMatched = false;
                recovered.reconciliationHydrationId.clear();
                recovered.reconciliationNextOffset = 0;
                recovered.reconciliationAttempts = 0;
                recovered.reconciliationPageCount = 0;
                recovered.retiredReceiptReplay = true;
            } else {
                recovered.requestId = requestId;
                recovered.operation = operation;
                recovered.missionId = roomId;
                recovered.intentKnowledge =
                    IntentKnowledge::RecoveredWithoutIntent;
            }
            recovered.expectedFingerprint = fingerprint;
            recovered.deadline = QDateTime::currentDateTimeUtc();
            recovered.receiptAuthoritative = true;
            recovered.exhausted = false;
            pending = m_pending.insert(requestId, std::move(recovered));
            const auto limitation = restoredRetiredReceipt
                ? QStringLiteral(
                      "A locally retired receipt reappeared. Protocol v37 has no positive retirement acknowledgement, so the Mission change is being verified again.")
                : QString {};
            m_lastError = limitation;
            setLedgerPresentation(
                roomId,
                Outcome::Reconciling,
                limitation);
            emit stateChanged();
        } else if (!pending->receiptAuthoritative
            || pending->operation != operation || pending->missionId != roomId
            || (!pending->expectedFingerprint.isEmpty()
                && pending->expectedFingerprint != fingerprint)) {
            rejectCorrelation(
                requestId,
                QStringLiteral(
                    "The recovered Mission result did not match this change."));
            return;
        } else {
            pending->expectedFingerprint = fingerprint;
            pending->deadline = QDateTime::currentDateTimeUtc();
            pending->exhausted = false;
            setLedgerPresentation(roomId, Outcome::Reconciling);
        }
        if (!status.isEmpty() && status != QStringLiteral("unknown")) {
            settle(
                requestId,
                status,
                optionalString(object, QStringLiteral("message")),
                optionalString(object, QStringLiteral("entityId")));
        } else {
            const auto accepted = m_dispatcher.send(
                CommandLane::Rooms,
                json({
                    {QStringLiteral("type"),
                     QStringLiteral("room.mutations.reconcile")},
                    {QStringLiteral("requestId"), requestId},
                }));
            pending = m_pending.find(requestId);
            if (pending != m_pending.end()) {
                pending->deadline =
                    QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
            }
            if (!accepted) {
                m_lastError = QStringLiteral(
                    "The runtime did not accept the Mission receipt query.");
                emit stateChanged();
            }
            scheduleReceiptTimeout();
        }
        return;
    }
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end()) {
        return;
    }
    const auto resultEntityId =
        optionalString(object, QStringLiteral("entityId"));
    const auto roomMatches =
        pending->operation == QStringLiteral("create")
        ? roomId.isEmpty() || roomId == requestId
            || (!resultEntityId.isEmpty()
                && roomId == resultEntityId)
        : roomId == pending->missionId;
    if (operation != pending->operation || !roomMatches) {
        return;
    }
    if (type == QStringLiteral("room.action.accepted")) {
        const auto fingerprint =
            optionalString(object, QStringLiteral("fingerprint"));
        if (!pending->receiptAuthoritative) {
            pending->deadline =
                QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
            scheduleReceiptTimeout();
            return;
        }
        if (!validFingerprint(fingerprint)) {
            return;
        }
        if (!pending->expectedFingerprint.isEmpty()
            && pending->expectedFingerprint != fingerprint) {
            rejectCorrelation(
                requestId,
                QStringLiteral(
                    "The Mission receipt did not match this change."));
            return;
        }
        pending->expectedFingerprint = fingerprint;
        pending->deadline =
            QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
        pending->exhausted = false;
        setLedgerPresentation(
            pending->missionId,
            Outcome::Pending);
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }
    if (type == QStringLiteral("room.action.result")) {
        const auto fingerprint = object.value(QStringLiteral("fingerprint"));
        const auto status = optionalString(object, QStringLiteral("status"));
        if (!validStatus(status)) {
            return;
        }
        if (pending->receiptAuthoritative) {
            const auto preAdmissionFailure =
                pending->expectedFingerprint.isEmpty()
                && (fingerprint.isUndefined() || fingerprint.isNull())
                && status == QStringLiteral("failed");
            const auto receiptMatches =
                !pending->expectedFingerprint.isEmpty()
                && fingerprint.isString()
                && validFingerprint(fingerprint.toString())
                && fingerprint.toString() == pending->expectedFingerprint;
            if (!preAdmissionFailure && !receiptMatches) {
                rejectCorrelation(
                    requestId,
                    QStringLiteral(
                        "The Mission result did not match this change."));
                return;
            }
        }
        settle(
            requestId,
            status,
            optionalString(object, QStringLiteral("message")),
            resultEntityId);
        return;
    }
}

void MissionActions::scheduleReceiptTimeout()
{
    if (m_pending.isEmpty()) {
        m_receiptTimer.stop();
        return;
    }
    std::optional<QDateTime> deadline;
    for (auto pending = m_pending.cbegin(); pending != m_pending.cend(); ++pending) {
        if (!pending->deadline.isValid()) {
            continue;
        }
        deadline = !deadline
            ? std::optional<QDateTime> {pending->deadline}
            : std::optional<QDateTime> {std::min(*deadline, pending->deadline)};
    }
    if (!deadline) {
        m_receiptTimer.stop();
        return;
    }
    const auto delay = std::clamp<qint64>(
        QDateTime::currentDateTimeUtc().msecsTo(*deadline),
        0,
        std::numeric_limits<int>::max());
    m_receiptTimer.start(static_cast<int>(delay));
}

void MissionActions::reconcileExpired()
{
    const auto now = QDateTime::currentDateTimeUtc();
    std::optional<bool> recoveryAccepted;
    for (auto pending = m_pending.begin(); pending != m_pending.end();) {
        if (pending->deadline > now) {
            ++pending;
            continue;
        }
        if (pending->reconciliationAttempts >= maximumReconciliationAttempts) {
            m_lastError = pending->receiptAuthoritative
                ? QStringLiteral(
                      "A durable Mission change is still unconfirmed. Retry reconciliation before changing that Mission.")
                : QStringLiteral(
                      "A Mission change is still unconfirmed. Refresh or explicitly discard it before retrying.");
            pending->deadline = {};
            pending->exhausted = true;
            if (pending->receiptAuthoritative) {
                setLedgerPresentation(
                    pending->missionId,
                    Outcome::Unknown,
                    m_lastError);
            } else {
                completeDirectDraft(
                    *pending,
                    Outcome::Unknown,
                    m_lastError);
            }
            ++pending;
            continue;
        }
        const auto missionId = pending->missionId;
        bool accepted = false;
        if (pending->receiptAuthoritative
            && pending->terminalAwaitingProjection) {
            accepted = refreshReceiptProjections(*pending);
            setLedgerPresentation(
                missionId,
                accepted ? Outcome::AcceptedAwaitingProjection
                         : Outcome::Unknown,
                accepted
                    ? QString {}
                    : QStringLiteral(
                          "The authoritative Mission projection could not be refreshed."));
        } else if (pending->receiptAuthoritative) {
            if (!recoveryAccepted) {
                recoveryAccepted = static_cast<bool>(m_dispatcher.send(
                    CommandLane::Rooms,
                    QByteArrayLiteral(
                        "{\"type\":\"room.mutations.recover\"}")));
            }
            accepted = *recoveryAccepted;
            setLedgerPresentation(
                missionId,
                Outcome::Reconciling);
        } else if (pending->operation == QStringLiteral("create")) {
            accepted = m_directory.refresh();
        } else if (pending->operation == QStringLiteral("invite")) {
            accepted = m_directory.refresh();
        } else if (pending->operation == QStringLiteral("chat.post")) {
            pending->reconciliationHydrationId =
                QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            accepted = static_cast<bool>(m_dispatcher.send(
                CommandLane::Rooms,
                json({
                    {QStringLiteral("type"),
                     QStringLiteral("room.chat.list")},
                    {QStringLiteral("room_id"), missionId},
                    {QStringLiteral("limit"), 1'000},
                    {QStringLiteral("tail"), true},
                    {QStringLiteral("hydration_id"),
                     pending->reconciliationHydrationId},
                })));
        } else {
            pending->reconciliationHydrationId =
                QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            pending->reconciliationNextOffset = 0;
            pending->reconciliationPageCount = 0;
            accepted = static_cast<bool>(m_dispatcher.send(
                CommandLane::Rooms,
                json({
                    {QStringLiteral("type"),
                     QStringLiteral("room.tasks.list")},
                    {QStringLiteral("room_id"), missionId},
                    {QStringLiteral("offset"), 0},
                    {QStringLiteral("limit"), 500},
                    {QStringLiteral("hydration_id"),
                     pending->reconciliationHydrationId},
                })));
        }
        if (!pending->receiptAuthoritative
            && directCreateOperation(pending->operation)) {
            completeDirectDraft(
                *pending,
                Outcome::Reconciling,
                {});
        }
        ++pending->reconciliationAttempts;
        pending->deadline = now.addMSecs(m_receiptTimeoutMs);
        if (!accepted) {
            m_lastError =
                QStringLiteral("The Mission snapshot could not be refreshed for reconciliation.");
        }
        ++pending;
    }
    scheduleReceiptTimeout();
    emit stateChanged();
}

void MissionActions::settle(
    const QString& requestId,
    const QString& status,
    const QString& message,
    const QString& entityId)
{
    auto found = m_pending.find(requestId);
    if (found == m_pending.end()) {
        return;
    }
    if (status == QStringLiteral("unknown")) {
        m_lastError = !message.isEmpty()
            ? message
            : QStringLiteral(
                "The Mission change outcome is still unknown.");
        found->deadline =
            QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
        found->exhausted = false;
        if (found->receiptAuthoritative) {
            setLedgerPresentation(
                found->missionId,
                Outcome::Unknown,
                m_lastError);
        } else {
            completeDirectDraft(
                *found,
                Outcome::Unknown,
                m_lastError);
        }
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }
    if (found->receiptAuthoritative) {
        if (status == QStringLiteral("succeeded")) {
            if (entityId.isEmpty()
                && found->intentKnowledge
                    == IntentKnowledge::RecoveredWithoutIntent) {
                const auto limitation = QStringLiteral(
                    "Protocol v37 recovered this durable Mission change without its original intent or a terminal entity correlation.");
                found->terminalStatus = QStringLiteral("unknown");
                found->terminalMessage = limitation;
                found->terminalAwaitingProjection = false;
                found->deadline =
                    QDateTime::currentDateTimeUtc()
                        .addMSecs(m_receiptTimeoutMs);
                found->reconciliationAttempts = 0;
                found->exhausted = false;
                m_lastError = limitation;
                setLedgerPresentation(
                    found->missionId,
                    Outcome::Unknown,
                    limitation);
                (void)m_dispatcher.send(
                    CommandLane::Rooms,
                    json({
                        {QStringLiteral("type"),
                         QStringLiteral("room.mutations.reconcile")},
                        {QStringLiteral("requestId"), requestId},
                    }));
                scheduleReceiptTimeout();
                emit stateChanged();
                return;
            }
            if (entityId.isEmpty()
                || (!found->targetId.isEmpty()
                    && found->targetId != entityId)) {
                rejectCorrelation(
                    requestId,
                    QStringLiteral(
                        "The Mission result target did not match this change."));
                return;
            }
            found->targetId = entityId;
        }
        found->terminalStatus = status;
        found->terminalMessage = message;
        found->reconciliationAttempts = 0;
        found->exhausted = false;
        if (status == QStringLiteral("succeeded")) {
            found->terminalAwaitingProjection = true;
            found->projectionRevisionMatched =
                found->expectedRevision < 0
                || found->operation
                    != QStringLiteral("removeMember");
            found->projectionEntityMatched = false;
            const auto replayLimitation = found->retiredReceiptReplay
                ? QStringLiteral(
                      "A locally retired receipt reappeared. Protocol v37 has no positive retirement acknowledgement, so the Mission change is being verified again.")
                : QString {};
            if (!replayLimitation.isEmpty()) {
                m_lastError = replayLimitation;
            }
            setLedgerPresentation(
                found->missionId,
                found->retiredReceiptReplay
                    ? Outcome::Reconciling
                    : Outcome::AcceptedAwaitingProjection,
                replayLimitation);
            if (!refreshReceiptProjections(*found)) {
                failProjection(
                    requestId,
                    QStringLiteral(
                        "The Mission result arrived, but its authoritative projection could not be refreshed."));
                return;
            }
            found = m_pending.find(requestId);
            if (found != m_pending.end()) {
                found->deadline =
                    QDateTime::currentDateTimeUtc()
                        .addMSecs(m_receiptTimeoutMs);
            }
        } else {
            found->terminalAwaitingProjection = false;
            finalizeReceipt(requestId, *found);
            m_pending.erase(found);
        }
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }

    if (directCreateOperation(found->operation)
        && status == QStringLiteral("succeeded")) {
        found->expectedEntityId =
            entityId.isEmpty() ? requestId : entityId;
        found->terminalAwaitingProjection = true;
        found->deadline =
            QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
        found->reconciliationAttempts = 0;
        found->exhausted = false;
        completeDirectDraft(
            *found,
            Outcome::AcceptedAwaitingProjection,
            {});
        if (!refreshDirectProjection(*found)) {
            failProjection(
                requestId,
                QStringLiteral(
                    "The authoritative Mission projection could not be refreshed."));
            return;
        }
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }

    const auto pending = m_pending.take(requestId);
    if (status == QStringLiteral("succeeded")) {
        m_lastError.clear();
        completeDirectDraft(pending, Outcome::Succeeded, {});
        if (pending.operation == QStringLiteral("create")) {
            emit missionCreated(requestId);
        } else if (pending.operation == QStringLiteral("chat.post")) {
            emit chatCompleted(pending.missionId);
        } else if (pending.operation == QStringLiteral("tasks.create")) {
            emit taskCompleted(pending.missionId);
        } else if (pending.operation == QStringLiteral("invite")) {
            emit invitationSent(pending.missionId);
            (void)m_directory.refresh();
        }
    } else {
        m_lastError = !message.isEmpty()
            ? message
            : status == QStringLiteral("conflict")
            ? QStringLiteral("The Mission changed elsewhere. Refresh and try again.")
            : QStringLiteral("The Mission change failed.");
        completeDirectDraft(
            pending,
            status == QStringLiteral("conflict")
                ? Outcome::Conflict
                : Outcome::Failed,
            m_lastError);
    }
    scheduleReceiptTimeout();
    emit stateChanged();
}

void MissionActions::reconcileEntity(
    const QString& entityId,
    const QString& missionId,
    const QString& body,
    const QStringList& recipientSessionIds,
    const QStringList& recipientUserIds,
    const QString& title,
    const QString& description,
    const QString& assignedSessionId,
    const QString& assignedSessionIncarnationId,
    const QString& dueAt)
{
    auto pending = std::ranges::find_if(
        m_pending,
        [&](const Pending& candidate) {
            const auto expectedEntityId =
                candidate.expectedEntityId.isEmpty()
                ? candidate.requestId
                : candidate.expectedEntityId;
            return !candidate.receiptAuthoritative
                && (candidate.operation
                        == QStringLiteral("chat.post")
                    || candidate.operation
                        == QStringLiteral("tasks.create"))
                && candidate.missionId == missionId
                && expectedEntityId == entityId;
        });
    if (pending == m_pending.end()) {
        return;
    }
    const auto exact = pending->operation == QStringLiteral("chat.post")
        ? !body.isEmpty() && body == pending->submittedBody
            && recipientSessionIds
                == pending->submittedRecipientSessionIds
            && recipientUserIds == pending->submittedRecipientUserIds
        : pending->operation == QStringLiteral("tasks.create")
        && !title.isEmpty() && title == pending->submittedTitle
        && description == pending->submittedDescription
        && assignedSessionId == pending->submittedAssignedSessionId
        && assignedSessionIncarnationId
            == pending->submittedAssignedSessionIncarnationId
        && sameOptionalRfc3339Instant(
            dueAt,
            pending->submittedDueAt);
    const auto requestId = pending.key();
    if (exact) {
        finalizeDirectProjection(requestId);
    } else {
        failProjection(
            requestId,
            QStringLiteral(
                "The authoritative Mission entity does not match the submitted draft."));
    }
}

void MissionActions::reconcileRoomSnapshot(const QJsonArray& rooms)
{
    for (const auto& room : rooms) {
        if (!room.isObject()
            || !MissionDirectoryModel::isCompleteRoomEntity(
                room.toObject())) {
            return;
        }
    }
    QStringList createRequests;
    for (auto pending = m_pending.cbegin(); pending != m_pending.cend(); ++pending) {
        if (pending->operation == QStringLiteral("create")) {
            createRequests.push_back(pending.key());
        }
    }
    for (const auto& requestId : createRequests) {
        const auto pending = m_pending.constFind(requestId);
        if (pending == m_pending.cend()) {
            continue;
        }
        int matchCount = 0;
        QJsonObject match;
        const auto expectedEntityId =
            pending->expectedEntityId.isEmpty()
            ? requestId
            : pending->expectedEntityId;
        for (const auto& value : rooms) {
            if (!value.isObject()
                || optionalString(value.toObject(), QStringLiteral("id"))
                    != expectedEntityId) {
                continue;
            }
            ++matchCount;
            match = value.toObject();
        }
        if (matchCount == 0) {
            continue;
        }
        const auto exact = matchCount == 1
            && optionalString(match, QStringLiteral("name"))
                == pending->submittedName
            && optionalString(match, QStringLiteral("slug"))
                == pending->submittedSlug;
        if (exact) {
            finalizeDirectProjection(requestId);
        } else {
            failProjection(
                requestId,
                matchCount > 1
                    ? QStringLiteral(
                          "Mission creation reconciliation was ambiguous.")
                    : QStringLiteral(
                          "The authoritative Mission does not match the submitted name and slug."));
        }
    }
}

void MissionActions::reconcileInvitation(
    const QJsonObject& invitation,
    const bool outgoing)
{
    const auto requestId =
        optionalString(invitation, QStringLiteral("id"));
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end()
        || pending->operation != QStringLiteral("invite")) {
        return;
    }
    const auto exact = outgoing
        && optionalString(invitation, QStringLiteral("roomId"))
            == pending->missionId
        && optionalString(invitation, QStringLiteral("inviteeUserId"))
            == pending->inviteeUserId;
    settle(
        requestId,
        exact ? QStringLiteral("succeeded") : QStringLiteral("conflict"),
        exact ? QString {}
              : QStringLiteral(
                    "The recovered Mission invitation does not match the submitted friend."),
        requestId);
}

bool MissionActions::requestReceiptRetirement(const QString& requestId)
{
    return static_cast<bool>(m_dispatcher.send(
        CommandLane::Rooms,
        json({
            {QStringLiteral("type"),
             QStringLiteral("room.mutations.reconcile")},
            {QStringLiteral("requestId"), requestId},
        })));
}

bool MissionActions::refreshReceiptProjections(Pending& pending)
{
    pending.projectionRevisionMatched =
        pending.operation != QStringLiteral("acceptInvitation")
        && (pending.expectedRevision < 0
            || pending.operation != QStringLiteral("removeMember"));
    pending.projectionEntityMatched = false;
    pending.reconciliationHydrationId.clear();
    pending.reconciliationNextOffset = 0;
    pending.reconciliationPageCount = 0;

    if (pending.operation == QStringLiteral("removeMember")) {
        const auto directoryAccepted = m_directory.refresh();
        bool membersAccepted = false;
        if (m_detail.missionId() == pending.missionId) {
            membersAccepted = m_detail.openMission(pending.missionId);
            pending.reconciliationHydrationId =
                m_detail.m_memberHydrationId;
        } else {
            pending.reconciliationHydrationId =
                QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            membersAccepted = static_cast<bool>(m_dispatcher.send(
                CommandLane::Rooms,
                json({
                    {QStringLiteral("type"),
                     QStringLiteral("room.refreshMembers")},
                    {QStringLiteral("room_id"), pending.missionId},
                    {QStringLiteral("hydration_id"),
                     pending.reconciliationHydrationId},
                })));
        }
        return directoryAccepted && membersAccepted;
    }
    if (pending.operation == QStringLiteral("tasks.transition")
        || pending.operation == QStringLiteral("tasks.assign")) {
        if (m_detail.missionId() == pending.missionId) {
            const auto accepted = m_detail.refreshTasks();
            pending.reconciliationHydrationId =
                m_detail.m_taskLoad
                ? m_detail.m_taskLoad->token
                : QString {};
            return accepted;
        }
        pending.reconciliationHydrationId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        return static_cast<bool>(m_dispatcher.send(
            CommandLane::Rooms,
            json({
                {QStringLiteral("type"),
                 QStringLiteral("room.tasks.list")},
                {QStringLiteral("room_id"), pending.missionId},
                {QStringLiteral("offset"), 0},
                {QStringLiteral("limit"), 500},
                {QStringLiteral("hydration_id"),
                 pending.reconciliationHydrationId},
            })));
    }
    if (pending.operation == QStringLiteral("acceptInvitation")) {
        return m_directory.refresh();
    }
    if (pending.operation == QStringLiteral("declineInvitation")
        || pending.operation == QStringLiteral("cancelInvitation")) {
        return static_cast<bool>(m_dispatcher.send(
            CommandLane::Rooms,
            QByteArrayLiteral(
                "{\"type\":\"room.refreshInvitations\"}")));
    }
    return false;
}

bool MissionActions::refreshDirectProjection(Pending& pending)
{
    pending.terminalAwaitingProjection = true;
    if (pending.operation == QStringLiteral("create")) {
        return m_directory.refresh();
    }
    pending.reconciliationHydrationId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    if (pending.operation == QStringLiteral("chat.post")) {
        return static_cast<bool>(m_dispatcher.send(
            CommandLane::Rooms,
            json({
                {QStringLiteral("type"), QStringLiteral("room.chat.list")},
                {QStringLiteral("room_id"), pending.missionId},
                {QStringLiteral("limit"), 1'000},
                {QStringLiteral("tail"), true},
                {QStringLiteral("hydration_id"),
                 pending.reconciliationHydrationId},
            })));
    }
    if (pending.operation == QStringLiteral("tasks.create")) {
        pending.reconciliationNextOffset = 0;
        pending.reconciliationPageCount = 0;
        return static_cast<bool>(m_dispatcher.send(
            CommandLane::Rooms,
            json({
                {QStringLiteral("type"), QStringLiteral("room.tasks.list")},
                {QStringLiteral("room_id"), pending.missionId},
                {QStringLiteral("offset"), 0},
                {QStringLiteral("limit"), 500},
                {QStringLiteral("hydration_id"),
                 pending.reconciliationHydrationId},
            })));
    }
    return false;
}

void MissionActions::finalizeReceipt(
    const QString& requestId,
    const Pending& pending)
{
    const auto retirementRequested =
        requestReceiptRetirement(requestId);
    if (pending.terminalStatus == QStringLiteral("succeeded")) {
        QString limitation;
        if (pending.intentKnowledge
                == IntentKnowledge::RecoveredWithoutIntent
            && (pending.operation == QStringLiteral("tasks.transition")
                || pending.operation == QStringLiteral("tasks.assign"))) {
            limitation = QStringLiteral(
                "Protocol v37 recovery verified the complete task entity, entity ID, and Mission, but the original transition or assignment intent was unavailable.");
        }
        if (!retirementRequested) {
            if (!limitation.isEmpty()) {
                limitation += QLatin1Char(' ');
            }
            limitation += QStringLiteral(
                "The runtime did not accept the receipt-retirement request; protocol v37 has no positive retirement acknowledgement, so recovery will verify the receipt if it reappears.");
        }
        m_lastError = limitation;
        setLedgerPresentation(
            pending.missionId,
            Outcome::Succeeded,
            limitation);
    } else {
        m_lastError = !pending.terminalMessage.isEmpty()
            ? pending.terminalMessage
            : pending.terminalStatus == QStringLiteral("conflict")
            ? QStringLiteral(
                  "The Mission changed elsewhere. Its latest state is being shown.")
            : QStringLiteral("The Mission change failed.");
        setLedgerPresentation(
            pending.missionId,
            pending.terminalStatus == QStringLiteral("conflict")
                ? Outcome::Conflict
                : Outcome::Failed,
            m_lastError);
    }
    retireReceipt(pending);
}

void MissionActions::retireReceipt(const Pending& pending)
{
    pruneRetiredReceipts();
    auto retired = pending;
    retired.deadline = {};
    retired.terminalAwaitingProjection = false;
    retired.reconciliationHydrationId.clear();
    retired.reconciliationNextOffset = 0;
    retired.reconciliationAttempts = 0;
    retired.reconciliationPageCount = 0;
    retired.exhausted = false;
    m_retiredReceipts.insert(
        pending.requestId,
        {
            .pending = std::move(retired),
            .expiresAt =
                QDateTime::currentDateTimeUtc()
                    .addMSecs(retiredReceiptLifetimeMs),
        });
    m_retiredReceiptOrder.removeAll(pending.requestId);
    m_retiredReceiptOrder.push_back(pending.requestId);
    m_retiredAccountUserId = m_accountUserId;
    while (m_retiredReceiptOrder.size() > maximumRetiredReceipts) {
        m_retiredReceipts.remove(m_retiredReceiptOrder.takeFirst());
    }
}

void MissionActions::pruneRetiredReceipts()
{
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto request = m_retiredReceiptOrder.begin();
         request != m_retiredReceiptOrder.end();) {
        const auto retired = m_retiredReceipts.constFind(*request);
        if (retired == m_retiredReceipts.cend()
            || retired->expiresAt <= now) {
            m_retiredReceipts.remove(*request);
            request = m_retiredReceiptOrder.erase(request);
        } else {
            ++request;
        }
    }
}

void MissionActions::finalizeDirectProjection(const QString& requestId)
{
    const auto found = m_pending.find(requestId);
    if (found == m_pending.end() || found->receiptAuthoritative) {
        return;
    }
    const auto pending = m_pending.take(requestId);
    const auto entityId = pending.expectedEntityId.isEmpty()
        ? pending.requestId
        : pending.expectedEntityId;
    m_lastError.clear();
    completeDirectDraft(pending, Outcome::Succeeded, {});
    if (pending.operation == QStringLiteral("create")) {
        emit missionCreated(entityId);
    } else if (pending.operation == QStringLiteral("chat.post")) {
        emit chatCompleted(pending.missionId);
    } else if (pending.operation == QStringLiteral("tasks.create")) {
        emit taskCompleted(pending.missionId);
    }
    scheduleReceiptTimeout();
    emit stateChanged();
}

void MissionActions::completeReceiptProjection(const QString& requestId)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end() || !pending->receiptAuthoritative
        || !pending->terminalAwaitingProjection
        || pending->terminalStatus != QStringLiteral("succeeded")) {
        return;
    }
    pending->terminalAwaitingProjection = false;
    pending->reconciliationHydrationId.clear();
    pending->exhausted = false;
    finalizeReceipt(requestId, *pending);
    m_pending.erase(pending);
    scheduleReceiptTimeout();
    emit stateChanged();
}

void MissionActions::failProjection(
    const QString& requestId,
    QString message)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end()) {
        return;
    }
    pending->deadline = {};
    pending->exhausted = true;
    pending->reconciliationHydrationId.clear();
    pending->reconciliationNextOffset = 0;
    pending->reconciliationPageCount = 0;
    m_lastError = message;
    if (pending->receiptAuthoritative) {
        pending->terminalAwaitingProjection = true;
        setLedgerPresentation(
            pending->missionId,
            Outcome::Unknown,
            message);
    } else {
        completeDirectDraft(
            *pending,
            Outcome::Unknown,
            std::move(message));
    }
    scheduleReceiptTimeout();
    emit stateChanged();
}

bool MissionActions::taskProjectionMatches(
    const Pending& pending,
    const QJsonObject& task) const
{
    if (!MissionDetailModel::isCompleteTaskEntity(
            task,
            pending.missionId)
        || optionalString(task, QStringLiteral("id"))
            != pending.targetId) {
        return false;
    }
    if (pending.intentKnowledge
        == IntentKnowledge::RecoveredWithoutIntent) {
        return true;
    }
    const auto revision =
        nonnegativeInteger(task.value(QStringLiteral("revision")));
    if (!revision
        || (pending.expectedRevision >= 0
            && *revision <= pending.expectedRevision)) {
        return false;
    }
    if (pending.operation == QStringLiteral("tasks.transition")) {
        return optionalString(task, QStringLiteral("status"))
                == pending.expectedTaskStatus
            && (pending.expectedTaskResult.isEmpty()
                || optionalString(task, QStringLiteral("result"))
                    == pending.expectedTaskResult);
    }
    if (pending.operation == QStringLiteral("tasks.assign")) {
        return optionalString(
                   task,
                   QStringLiteral("assignedSessionId"))
                == pending.expectedAssignedSessionId
            && optionalString(
                   task,
                   QStringLiteral(
                       "assignedSessionIncarnationId"))
                == pending.expectedAssignedSessionIncarnationId;
    }
    return false;
}

void MissionActions::rejectCorrelation(
    const QString& requestId,
    const QString& message)
{
    const auto pending = m_pending.find(requestId);
    if (pending == m_pending.end()) {
        return;
    }
    const auto missionId = pending->missionId;
    m_pending.erase(pending);
    m_lastError = message;
    setLedgerPresentation(
        missionId,
        Outcome::Conflict,
        message);
    scheduleReceiptTimeout();
    emit stateChanged();
}

MissionActions::ChatDraft* MissionActions::currentChatDraft(const bool create)
{
    const auto missionId = currentMissionId();
    if (missionId.isEmpty()) {
        return nullptr;
    }
    if (!m_chatDrafts.contains(missionId) && !create) {
        return nullptr;
    }
    (void)m_chatDrafts[missionId];
    touchChatDraft(missionId);
    return &m_chatDrafts[missionId];
}

const MissionActions::ChatDraft* MissionActions::currentChatDraft() const
{
    const auto found = m_chatDrafts.constFind(currentMissionId());
    return found == m_chatDrafts.cend() ? nullptr : &*found;
}

MissionActions::TaskDraft* MissionActions::currentTaskDraft(const bool create)
{
    const auto missionId = currentMissionId();
    if (missionId.isEmpty()) {
        return nullptr;
    }
    if (!m_taskDrafts.contains(missionId) && !create) {
        return nullptr;
    }
    (void)m_taskDrafts[missionId];
    touchTaskDraft(missionId);
    return &m_taskDrafts[missionId];
}

const MissionActions::TaskDraft* MissionActions::currentTaskDraft() const
{
    const auto found = m_taskDrafts.constFind(currentMissionId());
    return found == m_taskDrafts.cend() ? nullptr : &*found;
}

QString MissionActions::currentMissionId() const
{
    return m_detail.missionId();
}

void MissionActions::touchChatDraft(const QString& missionId)
{
    m_chatDraftOrder.removeAll(missionId);
    m_chatDraftOrder.push_back(missionId);
    pruneDraftCaches();
}

void MissionActions::touchTaskDraft(const QString& missionId)
{
    m_taskDraftOrder.removeAll(missionId);
    m_taskDraftOrder.push_back(missionId);
    pruneDraftCaches();
}

void MissionActions::pruneDraftCaches()
{
    const auto prune =
        [](auto& drafts, auto& order) {
            while (drafts.size() > maximumDrafts && !order.isEmpty()) {
                const auto candidate = order.constFirst();
                const auto found = drafts.find(candidate);
                if (found == drafts.end()) {
                    order.removeFirst();
                    continue;
                }
                if (!found->state.requestId.isEmpty()) {
                    order.removeFirst();
                    order.push_back(candidate);
                    if (std::ranges::all_of(drafts, [](const auto& draft) {
                            return !draft.state.requestId.isEmpty();
                        })) {
                        break;
                    }
                    continue;
                }
                drafts.erase(found);
                order.removeFirst();
            }
        };
    prune(m_chatDrafts, m_chatDraftOrder);
    prune(m_taskDrafts, m_taskDraftOrder);
}

void MissionActions::reconcileDraftAuthorities()
{
    if (!m_detail.membersReady()) {
        publishCurrentDraftSelection();
        return;
    }
    auto* chat = currentChatDraft(false);
    bool changed = false;
    if (chat != nullptr) {
        for (auto id = chat->recipientPresentationIds.begin();
             id != chat->recipientPresentationIds.end();) {
            const auto context = m_detail.crew()->recipientContext(*id);
            if (!context || !context->canDispatch) {
                id = chat->recipientPresentationIds.erase(id);
                changed = true;
            } else {
                ++id;
            }
        }
        if (changed) {
            ++chat->revision;
            chat->state.error =
                tr("Unavailable recipients were removed from this draft.");
        }
    }
    auto* task = currentTaskDraft(false);
    if (task != nullptr && !task->assignmentPresentationId.isEmpty()) {
        const auto context = m_detail.crew()->recipientContext(
            task->assignmentPresentationId);
        if (!context || context->kind != MissionCrewModel::Kind::Agent
            || !context->canAssignTask) {
            task->assignmentPresentationId.clear();
            ++task->revision;
            task->state.error =
                tr("The draft assignee is no longer eligible.");
            changed = true;
        }
    }
    publishCurrentDraftSelection();
    if (changed) {
        emit draftsChanged();
    }
}

void MissionActions::publishCurrentDraftSelection()
{
    const auto* draft = currentChatDraft();
    m_detail.setDispatchSelection(
        draft == nullptr ? QSet<QString> {}
                         : draft->recipientPresentationIds);
}

void MissionActions::completeDirectDraft(
    const Pending& pending,
    const Outcome outcome,
    QString error)
{
    const auto active =
        outcome == Outcome::Pending
        || outcome == Outcome::AcceptedAwaitingProjection
        || outcome == Outcome::Unknown
        || outcome == Outcome::Reconciling;
    if (pending.operation == QStringLiteral("create")
        && m_createDraft.state.requestId == pending.requestId) {
        const auto unchanged =
            m_createDraft.revision
            == m_createDraft.state.submittedRevision;
        m_createDraft.state.outcome = outcome;
        m_createDraft.state.error = std::move(error);
        if (!active) {
            m_createDraft.state.requestId.clear();
            if (outcome == Outcome::Succeeded && unchanged) {
                m_createDraft.name.clear();
                m_createDraft.slug.clear();
            }
        }
        emit draftsChanged();
        return;
    }
    if (pending.operation == QStringLiteral("chat.post")) {
        auto found = m_chatDrafts.find(pending.missionId);
        if (found == m_chatDrafts.end()
            || found->state.requestId != pending.requestId) {
            return;
        }
        const auto unchanged =
            found->revision == found->state.submittedRevision;
        found->state.outcome = outcome;
        found->state.error = std::move(error);
        if (!active) {
            found->state.requestId.clear();
            if (outcome == Outcome::Succeeded && unchanged) {
                found->body.clear();
                found->recipientPresentationIds.clear();
            }
        }
        publishCurrentDraftSelection();
        emit draftsChanged();
        return;
    }
    if (pending.operation == QStringLiteral("tasks.create")) {
        auto found = m_taskDrafts.find(pending.missionId);
        if (found == m_taskDrafts.end()
            || found->state.requestId != pending.requestId) {
            return;
        }
        const auto unchanged =
            found->revision == found->state.submittedRevision;
        found->state.outcome = outcome;
        found->state.error = std::move(error);
        if (!active) {
            found->state.requestId.clear();
            if (outcome == Outcome::Succeeded && unchanged) {
                const auto succeeded = found->state;
                *found = {};
                found->state.outcome = succeeded.outcome;
            }
        }
        emit draftsChanged();
    }
}

void MissionActions::setLedgerPresentation(
    const QString& missionId,
    const Outcome outcome,
    QString error)
{
    if (missionId.isEmpty()) {
        return;
    }
    auto& presentation = m_ledgerPresentations[missionId];
    presentation.outcome = outcome;
    presentation.error = std::move(error);
}

bool MissionActions::checkDirect(
    const QString& operation,
    const QString& missionId)
{
    auto found = std::ranges::find_if(
        m_pending,
        [&](const Pending& pending) {
            return pending.operation == operation
                && pending.missionId == missionId;
        });
    if (found == m_pending.end()
        || !canCheck(
            operation == QStringLiteral("create")
                ? m_createDraft.state.outcome
                : operation == QStringLiteral("chat.post")
                ? m_chatDrafts.value(missionId).state.outcome
                : m_taskDrafts.value(missionId).state.outcome)) {
        return false;
    }
    found->reconciliationAttempts = 0;
    found->exhausted = false;
    completeDirectDraft(*found, Outcome::Reconciling, {});
    const auto accepted = refreshDirectProjection(*found);
    if (!accepted) {
        const auto requestId = found.key();
        failProjection(
            requestId,
            QStringLiteral(
                "The authoritative Mission projection could not be refreshed."));
        return false;
    }
    found->deadline = QDateTime::currentDateTimeUtc()
                          .addMSecs(m_receiptTimeoutMs);
    scheduleReceiptTimeout();
    emit stateChanged();
    return accepted;
}

bool MissionActions::discardDirect(
    const QString& operation,
    const QString& missionId)
{
    if (operation == QStringLiteral("chat.post")) {
        auto found = m_chatDrafts.find(missionId);
        const auto canDiscardDraft = found != m_chatDrafts.end()
            && (!found->state.requestId.isEmpty()
                    ? canDiscard(found->state.outcome)
                    : canDiscard(found->state.outcome)
                        || !found->body.isEmpty()
                        || !found->recipientPresentationIds.isEmpty());
        if (found == m_chatDrafts.end()
            || !canDiscardDraft) {
            return false;
        }
        if (!found->state.requestId.isEmpty()) {
            m_pending.remove(found->state.requestId);
        }
        m_chatDrafts.erase(found);
        m_chatDraftOrder.removeAll(missionId);
        publishCurrentDraftSelection();
    } else if (operation == QStringLiteral("tasks.create")) {
        auto found = m_taskDrafts.find(missionId);
        const auto canDiscardDraft = found != m_taskDrafts.end()
            && (!found->state.requestId.isEmpty()
                    ? canDiscard(found->state.outcome)
                    : canDiscard(found->state.outcome)
                        || !found->title.isEmpty()
                        || !found->description.isEmpty()
                        || !found->assignmentPresentationId.isEmpty()
                        || found->hasDueAt);
        if (found == m_taskDrafts.end()
            || !canDiscardDraft) {
            return false;
        }
        if (!found->state.requestId.isEmpty()) {
            m_pending.remove(found->state.requestId);
        }
        m_taskDrafts.erase(found);
        m_taskDraftOrder.removeAll(missionId);
    } else {
        return false;
    }
    scheduleReceiptTimeout();
    emit draftsChanged();
    emit stateChanged();
    return true;
}

bool MissionActions::checkLedger()
{
    const auto missionId = currentMissionId();
    auto pending = std::ranges::find_if(
        m_pending,
        [&](const Pending& value) {
            return value.receiptAuthoritative
                && value.missionId == missionId;
        });
    if (pending == m_pending.end() || !ledgerCanCheck()) {
        return false;
    }
    pending->deadline = QDateTime::currentDateTimeUtc();
    pending->reconciliationAttempts = 0;
    pending->exhausted = false;
    reconcileExpired();
    return true;
}

bool MissionActions::retryLedger()
{
    if (!ledgerCanRetry()) {
        return false;
    }
    return checkLedger();
}

bool MissionActions::discardLedger()
{
    return false;
}

bool MissionActions::canCheck(const Outcome outcome) noexcept
{
    return outcome == Outcome::Unknown
        || outcome == Outcome::AcceptedAwaitingProjection;
}

bool MissionActions::canRetry(const Outcome outcome) noexcept
{
    return outcome == Outcome::Failed || outcome == Outcome::Conflict;
}

bool MissionActions::canDiscard(const Outcome outcome) noexcept
{
    return outcome == Outcome::Unknown || outcome == Outcome::Failed
        || outcome == Outcome::Conflict || outcome == Outcome::Succeeded;
}

} // namespace kodosi
