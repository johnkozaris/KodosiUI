#include "models/SessionActions.hpp"
#include "models/DesktopSettings.hpp"
#include "models/ProviderConversationResumeResolver.hpp"

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
    QVector<QByteArray> rawCommands;
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
        rawCommands.push_back(json.toByteArray());
        commands.push_back(document.object());
        return {};
    }
};

class FakeResumeResolver final
    : public kodosi::ProviderConversationResumeResolver {
public:
    std::optional<kodosi::ProviderConversationResumeTarget>
    resolveResumeTarget(const QString& presentationId) const override
    {
        return presentationId == acceptedPresentationId ? target
                                                        : std::nullopt;
    }

    QString acceptedPresentationId =
        QStringLiteral("conversation-opaque");
    std::optional<kodosi::ProviderConversationResumeTarget> target;
};

class SessionActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void dispatchesCurrentIncarnationOnly();
    void closeIsIdempotentAndRecoversAfterTimeout();
    void rejectsUnavailableLifecycleActions();
    void correlatesAsynchronousFailures();
    void createsLocalSessionWithCorrelatedReceipt();
    void tracksConcurrentCreationsIndependently();
    void authoritativeProjectionRecoversLostCreationReceipt();
    void createsResumedSessionFromOpaqueCurrentIdentity();
    void usesReadablePersistedWorkingDirectoryAsCreationDefault();
    void renamesAndCleansInactiveLocalSessions();
    void authoritativeSnapshotSettlesInactiveCleanup();
    void inactiveCleanupIsBoundedAndTimeoutReleasesRetry();
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

void SessionActionsTest::closeIsIdempotentAndRecoversAfterTimeout()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions, 1);

    QVERIFY(actions.requestCloseConfirmation(QStringLiteral("session-1")));
    QVERIFY(actions.confirmClose(QStringLiteral("session-1")));
    const auto commandCount = dispatcher.commands.size();
    QVERIFY(!actions.canClose(QStringLiteral("session-1")));
    QVERIFY(!actions.requestCloseConfirmation(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), commandCount);

    QTRY_VERIFY_WITH_TIMEOUT(
        actions.canClose(QStringLiteral("session-1")),
        250);
    QVERIFY(actions.lastError().contains(QStringLiteral("Try Close Session")));
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
    QVERIFY(actions.creating());
    QCOMPARE(created.count(), 0);

    sessions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"other\",\"accountEpoch\":2}"));
    const auto projected = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("other")},
        {QStringLiteral("accountEpoch"), 2},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("local")},
             {QStringLiteral("id"), QStringLiteral("new-session")},
             {QStringLiteral("incarnationId"),
              QStringLiteral("new-incarnation")},
             {QStringLiteral("name"), QStringLiteral("Agent 2")},
             {QStringLiteral("project"), directory.path()},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("recovery"), QStringLiteral("live")},
             {QStringLiteral("scope"), QStringLiteral("justMe")},
             {QStringLiteral("access"), QStringLiteral("approve")},
         }},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(projected);
    actions.ingestSessionEvent(projected);
    QVERIFY(!actions.creating());
    QCOMPARE(created.count(), 1);
}

void SessionActionsTest::tracksConcurrentCreationsIndependently()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy resolved(
        &actions,
        &kodosi::SessionActions::sessionCreationResolved);

    QVERIFY(actions.create(QStringLiteral("First"), directory.path()));
    const auto firstRequestId = actions.lastCreateRequestId();
    QVERIFY(actions.create(QStringLiteral("Second"), directory.path()));
    const auto secondRequestId = actions.lastCreateRequestId();
    QVERIFY(firstRequestId != secondRequestId);
    QCOMPARE(actions.pendingCreations().size(), 2);

    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.create")},
        {QStringLiteral("requestId"), firstRequestId},
        {QStringLiteral("message"), QStringLiteral("first failed")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(actions.pendingCreations().size(), 1);
    QVERIFY(actions.isCreatePending(secondRequestId));

    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.created")},
        {QStringLiteral("requestId"), secondRequestId},
        {QStringLiteral("sessionId"), QStringLiteral("second-session")},
        {QStringLiteral("runtimeIncarnationId"), QStringLiteral("second-inc")},
    }).toJson(QJsonDocument::Compact));
    QVERIFY(actions.isCreatePending(secondRequestId));
    auth(sessions);
    const auto projected = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("local")},
             {QStringLiteral("id"), QStringLiteral("second-session")},
             {QStringLiteral("incarnationId"), QStringLiteral("second-inc")},
             {QStringLiteral("name"), QStringLiteral("Second")},
             {QStringLiteral("project"), directory.path()},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("recovery"), QStringLiteral("live")},
             {QStringLiteral("scope"), QStringLiteral("justMe")},
             {QStringLiteral("access"), QStringLiteral("approve")},
         }},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(projected);
    actions.ingestSessionEvent(projected);

    QVERIFY(!actions.creating());
    QVERIFY(actions.pendingCreations().isEmpty());
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(
        resolved.constFirst().at(0).toString(),
        secondRequestId);
    QCOMPARE(
        resolved.constFirst().at(1).toString(),
        QStringLiteral("second-session"));
}

void SessionActionsTest::authoritativeProjectionRecoversLostCreationReceipt()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy resolved(
        &actions,
        &kodosi::SessionActions::sessionCreationResolved);

    QVERIFY(actions.create(QStringLiteral("Projected"), directory.path()));
    const auto requestId = actions.lastCreateRequestId();
    auth(sessions);
    const auto projected = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("local")},
             {QStringLiteral("id"), QStringLiteral("projected-session")},
             {QStringLiteral("incarnationId"), QStringLiteral("projected-inc")},
             {QStringLiteral("createRequestId"), requestId},
             {QStringLiteral("name"), QStringLiteral("Projected")},
             {QStringLiteral("project"), directory.path()},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("recovery"), QStringLiteral("live")},
             {QStringLiteral("scope"), QStringLiteral("justMe")},
             {QStringLiteral("access"), QStringLiteral("approve")},
         }},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(projected);
    actions.ingestSessionEvent(projected);

    QVERIFY(!actions.creating());
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.constFirst().at(0).toString(), requestId);
    QCOMPARE(
        resolved.constFirst().at(1).toString(),
        QStringLiteral("projected-session"));
}

void SessionActionsTest::createsResumedSessionFromOpaqueCurrentIdentity()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions actions(dispatcher, sessions);
    FakeResumeResolver resolver;
    QTemporaryDir directory(
        QDir::current().filePath(
            QStringLiteral("resumed-session-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto canonicalDirectory =
        QFileInfo(directory.path()).canonicalFilePath();
    resolver.target = kodosi::ProviderConversationResumeTarget {
        .provider = QStringLiteral("copilot"),
        .nativeConversationId =
            QStringLiteral("01900000-0000-4000-8000-000000000123"),
        .workingDirectory = canonicalDirectory,
        .title = QStringLiteral("Review flaky terminal tests"),
        .accountUserId = QStringLiteral("me"),
        .accountEpoch = 1,
    };
    actions.setProviderConversationResumeResolver(&resolver);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));

    const auto preResumeCommands = dispatcher.commands.size();
    QVERIFY(!actions.createResumed(
        QStringLiteral("not-current")));
    QCOMPARE(dispatcher.commands.size(), preResumeCommands);
    QVERIFY(actions.createResumed(
        resolver.acceptedPresentationId));
    const auto command = dispatcher.commands.constLast();
    const auto commandKeys = command.keys();
    QCOMPARE(
        QSet<QString>(commandKeys.cbegin(), commandKeys.cend()),
        QSet<QString>({
            QStringLiteral("type"),
            QStringLiteral("requestId"),
            QStringLiteral("name"),
            QStringLiteral("workingDir"),
            QStringLiteral("resume"),
        }));
    QCOMPARE(
        dispatcher.rawCommands.constLast(),
        QJsonDocument(command).toJson(QJsonDocument::Compact));
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.create"));
    QCOMPARE(
        command.value(QStringLiteral("name")).toString(),
        QStringLiteral("Copilot: Review flaky terminal tests"));
    QCOMPARE(
        command.value(QStringLiteral("workingDir")).toString(),
        canonicalDirectory);
    const auto resume = command.value(QStringLiteral("resume")).toObject();
    QCOMPARE(
        resume,
        QJsonObject({
            {QStringLiteral("provider"), QStringLiteral("copilot")},
            {QStringLiteral("nativeConversationId"),
             QStringLiteral(
                 "01900000-0000-4000-8000-000000000123")},
        }));

    const auto requestId =
        command.value(QStringLiteral("requestId")).toString();
    actions.ingestSessionEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.created")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), QStringLiteral("resumed")},
        {QStringLiteral("runtimeIncarnationId"),
         QStringLiteral("01900000-0000-7000-8000-000000000124")},
    }).toJson(QJsonDocument::Compact));
    QVERIFY(actions.creating());
    auth(sessions);
    const auto projected = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("local")},
             {QStringLiteral("id"), QStringLiteral("resumed")},
             {QStringLiteral("incarnationId"),
              QStringLiteral("01900000-0000-7000-8000-000000000124")},
             {QStringLiteral("name"),
              QStringLiteral("Copilot: Review flaky terminal tests")},
             {QStringLiteral("project"), canonicalDirectory},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("recovery"), QStringLiteral("live")},
             {QStringLiteral("scope"), QStringLiteral("justMe")},
             {QStringLiteral("access"), QStringLiteral("approve")},
         }},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(projected);
    actions.ingestSessionEvent(projected);
    QVERIFY(!actions.creating());

    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"other\",\"accountEpoch\":2}"));
    const auto before = dispatcher.commands.size();
    QVERIFY(!actions.createResumed(
        resolver.acceptedPresentationId));
    QCOMPARE(dispatcher.commands.size(), before);
    QVERIFY(actions.lastError().contains(QStringLiteral("no longer current")));
}

void SessionActionsTest::renamesAndCleansInactiveLocalSessions()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
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
    QVERIFY(!sessions.containsSession(QStringLiteral("session-1")));
    QCOMPARE(actions.inactiveCleanupCount(), 1);
    auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.delete"));
    QCOMPARE(
        command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-1"));
    QCOMPARE(
        QUuid(command.value(QStringLiteral("requestId")).toString()).version(),
        QUuid::Version::UnixEpoch);
    const auto repeated = localSession(
        sessions,
        QStringLiteral("inc-1"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Renamed"));
    actions.ingestSessionEvent(repeated);
    QCOMPARE(actions.inactiveCleanupCount(), 1);

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

void SessionActionsTest::authoritativeSnapshotSettlesInactiveCleanup()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    localSession(sessions, QStringLiteral("inc-1"));
    kodosi::SessionActions actions(dispatcher, sessions);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
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

    const auto stopped = snapshot(
        QStringLiteral("inc-1"),
        QStringLiteral("stopped"),
        QStringLiteral("recoverable"),
        QStringLiteral("Snapshot Name"));
    sessions.ingestSessionEvent(stopped);
    actions.ingestSessionEvent(stopped);
    QCOMPARE(actions.inactiveCleanupCount(), 1);
    QVERIFY(!sessions.containsSession(QStringLiteral("session-1")));

    const auto empty = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), QJsonArray {}},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(empty);
    actions.ingestSessionEvent(empty);
    QCOMPARE(actions.inactiveCleanupCount(), 0);
    QVERIFY(!sessions.containsSession(QStringLiteral("session-1")));
}

void SessionActionsTest::inactiveCleanupIsBoundedAndTimeoutReleasesRetry()
{
    FakeSessionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    auth(sessions);
    kodosi::SessionActions actions(dispatcher, sessions, 50);
    actions.ingestAuthEvent(
        QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"me\",\"accountEpoch\":1}"));
    const auto inactiveSession = [](const QString& id) {
        return QJsonObject {
            {QStringLiteral("kind"), QStringLiteral("local")},
            {QStringLiteral("id"), id},
            {QStringLiteral("incarnationId"), id + QStringLiteral("-inc")},
            {QStringLiteral("name"), id},
            {QStringLiteral("project"), QStringLiteral("/repo")},
            {QStringLiteral("mode"), QStringLiteral("normal")},
            {QStringLiteral("status"), QStringLiteral("stopped")},
            {QStringLiteral("recovery"), QStringLiteral("recoverable")},
            {QStringLiteral("scope"), QStringLiteral("justMe")},
            {QStringLiteral("access"), QStringLiteral("approve")},
        };
    };
    const auto snapshot = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             inactiveSession(QStringLiteral("inactive-1")),
             inactiveSession(QStringLiteral("inactive-2")),
             inactiveSession(QStringLiteral("inactive-3")),
         }},
    }).toJson(QJsonDocument::Compact);

    sessions.ingestSessionEvent(snapshot);
    actions.ingestSessionEvent(snapshot);
    const auto deleteCount = [&] {
        return std::ranges::count_if(
            dispatcher.commands,
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString()
                    == QStringLiteral("session.delete");
            });
    };
    QCOMPARE(deleteCount(), 2);
    QCOMPARE(actions.inactiveCleanupCount(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(deleteCount(), 3, 500);
    QTRY_COMPARE_WITH_TIMEOUT(actions.inactiveCleanupCount(), 0, 500);

    const auto retry = QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("me")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         inactiveSession(QStringLiteral("inactive-1"))},
    }).toJson(QJsonDocument::Compact);
    sessions.ingestSessionEvent(retry);
    actions.ingestSessionEvent(retry);
    QCOMPARE(deleteCount(), 4);
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
