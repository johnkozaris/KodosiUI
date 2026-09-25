#include "presentation/MissionsModel.hpp"
#include "presentation/SessionCatalogModel.hpp"
#include "presentation/Workspace.hpp"

#include <QJsonValue>

namespace kodosi {
MissionsModel::MissionsModel(Workspace& workspace, SessionCatalogModel& sessions)
    : m_workspace(workspace)
    , m_sessions(sessions)
{
    connect(&sessions, &SessionCatalogModel::authorityStateChanged, this, &MissionsModel::selectionChanged);
}

bool MissionsModel::busy() const
{
    return m_workspace.hasPending(Workspace::PendingDomain::Mission);
}

QStringList MissionsModel::sessionIds() const
{
    QStringList result;
    if (m_selectedId.isEmpty() || !m_sessions.hasAuthoritativeSnapshot())
        return result;
    for (int row = 0; row < m_sessions.rowCount(); ++row) {
        const auto index = m_sessions.index(row, 0);
        if (m_sessions.data(index, SessionCatalogModel::MissionIdRole).toString() == m_selectedId)
            result.append(m_sessions.data(index, SessionCatalogModel::SessionIdRole).toString());
    }
    return result;
}

void MissionsModel::create(const QString& name)
{
    if (!Workspace::validName(name)) {
        m_workspace.setError(tr("Use a name of 1–128 UTF-8 bytes."));
        return;
    }
    m_workspace.sendTracked(Workspace::PendingDomain::Mission, QStringLiteral("mission.create"),
        { { QStringLiteral("name"), name.trimmed() } });
}

void MissionsModel::open(const QString& id)
{
    m_selectedId = id;
    m_selected.clear();
    m_members.clear();
    emit selectionChanged();
    for (auto it = m_workspace.m_pending.begin(); it != m_workspace.m_pending.end();) {
        if (it->operation == QStringLiteral("mission.open"))
            it = m_workspace.m_pending.erase(it);
        else
            ++it;
    }
    m_workspace.notifyBusy();
    if (!id.isEmpty()) {
        m_workspace.sendTracked(Workspace::PendingDomain::Mission, QStringLiteral("mission.open"),
            { { QStringLiteral("missionId"), id } }, {}, id);
    }
}

void MissionsModel::command(QString type, QJsonObject values)
{
    if (m_selectedId.isEmpty())
        return;
    values.insert(QStringLiteral("missionId"), m_selectedId);
    m_workspace.sendTracked(
        Workspace::PendingDomain::Mission, std::move(type), std::move(values), {}, m_selectedId);
}

void MissionsModel::rename(const QString& name)
{
    if (!Workspace::validName(name)) {
        m_workspace.setError(tr("Use a name of 1–128 UTF-8 bytes."));
        return;
    }
    command(QStringLiteral("mission.rename"), { { QStringLiteral("name"), name.trimmed() } });
}

void MissionsModel::remove()
{
    command(QStringLiteral("mission.delete"));
}

void MissionsModel::invite(const QString& userId)
{
    command(QStringLiteral("mission.invite"), { { QStringLiteral("userId"), userId } });
}

void MissionsModel::removeMember(const QString& userId)
{
    command(QStringLiteral("mission.removeMember"), { { QStringLiteral("userId"), userId } });
}

void MissionsModel::leave()
{
    command(QStringLiteral("mission.leave"));
}

void MissionsModel::acceptInvitation(const QString& id)
{
    m_workspace.sendTracked(Workspace::PendingDomain::Mission,
        QStringLiteral("mission.invitation.accept"),
        { { QStringLiteral("invitationId"), id } });
}

void MissionsModel::declineInvitation(const QString& id)
{
    m_workspace.sendTracked(Workspace::PendingDomain::Mission,
        QStringLiteral("mission.invitation.reject"),
        { { QStringLiteral("invitationId"), id } });
}

void MissionsModel::reset()
{
    m_missions.clear();
    m_invitations.clear();
    m_selected.clear();
    m_members.clear();
    m_selectedId.clear();
    m_catalogTruncated = false;
    emit catalogChanged();
    emit selectionChanged();
}
}
