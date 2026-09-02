#include "models/MissionActions.hpp"

#include <QJsonArray>
#include <QJsonDocument>
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
    m_receiptTimer.setSingleShot(true);
    connect(&m_receiptTimer, &QTimer::timeout, this, [this] {
        reconcileExpired();
    });
    const auto availabilityChanged = [this] { emit stateChanged(); };
    const auto assignmentsChanged = [this] {
        ++m_assignmentRevision;
        emit assignmentChanged();
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

bool MissionActions::postBroadcast(
    const QString& missionId,
    const QString& body)
{
    const auto trimmed = body.trimmed();
    if (!m_directory.containsMission(missionId) || trimmed.isEmpty()
        || trimmed.size() > 4'000) {
        m_lastError =
            QStringLiteral("Enter a Mission message of at most 4000 characters.");
        emit stateChanged();
        return false;
    }
    return dispatchMutation(
        {
            .operation = QStringLiteral("chat.post"),
            .missionId = missionId,
        },
        {
            {QStringLiteral("type"), QStringLiteral("room.chat.post")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("body"), trimmed},
        });
}

bool MissionActions::createTask(
    const QString& missionId,
    const QString& title,
    const QString& description)
{
    const auto trimmedTitle = title.trimmed();
    const auto trimmedDescription = description.trimmed();
    if (!m_directory.containsMission(missionId)
        || m_detail.missionId() != missionId
        || !canCreateSelectedTasks() || trimmedTitle.isEmpty()
        || trimmedTitle.size() > 200 || trimmedDescription.size() > 4'000) {
        m_lastError = QStringLiteral(
            "Enter a task title of at most 200 characters and description of at most 4000.");
        emit stateChanged();
        return false;
    }
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("room.tasks.create")},
        {QStringLiteral("room_id"), missionId},
        {QStringLiteral("title"), trimmedTitle},
    };
    if (!trimmedDescription.isEmpty()) {
        command.insert(QStringLiteral("description"), trimmedDescription);
    }
    return dispatchMutation(
        {
            .operation = QStringLiteral("tasks.create"),
            .missionId = missionId,
        },
        std::move(command));
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
    return dispatchMutation(
        {
            .operation = QStringLiteral("invite"),
            .missionId = missionId,
            .inviteeUserId = *friendId,
        },
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
    return dispatchMutation(
        {
            .operation = QStringLiteral("removeMember"),
            .missionId = missionId,
            .targetId = userId,
            .receiptAuthoritative = true,
        },
        {
            {QStringLiteral("type"), QStringLiteral("room.removeMember")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("user_id"), userId},
            {QStringLiteral("expected_roster_generation"),
             mission->rosterGeneration},
        });
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
        || (requiresEvidence
            && (trimmedResult.isEmpty() || trimmedResult.size() > 4'000))) {
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
    if (requiresEvidence) {
        command.insert(QStringLiteral("result"), trimmedResult);
    }
    return dispatchMutation(
        {
            .operation = QStringLiteral("tasks.transition"),
            .missionId = missionId,
            .targetId = taskId,
            .receiptAuthoritative = true,
        },
        std::move(command));
}

bool MissionActions::assignTask(
    const QString& missionId,
    const QString& taskId,
    const QString& sessionId)
{
    const auto task = m_detail.tasks()->actionContext(taskId);
    if (!task || m_detail.missionId() != missionId
        || !canActOnSelectedTasks()) {
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
    return dispatchMutation(
        {
            .operation = QStringLiteral("tasks.assign"),
            .missionId = missionId,
            .targetId = taskId,
            .receiptAuthoritative = true,
        },
        std::move(command));
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
            pending = m_pending.erase(pending);
            removed = true;
        } else {
            ++pending;
        }
    }
    if (removed) {
        m_lastError.clear();
        scheduleReceiptTimeout();
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
    m_pending.clear();
    m_lastError.clear();
    m_accountUserId.clear();
    ++m_assignmentRevision;
    emit assignmentChanged();
    emit stateChanged();
}

bool MissionActions::dispatchMutation(Pending pending, QJsonObject command)
{
    if (m_pending.size() >= maximumPending) {
        m_lastError =
            QStringLiteral("Too many Mission changes are awaiting confirmation.");
        emit stateChanged();
        return false;
    }
    if (hasPendingForMission(pending.missionId)) {
        m_lastError =
            QStringLiteral("Another change for this Mission is still pending.");
        emit stateChanged();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    pending.submittedBody = optionalString(command, QStringLiteral("body"));
    pending.submittedTitle = optionalString(command, QStringLiteral("title"));
    pending.submittedDescription =
        optionalString(command, QStringLiteral("description"));
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
    return dispatchMutation(
        {
            .operation = operation,
            .missionId = invitation->roomId,
            .targetId = invitationId,
            .receiptAuthoritative = true,
        },
        {
            {QStringLiteral("type"), commandType},
            {QStringLiteral("invitation_id"), invitationId},
            {QStringLiteral("room_id"), invitation->roomId},
            {QStringLiteral("expected_roster_generation"),
             invitation->baseRosterGeneration},
        });
}

bool MissionActions::hasPendingForMission(const QString& missionId) const
{
    return std::ranges::any_of(
        m_pending,
        [&](const Pending& pending) { return pending.missionId == missionId; });
}

void MissionActions::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        m_receiptTimer.stop();
        m_pending.clear();
        m_lastError.clear();
        m_accountUserId = std::move(userId);
        ++m_assignmentRevision;
        emit assignmentChanged();
        emit stateChanged();
        if (!m_accountUserId.isEmpty()
            && !m_dispatcher.send(
                CommandLane::Rooms,
                QByteArrayLiteral("{\"type\":\"room.mutations.recover\"}"))) {
            m_lastError =
                QStringLiteral("The runtime did not accept Mission recovery.");
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
    const auto reconcileMessage = [this, &object](const QJsonObject& message) {
        const auto recipientSessions =
            message.value(QStringLiteral("recipientSessionIds"));
        const auto recipientUsers =
            message.value(QStringLiteral("recipientUserIds"));
        const auto broadcast =
            (recipientSessions.isUndefined() || recipientSessions.isNull()
                || (recipientSessions.isArray()
                    && recipientSessions.toArray().isEmpty()))
            && (recipientUsers.isUndefined() || recipientUsers.isNull()
                || (recipientUsers.isArray()
                    && recipientUsers.toArray().isEmpty()));
        if (broadcast) {
            reconcileEntity(
                optionalString(message, QStringLiteral("id")),
                optionalString(object, QStringLiteral("room_id")),
                optionalString(message, QStringLiteral("body")),
                {},
                {});
        }
    };
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
            if (message.isObject()) {
                reconcileMessage(message.toObject());
            }
        }
        return;
    }
    if (type == QStringLiteral("room.invitations")) {
        const auto outgoing = object.value(QStringLiteral("outgoing"));
        if (!outgoing.isArray()) {
            return;
        }
        for (const auto& invitation : outgoing.toArray()) {
            if (invitation.isObject()) {
                reconcileInvitation(invitation.toObject(), true);
            }
        }
        return;
    }
    if (type == QStringLiteral("room.tasks.upserted")) {
        const auto roomId = optionalString(object, QStringLiteral("room_id"));
        const auto task = object.value(QStringLiteral("task"));
        if (task.isObject()) {
            const auto value = task.toObject();
            reconcileEntity(
                optionalString(value, QStringLiteral("id")),
                roomId,
                {},
                optionalString(value, QStringLiteral("title")),
                optionalString(value, QStringLiteral("description")));
        }
        return;
    }
    if (type == QStringLiteral("room.tasks.snapshot")
        || type == QStringLiteral("room.tasks.page")) {
        const auto roomId = optionalString(object, QStringLiteral("room_id"));
        const auto tasks = object.value(QStringLiteral("tasks"));
        if (!tasks.isArray()) {
            return;
        }
        for (const auto& task : tasks.toArray()) {
            if (!task.isObject()) {
                continue;
            }
            const auto value = task.toObject();
            reconcileEntity(
                optionalString(value, QStringLiteral("id")),
                roomId,
                {},
                optionalString(value, QStringLiteral("title")),
                optionalString(value, QStringLiteral("description")));
        }
        if (type != QStringLiteral("room.tasks.page")) {
            return;
        }
        const auto hydrationId =
            optionalString(object, QStringLiteral("hydration_id"));
        const auto requestOffset =
            nonnegativeInteger(object.value(QStringLiteral("request_offset")));
        const auto hasMore = object.value(QStringLiteral("has_more"));
        auto pending = std::ranges::find_if(
            m_pending,
            [&](const Pending& candidate) {
                return candidate.operation == QStringLiteral("tasks.create")
                    && candidate.missionId == roomId
                    && !candidate.reconciliationHydrationId.isEmpty()
                    && candidate.reconciliationHydrationId == hydrationId;
            });
        if (pending == m_pending.end() || !requestOffset
            || *requestOffset != pending->reconciliationNextOffset
            || !hasMore.isBool()) {
            return;
        }
        ++pending->reconciliationPageCount;
        if (!hasMore.toBool()) {
            pending->reconciliationHydrationId.clear();
            return;
        }
        const auto nextOffset =
            nonnegativeInteger(object.value(QStringLiteral("next_offset")));
        if (!nextOffset || *nextOffset <= *requestOffset
            || pending->reconciliationPageCount >= 20
            || *nextOffset > 2'000) {
            pending->reconciliationHydrationId.clear();
            return;
        }
        pending->reconciliationNextOffset = *nextOffset;
        const auto accepted = m_dispatcher.send(
            CommandLane::Rooms,
            json({
                {QStringLiteral("type"), QStringLiteral("room.tasks.list")},
                {QStringLiteral("room_id"), roomId},
                {QStringLiteral("offset"),
                 static_cast<qint64>(*nextOffset)},
                {QStringLiteral("limit"), 100},
                {QStringLiteral("hydration_id"), hydrationId},
            }));
        if (accepted) {
            pending->deadline =
                QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
        } else {
            m_lastError = QStringLiteral(
                "The Mission task snapshot could not continue for reconciliation.");
            emit stateChanged();
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
        auto pending = m_pending.find(requestId);
        if (pending == m_pending.end()) {
            if (m_pending.size() >= maximumPending) {
                m_lastError = QStringLiteral(
                    "Too many recovered Mission changes await confirmation.");
                emit stateChanged();
                return;
            }
            pending = m_pending.insert(requestId, {
                .operation = operation,
                .missionId = roomId,
                .expectedFingerprint = fingerprint,
                .deadline = QDateTime::currentDateTimeUtc(),
                .receiptAuthoritative = true,
            });
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
    if (operation != pending->operation || roomId != pending->missionId) {
        return;
    }
    if (type == QStringLiteral("room.action.accepted")) {
        const auto fingerprint =
            optionalString(object, QStringLiteral("fingerprint"));
        if (!pending->receiptAuthoritative
            || !validFingerprint(fingerprint)) {
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
        } else if (!fingerprint.isUndefined() && !fingerprint.isNull()) {
            return;
        }
        settle(
            requestId,
            status,
            optionalString(object, QStringLiteral("message")),
            optionalString(object, QStringLiteral("entityId")));
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
            ++pending;
            continue;
        }
        const auto missionId = pending->missionId;
        bool accepted = false;
        if (pending->terminalAwaitingProjection) {
            accepted = refreshReceiptProjections(*pending);
            if (accepted) {
                pending->terminalAwaitingProjection = false;
                pending->terminalAwaitingAcknowledgement = true;
            }
        }
        if (pending->terminalAwaitingAcknowledgement) {
            accepted = finalizeReceipt(pending.key(), *pending);
            if (accepted) {
                pending = m_pending.erase(pending);
                continue;
            }
        } else if (pending->terminalAwaitingProjection) {
            accepted = false;
        } else if (pending->receiptAuthoritative) {
            if (!recoveryAccepted) {
                recoveryAccepted = static_cast<bool>(m_dispatcher.send(
                    CommandLane::Rooms,
                    QByteArrayLiteral(
                        "{\"type\":\"room.mutations.recover\"}")));
            }
            accepted = *recoveryAccepted;
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
                    {QStringLiteral("limit"), 500},
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
                    {QStringLiteral("limit"), 100},
                    {QStringLiteral("hydration_id"),
                     pending->reconciliationHydrationId},
                })));
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
    Q_UNUSED(entityId);
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
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }
    if (found->receiptAuthoritative) {
        found->terminalStatus = status;
        found->terminalMessage = message;
        found->terminalAwaitingProjection = true;
        found->terminalAwaitingAcknowledgement = false;
        found->requiresMemberProjectionRefresh =
            found->requiresMemberProjectionRefresh
            || (found->operation == QStringLiteral("removeMember")
                && m_detail.missionId() == found->missionId);
        found->requiresTaskProjectionRefresh =
            found->requiresTaskProjectionRefresh
            || ((found->operation == QStringLiteral("tasks.transition")
                    || found->operation == QStringLiteral("tasks.assign"))
                && m_detail.missionId() == found->missionId);
        found->reconciliationAttempts = 0;
        found->exhausted = false;
        if (refreshReceiptProjections(*found)) {
            found->terminalAwaitingProjection = false;
            found->terminalAwaitingAcknowledgement = true;
            if (finalizeReceipt(requestId, *found)) {
                m_pending.erase(found);
            } else {
                found->deadline = QDateTime::currentDateTimeUtc()
                                      .addMSecs(m_receiptTimeoutMs);
                m_lastError = QStringLiteral(
                    "The Mission result is saved, but its receipt could not be acknowledged.");
            }
        } else {
            found->deadline =
                QDateTime::currentDateTimeUtc().addMSecs(m_receiptTimeoutMs);
            m_lastError = QStringLiteral(
                "The Mission result arrived, but its latest state could not be refreshed.");
        }
        scheduleReceiptTimeout();
        emit stateChanged();
        return;
    }

    const auto pending = m_pending.take(requestId);
    if (status == QStringLiteral("succeeded")) {
        m_lastError.clear();
        if (pending.operation == QStringLiteral("chat.post")) {
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
    }
    scheduleReceiptTimeout();
    emit stateChanged();
}

void MissionActions::reconcileEntity(
    const QString& requestId,
    const QString& missionId,
    const QString& body,
    const QString& title,
    const QString& description)
{
    auto pending = m_pending.find(requestId);
    if (pending == m_pending.end() || pending->missionId != missionId) {
        return;
    }
    const auto exact = pending->operation == QStringLiteral("chat.post")
        ? !body.isEmpty() && body == pending->submittedBody
        : pending->operation == QStringLiteral("tasks.create")
        && !title.isEmpty() && title == pending->submittedTitle
        && description == pending->submittedDescription;
    settle(
        requestId,
        exact ? QStringLiteral("succeeded") : QStringLiteral("conflict"),
        exact ? QString {}
              : QStringLiteral(
                    "The recovered Mission entity does not match the submitted draft."),
        requestId);
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

bool MissionActions::acknowledgeReceipt(const QString& requestId)
{
    return static_cast<bool>(m_dispatcher.send(
        CommandLane::Rooms,
        json({
            {QStringLiteral("type"),
             QStringLiteral("room.mutations.reconcile")},
            {QStringLiteral("requestId"), requestId},
        })));
}

bool MissionActions::refreshReceiptProjections(const Pending& pending)
{
    const auto directoryAccepted = m_directory.refresh();
    bool detailAccepted = true;
    if (pending.requiresMemberProjectionRefresh) {
        detailAccepted = m_detail.openMission(pending.missionId);
    } else if (pending.requiresTaskProjectionRefresh) {
        detailAccepted = m_detail.missionId() == pending.missionId
            && m_detail.refreshTasks();
    }
    return directoryAccepted && detailAccepted;
}

bool MissionActions::finalizeReceipt(
    const QString& requestId,
    const Pending& pending)
{
    if (!acknowledgeReceipt(requestId)) {
        return false;
    }
    if (pending.terminalStatus == QStringLiteral("succeeded")) {
        m_lastError.clear();
    } else {
        m_lastError = !pending.terminalMessage.isEmpty()
            ? pending.terminalMessage
            : pending.terminalStatus == QStringLiteral("conflict")
            ? QStringLiteral(
                  "The Mission changed elsewhere. Its latest state is being shown.")
            : QStringLiteral("The Mission change failed.");
    }
    return true;
}

void MissionActions::rejectCorrelation(
    const QString& requestId,
    const QString& message)
{
    if (m_pending.remove(requestId) == 0) {
        return;
    }
    m_lastError = message;
    scheduleReceiptTimeout();
    emit stateChanged();
}

} // namespace kodosi
