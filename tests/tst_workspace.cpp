#include "SessionFixture.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/Workspace.hpp"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>
#include <limits>

class Commands final : public kodosi::CommandDispatcher {
public:
    QList<QJsonObject> values;
    bool reject = false;
    Result send(QByteArrayView bytes) override
    {
        if (reject)
            return std::unexpected(kodosi::RuntimeFailure {
                kodosi::RuntimeFailure::Code::FfiRejected, 6, QStringLiteral("Busy") });
        values.append(QJsonDocument::fromJson(bytes.toByteArray()).object());
        return {};
    }
};
struct Fixture {
    QTemporaryDir dir;
    Commands commands;
    kodosi::SessionCatalogModel sessions;
    kodosi::DesktopStateModel desktop {
        std::make_unique<QSettings>(dir.filePath(QStringLiteral("ui.ini")), QSettings::IniFormat), false
    };
    kodosi::Workspace workspace { commands, sessions, desktop };
    Fixture() { desktop.attachSessionCatalog(&sessions); }
};
class WorkspaceTest final : public QObject {
    Q_OBJECT
private slots:
    void terminalControlFollowsOneConnectionState()
    {
        Fixture f;
        auto connected = test::session(1, true, false);
        connected.insert(QStringLiteral("connectionState"), QStringLiteral("connected"));
        f.sessions.apply(test::snapshot({ connected }));
        const auto presentation = f.sessions.presentationSession(test::id(1));
        QVERIFY(presentation);
        QVERIFY(presentation->canControl);
        const auto fields = f.sessions.presentationForSession(test::id(1));
        QVERIFY(fields.value(QStringLiteral("canControl")).toBool());
        for (const auto* field : { "canSendInput", "canRetainFocus", "canSendFocus", "canResize", "isStageReady" })
            QVERIFY(!fields.contains(QLatin1String(field)));
        for (const auto* state : { "connecting", "offline", "blocked" }) {
            connected.insert(QStringLiteral("connectionState"), QLatin1String(state));
            f.sessions.apply(test::snapshot({ connected }));
            QVERIFY(!f.sessions.presentationSession(test::id(1))->canControl);
        }
        f.sessions.apply(test::snapshot({ test::session(1) }));
        QVERIFY(f.sessions.presentationSession(test::id(1))->canControl);
    }
    void folderGroupsKeepNamesAndRemoteDirectoriesPresentationOnly()
    {
        Fixture f;
        auto local = test::session(1);
        local.insert(QStringLiteral("workingDir"), QStringLiteral("/repo/project"));
        local.insert(QStringLiteral("title"), QStringLiteral("GitHub Copilot"));
        auto remote = test::session(2, true, false);
        remote.insert(QStringLiteral("workingDir"), QStringLiteral("/host/project"));
        f.sessions.apply(test::snapshot({ local, remote }));
        QCOMPARE(f.sessions.folderGroups().size(), 2);
        const auto fields = f.sessions.presentationForSession(test::id(1));
        QCOMPARE(fields.value(QStringLiteral("name")).toString(), QStringLiteral("Terminal"));
        QCOMPARE(fields.value(QStringLiteral("headerTitle")).toString(), QStringLiteral("Terminal · GitHub Copilot"));
        const auto hosted = f.sessions.presentationForSession(test::id(2));
        QVERIFY(hosted.value(QStringLiteral("workingDirectory")).toString().isEmpty());
        QCOMPARE(hosted.value(QStringLiteral("displayDirectory")).toString(), QStringLiteral("/host/project"));
    }
    void remoteActivationAndClose()
    {
        Fixture f;
        auto remote = test::session(1, true, false);
        remote.insert(QStringLiteral("connectionState"), QStringLiteral("offline"));
        f.workspace.apply(test::snapshot({ remote }), 0);
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("session.openRemote"));
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
        QVERIFY(f.workspace.closeSession(test::id(1)));
        const auto command = f.commands.values.last();
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("session.close"));
        QCOMPARE(command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(), test::id(101));
        QVERIFY(f.workspace.closeView(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),QStringLiteral("session.disconnect"));
        QVERIFY(f.sessions.containsSession(test::id(1)));
        QVERIFY(!f.workspace.shareSession(test::id(1), {}));
    }
    void connectedRemoteActivationAcquiresThisClientsDemand()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({test::session(1, true, false)}), 0);
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("session.openRemote"));
        const auto count = f.commands.values.size();
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QCOMPARE(f.commands.values.size(), count);
        QVERIFY(f.workspace.closeView(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("session.disconnect"));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("session.openRemote"));
        QCOMPARE(f.commands.values.size(), count + 2);
    }
    void minimizeKeepsProcessAndClosePrunesFromCatalog()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({ test::session(1) }), 0);
        QVERIFY(f.workspace.activateSession(test::id(1)));
        f.commands.values.clear();
        QVERIFY(f.workspace.closeView(test::id(1)));
        QVERIFY(f.commands.values.isEmpty());
        QVERIFY(f.sessions.containsSession(test::id(1)));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QVERIFY(f.workspace.closeSession(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("session.close"));
        QVERIFY(f.workspace.busy());
        QVERIFY(f.sessions.containsSession(test::id(1)));
        QVERIFY(f.desktop.stagedSessionIds().contains(test::id(1)));
        f.workspace.apply(test::snapshot({}), 0);
        QVERIFY(!f.workspace.busy());
        QVERIFY(!f.sessions.containsSession(test::id(1)));
        QVERIFY(f.desktop.stagedSessionIds().isEmpty());
    }
    void closingStatusDisablesActivationAndControl()
    {
        Fixture f;
        auto closing = test::session(1);
        closing.insert(QStringLiteral("status"), QStringLiteral("closing"));
        f.workspace.apply(test::snapshot({ closing }), 0);
        QVERIFY(f.sessions.hasAuthoritativeSnapshot());
        QCOMPARE(f.sessions.session(test::id(1))->status, QStringLiteral("closing"));
        QVERIFY(!f.sessions.presentationSession(test::id(1))->canControl);
        QVERIFY(!f.workspace.activateSession(test::id(1)));
        QVERIFY(f.commands.values.isEmpty());
        closing.insert(QStringLiteral("status"), QStringLiteral("stopping"));
        f.workspace.apply(test::snapshot({ closing }), 0);
        QCOMPARE(f.sessions.authorityState(), kodosi::SessionCatalogModel::Failed);
    }
    void createsOnlyAfterCorrelatedSnapshot()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({}), 0);
        QVERIFY(f.workspace.createSession(QStringLiteral("Work"), QStringLiteral("/repo")));
        QVERIFY(f.desktop.selectedSessionId().isEmpty());
        auto created = test::session(1);
        created.insert(
            QStringLiteral("createRequestId"), f.commands.values.last().value(QStringLiteral("requestId")));
        f.workspace.apply(test::snapshot({ created }), 0);
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
        QVERIFY(!f.workspace.busy());
    }
    void startupLinkSurvivesEmptyCatalogAndSignIn()
    {
        Fixture f;
        f.workspace.route({ test::id(1) });
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.finalizing") } }, 0);
        f.workspace.apply(test::snapshot({}), 0);
        QVERIFY(f.commands.values.isEmpty());
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.required") },
            { QStringLiteral("accountEpoch"), 0 } }, 0);
        f.workspace.apply(test::snapshot({}), 0);
        QCOMPARE(f.desktop.activeView(), 3);
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } }, 1);
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("session.openRemote"));
        f.workspace.apply(test::snapshot({ test::session(1, true, false) }), 0);
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
    }
    void queuedStartupLinksAreNotOverwritten()
    {
        Fixture f;
        f.workspace.route({ test::id(1) });
        f.workspace.route({ test::id(2) });
        f.workspace.apply(test::snapshot({ test::session(1, true, false), test::session(2, true, false) }), 0);
        QVERIFY(f.desktop.stagedSessionIds().contains(test::id(1)));
        QVERIFY(f.desktop.stagedSessionIds().contains(test::id(2)));
    }
    void accountChangeClearsViewsAndPending()
    {
        Fixture f;
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } }, 1);
        f.workspace.apply(test::snapshot({ test::session(1) }), 0);
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QVERIFY(f.workspace.closeSession(test::id(1)));
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.required") },
            { QStringLiteral("accountEpoch"), 2 } }, 2);
        QVERIFY(f.desktop.stagedSessionIds().isEmpty());
        QVERIFY(!f.workspace.busy());
        QVERIFY(!f.workspace.signedIn());
    }
    void boundedStageAndInvalidSnapshot()
    {
        Fixture f;
        QJsonArray entries;
        for (int i = 1; i <= 7; ++i)
            entries.append(test::session(i));
        f.workspace.apply(test::snapshot(entries), 0);
        for (int i = 1; i <= 6; ++i)
            QVERIFY(f.workspace.activateSession(test::id(i)));
        QVERIFY(!f.workspace.activateSession(test::id(7)));
        auto invalid = test::session(9);
        invalid.insert(QStringLiteral("incarnationId"), QStringLiteral("bad"));
        f.workspace.apply(test::snapshot({ invalid }), 0);
        QCOMPARE(f.sessions.authorityState(), kodosi::SessionCatalogModel::Failed);
    }
    void missionRepliesAreRequestScopedAndDeleteKeepsTerminal()
    {
        Fixture f;
        auto terminal = test::session(1);
        terminal.insert(QStringLiteral("roomId"), test::id(11));
        f.workspace.apply(test::snapshot({ terminal }), 0);
        f.workspace.openMission(test::id(10));
        const auto oldRequest = f.commands.values.last().value(QStringLiteral("requestId"));
        f.workspace.openMission(test::id(11));
        const auto request = f.commands.values.last().value(QStringLiteral("requestId"));
        const QJsonObject room { { QStringLiteral("id"), test::id(11) },
            { QStringLiteral("name"), QStringLiteral("Project") },
            { QStringLiteral("ownerUserId"), test::id(50) } };
        QJsonObject reply { { QStringLiteral("type"), QStringLiteral("room.snapshot") },
            { QStringLiteral("requestId"), oldRequest }, { QStringLiteral("room"), room },
            { QStringLiteral("members"), QJsonArray {} } };
        f.workspace.apply(reply, 0);
        QVERIFY(f.workspace.mission().isEmpty());
        reply.insert(QStringLiteral("requestId"), request);
        f.workspace.apply(reply, 0);
        QCOMPARE(f.workspace.missionSessionIds(), QStringList { test::id(1) });
        f.workspace.apply({{QStringLiteral("type"),QStringLiteral("rooms.snapshot")},{QStringLiteral("rooms"),QJsonArray{}},{QStringLiteral("invitations"),QJsonArray{}}}, 0);
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        f.workspace.openMission(test::id(11));
        f.workspace.deleteMission();
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("room.result") },
            { QStringLiteral("operation"), QStringLiteral("room.delete") },
            { QStringLiteral("requestId"), f.commands.values.last().value(QStringLiteral("requestId")) },
            { QStringLiteral("roomId"), test::id(11) } }, 0);
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        QVERIFY(f.sessions.containsSession(test::id(1)));
    }
    void missionCreationNeedsOnlyNameAndAttachedTerminalsFollowCatalog()
    {
        Fixture f;
        f.workspace.createMission(QStringLiteral("  Release work  "));
        QCOMPARE(f.commands.values.size(), 1);
        const auto created = f.commands.values.first();
        QCOMPARE(created.value(QStringLiteral("type")).toString(), QStringLiteral("room.create"));
        QCOMPARE(created.value(QStringLiteral("name")).toString(), QStringLiteral("Release work"));
        QCOMPARE(created.size(), 3);
        f.workspace.createMission(QStringLiteral("   "));
        QCOMPARE(f.commands.values.size(), 1);
        auto terminal = test::session(1);
        terminal.insert(QStringLiteral("roomId"), test::id(10));
        f.workspace.apply(test::snapshot({ terminal }), 0);
        f.workspace.openMission(test::id(10));
        QCOMPARE(f.workspace.missionSessionIds(), QStringList {test::id(1)});
        QSignalSpy changed(&f.workspace, &kodosi::Workspace::missionChanged);
        terminal.insert(QStringLiteral("roomId"), test::id(11));
        f.workspace.apply(test::snapshot({ terminal }), 0);
        QVERIFY(!changed.isEmpty());
        QVERIFY(f.workspace.missionSessionIds().isEmpty());
        f.workspace.openMission(test::id(11));
        QCOMPARE(f.workspace.missionSessionIds(), QStringList {test::id(1)});
        f.sessions.beginRefresh();
        QVERIFY(f.workspace.missionSessionIds().isEmpty());
        f.workspace.apply(test::snapshot({}), 0);
        QVERIFY(f.workspace.missionSessionIds().isEmpty());
    }
    void truncatedMissionCatalogRetainsSelectionAndAllowsRecovery()
    {
        Fixture f;
        const QJsonObject room {{QStringLiteral("id"), test::id(11)},
            {QStringLiteral("name"), QStringLiteral("Beyond the page")},
            {QStringLiteral("ownerUserId"), test::id(50)}};
        f.workspace.openMission(test::id(11));
        const auto previousRequest = f.commands.values.last().value(QStringLiteral("requestId"));
        QJsonObject snapshot {{QStringLiteral("type"), QStringLiteral("rooms.snapshot")},
            {QStringLiteral("rooms"), QJsonArray {}},
            {QStringLiteral("invitations"), QJsonArray {QJsonObject {
                {QStringLiteral("id"), test::id(12)}, {QStringLiteral("roomId"), test::id(10)},
                {QStringLiteral("roomName"), QStringLiteral("Invitation")}}}},
            {QStringLiteral("roomsTruncated"), true},
            {QStringLiteral("invitationsTruncated"), true}};
        f.workspace.apply(snapshot, 0);
        QVERIFY(f.workspace.missionCatalogTruncated());
        QCOMPARE(f.workspace.selectedMissionId(), test::id(11));
        const auto detailRequest = f.commands.values.last();
        QCOMPARE(detailRequest.value(QStringLiteral("type")).toString(), QStringLiteral("room.open"));
        QCOMPARE(detailRequest.value(QStringLiteral("roomId")).toString(), test::id(11));
        QVERIFY(detailRequest.value(QStringLiteral("requestId")) != previousRequest);
        f.workspace.apply({{QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("requestId"), detailRequest.value(QStringLiteral("requestId"))},
            {QStringLiteral("room"), room}, {QStringLiteral("members"), QJsonArray {}}}, 0);
        QCOMPARE(f.workspace.mission().value(QStringLiteral("name")).toString(), QStringLiteral("Beyond the page"));
        f.workspace.rejectInvitation(test::id(12));
        auto command = f.commands.values.last();
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("room.invitation.reject"));
        f.workspace.apply({{QStringLiteral("type"), QStringLiteral("room.result")},
            {QStringLiteral("operation"), command.value(QStringLiteral("type"))},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))}}, 0);
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("room.list"));
        f.workspace.leaveMission();
        command = f.commands.values.last();
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("room.leave"));
        QCOMPARE(command.value(QStringLiteral("roomId")).toString(), test::id(11));
        f.workspace.apply({{QStringLiteral("type"), QStringLiteral("room.result")},
            {QStringLiteral("operation"), command.value(QStringLiteral("type"))},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))}}, 0);
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(), QStringLiteral("room.list"));
        snapshot.remove(QStringLiteral("roomsTruncated"));
        snapshot.remove(QStringLiteral("invitationsTruncated"));
        f.workspace.apply(snapshot, 0);
        QVERIFY(!f.workspace.missionCatalogTruncated());
        snapshot.insert(QStringLiteral("invitationsTruncated"), true);
        f.workspace.openMission(test::id(11));
        f.workspace.apply(snapshot, 0);
        QVERIFY(f.workspace.missionCatalogTruncated());
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        f.workspace.reset();
        QVERIFY(!f.workspace.missionCatalogTruncated());
    }
    void unavailableMissionOutsideTruncatedPageClearsOnlyCurrentDetail()
    {
        Fixture f;
        f.workspace.openMission(test::id(10));
        const auto stale = f.commands.values.last();
        f.workspace.apply({{QStringLiteral("type"), QStringLiteral("rooms.snapshot")},
            {QStringLiteral("rooms"), QJsonArray {}}, {QStringLiteral("invitations"), QJsonArray {}},
            {QStringLiteral("roomsTruncated"), true}}, 0);
        const auto current = f.commands.values.last();
        const auto failure = [](const QJsonObject& command) {
            return QJsonObject {{QStringLiteral("type"), QStringLiteral("room.error")},
                {QStringLiteral("operation"), QStringLiteral("room.open")},
                {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
                {QStringLiteral("roomId"), command.value(QStringLiteral("roomId"))},
                {QStringLiteral("message"), QStringLiteral("Mission unavailable")}};
        };
        f.workspace.apply(failure(stale), 0);
        QCOMPARE(f.workspace.selectedMissionId(), test::id(10));
        f.workspace.apply(failure(current), 0);
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        QVERIFY(f.workspace.missionCatalogTruncated());
        QVERIFY(!f.workspace.error().isEmpty());
    }
    void friendDeviceAndShareCommandsStaySeparate()
    {
        Fixture f;
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } }, 1);
        f.workspace.apply(test::snapshot({ test::session(1), test::session(2, true, true) }), 0);
        QVERIFY(f.workspace.shareSession(test::id(1), { test::id(51) }));
        auto command = f.commands.values.last();
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("session.share"));
        QVERIFY(!command.contains(QStringLiteral("access")));
        QCOMPARE(command.value(QStringLiteral("userIds")).toArray().size(), 1);
        QVERIFY(!f.workspace.shareSession(test::id(2), { test::id(51) }));
        QVERIFY(f.workspace.attachMission(test::id(2), test::id(10)));
        f.workspace.approveDevice(QStringLiteral("ABCD-EFGH"));
        command = f.commands.values.last();
        QVERIFY(!command.contains(QStringLiteral("requestId")));
        QCOMPARE(command.value(QStringLiteral("userCode")).toString(), QStringLiteral("ABCD-EFGH"));
        f.workspace.requestFriend(QStringLiteral("friend"));
        QVERIFY(f.commands.values.last().contains(QStringLiteral("requestId")));
    }
    void exactAccountEpochChangeClearsStateAboveSignedRange()
    {
        Fixture f;
        const auto user = test::id(50);
        const QJsonObject ready {{QStringLiteral("type"), QStringLiteral("auth.ready")},
            {QStringLiteral("userId"), user}};
        f.workspace.apply(ready, std::numeric_limits<std::uint64_t>::max() - 1);
        f.workspace.apply(test::snapshot({test::session(1)}), std::numeric_limits<std::uint64_t>::max() - 1);
        QVERIFY(f.workspace.activateSession(test::id(1)));
        f.workspace.apply(ready, std::numeric_limits<std::uint64_t>::max());
        QVERIFY(f.desktop.stagedSessionIds().isEmpty());
    }
    void strictRefreshHasNoRequestId()
    {
        Fixture f;
        f.workspace.refresh();
        QVERIFY(!f.commands.values.first().contains(QStringLiteral("requestId")));
        f.commands.reject = true;
        QVERIFY(!f.workspace.createSession(QStringLiteral("Work"), QStringLiteral("/repo")));
        QVERIFY(!f.workspace.busy());
        QVERIFY(!f.workspace.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(WorkspaceTest)
#include "tst_workspace.moc"
