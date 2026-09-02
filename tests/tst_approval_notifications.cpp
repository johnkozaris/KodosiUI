#include "attention/ApprovalNotifications.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

class FakePermissionDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::System) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Wrong lane"),
            });
        }
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Invalid JSON"),
            });
        }
        commands.push_back(document.object());
        return {};
    }
};

class FakeNotificationDriver final : public kodosi::DesktopNotificationDriver {
public:
    using DesktopNotificationDriver::DesktopNotificationDriver;

    QVector<kodosi::DesktopNotification> posted;
    QVector<QString> withdrawn;

    void post(const kodosi::DesktopNotification& notification) override
    {
        posted.push_back(notification);
    }

    void withdraw(const QString& key) override
    {
        withdrawn.push_back(key);
    }

    void invoke(
        const QString& key,
        const QString& action,
        const QString& activationToken = {})
    {
        emit actionInvoked(key, action, activationToken);
    }
};

class ApprovalNotificationsTest final : public QObject {
    Q_OBJECT

private slots:
    void postsRiskGatedNotificationWhenInactive();
    void skipsNotificationsWhenActiveOrDisabled();
    void withdrawsReplacedAndResolvedRequests();
    void revalidatesEveryNativeAction();
    void opensOnlyAnExactCurrentReview();
    void protectsSensitiveAndUntrustedBodyText();
};

namespace {

QByteArray auth()
{
    return QByteArrayLiteral(
        "{\"type\":\"auth.ready\",\"userId\":\"account\","
        "\"accountEpoch\":1}");
}

QByteArray sessions()
{
    return QByteArrayLiteral(
        "{\"authority\":\"accountContext\",\"accountUserId\":\"account\","
        "\"accountEpoch\":1,\"type\":\"session.list\",\"sessions\":[{"
        "\"kind\":\"local\",\"id\":\"session-1\",\"incarnationId\":\"inc-1\","
        "\"name\":\"Session\",\"project\":\"/repo\",\"mode\":\"normal\","
        "\"status\":\"active\",\"recovery\":\"live\",\"scope\":\"justMe\","
        "\"access\":\"approve\"}]}");
}

QJsonObject request(
    const quint64 requestGeneration,
    const QString& risk = QStringLiteral("safe"),
    const QString& summary = QStringLiteral("ls -la"))
{
    return {
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), QStringLiteral("inc-1")},
        {QStringLiteral("requestGeneration"),
         static_cast<qint64>(requestGeneration)},
        {QStringLiteral("toolUseId"), QStringLiteral("tool-1")},
        {QStringLiteral("toolName"), QStringLiteral("Bash")},
        {QStringLiteral("toolInput"),
         QJsonObject {{QStringLiteral("command"), summary}}},
        {QStringLiteral("createdAtMs"), 1'700'000'000'000.0},
        {QStringLiteral("deadlineAtMs"), 1'800'000'000'000.0},
        {QStringLiteral("risk"), risk},
        {QStringLiteral("decisionPhase"), QStringLiteral("actionable")},
    };
}

QByteArray snapshot(
    const quint64 generation,
    const QJsonArray& requests)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.pendingPermissionsSnapshot")},
        {QStringLiteral("generation"), static_cast<qint64>(generation)},
        {QStringLiteral("requests"), requests},
    }).toJson(QJsonDocument::Compact);
}

struct Fixture {
    QTemporaryDir directory;
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::DesktopSettings settings;
    kodosi::PendingPermissionsModel permissions;
    FakeNotificationDriver driver;
    bool active = false;
    kodosi::ApprovalNotifications notifications;

    Fixture()
        : settings(std::make_unique<QSettings>(
              directory.filePath(QStringLiteral("settings.ini")),
              QSettings::IniFormat))
        , permissions(dispatcher, sessionsModel)
        , notifications(
              permissions,
              sessionsModel,
              settings,
              driver,
              [this] { return active; })
    {
        Q_ASSERT(directory.isValid());
        const auto authEvent = auth();
        sessionsModel.ingestAuthEvent(authEvent);
        sessionsModel.ingestSessionEvent(sessions());
        permissions.ingestAuthEvent(authEvent);
        dispatcher.commands.clear();
    }

    [[nodiscard]] QString token() const
    {
        return permissions
            .data(
                permissions.index(0),
                kodosi::PendingPermissionsModel::IdentityTokenRole)
            .toString();
    }

    void disableAlerts()
    {
        const auto values = kodosi::DesktopSettings::defaultValues();
        QVERIFY(settings.apply(
            values.fontFamily,
            values.fontSize,
            static_cast<int>(values.cursorStyle),
            values.lineHeight,
            values.scrollbackLines,
            values.cursorBlink,
            false,
            {}));
    }
};

} // namespace

void ApprovalNotificationsTest::postsRiskGatedNotificationWhenInactive()
{
    Fixture fixture;
    fixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));

    QCOMPARE(fixture.driver.posted.size(), 1);
    const auto& notification = fixture.driver.posted.constFirst();
    QCOMPARE(
        notification.key,
        QStringLiteral("approval:") + fixture.token());
    QCOMPARE(notification.title, QStringLiteral("Bash \u2014 Session"));
    QCOMPARE(notification.body, QStringLiteral("ls -la"));
    QCOMPARE(
        notification.actions,
        QStringList({
            QStringLiteral("default"),
            QStringLiteral("Open Kodosi"),
            QStringLiteral("approve"),
            QStringLiteral("Approve"),
            QStringLiteral("deny"),
            QStringLiteral("Deny"),
        }));
}

void ApprovalNotificationsTest::skipsNotificationsWhenActiveOrDisabled()
{
    Fixture activeFixture;
    activeFixture.active = true;
    activeFixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));
    QVERIFY(activeFixture.driver.posted.isEmpty());

    Fixture disabledFixture;
    disabledFixture.disableAlerts();
    disabledFixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));
    QVERIFY(disabledFixture.driver.posted.isEmpty());
}

void ApprovalNotificationsTest::withdrawsReplacedAndResolvedRequests()
{
    Fixture fixture;
    fixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));
    const auto firstToken = fixture.token();
    const auto firstKey = fixture.driver.posted.constFirst().key;
    fixture.permissions.ingestAgentIntelEvent(snapshot(
        2,
        QJsonArray {request(8)}));
    const auto replacementToken = fixture.token();
    const auto replacementKey = fixture.driver.posted.constLast().key;

    QVERIFY(firstToken != replacementToken);
    QCOMPARE(fixture.driver.posted.size(), 2);
    QCOMPARE(fixture.driver.withdrawn, QVector<QString> {firstKey});

    fixture.permissions.ingestAgentIntelEvent(snapshot(3, {}));
    QCOMPARE(
        fixture.driver.withdrawn,
        QVector<QString>({firstKey, replacementKey}));
}

void ApprovalNotificationsTest::revalidatesEveryNativeAction()
{
    Fixture fixture;
    fixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));
    const auto safeKey = fixture.driver.posted.constFirst().key;
    fixture.driver.invoke(
        safeKey,
        QStringLiteral("approve"));
    QCOMPARE(fixture.dispatcher.commands.size(), 1);
    QCOMPARE(
        fixture.dispatcher.commands.constFirst()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("agent.intel.allowPendingPermissionRequest"));
    QCOMPARE(fixture.driver.withdrawn.constLast(), safeKey);

    Fixture destructiveFixture;
    destructiveFixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(8, QStringLiteral("destructive"))}));
    const auto destructiveKey =
        destructiveFixture.driver.posted.constFirst().key;
    QVERIFY(!destructiveFixture.driver.posted.constFirst().actions.contains(
        QStringLiteral("approve")));
    destructiveFixture.driver.invoke(
        destructiveKey,
        QStringLiteral("approve"));
    QVERIFY(destructiveFixture.dispatcher.commands.isEmpty());
    destructiveFixture.driver.invoke(
        destructiveKey,
        QStringLiteral("deny"));
    QCOMPARE(destructiveFixture.dispatcher.commands.size(), 1);
    QCOMPARE(
        destructiveFixture.dispatcher.commands.constFirst()
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("agent.intel.denyPendingPermissionRequest"));

    destructiveFixture.permissions.ingestAgentIntelEvent(snapshot(
        2,
        QJsonArray {request(9, QStringLiteral("safe"))}));
    destructiveFixture.dispatcher.commands.clear();
    destructiveFixture.driver.invoke(
        destructiveKey,
        QStringLiteral("deny"));
    QVERIFY(destructiveFixture.dispatcher.commands.isEmpty());
}

void ApprovalNotificationsTest::opensOnlyAnExactCurrentReview()
{
    Fixture fixture;
    QSignalSpy reviews(
        &fixture.notifications,
        &kodosi::ApprovalNotifications::reviewRequested);
    fixture.permissions.ingestAgentIntelEvent(snapshot(
        1,
        QJsonArray {request(7)}));
    const auto token = fixture.token();
    const auto key = fixture.driver.posted.constFirst().key;

    fixture.driver.invoke(
        QStringLiteral("terminal:other"),
        QStringLiteral("default"),
        QStringLiteral("other-token"));
    QVERIFY(fixture.driver.withdrawn.isEmpty());
    fixture.driver.invoke(
        QStringLiteral("approval:stale"),
        QStringLiteral("default"),
        QStringLiteral("stale-token"));
    QVERIFY(reviews.isEmpty());
    fixture.driver.invoke(
        key,
        QStringLiteral("default"),
        QStringLiteral("activation-token"));
    QCOMPARE(reviews.size(), 1);
    QCOMPARE(reviews.constFirst().at(0).toString(), token);
    QCOMPARE(
        reviews.constFirst().at(1).toString(),
        QStringLiteral("session-1"));
    QCOMPARE(
        reviews.constFirst().at(2).toString(),
        QStringLiteral("activation-token"));
}

void ApprovalNotificationsTest::protectsSensitiveAndUntrustedBodyText()
{
    QCOMPARE(
        kodosi::ApprovalNotifications::notificationBody(
            QStringLiteral("credential"),
            QStringLiteral("secret")),
        QStringLiteral("Open Kodosi to review this request."));
    QCOMPARE(
        kodosi::ApprovalNotifications::notificationBody(
            QStringLiteral("unknown"),
            QStringLiteral("unknown command")),
        QStringLiteral("Open Kodosi to review this request."));
    QCOMPARE(
        kodosi::ApprovalNotifications::notificationBody(
            QStringLiteral("destructive"),
            QStringLiteral("<b>remove</b>\nnow")),
        QStringLiteral("<b>remove</b> now"));
}

QTEST_GUILESS_MAIN(ApprovalNotificationsTest)

#include "tst_approval_notifications.moc"
