#include "attention/TerminalNotifications.hpp"

#include <QByteArray>
#include <QSignalSpy>
#include <QtTest/QTest>

class FakeTerminalNotificationDriver final
    : public kodosi::DesktopNotificationDriver {
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

    void close(const QString& key)
    {
        emit notificationClosed(key);
    }
};

class TerminalNotificationsTest final : public QObject {
    Q_OBJECT

private slots:
    void postsAndActivatesCurrentSession();
    void rejectsStaleSessionIncarnations();
    void routesOnlyTerminalNotificationKeys();
    void boundsTrackedNotifications();
};

namespace {

QByteArray auth()
{
    return QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"account","accountEpoch":1})");
}

QByteArray sessions(const QString& incarnation)
{
    return QStringLiteral(
        R"({"authority":"accountContext","accountUserId":"account","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"session-1","incarnationId":"%1","name":"Session","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"approve"}]})")
        .arg(incarnation)
        .toUtf8();
}

struct Fixture {
    kodosi::SessionCatalogModel sessionsModel;
    FakeTerminalNotificationDriver driver;
    kodosi::TerminalNotifications notifications {
        sessionsModel,
        driver,
    };

    Fixture()
    {
        sessionsModel.ingestAuthEvent(auth());
        sessionsModel.ingestSessionEvent(
            sessions(QStringLiteral("inc-1")));
    }

    void post(
        const QString& incarnation = QStringLiteral("inc-1"),
        const QString& title = QStringLiteral("Build complete"),
        const QString& body = QStringLiteral("All checks passed"))
    {
        notifications.receiveTerminalNotification({
            .sessionId = QStringLiteral("session-1"),
            .runtimeIncarnationId = incarnation,
            .title = title,
            .body = body,
        });
    }
};

} // namespace

void TerminalNotificationsTest::postsAndActivatesCurrentSession()
{
    Fixture fixture;
    QSignalSpy requests(
        &fixture.notifications,
        &kodosi::TerminalNotifications::sessionRequested);
    fixture.post();

    QCOMPARE(fixture.driver.posted.size(), 1);
    const auto notification = fixture.driver.posted.constFirst();
    QVERIFY(notification.key.startsWith(QStringLiteral("terminal:")));
    QCOMPARE(notification.title, QStringLiteral("Build complete"));
    QCOMPARE(notification.body, QStringLiteral("All checks passed"));
    QCOMPARE(
        notification.actions,
        QStringList({
            QStringLiteral("default"),
            QStringLiteral("Open Kodosi"),
        }));

    fixture.driver.invoke(
        notification.key,
        QStringLiteral("default"),
        QStringLiteral("activation-token"));
    QCOMPARE(requests.size(), 1);
    QCOMPARE(
        requests.constFirst().at(0).toString(),
        QStringLiteral("session-1"));
    QCOMPARE(
        requests.constFirst().at(1).toString(),
        QStringLiteral("activation-token"));
    QCOMPARE(fixture.driver.withdrawn.constLast(), notification.key);
}

void TerminalNotificationsTest::rejectsStaleSessionIncarnations()
{
    Fixture fixture;
    fixture.post(QStringLiteral("stale"));
    QVERIFY(fixture.driver.posted.isEmpty());

    fixture.post();
    const auto key = fixture.driver.posted.constFirst().key;
    fixture.sessionsModel.ingestSessionEvent(
        sessions(QStringLiteral("inc-2")));
    QVERIFY(fixture.driver.withdrawn.contains(key));

    QSignalSpy requests(
        &fixture.notifications,
        &kodosi::TerminalNotifications::sessionRequested);
    fixture.driver.invoke(
        key,
        QStringLiteral("default"));
    QVERIFY(requests.isEmpty());
}

void TerminalNotificationsTest::routesOnlyTerminalNotificationKeys()
{
    Fixture fixture;
    fixture.post();
    const auto key = fixture.driver.posted.constFirst().key;
    fixture.driver.invoke(
        QStringLiteral("approval:opaque"),
        QStringLiteral("default"));
    fixture.driver.invoke(key, QStringLiteral("approve"));
    QVERIFY(fixture.driver.withdrawn.isEmpty());
}

void TerminalNotificationsTest::boundsTrackedNotifications()
{
    Fixture fixture;
    for (auto index = 0; index < 257; ++index) {
        fixture.post(
            QStringLiteral("inc-1"),
            QStringLiteral("Terminal"),
            QString::number(index));
    }
    QCOMPARE(fixture.driver.posted.size(), 257);
    QCOMPARE(fixture.driver.withdrawn.size(), 1);
    QCOMPARE(
        fixture.driver.withdrawn.constFirst(),
        fixture.driver.posted.constFirst().key);

    fixture.driver.close(fixture.driver.posted.at(1).key);
    fixture.post();
    QCOMPARE(fixture.driver.withdrawn.size(), 1);
}

QTEST_GUILESS_MAIN(TerminalNotificationsTest)

#include "tst_terminal_notifications.moc"
