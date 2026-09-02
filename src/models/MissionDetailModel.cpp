#include "models/MissionDetailModel.hpp"

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

std::optional<QStringList> stringArray(const QJsonObject& object, const QString& key)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        return QStringList {};
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
    };
}

void MissionMessagesModel::replace(QVector<Message> messages)
{
    std::ranges::sort(messages, {}, &Message::sequence);
    if (messages.size() > 500) {
        messages = messages.sliced(messages.size() - 500);
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

MissionDetailModel::MissionDetailModel(
    CommandDispatcher& dispatcher,
    MissionDirectoryModel& directory,
    const qint64 hydrationTimeoutMs,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_directory(directory)
    , m_members(this)
    , m_messages(this)
    , m_tasks(this)
    , m_hydrationTimeoutMs(hydrationTimeoutMs)
{
    Q_ASSERT(hydrationTimeoutMs >= 0);
    m_hydrationTimer.setSingleShot(true);
    connect(&m_hydrationTimer, &QTimer::timeout, this, [this] {
        hydrationTimedOut();
    });
    connect(&directory, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_missionId.isEmpty() && !m_directory.containsMission(m_missionId)) {
            closeMission();
        }
    });
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

bool MissionDetailModel::openMission(const QString& missionId)
{
    if (!m_authenticated || !m_directory.containsMission(missionId)) {
        return false;
    }
    clearDetail();
    m_missionId = missionId;
    m_memberHydrationId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_chatHydrationId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_taskLoad = TaskLoad {
        .token = QUuid::createUuidV7().toString(QUuid::WithoutBraces),
        .nextOffset = 0,
        .pageCount = 0,
        .tasks = {},
    };
    m_membersLoading = true;
    m_chatLoading = true;
    m_tasksLoading = true;
    emit stateChanged();

    const auto membersAccepted = m_dispatcher.send(
        CommandLane::Rooms,
        commandJson({
            {QStringLiteral("type"), QStringLiteral("room.refreshMembers")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("hydration_id"), m_memberHydrationId},
        }));
    const auto chatAccepted = m_dispatcher.send(
        CommandLane::Rooms,
        commandJson({
            {QStringLiteral("type"), QStringLiteral("room.chat.list")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("limit"), 500},
            {QStringLiteral("tail"), true},
            {QStringLiteral("hydration_id"), m_chatHydrationId},
        }));
    const auto tasksAccepted = requestTaskPage();
    if (membersAccepted && chatAccepted && tasksAccepted) {
        m_hydrationTimer.start(static_cast<int>(m_hydrationTimeoutMs));
        return true;
    }
    clearDetail();
    m_lastError =
        QStringLiteral("The runtime did not accept Mission detail hydration.");
    emit stateChanged();
    return false;
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
    closeMission();
}

void MissionDetailModel::activateAccount(
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
        clearDetail();
    }
    m_authenticated = authenticated;
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
            const auto found =
                std::ranges::find(messages, live.id, &MissionMessagesModel::Message::id);
            if (found == messages.end()) {
                messages.push_back(live);
            } else if (live.sequence >= found->sequence) {
                *found = live;
            }
        }
        m_messages.replace(std::move(messages));
        if (!hydration.isEmpty()) {
            m_chatHydrationId.clear();
            m_chatLoading = false;
        }
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
        m_messages.upsert(std::move(*message));
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
            const auto found =
                std::ranges::find(tasks, live.id, &MissionTasksModel::Task::id);
            if (found == tasks.end()) {
                tasks.push_back(live);
            } else if (live.revision > found->revision) {
                *found = live;
            }
        }
        m_tasks.replace(std::move(tasks));
        m_taskLoad.reset();
        m_tasksLoading = false;
        m_tasksAuthoritative = true;
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
        m_tasksAuthoritative = true;
        emit stateChanged();
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
        m_tasks.upsert(std::move(*task));
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
    m_tasksLoading = false;
    m_tasksAuthoritative = false;
    m_members.replace({});
    m_messages.replace({});
    m_tasks.replace({});
}

void MissionDetailModel::updateLoading()
{
    if (!loading()) {
        m_hydrationTimer.stop();
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
    m_tasksAuthoritative = false;
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
    const QString& missionId)
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
        stringArray(object, QStringLiteral("recipientSessionIds"));
    const auto recipientUsers =
        stringArray(object, QStringLiteral("recipientUserIds"));
    if (!id || !roomId || *roomId != missionId || !author || !kind || !body
        || !sequence || !postedAt || !recipientSessions || !recipientUsers
        || (*kind != QStringLiteral("Human")
            && *kind != QStringLiteral("Agent"))) {
        return std::nullopt;
    }
    const auto authorSession = object.value(QStringLiteral("authorSessionId"));
    if (!authorSession.isUndefined() && !authorSession.isNull()
        && (!authorSession.isString() || authorSession.toString().isEmpty())) {
        return std::nullopt;
    }
    return MissionMessagesModel::Message {
        .id = *id,
        .authorUserId = *author,
        .authorSessionId =
            authorSession.isString() ? authorSession.toString() : QString {},
        .authorKind = *kind,
        .body = *body,
        .recipientSessionIds = *recipientSessions,
        .recipientUserIds = *recipientUsers,
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
    QDateTime dueAt;
    const auto due = optionalString(object, QStringLiteral("dueAt"));
    if (!due.isEmpty()) {
        const auto parsed = rfc3339(due);
        if (!parsed) {
            return std::nullopt;
        }
        dueAt = *parsed;
    }
    const auto assignedSessionId =
        optionalString(object, QStringLiteral("assignedSessionId"));
    const auto assignedSessionIncarnationId =
        optionalString(
            object,
            QStringLiteral("assignedSessionIncarnationId"));
    if (assignedSessionId.isEmpty()
        != assignedSessionIncarnationId.isEmpty()) {
        return std::nullopt;
    }
    return MissionTasksModel::Task {
        .id = *id,
        .title = *title,
        .description = optionalString(object, QStringLiteral("description")),
        .status = *status,
        .assignedSessionId = assignedSessionId,
        .assignedSessionIncarnationId = assignedSessionIncarnationId,
        .result = optionalString(object, QStringLiteral("result")),
        .revision = *revision,
        .dueAt = dueAt,
        .updatedAt = *updatedAt,
    };
}

} // namespace kodosi
