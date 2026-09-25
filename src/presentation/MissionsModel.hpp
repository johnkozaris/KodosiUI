#pragma once

#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace kodosi {
class SessionCatalogModel;
class Workspace;

class MissionsModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList missions READ missions NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList invitations READ invitations NOTIFY catalogChanged)
    Q_PROPERTY(bool catalogTruncated READ catalogTruncated NOTIFY catalogChanged)
    Q_PROPERTY(QVariantMap selectedMission READ selectedMission NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList members READ members NOTIFY selectionChanged)
    Q_PROPERTY(QStringList sessionIds READ sessionIds NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedMissionId READ selectedMissionId NOTIFY selectionChanged)

public:
    MissionsModel(Workspace& workspace, SessionCatalogModel& sessions);

    bool busy() const;
    QVariantList missions() const { return m_missions; }
    QVariantList invitations() const { return m_invitations; }
    bool catalogTruncated() const { return m_catalogTruncated; }
    QVariantMap selectedMission() const { return m_selected; }
    QVariantList members() const { return m_members; }
    QStringList sessionIds() const;
    QString selectedMissionId() const { return m_selectedId; }

    Q_INVOKABLE void create(const QString& name);
    Q_INVOKABLE void open(const QString& id);
    Q_INVOKABLE void rename(const QString& name);
    Q_INVOKABLE void remove();
    Q_INVOKABLE void invite(const QString& userId);
    Q_INVOKABLE void removeMember(const QString& userId);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void acceptInvitation(const QString& id);
    Q_INVOKABLE void declineInvitation(const QString& id);

signals:
    void busyChanged();
    void catalogChanged();
    void selectionChanged();

private:
    friend class Workspace;

    Workspace& m_workspace;
    SessionCatalogModel& m_sessions;
    QVariantList m_missions;
    QVariantList m_invitations;
    QVariantMap m_selected;
    QVariantList m_members;
    QString m_selectedId;
    bool m_catalogTruncated = false;

    void command(QString type, QJsonObject values = {});
    void reset();
};
}
