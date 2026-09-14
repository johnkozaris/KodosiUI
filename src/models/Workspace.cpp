#include "models/Workspace.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUuid>
#include <algorithm>

namespace kodosi {
namespace {
    QVariantList list(const QJsonObject& o, const char* key)
    {
        return o.value(QLatin1String(key)).toArray().toVariantList();
    }
    bool validName(const QString& name)
    {
        return !name.trimmed().isEmpty() && name.trimmed().toUtf8().size() <= 128
            && !name.contains(QChar::Null);
    }
    QString requestId()
    {
        return QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    }
}
Workspace::Workspace(
    CommandDispatcher& commands, SessionCatalogModel& sessions, DesktopStateModel& desktop, QObject* parent)
    : QObject(parent)
    , m_commands(commands)
    , m_sessions(sessions)
    , m_desktop(desktop)
{
    m_timeout.setInterval(1000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_pending.isEmpty()) {
            m_timeout.stop();
            return;
        }
        bool expired = false;
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->deadline.hasExpired()) {
                if (it->operation == QStringLiteral("session.openRemote") && m_link
                    && m_link->sessionId == it->sessionId)
                    m_link.reset();
                it = m_pending.erase(it);
                expired = true;
            } else
                ++it;
        }
        if (m_pending.isEmpty())
            m_timeout.stop();
        if (expired) {
            activatePendingLink();
            emit busyChanged();
            setError(tr("The operation has not been confirmed. Refresh to check the current state."));
        }
    });
    connect(
        &sessions, &SessionCatalogModel::authoritativeSnapshotApplied, this, &Workspace::activatePendingLink);
    connect(&sessions, &SessionCatalogModel::authorityStateChanged, this, &Workspace::missionChanged);
}
bool Workspace::send(QString type, QJsonObject values, QString sessionId)
{
    if (m_pending.size() >= 32) {
        setError(tr("Wait for the current operations to finish."));
        return false;
    }
    const auto id = requestId();
    values.insert(QStringLiteral("type"), type);
    const bool tracked
        = (type.startsWith(QStringLiteral("session.")) && type != QStringLiteral("session.list")
              && type != QStringLiteral("session.disconnect"))
        || (type.startsWith(QStringLiteral("room.")) && type != QStringLiteral("room.list"));
    if (tracked || type == QStringLiteral("friends.request.send"))
        values.insert(QStringLiteral("requestId"), id);
    const auto result = m_commands.send(QJsonDocument(values).toJson(QJsonDocument::Compact));
    if (!result) {
        setError(result.error().message);
        return false;
    }
    if (tracked) {
        m_pending.insert(id,
            { type, sessionId, values.value(QStringLiteral("roomId")).toString(), QDeadlineTimer(15000) });
        if (!m_timeout.isActive())
            m_timeout.start();
        emit busyChanged();
    }
    if (tracked)
        clearError();
    return true;
}
void Workspace::login()
{
    send(QStringLiteral("auth.login.start"));
}
void Workspace::logout()
{
    send(QStringLiteral("auth.logout"));
}
void Workspace::refresh()
{
    m_sessions.beginRefresh();
    if (!send(QStringLiteral("session.list")))
        m_sessions.fail(m_error);
    if (signedIn()) {
        send(QStringLiteral("friends.refresh"));
        send(QStringLiteral("devices.refresh"));
        send(QStringLiteral("room.list"));
    }
    if (!m_selectedMission.isEmpty())
        openMission(m_selectedMission);
}
bool Workspace::activateSession(const QString& id)
{
    const auto s = m_sessions.session(id);
    if (!s && signedIn()) {
        for (const auto& pending : m_pending) {
            if (pending.operation == QStringLiteral("session.openRemote") && pending.sessionId == id)
                return true;
        }
        if (QUuid(id).isNull()) {
            setError(tr("The session link is invalid."));
            return false;
        }
        m_link = DeepLinkDestination { id };
        if (send(QStringLiteral("session.openRemote"), { { QStringLiteral("sessionId"), id } }, id))
            return true;
        m_link.reset();
        return false;
    }
    if (!s || s->connectionState == QStringLiteral("blocked") || s->status == QStringLiteral("closing")) {
        setError(s && !s->message.isEmpty() ? s->message : tr("This session is no longer available."));
        return false;
    }
    if (!m_desktop.stagedSessionIds().contains(id) && m_desktop.stagedSessionIds().size() >= 6)
        return m_desktop.stageSession(id);
    if (s->kind == QStringLiteral("remote")) {
        for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
            if (it->operation == QStringLiteral("session.openRemote") && it->sessionId == id) {
                if (!m_desktop.stageSession(id))
                    return false;
                emit sessionActivated(id);
                return true;
            }
        }
        if (!send(QStringLiteral("session.openRemote"), { { QStringLiteral("sessionId"), id } }, id))
            return false;
    }
    if (!m_desktop.stageSession(id))
        return false;
    emit sessionActivated(id);
    return true;
}
bool Workspace::closeView(const QString& id)
{
    if (!m_desktop.unstageSession(id))
        return false;
    if (m_link && m_link->sessionId == id)
        m_link.reset();
    for (auto pending = m_pending.begin(); pending != m_pending.end();) {
        if (pending->operation == QStringLiteral("session.openRemote") && pending->sessionId == id)
            pending = m_pending.erase(pending);
        else
            ++pending;
    }
    emit busyChanged();
    const auto session = m_sessions.session(id);
    if (session && session->kind == QStringLiteral("remote"))
        send(QStringLiteral("session.disconnect"), { { QStringLiteral("sessionId"), id } });
    return true;
}
bool Workspace::createSession(
    const QString& name, const QString& directory, const QString& provider, const QString& nativeId)
{
    QString title = name.trimmed();
    if (title.isEmpty()) {
        const QStringList adjectives { QStringLiteral("Amber"), QStringLiteral("Cedar"), QStringLiteral("Copper"), QStringLiteral("Jade"), QStringLiteral("Pearl"), QStringLiteral("Silver"), QStringLiteral("Slate"), QStringLiteral("Willow") };
        const QStringList animals { QStringLiteral("Falcon"), QStringLiteral("Fox"), QStringLiteral("Gecko"), QStringLiteral("Heron"), QStringLiteral("Lynx"), QStringLiteral("Otter"), QStringLiteral("Quail"), QStringLiteral("Wren") };
        title = adjectives.at(QRandomGenerator::global()->bounded(adjectives.size())) + QLatin1Char(' ')
            + animals.at(QRandomGenerator::global()->bounded(animals.size()));
    }
    if (!validName(title)) {
        setError(tr("Use a name of 1–128 UTF-8 bytes."));
        return false;
    }
    QJsonObject args { { QStringLiteral("name"), title } };
    if (!directory.isEmpty())
        args.insert(QStringLiteral("workingDir"), directory);
    if (!nativeId.isEmpty()) {
        if (provider != QStringLiteral("claude") && provider != QStringLiteral("copilot")) {
            setError(tr("Choose a supported provider."));
            return false;
        }
        args.insert(QStringLiteral("resume"),
            QJsonObject { { QStringLiteral("provider"), provider },
                { QStringLiteral("nativeConversationId"), nativeId } });
    }
    return send(QStringLiteral("session.create"), args);
}
bool Workspace::sessionCommand(QString type, const QString& id, QJsonObject values, bool ownerOnly)
{
    const auto s = m_sessions.session(id);
    if (!m_sessions.hasAuthoritativeSnapshot() || !s || s->connectionState == QStringLiteral("blocked")
        || (ownerOnly && !s->isOwner)) {
        setError(tr("This operation is not available for the selected session."));
        return false;
    }
    values.insert(QStringLiteral("sessionId"), id);
    values.insert(QStringLiteral("expectedRuntimeIncarnationId"), s->incarnationId);
    return send(std::move(type), std::move(values), id);
}
bool Workspace::closeSession(const QString& id)
{
    return sessionCommand(QStringLiteral("session.close"), id);
}
bool Workspace::shareSession(const QString& id, const QStringList& users, const QStringList& expectedUsers)
{
    const auto session = m_sessions.session(id);
    if (!session || session->kind != QStringLiteral("local")) {
        setError(tr("Change sharing on the computer hosting this terminal."));
        return false;
    }
    if (!signedIn()) {
        setError(tr("Sign in before sharing a terminal."));
        return false;
    }
    return sessionCommand(QStringLiteral("session.share"), id,
        { { QStringLiteral("userIds"), QJsonArray::fromStringList(users) },
            { QStringLiteral("expectedUserIds"), QJsonArray::fromStringList(expectedUsers) } }, true);
}
bool Workspace::leaveSession(const QString& id)
{
    return sessionCommand(QStringLiteral("session.leave"), id);
}
bool Workspace::attachMission(const QString& id, const QString& room)
{
    return sessionCommand(QStringLiteral("session.attachMission"), id,
        { { QStringLiteral("roomId"), room.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(room) } },
        true);
}
void Workspace::requestFriend(const QString& name)
{
    send(QStringLiteral("friends.request.send"), { { QStringLiteral("username"), name.trimmed() } });
}
void Workspace::acceptFriend(const QString& name)
{
    send(QStringLiteral("friends.request.accept"), { { QStringLiteral("username"), name } });
}
void Workspace::rejectFriend(const QString& name)
{
    send(QStringLiteral("friends.request.reject"), { { QStringLiteral("username"), name } });
}
void Workspace::cancelFriend(const QString& name)
{
    send(QStringLiteral("friends.request.cancel"), { { QStringLiteral("username"), name } });
}
void Workspace::removeFriend(const QString& name)
{
    send(QStringLiteral("friends.remove"), { { QStringLiteral("username"), name } });
}
void Workspace::revokeDevice(const QString& id)
{
    send(QStringLiteral("devices.revoke"), { { QStringLiteral("deviceId"), id } });
}
void Workspace::approveDevice(const QString& code)
{
    send(QStringLiteral("devices.link.approve"), { { QStringLiteral("userCode"), code } });
}
void Workspace::enrollDevice()
{
    send(QStringLiteral("devices.link.startSelf"));
}
void Workspace::cancelEnrollment()
{
    send(QStringLiteral("devices.link.cancelSelf"));
}
void Workspace::createMission(const QString& name)
{
    if (!validName(name)) {
        setError(tr("Use a name of 1–128 UTF-8 bytes."));
        return;
    }
    send(QStringLiteral("room.create"), { { QStringLiteral("name"), name.trimmed() } });
}
QStringList Workspace::missionSessionIds() const
{
    QStringList result;
    if (m_selectedMission.isEmpty() || !m_sessions.hasAuthoritativeSnapshot())
        return result;
    for (int row = 0; row < m_sessions.rowCount(); ++row) {
        const auto index = m_sessions.index(row, 0);
        if (m_sessions.data(index, SessionCatalogModel::RoomIdRole).toString() == m_selectedMission)
            result.append(m_sessions.data(index, SessionCatalogModel::SessionIdRole).toString());
    }
    return result;
}
void Workspace::openMission(const QString& id)
{
    m_selectedMission = id;
    m_mission.clear();
    m_members.clear();
    emit missionChanged();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->operation == QStringLiteral("room.open"))
            it = m_pending.erase(it);
        else
            ++it;
    }
    if (!id.isEmpty())
        send(QStringLiteral("room.open"), { { QStringLiteral("roomId"), id } });
}
void Workspace::missionCommand(QString type, QJsonObject values)
{
    if (m_selectedMission.isEmpty())
        return;
    values.insert(QStringLiteral("roomId"), m_selectedMission);
    send(std::move(type), std::move(values));
}
void Workspace::renameMission(const QString& name)
{
    if (!validName(name)) {
        setError(tr("Use a name of 1–128 UTF-8 bytes."));
        return;
    }
    missionCommand(QStringLiteral("room.rename"), { { QStringLiteral("name"), name.trimmed() } });
}
void Workspace::deleteMission()
{
    missionCommand(QStringLiteral("room.delete"));
}
void Workspace::inviteToMission(const QString& id)
{
    missionCommand(QStringLiteral("room.invite"), { { QStringLiteral("userId"), id } });
}
void Workspace::removeMember(const QString& id)
{
    missionCommand(QStringLiteral("room.removeMember"), { { QStringLiteral("userId"), id } });
}
void Workspace::leaveMission()
{
    missionCommand(QStringLiteral("room.leave"));
}
void Workspace::acceptInvitation(const QString& id)
{
    send(QStringLiteral("room.invitation.accept"), { { QStringLiteral("invitationId"), id } });
}
void Workspace::rejectInvitation(const QString& id)
{
    send(QStringLiteral("room.invitation.reject"), { { QStringLiteral("invitationId"), id } });
}
void Workspace::clearError()
{
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
}
void Workspace::setError(QString error)
{
    m_error = std::move(error);
    emit errorChanged();
}
void Workspace::clearAccount()
{
    m_pending.clear();
    m_timeout.stop();
    m_friends.clear();
    m_incoming.clear();
    m_outgoing.clear();
    m_devices.clear();
    m_deviceRequests.clear();
    m_missions.clear();
    m_invitations.clear();
    m_missionCatalogTruncated = false;
    m_mission.clear();
    m_members.clear();
    m_selectedMission.clear();
    m_selfDeviceCode.clear();
    m_selfDeviceId.clear();
    m_enrolled = false;
    if (signedIn()) {
        m_link.reset();
        m_links.clear();
    }
    m_desktop.clearSessions();
    m_sessions.resetRuntimeAuthority();
    emit busyChanged();
    emit peopleChanged();
    emit devicesChanged();
    emit missionsChanged();
    emit missionChanged();
}
void Workspace::reset()
{
    clearAccount();
    m_userId.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_accountEpoch.reset();
    m_finalizing = true;
    emit accountChanged();
    emit loginChanged();
}
void Workspace::route(const DeepLinkDestination& destination)
{
    if ((m_link && m_link->sessionId == destination.sessionId)
        || std::ranges::any_of(m_links, [&](const auto& link) { return link.sessionId == destination.sessionId; })) return;
    if (m_links.size() >= 32) { setError(tr("Too many pending session links.")); return; }
    m_links.append(destination);
    activatePendingLink();
}
void Workspace::activatePendingLink()
{
    if (!m_link && !m_links.isEmpty()) m_link = m_links.takeFirst();
    if (!m_link)
        return;
    const auto id = m_link->sessionId;
    if (m_sessions.containsSession(id)) {
        m_link.reset();
        activateSession(id);
        activatePendingLink();
        return;
    }
    if (m_finalizing)
        return;
    if (!signedIn()) {
        setError(tr("Sign in to open the linked terminal."));
        m_desktop.setActiveView(3);
        return;
    }
    activateSession(id);
}
void Workspace::apply(const QJsonObject& event, std::uint64_t accountEpoch)
{
    const auto type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("auth.finalizing")) {
        m_finalizing = true;
        emit loginChanged();
        return;
    }
    if (type == QStringLiteral("auth.ready") || type == QStringLiteral("auth.required")) {
        m_finalizing = false;
        const auto epoch = accountEpoch;
        const auto user = type == QStringLiteral("auth.ready")
            ? event.value(QStringLiteral("userId")).toString()
            : QString {};
        if (epoch != m_accountEpoch || user != m_userId) {
            clearAccount();
            m_accountEpoch = epoch;
            m_userId = user;
        }
        m_userCode.clear();
        m_verificationUri.clear();
        emit accountChanged();
        emit loginChanged();
        refresh();
        activatePendingLink();
        return;
    }
    if (type == QStringLiteral("auth.device_code")) {
        m_finalizing = false;
        m_userCode = event.value(QStringLiteral("userCode")).toString();
        m_verificationUri = event.value(QStringLiteral("verificationUri")).toString();
        emit loginChanged();
        return;
    }
    if (type == QStringLiteral("auth.notice")) {
        if (!event.value(QStringLiteral("message")).toString().isEmpty())
            setError(event.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QStringLiteral("sessions.snapshot")) {
        m_sessions.apply(event);
        if (!m_sessions.hasAuthoritativeSnapshot()) {
            setError(m_sessions.authorityError());
            return;
        }
        QHash<QString, QString> createdSessions;
        for (const auto& value : event.value(QStringLiteral("sessions")).toArray()) {
            const auto session = value.toObject();
            const auto request = session.value(QStringLiteral("createRequestId")).toString();
            if (!request.isEmpty()) createdSessions.insert(request, session.value(QStringLiteral("id")).toString());
        }
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            const auto pending = it.value();
            if (pending.operation == QStringLiteral("session.create")) {
                const auto created = createdSessions.value(it.key());
                if (!created.isEmpty()) {
                    it = m_pending.erase(it);
                    activateSession(created);
                    continue;
                }
            } else if (pending.operation == QStringLiteral("session.close")
                && !m_sessions.containsSession(pending.sessionId)) {
                it = m_pending.erase(it);
                continue;
            }
            ++it;
        }
        emit busyChanged();
        return;
    }
    if (type == QStringLiteral("friends.snapshot")) {
        m_friends = list(event, "friends");
        m_incoming = list(event, "incoming");
        m_outgoing = list(event, "outgoing");
        emit peopleChanged();
        return;
    }
    if (type == QStringLiteral("devices.list")) {
        m_devices = list(event, "devices");
        m_selfDeviceId = event.value(QStringLiteral("selfDeviceId")).toString();
        m_enrolled = event.value(QStringLiteral("localDeviceEnrolled")).toBool();
        emit devicesChanged();
        return;
    }
    if (type == QStringLiteral("devices.link.snapshot")) {
        m_deviceRequests = list(event, "requests");
        emit devicesChanged();
        return;
    }
    if (type == QStringLiteral("devices.link.selfPending")) {
        m_selfDeviceCode = event.value(QStringLiteral("userCode")).toString();
        emit devicesChanged();
        return;
    }
    if (type == QStringLiteral("devices.link.selfResolved")) {
        m_selfDeviceCode.clear();
        emit devicesChanged();
        send(QStringLiteral("devices.refresh"));
        return;
    }
    if (type == QStringLiteral("devices.link.resolved")) {
        send(QStringLiteral("devices.refresh"));
        return;
    }
    if (type == QStringLiteral("rooms.snapshot")) {
        m_missions = list(event, "rooms");
        m_invitations = list(event, "invitations");
        const bool roomsTruncated = event.value(QStringLiteral("roomsTruncated")).toBool();
        m_missionCatalogTruncated = roomsTruncated
            || event.value(QStringLiteral("invitationsTruncated")).toBool();
        emit missionsChanged();
        if (!m_selectedMission.isEmpty()) {
            const auto exists = std::any_of(m_missions.cbegin(), m_missions.cend(), [&](const QVariant& room) {
                return room.toMap().value(QStringLiteral("id")).toString() == m_selectedMission;
            });
            if (!exists && !roomsTruncated) openMission({});
            else openMission(m_selectedMission);
        }
        return;
    }
    if (type == QStringLiteral("room.snapshot")) {
        const auto room = event.value(QStringLiteral("room")).toObject();
        const auto request = event.value(QStringLiteral("requestId")).toString();
        const auto pending = m_pending.value(request);
        if (pending.operation != QStringLiteral("room.open") || pending.roomId != m_selectedMission)
            return;
        if (room.value(QStringLiteral("id")).toString() == m_selectedMission) {
            m_mission = room.toVariantMap();
            m_members = list(event, "members");
            emit missionChanged();
        }
    }
    const auto id = event.value(QStringLiteral("requestId")).toString();
    if (type == QStringLiteral("session.result") || type == QStringLiteral("room.result")
        || type == QStringLiteral("room.snapshot")) {
        if (!m_pending.contains(id))
            return;
        const auto expected = m_pending.value(id).operation;
        if (type != QStringLiteral("room.snapshot")
            && event.value(QStringLiteral("operation")).toString() != expected)
            return;
        const auto pending = m_pending.take(id);
        emit busyChanged();
        if (pending.operation == QStringLiteral("session.create")
            && !event.value(QStringLiteral("sessionId")).toString().isEmpty()) {
            const auto sessionId = event.value(QStringLiteral("sessionId")).toString();
            if (m_sessions.containsSession(sessionId))
                activateSession(sessionId);
            else
                m_link = DeepLinkDestination { sessionId };
        }
        if (type == QStringLiteral("room.result")) {
            if ((pending.operation == QStringLiteral("room.delete")
                    || pending.operation == QStringLiteral("room.leave"))
                && pending.roomId == m_selectedMission)
                openMission({});
            send(QStringLiteral("room.list"));
        }
        return;
    }
    if (type == QStringLiteral("provider.error"))
        return;
    if (type.endsWith(QStringLiteral(".error"))) {
        const auto pending = m_pending.value(id);
        if (pending.operation == QStringLiteral("session.openRemote") && m_link
            && m_link->sessionId == pending.sessionId) {
            m_link.reset();
            activatePendingLink();
        }
        if (type == QStringLiteral("auth.error")) {
            m_finalizing = false;
            m_userCode.clear();
            emit loginChanged();
        }
        if (pending.operation == QStringLiteral("room.open") && pending.roomId == m_selectedMission)
            openMission({});
        m_pending.remove(id);
        emit busyChanged();
        setError(event.value(QStringLiteral("message")).toString());
        if (type == QStringLiteral("session.error")
            && event.value(QStringLiteral("operation")).toString() == QStringLiteral("session.list"))
            m_sessions.fail(m_error);
    }
}
}
