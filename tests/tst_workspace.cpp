#include "SessionFixture.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/Workspace.hpp"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QTest>

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
    void remoteActivationAndStop()
    {
        Fixture f;
        auto remote = test::session(1, true, false);
        remote.insert(QStringLiteral("connectionState"), QStringLiteral("offline"));
        f.workspace.apply(test::snapshot({ remote }));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("session.openRemote"));
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
        QVERIFY(f.workspace.stopSession(test::id(1)));
        const auto command = f.commands.values.last();
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("session.stop"));
        QCOMPARE(command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(), test::id(101));
        QVERIFY(f.workspace.closeView(test::id(1)));
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),QStringLiteral("session.disconnect"));
        QVERIFY(f.sessions.containsSession(test::id(1)));
        QVERIFY(!f.workspace.shareSession(test::id(1), {}));
    }
    void closeIsNotStopAndCatalogPrunes()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({ test::session(1) }));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        f.commands.values.clear();
        QVERIFY(f.workspace.closeView(test::id(1)));
        QVERIFY(f.commands.values.isEmpty());
        QVERIFY(f.sessions.containsSession(test::id(1)));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        f.workspace.apply(test::snapshot({}));
        QVERIFY(f.desktop.stagedSessionIds().isEmpty());
    }
    void createsOnlyAfterCorrelatedSnapshot()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({}));
        QVERIFY(f.workspace.createSession(QStringLiteral("Work"), QStringLiteral("/repo")));
        QVERIFY(f.desktop.selectedSessionId().isEmpty());
        auto created = test::session(1);
        created.insert(
            QStringLiteral("createRequestId"), f.commands.values.last().value(QStringLiteral("requestId")));
        f.workspace.apply(test::snapshot({ created }));
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
        QVERIFY(!f.workspace.busy());
    }
    void startupLinkSurvivesEmptyCatalogAndSignIn()
    {
        Fixture f;
        f.workspace.route({ test::id(1) });
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.finalizing") } });
        f.workspace.apply(test::snapshot({}));
        QVERIFY(f.commands.values.isEmpty());
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.required") },
            { QStringLiteral("accountEpoch"), 0 } });
        f.workspace.apply(test::snapshot({}));
        QCOMPARE(f.desktop.activeView(), 3);
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } });
        QCOMPARE(f.commands.values.last().value(QStringLiteral("type")).toString(),
            QStringLiteral("session.openRemote"));
        f.workspace.apply(test::snapshot({ test::session(1, true, false) }));
        QCOMPARE(f.desktop.selectedSessionId(), test::id(1));
    }
    void queuedStartupLinksAreNotOverwritten()
    {
        Fixture f;
        f.workspace.route({ test::id(1) });
        f.workspace.route({ test::id(2) });
        f.workspace.apply(test::snapshot({ test::session(1, true, false), test::session(2, true, false) }));
        QVERIFY(f.desktop.stagedSessionIds().contains(test::id(1)));
        QVERIFY(f.desktop.stagedSessionIds().contains(test::id(2)));
    }
    void accountChangeClearsViewsAndPending()
    {
        Fixture f;
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } });
        f.workspace.apply(test::snapshot({ test::session(1) }));
        QVERIFY(f.workspace.activateSession(test::id(1)));
        QVERIFY(f.workspace.stopSession(test::id(1)));
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.required") },
            { QStringLiteral("accountEpoch"), 2 } });
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
        f.workspace.apply(test::snapshot(entries));
        for (int i = 1; i <= 6; ++i)
            QVERIFY(f.workspace.activateSession(test::id(i)));
        QVERIFY(!f.workspace.activateSession(test::id(7)));
        auto invalid = test::session(9);
        invalid.insert(QStringLiteral("incarnationId"), QStringLiteral("bad"));
        f.workspace.apply(test::snapshot({ invalid }));
        QCOMPARE(f.sessions.authorityState(), kodosi::SessionCatalogModel::Failed);
    }
    void missionRepliesAreRequestScopedAndDeleteKeepsTerminal()
    {
        Fixture f;
        f.workspace.apply(test::snapshot({ test::session(1) }));
        f.workspace.openMission(test::id(10));
        const auto oldRequest = f.commands.values.last().value(QStringLiteral("requestId"));
        f.workspace.openMission(test::id(11));
        const auto request = f.commands.values.last().value(QStringLiteral("requestId"));
        const QJsonObject room { { QStringLiteral("id"), test::id(11) },
            { QStringLiteral("name"), QStringLiteral("Project") },
            { QStringLiteral("slug"), QStringLiteral("project") },
            { QStringLiteral("ownerUserId"), test::id(50) } };
        QJsonObject reply { { QStringLiteral("type"), QStringLiteral("room.snapshot") },
            { QStringLiteral("requestId"), oldRequest }, { QStringLiteral("room"), room },
            { QStringLiteral("members"), QJsonArray {} },
            { QStringLiteral("sessionIds"), QJsonArray { test::id(1) } } };
        f.workspace.apply(reply);
        QVERIFY(f.workspace.mission().isEmpty());
        reply.insert(QStringLiteral("requestId"), request);
        f.workspace.apply(reply);
        QCOMPARE(f.workspace.missionSessionIds(), QStringList { test::id(1) });
        f.workspace.apply({{QStringLiteral("type"),QStringLiteral("rooms.snapshot")},{QStringLiteral("rooms"),QJsonArray{}},{QStringLiteral("invitations"),QJsonArray{}}});
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        f.workspace.openMission(test::id(11));
        f.workspace.deleteMission();
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("room.result") },
            { QStringLiteral("operation"), QStringLiteral("room.delete") },
            { QStringLiteral("requestId"), f.commands.values.last().value(QStringLiteral("requestId")) },
            { QStringLiteral("roomId"), test::id(11) } });
        QVERIFY(f.workspace.selectedMissionId().isEmpty());
        QVERIFY(f.sessions.containsSession(test::id(1)));
    }
    void friendDeviceAndShareCommandsStaySeparate()
    {
        Fixture f;
        f.workspace.apply({ { QStringLiteral("type"), QStringLiteral("auth.ready") },
            { QStringLiteral("userId"), test::id(50) }, { QStringLiteral("accountEpoch"), 1 } });
        f.workspace.apply(test::snapshot({ test::session(1), test::session(2, true, true) }));
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
