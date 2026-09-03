#include "models/SessionActions.hpp"
#include "models/DesktopSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <memory>

class FakeSessionDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    QString rejectType;
    int rejectionsRemaining = 0;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        const auto type = document.isObject()
            ? document.object().value(QStringLiteral("type")).toString()
            : QString {};
        if (lane != kodosi::CommandLane::Sessions || !document.isObject()
            || (type == rejectType && rejectionsRemaining-- > 0)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.push_back(document.object());
        return {};
    }
};

class SessionActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesCurrentIncarnationOnly();
    void rejectsUnavailableLifecycleActions();
    void correlatesAsynchronousFailures();
    void createsLocalSessionWithCorrelatedReceipt();
    void usesReadablePersistedWorkingDirectoryAsCreationDefault();
    void renamesReopensAndDeletesLocalSessions();
    void authoritativeSnapshotRecoversDroppedLifecycleEvents();
    void opensHidesAndRestoresRemoteSessions();
    void reincarnatedRemoteOpenClearsStalePendingIdentity();
};

namespace {

void auth(kodosi::SessionCatalogModel& sessions)
{
    sessions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
}

QByteArray localSession(
    kodosi::SessionCatalogModel& sessions,
    const QString& incarnation,
    const QString& status = QStringLiteral("active"),
    const QString& recovery = QStringLiteral("live"),
    const QString& name = QStringLiteral("Agent"))
{
    const QJsonObject session {
        {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("id"), QStringLiteral("session-1")},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), name},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), status},
        {QStringLiteral("recovery"), recovery},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
    const auto event = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"), session},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(event);
    return event;
}

QJsonObject lastCommand(
    const FakeSessionDispatcher& dispatcher,
    const QString& type)
{
    for (auto command = dispatcher.commands.crbegin();
         command != dispatcher.commands.crend();
         ++command) {
        if (command->value(QStringLiteral("type")).toString() == type) {
            return *command;
        }
    }
    return {};
}

} // namespace

void SessionActionsTest::dispatchesCurrentIncarnationOnly()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);

    QVERIFY(actions.interrupt(QStringLiteral("session-1")));
    QCOMPARE(
        dispatcher.commands.back()
            .value(QStringLiteral("expectedRuntimeIncarnationId"))
            .toString(),
        QStringLiteral("inc-1"));

    localSession(sessions, QStringLiteral("inc-2"));
    QVERIFY(actions.setMode(QStringLiteral("session-1"), QStringLiteral("plan")));
    QCOMPARE(
        dispatcher.commands.back()
            .value(QStringLiteral("expectedRuntimeIncarnationId"))
            .toString(),
        QStringLiteral("inc-2"));
    QVERIFY(actions.requestCloseConfirmation(QStringLiteral("session-1")));
    QVERIFY(actions.confirmClose(QStringLiteral("session-1")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.close"));
}

void SessionActionsTest::usesReadablePersistedWorkingDirectoryAsCreationDefault()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("session-default-directory-XXXXXX")));
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkdir(QStringLiteral("project")));
    const auto project = root.filePath(QStringLiteral("project"));
    auto storage = std::make_unique<QSettings>(
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat);
    kodosi::DesktopSettings settings(std::move(storage));
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions actions(dispatcher, sessions, settings);
    QSignalSpy changed(
        &actions,
        &kodosi::SessionActions::defaultWorkingDirectoryChanged);

    QVERIFY(settings.apply(
        QStringLiteral("JetBrains Mono"),
        14,
        static_cast<int>(kodosi::DesktopSettings::CursorStyle::Block),
        1.1,
        10'000,
        false,
        true,
        project));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(
        actions.defaultWorkingDirectory(),
        QFileInfo(project).canonicalFilePath());

    QVERIFY(actions.createDefault());
    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.create"));
    QCOMPARE(
        command.value(QStringLiteral("workingDir")).toString(),
        QFileInfo(project).canonicalFilePath());
    const auto generatedName =
        command.value(QStringLiteral("name")).toString();
    QVERIFY(!generatedName.isEmpty());
    QVERIFY(generatedName.size() <= 128);
    QCOMPARE(generatedName.count(QLatin1Char(' ')), 1);

    QVERIFY(root.rmdir(QStringLiteral("project")));
    QCOMPARE(actions.defaultWorkingDirectory(), QDir::homePath());
}

void SessionActionsTest::rejectsUnavailableLifecycleActions()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(
        sessions,
        QStringLiteral("inc-1"),
        QStringLiteral("stopped"));
    kodosi::SessionActions actions(dispatcher, sessions);

    QVERIFY(!actions.canInterrupt(QStringLiteral("session-1")));
    QVERIFY(!actions.interrupt(QStringLiteral("session-1")));
    QVERIFY(!actions.requestCloseConfirmation(QStringLiteral("missing")));
    QVERIFY(!actions.setMode(QStringLiteral("session-1"), QStringLiteral("future")));
    QCOMPARE(dispatcher.commands.size(), 0);

    localSession(
        sessions,
        QStringLiteral("inc-2"),
        QStringLiteral("reconnecting"));
    QVERIFY(!actions.canInterrupt(QStringLiteral("session-1")));
    QVERIFY(actions.canClose(QStringLiteral("session-1")));
}

void SessionActionsTest::correlatesAsynchronousFailures()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QSignalSpy availability(&actions, &kodosi::SessionActions::availabilityChanged);

    QVERIFY(actions.requestCloseConfirmation(QStringLiteral("session-1")));
    QVERIFY(actions.confirmClose(QStringLiteral("session-1")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.close")},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"), QStringLiteral("process refused to stop")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.lastError(), QStringLiteral("process refused to stop"));

    localSession(sessions, QStringLiteral("inc-2"));
    QVERIFY(availability.count() > 0);

    QVERIFY(actions.requestCloseConfirmation(QStringLiteral("session-1")));
    localSession(sessions, QStringLiteral("inc-3"));
    QVERIFY(actions.closeConfirmationSessionId().isEmpty());
    const auto commandCount = dispatcher.commands.size();
    QVERIFY(!actions.confirmClose(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), commandCount);
}

void SessionActionsTest::createsLocalSessionWithCorrelatedReceipt()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy created(&actions, &kodosi::SessionActions::sessionCreated);

    QVERIFY(!actions.create(QString {}, directory.path()));
    QVERIFY(!actions.create(QStringLiteral("Agent"), QStringLiteral("/missing")));
    QVERIFY(actions.create(QStringLiteral(" Agent "), directory.path()));
    QVERIFY(actions.creating());
    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.create"));
    QCOMPARE(
        command.value(QStringLiteral("name")).toString(),
        QStringLiteral("Agent"));
    const auto requestId =
        command.value(QStringLiteral("requestId")).toString();
    const QUuid parsed(requestId);
    QCOMPARE(parsed.version(), QUuid::Version::UnixEpoch);

    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    QVERIFY(actions.requestCloseConfirmation(QStringLiteral("session-1")));
    actions.cancelCloseConfirmation();
    QVERIFY(actions.creating());

    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"other\",\"accountEpoch\":2}"));
    QVERIFY(actions.creating());
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("other")},
        {QStringLiteral("accountEpoch"), 2},
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.create")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"), QStringLiteral("creation failed")},
    }).toJson(QJsonDocument::Compact));
    QVERIFY(!actions.creating());
    QCOMPARE(actions.lastError(), QStringLiteral("creation failed"));

    QVERIFY(actions.create(QStringLiteral("Agent 2"), directory.path()));
    const auto secondRequestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("other")},
        {QStringLiteral("accountEpoch"), 2},
        {QStringLiteral("type"), QStringLiteral("session.created")},
        {QStringLiteral("requestId"), secondRequestId},
        {QStringLiteral("sessionId"), QStringLiteral("new-session")},
        {QStringLiteral("runtimeIncarnationId"), QStringLiteral("new-incarnation")},
    }).toJson(QJsonDocument::Compact));
    QVERIFY(!actions.creating());
    QCOMPARE(created.count(), 1);
}

void SessionActionsTest::renamesReopensAndDeletesLocalSessions()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QSignalSpy reopened(&actions, &kodosi::SessionActions::sessionReopened);

    QVERIFY(actions.canRename(QStringLiteral("session-1")));
    QVERIFY(!actions.rename(
        QStringLiteral("session-1"),
        QString(65, QChar(0x00e9))));
    QVERIFY(actions.rename(
        QStringLiteral("session-1"),
        QStringLiteral(" Renamed ")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.rename"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("name")).toString(),
        QStringLiteral("Renamed"));
    QVERIFY(!actions.canRename(QStringLiteral("session-1")));
    const auto renamed = localSession(
        sessions,
        QStringLiteral("inc-1"),
        QStringLiteral("active"),
        QStringLiteral("live"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(renamed);
    QVERIFY(actions.canRename(QStringLiteral("session-1")));

    const auto stopped = localSession(
        sessions,
        QStringLiteral("inc-1"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(stopped);
    QVERIFY(actions.canReopen(QStringLiteral("session-1")));
    QVERIFY(actions.canDelete(QStringLiteral("session-1")));
    QVERIFY(actions.reopen(QStringLiteral("session-1")));
    auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.reopen"));
    QCOMPARE(
        command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-1"));
    QCOMPARE(
        QUuid(command.value(QStringLiteral("requestId")).toString()).version(),
        QUuid::Version::UnixEpoch);
    const auto reopenedEvent = localSession(
        sessions,
        QStringLiteral("inc-2"),
        QStringLiteral("active"),
        QStringLiteral("live"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(reopenedEvent);
    QCOMPARE(reopened.count(), 1);

    const auto failedRecoverable = localSession(
        sessions,
        QStringLiteral("inc-2"),
        QStringLiteral("blocked"),
        QStringLiteral("recoverable"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(failedRecoverable);
    QVERIFY(actions.canReopen(QStringLiteral("session-1")));

    const auto stoppedAgain = localSession(
        sessions,
        QStringLiteral("inc-2"),
        QStringLiteral("blocked"),
        QStringLiteral("crashed"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(stoppedAgain);
    QVERIFY(actions.canReopen(QStringLiteral("session-1")));
    QVERIFY(actions.reopen(QStringLiteral("session-1")));
    const auto immediateStop = localSession(
        sessions,
        QStringLiteral("inc-3"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(immediateStop);
    QVERIFY(actions.lastError().contains(
        QStringLiteral("stopped before becoming ready")));
    QVERIFY(actions.canReopen(QStringLiteral("session-1")));
    QVERIFY(actions.requestDeleteConfirmation(QStringLiteral("session-1")));
    const auto replacement = localSession(
        sessions,
        QStringLiteral("inc-4"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(replacement);
    QVERIFY(actions.deleteConfirmationSessionId().isEmpty());
    QVERIFY(!actions.confirmDelete(QStringLiteral("session-1")));

    QVERIFY(actions.requestDeleteConfirmation(QStringLiteral("session-1")));
    QVERIFY(actions.confirmDelete(QStringLiteral("session-1")));
    QCOMPARE(actions.inactiveCleanupCount(), 1);
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.delete"));
    QCOMPARE(
        command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-4"));

    const auto removed = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.removed")},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(removed);
    actions.ingestSessionEvent(removed);
    QCOMPARE(actions.inactiveCleanupCount(), 0);
    QVERIFY(!sessions.containsSession(QStringLiteral("session-1")));
}

void SessionActionsTest::authoritativeSnapshotRecoversDroppedLifecycleEvents()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QSignalSpy reopened(&actions, &kodosi::SessionActions::sessionReopened);

    const auto snapshot = [&](const QString& incarnation,
                              const QString& status,
                              const QString& recovery,
                              const QString& name) {
        return QJsonDocument(QJsonObject {
            {QStringLiteral("authority"), QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), QStringLiteral("me")},
            {QStringLiteral("accountEpoch"), 1},
            {QStringLiteral("type"), QStringLiteral("session.list")},
            {QStringLiteral("sessions"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("kind"), QStringLiteral("local")},
                     {QStringLiteral("id"), QStringLiteral("session-1")},
                     {QStringLiteral("incarnationId"), incarnation},
                     {QStringLiteral("name"), name},
                     {QStringLiteral("project"), QStringLiteral("/repo")},
                     {QStringLiteral("mode"), QStringLiteral("normal")},
                     {QStringLiteral("status"), status},
                     {QStringLiteral("recovery"), recovery},
                     {QStringLiteral("scope"), QStringLiteral("justMe")},
                     {QStringLiteral("access"), QStringLiteral("approve")},
                 },
             }},
        }).toJson(QJsonDocument::Compact);
    };

    QVERIFY(actions.rename(
        QStringLiteral("session-1"),
        QStringLiteral("Snapshot Name")));
    const auto renamed = snapshot(
        QStringLiteral("inc-1"),
        QStringLiteral("active"),
        QStringLiteral("live"),
        QStringLiteral("Snapshot Name"));
    sessions.ingestSessionEvent(renamed);
    actions.ingestSessionEvent(renamed);
    QVERIFY(actions.canRename(QStringLiteral("session-1")));

    const auto stopped = localSession(
        sessions,
        QStringLiteral("inc-1"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Snapshot Name"));
    actions.ingestSessionEvent(stopped);
    QVERIFY(actions.reopen(QStringLiteral("session-1")));
    const auto active = snapshot(
        QStringLiteral("inc-2"),
        QStringLiteral("active"),
        QStringLiteral("live"),
        QStringLiteral("Snapshot Name"));
    sessions.ingestSessionEvent(active);
    actions.ingestSessionEvent(active);
    QCOMPARE(reopened.count(), 1);

    const auto stoppedAgain = localSession(
        sessions,
        QStringLiteral("inc-2"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Snapshot Name"));
    actions.ingestSessionEvent(stoppedAgain);
    QVERIFY(actions.requestDeleteConfirmation(QStringLiteral("session-1")));
    QVERIFY(actions.confirmDelete(QStringLiteral("session-1")));
    const auto empty = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), QJsonArray {}},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(empty);
    actions.ingestSessionEvent(empty);
    QVERIFY(!sessions.containsSession(QStringLiteral("session-1")));
}

void SessionActionsTest::opensHidesAndRestoresRemoteSessions()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    const auto remote = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("remote")},
             {QStringLiteral("id"), QStringLiteral("remote")},
             {QStringLiteral("incarnationId"), QStringLiteral("remote-inc")},
             {QStringLiteral("name"), QStringLiteral("Shared Agent")},
             {QStringLiteral("project"), QStringLiteral("/shared")},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("scope"), QStringLiteral("friends")},
             {QStringLiteral("access"), QStringLiteral("view")},
             {QStringLiteral("owner"), QStringLiteral("Alice")},
             {QStringLiteral("ownerUserId"), QStringLiteral("alice")},
             {QStringLiteral("permissions"), 1},
             {QStringLiteral("connectionState"), QStringLiteral("offline")},
             {QStringLiteral("accessState"), QStringLiteral("ready")},
         }},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(remote);
    kodosi::SessionActions actions(dispatcher, sessions);
    QSignalSpy openSucceeded(
        &actions,
        &kodosi::SessionActions::remoteOpenSucceeded);
    QSignalSpy openFailed(
        &actions,
        &kodosi::SessionActions::remoteOpenFailed);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    const auto initialHiddenRequest = lastCommand(
        dispatcher,
        QStringLiteral("session.listHidden"))
                                          .value(QStringLiteral("requestId"))
                                          .toString();
    QVERIFY(!initialHiddenRequest.isEmpty());
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.hiddenList")},
        {QStringLiteral("requestId"), initialHiddenRequest},
        {QStringLiteral("entries"), QJsonArray {}},
    }).toJson(QJsonDocument::Compact));
    QVERIFY(!actions.loadingHidden());

    QVERIFY(actions.canOpenRemote(QStringLiteral("remote")));
    QVERIFY(actions.openRemote(QStringLiteral("remote")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.openRemote"));
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.opened")},
        {QStringLiteral("sessionId"), QStringLiteral("remote")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(openSucceeded.count(), 1);
    QCOMPARE(
        openSucceeded.constFirst().at(0).toString(),
        QStringLiteral("remote"));
    QCOMPARE(
        openSucceeded.constFirst().at(1).toString(),
        QStringLiteral("remote-inc"));

    auto connectedDocument = QJsonDocument::fromJson(remote);
    auto connectedEvent = connectedDocument.object();
    auto connectedSession =
        connectedEvent.value(QStringLiteral("session")).toObject();
    connectedSession.insert(
        QStringLiteral("connectionState"),
        QStringLiteral("connected"));
    connectedEvent.insert(
        QStringLiteral("session"),
        connectedSession);
    sessions.ingestSessionEvent(
        QJsonDocument(connectedEvent).toJson(QJsonDocument::Compact));
    QVERIFY(!actions.canOpenRemote(QStringLiteral("remote")));
    QVERIFY(actions.restoreRemote(QStringLiteral("remote")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.openRemote"));
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.opened")},
        {QStringLiteral("sessionId"), QStringLiteral("remote")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(openSucceeded.count(), 2);

    dispatcher.rejectType = QStringLiteral("session.openRemote");
    dispatcher.rejectionsRemaining = 1;
    QVERIFY(!actions.restoreRemote(QStringLiteral("remote")));
    QCOMPARE(openFailed.count(), 1);
    QCOMPARE(
        openFailed.constFirst().at(1).toString(),
        QStringLiteral("remote-inc"));
    dispatcher.rejectType.clear();

    QVERIFY(actions.restoreRemote(QStringLiteral("remote")));
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.openRemote")},
        {QStringLiteral("sessionId"), QStringLiteral("remote")},
        {QStringLiteral("message"), QStringLiteral("open denied")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(openFailed.count(), 2);
    QCOMPARE(
        openFailed.constLast().at(0).toString(),
        QStringLiteral("remote"));
    QCOMPARE(
        openFailed.constLast().at(1).toString(),
        QStringLiteral("remote-inc"));

    QVERIFY(actions.canHide(QStringLiteral("remote")));
    dispatcher.rejectType = QStringLiteral("session.listHidden");
    dispatcher.rejectionsRemaining = 1;
    QVERIFY(actions.hide(QStringLiteral("remote")));
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.listHidden"),
        1'000);
    dispatcher.rejectType.clear();
    QCOMPARE(
        dispatcher.commands.at(dispatcher.commands.size() - 2)
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("session.hide"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.listHidden"));
    const auto hiddenRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.hiddenList")},
        {QStringLiteral("requestId"), hiddenRequest},
        {QStringLiteral("entries"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("remote")},
                 {QStringLiteral("name"), QStringLiteral("Shared Agent")},
                 {QStringLiteral("project"), QStringLiteral("/shared")},
                 {QStringLiteral("owner"), QStringLiteral("Alice")},
             },
         }},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.hiddenSessions()->rowCount(), 1);

    QVERIFY(actions.refreshHidden());
    const auto preMutationRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(actions.unhide(QStringLiteral("remote")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.unhide"));
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.hiddenList")},
        {QStringLiteral("requestId"), preMutationRequest},
        {QStringLiteral("entries"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("remote")},
                 {QStringLiteral("name"), QStringLiteral("Shared Agent")},
                 {QStringLiteral("project"), QStringLiteral("/shared")},
                 {QStringLiteral("owner"), QStringLiteral("Alice")},
             },
         }},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.listHidden"));
    const auto unhiddenRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(unhiddenRequest != preMutationRequest);
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.hiddenList")},
        {QStringLiteral("requestId"), unhiddenRequest},
        {QStringLiteral("entries"), QJsonArray {}},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.hiddenSessions()->rowCount(), 0);

    QVERIFY(actions.hide(QStringLiteral("remote")));
    const auto failedHideReadback =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.hide")},
        {QStringLiteral("sessionId"), QStringLiteral("remote")},
        {QStringLiteral("message"), QStringLiteral("hide denied")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.lastError(), QStringLiteral("hide denied"));
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.hiddenList")},
        {QStringLiteral("requestId"), failedHideReadback},
        {QStringLiteral("entries"), QJsonArray {}},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.lastError(), QStringLiteral("hide denied"));
}

void SessionActionsTest::reincarnatedRemoteOpenClearsStalePendingIdentity()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"me","accountEpoch":1})"));
    QSignalSpy openSucceeded(
        &actions,
        &kodosi::SessionActions::remoteOpenSucceeded);
    QSignalSpy openFailed(
        &actions,
        &kodosi::SessionActions::remoteOpenFailed);

    const auto remoteUpsert = [](const QString& incarnation,
                                  const QString& connectionState) {
        return QJsonDocument(QJsonObject {
            {QStringLiteral("authority"), QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), QStringLiteral("me")},
            {QStringLiteral("accountEpoch"), 1},
            {QStringLiteral("type"), QStringLiteral("session.upsert")},
            {QStringLiteral("session"),
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("remote")},
                 {QStringLiteral("id"), QStringLiteral("remote")},
                 {QStringLiteral("incarnationId"), incarnation},
                 {QStringLiteral("name"), QStringLiteral("Shared Agent")},
                 {QStringLiteral("project"), QStringLiteral("/shared")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("scope"), QStringLiteral("friends")},
                 {QStringLiteral("access"), QStringLiteral("view")},
                 {QStringLiteral("owner"), QStringLiteral("Alice")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("alice")},
                 {QStringLiteral("permissions"), 1},
                 {QStringLiteral("connectionState"), connectionState},
                 {QStringLiteral("accessState"), QStringLiteral("ready")},
             }},
        }).toJson(QJsonDocument::Compact);
    };

    const auto incarnationA = QStringLiteral("remote-inc-a");
    const auto incarnationB = QStringLiteral("remote-inc-b");
    const auto remoteA =
        remoteUpsert(incarnationA, QStringLiteral("offline"));
    sessions.ingestSessionEvent(remoteA);
    actions.ingestSessionEvent(remoteA);
    QVERIFY(actions.restoreRemote(QStringLiteral("remote")));

    const auto connectingA =
        remoteUpsert(incarnationA, QStringLiteral("connecting"));
    sessions.ingestSessionEvent(connectingA);
    actions.ingestSessionEvent(connectingA);
    QCOMPARE(openFailed.count(), 0);
    QVERIFY(!actions.canOpenRemote(QStringLiteral("remote")));

    const auto remoteB =
        remoteUpsert(incarnationB, QStringLiteral("offline"));
    sessions.ingestSessionEvent(remoteB);
    actions.ingestSessionEvent(remoteB);

    QCOMPARE(openFailed.count(), 1);
    QCOMPARE(
        openFailed.constFirst().at(0).toString(),
        QStringLiteral("remote"));
    QCOMPARE(openFailed.constFirst().at(1).toString(), incarnationA);
    QCOMPARE(
        sessions.incarnationForSession(QStringLiteral("remote")),
        std::optional<QString> {incarnationB});
    QVERIFY(actions.canOpenRemote(QStringLiteral("remote")));

    const auto previousOpenCount = std::ranges::count_if(
        dispatcher.commands,
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString()
                == QStringLiteral("session.openRemote");
        });
    QVERIFY(actions.openRemote(QStringLiteral("remote")));
    QCOMPARE(
        std::ranges::count_if(
            dispatcher.commands,
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString()
                    == QStringLiteral("session.openRemote");
            }),
        previousOpenCount + 1);
    QCOMPARE(
        dispatcher.commands.constLast()
            .value(QStringLiteral("sessionId"))
            .toString(),
        QStringLiteral("remote"));

    const auto connectedB =
        remoteUpsert(incarnationB, QStringLiteral("connected"));
    sessions.ingestSessionEvent(connectedB);
    actions.ingestSessionEvent(connectedB);
    QCOMPARE(openSucceeded.count(), 1);
    QCOMPARE(
        openSucceeded.constFirst().at(0).toString(),
        QStringLiteral("remote"));
    QCOMPARE(openSucceeded.constFirst().at(1).toString(), incarnationB);
}

QTEST_GUILESS_MAIN(SessionActionsTest)

#include "tst_session_actions.moc"
