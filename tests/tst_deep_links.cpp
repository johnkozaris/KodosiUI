#include "app/DeepLinkController.hpp"
#include "app/DeepLinkRouter.hpp"
#include "bridge/RuntimeBridge.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <chrono>
#include <utility>
#include <vector>

class DeepLinkDispatcher final : public kodosi::CommandDispatcher {
public:
    bool reject = false;

    Result send(
        const kodosi::CommandLane,
        const QByteArrayView) override
    {
        if (reject) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }
};

namespace {

QByteArray auth(const QString& user, const quint64 epoch)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), user},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QByteArray sessions(
    const QString& user,
    const quint64 epoch,
    const QString& incarnation)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), user},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("session-1")},
                 {QStringLiteral("incarnationId"), incarnation},
                 {QStringLiteral("name"), QStringLiteral("Session")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("justMe")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
             },
         }},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject request(
    const QString& incarnation,
    const QString& toolUseId = QStringLiteral("tool-1"))
{
    return {
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("requestGeneration"), 7},
        {QStringLiteral("toolUseId"), toolUseId},
        {QStringLiteral("toolName"), QStringLiteral("Bash")},
        {QStringLiteral("toolInput"), QJsonObject {}},
        {QStringLiteral("createdAtMs"), 1'700'000'000'000.0},
        {QStringLiteral("deadlineAtMs"), 1'800'000'000'000.0},
        {QStringLiteral("risk"), QStringLiteral("safe")},
        {QStringLiteral("decisionPhase"), QStringLiteral("actionable")},
    };
}

QByteArray snapshot(
    const QString& user,
    const quint64 epoch,
    const quint64 generation,
    QJsonArray requests)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), user},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.pendingPermissionsSnapshot")},
        {QStringLiteral("generation"), static_cast<qint64>(generation)},
        {QStringLiteral("requests"), std::move(requests)},
    }).toJson(QJsonDocument::Compact);
}

kodosi::RuntimeBridge::Result rejectedStart()
{
    return std::unexpected(kodosi::RuntimeFailure {
        .code = kodosi::RuntimeFailure::Code::StartRejected,
        .ffiResult = -1,
        .message = QStringLiteral("injected failure"),
    });
}

kodosi::DeepLinkController::RuntimeReadiness readyRuntime(
    const quint64 generation = 1)
{
    return {
        .state = kodosi::ApplicationLifecycleModel::State::Ready,
        .generation = generation,
    };
}

void activate(
    kodosi::DeepLinkController& controller,
    kodosi::SessionCatalogModel& catalog,
    kodosi::PendingPermissionsModel& permissions,
    const QString& user = QStringLiteral("account"),
    const quint64 epoch = 1,
    const QString& incarnation = QStringLiteral("inc-1"))
{
    const auto authEvent = auth(user, epoch);
    controller.ingestAuthEvent(authEvent);
    catalog.ingestAuthEvent(authEvent);
    catalog.ingestSessionEvent(sessions(user, epoch, incarnation));
    permissions.ingestAuthEvent(authEvent);
}

} // namespace

class DeepLinksTest final : public QObject {
    Q_OBJECT

private slots:
    void parserAcceptsPublicRoute();
    void parserRejectsAbuse_data();
    void parserRejectsAbuse();
    void parserRejectsNonAsciiEscapeNibbles();
    void waitsForCatalogAndOpensExactSession();
    void waitsForPermissionAuthorityAndEmitsOpaqueIdentity();
    void reportsAuthoritativeMissingApproval();
    void staleIncarnationApprovalIsAuthoritativelyMissing();
    void opensSessionWhenApprovalAuthorityFails();
    void cancelsAccountAndIncarnationStaleRoutes();
    void lifecycleSignalsRetainRouteAcrossFailureAndRetry();
    void retainsRouteUntilFirstReadyBeyondThirtySeconds();
    void pausesDeadlineAcrossFailureAndRetry();
    void resumedDeadlineKeepsRemainingBudget();
    void invalidStatusSurvivesRuntimeRecovery();
    void boundsPendingRoutes();
    void statusCanBeDismissedAndReplaced();
    void qmlAndDesktopRegistrationContract();
};

void DeepLinksTest::parserAcceptsPublicRoute()
{
    const auto plain = kodosi::DeepLinkRouter::parse(
        u"kodosi://session/session-42");
    QVERIFY(plain.accepted());
    QCOMPARE(plain.destination->sessionId, QStringLiteral("session-42"));
    QVERIFY(!plain.destination->toolUseId);

    const auto approval = kodosi::DeepLinkRouter::parse(
        u"KODOSI://session/session%2D42?toolUseId=tool%2D7");
    QVERIFY(approval.accepted());
    QCOMPARE(
        approval.destination->toolUseId,
        std::optional<QString> {QStringLiteral("tool-7")});
}

void DeepLinksTest::parserRejectsAbuse_data()
{
    QTest::addColumn<QString>("url");
    QTest::newRow("wrong-scheme") << QStringLiteral("https://session/s1");
    QTest::newRow("wrong-host") << QStringLiteral("kodosi://room/s1");
    QTest::newRow("host-case") << QStringLiteral("kodosi://Session/s1");
    QTest::newRow("missing-segment") << QStringLiteral("kodosi://session/");
    QTest::newRow("extra-path") << QStringLiteral("kodosi://session/s1/x");
    QTest::newRow("credentials") << QStringLiteral("kodosi://u@session/s1");
    QTest::newRow("port") << QStringLiteral("kodosi://session:9/s1");
    QTest::newRow("fragment") << QStringLiteral("kodosi://session/s1#x");
    QTest::newRow("empty-query")
        << QStringLiteral("kodosi://session/s1?");
    QTest::newRow("empty-tool")
        << QStringLiteral("kodosi://session/s1?toolUseId=");
    QTest::newRow("extra-query")
        << QStringLiteral("kodosi://session/s1?toolUseId=t&x=1");
    QTest::newRow("duplicate-query")
        << QStringLiteral(
               "kodosi://session/s1?toolUseId=t&toolUseId=u");
    QTest::newRow("unknown-query")
        << QStringLiteral("kodosi://session/s1?other=t");
    QTest::newRow("malformed-percent")
        << QStringLiteral("kodosi://session/%GG");
    QTest::newRow("truncated-percent")
        << QStringLiteral("kodosi://session/%2");
    QTest::newRow("invalid-utf8")
        << QStringLiteral("kodosi://session/%C0%AF");
    QTest::newRow("encoded-slash")
        << QStringLiteral("kodosi://session/a%2Fb");
    QTest::newRow("nul")
        << QStringLiteral("kodosi://session/a%00b");
    QTest::newRow("control")
        << QStringLiteral("kodosi://session/a%1Fb");
    QTest::newRow("bidi")
        << QStringLiteral("kodosi://session/a%E2%80%AEb");
    QTest::newRow("oversized")
        << QStringLiteral("kodosi://session/")
            + QString(
                kodosi::DeepLinkRouter::MaximumUrlCharacters,
                u'a');
}

void DeepLinksTest::parserRejectsAbuse()
{
    QFETCH(QString, url);
    QVERIFY(!kodosi::DeepLinkRouter::parse(url).accepted());
}

void DeepLinksTest::parserRejectsNonAsciiEscapeNibbles()
{
    const QStringList urls {
        QStringLiteral("kodosi://session/%١A"),
        QStringLiteral("kodosi://session/%１A"),
        QStringLiteral("kodosi://session/%ＦF"),
        QStringLiteral("kodosi://session/%A١"),
        QStringLiteral("kodosi://session/%AＦ"),
        QStringLiteral("kodosi://session/s1?toolUseId=%F１"),
    };
    for (const auto& url : urls) {
        const auto result = kodosi::DeepLinkRouter::parse(url);
        QVERIFY2(!result.accepted(), qPrintable(url));
        QCOMPARE(result.error, kodosi::DeepLinkParseError::MalformedEncoding);
    }
}

void DeepLinksTest::waitsForCatalogAndOpensExactSession()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy opened(
        &controller,
        &kodosi::DeepLinkController::openSessionRequested);

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
    });
    QCOMPARE(controller.pendingCount(), 1);
    QCOMPARE(controller.statusCode(), QStringLiteral("waitingAccount"));

    activate(controller, catalog, permissions);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(
        opened.takeFirst().constFirst().toString(),
        QStringLiteral("session-1"));
    QCOMPARE(controller.pendingCount(), 0);
}

void DeepLinksTest::waitsForPermissionAuthorityAndEmitsOpaqueIdentity()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy reviewed(
        &controller,
        &kodosi::DeepLinkController::reviewApprovalRequested);
    activate(controller, catalog, permissions);

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("tool-1"),
    });
    QCOMPARE(controller.statusCode(), QStringLiteral("waitingApprovals"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));

    QCOMPARE(reviewed.count(), 1);
    const auto arguments = reviewed.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("session-1"));
    QVERIFY(!arguments.at(1).toString().isEmpty());
    QVERIFY(!arguments.at(1).toString().contains(QStringLiteral("tool-1")));
}

void DeepLinksTest::reportsAuthoritativeMissingApproval()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy missing(
        &controller,
        &kodosi::DeepLinkController::missingApprovalRequested);
    activate(controller, catalog, permissions);
    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("gone"),
    });
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        {}));

    QCOMPARE(missing.count(), 1);
    QCOMPARE(controller.statusCode(), QStringLiteral("approvalMissing"));
}

void DeepLinksTest::staleIncarnationApprovalIsAuthoritativelyMissing()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy missing(
        &controller,
        &kodosi::DeepLinkController::missingApprovalRequested);
    activate(controller, catalog, permissions);
    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("tool-1"),
    });
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("stale-incarnation"))}));

    QCOMPARE(missing.count(), 1);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.statusCode(), QStringLiteral("approvalMissing"));
}

void DeepLinksTest::opensSessionWhenApprovalAuthorityFails()
{
    DeepLinkDispatcher dispatcher;
    dispatcher.reject = true;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy opened(
        &controller,
        &kodosi::DeepLinkController::openSessionRequested);
    activate(controller, catalog, permissions);
    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("tool-1"),
    });

    QCOMPARE(opened.count(), 1);
    QCOMPARE(
        controller.statusCode(),
        QStringLiteral("approvalsUnavailable"));
}

void DeepLinksTest::cancelsAccountAndIncarnationStaleRoutes()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    QSignalSpy reviewed(
        &controller,
        &kodosi::DeepLinkController::reviewApprovalRequested);
    activate(controller, catalog, permissions);
    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("tool-1"),
    });
    controller.ingestAuthEvent(auth(QStringLiteral("other"), 2));
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.statusCode(), QStringLiteral("accountChanged"));

    activate(
        controller,
        catalog,
        permissions,
        QStringLiteral("other"),
        2,
        QStringLiteral("inc-old"));
    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
        .toolUseId = QStringLiteral("tool-1"),
    });
    catalog.ingestSessionEvent(sessions(
        QStringLiteral("other"),
        2,
        QStringLiteral("inc-new")));
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.statusCode(), QStringLiteral("sessionRestarted"));
    QCOMPARE(reviewed.count(), 0);
}

void DeepLinksTest::lifecycleSignalsRetainRouteAcrossFailureAndRetry()
{
    using namespace std::chrono_literals;

    DeepLinkDispatcher dispatcher;
    kodosi::RuntimeBridge runtime;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    std::vector<kodosi::ApplicationLifecycleModel::Completion>
        completions;
    kodosi::ApplicationLifecycleModel lifecycle(
        runtime,
        [&completions](
            kodosi::ApplicationLifecycleModel::Completion completion) {
            completions.push_back(std::move(completion));
        },
        [] {});
    auto now = kodosi::DeepLinkController::MonotonicTime {};
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        lifecycle,
        [&now] { return now; });
    QSignalSpy opened(
        &controller,
        &kodosi::DeepLinkController::openSessionRequested);

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
    });
    lifecycle.scheduleInitialStart();
    QTRY_COMPARE(completions.size(), std::size_t {1});
    now += 31s;
    completions.front()(rejectedStart());
    QCOMPARE(
        lifecycle.state(),
        kodosi::ApplicationLifecycleModel::State::Failed);
    QCOMPARE(controller.pendingCount(), 1);

    lifecycle.retry();
    QTRY_COMPARE(completions.size(), std::size_t {2});
    activate(controller, catalog, permissions);
    now += 31s;
    completions.back()(kodosi::RuntimeBridge::Result {});

    QCOMPARE(
        lifecycle.state(),
        kodosi::ApplicationLifecycleModel::State::Ready);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(controller.pendingCount(), 0);
}

void DeepLinksTest::retainsRouteUntilFirstReadyBeyondThirtySeconds()
{
    using namespace std::chrono_literals;

    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    auto now = kodosi::DeepLinkController::MonotonicTime {};
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        {
            .state =
                kodosi::ApplicationLifecycleModel::State::Starting,
            .generation = 0,
        },
        [&now] { return now; });
    QSignalSpy opened(
        &controller,
        &kodosi::DeepLinkController::openSessionRequested);

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
    });
    QCOMPARE(controller.statusCode(), QStringLiteral("waitingRuntime"));
    now += 31s;
    activate(controller, catalog, permissions);
    QCOMPARE(controller.pendingCount(), 1);
    QCOMPARE(opened.count(), 0);

    controller.updateRuntimeReadiness(readyRuntime());
    QCOMPARE(opened.count(), 1);
    QCOMPARE(controller.pendingCount(), 0);
}

void DeepLinksTest::pausesDeadlineAcrossFailureAndRetry()
{
    using namespace std::chrono_literals;

    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    auto now = kodosi::DeepLinkController::MonotonicTime {};
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime(),
        [&now] { return now; });
    QSignalSpy opened(
        &controller,
        &kodosi::DeepLinkController::openSessionRequested);

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
    });
    now += 20s;
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Failed,
        .generation = 1,
    });
    controller.resetRuntimeAuthority();
    now += 40s;
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Starting,
        .generation = 1,
    });
    activate(controller, catalog, permissions);
    QCOMPARE(controller.pendingCount(), 1);
    QCOMPARE(opened.count(), 0);

    controller.updateRuntimeReadiness(readyRuntime(2));
    QCOMPARE(opened.count(), 1);
    QCOMPARE(controller.pendingCount(), 0);
}

void DeepLinksTest::resumedDeadlineKeepsRemainingBudget()
{
    using namespace std::chrono_literals;

    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    auto now = kodosi::DeepLinkController::MonotonicTime {};
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime(),
        [&now] { return now; });

    controller.enqueue({
        .sessionId = QStringLiteral("session-1"),
    });
    now += 20s;
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Failed,
        .generation = 1,
    });
    now += 40s;
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Starting,
        .generation = 1,
    });
    controller.updateRuntimeReadiness(readyRuntime(2));
    QCOMPARE(controller.pendingCount(), 1);

    now += 11s;
    controller.ingestAuthEvent(auth(QStringLiteral("account"), 1));
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.statusCode(), QStringLiteral("expired"));
}

void DeepLinksTest::invalidStatusSurvivesRuntimeRecovery()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    auto now = kodosi::DeepLinkController::MonotonicTime {};
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        {
            .state =
                kodosi::ApplicationLifecycleModel::State::Starting,
            .generation = 0,
        },
        [&now] { return now; });

    controller.reject(kodosi::DeepLinkParseError::UnsupportedRoute);
    now += std::chrono::minutes(1);
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Failed,
        .generation = 0,
    });
    controller.updateRuntimeReadiness({
        .state = kodosi::ApplicationLifecycleModel::State::Starting,
        .generation = 0,
    });
    controller.updateRuntimeReadiness(readyRuntime());

    QCOMPARE(controller.statusCode(), QStringLiteral("invalid"));
}

void DeepLinksTest::boundsPendingRoutes()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());
    for (auto index = 0; index < 17; ++index) {
        controller.enqueue({
            .sessionId =
                QStringLiteral("session-%1").arg(index),
        });
    }
    QCOMPARE(controller.pendingCount(), 16);
    QCOMPARE(controller.statusCode(), QStringLiteral("queueFull"));
}

void DeepLinksTest::statusCanBeDismissedAndReplaced()
{
    DeepLinkDispatcher dispatcher;
    kodosi::SessionCatalogModel catalog;
    kodosi::PendingPermissionsModel permissions(dispatcher, catalog);
    kodosi::DeepLinkController controller(
        catalog,
        permissions,
        readyRuntime());

    controller.reject(kodosi::DeepLinkParseError::UnsupportedRoute);
    QCOMPARE(controller.statusCode(), QStringLiteral("invalid"));
    controller.clearStatus();
    QVERIFY(controller.statusCode().isEmpty());

    controller.reject(kodosi::DeepLinkParseError::MalformedEncoding);
    QCOMPARE(
        controller.statusCode(),
        QStringLiteral("malformedEncoding"));
    controller.enqueue({
        .sessionId = QStringLiteral("session-later"),
    });
    QCOMPARE(
        controller.statusCode(),
        QStringLiteral("waitingAccount"));
}

void DeepLinksTest::qmlAndDesktopRegistrationContract()
{
    QFile qml(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml/Main.qml"));
    QVERIFY(qml.open(QIODevice::ReadOnly));
    const auto contents = qml.readAll();
    QVERIFY(contents.contains("target: Models.DeepLinks"));
    QVERIFY(contents.contains("window.openDeepLinkedSession(sessionId)"));
    QVERIFY(contents.contains("window.openDeepLinkedApproval("));
    QVERIFY(contents.contains("window.openMissingDeepLinkedApproval("));
    QVERIFY(contents.contains("activateSessionWithRemote(sessionId, true)"));
    QVERIFY(contents.contains(
        "requestSessionSelection(sessionId, openRemote === true)"));
    QVERIFY(contents.contains("objectName: \"deepLink.status\""));
    QVERIFY(contents.contains(
        "objectName: \"deepLink.status.dismiss\""));
    QVERIFY(contents.contains(
        "onClicked: Models.DeepLinks.clearStatus()"));
    QVERIFY(contents.contains(
        "objectName: \"deepLink.status.label\""));
    QVERIFY(contents.contains("case \"waitingRuntime\":"));
    QVERIFY(contents.contains(
        "Waiting for Kodosi to finish starting before opening the link"));
    QVERIFY(contents.contains(
        "Models.ApplicationLifecycle.state"));
    QVERIFY(contents.contains(
        "=== Models.ApplicationLifecycle.Ready"));
    QVERIFY(contents.contains(
        "enabled: deepLinkStatus.available"));
    QVERIFY(contents.contains(
        "Accessible.ignored: !deepLinkStatus.available"));
    QVERIFY(contents.contains("sessionSidebar.closeConflictingOverlays()"));
    QVERIFY(!contents.contains("XDG_ACTIVATION_TOKEN"));

    QFile composition(
        QStringLiteral(KODOSI_SOURCE_DIR "/src/app/main.cpp"));
    QVERIFY(composition.open(QIODevice::ReadOnly));
    const auto compositionSource = composition.readAll();
    QVERIFY(compositionSource.contains(
        "SingleInstanceGuard::withActivationToken("));
    QVERIFY(compositionSource.contains(
        "pendingPermissions,\n        applicationLifecycle"));
    QVERIFY(compositionSource.contains(
        "qunsetenv(\"XDG_ACTIVATION_TOKEN\")"));

    QFile probe(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/scripts/smoke-deep-link-ui-probe.sh"));
    QVERIFY(probe.open(QIODevice::ReadOnly));
    const auto probeSource = probe.readAll();
    QVERIFY(probeSource.contains(
        "--window-size 820x560"));
    QVERIFY(probeSource.contains(
        "\"kodosi://session/%GG\""));
    QVERIFY(probeSource.contains(
        "--id deepLink.status.dismiss"));

    QFile desktop(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/packaging/linux/com.kodosi.Kodosi.desktop"));
    QVERIFY(desktop.open(QIODevice::ReadOnly));
    const auto registration = desktop.readAll();
    QVERIFY(registration.contains("Exec=kodosi-qt %u\n"));
    QVERIFY(registration.contains(
        "MimeType=x-scheme-handler/kodosi;\n"));
}

QTEST_GUILESS_MAIN(DeepLinksTest)

#include "tst_deep_links.moc"
