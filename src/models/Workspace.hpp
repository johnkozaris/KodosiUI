#pragma once

#include <QDeadlineTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

#include "app/DeepLinkRouter.hpp"
#include "bridge/RuntimeBridge.hpp"

namespace kodosi {
class SessionCatalogModel;
class DesktopStateModel;

class Workspace final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY loginChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY accountChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY loginChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY loginChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList friends READ friends NOTIFY peopleChanged)
    Q_PROPERTY(QVariantList incoming READ incoming NOTIFY peopleChanged)
    Q_PROPERTY(QVariantList outgoing READ outgoing NOTIFY peopleChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList deviceRequests READ deviceRequests NOTIFY devicesChanged)
    Q_PROPERTY(QString selfDeviceCode READ selfDeviceCode NOTIFY devicesChanged)
    Q_PROPERTY(bool localDeviceEnrolled READ localDeviceEnrolled NOTIFY devicesChanged)
    Q_PROPERTY(QString selfDeviceId READ selfDeviceId NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList missions READ missions NOTIFY missionsChanged)
    Q_PROPERTY(QVariantList invitations READ invitations NOTIFY missionsChanged)
    Q_PROPERTY(QVariantMap mission READ mission NOTIFY missionChanged)
    Q_PROPERTY(QVariantList missionMembers READ missionMembers NOTIFY missionChanged)
    Q_PROPERTY(QStringList missionSessionIds READ missionSessionIds NOTIFY missionChanged)
    Q_PROPERTY(QString selectedMissionId READ selectedMissionId NOTIFY missionChanged)
public:
    Workspace(CommandDispatcher& commands, SessionCatalogModel& sessions, DesktopStateModel& desktop,
        QObject* parent = nullptr);
    bool signedIn() const { return !m_userId.isEmpty(); }
    bool signingIn() const { return m_finalizing || !m_userCode.isEmpty(); }
    QString userId() const { return m_userId; }
    QString userCode() const { return m_userCode; }
    QString verificationUri() const { return m_verificationUri; }
    QString error() const { return m_error; }
    bool busy() const { return !m_pending.isEmpty(); }
    QVariantList friends() const { return m_friends; }
    QVariantList incoming() const { return m_incoming; }
    QVariantList outgoing() const { return m_outgoing; }
    QVariantList devices() const { return m_devices; }
    QVariantList deviceRequests() const { return m_deviceRequests; }
    QString selfDeviceCode() const { return m_selfDeviceCode; }
    bool localDeviceEnrolled() const { return m_enrolled; }
    QString selfDeviceId() const { return m_selfDeviceId; }
    QVariantList missions() const { return m_missions; }
    QVariantList invitations() const { return m_invitations; }
    QVariantMap mission() const { return m_mission; }
    QVariantList missionMembers() const { return m_members; }
    QStringList missionSessionIds() const { return m_missionSessions; }
    QString selectedMissionId() const { return m_selectedMission; }
    Q_INVOKABLE void login();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool activateSession(const QString& sessionId);
    Q_INVOKABLE bool closeView(const QString& sessionId);
    Q_INVOKABLE bool createSession(const QString& name, const QString& directory,
        const QString& provider = {}, const QString& nativeId = {});
    Q_INVOKABLE bool stopSession(const QString& sessionId);
    Q_INVOKABLE bool shareSession(const QString& sessionId, const QStringList& userIds, const QStringList& expectedUsers = {});
    Q_INVOKABLE bool leaveSession(const QString& sessionId);
    Q_INVOKABLE bool attachMission(const QString& sessionId, const QString& missionId);
    Q_INVOKABLE void requestFriend(const QString& username);
    Q_INVOKABLE void acceptFriend(const QString& username);
    Q_INVOKABLE void rejectFriend(const QString& username);
    Q_INVOKABLE void cancelFriend(const QString& username);
    Q_INVOKABLE void removeFriend(const QString& username);
    Q_INVOKABLE void revokeDevice(const QString& deviceId);
    Q_INVOKABLE void approveDevice(const QString& code);
    Q_INVOKABLE void enrollDevice();
    Q_INVOKABLE void cancelEnrollment();
    Q_INVOKABLE void createMission(const QString& name, const QString& slug);
    Q_INVOKABLE void openMission(const QString& id);
    Q_INVOKABLE void renameMission(const QString& name);
    Q_INVOKABLE void deleteMission();
    Q_INVOKABLE void inviteToMission(const QString& userId);
    Q_INVOKABLE void removeMember(const QString& userId);
    Q_INVOKABLE void leaveMission();
    Q_INVOKABLE void acceptInvitation(const QString& id);
    Q_INVOKABLE void rejectInvitation(const QString& id);
    Q_INVOKABLE void clearError();
    void apply(const QJsonObject& event);
    void reset();
    void route(const DeepLinkDestination& destination);
    void setError(QString error);
signals:
    void accountChanged();
    void loginChanged();
    void errorChanged();
    void busyChanged();
    void peopleChanged();
    void devicesChanged();
    void missionsChanged();
    void missionChanged();
    void sessionActivated(QString sessionId);

private:
    struct Pending {
        QString operation;
        QString sessionId;
        QString roomId;
        QDeadlineTimer deadline;
    };
    CommandDispatcher& m_commands;
    SessionCatalogModel& m_sessions;
    DesktopStateModel& m_desktop;
    QHash<QString, Pending> m_pending;
    QTimer m_timeout;
    QString m_userId, m_userCode, m_verificationUri, m_error, m_selfDeviceCode, m_selfDeviceId;
    QString m_selectedMission;
    qint64 m_accountEpoch = -1;
    bool m_enrolled = false;
    bool m_finalizing = true;
    QVariantList m_friends, m_incoming, m_outgoing, m_devices, m_deviceRequests, m_missions, m_invitations,
        m_members;
    QVariantMap m_mission;
    QStringList m_missionSessions;
    std::optional<DeepLinkDestination> m_link;
    QList<DeepLinkDestination> m_links;
    bool send(QString type, QJsonObject values = {}, QString sessionId = {});
    bool sessionCommand(QString type, const QString& id, QJsonObject values = {}, bool ownerOnly = false);
    void missionCommand(QString type, QJsonObject values = {});
    void clearAccount();
    void activatePendingLink();
};
}
